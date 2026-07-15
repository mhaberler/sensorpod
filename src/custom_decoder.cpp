/// Custom BLE advertisement decoder — Mikrotik TG-BT5 tags.
/// Ported from tab5-lvgl9.4 BLEScanner.cpp decodeMikrotik.

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

bool custom_decode(JsonObject BLEdata, JsonDocument &outDoc) {
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
