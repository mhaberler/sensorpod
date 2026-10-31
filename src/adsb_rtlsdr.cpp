#include "adsb_rtlsdr.hpp"

#ifdef ADSB_RTLSDR

#if !defined(ADSB_UART) || !defined(ADSB_USB)
#error "ADSB_RTLSDR needs ADSB_UART and ADSB_USB (shared USB host install)"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>
#include <usb/usb_host.h>

#include <mode-s.h>
#include <rtl-sdr.h>
extern "C" {
#include <rtlsdr_i2c.h> // rtlsdr_get_dongle_info()
}

// RTL-SDR ADS-B receiver: vendored librtlsdr (lib/rtlsdr) on a second USB
// host client next to the FTDI one in adsb_usb.cpp. The driver's transfers
// are synchronous and pump this client's events themselves, so one task
// both waits for attach and runs the whole session: read
// RTLSDR_BUF_LEN of 2 MS/s 8-bit I/Q, demodulate it (libmodes), repeat.
// Samples arriving during demodulation are lost, as in ADS-B Scope, which
// this follows. CRC-valid DF17/18 frames are re-encoded as "*<hex>;" and
// fed through the same framer/ring buffer as the UART and FTDI sources,
// so aircraft table, MQTT publish and ihex behave identically.

#define RTLSDR_VID 0x0bda
#define RTLSDR_FREQ 1090000000
#define RTLSDR_RATE 2000000
#define RTLSDR_XFER 16384                // bytes per bulk read
#define RTLSDR_BUF_LEN (RTLSDR_XFER * 8) // bytes demodulated per pass
#define RTLSDR_MAX_ERRORS 10             // consecutive, then give up

static usb_host_client_handle_t client;
static QueueHandle_t new_dev_queue;
static volatile usb_device_handle_t session_hdl; // device being streamed
static volatile bool gone;

static mode_s_t mode_s_ctx;
static uint8_t *iq_buf;
static uint16_t *mag_buf;

static AdsbRtlsdrInfo info; // published once connected is set
static volatile uint32_t total_bytes, total_errors, total_frames;
static volatile uint32_t total_strong, total_clipped;
static volatile float noise_dbfs = NAN;

AdsbRtlsdrInfo adsb_rtlsdr_info() {
  AdsbRtlsdrInfo i = info;
  i.bytes = total_bytes;
  i.read_errors = total_errors;
  i.frames = total_frames;
  i.noise_dbfs = noise_dbfs;
  i.strong_signals = total_strong;
  i.clipped = total_clipped;
  return i;
}

static void on_msg(mode_s_t *, struct mode_s_msg *mm) {
  if (!mm->crcok || (mm->msgtype != 17 && mm->msgtype != 18))
    return;
  static const char hexd[] = "0123456789ABCDEF";
  char hex[2 * MODE_S_LONG_MSG_BYTES];
  size_t n = 0;
  for (int i = 0; i < mm->msgbits / 8; i++) {
    hex[n++] = hexd[mm->msg[i] >> 4];
    hex[n++] = hexd[mm->msg[i] & 0xf];
  }
  total_frames++;
  if (mm->signal_level > 0.501187) // -3 dBFS, readsb "strong_signals"
    total_strong++;
  adsb_feed_rtlsdr(hex, n, (float)mm->signal_level);
}

