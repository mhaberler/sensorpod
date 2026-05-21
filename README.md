# SensorPod

A PlatformIO-based firmware for ESP32 sensor devices with WiFi, MQTT, and I2C sensor integration.

It is a demonstration how to use [Sensor Logger](https://www.tszheichoi.com/sensorlogger) to record sensors which are beyond the capabilities of mobiles. In this example we use a time-of-flight distance sensor.

## Hardware example

<div style="display: flex; justify-content: center;">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/hardware.jpg" alt="Placeholder" style="max-width: 300px; max-height: 300px;">
</div>

## How SensorPod talks to Sensor Logger

SensorPod always runs a WiFi access point and can simultaneously connect to another access point as a station once credentials are provisioned. Three deployment scenarios:

1. the mobile is a personal hotspot and SensorPod connects to it
2. the mobile as well as SensorPod connect to some common WiFi access point
3. the mobile connects as WiFi client to the SensorPod WiFi access point.

SensorPod runs an MQTT broker which publishes sensor updates:

- MQTT-over-TCP: port 1883, no TLS
- MQTT-over-Websockets: port 8883, no TLS

The broker is reachable at:

- `sensorpod.local` (resolved via mDNS when SensorPod is a WiFi client — recommended for iOS)
- `192.168.4.1` (when the mobile connects to SensorPod's own AP — recommended for Android)

The broker starts at boot and runs on both the AP and the STA interface simultaneously, so it is reachable regardless of WiFi state.

### mDNS service announcements

SensorPod advertises itself on both interfaces via mDNS as `<HOSTNAME>.local` (default `sensorpod.local`). The following services are announced:

- `_mqtt._tcp` on port 1883 — instance name `sensorpod-TCP-<MAC>`
- `_mqtt-ws._tcp` on port 8883 with TXT record `path=/mqtt` — instance name `sensorpod-WS-<MAC>`
- `_workstation._tcp` (generic host advertisement)

Clients that browse mDNS (iOS, Linux Avahi, Home Assistant) can therefore discover the broker without knowing its IP. Android's mDNS resolver is unreliable — use the fixed AP IP `192.168.4.1` there.

On recording start, Sensor Logger connects to this broker and subscribes (typically to topic `#` — all topics). Sensor Logger can then run in the background logging arbitrary sensors including Bluetooth sensors which are not supported natively.

## How to configure Sensor Logger manually

here are screenshots from the Settings -> Data Streaming page:

<div align="center">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/broker-ios.jpg" width="30%"> &emsp; &emsp;&emsp; &emsp;&emsp; &emsp;
	<img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/broker-android.jpg" width="30%">
</div>

If SensorPod and SensorLogger already are WiFi-connected, try the 'Test Push' button — it should show a success: "Message sent"

## Configure Sensor Logger via link or QR Code

This is easier than typing in manually and reflects the above configurations

### iOS

[Click this link](sensorlogger://config/KLUv/WCUA/UMAOZXSCQgraYNAwy3aM8KMR3UHlnKYeJpOjPLO2mZYnHUNQU6MgAwgHE6AD4AQAAEX1XUqp2Ug3xVZ07HrO75NJp0WZM8bY1ME74qhmEaOfFXssMH50+6Cr7c7JR0EfIJ59nYDav8kj8RqxKdCvk6gtpeNrsR8uTkq1JErFEliua2DtnIoOjFRGAa3JQw0vkglDDjJGmkX7ZdyEkZ614u3SJwD9IlYwHNrr8e4HA0d96r09cdMnWLR6duCIm1NZo8L6dm4S9tm5PK17HxYTAM43Aej0WzcAw0i8ajYTAc526pqTv6qiwB1d/CnacFJQimTMvXBZovVQC99h6yhm5SS1CogtOIcyNKY15q2+AAlKy0ZVyh+OuTCOUXCsv3lG1CkDrWJg0vICCCZE3aAViS3fkULBr8eUHiUifGu+IbiIzwRrm6yXAyVsGgyjVFh+KOZQB1lkgED2Vh0cN13suFCtqptPknJgy1+RUTDuuMgGMNaBjASAPmTAlsGLrC8z5iD3gZls7L8vwzBi6L2+GYpMECOiivvUKkjdfRsELg8A==) and import the config into Sensor Logger or scan this QR Code:

<div style="display: flex; justify-content: center;">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/qrcode-ios.jpg" alt="Placeholder" style="max-width: 300px; max-height: 300px;">
</div>

### Android

[Click this link](https://sensorlogger.app/link/config/KLUv/WBZBEUQAKaeXCUgregDjDuTsdme5pqesGWQPANRkfpbRQUwMru3iVusiACEAMQBTQBTAFcAkY/7mrIYO+fWaP5NaCMsXOeibBvrmX8zCMm2SfeF7gxzI1sYt3tT5PfqeCHbxnEcgcU82zpUCCgxCy8at/f34lZIVkC5r8/Q7HE9KwKLvYxn49dbuWRLfcU0HMdxQI8H42E4DB6GwONpNCAoYWfYv5Vtk2mm+J1sfKt6ren9VwhmK9k2C8u98rR4f+2wr5bFt26C82jvpZHfWOnkrtjFAZSDhwjV8CBNCpRERZIAaYomDwgTBYmFEpEsl04yskcw9Go+WPggsn4pAai9vHZ+yTxbViJUaii3MbLrrSqGULqH8Tv8bDngcHwzli0fBsG/xOX8bFUwMZuEMItYli0XwxdU95qrfXXZkmNC6ZeomtBIwRL3eq8sZkuv4oPYpb5NyShFhO1msmXBk51JqvhiVMfw73oLC55Atnzbop57Z0uJRFHRZAk6IBCmqMpuB+MI1LWHGpxVQ/TAwc4ZFKedXYIoeFFwmiAB74WBTZEdHOx1Wr8rvQE4hDeSK5sMF2sVSqni+2wi66ADuiNAvSUSwENZWPRynZflggTtVNp8ExPG2jzFBMI6K+CgBjQOYKgBc03JbBhG4TkfsQe8DEsnZXniMwYqi9PhmKTBGjporz1CpMHreFghcHg=) and import the config into Sensor Logger or scan this QR Code:

<div style="display: flex; justify-content: center;">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/qrcode-android.png" alt="Placeholder" style="max-width: 300px; max-height: 300px;">
</div>

## Supported Boards

The active build envs cover a fleet of M5Stack boards plus a few generic devkits — see `[env:*]` sections in `platformio.ini` for the full list. The default env is `m5stack-nanoc6`. Frequently used:

- **[M5Stack NanoC6](https://docs.m5stack.com/en/core/nanoc6)** (default)
- **[M5Stack Tab5-P4](https://docs.m5stack.com/en/core/Tab5)**
- **[M5Stack CoreS3](https://docs.m5stack.com/en/core/cores3)**
- **[M5Stack AtomS3](https://docs.m5stack.com/en/core/atoms3)**
- **[M5Stack Stamp S3](https://docs.m5stack.com/en/core/stamp_s3)**
- **[Seeed XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)**
- **[ESP32-C5 DevKit](https://www.waveshare.com/esp32-c5-wifi6-kit-n16r4.htm)**
- **[ESP32-P4-WiFi6](https://www.waveshare.com/esp32-p4-wifi6.htm)**

## Building

### Prerequisites

```bash
# Install PlatformIO
pip install platformio

# Clone and navigate to project
cd sensorpod
```

### Build, flash, monitor

```bash
# Build default env (m5stack-nanoc6)
pio run

# Build a specific env
pio run -e esp32-c5-devkitc1-n16r8

# Flash and open serial monitor
pio run -e m5stack-nanoc6 -t upload -t monitor

# Build a merged single-binary image (drops into firmware/)
pio run -e m5stack-nanoc6 -t firmware
```

## WiFi provisioning

SensorPod does **not** take WiFi credentials at build time. Provisioning is done at runtime over the **serial** transport of the [Improv-WiFi](https://www.improv-wifi.com/) protocol (BLE transport is not enabled in this firmware). Credentials are stored in NVS (`Preferences` namespace `wifi-creds`).

See the [pioarduino-ghota README](https://github.com/mhaberler/pioarduino-ghota/blob/master/README.md) for the end-to-end provisioning walkthrough. For SensorPod use the **serial** Improv client, e.g. [Improv Web — Serial](https://www.improv-wifi.com/serial/), instead of the BLE client linked there.

The device's own AP is named `ESP32-<MAC>` with PSK = the hostname (`sensorpod` unless overridden by `-DHOSTNAME=…` at build time). The AP is always up regardless of whether STA credentials are present. Once provisioned, the Arduino-ESP32 driver auto-reconnects if the upstream AP later drops.

### Erasing credentials

Long-press the on-board button (≥800 ms) to wipe saved credentials and stop the STA. The AP stays up. The device does not reboot. Power-cycle confirms the wipe persisted.

Click counts (single/double/multi) are reserved for application-level button events.

## Sensors

The default `loop()` polls a VL53L0X time-of-flight distance sensor on the I2C bus and publishes:

- `VL53L0X` topic: `{"distance_mm": <uint>}` at ~5 Hz
- `status` topic: `{"uptime": <s>, "cpu_temperature": <°C>, "rssi": <dBm>}` at 1 Hz

If no VL53L0X is detected at boot, polling is skipped and only the `status` topic publishes.

## Configuration

### Build flags

Key options in `platformio.ini`:

```ini
-DCORE_DEBUG_LEVEL=5    # 0=none, 5=verbose (release env uses 1)
-DMQTT_PORT=1883
-DMQTTWS_PORT=8883
-DHOSTNAME=\"sensorpod\"
```

`SGO_DEFAULT_OWNER` / `SGO_DEFAULT_REPO` / `SGO_DEFAULT_BIN` and `BUILD_SHA` / `BUILD_DATE` are auto-injected by `scripts/inject_build_info.py` from `git remote` / commit metadata.

## Project structure

```
sensorpod/
├── src/
│   ├── main.cpp        # setup/loop, sensor polling, MQTT publish, button
│   ├── wifisetup.cpp   # AP + STA + mDNS, Improv-WiFi handoff
│   ├── mqtt.cpp        # PicoMQTT broker (TCP + WebSocket)
│   └── credstore.hpp   # NVS wrapper for WiFi credentials
├── scripts/            # PlatformIO extra_scripts (build info, version, OTA)
├── platformio.ini      # Boards + envs + lib_deps
└── *.csv               # Partition tables
```

## Dependencies

Resolved automatically via `lib_deps` in `platformio.ini`:

- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)** — JSON encoding for MQTT payloads
- **[PicoMQTT](https://github.com/mlesniew/PicoMQTT)** + **[PicoWebsocket](https://github.com/mlesniew/PicoWebsocket)** — embedded broker + WS transport
- **[Improv-WiFi-Library](https://github.com/mhaberler/Improv-WiFi-Library)** — serial provisioning protocol
- **[OneButton](https://github.com/mathertel/OneButton)** — debounced button + long-press detection
- **[Adafruit_VL53L0X](https://github.com/adafruit/Adafruit_VL53L0X)** — time-of-flight distance sensor driver

## License

MIT