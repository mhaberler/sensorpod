#include "BLEScanner.h"
#include "credstore.hpp"
#include "deviceconfig.hpp"
#include "http_server.hpp"
#include "i2c.hpp"
#ifdef ADSB_UART
#include "adsb.hpp"
#include "adsb_rtlsdr.hpp"
#endif
#include <WebServer.h>
#include <WiFi.h>
#if defined(BOARD_HAS_SDIO_ESP_HOSTED) && defined(ESP_HOSTED_DOWNGRADE)
#include "hosted_ota.hpp"
#include <Network.h>
#include <esp32-hal-hosted.h>
bool hosted_update_busy();
#endif

extern bool is_broker_mode;
extern bool wifi_sleep_enabled;
extern volatile uint8_t ble_decoder_mode;
extern volatile bool ble_retain_undecoded;
extern volatile bool ble_dedup_enabled;
extern volatile uint32_t ble_dedup_age;

WebServer http_server(80);

// Helper to log incoming requests
static void log_request() {
  String client_ip = http_server.client().remoteIP().toString().c_str();
  log_w("HTTP request from %s: %s %s", client_ip.c_str(),
        http_server.method() == HTTP_GET      ? "GET"
        : http_server.method() == HTTP_POST   ? "POST"
        : http_server.method() == HTTP_PUT    ? "PUT"
        : http_server.method() == HTTP_DELETE ? "DELETE"
                                              : "OTHER",
        http_server.uri().c_str());
}

void webserver_setup() {
  http_server.on("/", HTTP_GET, []() {
    log_request();
    String body;
    body.reserve(4096);
    sysinfo_html(body, is_broker_mode);
    http_server.sendHeader("Connection", "close");
    http_server.sendHeader("Cache-Control", "no-store");
    http_server.send(200, "text/html", body);
  });
  http_server.on("/", HTTP_POST, []() {
    log_request();
    String body = "Hello, Sensor Logger";
    http_server.sendHeader("Connection", "close");
    http_server.sendHeader("Cache-Control", "no-store");
    http_server.send(200, "text/html", body);
  });
  http_server.on("/data", HTTP_GET, []() {
    log_request();
    String body;
    body.reserve(2048);
    sysinfo_json(body, is_broker_mode);
    http_server.sendHeader("Cache-Control", "no-store");
    http_server.send(200, "application/json", body);
  });

  // Role switching endpoint
  http_server.on("/api/set-role", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("role")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing role parameter\"}");
      return;
    }
    String role = http_server.arg("role");
    bool broker_mode = (role == "broker");
    DeviceConfig::setBrokerMode(broker_mode);
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"restarting\":true}");
    delay(500);
    ESP.restart();
  });

  // Broker selection endpoint (Client mode)
  http_server.on("/api/set-broker", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("hostname")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing hostname parameter\"}");
      return;
    }
    String hostname = http_server.arg("hostname");
    DeviceConfig::setSelectedBrokerHostname(hostname);
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"restarting\":true}");
    delay(500);
    ESP.restart();
  });

  // WiFi modem-sleep toggle (both roles). Saved to NVS and applied live -
  // no reboot. false = sleep off (keeps mDNS discovery alive on a hotspot).
  http_server.on("/api/set-wifi-sleep", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("enabled")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing enabled parameter\"}");
      return;
    }
    bool on = http_server.arg("enabled").toInt() != 0;
    if (!DeviceConfig::setWifiSleepEnabled(on)) {
      http_server.send(500, "application/json",
                       "{\"error\":\"failed to save setting\"}");
      return;
    }
    wifi_sleep_enabled = on;
    WiFi.setSleep(wifi_sleep_enabled);
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"applied\":true}");
  });

#ifdef ADSB_UART
  // ADS-B publish toggles. Saved to NVS, applied live.
  http_server.on("/api/set-adsb", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("publish") && !http_server.hasArg("ihex") &&
        !http_server.hasArg("ihex_signal") && !http_server.hasArg("rtl_gain") &&
        !http_server.hasArg("rtl_cal_k")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing parameter\"}");
      return;
    }
    if (http_server.hasArg("publish")) {
      bool on = http_server.arg("publish").toInt() != 0;
      if (!DeviceConfig::setAdsbPublishEnabled(on)) {
        http_server.send(500, "application/json",
                         "{\"error\":\"failed to save setting\"}");
        return;
      }
      adsb_publish_enabled = on;
    }
    if (http_server.hasArg("ihex")) {
      bool on = http_server.arg("ihex").toInt() != 0;
      if (!DeviceConfig::setAdsbIhexEnabled(on)) {
        http_server.send(500, "application/json",
                         "{\"error\":\"failed to save setting\"}");
        return;
      }
      adsb_ihex_enabled = on;
    }
