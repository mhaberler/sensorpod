#if __has_include(<M5Unified.h>)
#define HAS_M5UNIFIED
#include <M5Unified.h>
#endif
#include "Adafruit_VL53L0X.h"
#include "button.hpp"
#include "credstore.hpp"
#include "deviceconfig.hpp"
#include "led.hpp"
#include "listenv.hpp"
#include "logging.hpp"
#include "mqtt_device.hpp"
#include <ArduinoJson.h>
#include <ImprovWiFiBLE.h>
#include <ImprovWiFiLibrary.h>
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <WiFi.h>
#include <Wire.h>
#include <optional>

#if __has_include("build_info.hpp")
#include "build_info.hpp"
#endif

// start BLE provisioning service only afeter wifi connect failed
// p4 takes longer time to get set up
#ifdef CONFIG_IDF_TARGET_ESP32P4
#define TIME_TO_CONNECT 15 * 1000
#else
#define TIME_TO_CONNECT 8 * 1000
#endif

#define DEVICE_NAME "sensorpod" // The name of the sensor
#define DURATION 200            // mS

int numClicks;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
bool lox_present;

#include "mdns_client.hpp"
#include "mqtt.hpp"
#include "mqtt_client.hpp"
ImprovWiFi improvSerial(&Serial);

extern String hostName;
bool is_broker_mode = true;
bool wifi_sleep_enabled = false;
// BLE options (deviceconfig.hpp), loaded at boot, live-updated by web UI
volatile uint8_t ble_decoder_mode = DeviceConfig::BLE_DECODER_THEENGS;
volatile bool ble_retain_undecoded = false;
volatile bool ble_dedup_enabled = true;
volatile uint32_t ble_dedup_age = 1;
static bool ble_scan_enabled = true; // boot-time gate, restart to change
// True while an Improv provisioning connect is in progress, so the STA
// reconnect watchdog in wifisetup.cpp stays out of Improv's way.
bool improv_provisioning = false;

void wifi_setup(void);
void wifi_loop(void);
void startStaAttempt(const String &ssid, const String &pass);
void cacheStaCredentials(const String &ssid, const String &pass);
void stopSta();
uint8_t safe_ap_station_num();
bool hosted_update_busy();
void blescanner_setup();
void blescanner_loop();
bool i2c_probe(TwoWire &w, uint8_t addr);
void i2c_scan(TwoWire &w);
bool lox_init(TwoWire &wire);
std::optional<uint16_t> lox_poll(TwoWire &wire);

void onImprovWiFiErrorCb(ImprovTypes::Error err) {
  improv_provisioning = false;
  String ssid, pass;
  if (loadWiFiCredentials(ssid, pass))
    logging_quiet_end();
  // else stay quiet — still waiting for successful provision
  log_e("Improv error %d", err);
  WiFi.setAutoReconnect(true);
  blinkLed(2000, 3);
}

void onImprovWiFiConnectedCb(const char *ssid, const char *password) {
  improv_provisioning = false;
  saveWiFiCredentials(ssid, password);
  cacheStaCredentials(ssid, password);
  logging_quiet_end();
  log_i("Improv provisioned ssid=%s", ssid);
  WiFi.setAutoReconnect(true);
  blinkLed(100, 3);
  startStaAttempt(String(ssid), String(password));
}

// Disable auto-reconnect before Improv's WiFi.begin() to prevent driver
// interference, then delegate to the library's default connect logic.
bool onImprovCustomConnect(const char *ssid, const char *password) {
  improv_provisioning = true;
  logging_quiet_begin();
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  delay(50);
  return improvSerial.tryConnectToWifi(ssid, password);
}

void startImprovSerialProvisioning() {
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "ImprovWiFiLib",
                             IMPROV_WIFI_LIBRARY_VERSION, hostName.c_str(),
                             "http://{LOCAL_IPV4}");
  improvSerial.onImprovError(onImprovWiFiErrorCb);
  improvSerial.onImprovConnected(onImprovWiFiConnectedCb);
  improvSerial.setCustomConnectWiFi(onImprovCustomConnect);
}

