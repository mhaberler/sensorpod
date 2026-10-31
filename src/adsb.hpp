#pragma once

#include <stdint.h>

#define ADSB_BAUD 921600

// Publish toggles, loaded from NVS in adsb_setup(), updated live by web UI
extern volatile bool adsb_publish_enabled; // ADSB/{icao} aircraft objects
extern volatile bool adsb_ihex_enabled;    // raw "*<hex>;" lines on ihex
extern volatile bool adsb_ihex_signal; // RTL-SDR frames as "+SS<hex>;" on ihex

void adsb_setup();
void adsb_loop();

struct AdsbStats {
  uint32_t frames_seen;       // '*...;' frames received from the module
  uint32_t crc_fail;          // frames dropped: bad Mode-S checksum
  uint32_t decoded;           // frames accepted (crcok, DF17/18)
  uint32_t dropped_lines;     // oversize/garbage frames (reader task)
  uint32_t dropped_overflow;  // ring buffer full (reader task)
  uint32_t aircraft_count;    // live entries in the aircraft table
  bool usb_active = false;    // USB FTDI dongle is the current frame source
  uint32_t usb_frames = 0;    // frames framed from the USB dongle
  bool rtlsdr_active = false; // RTL-SDR dongle is the current frame source
  uint32_t rtlsdr_frames = 0; // frames demodulated by the RTL-SDR
  uint32_t uart_bytes;        // bytes read from the UART (incl. discarded)
  uint32_t uart_frames;       // frames framed from the UART
  uint32_t uart_last_rx_ms;   // millis() of last UART byte, 0 = never
};

AdsbStats adsb_get_stats();
