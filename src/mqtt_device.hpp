#pragma once

class MQTTDevice {
public:
  virtual ~MQTTDevice() = default;

  virtual void begin() = 0;
  virtual void loop() = 0;
  virtual bool connected() = 0;
  // retain: honored by MQTTClient (remote broker); PicoMQTT's own broker
  // ignores retained messages.
  virtual void publish(const char *topic, const char *payload,
                       bool retain = false) = 0;
};

extern MQTTDevice *mqtt_device;
