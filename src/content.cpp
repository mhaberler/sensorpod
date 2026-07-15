#include <Esp.h>
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_arduino_version.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <stdarg.h>

#include "credstore.hpp"
#include "deviceconfig.hpp"
#include "http_server.hpp"
#include "mdns.h"
#include "mdns_client.hpp"
#include "mdns_state.hpp"
#include "mqtt.hpp"
#include "mqtt_client.hpp"

#if __has_include("build_info.hpp")
#include "build_info.hpp"
#endif

#ifdef BUILD_TAG
#define FW_VERSION BUILD_TAG
#else
#define FW_VERSION "n/a"
#endif

#ifndef BUILD_SHA
#define BUILD_SHA "unknown"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif
#ifndef BUILD_HOST
#define BUILD_HOST "unknown"
#endif
extern String hostName;
int wifi_sta_channel();
const char *wifi_sta_band();
const char *wifi_sta_encryption();
int wifi_ap_channel();
uint8_t safe_ap_station_num();

static void appendf(String &out, const char *fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out += buf;
}

static void append_html_attr(String &out, const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&#39;";
      break;
    default:
      out += c;
    }
  }
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

static void json_kv_str(String &out, const char *k, const char *v,
                        bool &first) {
  if (!first)
    out += ',';
  first = false;
  appendf(out, "\"%s\":\"%s\"", k, v);
}
static void json_kv_u(String &out, const char *k, uint32_t v, bool &first) {
  if (!first)
    out += ',';
  first = false;
  appendf(out, "\"%s\":%u", k, (unsigned)v);
}
static void json_kv_i(String &out, const char *k, int32_t v, bool &first) {
  if (!first)
    out += ',';
  first = false;
  appendf(out, "\"%s\":%d", k, (int)v);
}

void sysinfo_html(String &out, bool is_broker_mode) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  const bool wifi_sleep = DeviceConfig::isWifiSleepEnabled();

  out += "<!DOCTYPE HTML><html><head><meta charset='utf-8'>";
  appendf(out, "<title>%s</title>", hostName.c_str());
  out += HTTP_PAGE_STYLE;
  out += "<style>.config-section{background:#f5f5f5;padding:1em;margin:1em "
         "0;border-radius:5px}"
         ".config-btn{padding:8px "
         "16px;margin:4px;background:#007bff;color:white;border:none;border-"
         "radius:3px;cursor:pointer}"
         ".config-btn:hover{background:#0056b3}"
         ".config-btn.danger{background:#dc3545}"
         ".config-btn.danger:hover{background:#c82333}"
         "</style>";
  out += "</head><body>";
  appendf(out, "<h1>%s</h1><p>", hostName.c_str());
#ifdef OTA_WEB_UPDATER
  out += "<a href='/update'>Firmware update</a> | ";
