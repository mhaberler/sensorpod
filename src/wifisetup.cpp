#include <ESPmDNS.h>
#include <Network.h>
#include <PicoMQTT.h>
#include <WiFi.h>
#include <ESP_HostedOTA.h>

#include "credstore.hpp"
#include "deviceconfig.hpp"
#include "mdns.h"
#include "http_server.hpp"
#include "mdns_state.hpp"

extern bool is_broker_mode;

String hostName;

static uint8_t prev_clients = 255;

MdnsAnnounce mdns_services[4];
size_t mdns_count = 0;
static String mqttInstance, mqttWsInstance, httpInstance;
static wl_status_t wifiStatus = WL_NO_SHIELD;

static void add_mdns(const char *instance, const char *svc, const char *proto,
                     uint16_t port, const char *txt = nullptr) {
    if (mdns_count >= sizeof(mdns_services) / sizeof(mdns_services[0])) return;
    mdns_services[mdns_count++] = {instance, svc, proto, port, txt};
}

static const char *part_type_name(esp_partition_type_t t) {
    switch (t) {
        case ESP_PARTITION_TYPE_APP:
            return "app";
        case ESP_PARTITION_TYPE_DATA:
            return "data";
        default:
            return "?";
    }
}

static void onNetworkEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            log_w("STA connected to %s %s RSSI %d IP: %s", WiFi.STA.SSID().c_str(),
                  WiFi.STA.BSSIDstr().c_str(), WiFi.STA.RSSI(),
                  WiFi.STA.localIP().toString().c_str());
            if (Network.isOnline() && updateEspHostedSlave()) {
                // Restart the host ESP32 after successful update
                // This is currently required to properly activate the new firmware
                // on the ESP-Hosted co-processor
                ESP.restart();
            }
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

static void start_ap() {
    String apSSID = hostName;
    String apPASS = hostName;
    log_w("AP SSID: %s PW: %s", apSSID.c_str(), apPASS.c_str());
    WiFi.AP.create(apSSID, apPASS);
    WiFi.AP.enableIPv6();
    WiFi.AP.begin();
    WiFi.AP.enableDhcpCaptivePortal();
}

static void start_sta(const String& ssid, const String& pass) {
    startStaAttempt(ssid, pass);
}

void wifi_setup() {
#ifdef BOARD_HAS_SDIO_ESP_HOSTED
    WiFi.setPins(BOARD_SDIO_ESP_HOSTED_CLK, BOARD_SDIO_ESP_HOSTED_CMD,
                 BOARD_SDIO_ESP_HOSTED_D0, BOARD_SDIO_ESP_HOSTED_D1,
                 BOARD_SDIO_ESP_HOSTED_D2, BOARD_SDIO_ESP_HOSTED_D3,
                 BOARD_SDIO_ESP_HOSTED_RESET);
#endif

    WiFi.mode(WIFI_AP_STA);
    WiFi.STA.begin(false);
    WiFi.STA.setAutoReconnect(true);

    // AP always on (in both Broker and Client modes)
    start_ap();

    // Branch on role for STA and mDNS setup
    if (is_broker_mode) {
        // Broker mode: STA optional
        String ssid, pass;
        if (loadWiFiCredentials(ssid, pass)) {
            log_w("Broker mode: loaded creds, starting STA");
            start_sta(ssid, pass);
        } else {
            log_w("Broker mode: no creds, AP-only");
        }
    } else {
        // Client mode: STA required
        String ssid, pass;
        if (loadWiFiCredentials(ssid, pass)) {
            log_w("Client mode: loaded creds, starting STA");
            start_sta(ssid, pass);
        } else {
            log_w("Client mode: no creds, Improv provisioning required");
        }
    }

    webserver_setup();

    // mDNS: Broker mode announces, Client mode will discover
    if (MDNS.begin(hostName)) {
        log_i("starting MDNS for %s", hostName.c_str());
        MDNS.enableWorkstation();

        if (is_broker_mode) {
            // Broker mode: announce self
            MDNS.addService("mqtt", "tcp", MQTT_PORT);
            MDNS.addService("mqtt-ws", "tcp", MQTTWS_PORT);
            MDNS.addService("http", "tcp", 80);
            MDNS.addServiceTxt("mqtt-ws", "tcp", "path", "/mqtt");

            mqttInstance   = "TCP-" + hostName;
            mqttWsInstance = "WS-" + hostName;
            httpInstance   = hostName;
            mdns_service_instance_name_set("_mqtt", "_tcp", mqttInstance.c_str());
            mdns_service_instance_name_set("_mqtt-ws", "_tcp", mqttWsInstance.c_str());

            add_mdns(mqttInstance.c_str(),   "_mqtt",    "_tcp", MQTT_PORT);
            add_mdns(mqttWsInstance.c_str(), "_mqtt-ws", "_tcp", MQTTWS_PORT, "path=/mqtt");
            add_mdns(httpInstance.c_str(),   "_http",    "_tcp", 80);
        } else {
            // Client mode: just enable workstation, no announcements
            log_d("Client mode: mDNS workstation enabled for discovery");
        }
    }

    Network.onEvent(onNetworkEvent);
}

void wifi_loop() {
    uint8_t clients = WiFi.softAPgetStationNum();
    if (prev_clients ^ clients) {
        log_w("AP clients: %u", clients);
        prev_clients = clients;
    }
    wl_status_t s = WiFi.status();
    if (wifiStatus ^ s) {
        log_w("WiFi status change %u -> %u", wifiStatus, s);
        wifiStatus = s;
    }
    webserver_loop();
}
