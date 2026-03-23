#include "i2cio.hpp"
#include "Adafruit_VL53L0X.h"
#include "ArduinoJson.h"
#include <PicoMQTT.h>
#include <Wire.h>

extern PicoMQTT::Server mqtt;

Adafruit_VL53L0X lox = Adafruit_VL53L0X();
bool lox_present = false;

bool i2c_probe(TwoWire &w, uint8_t addr) {
  w.beginTransmission(addr);
  return (w.endTransmission() == 0);
}

void i2c_scan(TwoWire &w) {
  uint8_t bus = (&w == &Wire) ? 0 : 1;
  log_i("scanning Wire%u", bus);
  for (auto i = 0; i < 128; i++) {
    w.beginTransmission(i);
    if (w.endTransmission() == 0) {
      log_i("Wire%u dev at 0x%x", bus, i);
    }
  }
}

void i2c_init(TwoWire &wire) {
  i2c_scan(wire);

  lox_present = lox.begin();
  log_w("VL53L0X:%s detected", lox_present ? "" : " not");
}

void i2c_poll(TwoWire &wire) {

  if (!lox_present)
    return;

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);
  JsonDocument doc;
  doc["distance_mm"] = measure.RangeStatus != 4 ? measure.RangeMilliMeter : NAN;
  auto publish = mqtt.begin_publish("VL53L0X", measureJson(doc));
  serializeJson(doc, publish);
  publish.send();
}
