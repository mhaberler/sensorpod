#include <Arduino.h> // pins_arduino.h defines BOARD_HAS_SDIO_ESP_HOSTED

#include "hosted_ota.hpp"

#if defined(BOARD_HAS_SDIO_ESP_HOSTED) && defined(ESP_HOSTED_DOWNGRADE)

#include <HTTPClient.h>
#include <Network.h>
#include <NetworkClientSecure.h>
#include <esp32-hal-hosted.h>
#include <stdio.h>

void buildHostedFwUrl(char *buf, size_t n, uint32_t maj, uint32_t min,
                      uint32_t pat) {
  if (!buf || n == 0)
    return;
  const char *target = hostedGetSlaveTargetName();
  snprintf(buf, n,
           "https://espressif.github.io/arduino-esp32/hosted/"
           "%s-v%lu.%lu.%lu.bin",
           target ? target : "esp32c6", (unsigned long)maj, (unsigned long)min,
           (unsigned long)pat);
}

bool hostedFwUrlExists(const char *url) {
  if (!url || !*url || !Network.isOnline())
    return false;

  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (!https.begin(client, url))
    return false;

  // HEAD avoids downloading the multi-MB image just to probe.
  int code = https.sendRequest("HEAD");
  https.end();
  return code == HTTP_CODE_OK;
}

// Arduino CDN only publishes sparse versions. Oldest-first for a harsh
// downgrade stress test (probed against
// https://espressif.github.io/arduino-esp32/hosted/).
static const uint8_t kKnownHostedMaj[] = {2, 2, 2, 2, 2, 2, 2};
static const uint8_t kKnownHostedMin[] = {8, 9, 9, 11, 12, 12, 12};
static const uint8_t kKnownHostedPat[] = {5, 2, 6, 6, 3, 8, 11};
static const size_t kKnownHostedCount =
    sizeof(kKnownHostedMaj) / sizeof(kKnownHostedMaj[0]);

static uint32_t hostedVerPack(uint32_t maj, uint32_t min, uint32_t pat) {
  return (maj << 16) | (min << 8) | pat;
}

bool findOlderHostedFw(uint32_t *maj, uint32_t *min, uint32_t *pat) {
  if (!maj || !min || !pat)
    return false;
  if (!hostedIsInitialized())
    return false;

  uint32_t hm = 0, hn = 0, hp = 0;
  hostedGetHostVersion(&hm, &hn, &hp);
  const uint32_t host = hostedVerPack(hm, hn, hp);

  // Prefer the oldest known CDN image that is still strictly older than host.
  for (size_t i = 0; i < kKnownHostedCount; i++) {
    const uint32_t tm = kKnownHostedMaj[i];
    const uint32_t tn = kKnownHostedMin[i];
    const uint32_t tp = kKnownHostedPat[i];
    if (hostedVerPack(tm, tn, tp) >= host)
      continue;
    char url[128];
    buildHostedFwUrl(url, sizeof(url), tm, tn, tp);
    log_w("hosted downgrade: probing old target %s", url);
    if (hostedFwUrlExists(url)) {
      *maj = tm;
      *min = tn;
      *pat = tp;
      log_w("hosted downgrade: using %lu.%lu.%lu", (unsigned long)tm,
            (unsigned long)tn, (unsigned long)tp);
      return true;
    }
  }
  log_e("hosted downgrade: no known older image on Arduino CDN "
        "(host %lu.%lu.%lu)",
        (unsigned long)hm, (unsigned long)hn, (unsigned long)hp);
  return false;
}

bool flashEspHostedFromUrl(const char *url) {
  if (!url || !*url) {
    log_e("hosted flash: empty URL");
    return false;
  }
  if (!hostedIsInitialized()) {
    log_e("hosted flash: esp-hosted not initialized");
    return false;
  }
  if (!Network.isOnline()) {
    log_e("hosted flash: network not online");
    return false;
  }

  log_w("Flashing esp-hosted co-processor from %s", url);

  // Stack client: heap NetworkClientSecure + HTTPClient::end() after 404
  // has been observed to Instruction-fault (MEPC=0) on P4 hosted.
  NetworkClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, url)) {
    log_e("hosted flash: HTTP begin failed");
    return false;
  }

  bool updateSuccess = false;
  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    if (httpCode == HTTP_CODE_NOT_FOUND)
      log_e("hosted flash: file not found (HTTP 404) — %s", url);
    else
      log_e("hosted flash: HTTP request failed with code %d", httpCode);
    https.end();
    return false;
  }

  int len = https.getSize();
  if (len < 0) {
    log_e("hosted flash: update size not received");
    https.end();
    return false;
  }

  NetworkClient *stream = https.getStreamPtr();
  log_w("Beginning hosted flash (%d bytes)...", len);
  // Req_OTABegin often times out if SDIO is busy; retry a few times.
  bool began = false;
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (hostedBeginUpdate()) {
      began = true;
      break;
    }
    log_w("hosted flash: begin attempt %d failed; settling", attempt);
    delay(1000);
  }
  if (!began) {
    log_e("hosted flash: begin failed after retries");
    https.end();
    return false;
  }

#define HOSTED_OTA_BUF_SIZE 2048
  uint8_t *buff = (uint8_t *)malloc(HOSTED_OTA_BUF_SIZE);
  if (!buff) {
    log_e("hosted flash: could not allocate buffer");
    https.end();
    return false;
  }

  while (https.connected() && len > 0) {
    size_t size = stream->available();
    if (size > 0) {
      Serial.print(".");
      if (size > HOSTED_OTA_BUF_SIZE)
        size = HOSTED_OTA_BUF_SIZE;
      if (size > (size_t)len) {
        log_e("\nhosted flash: extra bytes %lu",
              (unsigned long)(size - (size_t)len));
        break;
      }
      int readLen = stream->readBytes(buff, size);
      len -= readLen;
      if (!hostedWriteUpdate(buff, readLen)) {
        log_e("\nhosted flash: write failed");
        break;
      }
      if (len == 0) {
        Serial.println();
        log_w("Finalizing hosted flash...");
        if (!hostedEndUpdate()) {
          log_e("hosted flash: end failed");
          break;
        }
        log_w("Activating hosted firmware...");
        if (!hostedActivateUpdate()) {
          log_e("hosted flash: activate failed");
          break;
        }
        updateSuccess = true;
        log_w("SUCCESS: esp-hosted co-processor flashed");
        break;
      }
    }
    delay(1);
  }
  free(buff);
  Serial.println();
  https.end();
  return updateSuccess;
}

#endif // BOARD_HAS_SDIO_ESP_HOSTED && ESP_HOSTED_DOWNGRADE
