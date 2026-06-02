#include <stdarg.h>
#include <ESPmDNS.h>
#include <Network.h>
#include <PicoMQTT.h>
#include <WiFi.h>
#include <ESP_HostedOTA.h>
#include <WebServer.h>
#include <Esp.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>

#include "credstore.hpp"
#include "mdns.h"
#include "http_server.hpp"

#ifdef BUILD_TAG
    #define FW_VERSION  BUILD_TAG
#else
    #define FW_VERSION  "0.0.1"
#endif

#ifndef BUILD_SHA
    #define BUILD_SHA "unknown"
#endif
#ifndef BUILD_DATE
    #define BUILD_DATE "unknown"
#endif

extern PicoMQTT::Server mqtt;

String macAddress;

static uint8_t prev_clients = 255;

WebServer http_server(80);

struct MdnsAnnounce {
    const char *instance;
    const char *service;
    const char *proto;
    uint16_t port;
    const char *txt;
};
static MdnsAnnounce mdns_services[4];
static size_t mdns_count = 0;
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

static void appendf(String &out, const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

void sysinfo_html(String &out) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);

    out += "<!DOCTYPE HTML><html><head><meta charset='utf-8'>";
    appendf(out, "<title>%s</title>", HOSTNAME);
    out += HTTP_PAGE_STYLE;
    out += "</head><body>";
    appendf(out, "<h1>%s</h1><p>", HOSTNAME);
#ifdef OTA_WEB_UPDATER
    out += "<a href='/update'>Firmware update</a> | ";
#endif
    out += "<a href='/data'>JSON</a></p>";

    out += "<h3>Identity</h3><ul>";
    appendf(out, "<li>FW: %s</li>", FW_VERSION);
    appendf(out, "<li>Build SHA: %s</li>", BUILD_SHA);
    appendf(out, "<li>Build date: %s</li>", BUILD_DATE);
    appendf(out, "<li>MAC: %s</li>", WiFi.macAddress().c_str());
    appendf(out, "<li>Uptime: %lus</li></ul>", (unsigned long)(millis() / 1000));

    out += "<h3>Chip</h3><ul>";
    appendf(out, "<li>Model: %s</li>", ESP.getChipModel());
    appendf(out, "<li>Cores: %u</li>", ESP.getChipCores());
    appendf(out, "<li>Rev: %u</li>", ESP.getChipRevision());
    appendf(out, "<li>CPU: %u MHz</li></ul>", (unsigned)ESP.getCpuFreqMHz());

    out += "<h3>Memory</h3><ul>";
    appendf(out, "<li>Heap free: %u</li>", (unsigned)ESP.getFreeHeap());
    appendf(out, "<li>Heap min free: %u</li>", (unsigned)ESP.getMinFreeHeap());
    appendf(out, "<li>Heap total: %u</li>", (unsigned)ESP.getHeapSize());
    appendf(out, "<li>Heap max alloc: %u</li>", (unsigned)ESP.getMaxAllocHeap());
    if (psramFound()) {
        appendf(out, "<li>PSRAM size: %u</li>", (unsigned)ESP.getPsramSize());
        appendf(out, "<li>PSRAM free: %u</li>", (unsigned)ESP.getFreePsram());
    }
    out += "</ul>";

    out += "<h3>Flash</h3><ul>";
    appendf(out, "<li>Flash size: %u</li>", (unsigned)ESP.getFlashChipSize());
    appendf(out, "<li>Sketch size: %u</li>", (unsigned)ESP.getSketchSize());
    appendf(out, "<li>Free sketch space: %u</li></ul>", (unsigned)ESP.getFreeSketchSpace());

    out += "<h3>Partitions</h3><table><tr><th>Label</th><th>Type</th><th>Subtype</th><th>Offset</th><th>Size</th><th></th></tr>";
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        const char *cls = (p == running) ? "run" : (p == next) ? "nxt" : "";
        const char *tag = (p == running) ? "RUN" : (p == next) ? "NEXT" : "";
        appendf(out, "<tr class='%s'><td>%s</td><td>%s</td><td>0x%02x</td><td>0x%06x</td><td>%u</td><td>%s</td></tr>",
                cls, p->label, part_type_name(p->type), p->subtype,
                (unsigned)p->address, (unsigned)p->size, tag);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    out += "</table>";

    out += "<h3>Network</h3><ul>";
    appendf(out, "<li>STA SSID: %s</li>", WiFi.SSID().c_str());
    appendf(out, "<li>STA IP: %s</li>", WiFi.localIP().toString().c_str());
    appendf(out, "<li>STA RSSI: %d</li>", WiFi.RSSI());
    appendf(out, "<li>AP IP: %s</li>", WiFi.softAPIP().toString().c_str());
    appendf(out, "<li>AP clients: %u</li>", (unsigned)WiFi.softAPgetStationNum());
    appendf(out, "<li>mDNS: %s.local", HOSTNAME);
    if (mdns_count) {
        out += "<ul>";
        for (size_t i = 0; i < mdns_count; i++) {
            const MdnsAnnounce &m = mdns_services[i];
            appendf(out, "<li>%s.%s.%s.local:%u", m.instance, m.service, m.proto,
                    (unsigned)m.port);
            if (m.txt) appendf(out, " (%s)", m.txt);
            out += "</li>";
        }
        out += "</ul>";
    }
    out += "</li></ul>";

    out += "</body></html>";
}

