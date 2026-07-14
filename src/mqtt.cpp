#include "mqtt.hpp"

// Remote address of the most recently accepted broker connection (v4 or v6).
// Valid inside on_connected(): PicoMQTT's loop() calls on_connected()
// immediately after the accept that produced the client, so no other accept
// can intervene.
IPAddress mqtt_last_client_ip;

// WiFiServer::accept() is non-virtual, but PicoMQTT/PicoWebsocket reach it
// statically through templates instantiated on the declared type - so this
// shadow is called as long as the objects below are declared as
// TrackingWiFiServer (and the PicoWebsocket::Server template parameter is
// TrackingWiFiServer, not ::WiFiServer).
class TrackingWiFiServer : public WiFiServer {
public:
  using WiFiServer::WiFiServer;
  NetworkClient accept() {
    NetworkClient c = WiFiServer::accept();
    if (c)
      mqtt_last_client_ip = c.remoteIP();
    return c;
  }
};

static TrackingWiFiServer tcp_server(MQTT_PORT);
static TrackingWiFiServer websocket_underlying_server(MQTTWS_PORT);
PicoWebsocket::Server<TrackingWiFiServer>
    websocket_server(websocket_underlying_server);

CustomMQTTServer mqtt_broker(tcp_server, websocket_server);
MQTTDevice *mqtt_device = nullptr;
