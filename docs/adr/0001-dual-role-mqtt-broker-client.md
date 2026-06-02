# ADR 0001: Dual-Role MQTT Broker/Client Architecture

**Date**: 2026-06-02  
**Status**: Proposed  
**Deciders**: Michael Haberler, Claude Haiku 4.5

## Context

The sensorpod firmware currently runs as a local MQTT broker, suitable for a single device acting as a hub. There is a parallel mqtt-client branch that implements a client-mode where the device connects to a remote broker. Merging these branches requires deciding how to support both modes in a single firmware image.

### Problem

- Two separate codebases (master and mqtt-client) with overlapping functionality
- No mechanism to switch roles without rebuilding firmware
- Unclear how to handle WiFi setup, MQTT initialization, and web UI across both modes

### Why This Matters

This decision is hard to reverse (significant refactoring impact), surprising without context (why not just pick one mode?), and the result of a real trade-off (single binary vs. smaller binaries, runtime flexibility vs. compile-time safety).

## Decision

We will implement a **single-firmware, dual-role architecture** where the device boots into one of two modes (Broker or Client) determined at runtime via NVS Preferences, with reboot required to switch.

### Key Design Points

**1. Role Selection & Storage**
- Create `DeviceConfig` class (mirroring `CredStore` pattern) in `src/deviceconfig.hpp`
- Store role as boolean in NVS namespace `"device-config"`
- Default role: Broker mode (`is_broker_mode = true`)
- Roles are:
  - **Broker Mode**: Local MQTT broker, AP always on, STA optional
  - **Client Mode**: Connects to remote MQTT broker, AP always on, STA required

**2. MQTT Abstraction**
- Create abstract base class `MQTTDevice` with virtual methods: `begin()`, `loop()`, `publish(topic, payload)`
- Implement two concrete subclasses:
  - `CustomMQTTServer extends MQTTDevice` (existing Broker code)
  - `MQTTClient extends MQTTDevice` (new Client code)
- Global `extern MQTTDevice* mqtt_device;` pointer initialized in `setup()` based on role
- All MQTT calls use `mqtt_device->` interface (type-safe polymorphism)

**3. WiFi Initialization Refactor**
- Extract shared WiFi primitives into helper functions:
  - `void start_ap()` — bring up AP, enable DHCP captive portal
  - `void start_sta(const String& ssid, const String& pass)` — connect as station
- `wifi_setup()` branches on `is_broker_mode`:
  - **Broker**: `start_ap()`, optionally `start_sta()` if creds exist
  - **Client**: `start_ap()`, then `start_sta()` with stored WiFi creds (or trigger Improv if missing)
- Both modes run AP always (time-limited to `UINT32_MAX` for now, set in main.cpp)

**4. Topic Prefixing**
- Create `mqtt_publish(const char* topic, const char* payload)` wrapper in `mqtt.hpp`
- Prepends `hostName/` to all topics: `hostName/VL53L0X`, `hostName/status`
- All sensor publishing goes through this wrapper (single point of control)

**5. mDNS Behavior**
- **Broker Mode**: Announce via mDNS only (`_mqtt._tcp`, etc.), no discovery
- **Client Mode**: Discover remote brokers via mDNS, no announcements
- Background discovery thread runs periodically in Client mode (every 10s), results cached for web UI

**6. Web UI Integration**
- Extend `sysinfo_html()` signature: `void sysinfo_html(String &out, bool is_broker_mode, const std::vector<DiscoveredBroker>* brokers = nullptr)`
- Unified `/` page shows:
  - **Both modes**: Current role, role-switch checkbox, save & restart button, reboot button
  - **Client mode only**: List of discovered brokers, which is connected, broker selection dropdown
- Broker selection stored in `DeviceConfig` (persists across reboots)

**7. Role Switching Flow**
- User clicks "Switch to Client" checkbox and "Save & Restart"
- Web handler calls `DeviceConfig::setBrokerMode(false)`, triggers `ESP.restart()`
- Device reboots, `setup()` reads new role from Preferences
- `setup()` calls `wifi_setup()` (Client-specific initialization)
- If Client mode lacks WiFi creds, Improv provisioning triggers before MQTT startup

**8. Error Handling**
- **Client mode without WiFi creds**: Start Improv-BLE/Serial, block MQTT until connected
- **Broker goes down (Client mode)**: `mqtt_client.connected()` returns false, sensor publishes fail silently (acceptable for telemetry)
- **Broker mode, AP-only**: Works fine, sensors publish to local broker via AP clients

## Consequences

### Positive
- Single firmware supports both roles — no separate binaries
- Role switchable via web UI without reflashing
- Clear abstraction (MQTTDevice interface) makes both modes maintainable
- Reuses WiFi setup code via extracted helpers
- Backward compatible: defaults to Broker mode for existing users

### Negative
- Both broker and client code linked into binary (slightly larger firmware)
- More complex setup() logic due to role branching
- Web UI must handle both modes (slightly more HTML)

### Risks & Mitigations
- **Risk**: User switches to Client mode, forgets to provision WiFi → blocked state
  - **Mitigation**: Improv provisioning triggered automatically if creds missing
- **Risk**: Broker discovery is async, web UI shows stale results
  - **Mitigation**: Background discovery with timestamp shown to user; user can trigger refresh
- **Risk**: Topic prefixing applied inconsistently across code
  - **Mitigation**: Single `mqtt_publish()` wrapper ensures consistency

## Implementation Sequence

1. Create `src/deviceconfig.hpp` — role storage and retrieval
2. Create `src/mqtt_device.hpp` — abstract MQTTDevice base class
3. Refactor `src/mqtt.cpp` — CustomMQTTServer now extends MQTTDevice
4. Create `src/mdns_client.cpp` (from mqtt-client branch) — mDNS discovery for Client mode
5. Refactor `src/wifisetup.cpp` — extract start_ap(), start_sta(), add role branching
6. Update `src/main.cpp` — read role at boot, branch MQTT initialization
7. Update `src/content.cpp` — extend sysinfo_html() signature for role/broker display
8. Create `mqtt_publish()` wrapper in `src/mqtt.hpp` — centralize topic prefixing
9. Update `src/webserver.cpp` — add broker selection POST handler, reboot endpoint
10. Merge mqtt-client branch (cherry-pick commits for Client-mode logic)
11. Test Scenarios A, B, C (fresh boot, role switch, broker down, etc.)

## References

- CONTEXT.md — Domain model and role definitions
- mqtt-client branch — Client mode implementation reference
