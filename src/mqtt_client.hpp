#pragma once

#include "mqtt_device.hpp"
#include <PicoMQTT.h>
#include <functional>
#include <vector>

class MQTTClient : public MQTTDevice {
public:
  void begin() override;
  void loop() override;
  bool connected() override;
  void publish(const char *topic, const char *payload,
               bool retain = false) override;

  // Called after every (re)connect to the remote broker.
  std::function<void()> on_connect_hook;

  // Clears stale retained messages: subscribes to filter for
  // SWEEP_MS, collects topics holding a retained (non-empty) payload, then
  // publishes an empty retained payload for each topic keep() rejects.
  void sweep_retained(const char *filter,
                      std::function<bool(const char *topic)> keep);

  void connect(const char *host, uint16_t port = 1883);
  String client_id;

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

  static const unsigned long SWEEP_MS = 2000;
  String sweep_filter;
  std::function<bool(const char *)> sweep_keep;
  std::vector<String> sweep_topics;
  unsigned long sweep_start_ms = 0;
  void sweep_finish();
};

extern MQTTClient mqtt_client;
