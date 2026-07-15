#include "BLEScanner.h"

#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

#include "esp_timer.h"
#include "freertos/ringbuf.h"
#include "ringbuffer.hpp"

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>

#include "BTHomeDecoder.h"
#include "custom_decoder.hpp"
#include "decoder.h"
#include "deviceconfig.hpp"

// ---------------------------------------------------------------------------
// Timing helper (replaces fmicro.h dependency)
// ---------------------------------------------------------------------------
static inline float fseconds() {
  return ((float)esp_timer_get_time()) * 1.0e-6f;
}

// ---------------------------------------------------------------------------
// Hex conversion helpers
// ---------------------------------------------------------------------------
static bool hexStringToVector(const String &hexStr,
                              std::vector<uint8_t> &buffer) {
  size_t len = hexStr.length();
  if (len & 1)
    return false;

  buffer.clear();
  if (len == 0)
    return true;

  buffer.resize(len / 2);
  uint8_t *out = buffer.data();
  const char *in = hexStr.c_str();

  for (size_t i = 0; i < len; i += 2) {
    uint8_t val = 0;
    for (int j = 0; j < 2; j++) {
      uint8_t nibble;
      char c = in[i + j];
      if (c >= '0' && c <= '9')
        nibble = c - '0';
      else if (c >= 'A' && c <= 'F')
        nibble = c - 'A' + 10;
      else if (c >= 'a' && c <= 'f')
        nibble = c - 'a' + 10;
      else {
        buffer.clear();
        return false;
      }
      val = (val << 4) | nibble;
    }
    *out++ = val;
  }
  return true;
}

static void bytesToHexString(const uint8_t *data, size_t len, String &hexStr) {
  static const char HEX_CHARS[] = "0123456789ABCDEF";
  hexStr = "";
  if (len == 0)
    return;
  hexStr.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    hexStr += HEX_CHARS[data[i] >> 4];
    hexStr += HEX_CHARS[data[i] & 0x0F];
  }
}

// // Non-static — also used by BTHomeDecoder.cpp
bool stringToHexString(const String &str, String &hexStr) {
  bytesToHexString((const uint8_t *)str.c_str(), str.length(), hexStr);
  return true;
}

// ---------------------------------------------------------------------------
// BLEScanner::Impl — hidden state
// ---------------------------------------------------------------------------
struct BLEScanner::Impl {
  espidf::RingBuffer *queue = nullptr;
  BLEScan *pBLEScan = nullptr;
  BTHomeDecoder bthDecoder;
  const char *bthKey = "";

  uint32_t scanTimeMs = 15000;
  uint16_t scanInterval = 100;
  uint16_t scanWindow = 99;
  bool activeScan = false;

  uint32_t queueFull = 0;
  uint32_t acquireFail = 0;
  uint32_t received = 0;
  uint32_t decoded = 0;
};

// Singleton storage — the Impl pointer lives on the single instance.
static BLEScanner::Impl *s_impl = nullptr;

static TheengsDecoder decoder;