#endif
  out += "<a href='/data'>JSON</a></p>";

  // Device Configuration Section
  out += "<div class='config-section'><h3>Device Configuration</h3>";
  appendf(out, "<p><strong>Current Role:</strong> %s</p>",
          is_broker_mode ? "Broker Mode" : "Client Mode");
  appendf(out,
          "<form id='roleForm'><input type='hidden' name='role' value='%s'>",
          is_broker_mode ? "client" : "broker");
  appendf(out,
          "<button type='button' class='config-btn' onclick='switchRole()'>"
          "Switch to %s Mode and Reboot</button> ",
          is_broker_mode ? "Client" : "Broker");
  out += "<button type='button' class='config-btn danger' "
         "onclick='reboot()'>Reboot</button></form>";

  out += "<form id='wifiSleepForm'>";
  appendf(out, "<p><strong>WiFi modem-sleep:</strong> %s</p>",
          wifi_sleep ? "Enabled" : "Disabled");
  appendf(out, "<input type='hidden' name='enabled' value='%d'>",
          wifi_sleep ? 0 : 1);
  appendf(out,
          "<button type='button' class='config-btn' "
          "onclick='saveWifiSleep()'>%s WiFi Modem-Sleep</button>",
          wifi_sleep ? "Disable" : "Enable");
  out += "<p><small>Disabling modem-sleep keeps mDNS discovery alive on a "
         "phone hotspot; costs more idle current. Applied immediately, no "
         "reboot.</small></p></form>";

  // BLE options
  {
    const bool ble_scan = DeviceConfig::isBleScanEnabled();
    const uint8_t ble_decoder = DeviceConfig::getBleDecoder();
    const bool ble_retain = DeviceConfig::isBleRetainUndecoded();
    const bool ble_dedup = DeviceConfig::isBleDedupEnabled();
    const uint32_t ble_age = DeviceConfig::getBleDedupAge();

    out += "<h3>BLE</h3><form id='bleForm'>";
    appendf(out, "<p><strong>BLE scanning:</strong> %s</p>",
            ble_scan ? "Enabled" : "Disabled");
    appendf(out,
            "<button type='button' class='config-btn' "
            "onclick='saveBleScan(%d)'>%s BLE Scanning and Reboot</button>",
            ble_scan ? 0 : 1, ble_scan ? "Disable" : "Enable");

    out += "<p><strong>Decoder:</strong> (applied immediately)</p>";
    static const char *decoder_labels[] = {"Theengs decoder",
                                           "BTHomeV2 decoder", "Custom decoder examples (Mikrotik and Qingping)",
                                           "Undecoded advertisements"};
    for (int i = 0; i < 4; i++) {
      appendf(out,
              "<label><input type='radio' name='bleDecoder' value='%d'%s "
              "onclick='saveBleDecoder(%d)'> %s</label><br>",
              i, i == ble_decoder ? " checked" : "", i, decoder_labels[i]);
    }

    appendf(out,
            "<p><label><input type='checkbox' id='bleRetain'%s "
            "onclick='saveBleRetain(this.checked)'> Retain undecoded "
            "advertisements (publish raw)</label></p>",
            ble_retain ? " checked" : "");

    appendf(out,
            "<p><label><input type='checkbox' id='bleDedup'%s> Deduplicate "
            "advertisements</label> "
            "<label>max age <input type='number' id='bleDedupAge' value='%u' "
            "min='1' style='width:4em'> s</label> "
            "<button type='button' class='config-btn' "
            "onclick='saveBleDedup()'>Apply</button></p>",
            ble_dedup ? " checked" : "", (unsigned)ble_age);
    out += "</form>";
  }

  // Client mode: show broker selection section
  if (!is_broker_mode) {
    out += "<p><strong>Broker Configuration (Client Mode):</strong></p>";
    out += "<p>Discovered brokers appear here after mDNS scan (runs every "
           "10s).</p>";
    out += "<form id='brokerForm'>"
           "<select id='brokerSelect'><option value=''>Select a "
           "broker...</option></select><br>"
           "<button type='button' class='config-btn' "
           "onclick='selectBroker()'>Connect to Broker</button> "
           "<button type='button' class='config-btn' "
           "onclick='refreshBrokers()'>Refresh List</button>"
           "</form>";
  }

  // WiFi credentials form (prefilled from saved NVS creds)
  String savedSsid, savedPass;
  loadWiFiCredentials(savedSsid, savedPass); // empty if none saved
  out += "<h3>WiFi Credentials</h3><form id='wifiForm'>";
  out += "<label>SSID:<br><input type='text' id='wifiSsid' value=\"";
  append_html_attr(out, savedSsid);
  out += "\"></label><br><label>Password:<br><input type='password' "
         "id='wifiPass' value=\"";
  append_html_attr(out, savedPass);
  out += "\"></label><br>";
  out += "<label><input type='checkbox' id='wifiShow' "
         "onclick=\"document.getElementById('wifiPass').type="
         "this.checked?'text':'password'\"> Show password</label><br>";
  out += "<button type='button' class='config-btn' onclick='saveWifi()'>Save "
         "&amp; Restart</button>";
  out += "<p><small>Leave SSID empty and save to clear stored "
         "credentials.</small></p></form>";

  out +=
      "<script>"
      "function switchRole(){var "
      "newRole=document.querySelector('#roleForm input[name=role]').value;"
      "fetch('/api/"
      "set-role',{method:'POST',headers:{'Content-Type':'application/"
      "x-www-form-urlencoded'},body:'role='+newRole})"
      ".then(r=>r.json()).then(d=>{alert('Role change in progress, device "
      "restarting...')}).catch(e=>alert('Error: '+e))}"
      "function "
      "reboot(){fetch('/api/"
      "reboot',{method:'POST'}).then(r=>r.json()).then(d=>{alert('Rebooting...'"
      ")}).catch(e=>alert('Error: '+e))}"
      "function loadBrokers(){"
      "fetch('/data').then(r=>r.json()).then(d=>{"
      "var sel=document.getElementById('brokerSelect');if(!sel)return;"
      "var brokers=d.discovered_brokers||[];"
      "if(brokers.length===0){sel.innerHTML='<option>No brokers found (scan "
      "every 10s)</option>';return;}"
      "sel.innerHTML='';"
      "brokers.forEach(b=>{var "
      "opt=document.createElement('option');opt.value=b.hostname;opt."
      "textContent=b.instance+' ('+b.ip+':'+b.port+')';sel.appendChild(opt);});"
      "}).catch(e=>console.error('Error loading brokers:',e))"
      "}"
      "function selectBroker(){var "
      "broker=document.getElementById('brokerSelect').value;if(!broker){alert('"
      "Please select a broker');return;}"
      "fetch('/api/"
      "set-broker',{method:'POST',headers:{'Content-Type':'application/"
      "x-www-form-urlencoded'},body:'hostname='+broker})"
      ".then(r=>r.json()).then(d=>{alert('Broker saved, "
      "restarting...')}).catch(e=>alert('Error: '+e))}"
      "function refreshBrokers(){loadBrokers()}"
      "function saveWifi(){"
      "var s=document.getElementById('wifiSsid').value;"
      "var p=document.getElementById('wifiPass').value;"
      "if(s.length===0&&!confirm('Empty SSID will CLEAR saved WiFi credentials "
      "and reboot. Continue?'))return;"
      "fetch('/api/"
      "set-wifi',{method:'POST',headers:{'Content-Type':'application/"
      "x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(s)+'&password='+"
      "encodeURIComponent(p)})"
      ".then(r=>r.json()).then(d=>{alert('WiFi saved, device "
      "restarting...')}).catch(e=>alert('Error: '+e))}"
      "function saveWifiSleep(){"
      "var e=document.querySelector('#wifiSleepForm "
      "input[name=enabled]').value;"
      "fetch('/api/"
      "set-wifi-sleep',{method:'POST',headers:{'Content-Type':"
      "'application/x-www-form-urlencoded'},body:'enabled='+e})"
      ".then(r=>r.json()).then(d=>{if(d.error){alert('Error: "
      "'+d.error);return;}"
      "alert('Saved & applied');location.reload();})"
      ".catch(e=>alert('Error: '+e))}"
      "function saveBleScan(e){"
      "fetch('/api/"
      "set-ble-scan',{method:'POST',headers:{'Content-Type':"
      "'application/x-www-form-urlencoded'},body:'enabled='+e})"
      ".then(r=>r.json()).then(d=>{if(d.error){alert('Error: "
      "'+d.error);return;}"
      "alert('Saved, device restarting...')})"
      ".catch(e=>alert('Error: '+e))}"
      "function saveBleDecoder(v){"
      "fetch('/api/"
      "set-ble-decoder',{method:'POST',headers:{'Content-Type':"
      "'application/x-www-form-urlencoded'},body:'decoder='+v})"
      ".then(r=>r.json()).then(d=>{if(d.error)alert('Error: '+d.error)})"
      ".catch(e=>alert('Error: '+e))}"
      "function saveBleRetain(c){"
      "fetch('/api/"
      "set-ble-retain',{method:'POST',headers:{'Content-Type':"
      "'application/x-www-form-urlencoded'},body:'enabled='+(c?1:0)})"
      ".then(r=>r.json()).then(d=>{if(d.error)alert('Error: '+d.error)})"
      ".catch(e=>alert('Error: '+e))}"
      "function saveBleDedup(){"
      "var c=document.getElementById('bleDedup').checked?1:0;"
      "var a=document.getElementById('bleDedupAge').value;"
      "fetch('/api/"
      "set-ble-dedup',{method:'POST',headers:{'Content-Type':"
      "'application/x-www-form-urlencoded'},body:'enabled='+c+'&age='+a})"
      ".then(r=>r.json()).then(d=>{if(d.error){alert('Error: "
      "'+d.error);return;}"
      "alert('Saved & applied')})"
      ".catch(e=>alert('Error: '+e))}"
      "window.addEventListener('load',loadBrokers);"
      "</script></div>";

  out += "<h3>Network</h3><ul>";
  appendf(out, "<li>STA SSID: %s</li>", WiFi.SSID().c_str());
  appendf(out, "<li>BSSID: %s</li>", WiFi.BSSIDstr().c_str());
  appendf(out, "<li>STA IP: %s</li>", WiFi.localIP().toString().c_str());
  appendf(out, "<li>STA IPv6 link-local: %s</li>",
          WiFi.STA.linkLocalIPv6().toString().c_str());
  appendf(out, "<li>STA IPv6 global: %s</li>",
          WiFi.STA.globalIPv6().toString().c_str());
  appendf(out, "<li>STA RSSI: %d</li>", WiFi.RSSI());
  appendf(out, "<li>STA Channel: %d</li>", wifi_sta_channel());
  appendf(out, "<li>STA Band: %s</li>", wifi_sta_band());
  appendf(out, "<li>STA Encryption: %s</li>", wifi_sta_encryption());
  appendf(out, "<li>AP IP: %s</li>", WiFi.softAPIP().toString().c_str());
  appendf(out, "<li>AP IPv6 link-local: %s</li>",
          WiFi.AP.linkLocalIPv6().toString().c_str());
  appendf(out, "<li>AP Channel: %d</li>", wifi_ap_channel());
  appendf(out, "<li>AP clients: %u</li>", (unsigned)safe_ap_station_num());
  appendf(out, "<li>mDNS: %s.local", hostName.c_str());
  if (mdns_count) {
    out += "<ul>";
    for (size_t i = 0; i < mdns_count; i++) {
      const MdnsAnnounce &m = mdns_services[i];
      appendf(out, "<li>%s.%s.%s.local:%u", m.instance, m.service, m.proto,
              (unsigned)m.port);
      if (m.txt)
        appendf(out, " (%s)", m.txt);
      out += "</li>";
    }
    out += "</ul>";
  }
  out += "</li></ul>";

  out += "<h3>MQTT</h3><ul>";
  if (is_broker_mode) {
    out += "<li>Status: running</li>";
    appendf(out, "<li>Clients connected: %d</li>", mqtt_broker.client_count);
    appendf(out, "<li>Subscriptions: %d</li>", mqtt_broker.subscribed);
    appendf(out, "<li>Messages routed: %d</li>", mqtt_broker.messages);
  } else {
    appendf(out, "<li>Status: %s</li>",
            mqtt_client.connected() ? "connected" : "disconnected");
    if (mqtt_client.connected_since_ms > 0)
      appendf(out, "<li>Connected for: %lus</li>",
              (unsigned long)(millis() - mqtt_client.connected_since_ms) /
                  1000);
    appendf(out, "<li>Reconnects: %u</li>", mqtt_client.total_reconnects);
    appendf(out, "<li>Messages sent: %u</li>", mqtt_client.messages_sent);
    appendf(out, "<li>Messages failed: %u</li>", mqtt_client.messages_failed);
  }
  out += "</ul>";

  out += "<h3>Identity</h3><ul>";
  appendf(out, "<li>FW: %s</li>", FW_VERSION);
  appendf(out, "<li>Build SHA: %s</li>", BUILD_SHA);
  appendf(out, "<li>Build date: %s</li>", BUILD_DATE);
  appendf(out, "<li>Build host: %s</li>", BUILD_HOST);
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
  appendf(out, "<li>Arduino: %s</li>", ESP_ARDUINO_VERSION_STR);
  appendf(out, "<li>IDF: %s</li>", esp_app_get_description()->idf_ver);
  appendf(out, "<li>MAC: %s</li>", WiFi.macAddress().c_str());
  appendf(out, "<li>Uptime: %lus</li></ul>", (unsigned long)(millis() / 1000));

  out += "<h3>SafeGithubOTA</h3><ul>";
