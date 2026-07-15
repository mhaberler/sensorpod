#pragma once

#include "logging.hpp"
#include "mqtt_device.hpp"
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <WiFi.h>

class CustomMQTTServer : public PicoMQTT::Server, public MQTTDevice {
  using PicoMQTT::Server::Server;

public:
  int32_t client_count = 0, subscribed = 0, messages = 0;

  void begin() override { PicoMQTT::Server::begin(); }

  void loop() override { PicoMQTT::Server::loop(); }

  bool connected() override { return client_count > 0; }

  void publish(const char *topic, const char *payload) override {
    PicoMQTT::Server::publish(topic, payload);
  }

protected:
  void on_connected(const char *client_id) override {
    extern IPAddress mqtt_last_client_ip; // set in mqtt.cpp at accept time
    client_count++;
    log_w("client %s connected from %s (clients=%d)", client_id,
          mqtt_last_client_ip.toString().c_str(), client_count);
  }
  virtual void on_disconnected(const char *client_id) override {
    client_count--;
    log_w("client %s disconnected (clients=%d)", client_id, client_count);
  }
  virtual void on_subscribe(const char *client_id, const char *topic) override {
    subscribed++;
    log_w("client %s subscribed %s (subs=%d)", client_id, topic, subscribed);
  }
  virtual void on_unsubscribe(const char *client_id,
                              const char *topic) override {
    subscribed--;
    log_w("client %s unsubscribed %s (subs=%d)", client_id, topic, subscribed);
  }
  virtual void on_message(const char *topic,
                          PicoMQTT::IncomingPacket &packet) override {
    log_d("message topic=%s", topic);
    PicoMQTT::Server::Server::on_message(topic, packet);
    messages++;
  }
};

extern CustomMQTTServer mqtt_broker;
extern MQTTDevice *mqtt_device;

inline void mqtt_publish(const char *topic, const char *payload) {
  extern String hostName;
  String prefixed = String(hostName) + "/" + topic;
  if (mqtt_device) {
    mqtt_device->publish(prefixed.c_str(), payload);
  }
}
