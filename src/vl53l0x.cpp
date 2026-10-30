#include "vl53l0x.hpp"
#include "Adafruit_VL53L0X.h"

static Adafruit_VL53L0X lox = Adafruit_VL53L0X();
static bool lox_present;

bool lox_init(TwoWire &wire) {
  lox_present = lox.begin(VL53L0X_I2C_ADDR, false, &wire);
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
