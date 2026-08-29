#include "adsb.hpp"

#ifdef ADSB_UART

#include "mqtt.hpp"
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

// Rextron 89001090 ADS-B receiver module: fixed 921600 8N1 TTL UART,
// no flow control, ASCII frames "*<hex>;" (Mode-S extended squitter),
// up to ~100 aircraft concurrently. A dedicated reader task keeps
// draining the UART regardless of main-loop stalls (HTTP/OTA/mDNS);
// framed lines are handed off through a ring buffer to adsb_loop(),
// which publishes them over MQTT.

#define ADSB_BAUD 921600
#define ADSB_RX_BUF_SIZE 16384
#define ADSB_RINGBUF_SIZE 8192
#define ADSB_MAX_LINE 128

static HardwareSerial adsb_serial(1);
static RingbufHandle_t adsb_ringbuf;
static uint32_t adsb_dropped_lines = 0;
static uint32_t adsb_dropped_overflow = 0;

static void adsb_reader_task(void *arg) {
  char line[ADSB_MAX_LINE];
  size_t len = 0;
  bool in_frame = false;

  for (;;) {
    int c = adsb_serial.read();
    if (c < 0) {
      // Nothing pending right now; block briefly to give up the CPU.
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    if (c == '*') {
      // Start of a new frame; discard any partial garbage in progress.
      in_frame = true;
      len = 0;
      continue;
    }

    if (!in_frame)
      continue;

    if (c == ';') {
      in_frame = false;
      if (len > 0) {
        if (xRingbufferSend(adsb_ringbuf, line, len, 0) != pdTRUE)
          adsb_dropped_overflow++;
      }
      len = 0;
      continue;
    }

    if (len < ADSB_MAX_LINE) {
      line[len++] = (char)c;
    } else {
      // Frame too long for a Mode-S line; drop it and resync on next '*'.
      in_frame = false;
      len = 0;
      adsb_dropped_lines++;
    }
  }
}

void adsb_setup() {
  adsb_ringbuf = xRingbufferCreate(ADSB_RINGBUF_SIZE, RINGBUF_TYPE_NOSPLIT);
  adsb_serial.setRxBufferSize(ADSB_RX_BUF_SIZE);
  adsb_serial.begin(ADSB_BAUD, SERIAL_8N1, ADSB_RX_PIN, ADSB_TX_PIN);
  xTaskCreate(adsb_reader_task, "adsb_rx", 4096, nullptr, 5, nullptr);
  log_w("ADS-B UART started at %d baud (rx=%d tx=%d)", ADSB_BAUD, ADSB_RX_PIN,
        ADSB_TX_PIN);
}

void adsb_loop() {
  size_t item_size = 0;
  void *item = xRingbufferReceive(adsb_ringbuf, &item_size, 0);
  if (!item)
    return;

  char frame[ADSB_MAX_LINE + 1];
  size_t n = item_size < ADSB_MAX_LINE ? item_size : ADSB_MAX_LINE;
  memcpy(frame, item, n);
  frame[n] = '\0';
  vRingbufferReturnItem(adsb_ringbuf, item);

  mqtt_publish("ADSB", frame);
}

#endif // ADSB_UART
