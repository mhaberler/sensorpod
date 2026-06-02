#include <stdarg.h>
#include <WiFi.h>
#include <Esp.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>

#include "http_server.hpp"
#include "mdns_state.hpp"
#include "mdns.h"

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
extern String hostName;

static void appendf(String &out, const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
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

void sysinfo_html(String &out) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);

    out += "<!DOCTYPE HTML><html><head><meta charset='utf-8'>";
    appendf(out, "<title>%s</title>", hostName.c_str());
    out += HTTP_PAGE_STYLE;
    out += "</head><body>";
    appendf(out, "<h1>%s</h1><p>", hostName.c_str());
#ifdef OTA_WEB_UPDATER
    out += "<a href='/update'>Firmware update</a> | ";
#endif
    out += "<a href='/data'>JSON</a></p>";

    out += "<h3>Identity</h3><ul>";
    appendf(out, "<li>FW: %s</li>", FW_VERSION);
    appendf(out, "<li>Build SHA: %s</li>", BUILD_SHA);
    appendf(out, "<li>Build date: %s</li>", BUILD_DATE);
#ifdef BUILD_ENV
    appendf(out, "<li>Build env: %s</li>", BUILD_ENV);
#endif
#ifdef BUILD_BOARD
    appendf(out, "<li>Build board: %s</li>", BUILD_BOARD);
#endif
#ifdef BUILD_BOARD_NAME
    appendf(out, "<li>Build board name: %s</li>", BUILD_BOARD_NAME);
#endif
#ifdef BUILD_MCU
    appendf(out, "<li>Build MCU: %s</li>", BUILD_MCU);
#endif
#ifdef BUILD_VARIANT
    appendf(out, "<li>Build variant: %s</li>", BUILD_VARIANT);
#endif
#ifdef BUILD_TYPE
    appendf(out, "<li>Build type: %s</li>", BUILD_TYPE);
#endif
#ifdef BUILD_PARTITIONS
    appendf(out, "<li>Build partitions: %s</li>", BUILD_PARTITIONS);
#endif
#ifdef BUILD_FLASH_SIZE
    appendf(out, "<li>Build flash size: %s</li>", BUILD_FLASH_SIZE);
#endif
#ifdef BUILD_FRAMEWORK
    appendf(out, "<li>Build framework: %s</li>", BUILD_FRAMEWORK);
#endif
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
    appendf(out, "<li>mDNS: %s.local", hostName.c_str());
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

void sysinfo_json(String &out) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);
    bool first = true;
    out += '{';
    json_kv_str(out, "hostName.c_str()", hostName.c_str(), first);
    json_kv_str(out, "fw_version", FW_VERSION, first);
    json_kv_str(out, "build_sha", BUILD_SHA, first);
    json_kv_str(out, "build_date", BUILD_DATE, first);
#ifdef BUILD_ENV
    json_kv_str(out, "build_env", BUILD_ENV, first);
#endif
#ifdef BUILD_BOARD
    json_kv_str(out, "build_board", BUILD_BOARD, first);
#endif
#ifdef BUILD_BOARD_NAME
    json_kv_str(out, "build_board_name", BUILD_BOARD_NAME, first);
#endif
#ifdef BUILD_MCU
    json_kv_str(out, "build_mcu", BUILD_MCU, first);
#endif
#ifdef BUILD_VARIANT
    json_kv_str(out, "build_variant", BUILD_VARIANT, first);
#endif
#ifdef BUILD_TYPE
    json_kv_str(out, "build_type", BUILD_TYPE, first);
#endif
#ifdef BUILD_PARTITIONS
    json_kv_str(out, "build_partitions", BUILD_PARTITIONS, first);
#endif
#ifdef BUILD_FLASH_SIZE
    json_kv_str(out, "build_flash_size", BUILD_FLASH_SIZE, first);
#endif
#ifdef BUILD_FRAMEWORK
    json_kv_str(out, "build_framework", BUILD_FRAMEWORK, first);
#endif
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
