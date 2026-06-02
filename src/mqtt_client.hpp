#pragma once

#include "mqtt_device.hpp"
#include <String.h>

class MQTTClient : public MQTTDevice {
public:
  void begin() override;
  void loop() override;
  bool connected() override;
  void publish(const char* topic, const char* payload) override;

private:
  bool _connected = false;
};

extern MQTTClient mqtt_client;
