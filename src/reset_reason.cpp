#include <Arduino.h>
#include "reset_reason.hpp"

static int g_reset_reason_code = (int)ESP_RST_UNKNOWN;
static const char *g_reset_reason_name = "UNKNOWN";

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return "POWERON";
  case ESP_RST_EXT:
    return "EXT";
  case ESP_RST_SW:
    return "SW";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT_WDT";
  case ESP_RST_TASK_WDT:
    return "TASK_WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_SDIO:
    return "SDIO";
  case ESP_RST_USB:
    return "USB";
  case ESP_RST_JTAG:
    return "JTAG";
  case ESP_RST_EFUSE:
    return "EFUSE";
  case ESP_RST_PWR_GLITCH:
    return "PWR_GLITCH";
  case ESP_RST_CPU_LOCKUP:
    return "CPU_LOCKUP";
  case ESP_RST_UNKNOWN:
  default:
    return "UNKNOWN";
  }
}

static bool resetReasonIsWarning(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_PANIC:
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
  case ESP_RST_BROWNOUT:
  case ESP_RST_CPU_LOCKUP:
    return true;
  default:
    return false;
  }
}

void reset_reason_capture_and_log() {
  const esp_reset_reason_t reason = esp_reset_reason();
  g_reset_reason_code = (int)reason;
  g_reset_reason_name = resetReasonName(reason);
  if (resetReasonIsWarning(reason))
    log_w("Reset reason: %d (%s)", g_reset_reason_code, g_reset_reason_name);
  else
    log_i("Reset reason: %d (%s)", g_reset_reason_code, g_reset_reason_name);
}

int reset_reason_code() { return g_reset_reason_code; }

const char *reset_reason_name() { return g_reset_reason_name; }
