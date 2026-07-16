#pragma once

#include <Arduino.h>
#include <MycilaLogger.h>
#include <MycilaWebSerial.h>
#include <WebServer.h>
#include <esp32-hal-log.h>

extern Mycila::Logger logger;
extern WebSerial webSerial;

void logging_setup();
// Call immediately after Serial / M5.begin — non-blocking CDC TX + drop
// unfiltered putc2 before any HAL log can wedge a headless USB host.
void logging_cdc_harden();
void logging_begin(WebServer &server);
void logging_loop();

// Apply level to MycilaLogger and persist to NVS. Returns false on bad name
// or NVS failure.
bool logging_apply_level_name(const char *name);
bool logging_apply_level(uint8_t level);
// Runtime only (no NVS) — used for temporary Improv quiet window.
void logging_set_level_runtime(uint8_t level);
// Mute to NONE (RAM only) while Improv needs a clean Serial pipe; restore
// later. Log commands stay on WebSerial — Serial RX is owned by Improv.
void logging_quiet_begin();
void logging_quiet_end();
bool logging_is_quiet();
const char *logging_level_name(uint8_t level);
bool logging_parse_level(const char *name, uint8_t *out);
uint8_t logging_current_level();

// Redirect Arduino HAL log_* (libraries) via filtered putc → Serial only.
// App log_* macros route through Mycila::Logger → Serial + WebSerial.
#undef log_e
#undef log_w
#undef log_i
#undef log_d
#undef log_v
#ifndef LOG_TAG
#define LOG_TAG "sensorpod"
#endif
#define log_e(fmt, ...) logger.error(LOG_TAG, fmt, ##__VA_ARGS__)
#define log_w(fmt, ...) logger.warn(LOG_TAG, fmt, ##__VA_ARGS__)
#define log_i(fmt, ...) logger.info(LOG_TAG, fmt, ##__VA_ARGS__)
#define log_d(fmt, ...) logger.debug(LOG_TAG, fmt, ##__VA_ARGS__)
#define log_v(fmt, ...)                                                        \
  logger.log(ARDUHAL_LOG_LEVEL_VERBOSE, LOG_TAG, fmt, ##__VA_ARGS__)
