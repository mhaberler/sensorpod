#pragma once

#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <WiFi.h>

class CustomMQTTServer : public PicoMQTT::Server {
  using PicoMQTT::Server::Server;

public:
  int32_t connected, subscribed, messages;

protected:
  void on_connected(const char *client_id) override {
    connected++;
    log_w("client %s connected (clients=%d)", client_id, connected);
  }
  virtual void on_disconnected(const char *client_id) override {
    connected--;
    log_w("client %s disconnected (clients=%d)", client_id, connected);
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

extern CustomMQTTServer mqtt;
