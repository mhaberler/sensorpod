#include "i2c.hpp"
#include <esp32-hal-i2c.h>

std::vector<TwoWire *> i2c_buses() {
  std::vector<TwoWire *> buses = {&Wire};
  // mirror the Wire1 guard in Wire.h
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
#if SOC_I2C_NUM > 1
  buses.push_back(&Wire1);
#endif
#else
#if SOC_HP_I2C_NUM > 1
  buses.push_back(&Wire1);
#endif
#endif
  return buses;
}

bool i2c_bus_started(TwoWire &w) { return i2cIsInit(w.getBusNum()); }

bool i2c_probe(TwoWire &w, uint8_t addr) {
  w.beginTransmission(addr);
  return (w.endTransmission() == 0);
}

std::vector<uint8_t> i2c_scan(TwoWire &w) {
  std::vector<uint8_t> found;
  uint8_t bus = w.getBusNum();
  log_i("scanning Wire%u", bus);
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (i2c_probe(w, addr)) {
      log_i("Wire%u dev at 0x%02x", bus, addr);
      found.push_back(addr);
    }
  }
  return found;
}
