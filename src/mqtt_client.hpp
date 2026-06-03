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
};

extern MQTTClient mqtt_client;
