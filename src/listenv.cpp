#include <Arduino.h>

#if __has_include("build_info.hpp")
#include "build_info.hpp"
#endif

void listEnv() {
  log_i("=== Build Environment ===");

#ifdef BUILD_SHA
  log_i("BUILD_SHA:         %s", BUILD_SHA);
#endif

#ifdef BUILD_DATE
  log_i("BUILD_DATE:        %s", BUILD_DATE);
#endif

#ifdef BUILD_ENV
  log_i("BUILD_ENV:         %s", BUILD_ENV);
#endif

#ifdef BUILD_BOARD
  log_i("BUILD_BOARD:       %s", BUILD_BOARD);
#endif

#ifdef BUILD_BOARD_NAME
  log_i("BUILD_BOARD_NAME:  %s", BUILD_BOARD_NAME);
#endif

#ifdef BUILD_MCU
  log_i("BUILD_MCU:         %s", BUILD_MCU);
#endif

#ifdef BUILD_VARIANT
  log_i("BUILD_VARIANT:     %s", BUILD_VARIANT);
#endif

#ifdef BUILD_TYPE
  log_i("BUILD_TYPE:        %s", BUILD_TYPE);
#endif

#ifdef BUILD_PARTITIONS
  log_i("BUILD_PARTITIONS:  %s", BUILD_PARTITIONS);
#endif

#ifdef BUILD_FLASH_SIZE
  log_i("BUILD_FLASH_SIZE:  %s", BUILD_FLASH_SIZE);
#endif

#ifdef BUILD_FRAMEWORK
  log_i("BUILD_FRAMEWORK:   %s", BUILD_FRAMEWORK);
#endif

#ifdef BUILD_REPO
  log_i("BUILD_REPO:        %s", BUILD_REPO);
#endif

#ifdef BUILD_TAG
  log_i("BUILD_TAG:         %s", BUILD_TAG);
#endif

#ifdef BUILD_FIRMWARE_URI
  log_i("BUILD_FIRMWARE_URI: %s", BUILD_FIRMWARE_URI);
#endif
}