static String resolve_broker_host(const String &host) {
  if (host.indexOf(':') >= 0) // IPv6 literal - never append .local
    return host;
  if (host.indexOf('.') == -1)
    return host + ".local";
  return host;
}

void setup() {
#if defined(HAS_M5UNIFIED)
  auto cfg = M5.config();
  cfg.output_power = true;
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  // M5.Ex_I2C.begin();
  // Wire.end();
  // Wire.begin(M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL(), 100000);
#else
  Serial.begin(115200);
#endif
  // USB-Serial/JTAG (C6/H2/P4) blocks TX until a host attaches; wait briefly
  // so early logs are visible when a terminal is present, but boot headless.
  unsigned long serial_t0 = millis();
  while (!Serial && millis() - serial_t0 < 1500) {
    delay(10);
  }
  logging_setup();
  listEnv();
  hostName = WiFi.getHostname();
  hostName.toLowerCase();
  ledSetup();

  // Quiet Serial while waiting for Improv (no STA creds yet).
  {
    String ssid, pass;
    if (!loadWiFiCredentials(ssid, pass))
      logging_quiet_begin();
  }

  // Read device role early
  is_broker_mode = DeviceConfig::isBrokerMode();
  wifi_sleep_enabled = DeviceConfig::isWifiSleepEnabled();
  log_d("Device role: %s", is_broker_mode ? "Broker" : "Client");
  log_d("WiFi modem-sleep: %s", wifi_sleep_enabled ? "on" : "off");

  // BLE options
  ble_scan_enabled = DeviceConfig::isBleScanEnabled();
  ble_decoder_mode = DeviceConfig::getBleDecoder();
  ble_retain_undecoded = DeviceConfig::isBleRetainUndecoded();
  ble_dedup_enabled = DeviceConfig::isBleDedupEnabled();
  ble_dedup_age = DeviceConfig::getBleDedupAge();
  log_d("BLE: scan=%s decoder=%u retain=%s dedup=%s age=%us",
        ble_scan_enabled ? "on" : "off", ble_decoder_mode,
        ble_retain_undecoded ? "on" : "off", ble_dedup_enabled ? "on" : "off",
        (unsigned)ble_dedup_age);

#if !defined(HAS_M5UNIFIED)
  Wire.begin(SDA_PIN, SCL_PIN, 400000);
#endif
  startImprovSerialProvisioning();
  button_setup();
  // i2c_scan(Wire);
  lox_present = lox_init(Wire);
  wifi_setup();
  if (ble_scan_enabled)
    blescanner_setup();

  // Initialize MQTT device based on role
  if (is_broker_mode) {
    mqtt_device = (MQTTDevice *)&mqtt_broker;
    mqtt_device->begin();
    log_d("Broker mode initialized");
  } else {
    mqtt_device = (MQTTDevice *)&mqtt_client;
    mqtt_device->begin();

    // If broker is saved in Preferences, connect immediately
    String saved_broker = DeviceConfig::getSelectedBrokerHostname();
    if (saved_broker.length() > 0) {
      String resolved = resolve_broker_host(saved_broker);
      log_i("Client mode: connecting to saved broker %s", resolved.c_str());
      mqtt_client.connect(resolved.c_str(), 1883);
    }
    log_d("Client mode initialized");
  }

  blinkLed(100, 5);
}