#ifdef SGO_DEFAULT_OWNER
  appendf(out, "<li>Owner: %s</li>", SGO_DEFAULT_OWNER);
#endif
#ifdef SGO_DEFAULT_REPO
  appendf(out, "<li>Repo: %s</li>", SGO_DEFAULT_REPO);
#endif
#ifdef SGO_DEFAULT_BIN
  appendf(out, "<li>OTA bin: %s</li>", SGO_DEFAULT_BIN);
#endif
  out += "</ul>";

  out += "<h3>Chip</h3><ul>";
  appendf(out, "<li>Model: %s</li>", ESP.getChipModel());
  appendf(out, "<li>Cores: %u</li>", ESP.getChipCores());
  appendf(out, "<li>Rev: %u</li>", ESP.getChipRevision());
  appendf(out, "<li>CPU: %u MHz</li></ul>", (unsigned)ESP.getCpuFreqMHz());

  out += "<h3>Memory</h3><ul>";
  {
    uint32_t heap_total = ESP.getHeapSize();
    uint32_t heap_free = ESP.getFreeHeap();
    uint32_t heap_min_free = ESP.getMinFreeHeap();
    unsigned pct_free =
        heap_total ? (unsigned)(100ULL * heap_free / heap_total) : 0;
    unsigned hwm_pct =
        heap_total ? (unsigned)(100ULL * heap_min_free / heap_total) : 0;
    appendf(out, "<li>Heap total: %u</li>", (unsigned)heap_total);
    appendf(out, "<li>Heap free: %u (%u%% free)", (unsigned)heap_free,
            pct_free);
    appendf(out,
            " <span class='barwrap'><progress value='%u' max='100'>"
            "</progress><span class='tick' style='left:%u%%'></span>"
            "</span></li>",
            pct_free, hwm_pct);
    appendf(out, "<li>Heap min free (HWM): %u</li>", (unsigned)heap_min_free);
    appendf(out, "<li>Heap max alloc: %u</li>",
            (unsigned)ESP.getMaxAllocHeap());
  }
  if (psramFound()) {
    uint32_t psram_total = ESP.getPsramSize();
    uint32_t psram_free = ESP.getFreePsram();
    unsigned psram_pct_free =
        psram_total ? (unsigned)(100ULL * psram_free / psram_total) : 0;
    appendf(out, "<li>PSRAM size: %u</li>", (unsigned)psram_total);
    appendf(out, "<li>PSRAM free: %u (%u%% free)", (unsigned)psram_free,
            psram_pct_free);
    appendf(out, " <progress value='%u' max='100'></progress></li>",
            psram_pct_free);
  }
  out += "</ul>";

  out += "<h3>Flash</h3><ul>";
  appendf(out, "<li>Flash size: %u</li>", (unsigned)ESP.getFlashChipSize());
  appendf(out, "<li>Sketch size: %u</li>", (unsigned)ESP.getSketchSize());
  appendf(out, "<li>Free sketch space: %u</li></ul>",
          (unsigned)ESP.getFreeSketchSpace());

  out += "<h3>Partitions</h3><table><tr><th>Label</th><th>Type</"
         "th><th>Subtype</th><th>Offset</th><th>Size</th><th></th></tr>";
  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it) {
    const esp_partition_t *p = esp_partition_get(it);
    const char *cls = (p == running) ? "run" : (p == next) ? "nxt" : "";
    const char *tag = (p == running) ? "RUN" : (p == next) ? "NEXT" : "";
    appendf(out,
            "<tr "
            "class='%s'><td>%s</td><td>%s</td><td>0x%02x</td><td>0x%06x</"
            "td><td>%u",
            cls, p->label, part_type_name(p->type), p->subtype,
            (unsigned)p->address, (unsigned)p->size);
    if (p == running && p->size > 0) {
      unsigned pct = (unsigned)(100ULL * ESP.getSketchSize() / p->size);
      appendf(out, "<br><progress value='%u' max='100'></progress> %u%%", pct,
              pct);
    }
    appendf(out, "</td><td>%s</td></tr>", tag);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  out += "</table>";

  out += "</body></html>";
}

