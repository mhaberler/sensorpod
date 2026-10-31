#pragma once

#include <stddef.h>
#include <stdint.h>

// USB-host ingest for an ADS-B "*<hex>;" line source (ESP32-P4 OTG): FTDI
// dongle, CP210x bridge or any CDC-ACM device (e.g. an ESP32-C6 over
// USB-Serial/JTAG). While attached it takes over as frame source from the UART.

void adsb_usb_setup();

// Attached dongle properties + traffic, for the web UI.
struct AdsbUsbInfo {
  bool connected;
  uint8_t addr, hub_port;
  uint16_t vid, pid, bcd;
  const char *chip; // FTDI chip name, "CP210x" or "CDC-ACM"
  const char *speed;
  uint8_t ep_in;
  uint16_t mps;
  int baud; // FTDI: actual; CP210x: requested; CDC-ACM: 0
  uint32_t attached_ms;
  char manufacturer[32], product[32], serial[32];
  uint32_t bytes; // payload bytes received, all sessions
};
AdsbUsbInfo adsb_usb_info();

// Implemented in adsb.cpp, called from the USB client task.
void adsb_source_usb_attached();
uint32_t adsb_source_usb_detached(); // returns frames framed this session
void adsb_feed_usb(const uint8_t *buf, size_t n);
