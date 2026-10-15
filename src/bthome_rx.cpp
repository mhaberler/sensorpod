#include "bthome_rx.hpp"

#include <ctype.h>
#include <map>

#include "bthome.h"
#include "bthome_crypto_psa.h"

namespace bthome_rx {

// ---------------------------------------------------------------------------
// Object id -> name/unit. Strings kept identical to the former BTHomeDecoder
// so the published JSON does not change.
// ---------------------------------------------------------------------------
static const char *objectName(uint8_t id) {
  switch (id) {
  case 0x00:
    return "packet_id";
  case 0x01:
    return "battery_percent";
  case 0x02:
    return "temperature";
  case 0x03:
    return "humidity";
  case 0x04:
    return "pressure";
  case 0x05:
    return "illuminance";
  case 0x0A:
    return "energy";
  case 0x0B:
    return "power";
  case 0x0C:
    return "battery_voltage";
  case 0x12:
    return "CO2";
  case 0x13:
    return "VOC";
  case 0x14:
    return "moisture";
  case 0x0F:
    return "generic_boolean";
  case 0x10:
    return "power_binary";
  case 0x11:
    return "opening";
  case 0x15:
    return "battery_low";
  case 0x16:
    return "battery_charging";
  case 0x17:
    return "carbon_monoxide";
  case 0x18:
    return "cold";
  case 0x19:
    return "connectivity";
  case 0x1A:
    return "door";
  case 0x1B:
    return "garage_door";
  case 0x1C:
    return "gas_detected";
  case 0x1D:
    return "heat";
  case 0x1E:
    return "light";
  case 0x1F:
    return "lock";
  case 0x20:
    return "moisture_binary";
  case 0x21:
    return "motion";
  case 0x22:
    return "moving";
  case 0x23:
    return "occupancy";
  case 0x24:
    return "plug";
  case 0x25:
    return "presence";
  case 0x26:
    return "problem";
  case 0x27:
    return "running";
  case 0x28:
    return "safety";
  case 0x29:
    return "smoke";
  case 0x2A:
    return "sound";
  case 0x2B:
    return "tamper";
  case 0x2C:
    return "vibration";
  case 0x2D:
    return "window";
  case 0x2E:
    return "humidity";
  case 0x2F:
    return "soil_moisture";
  case 0x3A:
    return "button";
  case 0x3C:
    return "dimmer";
  case 0x3F:
    return "rotation";
  case 0x40:
    return "distance_mm";
  case 0x41:
    return "distance_m";
  case 0x42:
    return "duration_sec";
  case 0x43:
    return "current_A";
  case 0x44:
    return "speed_mps";
  case 0x45:
    return "temperature_0.1C";
  case 0x46:
    return "UV_index";
  case 0x47:
    return "volume_liters";
  case 0x48:
    return "volume_milliliters";
  case 0x49:
    return "flow_rate";
  case 0x4A:
    return "voltage_V";
  case 0x4B:
    return "gas_m3";
  case 0x50:
    return "timestamp";
  case 0x53:
    return "text";
  case 0x54:
    return "raw";
  case 0x55:
    return "volume_storage";
  case 0x56:
    return "conductivity";
  case 0x57:
  case 0x58:
    return "temperature";
  case 0x59:
  case 0x5A:
  case 0x5B:
    return "count";
  case 0x5C:
    return "power";
  case 0x5D:
    return "current";
  case 0x5E:
    return "direction";
  case 0x5F:
    return "precipitation";
  case 0x60:
    return "channel";
  case 0x61:
    return "rotational_speed";
  case 0x62:
    return "speed";
  case 0x63:
    return "acceleration";
  case 0x65:
    return "settings_revision";
  case 0xF0:
    return "device_type_id";
  case 0xF1:
  case 0xF2:
    return "firmware_version";
  default:
    return "unknown";
  }
}

static const char *objectUnit(uint8_t id) {
  switch (id) {
  case 0x01:
  case 0x03:
  case 0x14:
  case 0x2E:
  case 0x2F:
    return "percent";
  case 0x02:
  case 0x08:
  case 0x45:
    return "°C";
  case 0x04:
    return "hPa";
  case 0x05:
    return "lux";
  case 0x06:
    return "kg";
  case 0x07:
    return "lb";
  case 0x0A:
  case 0x4D:
    return "kWh";
  case 0x0B:
    return "W";
  case 0x0C:
  case 0x4A:
    return "V";
  case 0x0D:
  case 0x0E:
  case 0x13:
    return "ug/m3";
  case 0x12:
    return "ppm";
  case 0x3F:
    return "°";
  case 0x40:
    return "mm";
  case 0x41:
    return "m";
  case 0x42:
    return "s";
  case 0x43:
    return "A";
  case 0x44:
  case 0x62:
    return "m/s";
  case 0x47:
  case 0x4E:
  case 0x4F:
    return "L";
  case 0x48:
    return "mL";
  case 0x49:
    return "m3/hr";
  case 0x4B:
  case 0x4C:
    return "m3";
  case 0x51:
  case 0x63:
    return "m/s²";
  case 0x52:
    return "°/s";
  case 0x2D:
    return "open";
  default:
    return "";
  }
}

// ---------------------------------------------------------------------------
// Per-sender decryptors (replay state lives in the Decryptor). RAM only,
// touched from the BLE scan task alone, so no locking.
// ---------------------------------------------------------------------------
static constexpr size_t kMaxSenders = 16;

struct Sender {
  BTHome::Decryptor dec{&BTHome::psa_ccm_decrypt_backend};
  uint32_t used = 0;
};
static std::map<uint64_t, Sender> senders;
static uint32_t useTick = 0;

static bool parseHex(const char *s, uint8_t *out, size_t n) {
  size_t got = 0;
  uint8_t cur = 0;
  bool high = true;
  for (; s && *s; s++) {
    if (!isxdigit((unsigned char)*s))
      continue; // tolerate ':' / '-' / spaces
    uint8_t nib =
        isdigit((unsigned char)*s) ? *s - '0' : (tolower(*s) - 'a' + 10);
    if (high) {
      cur = nib << 4;
    } else {
      if (got == n)
        return false;
      out[got++] = cur | nib;
    }
    high = !high;
  }
  return got == n && high;
}

static Sender *senderFor(const uint8_t (&mac)[6]) {
  uint64_t k = 0;
  for (uint8_t b : mac)
    k = (k << 8) | b;
  auto it = senders.find(k);
  if (it == senders.end()) {
    if (senders.size() >= kMaxSenders) {
      auto lru = senders.begin();
      for (auto i = senders.begin(); i != senders.end(); ++i)
        if (i->second.used < lru->second.used)
          lru = i;
      senders.erase(lru);
    }
    it = senders.emplace(k, Sender()).first;
  }
  it->second.used = ++useTick;
  return &it->second;
}

static void addMeasurement(JsonArray arr, const BTHome::Decoded &o) {
  JsonObject m = arr.add<JsonObject>();
  m["object_id"] = o.object_id;
  m["name"] = objectName(o.object_id);
  switch (o.kind) {
  case BTHome::ObjectKind::Sensor:
    m["value"] = o.value;
    break;
  case BTHome::ObjectKind::Binary:
    m["value"] = o.on() ? 1 : 0;
    break;
  case BTHome::ObjectKind::ButtonEvent:
  case BTHome::ObjectKind::CommandEvent:
    m["value"] = o.event();
    break;
  case BTHome::ObjectKind::DimmerEvent:
    m["value"] = o.event();
    m["steps"] = o.steps();
    break;
  default: // PacketId, DeviceTypeId, FirmwareVersion: exact integers
    m["value"] = o.raw;
    break;
  }
  m["unit"] = objectUnit(o.object_id);
}

bool decode(const uint8_t *payload, size_t len, const char *mac,
            const char *keyHex, JsonDocument &json, Stats &stats) {
  if (!payload || len < 1)
    return false;

  uint8_t plain[64];
  const uint8_t *body = payload;
  size_t bodyLen = len;

  if (payload[0] & BTHome::DeviceInfo::kEncryptedBit) {
    uint8_t key[BTHome::Encryptor::kKeyBytes];
    uint8_t macBytes[BTHome::Encryptor::kMacBytes];
    if (!keyHex || !parseHex(keyHex, key, sizeof(key)) ||
        !parseHex(mac, macBytes, sizeof(macBytes)))
      return false;
    Sender *s = senderFor(macBytes);
    s->dec.setKey(key);
    s->dec.setMac(macBytes);
    size_t plainLen = 0;
    switch (
        s->dec.decryptPayload(payload, len, plain, sizeof(plain), plainLen)) {
    case BTHome::DecryptStatus::Ok:
      break;
    case BTHome::DecryptStatus::Replay:
      stats.replay++;
      return false;
    case BTHome::DecryptStatus::AuthFailed:
      stats.authFail++;
      return false;
    default:
      return false;
    }
    body = plain;
    bodyLen = plainLen;
  }

  BTHome::Decoder dec = BTHome::Decoder::fromPayload(body, bodyLen);
  if (dec.status() != BTHome::DecodeStatus::Ok)
    return false; // bad header / not v2 / still encrypted

  JsonObject root = json.to<JsonObject>();
  root["bthome_version"] = dec.version();
  JsonArray arr = root["measurements"].to<JsonArray>();

  BTHome::Decoded o;
  while (dec.next(o)) {
    if (o.kind == BTHome::ObjectKind::Text || o.kind == BTHome::ObjectKind::Raw)
      continue;
    addMeasurement(arr, o);
  }
  if (dec.status() != BTHome::DecodeStatus::End) {
    root["incomplete"] = true;
    stats.incomplete++;
  }
  return true;
}

} // namespace bthome_rx
