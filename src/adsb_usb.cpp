#include "adsb_usb.hpp"

#if defined(ADSB_UART) && defined(ADSB_USB)

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <usb/usb_host.h>

// Minimal USB-serial receive drivers on the ESP-IDF USB Host Library, enough
// to stream an ADS-B "*<hex>;" line source into adsb_feed_usb(). Works
// through hubs (CONFIG_USB_HOST_HUBS_SUPPORTED). Only the first matching
// device attached is used:
//
// - FTDI (FT232R/FT-X/FT232H/FT2232H/FT4232H, VID 0x0403): port A interface
//   configured for 921600 8N1. FTDI bulk IN packets carry 2 modem/line
//   status bytes at the start of every max-packet-size chunk; those are
//   stripped before feeding.
// - Silicon Labs CP210x (VID 0x10c4, e.g. CP2102/CP2104 0xea60): interface
//   0 enabled and set to 921600 8N1 via vendor requests; bulk IN carries
//   plain data. DTR/RTS (SET_MHS) left alone: ESP32 dev boards wire them
//   to the auto-reset circuit.
// - CDC-ACM (any device with a CDC data interface, class 0x0A, that has a
//   bulk IN endpoint, e.g. an ESP32-C6/C3/S3 USB-Serial/JTAG or TinyUSB
//   CDC): data interface claimed and streamed as-is. No line coding and no
//   SET_CONTROL_LINE_STATE - on Espressif USB-Serial/JTAG, DTR/RTS changes
//   are the auto-reset/download sequence and would reset the sender.

#define ADSB_USB_BAUD 921600
#define FTDI_VID 0x0403
#define CP210X_VID 0x10c4
#define FTDI_LATENCY_MS 2
#define BULK_XFER_SIZE 512 // multiple of both FS (64) and HS (512) MPS
#define BULK_XFER_COUNT 4
#define CTRL_TIMEOUT_MS 1000

// FTDI vendor requests
#define SIO_RESET 0x00
#define SIO_SET_MODEM_CTRL 0x01
#define SIO_SET_FLOW_CTRL 0x02
#define SIO_SET_BAUDRATE 0x03
#define SIO_SET_DATA 0x04
#define SIO_SET_LATENCY_TIMER 0x09

// CP210x vendor requests (AN571), interface recipient
#define CP210X_IFC_ENABLE 0x00
#define CP210X_SET_LINE_CTL 0x03
#define CP210X_PURGE 0x12
#define CP210X_SET_BAUDRATE 0x1e

#define SIO_RESET_SIO 0
#define SIO_RESET_PURGE_RX 1
#define SIO_RESET_PURGE_TX 2
#define SIO_SET_DTR_HIGH 0x0101
#define SIO_SET_RTS_HIGH 0x0202

static usb_host_client_handle_t client;
static QueueHandle_t new_dev_queue;

static struct {
  usb_device_handle_t hdl;
  uint8_t addr;
  enum { FTDI, CP210X, CDC } kind;
  uint8_t intf; // claimed interface
  uint8_t ep_in;
  uint16_t mps;
  usb_transfer_t *ctrl;
  usb_transfer_t *bulk[BULK_XFER_COUNT];
  volatile int inflight;
  volatile bool gone;
  bool raw_logged;
  uint32_t attached_ms;
} dev;

static AdsbUsbInfo usb_info;        // published once connected is set
static volatile uint32_t usb_bytes; // all sessions

// UTF-16LE string descriptor -> printable, HTML-safe ASCII.
static void str_desc_ascii(const usb_str_desc_t *d, char *out, size_t cap) {
  size_t n = 0;
  if (d && d->bLength > 2) {
    size_t chars = (d->bLength - 2) / 2;
    for (size_t i = 0; i < chars && n + 1 < cap; i++) {
      uint16_t c = d->wData[i];
      out[n++] = (c >= 0x20 && c < 0x7f && !strchr("<>&'\"", c)) ? c : '?';
    }
  }
  out[n] = 0;
}

AdsbUsbInfo adsb_usb_info() {
  AdsbUsbInfo i = usb_info;
  i.bytes = usb_bytes;
  return i;
}

// ---------------------------------------------------------------------------
// Chip identification + baud divisor (after libftdi ftdi_convert_baudrate)

static bool chip_is_h(uint16_t bcd) {
  return bcd == 0x0700 || bcd == 0x0800 || bcd == 0x0900;
}