#ifdef ADSB_RTLSDR
    if (http_server.hasArg("ihex_signal")) {
      bool on = http_server.arg("ihex_signal").toInt() != 0;
      if (!DeviceConfig::setAdsbIhexSignalEnabled(on)) {
        http_server.send(500, "application/json",
                         "{\"error\":\"failed to save setting\"}");
        return;
      }
      adsb_ihex_signal = on;
    }
    if (http_server.hasArg("rtl_gain")) {
      // Tenths of a dB, -1 = tuner max. Must be one of the attached
      // tuner's steps; without a dongle any 0..60 dB value is stored and
      // snapped on attach.
      String a = http_server.arg("rtl_gain");
      char *end;
      long g = strtol(a.c_str(), &end, 10);
      const AdsbRtlsdrInfo ri = adsb_rtlsdr_info();
      bool valid = a.length() && *end == 0 && g >= -1 && g <= 600;
      if (valid && g >= 0 && ri.connected) {
        valid = false;
        for (int k = 0; k < ri.ngains; k++)
          valid |= ri.gains[k] == g;
      }
      if (!valid) {
        http_server.send(400, "application/json",
                         "{\"error\":\"invalid rtl_gain\"}");
        return;
      }
      if (!DeviceConfig::setRtlGain(g)) {
        http_server.send(500, "application/json",
                         "{\"error\":\"failed to save setting\"}");
        return;
      }
      adsb_rtlsdr_set_gain(g);
    }
    if (http_server.hasArg("rtl_cal_k")) {
      // dB; empty = uncalibrated.
      String a = http_server.arg("rtl_cal_k");
      a.trim();
      float k = NAN;
      if (a.length()) {
        char *end;
        k = strtof(a.c_str(), &end);
        if (*end != 0 || isnan(k) || k < -200 || k > 200) {
          http_server.send(400, "application/json",
                           "{\"error\":\"invalid rtl_cal_k\"}");
          return;
        }
      }
      if (!DeviceConfig::setRtlCalK(k)) {
        http_server.send(500, "application/json",
                         "{\"error\":\"failed to save setting\"}");
        return;
      }
      adsb_rtlsdr_set_cal_k(k);
    }
#endif
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"applied\":true}");
  });
#endif

  // I2C bus scan. Synchronous - blocks loop() for the duration of the scan.
  http_server.on("/api/i2c-scan", HTTP_GET, []() {
    log_request();
    String body = "{\"buses\":[";
    bool first_bus = true;
    for (TwoWire *bus : i2c_buses()) {
      bool started = i2c_bus_started(*bus);
      if (!first_bus)
        body += ",";
      first_bus = false;
      body += "{\"bus\":" + String(bus->getBusNum()) +
              ",\"started\":" + (started ? "true" : "false") + ",\"devices\":[";
      if (started) {
        bool first_dev = true;
        for (uint8_t addr : i2c_scan(*bus)) {
          if (!first_dev)
            body += ",";
          first_dev = false;
          body += String(addr);
        }
      }
      body += "]}";
    }
    body += "]}";
    http_server.sendHeader("Cache-Control", "no-store");
    http_server.send(200, "application/json", body);
  });

  // Prefix MQTT topics with hostName/. Saved to NVS, applied live.
  http_server.on("/api/set-topic-prefix", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("enabled")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing enabled parameter\"}");
      return;
    }
    bool on = http_server.arg("enabled").toInt() != 0;
    if (!DeviceConfig::setTopicPrefixEnabled(on)) {
      http_server.send(500, "application/json",
                       "{\"error\":\"failed to save setting\"}");
      return;
    }
    topic_prefix_enabled = on;
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"applied\":true}");
  });

  // BLE scanning on/off. Boot-time gate - saved to NVS, needs reboot.
  http_server.on("/api/set-ble-scan", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("enabled")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing enabled parameter\"}");
      return;
    }
    bool on = http_server.arg("enabled").toInt() != 0;
    if (!DeviceConfig::setBleScanEnabled(on)) {
      http_server.send(500, "application/json",
                       "{\"error\":\"failed to save setting\"}");
      return;
    }
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"restarting\":true}");
    delay(500);
    ESP.restart();
  });

  // BLE decoder selection. Saved to NVS and applied live - no reboot.
  http_server.on("/api/set-ble-decoder", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("decoder")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing decoder parameter\"}");
      return;
    }
    uint8_t d = (uint8_t)http_server.arg("decoder").toInt();
    if (!DeviceConfig::setBleDecoder(d)) {
      http_server.send(400, "application/json",
                       "{\"error\":\"invalid decoder value\"}");
      return;
    }
    ble_decoder_mode = d;
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"applied\":true}");
  });

  // Retain/drop undecoded advertisements. Saved to NVS, applied live.
  http_server.on("/api/set-ble-retain", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("enabled")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing enabled parameter\"}");
      return;
    }
    bool on = http_server.arg("enabled").toInt() != 0;
    if (!DeviceConfig::setBleRetainUndecoded(on)) {
      http_server.send(500, "application/json",
                       "{\"error\":\"failed to save setting\"}");
      return;
    }
    ble_retain_undecoded = on;
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"applied\":true}");
  });

  // Advertisement dedup (bool + age in seconds). Saved to NVS, applied live.
  http_server.on("/api/set-ble-dedup", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("enabled")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing enabled parameter\"}");
      return;
    }
    bool on = http_server.arg("enabled").toInt() != 0;
    uint32_t age = ble_dedup_age;
    if (http_server.hasArg("age")) {
      long a = http_server.arg("age").toInt();
      if (a < 1) {
        http_server.send(400, "application/json",
                         "{\"error\":\"invalid age value\"}");
        return;
      }
      age = (uint32_t)a;
    }
    if (!DeviceConfig::setBleDedupEnabled(on) ||
        !DeviceConfig::setBleDedupAge(age)) {
      http_server.send(500, "application/json",
                       "{\"error\":\"failed to save setting\"}");
      return;
    }
    ble_dedup_age = age;
    ble_dedup_enabled = on;
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"applied\":true}");
  });

  // Reset BLE statistics counters
  http_server.on("/api/clear-ble-stats", HTTP_POST, []() {
    log_request();
    BLEScanner::instance().clearStats();
    http_server.send(200, "application/json", "{\"status\":\"cleared\"}");
  });

  // WiFi credentials endpoint (empty ssid = clear)
  http_server.on("/api/set-wifi", HTTP_POST, []() {
    log_request();
    if (!http_server.hasArg("ssid")) {
      http_server.send(400, "application/json",
                       "{\"error\":\"missing ssid parameter\"}");
      return;
    }
    String ssid = http_server.arg("ssid");
    String pass = http_server.arg("password");
    if (ssid.length() == 0) {
      clearWiFiCredentials();
    } else {
      saveWiFiCredentials(ssid, pass);
    }
    http_server.send(200, "application/json",
                     "{\"status\":\"saved\",\"restarting\":true}");
    delay(500);
    ESP.restart();
  });

  // Reboot endpoint
  http_server.on("/api/reboot", HTTP_POST, []() {
    log_request();
    http_server.send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
  });

