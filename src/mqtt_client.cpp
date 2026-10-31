#include "mqtt_client.hpp"
#include <WiFi.h>

MQTTClient mqtt_client;

void MQTTClient::begin() {
  log_d("MQTTClient::begin() — Client mode (awaiting connection via discover)");
  retry_count = 0;
  retry_backoff_ms = 1000;
  pending_host = "";
  connected_since_ms = 0;

  mqtt.connected_callback = [this]() {
    if (connected_since_ms != 0)
      total_reconnects++;
    connected_since_ms = millis();
    retry_count = 0;
    retry_backoff_ms = 1000;
    log_i("MQTT connected (reconnects=%u)", total_reconnects);
    if (on_connect_hook)
      on_connect_hook();
  };
  mqtt.disconnected_callback = [this]() {
    connected_since_ms = 0;
    log_w("MQTT disconnected");
  };
}

void MQTTClient::loop() {
  // Attempt pending connect once WiFi is up
  if (!mqtt.connected() && pending_host.length() > 0 &&
      WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();
    if (now - last_retry_time > retry_backoff_ms) {
      if (retry_count < MAX_RETRIES) {
        log_i("MQTT connect attempt %u/%u → %s:%u", retry_count + 1,
              MAX_RETRIES, pending_host.c_str(), pending_port);
        mqtt.connect(pending_host.c_str(), pending_port);
        retry_count++;
        last_retry_time = now;
        retry_backoff_ms = (retry_backoff_ms < MAX_BACKOFF_MS)
                               ? retry_backoff_ms * 2
                               : MAX_BACKOFF_MS;
      } else if (retry_count >= MAX_RETRIES) {
        log_w("MQTT max retries exhausted, awaiting rediscovery");
      }
    }
  }

  mqtt.loop();

  if (sweep_start_ms && millis() - sweep_start_ms >= SWEEP_MS)
    sweep_finish();
}

void MQTTClient::sweep_retained(const char *filter,
                                std::function<bool(const char *)> keep) {
  if (sweep_start_ms)
    sweep_finish();
  sweep_filter = filter;
  sweep_keep = keep;
  sweep_topics.clear();
  sweep_start_ms = millis();
  if (!sweep_start_ms)
    sweep_start_ms = 1;
  // Retained messages arrive right after SUBACK; live publishes (ours
  // included) may arrive too and are kept by keep() anyway.
  mqtt.subscribe(sweep_filter, [this](char *topic, char *payload) {
    if (payload[0] && sweep_topics.size() < 256) {
      for (auto &t : sweep_topics)
        if (t == topic)
          return;
      sweep_topics.push_back(topic);
    }
  });
}

void MQTTClient::sweep_finish() {
  mqtt.unsubscribe(sweep_filter);
  unsigned cleared = 0;
  for (auto &t : sweep_topics)
    if (!sweep_keep(t.c_str())) {
      publish(t.c_str(), "", true);
      cleared++;
    }
  log_i("MQTT sweep %s: %u retained, %u cleared", sweep_filter.c_str(),
        (unsigned)sweep_topics.size(), cleared);
  sweep_topics.clear();
  sweep_start_ms = 0;
}

bool MQTTClient::connected() { return mqtt.connected(); }

void MQTTClient::publish(const char *topic, const char *payload, bool retain) {
  if (mqtt.connected()) {
    mqtt.publish(topic, payload, 0, retain);
    messages_sent++;
    log_d("MQTTClient::publish(%s)", topic);
  } else {
    messages_failed++;
    log_w("MQTTClient::publish(%s) — broker disconnected, will retry", topic);
  }
}

void MQTTClient::connect(const char *host, uint16_t port) {
  pending_host = host;
  pending_port = port;
  retry_count = 0;
  retry_backoff_ms = 1000;
  last_retry_time = 0; // trigger immediately on next loop
  log_d("MQTTClient::connect() — pending %s:%u (will attempt when WiFi ready)",
        host, port);
}
