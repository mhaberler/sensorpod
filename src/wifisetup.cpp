#include <ESPmDNS.h>
#include <Network.h>
#include <PicoMQTT.h>
#include <WiFi.h>

#include "credstore.hpp"
#include "mdns.h"

extern PicoMQTT::Server mqtt;

static uint8_t prev_clients = 255;

String macAddress;

void getMacAddress(String &macStr) {
  uint8_t mac[6];
  Network.macAddress(mac);
  macStr = String(mac[0], HEX) + String(mac[1], HEX) + String(mac[2], HEX) +
           String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);
  macStr.toUpperCase();
}

static void onNetworkEvent(arduino_event_id_t event) {
  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    log_w("STA connected to %s %s RSSI %d IP: %s", WiFi.STA.SSID().c_str(),
          WiFi.STA.BSSIDstr().c_str(), WiFi.STA.RSSI(),
          WiFi.STA.localIP().toString().c_str());
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    log_w("STA disconnected");
    break;
  default:
    log_d("Network event: %s", NetworkEvents::eventName(event));
    break;
  }
}

void startStaAttempt(const String &ssid, const String &pass) {
  log_w("STA begin ssid=%s", ssid.c_str());
  WiFi.STA.connect(ssid.c_str(), pass.c_str());
}

void stopSta() {
  WiFi.STA.disconnect(false, true);
}

void wifi_setup() {
#ifdef BOARD_HAS_SDIO_ESP_HOSTED
  WiFi.setPins(BOARD_SDIO_ESP_HOSTED_CLK, BOARD_SDIO_ESP_HOSTED_CMD,
               BOARD_SDIO_ESP_HOSTED_D0, BOARD_SDIO_ESP_HOSTED_D1,
               BOARD_SDIO_ESP_HOSTED_D2, BOARD_SDIO_ESP_HOSTED_D3,
               BOARD_SDIO_ESP_HOSTED_RESET);
#endif

  getMacAddress(macAddress);
  log_w("MAC address=%s", macAddress.c_str());

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);

  String apSSID = "ESP32-" + macAddress;
  String apPASS = HOSTNAME;
  log_w("AP SSID: %s PW: %s", apSSID.c_str(), apPASS.c_str());
  WiFi.AP.create(apSSID, apPASS);
  WiFi.AP.enableIPv6();
  WiFi.AP.begin();
  WiFi.AP.enableDhcpCaptivePortal();

  if (MDNS.begin(HOSTNAME)) {
    log_i("starting MDNS for %s", HOSTNAME);
    MDNS.enableWorkstation();
    MDNS.addService("mqtt", "tcp", MQTT_PORT);
    MDNS.addService("mqtt-ws", "tcp", MQTTWS_PORT);
    MDNS.addServiceTxt("mqtt-ws", "tcp", "path", "/mqtt");
    mdns_service_instance_name_set("_mqtt", "_tcp",
                                   (HOSTNAME "-TCP-" + macAddress).c_str());
    mdns_service_instance_name_set("_mqtt-ws", "_tcp",
                                   (HOSTNAME "-WS-" + macAddress).c_str());
  }

  Network.onEvent(onNetworkEvent);

  String s, p;
  if (loadWiFiCredentials(s, p)) {
    log_w("loaded creds, starting STA");
    startStaAttempt(s, p);
  } else {
    log_w("no creds, AP-only");
  }
}

void wifi_loop() {
  uint8_t clients = WiFi.softAPgetStationNum();
  if (prev_clients ^ clients) {
    log_w("AP clients: %u", clients);
    prev_clients = clients;
  }
}
