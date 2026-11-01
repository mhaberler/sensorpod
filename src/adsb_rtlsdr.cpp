#include "adsb_rtlsdr.hpp"

#ifdef ADSB_RTLSDR

#if !defined(ADSB_UART) || !defined(ADSB_USB)
#error "ADSB_RTLSDR needs ADSB_UART and ADSB_USB (shared USB host install)"
#endif

#include "deviceconfig.hpp"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <limits.h>
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
#define RTLSDR_NBUF 4 // reader -> demod buffers (PSRAM, RTLSDR_BUF_LEN each)
#define RTLSDR_BUF_US (RTLSDR_BUF_LEN / 2 * 1000000LL / RTLSDR_RATE) // 32768
#define RTLSDR_COV_WINDOW_US 2000000

static usb_host_client_handle_t client;
static QueueHandle_t new_dev_queue;
static volatile usb_device_handle_t session_hdl; // device being streamed
static volatile bool gone;

// Reading and demodulating overlap: this USB task keeps reading (the
// driver's sync transfers pump this client's events, so reads must stay
// here), handing full buffers to demod_task on the other core. If the
// demodulator falls behind and no buffer is free, the reader reads into
// `scratch` and drops it (overrun) rather than stalling the dongle.
static mode_s_t mode_s_ctx; // demod_task only
static uint8_t *iq_bufs[RTLSDR_NBUF];
static uint8_t *scratch;
static uint16_t *mag_buf;            // demod_task only
static QueueHandle_t free_q, full_q; // buffer indices
static volatile bool cov_reset;
static volatile uint32_t total_overruns;

static AdsbRtlsdrInfo info; // published once connected is set
static volatile uint32_t total_bytes, total_errors, total_frames;
static volatile uint32_t total_strong, total_clipped;
static volatile float noise_dbfs = NAN;
static volatile float cal_k = NAN;
static volatile float demod_duty_ema, coverage_ema;
static volatile int pending_gain = INT_MIN; // set by web server

AdsbRtlsdrInfo adsb_rtlsdr_info() {
  AdsbRtlsdrInfo i = info;
  i.bytes = total_bytes;
  i.read_errors = total_errors;
  i.frames = total_frames;
  i.noise_dbfs = noise_dbfs;
  i.strong_signals = total_strong;
  i.clipped = total_clipped;
  i.cal_k = cal_k;
  i.demod_duty = demod_duty_ema;
  i.coverage = coverage_ema;
  i.overruns = total_overruns;
  i.noise_dbm = adsb_rtlsdr_dbm(i.noise_dbfs);
  return i;
}

void adsb_rtlsdr_set_gain(int tenths) { pending_gain = tenths; }

void adsb_rtlsdr_set_cal_k(float k) { cal_k = k; }

float adsb_rtlsdr_dbm(float dbfs) {
  return dbfs - info.gain / 10.0f + cal_k; // NAN propagates
}

// Nearest supported step; -1 = highest.
static int snap_gain(int want, const int *gains, int n) {
  if (n <= 0)
    return want;
  if (want < 0)
    return gains[n - 1];
  int best = gains[0];
  for (int k = 1; k < n; k++)
    if (abs(gains[k] - want) < abs(best - want))
      best = gains[k];
  return best;
}

