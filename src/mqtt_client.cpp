#include "mqtt_client.hpp"

MQTTClient mqtt_client;

void MQTTClient::begin() {
  log_d("MQTTClient::begin() — Client mode (awaiting connection via discover)");
  retry_count = 0;
  retry_backoff_ms = 1000;
}

void MQTTClient::loop() {
  if (mqtt.connected()) {
    mqtt.loop();
    if (retry_count > 0) {
      log_i("MQTT reconnected (after %u retries)", retry_count);
      retry_count = 0;
      retry_backoff_ms = 1000;
    }
  } else {
    mqtt.loop();  // Still call loop even when disconnected (handles pending operations)

    // Retry with exponential backoff
    unsigned long now = millis();
    if (now - last_retry_time > retry_backoff_ms && retry_count < MAX_RETRIES) {
      log_w("MQTT retry %u/%u (backoff: %ums)", retry_count + 1, MAX_RETRIES, retry_backoff_ms);
      // mqtt.connect() will be called by main loop's rediscovery logic
      retry_count++;
      last_retry_time = now;

      // Exponential backoff: 1s → 2s → 4s → 8s → 16s → cap at 60s
      retry_backoff_ms = (retry_backoff_ms < MAX_BACKOFF_MS) ? (retry_backoff_ms * 2) : MAX_BACKOFF_MS;
    }
  }
}

bool MQTTClient::connected() {
  return mqtt.connected();
}

void MQTTClient::publish(const char* topic, const char* payload) {
  if (mqtt.connected()) {
    mqtt.publish(topic, payload);
    log_d("MQTTClient::publish(%s)", topic);
  } else {
    log_w("MQTTClient::publish(%s) — broker disconnected, will retry", topic);
  }
}

void MQTTClient::connect(const char* host, uint16_t port) {
  log_i("MQTTClient connecting to %s:%u", host, port);
  mqtt.connect(host, port);
  last_retry_time = millis();
  retry_count = 0;  // Reset retry count on new connection attempt
}
