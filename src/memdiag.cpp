#include "memdiag.hpp"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <string.h>

#define MEMDIAG_MAGIC 0x4d454d44u // "MEMD"

struct RtcAllocFail {
  uint32_t magic;
  AllocFailInfo info;
};

// Survives panic / WDT / SW resets; garbage after power-on (magic check).
static RTC_NOINIT_ATTR RtcAllocFail rtc_fail;
static AllocFailInfo prev_fail; // copy of the previous boot's record
static char coredump_text[320];

static void copy_str(char *dst, size_t n, const char *src) {
  size_t i = 0;
  if (src)
    for (; i + 1 < n && src[i]; i++)
      dst[i] = src[i];
  dst[i] = 0;
}

// Runs in the context of the failing allocation - possibly ISR or with
// interrupts disabled: no logging framework, no allocation, no locks.
static void alloc_failed_hook(size_t size, uint32_t caps,
                              const char *function_name) {
  AllocFailInfo &f = rtc_fail.info;
  __atomic_fetch_add(&f.count, 1, __ATOMIC_RELAXED);
  f.size = size;
  f.caps = caps;
  f.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
  copy_str(f.task, sizeof(f.task),
           xPortInIsrContext() ? "ISR" : pcTaskGetName(nullptr));
  copy_str(f.func, sizeof(f.func), function_name);
  esp_rom_printf("ALLOC FAILED: %u bytes caps=0x%08x task=%s in %s\n",
                 (unsigned)size, (unsigned)caps, f.task, f.func);
}

// Digest of the coredump in flash, sanitized for direct HTML/JSON embedding.
static void read_coredump() {
  coredump_text[0] = 0;
  if (esp_core_dump_image_check() != ESP_OK)
    return;
  auto *s = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
  if (!s)
    return;
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    char reason[96] = "";
    esp_core_dump_get_panic_reason(reason, sizeof(reason));
    size_t n = snprintf(coredump_text, sizeof(coredump_text),
                        "task=%.16s pc=0x%08lx reason=%s", s->exc_task,
                        (unsigned long)s->exc_pc, reason[0] ? reason : "?");
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    if (n < sizeof(coredump_text))
      n += snprintf(coredump_text + n, sizeof(coredump_text) - n,
                    " bt%s=", s->exc_bt_info.corrupted ? "(corrupted)" : "");
    for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++)
      if (n < sizeof(coredump_text))
        n += snprintf(coredump_text + n, sizeof(coredump_text) - n, " 0x%08lx",
                      (unsigned long)s->exc_bt_info.bt[i]);
#else
    if (n < sizeof(coredump_text))
      snprintf(coredump_text + n, sizeof(coredump_text) - n,
               " ra=0x%08lx sp=0x%08lx mcause=0x%lx mtval=0x%08lx",
               (unsigned long)s->ex_info.ra, (unsigned long)s->ex_info.sp,
               (unsigned long)s->ex_info.mcause,
               (unsigned long)s->ex_info.mtval);
#endif
    for (char *p = coredump_text; *p; p++)
      if (*p == '"' || *p == '\\' || *p == '<' || *p == '>' || *p < ' ')
        *p = '_';
  }
  free(s);
}

void memdiag_init() {
  if (rtc_fail.magic == MEMDIAG_MAGIC &&
      esp_reset_reason() != ESP_RST_POWERON) {
    prev_fail = rtc_fail.info;
    prev_fail.task[sizeof(prev_fail.task) - 1] = 0;
    prev_fail.func[sizeof(prev_fail.func) - 1] = 0;
  }
  memset(&rtc_fail.info, 0, sizeof(rtc_fail.info));
  rtc_fail.magic = MEMDIAG_MAGIC;

  heap_caps_register_failed_alloc_callback(alloc_failed_hook);

  if (prev_fail.count)
    log_w("Previous boot: %u failed allocs, last %u bytes caps=0x%08x "
          "task=%s in %s at %us",
          (unsigned)prev_fail.count, (unsigned)prev_fail.size,
          (unsigned)prev_fail.caps, prev_fail.task, prev_fail.func,
          (unsigned)prev_fail.uptime_s);

  read_coredump();
  if (coredump_text[0])
    log_w("Coredump in flash: %s", coredump_text);
}

const AllocFailInfo &memdiag_alloc_fail_now() { return rtc_fail.info; }
const AllocFailInfo &memdiag_alloc_fail_prev() { return prev_fail; }
const char *memdiag_coredump() { return coredump_text; }