#if defined(BOARD_HAS_SDIO_ESP_HOSTED) && defined(ESP_HOSTED_DOWNGRADE)
  // Debug: force-flash an older C6 co-processor image, then reboot so the
  // normal upgrade path can be verified. Gated on -DESP_HOSTED_DOWNGRADE.
  http_server.on("/api/hosted-downgrade", HTTP_POST, []() {
    log_request();
    if (!hostedIsInitialized()) {
      http_server.send(503, "application/json",
                       "{\"error\":\"esp-hosted not initialized\"}");
      return;
    }
    if (!Network.isOnline()) {
      http_server.send(503, "application/json",
                       "{\"error\":\"network not online\"}");
      return;
    }
    if (hosted_update_busy()) {
      http_server.send(409, "application/json",
                       "{\"error\":\"hosted flash already in progress\"}");
      return;
    }

    uint32_t maj = 0, min = 0, pat = 0;
    const bool explicit_ver = http_server.hasArg("major") ||
                              http_server.hasArg("minor") ||
                              http_server.hasArg("patch");
    if (explicit_ver) {
      hostedGetHostVersion(&maj, &min, &pat);
      if (http_server.hasArg("major"))
        maj = (uint32_t)http_server.arg("major").toInt();
      if (http_server.hasArg("minor"))
        min = (uint32_t)http_server.arg("minor").toInt();
      if (http_server.hasArg("patch"))
        pat = (uint32_t)http_server.arg("patch").toInt();
      char probe[128];
      buildHostedFwUrl(probe, sizeof(probe), maj, min, pat);
      if (!hostedFwUrlExists(probe)) {
        http_server.send(404, "application/json",
                         "{\"error\":\"firmware not on Arduino CDN\"}");
        return;
      }
    } else if (!findOlderHostedFw(&maj, &min, &pat)) {
      // Default: oldest known CDN image older than host (currently 2.8.5).
      http_server.send(404, "application/json",
                       "{\"error\":\"no older firmware on Arduino CDN\"}");
      return;
    }

    char url[128];
    buildHostedFwUrl(url, sizeof(url), maj, min, pat);
    if (!request_hosted_downgrade(maj, min, pat)) {
      http_server.send(409, "application/json",
                       "{\"error\":\"could not arm hosted downgrade\"}");
      return;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\"status\":\"accepted\",\"target\":\"%lu.%lu.%lu\",\"url\":\"%"
             "s\"}",
             (unsigned long)maj, (unsigned long)min, (unsigned long)pat, url);
    http_server.send(200, "application/json", body);
  });
#endif // BOARD_HAS_SDIO_ESP_HOSTED && ESP_HOSTED_DOWNGRADE

  http_server.on("/favicon.ico", []() {
    log_request();
    http_server.send(
        204); // 204 No Content telling Chrome to stop waiting immediately
  });

#ifdef OTA_WEB_UPDATER
  ota_setup(http_server);
#endif
  http_server.begin();
}

void webserver_loop() { http_server.handleClient(); }
