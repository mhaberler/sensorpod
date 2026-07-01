#include "credstore.hpp"
#include "deviceconfig.hpp"
#include "http_server.hpp"
#include<WebServer.h>
#include<WiFi.h>

extern bool is_broker_mode;
extern bool wifi_sleep_enabled;

WebServer http_server(80);

// Helper to log incoming requests
static void log_request() {
  String client_ip = http_server.client().remoteIP().toString().c_str();
  log_i("HTTP request from %s: %s %s", client_ip.c_str(),
        http_server.method() == HTTP_GET ? "GET" :
        http_server.method() == HTTP_POST ? "POST" :
        http_server.method() == HTTP_PUT ? "PUT" :
        http_server.method() == HTTP_DELETE ? "DELETE" : "OTHER",
        http_server.uri().c_str());
}

void webserver_setup() {
  http_server.on("/", HTTP_GET,[]() {
    log_request();
    String body;
    body.reserve(4096);
    sysinfo_html(body, is_broker_mode);
    http_server.sendHeader("Connection", "close");
    http_server.sendHeader("Cache-Control", "no-store");
    http_server.send(200, "text/html", body);
  });
  http_server.on("/data", HTTP_GET,[]() {
    log_request();
    String body;
    body.reserve(2048);
    sysinfo_json(body, is_broker_mode);
    http_server.sendHeader("Cache-Control", "no-store");
    http_server.send(200, "application/json", body);
  });

  // Role switching endpoint
  http_server.on("/api/set-role", HTTP_POST,[]() {
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
  http_server.on("/api/set-broker", HTTP_POST,[]() {
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
  http_server.on("/api/set-wifi-sleep", HTTP_POST,[]() {
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

  // WiFi credentials endpoint (empty ssid = clear)
  http_server.on("/api/set-wifi", HTTP_POST,[]() {
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
  http_server.on("/api/reboot", HTTP_POST,[]() {
    log_request();
    http_server.send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
  });

  http_server.on("/favicon.ico",[]() {
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
