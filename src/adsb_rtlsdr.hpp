#pragma once

#include <stddef.h>
#include <stdint.h>

// RTL-SDR (RTL2832U + R820T/R828D) ADS-B source on the ESP32-P4 USB host:
// 1090 MHz, 2 MS/s I/Q, demodulated on-chip (libmodes mode_s_detect).
// While a dongle is attached it takes over as frame source from the UART.

// Tuner gain in tenths of a dB; -1 = highest gain the tuner supports.
#ifndef ADSB_RTLSDR_GAIN
#define ADSB_RTLSDR_GAIN -1
#endif
// Bias-tee (antenna/LNA power) on at attach. The Blog V4 may force it on
// via EEPROM regardless.
#ifndef ADSB_RTLSDR_BIAS_TEE
#define ADSB_RTLSDR_BIAS_TEE 0
#endif

// Registers a second USB host client; call after adsb_usb_setup().
void adsb_rtlsdr_setup();

// Attached dongle properties + traffic, for the web UI.
struct AdsbRtlsdrInfo {
  bool connected;
  uint8_t addr;
  uint16_t vid, pid;
  char model[64]; // "<manufacturer> <product> (<tuner>)"
  int gain;       // tenths of a dB
  bool bias_tee;
  uint32_t rate;
  uint32_t attached_ms;
  uint32_t bytes;       // I/Q bytes read, all sessions
  uint32_t read_errors; // failed/short bulk reads, all sessions
  uint32_t frames;      // DF17/18 frames demodulated, all sessions
};
AdsbRtlsdrInfo adsb_rtlsdr_info();

// Implemented in adsb.cpp, called from the RTL-SDR task.
void adsb_source_rtlsdr_attached();
uint32_t adsb_source_rtlsdr_detached(); // returns frames framed this session
void adsb_feed_rtlsdr(const uint8_t *buf, size_t n);