void sysinfo_json(String &out, bool is_broker_mode) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  const bool wifi_sleep = DeviceConfig::isWifiSleepEnabled();
  bool first = true;
  out += '{';
  json_kv_str(out, "hostname", hostName.c_str(), first);
  json_kv_str(out, "fw_version", FW_VERSION, first);
  json_kv_str(out, "build_sha", BUILD_SHA, first);
  json_kv_str(out, "build_date", BUILD_DATE, first);
  json_kv_str(out, "build_host", BUILD_HOST, first);
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
  json_kv_str(out, "arduino_ver", ESP_ARDUINO_VERSION_STR, first);
  json_kv_str(out, "idf_ver", esp_app_get_description()->idf_ver, first);
#ifdef SGO_DEFAULT_OWNER
  json_kv_str(out, "sgo_owner", SGO_DEFAULT_OWNER, first);
#endif
#ifdef SGO_DEFAULT_REPO
  json_kv_str(out, "sgo_repo", SGO_DEFAULT_REPO, first);
#endif
#ifdef SGO_DEFAULT_BIN
  json_kv_str(out, "sgo_bin", SGO_DEFAULT_BIN, first);
#endif
  json_kv_u(out, "uptime_s", millis() / 1000, first);
  json_kv_u(out, "broker_mode", is_broker_mode ? 1 : 0, first);
  json_kv_u(out, "wifi_sleep", wifi_sleep ? 1 : 0, first);
  json_kv_u(out, "ble_scan", DeviceConfig::isBleScanEnabled() ? 1 : 0, first);
  json_kv_u(out, "ble_decoder", DeviceConfig::getBleDecoder(), first);
  json_kv_u(out, "ble_retain", DeviceConfig::isBleRetainUndecoded() ? 1 : 0,
            first);
  json_kv_u(out, "ble_dedup", DeviceConfig::isBleDedupEnabled() ? 1 : 0, first);
  json_kv_u(out, "ble_dedup_age", DeviceConfig::getBleDedupAge(), first);

  json_kv_str(out, "chip_model", ESP.getChipModel(), first);
  json_kv_u(out, "chip_cores", ESP.getChipCores(), first);
  json_kv_u(out, "chip_rev", ESP.getChipRevision(), first);
  json_kv_u(out, "chip_cpu_mhz", ESP.getCpuFreqMHz(), first);

  json_kv_u(out, "mem_free_heap", ESP.getFreeHeap(), first);
  json_kv_u(out, "mem_min_free_heap", ESP.getMinFreeHeap(), first);
  json_kv_u(out, "mem_heap_total", ESP.getHeapSize(), first);
  json_kv_u(out, "mem_max_alloc", ESP.getMaxAllocHeap(), first);
  if (psramFound()) {
    json_kv_u(out, "mem_psram_size", ESP.getPsramSize(), first);
    json_kv_u(out, "mem_psram_free", ESP.getFreePsram(), first);
  }

  json_kv_u(out, "flash_size", ESP.getFlashChipSize(), first);
  json_kv_u(out, "flash_sketch_size", ESP.getSketchSize(), first);
  json_kv_u(out, "flash_free_sketch", ESP.getFreeSketchSpace(), first);
  if (running)
    json_kv_str(out, "part_running", running->label, first);
  if (next)
    json_kv_str(out, "part_next", next->label, first);

  if (!first)
    out += ',';
  first = false;
  out += "\"partitions\":[";
  bool pfirst = true;
  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it) {
    const esp_partition_t *p = esp_partition_get(it);
    if (!pfirst)
      out += ',';
    pfirst = false;
    appendf(out,
            "{\"label\":\"%s\",\"type\":\"%s\",\"subtype\":%u,\"offset\":%u,"
            "\"size\":%u}",
            p->label, part_type_name(p->type), (unsigned)p->subtype,
            (unsigned)p->address, (unsigned)p->size);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  out += ']';

  json_kv_str(out, "net_sta_ssid", WiFi.SSID().c_str(), first);
  json_kv_str(out, "net_sta_ip", WiFi.localIP().toString().c_str(), first);
  json_kv_str(out, "net_sta_ip6_ll",
              WiFi.STA.linkLocalIPv6().toString().c_str(), first);
  json_kv_str(out, "net_sta_ip6_global",
              WiFi.STA.globalIPv6().toString().c_str(), first);
  json_kv_i(out, "net_sta_rssi", WiFi.RSSI(), first);
  json_kv_i(out, "net_sta_channel", wifi_sta_channel(), first);
  json_kv_str(out, "net_sta_band", wifi_sta_band(), first);
  json_kv_str(out, "net_sta_encryption", wifi_sta_encryption(), first);
  json_kv_str(out, "net_ap_ip", WiFi.softAPIP().toString().c_str(), first);
  json_kv_str(out, "net_ap_ip6_ll", WiFi.AP.linkLocalIPv6().toString().c_str(),
              first);
  json_kv_i(out, "net_ap_channel", wifi_ap_channel(), first);
  json_kv_u(out, "net_ap_clients", safe_ap_station_num(), first);

  if (!first)
    out += ',';
  first = false;
  out += "\"mdns\":[";
  for (size_t i = 0; i < mdns_count; i++) {
    const MdnsAnnounce &m = mdns_services[i];
    if (i)
      out += ',';
    appendf(out,
            "{\"instance\":\"%s\",\"service\":\"%s\",\"proto\":\"%s\",\"port\":"
            "%u,\"txt\":",
            m.instance, m.service, m.proto, (unsigned)m.port);
    if (m.txt)
      appendf(out, "\"%s\"", m.txt);
    else
      out += "null";
    out += '}';
  }
  out += ']';

  if (!first)
    out += ',';
  first = false;
  out += "\"discovered_brokers\":[";
  auto brokers = mdns_client.get_last_brokers();
  for (size_t i = 0; i < brokers.size(); i++) {
    if (i)
      out += ',';
    const char *display_name = (brokers[i].instance_name.length() > 0)
                                   ? brokers[i].instance_name.c_str()
                                   : brokers[i].hostname.c_str();
    appendf(
        out,
        "{\"instance\":\"%s\",\"hostname\":\"%s\",\"ip\":\"%s\",\"port\":%u}",
        display_name, brokers[i].hostname.c_str(), brokers[i].ip.c_str(),
        (unsigned)brokers[i].port);
  }
  out += ']';

  if (!first)
    out += ',';
  first = false;
  out += "\"mqtt\":{";
  if (is_broker_mode) {
    appendf(out, "\"clients\":%d,\"subscriptions\":%d,\"messages_routed\":%d",
            mqtt_broker.client_count, mqtt_broker.subscribed,
            mqtt_broker.messages);
  } else {
    appendf(out, "\"connected\":%s",
            mqtt_client.connected() ? "true" : "false");
    if (mqtt_client.connected_since_ms > 0)
      appendf(out, ",\"connected_for_s\":%lu",
              (unsigned long)(millis() - mqtt_client.connected_since_ms) /
                  1000);
    appendf(out,
            ",\"reconnects\":%u,\"messages_sent\":%u,\"messages_failed\":%u",
            mqtt_client.total_reconnects, mqtt_client.messages_sent,
            mqtt_client.messages_failed);
  }
  out += '}';
  out += '}';
}
