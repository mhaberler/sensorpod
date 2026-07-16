#pragma once

#include "mqtt_device.hpp"
#include <PicoMQTT.h>

class MQTTClient : public MQTTDevice {
public:
  void begin() override;
  void loop() override;
  bool connected() override;
  void publish(const char *topic, const char *payload) override;

  void connect(const char *host, uint16_t port = 1883);
  String client_id;

  const String &broker_host() const { return pending_host; }
  uint16_t broker_port() const { return pending_port; }

  unsigned long connected_since_ms = 0;
  unsigned int total_reconnects = 0;
  uint32_t messages_sent = 0;
  uint32_t messages_failed = 0;

  unsigned int get_retry_count() const { return retry_count; }

  bool needs_rediscovery() {
    return !mqtt.connected() && retry_count >= MAX_RETRIES &&
           pending_host.length() > 0;
  }
  bool has_pending() const { return pending_host.length() > 0; }
  void clear_broker() {
    pending_host = "";
    retry_count = 0;
    retry_backoff_ms = 1000;
  }

private:
  PicoMQTT::Client mqtt;
  String pending_host;
  uint16_t pending_port = 1883;
  unsigned long last_retry_time = 0;
  unsigned int retry_count = 0;
  unsigned int retry_backoff_ms = 1000;
  static const unsigned int MAX_RETRIES = 5;
  static const unsigned int MAX_BACKOFF_MS = 60000;
};

extern MQTTClient mqtt_client;
