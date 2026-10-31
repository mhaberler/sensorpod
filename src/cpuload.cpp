#include "cpuload.hpp"

#include <Arduino.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

// Needs CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS + USE_TRACE_FACILITY (set in
// the prebuilt Arduino libs for P4 and C6). Counters are 32-bit us and wrap
// every ~71.6 min; unsigned deltas over a 5 s window are wrap-safe.

static CpuLoad load;

struct Prev {
  TaskHandle_t handle;
  uint32_t counter;
};
static Prev prev[CPULOAD_MAX_TASKS];
static int nprev;
static uint32_t prev_total;
static uint32_t last_ms;

const CpuLoad &cpuload_get() { return load; }

bool cpuload_loop() {
  uint32_t now = millis();
  if (nprev && now - last_ms < CPULOAD_INTERVAL_MS)
    return false;
  last_ms = now;

  static TaskStatus_t st[CPULOAD_MAX_TASKS];
  uint32_t total = 0;
  UBaseType_t n = uxTaskGetSystemState(st, CPULOAD_MAX_TASKS, &total);
  if (n == 0) // more tasks than CPULOAD_MAX_TASKS
    return false;

  uint32_t dt = total - prev_total;
  bool have_prev = nprev > 0 && dt > 0;
  if (have_prev) {
    load.cores = portNUM_PROCESSORS;
    load.window_ms = dt / 1000;
    load.ntasks = 0;
    for (int c = 0; c < 2; c++)
      load.core[c] = 0;
    for (UBaseType_t i = 0; i < n; i++) {
      uint32_t before = st[i].ulRunTimeCounter; // new task: whole counter
      for (int k = 0; k < nprev; k++)
        if (prev[k].handle == st[i].xHandle) {
          before = prev[k].counter;
          break;
        }
      float pct = 100.0f * (uint32_t)(st[i].ulRunTimeCounter - before) / dt;
      for (int c = 0; c < portNUM_PROCESSORS && c < 2; c++)
        if (st[i].xHandle == xTaskGetIdleTaskHandleForCore(c))
          load.core[c] = 100.0f - pct;
      CpuTask &t = load.tasks[load.ntasks++];
      strlcpy(t.name, st[i].pcTaskName, sizeof(t.name));
      t.core = st[i].xCoreID == tskNO_AFFINITY ? -1 : st[i].xCoreID;
      t.prio = st[i].uxCurrentPriority;
      t.pct = pct;
      t.stack = st[i].usStackHighWaterMark; // StackType_t = byte on ESP-IDF
    }
    std::sort(load.tasks, load.tasks + load.ntasks,
              [](const CpuTask &a, const CpuTask &b) { return a.pct > b.pct; });
    load.valid = true;
  }

  nprev = n;
  for (UBaseType_t i = 0; i < n; i++)
    prev[i] = {st[i].xHandle, st[i].ulRunTimeCounter};
  prev_total = total;
  return have_prev;
}
