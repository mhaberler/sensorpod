#pragma once

#include "mqtt_device.hpp"
#include <PicoMQTT.h>

class MQTTClient : public MQTTDevice {
public:
  void begin() override;
  void loop() override;
  bool connected() override;
  void publish(const char* topic, const char* payload) override;

  void connect(const char* host, uint16_t port = 1883);
  String client_id;

private:
  PicoMQTT::Client mqtt;
  String pending_host;
  uint16_t pending_port = 1883;
  unsigned long last_retry_time = 0;
  unsigned int retry_count = 0;
  unsigned int retry_backoff_ms = 1000;  // Start at 1 second
  static const unsigned int MAX_RETRIES = 5;
  static const unsigned int MAX_BACKOFF_MS = 60000;  // Cap at 60 seconds

public:
  bool needs_rediscovery() { return !mqtt.connected() && retry_count >= MAX_RETRIES && pending_host.length() > 0; }
  bool has_pending() const { return pending_host.length() > 0; }
  void clear_broker() { pending_host = ""; retry_count = 0; retry_backoff_ms = 1000; }
};

extern MQTTClient mqtt_client;
