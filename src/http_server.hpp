#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer http_server;

static constexpr const char *HTTP_PAGE_STYLE =
  "<style>"
  "body{font-family:sans-serif;margin:1em}"
  "table{border-collapse:collapse}"
  "td,th{padding:2px 8px;border:1px solid #ccc;font-size:0.9em}"
  "th{background:#eee}"
  ".run{background:#dfd}.nxt{background:#ffd}"
  "code{background:#eee;padding:1px 4px;border-radius:3px}"
  "progress{width:300px}"
  "</style>";

void sysinfo_html(String &out, bool is_broker_mode);
void sysinfo_json(String &out);
void webserver_setup();
void webserver_loop();

#ifdef OTA_WEB_UPDATER
void ota_setup(WebServer &srv);
#endif
