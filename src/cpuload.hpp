#pragma once

#include <stdint.h>

// CPU load from FreeRTOS run-time stats (esp_timer, 1 us): every
// CPULOAD_INTERVAL_MS a uxTaskGetSystemState() snapshot is diffed against
// the previous one. Per-core load = 100 % - idle task share of that core;
// per-task load is in % of one core (100 % = one core fully busy).

#define CPULOAD_INTERVAL_MS 5000
#define CPULOAD_MAX_TASKS 48

struct CpuTask {
  char name[16];
  int8_t core; // -1 = not pinned
  uint8_t prio;
  float pct;      // % of one core over the last interval
  uint32_t stack; // stack high-water mark (bytes never used)
};

struct CpuLoad {
  bool valid; // false until two snapshots exist
  uint8_t cores;
  float core[2]; // % busy per core
  uint32_t window_ms;
  uint16_t ntasks; // sorted by pct, descending
  CpuTask tasks[CPULOAD_MAX_TASKS];
};

// Call from loop(); returns true when a new interval was computed.
bool cpuload_loop();
const CpuLoad &cpuload_get();
