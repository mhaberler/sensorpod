/// Custom BLE advertisement decoder — Mikrotik TG-BT5 tags and Qingping
/// sensors. Ported from tab5-lvgl9.4 BLEScanner.cpp decodeMikrotik and
/// sensor-ble devices/qingping.js.

#include "custom_decoder.hpp"

#include <Arduino.h>
#include <vector>

// ---------------------------------------------------------------------------
// Byte helpers
// ---------------------------------------------------------------------------
static inline int16_t getInt16LE(const std::vector<uint8_t> &data, int index) {
  return (int16_t)((data[index]) | (data[index + 1] << 8));
}

static inline uint16_t getUint16LE(const std::vector<uint8_t> &data,
                                   int index) {
  return (uint16_t)((data[index]) | (data[index + 1] << 8));
}

static inline uint32_t getUint32LE(const std::vector<uint8_t> &data,
                                   int index) {
  return (uint32_t)((data[index]) | (data[index + 1] << 8) |
                    (data[index + 2] << 16) | (data[index + 3] << 24));
}

static inline uint8_t getUint8(const std::vector<uint8_t> &data, int index) {
  return data[index];
}

static inline float convert_8_8_to_float(const std::vector<uint8_t> &data,
                                         int index) {
  // 8.8 to float converter
  auto frac = getUint8(data, index);
  auto base = getUint8(data, index + 1);
  if (frac == 0xFF && base == 0xFF) {
    return 0.0f;
  } else {
    return (float)base + ((float)frac / 256.0f);
  }
}

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

// ---------------------------------------------------------------------------
// Mikrotik TG-BT5 (company ID 0x094F), no encryption
// ---------------------------------------------------------------------------
static bool decodeMikrotik(const std::vector<uint8_t> &data,
                           JsonDocument &json) {
  if (data.size() != 20)
    return false;
  int16_t t = getInt16LE(data, 12);
  if (t != -32768) { // 0x8000 -> temp is unsupported (indoor)
    json["dev"] = "Mikrotik TG-BT5-OUT";
    json["tempc"] = t / 256.0;
  } else {
    json["dev"] = "Mikrotik TG-BT5-IN";
  }
  json["version"] = getUint8(data, 2);
  auto user = getUint8(data, 3);
  if (user & 0x01) {
    json["encrypted"] = true;
  } else {
    json["salt"] = getUint16LE(data, 4);
    json["accx"] = convert_8_8_to_float(data, 6);
    json["accy"] = convert_8_8_to_float(data, 8);
    json["accz"] = convert_8_8_to_float(data, 10);

    // uptime (4 bytes, little-endian)
    json["uptime"] = getUint32LE(data, 14);

    // flags (1 byte)
    uint8_t flags = getUint8(data, 18);
    if (flags & 1) {
      json["reed_switch"] = true;
    }
    if (flags & 2) {
      json["accel_tilt"] = true;
    }
    if (flags & 4) {
      json["accel_drop"] = true;
    }
    if (flags & 8) {
      json["impact_x"] = true;
    }
    if (flags & 16) {
      json["impact_y"] = true;
    }
    if (flags & 32) {
      json["impact_z"] = true;
    }
    // battery (1 byte)
    uint8_t batt = getUint8(data, 19);
    json["batt"] = batt;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Qingping sensors (service data UUID 0xFDCD), TLV records from offset 8
// ---------------------------------------------------------------------------
static const char *qingpingDeviceType(uint8_t id) {
  switch (id) {
  case 0x01:
  case 0x07:
    return "CGG1";
  case 0x04:
    return "CGH1";
  case 0x09:
    return "CGP1W";
  case 0x0C:
    return "CGD1";
  case 0x0E:
    return "CGDN1";
  case 0x12:
    return "CGPR1";
  default:
    return nullptr;
  }
}

static bool decodeQingping(const std::vector<uint8_t> &buf,
                           JsonDocument &json) {
  if (buf.size() < 8)
    return false;

  uint8_t deviceId = buf[1];
  const char *dev = qingpingDeviceType(deviceId);
  if (!dev)
    return false;

  json["dev"] = dev;

  bool haveOpen = false;
  size_t off = 8;
  while (off + 2 <= buf.size()) {
    uint8_t id = buf[off];
    uint8_t size = buf[off + 1];
    off += 2;
    if (off + size > buf.size())
      break;
    if (id == 0x04 && size == 1) {
      json["open_dimensionless"] = buf[off] == 0 ? 1 : 0;
      haveOpen = true;
    } else if (id == 0x01 && size == 4) {
      json["temperature_C"] = getInt16LE(buf, off) / 10.0;
      json["humidity_percent"] = getUint16LE(buf, off + 2) / 10.0;
    } else if (id == 0x02 && size == 1) {
      json["battery_percent"] = buf[off];
    } else if (id == 0x07 && size == 2) {
      json["pressure_hPa"] = getUint16LE(buf, off) / 10.0;
    } else if (id == 0x08 && size == 4) {
      uint8_t motion = buf[off];
      json["motion_dimensionless"] = motion;
      json["illuminance_lux"] = getUint16LE(buf, off + 1) + buf[off + 3];
      if (motion)
        json["motionTimer_dimensionless"] = 1;
    } else if (id == 0x09 && size == 4) {
      json["illuminance_lux"] = getUint32LE(buf, off);
    } else if (id == 0x11 && size == 1) {
      json["light_dimensionless"] = buf[off];
    } else if (id == 0x12 && size == 4) {
      json["pm2_5_ugm3"] = getUint16LE(buf, off);
      json["pm10_ugm3"] = getUint16LE(buf, off + 2);
    } else if (id == 0x13 && size == 2) {
      json["co2_ppm"] = getUint16LE(buf, off);
    }
    off += size;
  }

  // CGH1 long format (17 bytes): contact state in last byte, 0=open, 1=closed
  if (deviceId == 0x04 && buf.size() == 17 && !haveOpen)
    json["open_dimensionless"] = buf[16] == 0 ? 1 : 0;

  // CGH1: device MAC embedded reversed at bytes 2..7
  if (deviceId == 0x04) {
    char macbuf[18];
    snprintf(macbuf, sizeof(macbuf), "%02x:%02x:%02x:%02x:%02x:%02x", buf[7],
             buf[6], buf[5], buf[4], buf[3], buf[2]);
    json["macAddress"] = macbuf;
  }
  return true;
}

bool custom_decode(JsonObject BLEdata, JsonDocument &outDoc) {
  // Qingping: service data on UUID 0xFDCD
  if (BLEdata["servicedatauuid"].is<const char *>() &&
      String(BLEdata["servicedatauuid"]).indexOf("fdcd") != -1) {
    std::vector<uint8_t> sd;
    if (hexStringToVector(BLEdata["servicedata"], sd) &&
        decodeQingping(sd, outDoc))
      return true;
  }

  // Mikrotik: manufacturer data, dispatch on company ID
  if (!BLEdata["manufacturerdata"].is<const char *>())
    return false;

  std::vector<uint8_t> mfd;
  if (!hexStringToVector(BLEdata["manufacturerdata"], mfd) || mfd.size() < 2)
    return false;

  uint16_t mfid = mfd[1] << 8 | mfd[0];
  switch (mfid) {
  case 0x094F:
    return decodeMikrotik(mfd, outDoc);
  default:
    return false;
  }
}
