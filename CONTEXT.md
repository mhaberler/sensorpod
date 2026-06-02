# Sensorpod Context

## Roles

**Broker Mode** (default)
- Device runs a local MQTT broker (PicoMQTT::Server)
- Always runs WiFi AP for local access
- Optionally connects as STA to a home network
- Announces itself via mDNS (`_mqtt._tcp`, `_mqtt-ws._tcp`, `_http._tcp`)
- Web UI accessible via AP
- Sensors publish to local broker with `hostName/` prefix

**Client Mode**
- Device connects to a remote MQTT broker (via PicoMQTT::Client)
- Discovers remote brokers via mDNS (`_mqtt._tcp`)
- Always runs WiFi AP for configuration access
- Requires STA connection to reach the remote broker
- Web UI accessible via AP, shows discovered brokers
- Sensors publish to remote broker with `hostName/` prefix

## Key Decisions

- **Single firmware**: Both roles in one binary; role selected at boot via Preferences, requires reboot to switch
- **AP always on**: Both modes keep AP running for configuration access (time-limited to infinity for now)
- **Shared WiFi primitives**: `start_ap()`, `start_sta()` helpers extracted for reuse
- **Topic prefixing**: All published topics prefixed with `hostName/` in both modes
- **Broker discovery**: Only in Client mode; Broker mode does not discover other brokers
- **STA optional in Broker**: AP-only operation supported if no WiFi credentials

## Web UI

Unified settings on `/` (sysinfo page) showing:
- Current role
- Role switch button (with implied reboot)
- If Client mode: discovered brokers list, which is connected, broker selection
- Reboot button
