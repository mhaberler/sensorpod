/// @file BLEScanner.h
/// @brief Singleton BLE advertisement scanner with runtime-selectable
/// decoders.
///
/// Scans for BLE advertisements in a dedicated FreeRTOS task and queues raw
/// data via a ring buffer. Optional dedup (ble_dedup_enabled/ble_dedup_age)
/// drops repeated identical raw payloads per MAC before enqueueing; the map
/// and its age-out sweep live on the scan task. The caller drains the queue
/// from the main loop by calling process(), which deserializes, decodes, and
/// returns a populated JsonDocument plus the device MAC.
///
/// The decode pipeline is selected at runtime via ble_decoder_mode
/// (deviceconfig.hpp, NVS-persisted, live-updated from the web UI):
///   - BLE_DECODER_THEENGS: TheengsDecoder device library
///   - BLE_DECODER_BTHOME:  BTHome v2 (optional AES key via setBTHomeKey())
///   - BLE_DECODER_CUSTOM:  custom_decoder.cpp (Mikrotik TG-BT5, Qingping)
///   - BLE_DECODER_NONE:    no decoding, everything returned raw
/// Undecoded advertisements are returned raw when ble_retain_undecoded is
/// set (or in NONE mode), otherwise dropped.
///
/// Usage:
/// @code
///   auto &scanner = BLEScanner::instance();
///   scanner.setActiveScan(false);           // optional, before begin()
///   scanner.setBTHomeKey("431d39c1...");     // optional, 32-char hex
///   scanner.begin();                        // starts RTOS scan task
///
///   // in loop():
///   JsonDocument doc;
///   char mac[16];
///   if (scanner.process(doc, mac, sizeof(mac))) {
///       // publish or handle doc + mac
///   }
/// @endcode

#pragma once
#include <cstddef>
#include <cstdint>

#define ARDUINOJSON_USE_LONG_LONG 1
#include "ArduinoJson.h"

class BLEScanner {
public:
  static BLEScanner &instance();

  BLEScanner(const BLEScanner &) = delete;
  BLEScanner &operator=(const BLEScanner &) = delete;

  /// Opaque implementation detail (defined in BLEScanner.cpp)
  struct Impl;

  /// Initialize and start the BLE scanning RTOS task.
  /// Idempotent — second call is a no-op.
  void begin(size_t ringBufSize = 2048, uint32_t scanTimeMs = 15000,
             uint16_t scanInterval = 100, uint16_t scanWindow = 99,
             uint32_t taskStackSize = 4096, UBaseType_t taskPriority = 1,
             UBaseType_t ringBufCap = MALLOC_CAP_DEFAULT);

  /// Drain one item from the ring buffer, decode per ble_decoder_mode and
  /// populate doc (decoded result, or raw advertisement in NONE mode /
  /// when ble_retain_undecoded is set).
  /// mac is filled with the colon-stripped lowercase MAC (e.g. "aabbccddeeff").
  /// Returns true if doc was populated, false if the queue was empty or
  /// the advertisement was dropped (undecoded, retain off).
  bool process(JsonDocument &doc, char *mac, size_t macLen);

  /// Set BTHome decryption key (32-char hex string). Empty disables decryption.
  void setBTHomeKey(const char *hexKey);

  /// Enable or disable active scanning. Call before begin().
  void setActiveScan(bool active);

  /// Ring buffer and queue statistics.
  struct Stats {
    size_t hwmBytes;         ///< High water mark (peak bytes used)
    size_t totalBytes;       ///< Ring buffer total capacity
    uint8_t hwmPercent;      ///< High water mark as percentage of total
    uint32_t queueFull;      ///< Times send_complete failed (queue full)
    uint32_t acquireFail;    ///< Times send_acquire failed (no space)
    uint32_t received;       ///< Total messages dequeued
    uint32_t decodedTheengs; ///< Decoded by the Theengs decoder
    uint32_t decodedBTHome;  ///< Decoded as BTHome v2
    uint32_t decodedCustom;  ///< Decoded by the custom decoder
    uint32_t rawAds;         ///< Advertisements no decoder claimed
    uint32_t dedupDrops;     ///< Advertisements dropped by dedup
  };

  /// Return current ring buffer statistics.
  Stats stats() const;

  /// Reset all counters (and the ring buffer high-water mark) to zero.
  void clearStats();

private:
  BLEScanner() = default;

  Impl *_impl = nullptr;
  bool _started = false;

  bool deliver(JsonDocument &rawDoc, JsonDocument &outDoc);
};
