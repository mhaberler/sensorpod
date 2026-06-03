#pragma once

#include <ESPmDNS.h>
#include <vector>

struct DiscoveredBroker {
  String hostname;
  String ip;
  uint16_t port;
  String instance_name;
};

class MDNSClient {
public:
  void begin();
  std::vector<DiscoveredBroker> discover_mqtt_brokers();
  void print_brokers(const std::vector<DiscoveredBroker>& brokers);
  std::vector<DiscoveredBroker> get_last_brokers() const { return last_brokers; }

private:
  std::vector<DiscoveredBroker> last_brokers;
};

extern MDNSClient mdns_client;
