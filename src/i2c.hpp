#pragma once

#include <Wire.h>
#include <vector>

// All TwoWire instances compiled in for this chip (Wire, Wire1 if present).
std::vector<TwoWire *> i2c_buses();
bool i2c_bus_started(TwoWire &w);
bool i2c_probe(TwoWire &w, uint8_t addr);
// Probe 7-bit addresses 0x08..0x77, return those that ACK.
std::vector<uint8_t> i2c_scan(TwoWire &w);
