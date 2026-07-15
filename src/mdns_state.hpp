#pragma once

#include <stdint.h>

struct MdnsAnnounce {
  const char *instance;
  const char *service;
  const char *proto;
  uint16_t port;
  const char *txt;
};

extern MdnsAnnounce mdns_services[4];
extern size_t mdns_count;
