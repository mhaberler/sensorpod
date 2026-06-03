#include "mqtt_client.hpp"

MQTTClient mqtt_client;

void MQTTClient::begin() {
  log_d("MQTTClient::begin() — Client mode stub (awaiting connection via discover)");
}

void MQTTClient::loop() {
  if (mqtt.connected()) {
    mqtt.loop();
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
    log_w("MQTTClient::publish(%s) — broker disconnected", topic);
  }
}

void MQTTClient::connect(const char* host, uint16_t port) {
  log_i("MQTTClient connecting to %s:%u", host, port);
  mqtt.connect(host, port);
}