static const char *chip_name(uint16_t bcd) {
  switch (bcd) {
  case 0x0400:
    return "FT232BM";
  case 0x0500:
    return "FT2232C";
  case 0x0600:
    return "FT232R";
  case 0x0700:
    return "FT2232H";
  case 0x0800:
    return "FT4232H";
  case 0x0900:
    return "FT232H";
  case 0x1000:
    return "FT-X";
  default:
    return "FTDI?";
  }
}

static int ftdi_to_clkbits(int baud, uint32_t clk, int clk_div,
                           uint32_t *encoded) {
  static const uint8_t frac_code[8] = {0, 3, 2, 4, 1, 5, 6, 7};
  int best_baud;
  if (baud >= (int)(clk / clk_div)) {
    *encoded = 0;
    best_baud = clk / clk_div;
  } else if (baud >= (int)(clk / (clk_div + clk_div / 2))) {
    *encoded = 1;
    best_baud = clk / (clk_div + clk_div / 2);
  } else if (baud >= (int)(clk / (2 * clk_div))) {
    *encoded = 2;
    best_baud = clk / (2 * clk_div);
  } else {
    // Divisor in 1/16ths, rounded to 1/8ths.
    uint32_t divisor = (uint64_t)clk * 16 / clk_div / baud;
    uint32_t best_div = (divisor & 1) ? divisor / 2 + 1 : divisor / 2;
    if (best_div > 0x20000)
      best_div = 0x1ffff;
    uint32_t b = (uint64_t)clk * 16 / clk_div / best_div;
    best_baud = (b & 1) ? b / 2 + 1 : b / 2;
    *encoded = (best_div >> 3) | ((uint32_t)frac_code[best_div & 7] << 14);
  }
  return best_baud;
}

// Returns the actual baud; fills wValue/wIndex for SIO_SET_BAUDRATE.
static int ftdi_baud(int baud, uint16_t bcd, uint8_t port, uint16_t *value,
                     uint16_t *index) {
  uint32_t enc;
  int actual;
  if (chip_is_h(bcd) && baud * 10 > 120000000 / 0x3fff) {
    actual = ftdi_to_clkbits(baud, 120000000, 10, &enc);
    enc |= 0x20000; // 120 MHz clock select
  } else {
    actual = ftdi_to_clkbits(baud, 48000000, 16, &enc);
  }
  *value = enc & 0xffff;
  if (chip_is_h(bcd) || bcd == 0x0500)
    *index = ((enc >> 8) & 0xff00) | port;
  else
    *index = enc >> 16;
  return actual;
}

// ---------------------------------------------------------------------------
// Transfers

static void ctrl_cb(usb_transfer_t *xfer) {
  *(volatile bool *)xfer->context = true;
}

// Synchronous OUT control request with optional data stage (<= 64 bytes);
// pumps client events until completion. Only call from the client task,
// outside event callbacks.
static esp_err_t ctrl_out(uint8_t type, uint8_t req, uint16_t value,
                          uint16_t index, const void *data = nullptr,
                          uint16_t len = 0) {
  volatile bool done = false;
  usb_transfer_t *x = dev.ctrl;
  usb_setup_packet_t *s = (usb_setup_packet_t *)x->data_buffer;
  s->bmRequestType = type;
  s->bRequest = req;
  s->wValue = value;
  s->wIndex = index;
  s->wLength = len;
  if (len)
    memcpy(x->data_buffer + sizeof(usb_setup_packet_t), data, len);
  x->num_bytes = sizeof(usb_setup_packet_t) + len;
  x->device_handle = dev.hdl;
  x->bEndpointAddress = 0;
  x->callback = ctrl_cb;
  x->context = (void *)&done;
  x->timeout_ms = CTRL_TIMEOUT_MS;

  esp_err_t err = usb_host_transfer_submit_control(client, x);
  if (err != ESP_OK)
    return err;
  uint32_t t0 = millis();
  while (!done) {
    usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
    if (millis() - t0 > CTRL_TIMEOUT_MS + 500)
      return ESP_ERR_TIMEOUT; // transfer still owned by the stack
  }
  return x->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_FAIL;
}

// Vendor, host-to-device, device recipient.
static esp_err_t ftdi_ctrl(uint8_t req, uint16_t value, uint16_t index) {
  return ctrl_out(0x40, req, value, index);
}

