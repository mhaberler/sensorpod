#include <ESP_HostedOTA.h>
#include <ESPmDNS.h>
#include <Network.h>
#include <PicoMQTT.h>
#include <WiFi.h>
#ifdef BOARD_HAS_SDIO_ESP_HOSTED
#include <esp32-hal-hosted.h>
#endif
#include <esp_netif.h>
#include <esp_wifi.h>

#include "credstore.hpp"
#include "deviceconfig.hpp"
#if defined(ESP_HOSTED_DOWNGRADE)
#include "hosted_ota.hpp"
#endif
#include "http_server.hpp"
#include "mdns.h"
#include "mdns_state.hpp"

extern bool is_broker_mode;
extern bool wifi_sleep_enabled;
extern bool improv_provisioning;
extern bool ble_scan_enabled;

void blescanner_setup();
void blescanner_stop();
bool blescanner_started();

String hostName;

// Cached STA credentials for the reconnect watchdog (see wifi_loop). The
// Arduino WiFi driver treats WIFI_REASON_AUTH_FAIL as fatal and stops
// auto-reconnecting, which strands the STA when an AP (e.g. a phone hotspot)
// disappears and returns not-yet-ready. The watchdog re-issues the connect.
static String sta_ssid, sta_pass;

#define STA_RECONNECT_TIMEOUT_MS 20000  // STA down this long -> start retrying
#define STA_RECONNECT_INTERVAL_MS 15000 // min gap between our retry attempts

static uint8_t prev_clients = 255;

MdnsAnnounce mdns_services[4];
size_t mdns_count = 0;
static String mqttInstance, mqttWsInstance, httpInstance;
static wl_status_t wifiStatus = WL_NO_SHIELD;

static int sta_channel = 0;
static wifi_auth_mode_t sta_authmode = WIFI_AUTH_OPEN;
static bool sta_is_5g = false;

// Set while updateEspHostedSlave() is flashing the ESP-Hosted co-processor.
// The flash runs over the same RPC/SDIO transport as WiFi.* accessors and
// hosted BLE (NimBLE HCI). Concurrent WiFi RPC or BLE scan during flash
// faults the transport (Req_OTAWrite timeouts, rpc_wifi_* panics). Callers
// check hosted_update_busy() and skip WiFi RPC / BLE work for the duration:
// wifi_loop() skips webserver_loop(), main.cpp skips status publish, and
// BLE is stopped before the OTA runs.
static volatile bool hosted_update_in_progress = false;
static uint8_t last_ap_station_num = 0;

#ifdef BOARD_HAS_SDIO_ESP_HOSTED
// GOT_IP only arms this; wifi_loop() runs the OTA off the NetworkEvents task
// after stopping BLE. Avoids HTTPS+flash on the event worker and BLE HCI
// racing hostedWriteUpdate.
static volatile bool hosted_ota_pending = false;
static bool hosted_ota_attempted = false;

#if defined(ESP_HOSTED_DOWNGRADE)
// Debug forced downgrade: armed by POST /api/hosted-downgrade, run in
// wifi_loop() (not the HTTP handler) with the same BLE quiesce as upgrade.
static volatile bool hosted_downgrade_pending = false;
static uint32_t hosted_downgrade_maj = 0;
static uint32_t hosted_downgrade_min = 0;
static uint32_t hosted_downgrade_pat = 0;

bool request_hosted_downgrade(uint32_t maj, uint32_t min, uint32_t pat) {
  if (!hostedIsInitialized()) {
    log_e("hosted downgrade: not initialized");
    return false;
  }
  if (hosted_update_in_progress || hosted_downgrade_pending) {
    log_e("hosted downgrade: flash already pending/in progress");
    return false;
  }
  if (!Network.isOnline()) {
    log_e("hosted downgrade: network not online");
    return false;
  }
  // Supersede a pending auto-upgrade check so the debug path wins.
  if (hosted_ota_pending) {
    log_w("hosted downgrade: cancelling pending auto-upgrade check");
    hosted_ota_pending = false;
  }
  hosted_downgrade_maj = maj;
  hosted_downgrade_min = min;
  hosted_downgrade_pat = pat;
  hosted_downgrade_pending = true;
  log_w("hosted downgrade armed: target %lu.%lu.%lu", (unsigned long)maj,
        (unsigned long)min, (unsigned long)pat);
  return true;
}
#endif // ESP_HOSTED_DOWNGRADE
#endif // BOARD_HAS_SDIO_ESP_HOSTED

