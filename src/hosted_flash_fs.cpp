#include <Arduino.h> // pins_arduino.h defines BOARD_HAS_SDIO_ESP_HOSTED

#include "hosted_flash_fs.hpp"

#if defined(BOARD_HAS_SDIO_ESP_HOSTED) && defined(HOSTED_FLASH_FS)

#include <LittleFS.h>
#include <Preferences.h>
#include <esp32-hal-hosted.h>

// Flashes the ESP-Hosted co-processor (C6) from an image bundled in the
// LittleFS partition, over the SDIO transport only - no WiFi, no network.
// Runs once at boot, before WiFi/BLE/USB load, when the co-processor's
// version differs from the host library's. The image must be named like the
// Arduino CDN file for the host version (hostedGetUpdateURL() basename,
// e.g. /esp32c6-v2.12.13.bin), so only a matching image is ever written.
// Attempts per host version are capped in NVS so a failing flash can't
// reboot-loop.

#define HOSTED_FLASH_MAX_TRIES 2
#define HOSTED_FLASH_CHUNK 2048

void wifi_set_hosted_pins(); // wifisetup.cpp

void hosted_flash_from_fs() {
  wifi_set_hosted_pins();
  if (!hostedInitWiFi()) { // SDIO transport only; esp_wifi is not started
    log_e("hosted fs flash: ESP-Hosted init failed");
    return;
  }
  uint32_t hm, hn, hp, sm, sn, sp;
  hostedGetHostVersion(&hm, &hn, &hp);
  hostedGetSlaveVersion(&sm, &sn, &sp);
  if (hm == sm && hn == sn && hp == sp) {
    log_i("hosted fs flash: co-processor %lu.%lu.%lu matches host",
          (unsigned long)sm, (unsigned long)sn, (unsigned long)sp);
    return;
  }

  const char *url = hostedGetUpdateURL();
  const char *name = strrchr(url, '/'); // "/esp32c6-vM.m.p.bin"
  if (!name || !LittleFS.begin(false)) {
    log_w("hosted fs flash: co-processor %lu.%lu.%lu != host %lu.%lu.%lu, "
          "no filesystem",
          (unsigned long)sm, (unsigned long)sn, (unsigned long)sp,
          (unsigned long)hm, (unsigned long)hn, (unsigned long)hp);
    return;
  }
  File f = LittleFS.open(name, "r");
  if (!f) {
    log_w("hosted fs flash: co-processor %lu.%lu.%lu != host %lu.%lu.%lu, "
          "image %s not in LittleFS (pio run -t uploadfs)",
          (unsigned long)sm, (unsigned long)sn, (unsigned long)sp,
          (unsigned long)hm, (unsigned long)hn, (unsigned long)hp, name);
    LittleFS.end();
    return;
  }

  Preferences prefs;
  char key[16];
  snprintf(key, sizeof(key), "hfl_%lu_%lu_%lu", (unsigned long)hm,
           (unsigned long)hn, (unsigned long)hp);
  prefs.begin("device-config", false);
  uint8_t tries = prefs.getUChar(key, 0);
  if (tries >= HOSTED_FLASH_MAX_TRIES) {
    log_e("hosted fs flash: %s already tried %u times, giving up (clear NVS "
          "key %s to retry)",
          name, tries, key);
    prefs.end();
    f.close();
    LittleFS.end();
    return;
  }
  prefs.putUChar(key, tries + 1);
  prefs.end();

  size_t total = f.size();
  log_w("hosted fs flash: co-processor %lu.%lu.%lu -> %lu.%lu.%lu from %s "
        "(%u bytes, attempt %u)",
        (unsigned long)sm, (unsigned long)sn, (unsigned long)sp,
        (unsigned long)hm, (unsigned long)hn, (unsigned long)hp, name,
        (unsigned)total, tries + 1);

  bool ok = false;
  for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
    ok = hostedBeginUpdate();
    if (!ok)
      delay(1000); // Req_OTABegin can time out while SDIO settles
  }
  uint8_t *buf = ok ? (uint8_t *)malloc(HOSTED_FLASH_CHUNK) : nullptr;
  size_t done = 0;
  int last_pct = -1;
  while (buf && done < total) {
    int n = f.read(buf, HOSTED_FLASH_CHUNK);
    if (n <= 0 || !hostedWriteUpdate(buf, n))
      break;
    done += n;
    int pct = done * 100 / total;
    if (pct / 10 != last_pct / 10) {
      log_w("hosted fs flash: %d%%", pct);
      last_pct = pct;
    }
  }
  free(buf);
  f.close();
  LittleFS.end();

  if (!ok || done != total || !hostedEndUpdate()) {
    log_e("hosted fs flash: failed at %u/%u bytes", (unsigned)done,
          (unsigned)total);
    return;
  }
  hostedActivateUpdate(); // may fail on old co-processor firmware; harmless
  log_w("hosted fs flash: done, restarting");
  delay(500);
  ESP.restart();
}

#else

void hosted_flash_from_fs() {}

#endif
