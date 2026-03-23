#include <ArduinoJson.h>
#include <PicoMQTT.h>
#include <PicoWebsocket.h>
#include <WiFi.h>
#include <WiFiMulti.h>

WiFiServer tcp_server(MQTT_PORT);
WiFiServer websocket_underlying_server(MQTTWS_PORT);
PicoWebsocket::Server<::WiFiServer>
    websocket_server(websocket_underlying_server);

extern JsonDocument wifiCredentials;
extern WiFiMulti wifiMulti;
void startWiFiScan();
void saveWifiCredentials();

class CustomMQTTServer : public PicoMQTT::Server {
  using PicoMQTT::Server::Server;

public:
  int32_t connected, subscribed, messages;

protected:
  void on_connected(const char *client_id) override {
    log_w("client %s connected", client_id);
    connected++;
  }
  virtual void on_disconnected(const char *client_id) override {
    log_w("client %s disconnected", client_id);
    connected--;
  }
  virtual void on_subscribe(const char *client_id, const char *topic) override {
    log_w("client %s subscribed %s", client_id, topic);
    subscribed++;
  }
  virtual void on_unsubscribe(const char *client_id,
                              const char *topic) override {
    log_w("client %s unsubscribed %s", client_id, topic);
    subscribed--;
  }
  virtual void on_message(const char *topic,
                          PicoMQTT::IncomingPacket &packet) override {
    log_w("message topic=%s", topic);
    PicoMQTT::Server::Server::on_message(topic, packet);
    messages++;
  }
};

CustomMQTTServer mqtt(tcp_server, websocket_server);