bool hosted_update_busy() { return hosted_update_in_progress; }

#ifdef BOARD_HAS_SDIO_ESP_HOSTED
bool hosted_ota_done() { return hosted_ota_attempted; }
#else
bool hosted_ota_done() { return true; }
#endif

uint8_t safe_ap_station_num() {
  if (hosted_update_in_progress) {
    return last_ap_station_num;
  }
  last_ap_station_num = WiFi.softAPgetStationNum();
  return last_ap_station_num;
}

static const char *auth_mode_str(wifi_auth_mode_t mode) {
  switch (mode) {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA_PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2_PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA_WPA2_PSK";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3_PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2_WPA3_PSK";
  case WIFI_AUTH_OWE:
    return "OWE";
  default:
    return "?";
  }
}

int wifi_sta_channel() { return sta_channel; }
const char *wifi_sta_band() { return sta_is_5g ? "5GHz" : "2.4GHz"; }
const char *wifi_sta_encryption() { return auth_mode_str(sta_authmode); }

// ESP32's single-radio AP+STA mode forces the AP onto whatever channel the
// STA is connected to, once STA associates - the requested channel passed
// to WiFi.AP.create() is only a starting point. Read back the effective
// channel rather than trusting the request.
int wifi_ap_channel() {
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_AP, &conf) == ESP_OK) {
    return conf.ap.channel;
  }
  return 0;
}

static void add_mdns(const char *instance, const char *svc, const char *proto,
                     uint16_t port, const char *txt = nullptr) {
  if (mdns_count >= sizeof(mdns_services) / sizeof(mdns_services[0]))
    return;
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
    {
      wifi_ap_record_t info;
      if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        sta_channel = info.primary;
        sta_authmode = info.authmode;
        sta_is_5g =
            sta_channel > 14 || info.phy_11a || info.phy_11ac || info.phy_11ax;
        log_w("STA band=%s channel=%u encryption=%s",
              sta_is_5g ? "5GHz" : "2.4GHz", sta_channel,
              auth_mode_str(sta_authmode));
      }
      log_w("AP channel=%d (may have followed STA)", wifi_ap_channel());
    }
#ifdef BOARD_HAS_SDIO_ESP_HOSTED
    // Defer OTA to wifi_loop(): NetworkEvents must not block on HTTPS+flash,
    // and BLE must be quiesced first (shared SDIO to the C6).
    if (Network.isOnline())
      hosted_ota_pending = true;
#endif
    break;
  case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
    log_d("AP client got IP");
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    log_w("STA disconnected");
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
    log_w("STA IPv6: link-local %s global %s",
          WiFi.STA.linkLocalIPv6().toString().c_str(),
          WiFi.STA.globalIPv6().toString().c_str());
    break;
  case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
    log_w("AP IPv6: link-local %s", WiFi.AP.linkLocalIPv6().toString().c_str());
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

void cacheStaCredentials(const String &ssid, const String &pass) {
  sta_ssid = ssid;
  sta_pass = pass;
}

void stopSta() { WiFi.STA.disconnect(false, true); }

// Scans in STA mode and picks the least-congested 2.4GHz channel (1-11,
// preferring the non-overlapping 1/6/11 on ties). Only meaningful when no
// STA will connect: ESP32's single-radio AP+STA mode forces the AP onto
// whatever channel the STA associates to once it connects (see
// wifi_ap_channel()'s comment), so this pick only sticks permanently in
// the AP-only case. Blocking, ~2-6s.
static int pick_low_interference_channel() {
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  int count[12] = {}; // index 1..11 used
  for (int i = 0; i < n; i++) {
    int c = WiFi.channel(i);
    log_w("AP channel scan: ch=%d rssi=%d ssid=%s", c, WiFi.RSSI(i),
          WiFi.SSID(i).c_str());
    if (c >= 1 && c <= 11)
      count[c]++;
  }
  WiFi.scanDelete();
  for (int c = 1; c <= 11; c++) {
    if (count[c] > 0)
      log_w("AP channel scan: channel %d in use by %d network(s)", c, count[c]);
  }
  int best = 1;
  for (int c = 2; c <= 11; c++) {
    if (count[c] < count[best]) {
      best = c;
    } else if (count[c] == count[best]) {
      bool c_pref = (c == 1 || c == 6 || c == 11);
      bool best_pref = (best == 1 || best == 6 || best == 11);
      if (c_pref && !best_pref)
        best = c;
    }
  }
  log_w("AP channel scan: %d networks seen, picked channel %d (count=%d)", n,
        best, count[best]);
  return best;
}

