#pragma once

#include <Arduino.h>
#include <Preferences.h>

class DeviceConfig {
public:
  static bool isBrokerMode() {
    Preferences prefs;
    prefs.begin("device-config", false);  // read-only
    bool role = prefs.getBool("role", false);  // default: broker
    prefs.end();
    return role;
  }

  static void setBrokerMode(bool enabled) {
    Preferences prefs;
    prefs.begin("device-config", false);  // read-write
    prefs.putBool("role", enabled);
    prefs.end();
    log_d("Device config: role = %s", enabled ? "broker" : "client");
  }

  static String getSelectedBrokerHostname() {
    Preferences prefs;
    prefs.begin("device-config", false);
    String hostname = prefs.getString("broker_host", "");
    prefs.end();
    return hostname;
  }

  static void setSelectedBrokerHostname(const String& hostname) {
    Preferences prefs;
    prefs.begin("device-config", false);
    prefs.putString("broker_host", hostname);
    prefs.end();
    log_d("Device config: broker_host = %s", hostname.c_str());
  }
};

// Global role flag, set at boot from Preferences
extern bool is_broker_mode;