// Opens, tunes and streams one dongle until it is unplugged or fails.
static void session(uint8_t addr, uint16_t vid, uint16_t pid) {
  rtlsdr_dev_t *dev = nullptr;
  int r = rtlsdr_open(&dev, addr, client);
  if (r < 0 || !dev) {
    log_e("ADS-B RTL-SDR: open addr %u failed: %d", addr, r);
    return;
  }

  AdsbRtlsdrInfo i = {};
  int tuner = 0, force_bt = 0;
  rtlsdr_get_dongle_info(dev, i.model, sizeof(i.model), &tuner, &force_bt);
  for (char *c = i.model; *c; c++) // USB strings end up in the web UI
    if (*c < 0x20 || *c >= 0x7f || strchr("<>&'\"", *c))
      *c = '?';

  if (rtlsdr_set_center_freq(dev, RTLSDR_FREQ) < 0)
    log_w("ADS-B RTL-SDR: set center freq failed");
  if (rtlsdr_set_sample_rate(dev, RTLSDR_RATE) < 0)
    log_w("ADS-B RTL-SDR: set sample rate failed");
  rtlsdr_set_tuner_gain_mode(dev, 1); // manual
  int gain = ADSB_RTLSDR_GAIN;
  if (gain < 0) {
    int gains[32];
    int ng = rtlsdr_get_tuner_gains(dev, nullptr);
    if (ng > 0 && ng <= 32 && rtlsdr_get_tuner_gains(dev, gains) == ng)
      gain = gains[ng - 1];
  }
  if (gain >= 0)
    rtlsdr_set_tuner_gain(dev, gain);
  bool bias_tee = ADSB_RTLSDR_BIAS_TEE || force_bt;
  rtlsdr_set_bias_tee(dev, bias_tee);
  rtlsdr_reset_buffer(dev);

  i.addr = addr;
  i.vid = vid;
  i.pid = pid;
  i.gain = rtlsdr_get_tuner_gain(dev);
  i.bias_tee = bias_tee;
  i.rate = rtlsdr_get_sample_rate(dev);
  i.attached_ms = millis();
  i.connected = true;
  info = i;
  adsb_source_rtlsdr_attached();
  log_w("ADS-B RTL-SDR: %s connected addr %u, %04x:%04x, %lu Hz, %lu S/s, "
        "gain %.1f dB, bias-tee %s; UART input suspended",
        i.model, addr, vid, pid, (unsigned long)rtlsdr_get_center_freq(dev),
        (unsigned long)i.rate, i.gain / 10.0, bias_tee ? "on" : "off");

  const char *why = "unplugged";
  int errors = 0;
  while (!gone) {
    bool ok = true;
    for (int off = 0; off < RTLSDR_BUF_LEN && !gone; off += RTLSDR_XFER) {
      int n = 0;
      r = rtlsdr_read_sync(dev, iq_buf + off, RTLSDR_XFER, &n);
      if (r < 0 || n < RTLSDR_XFER) {
        ok = false;
        break;
      }
      total_bytes += n;
    }
    if (gone)
      break;
    if (!ok) {
      total_errors++;
      if (++errors >= RTLSDR_MAX_ERRORS) {
        why = "read errors";
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    errors = 0;
    uint32_t clipped = 0;
    for (int k = 0; k < RTLSDR_BUF_LEN; k++)
      clipped += iq_buf[k] == 0 || iq_buf[k] == 255;
    total_clipped += clipped;
    mode_s_compute_magnitude_vector(iq_buf, mag_buf, RTLSDR_BUF_LEN);
    // Noise floor: messages occupy a tiny fraction of the buffer, so the
    // mean power of all samples is the noise power (readsb local.noise).
    uint64_t pwr = 0;
    for (int k = 0; k < RTLSDR_BUF_LEN / 2; k++)
      pwr += (uint32_t)mag_buf[k] * mag_buf[k];
    noise_dbfs =
        10 * log10f((float)pwr / (RTLSDR_BUF_LEN / 2) / (65535.0f * 65535.0f) +
                    1e-12f);
    mode_s_detect(&mode_s_ctx, mag_buf, RTLSDR_BUF_LEN / 2, on_msg);
  }

  info.connected = false;
  uint32_t frames = adsb_source_rtlsdr_detached();
  log_w("ADS-B RTL-SDR: addr %u disconnected (%s) after %lus, %lu frames; "
        "falling back to UART",
        addr, why, (unsigned long)((millis() - i.attached_ms) / 1000),
        (unsigned long)frames);
  rtlsdr_close(dev);
}

// Returns true (and the ids) if the device at addr is an RTL2832U.
static bool probe(uint8_t addr, uint16_t *vid, uint16_t *pid) {
  usb_device_handle_t hdl;
  if (usb_host_device_open(client, addr, &hdl) != ESP_OK)
    return false;
  const usb_device_desc_t *dd;
  usb_host_get_device_descriptor(hdl, &dd);
  *vid = dd->idVendor;
  *pid = dd->idProduct;
  usb_host_device_close(client, hdl);
  // The handle is the host's device object, shared by all clients and
  // valid until unplug; rtlsdr_open() reopens the same one.
  session_hdl = hdl;
  return *vid == RTLSDR_VID && (*pid == 0x2832 || *pid == 0x2838);
}

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *) {
  switch (msg->event) {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    xQueueSend(new_dev_queue, &msg->new_dev.address, 0);
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    if (msg->dev_gone.dev_hdl == session_hdl)
      gone = true;
    break;
  default:
    break;
  }
}

static void rtlsdr_task(void *) {
  for (;;) {
    usb_host_client_handle_events(client, pdMS_TO_TICKS(50));
    uint8_t addr;
    while (xQueueReceive(new_dev_queue, &addr, 0) == pdTRUE) {
      uint16_t vid, pid;
      if (!probe(addr, &vid, &pid))
        continue;
      gone = false;
      session(addr, vid, pid);
    }
  }
}

void adsb_rtlsdr_setup() {
  iq_buf = (uint8_t *)heap_caps_malloc(RTLSDR_BUF_LEN, MALLOC_CAP_SPIRAM);
  mag_buf = (uint16_t *)heap_caps_malloc(RTLSDR_BUF_LEN, MALLOC_CAP_SPIRAM);
  if (!iq_buf || !mag_buf) {
    log_e("ADS-B RTL-SDR: PSRAM buffer alloc failed, disabled");
    return;
  }
  mode_s_init(&mode_s_ctx); // magnitude LUT; fix_errors on (dump1090 default)

  new_dev_queue = xQueueCreate(8, sizeof(uint8_t));
  usb_host_client_config_t cfg = {};
  cfg.is_synchronous = false;
  cfg.max_num_event_msg = 8;
  cfg.async.client_event_callback = client_event_cb;
  esp_err_t err = usb_host_client_register(&cfg, &client);
  if (err != ESP_OK) {
    log_e("ADS-B RTL-SDR: usb_host_client_register failed: %s",
          esp_err_to_name(err));
    return;
  }
  xTaskCreate(rtlsdr_task, "adsb_rtlsdr", 8192, nullptr, 3, nullptr);
  log_w("ADS-B RTL-SDR: waiting for dongle (%u Hz, %u S/s)", RTLSDR_FREQ,
        RTLSDR_RATE);
}

#endif // ADSB_RTLSDR
