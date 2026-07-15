/// @file custom_decoder.hpp
/// @brief Custom BLE advertisement decoder (web UI decoder option "Custom").
///
/// Currently decodes Mikrotik TG-BT5-IN/-OUT tags from manufacturer data
/// (company ID 0x094F, unencrypted payload).

#pragma once

#define ARDUINOJSON_USE_LONG_LONG 1
#include "ArduinoJson.h"

/// Decode one raw advertisement (BLEScanner onResult schema, key
/// "manufacturerdata" holds hex-encoded manufacturer data). Fills outDoc
/// and returns true if the advertisement was claimed.
bool custom_decode(JsonObject BLEdata, JsonDocument &outDoc);
