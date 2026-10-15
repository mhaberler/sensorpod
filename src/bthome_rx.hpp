#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

/// BTHome v2 receive path on top of bthome-cpp: per-sender replay state,
/// AES-CCM decryption, JSON rendering.
namespace bthome_rx {

struct Stats {
  uint32_t replay = 0;     ///< Encrypted adverts whose counter did not advance
  uint32_t authFail = 0;   ///< MIC mismatch: wrong key or tampered
  uint32_t incomplete = 0; ///< Decode stopped on an unknown id / truncation
};

/// Decode one BTHome service-data payload (service UUID already stripped).
/// mac: sender address in display order, any separator ("AA:BB:CC:DD:EE:FF").
/// keyHex: 32-char hex bindkey, or "" / nullptr to refuse encrypted adverts.
/// On success json holds {bthome_version, measurements[], incomplete?}.
bool decode(const uint8_t *payload, size_t len, const char *mac,
            const char *keyHex, JsonDocument &json, Stats &stats);

} // namespace bthome_rx
