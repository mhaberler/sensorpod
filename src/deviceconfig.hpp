#pragma once

#include <Arduino.h>
#include <Preferences.h>

class DeviceConfig {
public:
  static bool isBrokerMode() {
    Preferences prefs;
    prefs.begin("device-config", false);     // read-only
    bool role = prefs.getBool("role", true); // default: broker
    prefs.end();
    return role;
  }

  static void setBrokerMode(bool enabled) {
    Preferences prefs;
    prefs.begin("device-config", false); // read-write
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

  static void setSelectedBrokerHostname(const String &hostname) {
    Preferences prefs;
    prefs.begin("device-config", false);
    prefs.putString("broker_host", hostname);
    prefs.end();
    log_d("Device config: broker_host = %s", hostname.c_str());
  }

  static bool isWifiSleepEnabled() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return false;
    bool enabled = prefs.getInt("wifi_sleep", 0) != 0; // default: sleep off
    prefs.end();
    return enabled;
  }

  static bool setWifiSleepEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for wifi_sleep");
      return false;
    }
    size_t n = prefs.putInt("wifi_sleep", enabled ? 1 : 0);
    prefs.end();
    if (n == 0) {
      log_e("Device config: failed to save wifi_sleep");
      return false;
    }
    log_i("Device config: wifi_sleep = %s", enabled ? "on" : "off");
    return true;
  }
};

// Global role flag, set at boot from Preferences
extern bool is_broker_mode;
extern bool wifi_sleep_enabled;
