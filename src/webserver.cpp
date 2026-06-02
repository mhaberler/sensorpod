#include <WebServer.h>
#include "http_server.hpp"

WebServer http_server(80);

void webserver_setup() {
    http_server.on("/", HTTP_GET, []() {
        String body;
        body.reserve(4096);
        sysinfo_html(body);
        http_server.send(200, "text/html", body);
    });
    http_server.on("/data", HTTP_GET, []() {
        String body;
        body.reserve(2048);
        sysinfo_json(body);
        http_server.send(200, "application/json", body);
    });
#ifdef OTA_WEB_UPDATER
    ota_setup(http_server);
#endif
    http_server.begin();
}

void webserver_loop() {
    http_server.handleClient();
}
