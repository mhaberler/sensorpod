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

  void start_async_discovery();
  bool is_discovering() const { return discovery_running; }

private:
  std::vector<DiscoveredBroker> last_brokers;
  volatile bool discovery_running = false;
  static void discovery_task(void* arg);
};

extern MDNSClient mdns_client;