static bool decodeBTHome(JsonObject BLEdata, JsonDocument &json,
                         BTHomeDecoder &decoder, const char *key) {
  // Guard: a null id would throw std::logic_error from the implicit
  // JsonVariant -> std::string conversion and abort the firmware.
  const char *id = BLEdata["id"].as<const char *>();
  if (!id)
    return false;

  std::vector<uint8_t> sd;
  if (!hexStringToVector(BLEdata["servicedata"], sd))
    return false;

  BTHomeDecodeResult bthRes =
      decoder.parseBTHomeV2(std::string(sd.begin(), sd.end()), id, key);

  if (bthRes.isBTHome && bthRes.decryptionSucceeded) {
    JsonObject root = json.to<JsonObject>();
    root["bthome_version"] = bthRes.bthomeVersion;
    JsonArray measArr = root["measurements"].to<JsonArray>();

    for (auto &m : bthRes.measurements) {
      JsonObject obj = measArr.add<JsonObject>();
      obj["object_id"] = m.objectID;
      obj["name"] = m.name;
      obj["value"] = m.value;
      obj["unit"] = m.unit;
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Advertisement dedup — MAC -> (raw payload, reception time). Touched only
// from the BLE scan task, so no locking. Controlled by ble_dedup_enabled /
// ble_dedup_age (live-updated from the web UI).
// ---------------------------------------------------------------------------
struct DedupEntry {
  std::vector<uint8_t> payload;
  int64_t seen_us;
};
static std::map<std::string, DedupEntry> dedup_map;
static int64_t dedup_last_sweep_us = 0;

// Returns true if this advertisement is a young duplicate and should be
// dropped. Also maintains the map (insert/update, periodic age-out).
static bool dedupDrop(const std::string &mac, const uint8_t *payload,
                      size_t payloadLen) {
  if (!ble_dedup_enabled) {
    if (!dedup_map.empty())
      dedup_map.clear();
    return false;
  }

  int64_t now = esp_timer_get_time();
  int64_t age_us = (int64_t)ble_dedup_age * 1000000LL;

  if (now - dedup_last_sweep_us > age_us) {
    dedup_last_sweep_us = now;
    for (auto it = dedup_map.begin(); it != dedup_map.end();) {
      if (now - it->second.seen_us > age_us)
        it = dedup_map.erase(it);
      else
        ++it;
    }
  }

  auto it = dedup_map.find(mac);
  if (it != dedup_map.end() && now - it->second.seen_us < age_us &&
      it->second.payload.size() == payloadLen &&
      memcmp(it->second.payload.data(), payload, payloadLen) == 0) {
    return true;
  }

  DedupEntry &e = dedup_map[mac];
  e.payload.assign(payload, payload + payloadLen);
  e.seen_us = now;
  return false;
}

// ---------------------------------------------------------------------------
// BLE scan callback — enqueues raw advertisement data as MsgPack
// ---------------------------------------------------------------------------
class ScanCallback : public BLEAdvertisedDeviceCallbacks {
  String convertServiceData(const String &deviceServiceData) {
    String out;
    out.reserve(2 * deviceServiceData.length());
    char byte[3];
    for (size_t i = 0; i < deviceServiceData.length(); i++) {
      snprintf(byte, sizeof(byte), "%.2x", (unsigned char)deviceServiceData[i]);
      out += byte;
    }
    return out;
  }

  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    // Serial.printf("Advertised Device: %s \n",
    // advertisedDevice.toString().c_str());
    String mac_adress = advertisedDevice.getAddress().toString().c_str();
    mac_adress.toLowerCase();

    if (dedupDrop(mac_adress.c_str(), advertisedDevice.getPayload(),
                  advertisedDevice.getPayloadLength()))
      return;

    JsonDocument doc;
    JsonObject BLEdata = doc.to<JsonObject>();
    BLEdata["id"] = (char *)mac_adress.c_str();

    if (advertisedDevice.haveName())
      BLEdata["name"] = (char *)advertisedDevice.getName().c_str();

    if (advertisedDevice.haveManufacturerData()) {
      char *manufacturerdata = BLEUtils::buildHexData(
          NULL, (uint8_t *)advertisedDevice.getManufacturerData().c_str(),
          advertisedDevice.getManufacturerData().length());
      BLEdata["manufacturerdata"] = manufacturerdata;
      free(manufacturerdata);
    }

    BLEdata["rssi"] = (int)advertisedDevice.getRSSI();

    if (advertisedDevice.haveTXPower())
      BLEdata["txpower"] = (int8_t)advertisedDevice.getTXPower();

    if (advertisedDevice.haveServiceData()) {
      int serviceDataCount = advertisedDevice.getServiceDataCount();
      for (int j = 0; j < serviceDataCount; j++) {
        BLEdata["servicedata"] =
            convertServiceData(advertisedDevice.getServiceData(j));
        BLEdata["servicedatauuid"] =
            advertisedDevice.getServiceDataUUID(j).toString();
      }
    }
    BLEdata["time"] = fseconds();
    void *ble_adv = nullptr;
    size_t total = measureMsgPack(BLEdata);
    if (s_impl->queue->send_acquire((void **)&ble_adv, total, 0) != pdTRUE) {
      s_impl->acquireFail++;
      return;
    }
    size_t n = serializeMsgPack(BLEdata, ble_adv, total);
    if (n != total) {
      log_e("serializeMsgPack: expected %u got %u", total, n);
    } else {
      if (s_impl->queue->send_complete(ble_adv) != pdTRUE) {
        s_impl->queueFull++;
      }
    }
  }
  // void onResult(BLEAdvertisedDevice advertisedDevice) override {
  //     if (!s_impl || !s_impl->queue)
  //         return;

  //     JsonDocument doc;
  //     JsonObject BLEdata = doc.to<JsonObject>();

  //     String mac = advertisedDevice.getAddress().toString();
  //     mac.toUpperCase();
  //     BLEdata["mac"] = (char *)mac.c_str();
  //     BLEdata["rssi"] = (int)advertisedDevice.getRSSI();

  //     if (advertisedDevice.haveName())
  //         BLEdata["name"] = (char *)advertisedDevice.getName().c_str();

  //     if (advertisedDevice.haveManufacturerData()) {
  //         String hexData;
  //         stringToHexString(advertisedDevice.getManufacturerData(), hexData);
  //         BLEdata["mfd"] = hexData;
  //     }

  //     if (advertisedDevice.haveServiceUUID())
  //         BLEdata["svcuuid"] = (char
  //         *)advertisedDevice.getServiceUUID().toString().c_str();

  //     int sdCount = advertisedDevice.getServiceDataUUIDCount();
  //     if (sdCount > 0) {
  //         int idx = sdCount - 1;
  //         BLEdata["svduuid"] = (char
  //         *)advertisedDevice.getServiceDataUUID(idx).toString().c_str();
  //         String hexData;
  //         stringToHexString(advertisedDevice.getServiceData(idx), hexData);
  //         BLEdata["sd"] = hexData;
  //     }

  //     if (advertisedDevice.haveTXPower())
  //         BLEdata["txpwr"] = (int8_t)advertisedDevice.getTXPower();

  //     BLEdata["time"] = fseconds();

  //     void *ble_adv = nullptr;
  //     size_t total = measureMsgPack(BLEdata);
  //     if (s_impl->queue->send_acquire((void **)&ble_adv, total, 0) != pdTRUE)
  //     {
  //         s_impl->acquireFail++;
  //         return;
  //     }

  //     size_t n = serializeMsgPack(BLEdata, ble_adv, total);
  //     if (n != total) {
  //         log_e("serializeMsgPack: expected %u got %u", total, n);
  //     } else {
  //         if (s_impl->queue->send_complete(ble_adv) != pdTRUE) {
  //             s_impl->queueFull++;
  //         } else {
  //             s_impl->queue->update_high_watermark();
  //         }
  //     }
  // }
};

// ---------------------------------------------------------------------------
// Scan task (runs forever on its own RTOS task)
// ---------------------------------------------------------------------------
static void scanTask(void *param) {
  auto *impl = static_cast<BLEScanner::Impl *>(param);

  BLEDevice::init("");
  impl->pBLEScan = BLEDevice::getScan();
  impl->pBLEScan->setAdvertisedDeviceCallbacks(new ScanCallback(), true, true);
  impl->pBLEScan->setActiveScan(impl->activeScan);
  impl->pBLEScan->setInterval(impl->scanInterval);
  impl->pBLEScan->setWindow(impl->scanWindow);

  while (true) {
    BLEScanResults *foundDevices =
        impl->pBLEScan->start(impl->scanTimeMs / 1000, false);
    log_i("Devices found: %d", foundDevices->getCount());
    impl->pBLEScan->clearResults();
    delay(1);
  }
}

// ---------------------------------------------------------------------------
// BLEScanner public API
// ---------------------------------------------------------------------------
BLEScanner &BLEScanner::instance() {
  static BLEScanner s;
  return s;
}

void BLEScanner::setBTHomeKey(const char *hexKey) {
  if (!_impl) {
    _impl = new Impl();
    s_impl = _impl;
  }
  _impl->bthKey = hexKey ? hexKey : "";
}

void BLEScanner::setActiveScan(bool active) {
  if (!_impl) {
    _impl = new Impl();
    s_impl = _impl;
  }
  _impl->activeScan = active;
}

BLEScanner::Stats BLEScanner::stats() const {
  Stats s = {};
  if (!_impl || !_impl->queue)
    return s;
  s.hwmBytes = _impl->queue->get_high_watermark();
  s.totalBytes = _impl->queue->get_total_size();
  s.hwmPercent =
      s.totalBytes > 0 ? (uint8_t)((s.hwmBytes * 100) / s.totalBytes) : 0;
  s.queueFull = _impl->queueFull;
  s.acquireFail = _impl->acquireFail;
  s.received = _impl->received;
  s.decoded = _impl->decoded;
  return s;
}

void BLEScanner::begin(size_t ringBufSize, uint32_t scanTimeMs,
                       uint16_t scanInterval, uint16_t scanWindow,
                       uint32_t taskStackSize, UBaseType_t taskPriority,
                       UBaseType_t ringBufCap) {
  if (_started)
    return;
  _started = true;

  if (!_impl) {
    _impl = new Impl();
    s_impl = _impl;
  }

  _impl->scanTimeMs = scanTimeMs;
  _impl->scanInterval = scanInterval;
  _impl->scanWindow = scanWindow;

  _impl->queue = new espidf::RingBuffer();
  _impl->queue->create(ringBufSize, RINGBUF_TYPE_NOSPLIT, ringBufCap);

  xTaskCreate(scanTask, "ble_scan", taskStackSize, _impl, taskPriority,
              nullptr);
}

bool BLEScanner::deliver(JsonDocument &inDoc, JsonDocument &outDoc) {
  auto BLEdata = inDoc.as<JsonObject>();

  switch (ble_decoder_mode) {
  case DeviceConfig::BLE_DECODER_THEENGS:
    if (decoder.decodeBLEJson(BLEdata) >= 0) {
      BLEdata.remove("manufacturerdata");
      BLEdata.remove("servicedata");
      BLEdata.remove("servicedatauuid");
      BLEdata.remove("type");
      BLEdata.remove("cidc");
      BLEdata.remove("acts");
      BLEdata.remove("cont");
      BLEdata.remove("track");
      outDoc.set(BLEdata);
      return true;
    }
    return false;

  case DeviceConfig::BLE_DECODER_BTHOME:
    if (BLEdata["servicedatauuid"].is<const char *>() &&
        String(BLEdata["servicedatauuid"]).indexOf("fcd2") != -1) {
      return decodeBTHome(BLEdata, outDoc, _impl->bthDecoder, _impl->bthKey);
    }
    return false;

  case DeviceConfig::BLE_DECODER_CUSTOM:
    return custom_decode(BLEdata, outDoc);

  default: // BLE_DECODER_NONE — raw only, no decode
    return false;
  }
}

bool BLEScanner::process(JsonDocument &doc, char *mac, size_t macLen) {
  if (!_impl || !_impl->queue)
    return false;

  size_t size = 0;
  void *buffer = _impl->queue->receive(&size, 0);
  if (buffer == nullptr)
    return false;

  JsonDocument rawDoc;
  DeserializationError e = deserializeMsgPack(rawDoc, buffer, size);
  _impl->queue->return_item(buffer);
  _impl->received++;

  if (e) {
    log_e("deserializeMsgPack: %s", e.c_str());
    return false;
  }
  // Guard: a missing id would crash the std::string conversions below.
  const char *idStr = rawDoc["id"].as<const char *>();
  if (!idStr) {
    log_w("advertisement without id, dropped");
    return false;
  }

  // Decode
  uint8_t mode = ble_decoder_mode;
  JsonDocument decodedDoc;
  bool decoded = false;
  if (mode != DeviceConfig::BLE_DECODER_NONE)
    decoded = deliver(rawDoc, decodedDoc);
  if (decoded)
    _impl->decoded++;

  // Undecoded: publish raw in NONE mode or when retain is on, else drop
  if (!decoded && mode != DeviceConfig::BLE_DECODER_NONE &&
      !ble_retain_undecoded)
    return false;

  JsonDocument &outDoc = decoded ? decodedDoc : rawDoc;

  // Merge common metadata into decoded results (raw already has it all)
  if (decoded) {
    outDoc["id"] = rawDoc["id"];
    outDoc["time"] = rawDoc["time"];
    outDoc["rssi"] = rawDoc["rssi"];
    if (rawDoc["name"].is<const char *>())
      outDoc["name"] = rawDoc["name"];
    if (rawDoc["txpower"].is<JsonVariant>())
      outDoc["txpower"] = rawDoc["txpower"];
  }

  // Extract MAC (strip colons, already lowercase from onResult)
  String macStr = idStr;
  macStr.replace(":", "");
  macStr.toLowerCase();
  size_t copyLen = macStr.length();
  if (copyLen >= macLen)
    copyLen = macLen - 1;
  memcpy(mac, macStr.c_str(), copyLen);
  mac[copyLen] = '\0';

  // Move result into caller's doc
  doc.set(outDoc);
  return true;
}
