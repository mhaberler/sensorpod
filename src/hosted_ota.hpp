/// @file hosted_ota.hpp
/// @brief Force-flash ESP-Hosted co-processor firmware from an HTTPS URL.
///
/// Compiled only when BOARD_HAS_SDIO_ESP_HOSTED and ESP_HOSTED_DOWNGRADE
/// are both defined (P4 debug downgrade path).

#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

#if defined(BOARD_HAS_SDIO_ESP_HOSTED) && defined(ESP_HOSTED_DOWNGRADE)

/// Build Arduino CDN URL:
///   https://espressif.github.io/arduino-esp32/hosted/{target}-vM.m.p.bin
void buildHostedFwUrl(char *buf, size_t n, uint32_t maj, uint32_t min,
                      uint32_t pat);

/// True if CDN returns HTTP 200 for the firmware URL (HEAD).
bool hostedFwUrlExists(const char *url);

/// Pick oldest known Arduino CDN image that is still older than host.
bool findOlderHostedFw(uint32_t *maj, uint32_t *min, uint32_t *pat);

/// Download URL over HTTPS and flash the co-processor. No version gate.
bool flashEspHostedFromUrl(const char *url);

/// Arm a forced downgrade (runs from wifi_loop).
bool request_hosted_downgrade(uint32_t maj, uint32_t min, uint32_t pat);

#endif
