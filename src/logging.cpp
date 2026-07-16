#include "logging.hpp"
#include "deviceconfig.hpp"

#include <esp_log.h>

Mycila::Logger logger;
WebSerial webSerial;

struct LogLevelEntry {
  const char *name;
  uint8_t level;
};

static constexpr LogLevelEntry kLogLevels[] = {
    {"none", ARDUHAL_LOG_LEVEL_NONE},   {"error", ARDUHAL_LOG_LEVEL_ERROR},
    {"warn", ARDUHAL_LOG_LEVEL_WARN},   {"info", ARDUHAL_LOG_LEVEL_INFO},
    {"debug", ARDUHAL_LOG_LEVEL_DEBUG}, {"verbose", ARDUHAL_LOG_LEVEL_VERBOSE},
};

static String pendingWsCommand;
static bool wsCommandPending = false;

// Improv quiet window: clamp runtime level to NONE without touching NVS.
static bool quiet_active = false;
static uint8_t level_before_quiet = ARDUHAL_LOG_LEVEL_INFO;

// Arduino HAL / ets putc path: fixed line buffer (NO heap — putc can run
// re-entrantly during String::realloc and StreamString would poison the heap).
// Emit to Serial only (never WebSerial).
static char halLineBuf[MYCILA_LOGGER_BUFFER_SIZE];
static size_t halLineLen = 0;
static bool halLogBusy = false;

static uint8_t level_from_arduino_line(const char *line) {
  // ARDUHAL_LOG_FORMAT (optional ANSI color prefix):
  //   "[%6u][I][file:line] func(): ..."
  for (const char *p = line; *p; ++p) {
    if (p[0] == ']' && p[1] == '[' && p[2] != '\0' && p[3] == ']') {
      switch (p[2]) {
      case 'E':
        return ARDUHAL_LOG_LEVEL_ERROR;
      case 'W':
        return ARDUHAL_LOG_LEVEL_WARN;
      case 'I':
        return ARDUHAL_LOG_LEVEL_INFO;
      case 'D':
        return ARDUHAL_LOG_LEVEL_DEBUG;
      case 'V':
        return ARDUHAL_LOG_LEVEL_VERBOSE;
      default:
        break;
      }
    }
  }
  // Unparseable / partial line — drop (do not default to ERROR; that leaked
  // truncated verbose lines through a warn/info filter).
  return ARDUHAL_LOG_LEVEL_NONE;
}

static void hal_log_char_filtered(char c) {
  if (halLogBusy)
    return;
  // Leave one byte for the NUL used by level_from_arduino_line().
  constexpr size_t kCap = sizeof(halLineBuf) - 1;
  if (halLineLen < kCap)
    halLineBuf[halLineLen++] = c;
  // Flush on newline or when the payload area is full (truncated line).
  if (c != '\n' && halLineLen < kCap)
    return;

  halLineBuf[halLineLen] = '\0';
  const uint8_t line_level = level_from_arduino_line(halLineBuf);
  const uint8_t max_level = logger.getLevel();
  if (max_level != ARDUHAL_LOG_LEVEL_NONE &&
      line_level > ARDUHAL_LOG_LEVEL_NONE && line_level <= max_level) {
    halLogBusy = true;
    Serial.write(reinterpret_cast<const uint8_t *>(halLineBuf), halLineLen);
    halLogBusy = false;
  }
  halLineLen = 0;
}

static esp_log_level_t to_esp_log_level(uint8_t level) {
  switch (level) {
  case ARDUHAL_LOG_LEVEL_NONE:
    return ESP_LOG_NONE;
  case ARDUHAL_LOG_LEVEL_ERROR:
    return ESP_LOG_ERROR;
  case ARDUHAL_LOG_LEVEL_WARN:
    return ESP_LOG_WARN;
  case ARDUHAL_LOG_LEVEL_INFO:
    return ESP_LOG_INFO;
  case ARDUHAL_LOG_LEVEL_DEBUG:
    return ESP_LOG_DEBUG;
  case ARDUHAL_LOG_LEVEL_VERBOSE:
  default:
    return ESP_LOG_VERBOSE;
  }
}

static void apply_esp_log_level(uint8_t level) {
  esp_log_level_set("*", to_esp_log_level(level));
}

static void install_hal_log_filter() {
  halLineLen = 0;
  // Arduino log_printf → ets_printf calls putc1 and putc2. USB CDC boards
  // (S3/C6/P4, …) install an unfiltered CDC writer on putc2 in
  // HWCDC::setDebugOutput(), so a putc1-only filter still lets every
  // log_v through — e.g. NeoPixel RMT spam that wedges headless CDC TX.
  // Route all HAL chars through our filter; re-emit via Serial.write.
  ets_install_putc1(hal_log_char_filtered);
  ets_install_putc2(NULL);
}

void logging_cdc_harden() {
  // TinyUSB USBCDC and HWCDC both expose these. Safe before/after begin():
  // setTxTimeoutMs only updates the timeout static/member.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
  // Framework begin() may install unfiltered putc2; kill it and keep it off.
  Serial.setDebugOutput(false);
#endif
  ets_install_putc2(NULL);
}

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
// Best-effort: run as soon as this TU's static ctors run (after USBSerial
// exists). setup() still re-hardens around M5.begin.
namespace {
struct EarlyCdcHarden {
  EarlyCdcHarden() { logging_cdc_harden(); }
};
static EarlyCdcHarden early_cdc_harden;
} // namespace
#endif