static void start_ap(int channel) {
  String apSSID = hostName + ".local";
  String apPASS = hostName + ".local";
  log_w("AP SSID: %s PW: %s", apSSID.c_str(), apPASS.c_str());
  WiFi.AP.create(apSSID, apPASS, channel);
  WiFi.AP.enableIPv6();
  WiFi.AP.begin();
  WiFi.AP.enableDhcpCaptivePortal();
  log_w("AP channel=%d", wifi_ap_channel());
}

static void start_sta(const String &ssid, const String &pass) {
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

  // Only worth scanning when no STA will connect: once STA associates, the
  // AP is forced onto its channel regardless of what we pick here (see
  // pick_low_interference_channel()'s comment).
  String ssid, pass;
  bool have_creds = loadWiFiCredentials(ssid, pass);
  int ap_channel = 1;
  if (!have_creds) {
    ap_channel = pick_low_interference_channel();
    WiFi.mode(WIFI_AP_STA); // scan leaves mode as WIFI_STA
  }

  WiFi.STA.begin(false);
  // Must be set before connect: IPv6 (link-local + SLAAC) only comes up at
  // netif start. Without it the mDNS responder has no AAAA to announce on
  // the STA network.
  WiFi.STA.enableIPv6();
  WiFi.STA.setAutoReconnect(true);

  // Disable STA modem-sleep by default so the responder keeps hearing mDNS
  // multicast queries on a phone hotspot (a sleeping STA misses buffered
  // multicast, letting discovered records expire). Toggle via web UI.
  WiFi.setSleep(wifi_sleep_enabled);
  log_w("WiFi modem-sleep: %s", WiFi.getSleep() ? "enabled" : "disabled");

  // AP always on (in both Broker and Client modes)
  start_ap(ap_channel);

  // Branch on role for STA and mDNS setup
  if (is_broker_mode) {
    // Broker mode: STA optional
    if (have_creds) {
      log_w("Broker mode: loaded creds, starting STA");
      cacheStaCredentials(ssid, pass);
      start_sta(ssid, pass);
    } else {
      log_w("Broker mode: no creds, AP-only");
    }
  } else {
    // Client mode: STA required
    if (have_creds) {
      log_w("Client mode: loaded creds, starting STA");
      cacheStaCredentials(ssid, pass);
      start_sta(ssid, pass);
    } else {
      log_w("Client mode: no creds, Improv provisioning required");
    }
  }

  webserver_setup();

  // mDNS: Broker mode announces, Client mode will discover
  if (MDNS.begin(hostName)) {
    log_i("starting MDNS for %s", hostName.c_str());
    MDNS.addService("http", "tcp", 80);
    if (is_broker_mode) {
      // Broker mode: announce self
      MDNS.addService("mqtt", "tcp", MQTT_PORT);
      MDNS.addService("mqtt-ws", "tcp", MQTTWS_PORT);
      MDNS.addServiceTxt("mqtt-ws", "tcp", "path", "/mqtt");

      mqttInstance = APP_NAME " MQTT broker - TCP at " + hostName;
      mqttWsInstance = APP_NAME " MQTT broker - WS at " + hostName;
      httpInstance = hostName;
      mdns_service_instance_name_set("_mqtt", "_tcp", mqttInstance.c_str());
      mdns_service_instance_name_set("_mqtt-ws", "_tcp",
                                     mqttWsInstance.c_str());

      add_mdns(mqttInstance.c_str(), "_mqtt", "_tcp", MQTT_PORT);
      add_mdns(mqttWsInstance.c_str(), "_mqtt-ws", "_tcp", MQTTWS_PORT,
               "path=/mqtt");
      add_mdns(httpInstance.c_str(), "_http", "_tcp", 80);
    } else {
      // Client mode: just enable workstation, no announcements
      log_d("Client mode: mDNS workstation enabled for discovery");
    }
  }

  Network.onEvent(onNetworkEvent);
}

