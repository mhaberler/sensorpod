#pragma once

#include <Wire.h>
#include <optional>

bool lox_init(TwoWire &wire);
std::optional<uint16_t> lox_poll(TwoWire &wire);
