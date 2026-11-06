#pragma once

#include <stdint.h>

// Heap / crash diagnostics, identical on all targets.
//  - failed-alloc hook: every failed heap_caps allocation is printed
//    (esp_rom_printf - hook may run in ISR context) and recorded in RTC
//    no-init memory, so the record of the boot that crashed survives the
//    panic reset and is reported on the next boot.
//  - coredump summary: one-line digest of the coredump stored in flash
//    (task, PC, panic reason, backtrace). The full ELF stays in flash for
//    `espcoredump.py`; it is overwritten by the next crash, never erased.

struct AllocFailInfo {
  uint32_t count;    // failed allocations
  uint32_t size;     // last failed request, bytes
  uint32_t caps;     // last failed MALLOC_CAP_* mask
  uint32_t uptime_s; // when the last one happened
  char task[16];     // task (or "ISR") that made the last failed request
  char func[24];     // heap API that failed (heap_caps_malloc, ...)
};

void memdiag_init();                            // call early in setup()
const AllocFailInfo &memdiag_alloc_fail_now();  // this boot
const AllocFailInfo &memdiag_alloc_fail_prev(); // previous boot (count 0: none)
const char *memdiag_coredump(); // "" when no valid coredump in flash
