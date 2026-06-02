#include "mqtt.hpp"

WiFiServer tcp_server(MQTT_PORT);
WiFiServer websocket_underlying_server(MQTTWS_PORT);
PicoWebsocket::Server<::WiFiServer>
    websocket_server(websocket_underlying_server);

CustomMQTTServer mqtt_broker(tcp_server, websocket_server);
MQTTDevice* mqtt_device = nullptr;