static void bulk_cb(usb_transfer_t *x) {
  if (x->status == USB_TRANSFER_STATUS_COMPLETED) {
    int n = x->actual_num_bytes;
    if (!dev.raw_logged && n > 2) {
      dev.raw_logged = true;
      log_w("ADS-B[usb] first raw chunk (%d bytes%s):", n,
            dev.kind == dev.FTDI ? ", incl. status" : "");
      log_buf_w(x->data_buffer, n < 32 ? n : 32);
    }
    if (dev.kind != dev.FTDI) {
      adsb_feed_usb(x->data_buffer, n);
      usb_bytes += n;
    } else
      for (int off = 0; off < n; off += dev.mps) {
        int chunk = n - off < dev.mps ? n - off : dev.mps;
        if (chunk > 2) {
          adsb_feed_usb(x->data_buffer + off + 2, chunk - 2);
          usb_bytes += chunk - 2;
        }
      }
    if (!dev.gone && usb_host_transfer_submit(x) == ESP_OK)
      return;
  } else if (!dev.gone && x->status != USB_TRANSFER_STATUS_NO_DEVICE &&
             x->status != USB_TRANSFER_STATUS_CANCELED) {
    log_w("ADS-B USB: bulk IN status %d, transfer dropped", x->status);
  }
  dev.inflight--;
}

// ---------------------------------------------------------------------------
// Device attach / detach (client task context)

static void ftdi_teardown(const char *why) {
  usb_info.connected = false;
  if (dev.ep_in && dev.inflight > 0) {
    usb_host_endpoint_halt(dev.hdl, dev.ep_in);
    usb_host_endpoint_flush(dev.hdl, dev.ep_in);
    uint32_t t0 = millis();
    while (dev.inflight > 0 && millis() - t0 < 500)
      usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
  }
  if (dev.inflight > 0) {
    // Freeing transfers the stack still owns would corrupt it; leak instead.
    log_e("ADS-B USB: %d transfers stuck, leaking", dev.inflight);
  } else {
    for (auto &b : dev.bulk)
      if (b)
        usb_host_transfer_free(b);
  }
  if (dev.ctrl)
    usb_host_transfer_free(dev.ctrl);
  if (dev.ep_in)
    usb_host_interface_release(client, dev.hdl, dev.intf);
  usb_host_device_close(client, dev.hdl);

  if (dev.attached_ms) {
    uint32_t frames = adsb_source_usb_detached();
    log_w("ADS-B USB: dongle addr %u disconnected (%s) after %lus, %lu "
          "frames; falling back to UART",
          dev.addr, why, (unsigned long)((millis() - dev.attached_ms) / 1000),
          (unsigned long)frames);
  }
  memset(&dev, 0, sizeof(dev));
}

static bool ftdi_configure(uint16_t bcd, int *actual_baud) {
  const uint8_t port = 1; // interface 0 = port A
  uint16_t bv, bi;
  *actual_baud = ftdi_baud(ADSB_USB_BAUD, bcd, port, &bv, &bi);

  struct {
    const char *name;
    uint8_t req;
    uint16_t value, index;
  } seq[] = {
      {"SIO_RESET", SIO_RESET, SIO_RESET_SIO, port},
      {"SIO_SET_BAUDRATE", SIO_SET_BAUDRATE, bv, bi},
      {"SIO_SET_DATA 8N1", SIO_SET_DATA, 0x0008, port},
      {"SIO_SET_FLOW_CTRL none", SIO_SET_FLOW_CTRL, 0, port},
      {"SIO_SET_LATENCY_TIMER", SIO_SET_LATENCY_TIMER, FTDI_LATENCY_MS, port},
      {"SIO_SET_MODEM_CTRL DTR", SIO_SET_MODEM_CTRL, SIO_SET_DTR_HIGH, port},
      {"SIO_SET_MODEM_CTRL RTS", SIO_SET_MODEM_CTRL, SIO_SET_RTS_HIGH, port},
      {"SIO_RESET purge RX", SIO_RESET, SIO_RESET_PURGE_RX, port},
      {"SIO_RESET purge TX", SIO_RESET, SIO_RESET_PURGE_TX, port},
  };
  for (auto &s : seq) {
    esp_err_t err = ftdi_ctrl(s.req, s.value, s.index);
    if (err != ESP_OK) {
      log_e("ADS-B USB: %s failed: %s", s.name, esp_err_to_name(err));
      return false;
    }
  }
  return true;
}

