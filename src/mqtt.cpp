#include "mqtt.hpp"
#include <cstdio>
#include <cstring>

#ifndef MQTT_CONN_TRACK
#define MQTT_CONN_TRACK 0
#endif
#ifndef MQTT_CONN_MAX_CLIENTS
#define MQTT_CONN_MAX_CLIENTS 8
#endif
#ifndef MQTT_CONN_MAX_TOPICS
#define MQTT_CONN_MAX_TOPICS 16
#endif
#ifndef MQTT_CONN_TOPIC_LEN
#define MQTT_CONN_TOPIC_LEN 64
#endif
#ifndef MQTT_CONN_CLIENT_ID_LEN
#define MQTT_CONN_CLIENT_ID_LEN 65
#endif

// Remote address / transport of the most recently accepted broker connection.
// Valid inside on_connected(): PicoMQTT's loop() calls on_connected()
// immediately after the accept that produced the client.
IPAddress mqtt_last_client_ip;
MqttTransport mqtt_last_client_transport = MqttTransport::TCP;

// WiFiServer::accept() is non-virtual, but PicoMQTT/PicoWebsocket reach it
// statically through templates instantiated on the declared type.
class TrackingWiFiServer : public WiFiServer {
public:
  TrackingWiFiServer(uint16_t port, MqttTransport transport)
      : WiFiServer(port), _transport(transport) {}
  NetworkClient accept() {
    NetworkClient c = WiFiServer::accept();
    if (c) {
      mqtt_last_client_ip = c.remoteIP();
      mqtt_last_client_transport = _transport;
    }
    return c;
  }

private:
  MqttTransport _transport;
};

static TrackingWiFiServer tcp_server(MQTT_PORT, MqttTransport::TCP);
static TrackingWiFiServer websocket_underlying_server(MQTTWS_PORT,
                                                      MqttTransport::WS);
PicoWebsocket::Server<TrackingWiFiServer>
    websocket_server(websocket_underlying_server);

CustomMQTTServer mqtt_broker(tcp_server, websocket_server);
MQTTDevice *mqtt_device = nullptr;

#if MQTT_CONN_TRACK

struct ConnEntry {
  bool used = false;
  char client_id[MQTT_CONN_CLIENT_ID_LEN] = {};
  IPAddress ip;
  MqttTransport transport = MqttTransport::TCP;
  uint8_t topic_count = 0;
  char topics[MQTT_CONN_MAX_TOPICS][MQTT_CONN_TOPIC_LEN] = {};
};

static ConnEntry conns[MQTT_CONN_MAX_CLIENTS];
static bool track_overflow_logged = false;

static ConnEntry *find_conn(const char *client_id) {
  if (!client_id)
    return nullptr;
  for (auto &c : conns) {
    if (c.used && strcmp(c.client_id, client_id) == 0)
      return &c;
  }
  return nullptr;
}

static ConnEntry *alloc_conn() {
  for (auto &c : conns) {
    if (!c.used)
      return &c;
  }
  return nullptr;
}

static void copy_trunc(char *dst, size_t dst_len, const char *src) {
  if (!dst || dst_len == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

void mqtt_conn_on_connected(const char *client_id) {
  ConnEntry *e = find_conn(client_id);
  if (!e)
    e = alloc_conn();
  if (!e) {
    if (!track_overflow_logged) {
      log_w("MQTT conn track: max clients (%d) — further connects untracked",
            MQTT_CONN_MAX_CLIENTS);
      track_overflow_logged = true;
    }
    return;
  }
  e->used = true;
  copy_trunc(e->client_id, sizeof(e->client_id), client_id);
  e->ip = mqtt_last_client_ip;
  e->transport = mqtt_last_client_transport;
  e->topic_count = 0;
}

void mqtt_conn_on_disconnected(const char *client_id) {
  ConnEntry *e = find_conn(client_id);
  if (!e)
    return;
  e->used = false;
  e->topic_count = 0;
  e->client_id[0] = '\0';
}

void mqtt_conn_on_subscribe(const char *client_id, const char *topic) {
  ConnEntry *e = find_conn(client_id);
  if (!e || !topic)
    return;
  for (uint8_t i = 0; i < e->topic_count; i++) {
    if (strcmp(e->topics[i], topic) == 0)
      return;
  }
  if (e->topic_count >= MQTT_CONN_MAX_TOPICS) {
    log_w("MQTT conn track: topic cap for %s", client_id);
    return;
  }
  copy_trunc(e->topics[e->topic_count], MQTT_CONN_TOPIC_LEN, topic);
  e->topic_count++;
}

void mqtt_conn_on_unsubscribe(const char *client_id, const char *topic) {
  ConnEntry *e = find_conn(client_id);
  if (!e || !topic)
    return;
  for (uint8_t i = 0; i < e->topic_count; i++) {
    if (strcmp(e->topics[i], topic) != 0)
      continue;
    for (uint8_t j = i + 1; j < e->topic_count; j++)
      memcpy(e->topics[j - 1], e->topics[j], MQTT_CONN_TOPIC_LEN);
    e->topic_count--;
    return;
  }
}

static void append_json_escaped(String &out, const char *s) {
  out += '"';
  if (s) {
    for (const char *p = s; *p; ++p) {
      char c = *p;
      if (c == '"' || c == '\\') {
        out += '\\';
        out += c;
      } else if ((uint8_t)c < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(uint8_t)c);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  out += '"';
}

void mqtt_conn_append_json(String &out) {
  out += '[';
  bool first = true;
  for (const auto &c : conns) {
    if (!c.used)
      continue;
    if (!first)
      out += ',';
    first = false;
    out += "{\"client_id\":";
    append_json_escaped(out, c.client_id);
    out += ",\"ip\":";
    append_json_escaped(out, c.ip.toString().c_str());
    out += ",\"transport\":\"";
    out += (c.transport == MqttTransport::WS) ? "WS" : "TCP";
    out += "\",\"topics\":[";
    for (uint8_t i = 0; i < c.topic_count; i++) {
      if (i)
        out += ',';
      append_json_escaped(out, c.topics[i]);
    }
    out += "]}";
  }
  out += ']';
}

#endif // MQTT_CONN_TRACK
