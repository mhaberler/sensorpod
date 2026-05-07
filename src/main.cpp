#if defined(M5UNIFIED)
#include <M5Unified.h>
#endif
#include "Adafruit_VL53L0X.h"
#include "BTHome.h"
#include <ArduinoJson.h>
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <WiFi.h>
#include <Wire.h>
#include <optional>

BTHome bthome;

#define DEVICE_NAME "sensorpod" // The name of the sensor
#define DURATION 200            // mS

int numClicks;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
bool lox_present;

extern PicoMQTT::Server mqtt;
extern uint8_t wifi_status;

void wifi_setup(void);
void wifi_loop(void);
void button_setup(void);
void button_loop(void);
bool i2c_probe(TwoWire &w, uint8_t addr);
void i2c_scan(TwoWire &w);
bool lox_init(TwoWire &wire);
std::optional<uint16_t> lox_poll(TwoWire &wire);

void setup() {
  Serial.begin(115200);
  delay(3000);
#if defined(M5UNIFIED)
  auto cfg = M5.config();
  cfg.output_power = true;
  M5.begin(cfg);
  M5.Ex_I2C.begin();
  Wire.end();
  Wire.begin(M5.Ex_I2C.getSDA(), M5.Ex_I2C.getSCL(), 100000);
#else
  Wire.begin();
#endif
  button_setup();
  i2c_scan(Wire);
  lox_present = lox_init(Wire);
  wifi_setup();
  bthome.begin(HOSTNAME, false, "", true);
  // bthome.setDeviceName(HOSTNAME);
}

void loop() {
  unsigned long now = millis();
  button_loop();
#if defined(M5UNIFIED)
  M5.update();
#endif
  static unsigned long last_lox_poll = 0;
  if (now - last_lox_poll > DURATION) {
    auto range = lox_poll(Wire);
    last_lox_poll = now;
    if (range.has_value()) {
      JsonDocument doc;
      doc["distance_mm"] = *range;
      auto publish = mqtt.begin_publish("VL53L0X", measureJson(doc));
      serializeJson(doc, publish);
      publish.send();

      bthome.stop();
      bthome.resetMeasurement();
      switch (numClicks) {
      default:
        break;
      case 1:
        log_i("add 1 click");
        bthome.addMeasurement_state(EVENT_BUTTON, EVENT_BUTTON_PRESS);
        break;
      case 2:
        log_i("add 2 clicks");
        bthome.addMeasurement_state(EVENT_BUTTON, EVENT_BUTTON_DOUBLE_PRESS);
        break;
      case 3:
        log_i("add 3 clicks");
        bthome.addMeasurement_state(EVENT_BUTTON, EVENT_BUTTON_TRIPLE_PRESS);
        break;
      }

      bthome.addMeasurement(ID_DISTANCE, (uint64_t)*range);

      bthome.buildPacket();
      bthome.start(DURATION);
    } else {
      numClicks = 0;
      log_i("bthome.stop");

      bthome.stop();
    }
  }

  static unsigned long lastStatusPublish = 0;
  if (now - lastStatusPublish > 1000) {
    lastStatusPublish = now;
    JsonDocument doc;
    doc["uptime"] = now / 1000;
    doc["cpu_temperature"] = temperatureRead();
    doc["rssi"] = WiFi.RSSI();

    auto publish = mqtt.begin_publish("status", measureJson(doc));
    serializeJson(doc, publish);
    publish.send();
  }

  mqtt.loop();
  wifi_loop();
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
    log_d("range = %u", measure.RangeMilliMeter);
    return measure.RangeMilliMeter;
  }
  return std::nullopt;
}

#if defined(BUTTON_PIN)

#include "OneButton.h"
OneButton button(BUTTON_PIN, true,
                 true); // Button pin, active low, pullup enabled

void singleClick() {
  log_i("singleClick() detected.");
  numClicks = 1;
}

void doubleClick() {
  log_i("doubleClick() detected.");
  numClicks = 2;
}

void multiClick() {
  int n = button.getNumberClicks();
  log_i("%d clicks detected.", n);
  numClicks = n;
}

#endif

void button_setup(void) {
#if defined(BUTTON_PIN)
  button.attachClick(singleClick);
  button.attachDoubleClick(doubleClick);
  button.attachMultiClick(multiClick);
#endif
}

void button_loop(void) {
#if defined(BUTTON_PIN)
  button.tick();
#endif
}
