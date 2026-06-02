#pragma once

class MQTTDevice {
public:
  virtual ~MQTTDevice() = default;

  virtual void begin() = 0;
  virtual void loop() = 0;
  virtual bool connected() = 0;
  virtual void publish(const char* topic, const char* payload) = 0;
};

extern MQTTDevice* mqtt_device;
