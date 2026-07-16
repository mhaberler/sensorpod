#pragma once

#include <esp_system.h>

const char *resetReasonName(esp_reset_reason_t reason);
void reset_reason_capture_and_log();
int reset_reason_code();
const char *reset_reason_name();