// CP210x: enable the UART, ADSB_USB_BAUD 8N1, purge. DTR/RTS untouched.
static bool cp210x_configure(uint8_t intf) {
  const uint8_t type = 0x41;     // vendor, host-to-device, interface recipient
  uint32_t baud = ADSB_USB_BAUD; // little-endian on the wire, as is the P4
  struct {
    const char *name;
    uint8_t req;
    uint16_t value;
    const void *data;
    uint16_t len;
  } seq[] = {
      {"IFC_ENABLE", CP210X_IFC_ENABLE, 1, nullptr, 0},
      {"SET_BAUDRATE", CP210X_SET_BAUDRATE, 0, &baud, sizeof(baud)},
      {"SET_LINE_CTL 8N1", CP210X_SET_LINE_CTL, 0x0800, nullptr, 0},
      {"PURGE", CP210X_PURGE, 0x000f, nullptr, 0},
  };
  for (auto &s : seq) {
    esp_err_t err = ctrl_out(type, s.req, s.value, intf, s.data, s.len);
    if (err != ESP_OK) {
      log_e("ADS-B USB: CP210x %s failed: %s", s.name, esp_err_to_name(err));
      return false;
    }
  }
  return true;
}

static void device_new(uint8_t addr) {
  usb_device_handle_t hdl;
  if (usb_host_device_open(client, addr, &hdl) != ESP_OK) {
    log_w("ADS-B USB: cannot open device addr %u", addr);
    return;
  }
  const usb_device_desc_t *dd;
  usb_device_info_t info;
  usb_host_get_device_descriptor(hdl, &dd);
  usb_host_device_info(hdl, &info);

  const usb_config_desc_t *cfg;
  usb_host_get_active_config_descriptor(hdl, &cfg);

  // FTDI: interface 0. Otherwise the first CDC data interface with a bulk
  // IN endpoint.
  bool ftdi = dd->idVendor == FTDI_VID;
  bool cp210x = dd->idVendor == CP210X_VID;
  int intf_num = -1;
  uint8_t ep_in = 0;
  uint16_t mps = 0;
  for (int n = 0; n < cfg->bNumInterfaces && intf_num < 0; n++) {
    int off = 0;
    const usb_intf_desc_t *intf =
        usb_parse_interface_descriptor(cfg, n, 0, &off);
    if (!intf ||
        ((ftdi || cp210x) ? n != 0
                          : intf->bInterfaceClass != USB_CLASS_CDC_DATA))
      continue;
    for (int i = 0; i < intf->bNumEndpoints; i++) {
      int eoff = off;
      const usb_ep_desc_t *ep = usb_parse_endpoint_descriptor_by_index(
          intf, i, cfg->wTotalLength, &eoff);
      if (ep && (ep->bEndpointAddress & 0x80) &&
          (ep->bmAttributes & 0x03) == USB_BM_ATTRIBUTES_XFER_BULK) {
        intf_num = n;
        ep_in = ep->bEndpointAddress;
        mps = ep->wMaxPacketSize & 0x7ff;
        break;
      }
    }
  }

  if (intf_num < 0 || !mps || dev.hdl) {
    log_w("USB device addr %u %04x:%04x (class 0x%02x) ignored%s", addr,
          dd->idVendor, dd->idProduct, dd->bDeviceClass,
          dev.hdl  ? " (ADS-B dongle already active)"
          : ftdi   ? " (FTDI without bulk IN on interface 0)"
          : cp210x ? " (CP210x without bulk IN on interface 0)"
                   : " (no FTDI/CP210x/CDC-ACM data interface)");
    usb_host_device_close(client, hdl);
    return;
  }

  dev.hdl = hdl;
  dev.addr = addr;
  dev.kind = ftdi ? dev.FTDI : cp210x ? dev.CP210X : dev.CDC;
  dev.intf = intf_num;
  uint16_t bcd = dd->bcdDevice;

  if (usb_host_interface_claim(client, hdl, intf_num, 0) != ESP_OK) {
    log_e("ADS-B USB: claim interface %d failed", intf_num);
    ftdi_teardown("claim failed"); // ep_in still 0: nothing to release
    return;
  }
  dev.ep_in = ep_in;
  dev.mps = mps;

  int actual_baud = 0;
  if (ftdi || cp210x) {
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 64, 0,
                                &dev.ctrl) != ESP_OK) {
      log_e("ADS-B USB: control transfer alloc failed");
      ftdi_teardown("alloc failed");
      return;
    }
    bool ok =
        ftdi ? ftdi_configure(bcd, &actual_baud) : cp210x_configure(intf_num);
    if (!ok) {
      ftdi_teardown("config failed");
      return;
    }
    if (cp210x)
      actual_baud = ADSB_USB_BAUD; // device rounds internally, not reported
  }

  for (auto &b : dev.bulk) {
    if (usb_host_transfer_alloc(BULK_XFER_SIZE, 0, &b) != ESP_OK) {
      log_e("ADS-B USB: bulk transfer alloc failed");
      ftdi_teardown("alloc failed");
      return;
    }
    b->device_handle = hdl;
    b->bEndpointAddress = dev.ep_in;
    b->callback = bulk_cb;
    b->num_bytes = BULK_XFER_SIZE;
    b->timeout_ms = 0;
  }

  // Activate before submitting so the very first bytes are framed.
  dev.attached_ms = millis();
  adsb_source_usb_attached();
  for (auto &b : dev.bulk) {
    if (usb_host_transfer_submit(b) == ESP_OK)
      dev.inflight++;
    else
      log_e("ADS-B USB: bulk IN submit failed");
  }

  AdsbUsbInfo i = {};
  i.addr = addr;
  i.hub_port = info.parent.port_num;
  i.vid = dd->idVendor;
  i.pid = dd->idProduct;
  i.bcd = bcd;
  i.chip = dev.kind == dev.CDC      ? "CDC-ACM"
           : dev.kind == dev.CP210X ? "CP210x"
                                    : chip_name(bcd);
  i.speed = info.speed == USB_SPEED_HIGH   ? "high"
            : info.speed == USB_SPEED_FULL ? "full"
                                           : "low";
  i.ep_in = dev.ep_in;
  i.mps = dev.mps;
  i.baud = actual_baud;
  i.attached_ms = dev.attached_ms;
  str_desc_ascii(info.str_desc_manufacturer, i.manufacturer,
                 sizeof(i.manufacturer));
  str_desc_ascii(info.str_desc_product, i.product, sizeof(i.product));
  str_desc_ascii(info.str_desc_serial_num, i.serial, sizeof(i.serial));
  i.connected = true;
  usb_info = i;

  log_w("ADS-B USB: dongle connected addr %u (parent hub port %u), "
        "%04x:%04x %s bcd=0x%04x intf %u, %s speed, EP 0x%02x MPS %u, baud %d "
        "(requested %d), latency %d ms; UART input suspended",
        addr, info.parent.port_num, dd->idVendor, dd->idProduct, i.chip, bcd,
        dev.intf,
        info.speed == USB_SPEED_HIGH   ? "high"
        : info.speed == USB_SPEED_FULL ? "full"
                                       : "low",
        dev.ep_in, dev.mps, actual_baud, ADSB_USB_BAUD, FTDI_LATENCY_MS);
}

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *) {
  switch (msg->event) {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    xQueueSend(new_dev_queue, &msg->new_dev.address, 0);
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    if (msg->dev_gone.dev_hdl == dev.hdl)
      dev.gone = true;
    break;
  default:
    break;
  }
}

