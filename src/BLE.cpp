/**
 * BLE scanner glue — starts the BLEScanner singleton and drains its queue
 * from the main loop, publishing results to MQTT as ble/<mac>.
 * Decoder selection and undecoded/dedup handling live in BLEScanner.cpp.
 */

#include "mqtt.hpp"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEScanner.h>
#include <cstring>

#ifdef BOARD_HAS_PSRAM
#define RBMEM MALLOC_CAP_SPIRAM
#define BLE_RING_BYTES 32768
#else
#define RBMEM MALLOC_CAP_DEFAULT
#define BLE_RING_BYTES 8192 // internal DRAM is tight (C6/C5 ~320 KB)
#endif

static auto &bleScanner = BLEScanner::instance();

void blescanner_setup() {

  // Optional: set a BTHome decryption key (32-char hex string)
  // bleScanner.setBTHomeKey("00112233445566778899aabbccddeeff");

  bleScanner.begin(BLE_RING_BYTES, // ring buffer size
                   1000,           // scan time (ms)
                   100,            // scan interval
                   99,             // scan window
                   4096,           // task stack size
                   1,              // task priority
                   RBMEM);         // ring buffer memory capability
}

// Drain up to this many queued advertisements per loop() pass, so a burst
// of BLE traffic doesn't starve the queue and drive acquireFail up while
// the rest of loop() (WiFi/MQTT/web/Improv) still gets serviced each pass.
#define BLE_DRAIN_BUDGET 32

// Reused across BLE advertisements. One doc alive at a time (built, used,
// destroyed each loop pass) so a bump allocator that resets when the last
// live block is freed is safe here and avoids malloc/free churn on the
// shared heap (visible via mem_max_alloc on long uptimes).
class BleJsonArena : public ArduinoJson::Allocator {
public:
  void *allocate(size_t size) override {
    size = (size + 7) & ~size_t(7);
    if (size == 0)
      size = 8;
    if (live_ == 0)
      offset_ = 0;
    if (offset_ + sizeof(size_t) + size > sizeof(arena_))
      return nullptr; // ArduinoJson degrades gracefully (overflowed_=true)
    auto *hdr = reinterpret_cast<size_t *>(arena_ + offset_);
    *hdr = size;
    void *ptr = arena_ + offset_ + sizeof(size_t);
    offset_ += sizeof(size_t) + size;
    live_++;
    return ptr;
  }
  void deallocate(void *ptr) override {
    if (!ptr)
      return;
    if (--live_ == 0)
      offset_ = 0;
  }
  void *reallocate(void *ptr, size_t new_size) override {
    if (!ptr)
      return allocate(new_size);
    size_t old_size = *reinterpret_cast<size_t *>(static_cast<uint8_t *>(ptr) -
                                                  sizeof(size_t));
    if (new_size <= old_size)
      return ptr;
    void *newPtr = allocate(new_size);
    if (!newPtr)
      return nullptr; // old ptr stays valid/live per realloc contract
    memcpy(newPtr, ptr, old_size);
    live_--; // old block is superseded; caller will not deallocate() it
    return newPtr;
  }

private:
  alignas(8) uint8_t arena_[4096];
  size_t offset_ = 0;
  int live_ = 0;
};
static BleJsonArena bleJsonArena;

void blescanner_loop() {
  for (int i = 0; i < BLE_DRAIN_BUDGET; i++) {
    JsonDocument doc(&bleJsonArena);
    char mac[16];
    if (!bleScanner.process(doc, mac, sizeof(mac)))
      break;
    char payload[512];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    log_d("ble/%s: %.*s", mac, (int)len, payload);
    char topic[24];
    snprintf(topic, sizeof(topic), "ble/%s", mac);
    mqtt_publish(topic, payload);
  }
}
