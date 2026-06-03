#include "mdns_client.hpp"

MDNSClient mdns_client;

void MDNSClient::begin() {
  // mDNS is initialized in wifisetup.cpp after WiFi connects
  // This is a placeholder for future initialization if needed
}

std::vector<DiscoveredBroker> MDNSClient::discover_mqtt_brokers() {
  std::vector<DiscoveredBroker> brokers;

  int n = MDNS.queryService("mqtt", "tcp");
  if (n == 0) {
    log_d("No MQTT brokers found via mDNS");
    return brokers;
  }

  for (int i = 0; i < n; i++) {
    DiscoveredBroker broker;
    broker.instance_name = MDNS.instanceName(i);
    broker.hostname = MDNS.hostname(i);
    broker.ip = MDNS.address(i).toString();
    broker.port = MDNS.port(i);
    brokers.push_back(broker);
  }

  log_i("Found %d MQTT broker(s) via mDNS", (int)brokers.size());
  for (size_t i = 0; i < brokers.size(); i++) {
    log_d("  [%d] instance=%s hostname=%s ip=%s port=%u", (int)i,
          brokers[i].instance_name.c_str(), brokers[i].hostname.c_str(),
          brokers[i].ip.c_str(), brokers[i].port);
  }

  last_brokers = brokers;
  return brokers;
}

void MDNSClient::print_brokers(const std::vector<DiscoveredBroker>& brokers) {
  if (brokers.empty()) {
    log_i("No MQTT brokers discovered");
    return;
  }

  log_i("Discovered %d MQTT broker(s):", (int)brokers.size());
  for (size_t i = 0; i < brokers.size(); i++) {
    log_i("  [%d] instance=%s hostname=%s ip=%s port=%u", (int)i,
          brokers[i].instance_name.c_str(), brokers[i].hostname.c_str(),
          brokers[i].ip.c_str(), brokers[i].port);
  }
}
