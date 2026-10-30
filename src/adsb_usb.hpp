#pragma once

#include <stddef.h>
#include <stdint.h>

// USB-host ingest for the FTDI-based ADS-B dongle (ESP32-P4 OTG). While an
// FTDI device is attached it takes over as frame source from the UART.

void adsb_usb_setup();

// Attached dongle properties + traffic, for the web UI.
struct AdsbUsbInfo {
  bool connected;
  uint8_t addr, hub_port;
  uint16_t vid, pid, bcd;
  const char *chip;
  const char *speed;
  uint8_t ep_in;
  uint16_t mps;
  int baud; // actual, after divisor rounding
  uint32_t attached_ms;
  char manufacturer[32], product[32], serial[32];
  uint32_t bytes; // payload bytes received, all sessions
};
AdsbUsbInfo adsb_usb_info();

// Implemented in adsb.cpp, called from the USB client task.
void adsb_source_usb_attached();
uint32_t adsb_source_usb_detached(); // returns frames framed this session
void adsb_feed_usb(const uint8_t *buf, size_t n);
