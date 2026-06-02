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
        http_server.send(200, "text/html", body);
    });
    http_server.on("/data", HTTP_GET, []() {
        String body;
        body.reserve(2048);
        sysinfo_json(body);
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

    // Reboot endpoint
    http_server.on("/api/reboot", HTTP_POST, []() {
        http_server.send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(500);
        ESP.restart();
    });

#ifdef OTA_WEB_UPDATER
    ota_setup(http_server);
#endif
    http_server.begin();
}

void webserver_loop() {
    http_server.handleClient();
}