void wifi_loop() {
  uint8_t clients = safe_ap_station_num();
  if (prev_clients ^ clients) {
    log_w("AP clients: %u", clients);
    prev_clients = clients;
  }
  wl_status_t s = WiFi.status();
  if (wifiStatus ^ s) {
    log_w("WiFi status change %u -> %u", wifiStatus, s);
    wifiStatus = s;
  }

  unsigned long now = millis();

  // STA reconnect watchdog: the Arduino WiFi driver gives up permanently on
  // WIFI_REASON_AUTH_FAIL, so after a hotspot toggles off/on the STA can stay
  // stuck at WL_IDLE_STATUS forever. Re-issue the connect ourselves once STA
  // has been down long enough, unless Improv is provisioning.
  if (sta_ssid.length() > 0 && !improv_provisioning) {
    static unsigned long last_sta_ok = 0;
    static unsigned long last_sta_retry = 0;
    if (s == WL_CONNECTED) {
      last_sta_ok = now;
    } else if (now - last_sta_ok > STA_RECONNECT_TIMEOUT_MS &&
               now - last_sta_retry > STA_RECONNECT_INTERVAL_MS) {
      last_sta_retry = now;
      log_w("STA reconnect watchdog: re-attempting connect");
      startStaAttempt(sta_ssid, sta_pass);
    }
  }

  // Handle HTTP before hosted flash work so POST /api/hosted-downgrade can arm
  // in the same loop turn. Skip while flashing (WiFi RPC races SDIO OTA).
  if (!hosted_update_in_progress) {
    webserver_loop();
  }

#ifdef BOARD_HAS_SDIO_ESP_HOSTED
#if defined(ESP_HOSTED_DOWNGRADE)
  if (hosted_downgrade_pending) {
    hosted_downgrade_pending = false;
    hosted_update_in_progress = true;
    char url[128];
    buildHostedFwUrl(url, sizeof(url), hosted_downgrade_maj,
                     hosted_downgrade_min, hosted_downgrade_pat);
    // Log versions before quiescing BLE (extra RPC after stop hurts OTA begin).
    if (hostedIsInitialized()) {
      uint32_t eh = 0, en = 0, ep = 0, fh = 0, fn = 0, fp = 0;
      (void)hostedHasUpdate();
      hostedGetHostVersion(&eh, &en, &ep);
      hostedGetSlaveVersion(&fh, &fn, &fp);
      log_w("esp-hosted %s fw: expected %lu.%lu.%lu, found %lu.%lu.%lu; "
            "forcing downgrade to %lu.%lu.%lu",
            hostedGetSlaveTargetName(), (unsigned long)eh, (unsigned long)en,
            (unsigned long)ep, (unsigned long)fh, (unsigned long)fn,
            (unsigned long)fp, (unsigned long)hosted_downgrade_maj,
            (unsigned long)hosted_downgrade_min,
            (unsigned long)hosted_downgrade_pat);
    }
    blescanner_stop();
    delay(500); // let hosted BLE/HCI drain before OTA RPC
    log_w("hosted downgrade URL: %s", url);
    bool ok = flashEspHostedFromUrl(url);
    hosted_update_in_progress = false;
    if (ok) {
      ESP.restart();
    }
    // Failed OTA begin/write often wedges SDIO RPC — do not restart BLE.
    log_e("hosted downgrade failed; restarting to recover SDIO");
    delay(200);
    ESP.restart();
  } else if (hosted_ota_pending) {
#else
  if (hosted_ota_pending) {
#endif // ESP_HOSTED_DOWNGRADE
    hosted_ota_pending = false;
    hosted_ota_attempted = true;
    hosted_update_in_progress = true;
    bool need_update = false;
    if (hostedIsInitialized()) {
      uint32_t eh = 0, en = 0, ep = 0, fh = 0, fn = 0, fp = 0;
      // Refresh slave version into HAL cache (also decides update need).
      need_update = hostedHasUpdate();
      hostedGetHostVersion(&eh, &en, &ep);
      hostedGetSlaveVersion(&fh, &fn, &fp);
      log_w("esp-hosted %s fw: expected %lu.%lu.%lu, found %lu.%lu.%lu",
            hostedGetSlaveTargetName(), (unsigned long)eh, (unsigned long)en,
            (unsigned long)ep, (unsigned long)fh, (unsigned long)fn,
            (unsigned long)fp);
    }
    if (need_update) {
      blescanner_stop();
      delay(500);
    }
    bool updated = updateEspHostedSlave();
    hosted_update_in_progress = false;
    if (updated) {
      // Restart host so the new co-processor firmware activates cleanly.
      ESP.restart();
    }
    if (need_update && !updated) {
      log_e("hosted upgrade failed; restarting to recover SDIO");
      delay(200);
      ESP.restart();
    }
    if (ble_scan_enabled && !blescanner_started())
      blescanner_setup();
  }
#endif // BOARD_HAS_SDIO_ESP_HOSTED
}
