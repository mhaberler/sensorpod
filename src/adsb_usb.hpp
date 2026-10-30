#pragma once

#include <stddef.h>
#include <stdint.h>

// USB-host ingest for the FTDI-based ADS-B dongle (ESP32-P4 OTG). While an
// FTDI device is attached it takes over as frame source from the UART.

void adsb_usb_setup();

// Implemented in adsb.cpp, called from the USB client task.
void adsb_source_usb_attached();
uint32_t adsb_source_usb_detached(); // returns frames framed this session
void adsb_feed_usb(const uint8_t *buf, size_t n);