const char *logging_level_name(uint8_t level) {
  for (const LogLevelEntry &entry : kLogLevels) {
    if (entry.level == level)
      return entry.name;
  }
  return "unknown";
}

bool logging_parse_level(const char *name, uint8_t *out) {
  if (!name || !out)
    return false;
  for (const LogLevelEntry &entry : kLogLevels) {
    if (strcmp(name, entry.name) == 0) {
      *out = entry.level;
      return true;
    }
  }
  return false;
}

uint8_t logging_current_level() {
  // Configured preference (NVS), not the temporary quiet clamp.
  return DeviceConfig::getLogLevel();
}

void logging_set_level_runtime(uint8_t level) {
  if (level > ARDUHAL_LOG_LEVEL_VERBOSE)
    return;
  logger.setLevel(level);
  apply_esp_log_level(level);
}

bool logging_apply_level(uint8_t level) {
  if (level > ARDUHAL_LOG_LEVEL_VERBOSE)
    return false;
  if (!DeviceConfig::setLogLevel(level))
    return false;
  if (quiet_active) {
    // Keep Serial quiet for Improv; remember the new preference for restore.
    level_before_quiet = level;
  } else {
    logging_set_level_runtime(level);
  }
  return true;
}

bool logging_apply_level_name(const char *name) {
  uint8_t level = 0;
  if (!logging_parse_level(name, &level))
    return false;
  return logging_apply_level(level);
}

void logging_quiet_begin() {
  if (quiet_active)
    return;
  level_before_quiet = DeviceConfig::getLogLevel();
  quiet_active = true;
  // NONE: any Serial TX (even ERROR) corrupts binary Improv framing.
  logging_set_level_runtime(ARDUHAL_LOG_LEVEL_NONE);
}

void logging_quiet_end() {
  if (!quiet_active)
    return;
  quiet_active = false;
  // Prefer NVS (may have been updated during quiet) over the snapshot.
  logging_set_level_runtime(DeviceConfig::getLogLevel());
}

bool logging_is_quiet() { return quiet_active; }

static void ackTo(const char *source, const char *msg) {
  if (strcmp(source, "webserial") == 0)
    webSerial.println(msg);
  else
    Serial.println(msg);
}

static String normalizeCommand(const char *raw) {
  String cmd(raw);
  cmd.trim();
  cmd.toLowerCase();
  return cmd;
}

static void printHelp(const char *source) {
  ackTo(source, "[LOGGER] Commands:");
  ackTo(source, "  level | level none|error|warn|info|debug|verbose");
  ackTo(source, "  help");
}

static void handleLogCommand(const char *raw, const char *source) {
  const String cmd = normalizeCommand(raw);
  if (cmd == "level") {
    char buf[80];
    snprintf(buf, sizeof(buf), "[LOGGER] Log level is %s%s",
             logging_level_name(logger.getLevel()),
             quiet_active ? " (quiet for Improv; NVS preference preserved)"
                          : "");
    ackTo(source, buf);
    return;
  }
  if (cmd.startsWith("level ")) {
    const char *name = cmd.c_str() + 6;
    uint8_t level = 0;
    if (!logging_parse_level(name, &level)) {
      ackTo(source, "[LOGGER] Unknown level. Use: none | error | warn | info "
                    "| debug | verbose");
      return;
    }
    if (!logging_apply_level(level)) {
      ackTo(source, "[LOGGER] Failed to save log level");
      return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "[LOGGER] Log level set to %s",
             logging_level_name(level));
    ackTo(source, buf);
    return;
  }
  if (cmd == "help") {
    printHelp(source);
    return;
  }
  printHelp(source);
  ackTo(source, "[LOGGER] Unknown command.");
}

void logging_setup() {
  const uint8_t level = DeviceConfig::getLogLevel();
  logger.setLevel(level);
  apply_esp_log_level(level);
  // Do NOT use logger.redirectArduinoLogs() — it fans HAL logs to every
  // Mycila output (including WebSerial) with no level filter, which (a) makes
  // `level error` look ignored and (b) breaks WebSocket clients.
  install_hal_log_filter();
  webSerial.setBuffer(MYCILA_LOGGER_BUFFER_SIZE);
  webSerial.onMessage([](const std::string &msg) {
    pendingWsCommand = msg.c_str();
    wsCommandPending = true;
  });
  logger.getOutputs().clear();
  logger.forwardTo(&Serial);
  logger.forwardTo(&webSerial);
  log_i("logging ready (level=%s)", logging_level_name(level));
}

void logging_begin(WebServer &server) { webSerial.begin(server); }

void logging_loop() {
  webSerial.handleClient();
  if (wsCommandPending) {
    wsCommandPending = false;
    const String cmd = pendingWsCommand;
    pendingWsCommand = "";
    handleLogCommand(cmd.c_str(), "webserial");
  }
  // Do not read Serial here — Improv owns the USB/UART RX path.
}