// USB strings end up in the web UI.
static void sanitize(char *s) {
  for (char *c = s; *c; c++)
    if (*c < 0x20 || *c >= 0x7f || strchr("<>&'\"", *c))
      *c = '?';
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
  sanitize(i.model);
  char serial[256] = ""; // driver writes up to 256 bytes
  rtlsdr_get_usb_strings(dev, nullptr, nullptr, serial);
  strlcpy(i.serial, serial, sizeof(i.serial));
  sanitize(i.serial);

  if (rtlsdr_set_center_freq(dev, RTLSDR_FREQ) < 0)
    log_w("ADS-B RTL-SDR: set center freq failed");
  if (rtlsdr_set_sample_rate(dev, RTLSDR_RATE) < 0)
    log_w("ADS-B RTL-SDR: set sample rate failed");
  rtlsdr_set_tuner_gain_mode(dev, 1); // manual
  int ng = rtlsdr_get_tuner_gains(dev, nullptr);
  if (ng > 0 && ng <= 32 && rtlsdr_get_tuner_gains(dev, i.gains) == ng)
    i.ngains = ng;
  int gain = DeviceConfig::getRtlGain();
  if (gain < 0)
    gain = ADSB_RTLSDR_GAIN;
  gain = snap_gain(gain, i.gains, i.ngains);
  pending_gain = INT_MIN;
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
  cov_reset = true;
  log_w("ADS-B RTL-SDR: %s connected addr %u, %04x:%04x, %lu Hz, %lu S/s, "
        "gain %.1f dB, bias-tee %s; UART input suspended",
        i.model, addr, vid, pid, (unsigned long)rtlsdr_get_center_freq(dev),
        (unsigned long)i.rate, i.gain / 10.0, bias_tee ? "on" : "off");

  const char *why = "unplugged";
  int errors = 0;
  while (!gone) {
    int want = pending_gain;
    if (want != INT_MIN) {
      pending_gain = INT_MIN;
      int g = snap_gain(want, info.gains, info.ngains);
      if (g >= 0 && rtlsdr_set_tuner_gain(dev, g) == 0) {
        info.gain = rtlsdr_get_tuner_gain(dev);
        adsb_signal_reset();
        log_w("ADS-B RTL-SDR: gain %.1f dB", info.gain / 10.0);
      }
    }
    uint8_t idx;
    bool have = xQueueReceive(free_q, &idx, 0) == pdTRUE;
    uint8_t *buf = have ? iq_bufs[idx] : scratch;
    bool ok = true;
    for (int off = 0; off < RTLSDR_BUF_LEN && !gone; off += RTLSDR_XFER) {
      int n = 0;
      r = rtlsdr_read_sync(dev, buf + off, RTLSDR_XFER, &n);
      if (r < 0 || n < RTLSDR_XFER) {
        ok = false;
        break;
      }
      total_bytes += n;
    }
    if (!ok || gone) {
      if (have)
        xQueueSend(free_q, &idx, 0);
      if (gone)
        break;
      total_errors++;
      if (++errors >= RTLSDR_MAX_ERRORS) {
        why = "read errors";
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    errors = 0;
    if (have)
      xQueueSend(full_q, &idx, 0);
    else
      total_overruns++;
  }

  info.connected = false;
  uint32_t frames = adsb_source_rtlsdr_detached();
  log_w("ADS-B RTL-SDR: addr %u disconnected (%s) after %lus, %lu frames; "
        "falling back to UART",
        addr, why, (unsigned long)((millis() - i.attached_ms) / 1000),
        (unsigned long)frames);
  rtlsdr_close(dev);
}

// Demodulates full buffers from the reader; pinned to the other core.
static void demod_task(void *) {
  int64_t win_start = 0;
  uint32_t win_bufs = 0;
  for (;;) {
    uint8_t idx;
    xQueueReceive(full_q, &idx, portMAX_DELAY);
    int64_t t0 = esp_timer_get_time();
    if (cov_reset) {
      cov_reset = false;
      win_start = t0;
      win_bufs = 0;
    }
    uint8_t *iq_buf = iq_bufs[idx];
    // One pass: magnitudes, clipped I/Q bytes, sum of I^2+Q^2.
    uint32_t clipped, iq_pwr;
    mode_s_compute_magnitude_vector_stats(iq_buf, mag_buf, RTLSDR_BUF_LEN,
                                          &clipped, &iq_pwr);
    total_clipped += clipped;
    // Noise floor: messages occupy a tiny fraction of the buffer, so the
    // mean power of all samples is the noise power (readsb local.noise).
    // mag = sqrt(I^2+Q^2) * 360 (libmodes LUT), full scale 65535.
    noise_dbfs = 10 * log10f((float)iq_pwr * (360.0f * 360.0f) /
                                 (RTLSDR_BUF_LEN / 2) / (65535.0f * 65535.0f) +
                             1e-12f);
    mode_s_detect(&mode_s_ctx, mag_buf, RTLSDR_BUF_LEN / 2, on_msg);
    xQueueSend(free_q, &idx, 0);

    // Demod time vs. buffer airtime; coverage = airtime demodulated per
    // wall time over RTLSDR_COV_WINDOW_US (overruns and read gaps lower it).
    int64_t t1 = esp_timer_get_time();
    float duty = 100.0f * (t1 - t0) / RTLSDR_BUF_US;
    demod_duty_ema =
        demod_duty_ema ? demod_duty_ema * 0.95f + duty * 0.05f : duty;
    win_bufs++;
    if (t1 - win_start >= RTLSDR_COV_WINDOW_US) {
      coverage_ema = fminf(100.0f, 100.0f * win_bufs * RTLSDR_BUF_US /
                                       (float)(t1 - win_start));
      win_start = t1;
      win_bufs = 0;
    }
  }
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
  free_q = xQueueCreate(RTLSDR_NBUF, sizeof(uint8_t));
  full_q = xQueueCreate(RTLSDR_NBUF, sizeof(uint8_t));
  bool ok = free_q && full_q;
  for (uint8_t k = 0; k < RTLSDR_NBUF && ok; k++) {
    iq_bufs[k] = (uint8_t *)heap_caps_malloc(RTLSDR_BUF_LEN, MALLOC_CAP_SPIRAM);
    ok = iq_bufs[k] && xQueueSend(free_q, &k, 0) == pdTRUE;
  }
  scratch = (uint8_t *)heap_caps_malloc(RTLSDR_BUF_LEN, MALLOC_CAP_SPIRAM);
  mag_buf = (uint16_t *)heap_caps_malloc(RTLSDR_BUF_LEN, MALLOC_CAP_SPIRAM);
  if (!ok || !scratch || !mag_buf) {
    log_e("ADS-B RTL-SDR: PSRAM buffer alloc failed, disabled");
    return;
  }
  mode_s_init(&mode_s_ctx); // magnitude LUT; fix_errors on (dump1090 default)
  cal_k = DeviceConfig::getRtlCalK();

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
  // Reader on core 0 next to the USB host lib task; demodulator alone on
  // the other core (core 1 on the P4; same core on single-core chips).
  // Demod at priority 1 = Arduino loopTask (also core 1): they time-slice,
  // so under overload the buffer queue overruns (counted) instead of the
  // web UI/MQTT loop starving.
  xTaskCreatePinnedToCore(rtlsdr_task, "adsb_rtlsdr", 8192, nullptr, 3, nullptr,
                          0);
  xTaskCreatePinnedToCore(demod_task, "adsb_demod", 8192, nullptr, 1, nullptr,
                          portNUM_PROCESSORS > 1 ? 1 : 0);
  log_w("ADS-B RTL-SDR: waiting for dongle (%u Hz, %u S/s)", RTLSDR_FREQ,
        RTLSDR_RATE);
}

#endif // ADSB_RTLSDR