static void json_kv_str(String &out, const char *k, const char *v, bool &first) {
    if (!first) out += ',';
    first = false;
    appendf(out, "\"%s\":\"%s\"", k, v);
}
static void json_kv_u(String &out, const char *k, uint32_t v, bool &first) {
    if (!first) out += ',';
    first = false;
    appendf(out, "\"%s\":%u", k, (unsigned)v);
}
static void json_kv_i(String &out, const char *k, int32_t v, bool &first) {
    if (!first) out += ',';
    first = false;
    appendf(out, "\"%s\":%d", k, (int)v);
}

void sysinfo_json(String &out) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);
    bool first = true;
    out += '{';
    json_kv_str(out, "hostname", HOSTNAME, first);
    json_kv_str(out, "fw_version", FW_VERSION, first);
    json_kv_str(out, "build_sha", BUILD_SHA, first);
    json_kv_str(out, "build_date", BUILD_DATE, first);
    json_kv_str(out, "mac", WiFi.macAddress().c_str(), first);
    json_kv_u(out,   "uptime_s", millis() / 1000, first);

    json_kv_str(out, "chip_model", ESP.getChipModel(), first);
    json_kv_u(out,   "chip_cores", ESP.getChipCores(), first);
    json_kv_u(out,   "chip_rev", ESP.getChipRevision(), first);
    json_kv_u(out,   "chip_cpu_mhz", ESP.getCpuFreqMHz(), first);

    json_kv_u(out,   "mem_free_heap", ESP.getFreeHeap(), first);
    json_kv_u(out,   "mem_min_free_heap", ESP.getMinFreeHeap(), first);
    json_kv_u(out,   "mem_heap_total", ESP.getHeapSize(), first);
    json_kv_u(out,   "mem_max_alloc", ESP.getMaxAllocHeap(), first);
    if (psramFound()) {
        json_kv_u(out, "mem_psram_size", ESP.getPsramSize(), first);
        json_kv_u(out, "mem_psram_free", ESP.getFreePsram(), first);
    }

    json_kv_u(out,   "flash_size", ESP.getFlashChipSize(), first);
    json_kv_u(out,   "flash_sketch_size", ESP.getSketchSize(), first);
    json_kv_u(out,   "flash_free_sketch", ESP.getFreeSketchSpace(), first);
    if (running) json_kv_str(out, "part_running", running->label, first);
    if (next)    json_kv_str(out, "part_next", next->label, first);

    if (!first) out += ',';
    first = false;
    out += "\"partitions\":[";
    bool pfirst = true;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it) {
        const esp_partition_t *p = esp_partition_get(it);
        if (!pfirst) out += ',';
        pfirst = false;
        appendf(out, "{\"label\":\"%s\",\"type\":\"%s\",\"subtype\":%u,\"offset\":%u,\"size\":%u}",
                p->label, part_type_name(p->type), (unsigned)p->subtype,
                (unsigned)p->address, (unsigned)p->size);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    out += ']';

    json_kv_str(out, "net_sta_ssid", WiFi.SSID().c_str(), first);
    json_kv_str(out, "net_sta_ip",   WiFi.localIP().toString().c_str(), first);
    json_kv_i(out,   "net_sta_rssi", WiFi.RSSI(), first);
    json_kv_str(out, "net_ap_ip",    WiFi.softAPIP().toString().c_str(), first);
    json_kv_u(out,   "net_ap_clients", WiFi.softAPgetStationNum(), first);

    if (!first) out += ',';
    first = false;
    out += "\"mdns\":[";
    for (size_t i = 0; i < mdns_count; i++) {
        const MdnsAnnounce &m = mdns_services[i];
        if (i) out += ',';
        appendf(out,
                "{\"instance\":\"%s\",\"service\":\"%s\",\"proto\":\"%s\",\"port\":%u,\"txt\":",
                m.instance, m.service, m.proto, (unsigned)m.port);
        if (m.txt) appendf(out, "\"%s\"", m.txt);
        else       out += "null";
        out += '}';
    }
    out += ']';
    out += '}';
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

void wifi_setup() {
#ifdef BOARD_HAS_SDIO_ESP_HOSTED
    WiFi.setPins(BOARD_SDIO_ESP_HOSTED_CLK, BOARD_SDIO_ESP_HOSTED_CMD,
                 BOARD_SDIO_ESP_HOSTED_D0, BOARD_SDIO_ESP_HOSTED_D1,
                 BOARD_SDIO_ESP_HOSTED_D2, BOARD_SDIO_ESP_HOSTED_D3,
                 BOARD_SDIO_ESP_HOSTED_RESET);
#endif


    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    macAddress = WiFi.macAddress();
    log_w("MAC address=%s", macAddress.c_str());

    macAddress.replace(":", "");

    String apSSID = "ESP32-" + macAddress;
    String apPASS = HOSTNAME;

    log_w("AP SSID: %s PW: %s", apSSID.c_str(), apPASS.c_str());
    WiFi.AP.create(apSSID, apPASS);
    WiFi.AP.enableIPv6();
    WiFi.AP.begin();
    WiFi.AP.enableDhcpCaptivePortal();

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

    if (MDNS.begin(HOSTNAME)) {
        log_i("starting MDNS for %s", HOSTNAME);
        MDNS.enableWorkstation();
        MDNS.addService("mqtt", "tcp", MQTT_PORT);
        MDNS.addService("mqtt-ws", "tcp", MQTTWS_PORT);
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("mqtt-ws", "tcp", "path", "/mqtt");

        mqttInstance   = HOSTNAME "-TCP-" + macAddress;
        mqttWsInstance = HOSTNAME "-WS-" + macAddress;
        httpInstance   = HOSTNAME;
        mdns_service_instance_name_set("_mqtt", "_tcp", mqttInstance.c_str());
        mdns_service_instance_name_set("_mqtt-ws", "_tcp", mqttWsInstance.c_str());

        add_mdns(mqttInstance.c_str(),   "_mqtt",    "_tcp", MQTT_PORT);
        add_mdns(mqttWsInstance.c_str(), "_mqtt-ws", "_tcp", MQTTWS_PORT, "path=/mqtt");
        add_mdns(httpInstance.c_str(),   "_http",    "_tcp", 80);
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
    wl_status_t s = WiFi.status();
    if (wifiStatus ^ s) {
        log_w("WiFi status change %u -> %u", wifiStatus, s);
        wifiStatus = s;
    }
    http_server.handleClient();
}