static void client_task(void *) {
  usb_host_client_config_t cfg = {};
  cfg.is_synchronous = false;
  cfg.max_num_event_msg = 8;
  cfg.async.client_event_callback = client_event_cb;
  cfg.async.callback_arg = nullptr;
  ESP_ERROR_CHECK(usb_host_client_register(&cfg, &client));

  for (;;) {
    usb_host_client_handle_events(client, pdMS_TO_TICKS(50));
    if (dev.hdl && dev.gone)
      ftdi_teardown("unplugged");
    uint8_t addr;
    while (xQueueReceive(new_dev_queue, &addr, 0) == pdTRUE)
      device_new(addr);
  }
}

static void lib_task(void *) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
      usb_host_device_free_all();
  }
}

// Required with CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK (Arduino
// prebuilt libs since 3.3.11): a NULL filter rejects every device.
static bool enum_filter(const usb_device_desc_t *, uint8_t *cfg_value) {
  *cfg_value = 1;
  return true;
}

void adsb_usb_setup() {
  new_dev_queue = xQueueCreate(8, sizeof(uint8_t));

  usb_host_config_t host_cfg = {};
  host_cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  host_cfg.enum_filter_cb = enum_filter;
  // peripheral_map = 0: default = high-speed OTG on the P4.
  esp_err_t err = usb_host_install(&host_cfg);
  if (err != ESP_OK) {
    log_e("ADS-B USB: usb_host_install failed: %s", esp_err_to_name(err));
    return;
  }
  xTaskCreate(lib_task, "usbh_lib", 4096, nullptr, 6, nullptr);
  xTaskCreate(client_task, "adsb_usb", 4096, nullptr, 5, nullptr);
  log_w("ADS-B USB host started, waiting for FTDI dongle (baud %d)",
        ADSB_USB_BAUD);
}

#endif // ADSB_UART && ADSB_USB