void loop() {
  // ImprovWiFi::handleSerial() reads one byte per call — drain the full
  // RX buffer here. Never read Serial elsewhere (logging commands use
  // WebSerial only); otherwise log polling steals IMPROV frame bytes and
  // the client hangs on "Querying device".
  while (Serial.available() > 0)
    improvSerial.handleSerial();

  unsigned long now = millis();
  button_loop();
  if (ble_scan_enabled)
    blescanner_loop();
#if defined(HAS_M5UNIFIED)
  M5.update();
#endif
  static unsigned long last_lox_poll = 0;
  if (now - last_lox_poll > DURATION) {
    auto range = lox_poll(Wire);
    last_lox_poll = now;
    if (range.has_value()) {
      JsonDocument doc;
      doc["distance_mm"] = *range;
      String payload;
      serializeJson(doc, payload);
      mqtt_publish("VL53L0X", payload.c_str());
    }
  }

  static unsigned long lastStatusPublish = 0;
  // Skip while the hosted co-processor is being flashed - WiFi.RSSI() races
  // the RPC transport the flash uses (see wifisetup.cpp:
  // hosted_update_in_progress).
  if (!hosted_update_busy() && now - lastStatusPublish > 1000) {
    lastStatusPublish = now;
    JsonDocument doc;
    doc["uptime"] = now / 1000;
    doc["cpu_temperature"] = temperatureRead();
    doc["rssi"] = WiFi.RSSI();
    if (numClicks) {
      doc["clicks"] = numClicks;
      numClicks = 0;
    }
    String payload;
    serializeJson(doc, payload);
    mqtt_publish("status", payload.c_str());
  }

  if (mqtt_device) {
    mqtt_device->loop();
  }

  // Client mode: periodic mDNS discovery and failover (async, non-blocking)
  if (!is_broker_mode) {
    static unsigned long last_discovery = 0;
    // Kick off discovery if due or retries exhausted, but don't block
    if (!mdns_client.is_discovering() &&
        (mqtt_client.needs_rediscovery() || now - last_discovery > 10000)) {
      last_discovery = now;
      mqtt_client.clear_broker();
      mdns_client.start_async_discovery();
    }
    // Pick up results when available
    if (!mdns_client.is_discovering()) {
      auto brokers = mdns_client.get_last_brokers();
      if (!brokers.empty() && !mqtt_client.connected() &&
          !mqtt_client.has_pending()) {
        String resolved = resolve_broker_host(brokers[0].hostname);
        log_i("Client mode: connecting to discovered broker %s at %s:%u",
              brokers[0].instance_name.c_str(), resolved.c_str(),
              brokers[0].port);
        DeviceConfig::setSelectedBrokerHostname(resolved);
        mqtt_client.connect(resolved.c_str(), brokers[0].port);
      }
    }
  }

  // LED feedback: WiFi/broker connection status
  bool wifi_connected =
      (safe_ap_station_num() > 0) || (WiFi.status() == WL_CONNECTED);
  bool mqtt_connected = mqtt_device && mqtt_device->connected();

  LEDState led_state;
  if (!wifi_connected) {
    led_state = LED_FAST_BLINK; // Red/fast: WiFi down
  } else if (!mqtt_connected) {
    led_state = LED_SLOW_BLINK; // Orange/slow: Broker down
  } else {
    led_state = LED_SOLID; // Green: All OK
  }
  updateLed(led_state);
  ledLoop();

  wifi_loop();
  logging_loop();
  yield();
}

bool i2c_probe(TwoWire &w, uint8_t addr) {
  w.beginTransmission(addr);
  return (w.endTransmission() == 0);
}

void i2c_scan(TwoWire &w) {
  uint8_t bus = (&w == &Wire) ? 0 : 1;
  log_i("scanning Wire%u", bus);
  for (auto i = 8; i < 128; i++) {
    w.beginTransmission(i);
    if (w.endTransmission() == 0) {
      log_i("Wire%u dev at 0x%x", bus, i);
    }
  }
}

bool lox_init(TwoWire &wire) {
  bool lox_present = lox.begin();
  log_w("VL53L0X:%s detected", lox_present ? "" : " not");
  return lox_present;
}

std::optional<uint16_t> lox_poll(TwoWire &wire) {
  if (!lox_present)
    return std::nullopt;

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);
  if (measure.RangeStatus != 4) {
    // log_d("range = %u", measure.RangeMilliMeter);
    return measure.RangeMilliMeter;
  }
  return std::nullopt;
}
