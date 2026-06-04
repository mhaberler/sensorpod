#include <WebServer.h>
#include "http_server.hpp"
#include "deviceconfig.hpp"

extern bool is_broker_mode;

WebServer http_server(80);

void webserver_setup() {
    http_server.on("/", HTTP_GET, []() {
        String body;
        body.reserve(4096);
        sysinfo_html(body, is_broker_mode);
        // Force the browser to immediately shut down this specific socket
        http_server.sendHeader("Connection", "close");
        http_server.send(200, "text/html", body);
    });
    http_server.on("/data", HTTP_GET, []() {
        String body;
        body.reserve(2048);
        sysinfo_json(body, is_broker_mode);
        http_server.send(200, "application/json", body);
    });

    // Role switching endpoint
    http_server.on("/api/set-role", HTTP_POST, []() {
        if (!http_server.hasArg("role")) {
            http_server.send(400, "application/json", "{\"error\":\"missing role parameter\"}");
            return;
        }
        String role = http_server.arg("role");
        bool broker_mode = (role == "broker");
        DeviceConfig::setBrokerMode(broker_mode);
        http_server.send(200, "application/json", "{\"status\":\"saved\",\"restarting\":true}");
        delay(500);
        ESP.restart();
    });

    // Broker selection endpoint (Client mode)
    http_server.on("/api/set-broker", HTTP_POST, []() {
        if (!http_server.hasArg("hostname")) {
            http_server.send(400, "application/json", "{\"error\":\"missing hostname parameter\"}");
            return;
        }
        String hostname = http_server.arg("hostname");
        DeviceConfig::setSelectedBrokerHostname(hostname);
        http_server.send(200, "application/json", "{\"status\":\"saved\",\"restarting\":true}");
        delay(500);
        ESP.restart();
    });

    // Reboot endpoint
    http_server.on("/api/reboot", HTTP_POST, []() {
        http_server.send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(500);
        ESP.restart();
    });

    http_server.on("/favicon.ico", []() {
        http_server.send(204); // 204 No Content telling Chrome to stop waiting immediately
    });

#ifdef OTA_WEB_UPDATER
    ota_setup(http_server);
#endif
    http_server.begin();
}

void webserver_loop() {
    http_server.handleClient();
}
