#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

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

  // BLE decoder selection values (ble_decoder key)
  enum BleDecoder : uint8_t {
    BLE_DECODER_THEENGS = 0,
    BLE_DECODER_BTHOME = 1,
    BLE_DECODER_CUSTOM = 2,
    BLE_DECODER_NONE = 3, // raw advertisements only
  };

  static bool isBleScanEnabled() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return true;
    bool enabled = prefs.getBool("ble_scan", true);
    prefs.end();
    return enabled;
  }

  static bool setBleScanEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for ble_scan");
      return false;
    }
    prefs.putBool("ble_scan", enabled);
    prefs.end();
    log_i("Device config: ble_scan = %s", enabled ? "on" : "off");
    return true;
  }

  static uint8_t getBleDecoder() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return BLE_DECODER_THEENGS;
    uint8_t d = prefs.getUChar("ble_decoder", BLE_DECODER_THEENGS);
    prefs.end();
    return d > BLE_DECODER_NONE ? (uint8_t)BLE_DECODER_THEENGS : d;
  }

  static bool setBleDecoder(uint8_t decoder) {
    if (decoder > BLE_DECODER_NONE)
      return false;
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for ble_decoder");
      return false;
    }
    prefs.putUChar("ble_decoder", decoder);
    prefs.end();
    log_i("Device config: ble_decoder = %u", decoder);
    return true;
  }

  static bool isBleRetainUndecoded() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return false;
    bool retain = prefs.getBool("ble_retain", false);
    prefs.end();
    return retain;
  }

  static bool setBleRetainUndecoded(bool retain) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for ble_retain");
      return false;
    }
    prefs.putBool("ble_retain", retain);
    prefs.end();
    log_i("Device config: ble_retain = %s", retain ? "on" : "off");
    return true;
  }

  static bool isBleDedupEnabled() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return true;
    bool enabled = prefs.getBool("ble_dedup", true);
    prefs.end();
    return enabled;
  }

  static bool setBleDedupEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for ble_dedup");
      return false;
    }
    prefs.putBool("ble_dedup", enabled);
    prefs.end();
    log_i("Device config: ble_dedup = %s", enabled ? "on" : "off");
    return true;
  }

  static uint32_t getBleDedupAge() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return 1;
    uint32_t age = prefs.getUInt("ble_dedup_age", 1);
    prefs.end();
    return age > 0 ? age : 1;
  }

  static bool setBleDedupAge(uint32_t seconds) {
    if (seconds == 0)
      return false;
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for ble_dedup_age");
      return false;
    }
    prefs.putUInt("ble_dedup_age", seconds);
    prefs.end();
    log_i("Device config: ble_dedup_age = %us", (unsigned)seconds);
    return true;
  }

  static bool isTopicPrefixEnabled() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return false;
    bool enabled = prefs.getBool("topic_prefix", false); // default: off
    prefs.end();
    return enabled;
  }

  static bool setTopicPrefixEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for topic_prefix");
      return false;
    }
    prefs.putBool("topic_prefix", enabled);
    prefs.end();
    log_i("Device config: topic_prefix = %s", enabled ? "on" : "off");
    return true;
  }

  static bool isAdsbPublishEnabled() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return true;
    bool enabled = prefs.getBool("adsb_pub", true); // default: on
    prefs.end();
    return enabled;
  }

  static bool setAdsbPublishEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for adsb_pub");
      return false;
    }
    prefs.putBool("adsb_pub", enabled);
    prefs.end();
    log_i("Device config: adsb_pub = %s", enabled ? "on" : "off");
    return true;
  }

  static bool isAdsbIhexEnabled() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return false;
    bool enabled = prefs.getBool("adsb_ihex", false); // default: off
    prefs.end();
    return enabled;
  }

  static bool setAdsbIhexEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for adsb_ihex");
      return false;
    }
    prefs.putBool("adsb_ihex", enabled);
    prefs.end();
    log_i("Device config: adsb_ihex = %s", enabled ? "on" : "off");
    return true;
  }

  // ihex lines carry the RTL-SDR signal byte ("+SS<hex>;").
  static bool isAdsbIhexSignalEnabled() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return false;
    bool enabled = prefs.getBool("adsb_ihex_sig", false); // default: off
    prefs.end();
    return enabled;
  }

  static bool setAdsbIhexSignalEnabled(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for adsb_ihex_sig");
      return false;
    }
    prefs.putBool("adsb_ihex_sig", enabled);
    prefs.end();
    log_i("Device config: adsb_ihex_sig = %s", enabled ? "on" : "off");
    return true;
  }

  // RTL-SDR tuner gain, tenths of a dB; -1 = unset (build default).
  static int getRtlGain() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return -1;
    int gain = prefs.getInt("rtl_gain", -1);
    prefs.end();
    return gain;
  }

  static bool setRtlGain(int tenths) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for rtl_gain");
      return false;
    }
    prefs.putInt("rtl_gain", tenths);
    prefs.end();
    log_i("Device config: rtl_gain = %d", tenths);
    return true;
  }

  // RTL-SDR calibration constant K (dB): dBm = dBFS - gain + K. NAN = unset.
  static float getRtlCalK() {
    Preferences prefs;
    if (!prefs.begin("device-config", true))
      return NAN;
    float k = prefs.isKey("rtl_cal_k") ? prefs.getFloat("rtl_cal_k", NAN) : NAN;
    prefs.end();
    return k;
  }

  static bool setRtlCalK(float k) {
    Preferences prefs;
    if (!prefs.begin("device-config", false)) {
      log_e("Device config: failed to open NVS for rtl_cal_k");
      return false;
    }
    if (isnan(k))
      prefs.remove("rtl_cal_k");
    else
      prefs.putFloat("rtl_cal_k", k);
    prefs.end();
    log_i("Device config: rtl_cal_k = %.1f", k);
    return true;
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
extern volatile bool topic_prefix_enabled; // prepend hostName/ to topics

// BLE options, set at boot from Preferences, updated live by web endpoints
extern volatile uint8_t ble_decoder_mode;  // DeviceConfig::BleDecoder
extern volatile bool ble_retain_undecoded; // publish raw when decoder skips
extern volatile bool ble_dedup_enabled;    // dedupe in scan callback
extern volatile uint32_t ble_dedup_age;    // seconds
