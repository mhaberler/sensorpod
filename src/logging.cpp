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

static String serialLine;
static String pendingWsCommand;
static bool wsCommandPending = false;

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
  return ARDUHAL_LOG_LEVEL_ERROR;
}

static void hal_log_char_filtered(char c) {
  if (halLogBusy)
    return;
  if (halLineLen + 1 < sizeof(halLineBuf))
    halLineBuf[halLineLen++] = c;
  if (c != '\n' && halLineLen + 1 < sizeof(halLineBuf))
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
  // Replaces default UART putc for Arduino log_printf — re-emit to Serial
  // only when the line's level is within the current Mycila level.
  ets_install_putc1(hal_log_char_filtered);
}

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

uint8_t logging_current_level() { return logger.getLevel(); }

bool logging_apply_level(uint8_t level) {
  if (level > ARDUHAL_LOG_LEVEL_VERBOSE)
    return false;
  logger.setLevel(level);
  apply_esp_log_level(level);
  return DeviceConfig::setLogLevel(level);
}

bool logging_apply_level_name(const char *name) {
  uint8_t level = 0;
  if (!logging_parse_level(name, &level))
    return false;
  return logging_apply_level(level);
}

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
    char buf[64];
    snprintf(buf, sizeof(buf), "[LOGGER] Log level is %s",
             logging_level_name(logger.getLevel()));
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

static void pollSerialInput() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      serialLine.trim();
      if (!serialLine.isEmpty())
        handleLogCommand(serialLine.c_str(), "serial");
      serialLine = "";
    } else {
      serialLine += c;
      if (serialLine.length() > 120)
        serialLine.remove(0, serialLine.length() - 120);
    }
  }
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
  pollSerialInput();
}
