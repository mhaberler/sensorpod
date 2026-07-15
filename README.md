# SensorPod

A PlatformIO-based firmware for ESP32 sensor devices with dual-role MQTT (Broker/Client), WiFi, and I2C sensor integration.

It is a demonstration how to use [Sensor Logger](https://www.tszheichoi.com/sensorlogger) to record sensors which are beyond the capabilities of mobiles. In this example we use a time-of-flight distance sensor. SensorPod can run as either a **local MQTT broker** (default) or as an **MQTT client** connecting to a discovered remote broker (switchable via web UI without reflash).

## Hardware example

<div style="display: flex; justify-content: center;">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/hardware.jpg" alt="Placeholder" style="max-width: 300px; max-height: 300px;">
</div>

## How SensorPod talks to Sensor Logger

SensorPod always runs a WiFi access point and can simultaneously connect to another access point as a station once credentials are provisioned. Three deployment scenarios:

1. the mobile is a personal hotspot and SensorPod connects to it
2. the mobile as well as SensorPod connect to some common WiFi access point
3. the mobile connects as WiFi client to the SensorPod WiFi access point.

### Broker Mode (Default)

SensorPod runs a local MQTT broker (PicoMQTT) which publishes sensor updates:

- MQTT-over-TCP: port 1883, no TLS
- MQTT-over-Websockets: port 8080, no TLS

The broker is reachable at:

- `{hostname}.local` (e.g., `esp32c6-5B0A24.local`, resolved via mDNS — recommended for iOS)
- `192.168.4.1` (when the mobile connects to SensorPod's own AP — recommended for Android)

The broker starts at boot and runs on both the AP and the STA interface simultaneously, so it is reachable regardless of WiFi state.

**All sensor topics are prefixed with the device hostname** (e.g., `esp32c6-5B0A24/VL53L0X`, `esp32c6-5B0A24/status`).

### Client Mode (Optional)

SensorPod can be switched to Client mode via the web UI (`/`), where it discovers and connects to a remote MQTT broker instead of running its own. The mode is runtime-switchable without reflash:

- Web UI shows role toggle: "Current Role: [Broker/Client Mode]"
- Click "Switch to Client Mode" and "Save & Restart"
- Device reboots in Client mode, discovers brokers via mDNS every 10 seconds
- Connects to first discovered broker with exponential backoff retry (1s → 2s → 4s → 8s → 16s → 60s cap)
- Falls back to rediscovery after max retries exhausted
- All sensor topics still prefixed with hostname, but published to remote broker

**Role is persistent:** switch once, role is saved to NVS and survives power cycles.

### mDNS Service Announcements (Broker Mode)

In Broker mode, SensorPod advertises itself on both interfaces via mDNS. The device hostname is derived from the MAC (e.g., `esp32c6-5B0A24`), and the following services are announced:

- **Host advertisement:** `esp32c6-5B0A24.local` (mDNS A/AAAA record)
- **MQTT service:** `_mqtt._tcp.local` on port 1883
    - Instance name: `esp32c6-5B0A24-TCP-083AF24483D8` (generated from hostname + MAC)
- **MQTT-WS service:** `_mqtt-ws._tcp.local` on port 8080 with TXT record `path=/mqtt`
    - Instance name: `esp32c6-5B0A24-WS-083AF24483D8`
- **HTTP service:** `_http._tcp.local` on port 80 (sysinfo web UI)
- **Workstation:** `_workstation._tcp.local` (generic host advertisement)

Clients that browse mDNS (iOS, Linux Avahi, Home Assistant) can discover the broker without knowing its IP. Android's mDNS resolver is unreliable — use the fixed AP IP `192.168.4.1` there.

**Client Mode:** No mDNS announcements. Instead, the device queries `_mqtt._tcp.local` to discover available brokers and displays them in the web UI dropdown. Discovered brokers show both instance name and IP.

On recording start, Sensor Logger connects to the broker (local or remote) and subscribes (typically to topic `#` — all topics). Sensor Logger can then run in the background logging arbitrary sensors including Bluetooth sensors which are not supported natively.

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

[Click this link](https://sensorlogger.app/link/config/KLUv/WCUA/UMAOZXSCQgraYNAwy3aM8KMR3UHlnKYeJpOjPLO2mZYnHUNQU6MgAwgHE6AD4AQAAEX1XUqp2Ug3xVZ07HrO75NJp0WZM8bY1ME74qhmEaOfFXssMH50+6Cr7c7JR0EfIJ59nYDav8kj8RqxKdCvk6gtpeNrsR8uTkq1JErFEliua2DtnIoOjFRGAa3JQw0vkglDDjJGmkX7ZdyEkZ614u3SJwD9IlYwHNrr8e4HA0d96r09cdMnWLR6duCIm1NZo8L6dm4S9tm5PK17HxYTAM43Aej0WzcAw0i8ajYTAc526pqTv6qiwB1d/CnacFJQimTMvXBZovVQC99h6yhm5SS1CogtOIcyNKY15q2+AAlKy0ZVyh+OuTCOUXCsv3lG1CkDrWJg0vICCCZE3aAViS3fkULBr8eUHiUifGu+IbiIzwRrm6yXAyVsGgyjVFh+KOZQB1lkgED2Vh0cN13suFCtqptPknJgy1+RUTDuuMgGMNaBjASAPmTAlsGLrC8z5iD3gZls7L8vwzBi6L2+GYpMECOiivvUKkjdfRsELg8A==) and import the config into Sensor Logger or scan this QR Code:

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

## Devices

**Flash size matters.** The full feature set — WiFi + BLE stacks, the Theengs decoder device database, MQTT broker, web UI — produces an app image of ~1.8–1.93 MB. On **4 MB flash devices** (M5Stack NanoC6, Seeed XIAO C6) that is a tight fit:

- The 4 MB envs use a single large app partition (`max_app_4MB.csv`); firmware is flashed over USB with esptool.
- Size-conscious build flags (`-Os`, `-fno-exceptions`) are already applied.

**Recommendation:** for new deployments prefer boards with **≥8 MB flash**, ideally the **ESP32-P4** family (M5Stack Tab5, Waveshare ESP32-P4-WiFi6, 16 MB) or other large-flash variants — they leave comfortable room for feature growth. Keep the 4 MB C6 boards for size-frozen installs.

## Building

### Prerequisites

This project uses [Pioarduino](https://github.com/pioarduino) - a fork of PlatformIO:

```bash
# Install
brew install pioarduino/pioarduino/pioarduino

# Clone and navigate to project
cd sensorpod
```

### Build, flash, monitor

```bash
# Build default env (m5stack-nanoc6)
pio run

# Flash and open serial monitor
pio run -e m5stack-nanoc6 -t upload -t monitor

# Build a merged single-binary image (drops into firmware/)
pio run -e m5stack-nanoc6 -t firmware
```

On the USB-Serial/JTAG boards (C6/H2/P4/C5), `Serial` is the native USB
peripheral. The firmware boots headless — it waits at most ~1.5s for a host
to attach, then proceeds regardless — so it runs fine on battery with no
cable. The monitor no longer resets the board on connect (`monitor_rts/dtr=0`),
so to catch the boot banner open the monitor within ~1.5s of resetting, or
flash and monitor in one step: `pio run -e <env> -t upload -t monitor`.

## Pre-built firmware & updates

Pre-built images can be found at https://github.com/mhaberler/sensorpod/releases

Download the `sensorpod_<env>_firmware_<latest version>.bin` merged image (bootloader + partition table + app) and flash it over USB with [ESPTool](https://jason2866.github.io/esp32tool/) (see also https://github.com/Jason2866/esp32tool).

Firmware updates work the same way: download the newer `*_firmware.bin` and reflash over USB. WiFi credentials and device settings live in NVS and survive the reflash.

## Web UI

Once SensorPod is on WiFi (either as STA or via its AP), it serves a web UI on port 80:

- `http://{hostname}.local/` (mDNS — e.g., `http://esp32c6-5B0A24.local/` — recommended for iOS/macOS/Linux)
- `http://192.168.4.1/` (when connected to SensorPod's own AP — recommended for Android)
- `http://<STA-IP>/` (look up the IP on your router or in the serial log)

The root page shows firmware identity (version, build SHA, build date), chip info, heap/PSRAM, flash, the partition table, network state, BLE statistics, and announced mDNS services. `GET /data` returns the same as JSON.

## WiFi provisioning

SensorPod does **not** take WiFi credentials at build time. Provisioning is done at runtime over the **serial** transport of the [Improv-WiFi](https://www.improv-wifi.com/) protocol. If serial provisioning doesn't complete within `TIME_TO_CONNECT` (15s on P4, 8s elsewhere), the firmware also falls back to Improv **BLE** provisioning (enabled via the `-DIMPROV_WIFI_BLE_ENABLED` flag in the `[improv]` block). Credentials are stored in NVS (`Preferences` namespace `wifi-creds`).

For SensorPod use the __Improv via Serial__ button.

Connect to your devices` port and set SSID and password for your Access Point or Mobile hotspot..

The device's own AP is named `{hostname}.local` (e.g., `esp32c6-5B0A24.local`, hostname derived from the MAC via `WiFi.getHostname()`) with PSK = the same `{hostname}.local` string. Using the `.local` mDNS name as the SSID means the SSID doubles as the device's hostname. The AP is always up regardless of whether STA credentials are present. Once provisioned, the Arduino-ESP32 driver auto-reconnects if the upstream AP later drops.

### Erasing credentials

Long-press the on-board button (≥800 ms) to wipe saved credentials and stop the STA. The AP stays up. The device does not reboot. Power-cycle confirms the wipe persisted.

Click counts (single/double/multi) are reserved for application-level button events.

## Sensors

The default `loop()` polls a VL53L0X time-of-flight distance sensor on the I2C bus and publishes to the MQTT broker (local or remote):

- `{hostname}/VL53L0X` topic: `{"distance_mm": <uint>}` at ~5 Hz
- `{hostname}/status` topic: `{"uptime": <s>, "cpu_temperature": <°C>, "rssi": <dBm>}` at 1 Hz

**Topic Prefixing:** All topics are automatically prefixed with the device's hostname for easy identification in multi-pod deployments. Example: `esp32c6-5B0A24/VL53L0X`, `esp32c6-5B0A24/status`.

If no VL53L0X is detected at boot, polling is skipped and only the `status` topic publishes.

## BLE sensor scanning

SensorPod continuously scans for BLE advertisements on a dedicated FreeRTOS task and publishes decoded sensor readings to MQTT. All BLE options are set on the web UI (`/`), stored in NVS, and — except for the scan on/off switch — applied **live, without reboot**:

- **BLE scanning** (on/off, reboot to apply): gates the whole scanner at boot.
- **Decoder** (radio button, applied immediately):
    - **Theengs decoder** (default) — the [Theengs Decoder](https://github.com/theengs/decoder) library, ~120 device models (Xiaomi, Govee, RuuviTag, SwitchBot, …)
    - **BTHomeV2 decoder** — [BTHome v2](https://bthome.io/) advertisements, optional AES-CCM decryption
    - **Custom decoder** — hand-written decoders in `src/custom_decoder.cpp`; currently Mikrotik TG-BT5-IN/-OUT tags and Qingping sensors (CGG1, CGH1, CGP1W, CGD1, CGDN1, CGPR1)
    - **Undecoded advertisements** — no decoding, every advertisement published raw
- **Retain undecoded advertisements**: when the selected decoder doesn't claim an advertisement, publish it raw instead of dropping it.
- **Deduplicate advertisements** + **max age** (default 1 s): drops repeated identical advertisements (same MAC, byte-identical radio payload) within the age window. Runs in the scan callback before queueing, so it reduces load in every mode. A changed sensor value changes the payload and always passes.

Decoded (or raw) readings are published as JSON to `{hostname}/ble/{mac}` (lowercase MAC, colons stripped), e.g.:

```text
esp32c6-5b0a24/ble/d401c3e0bd1f {"dev":"Mikrotik TG-BT5-OUT","tempc":24.69,"batt":100,"id":"d4:01:c3:e0:bd:1f","rssi":-55,...}
esp32c6-5b0a24/ble/582d3480bee8 {"dev":"CGH1","battery_percent":10,"open_dimensionless":1,"id":"58:2d:34:80:be:e8","rssi":-46,...}
```

Every publish carries `id` (MAC), `rssi`, `time`, and `name`/`txpower` when present in the advertisement.

## LED Status Feedback

The onboard LED (or RGB NeoPixel if present) provides real-time connection status:

| LED Pattern            | Color  | Meaning                                                                |
| ---------------------- | ------ | ---------------------------------------------------------------------- |
| **Solid**              | Green  | WiFi connected + MQTT broker (local or remote) is online and reachable |
| **Slow blink** (500ms) | Orange | WiFi connected, but MQTT broker is down or unreachable                 |
| **Fast blink** (200ms) | Red    | WiFi disconnected; device is in AP-only mode or STA connection lost    |
| **Off**                | —      | LED disabled or device in idle state                                   |

**Boot sequence:** After `setup()` completes, the LED flashes 5 times (100ms each) to indicate boot success.

**Improv provisioning:** When Improv WiFi provisioning is active (serial or BLE):

- Error: 3 slow blinks (2000ms each)
- Success: 3 quick blinks (100ms each)

The continuous status LED is updated every loop iteration based on WiFi and MQTT state, so the pattern reflects the current connection status in real-time.

## Configuration

### Build flags

Key options in `platformio.ini`:

```ini
-DCORE_DEBUG_LEVEL=4    # 0=none, 5=verbose (debug/default env uses 4, release uses 2)
-DMQTT_PORT=1883
-DMQTTWS_PORT=8080
```

Device hostname is auto-derived from the last 3 bytes of the MAC address (e.g., `esp32c6-5B0A24`) via `WiFi.getHostname()`.

`SGO_DEFAULT_OWNER` / `SGO_DEFAULT_REPO` / `SGO_DEFAULT_BIN` and `BUILD_SHA` / `BUILD_DATE` are auto-injected by `scripts/inject_build_info.py` from `git remote` / commit metadata.

## Project Structure

```
sensorpod/
├── src/
│   ├── main.cpp            # setup/loop, sensor polling, MQTT publish, button, mDNS discovery loop
│   ├── wifisetup.cpp       # AP + STA + mDNS announcements
│   ├── webserver.cpp       # HTTP server lifecycle, route registration
│   ├── content.cpp         # HTML/JSON sysinfo generation
│   ├── mqtt.cpp            # PicoMQTT broker (Broker mode)
│   ├── mqtt_client.cpp     # PicoMQTT client wrapper (Client mode)
│   ├── mdns_client.cpp     # mDNS discovery (async, non-blocking task)
│   ├── mqtt_device.hpp     # Abstract MQTT interface (Broker/Client polymorphism)
│   ├── BLEScanner.cpp/.h   # BLE scan task, dedup, decoder dispatch (Theengs/BTHome/custom/raw)
│   ├── BLE.cpp             # scanner glue: drain queue, publish ble/<mac>
│   ├── custom_decoder.cpp  # hand-written decoders (Mikrotik TG-BT5, Qingping)
│   ├── deviceconfig.hpp    # NVS wrapper for role, broker hostname, BLE options
│   ├── mdns_state.hpp      # mDNS service struct + externs
│   ├── led.hpp/cpp         # LED status feedback
│   ├── ota.cpp             # optional OTA web updater (off by default, gated on OTA_WEB_UPDATER)
│   ├── http_server.hpp     # shared WebServer handle + page style
│   └── credstore.hpp       # NVS wrapper for WiFi credentials
├── scripts/                # PlatformIO extra_scripts (build info, version, merged firmware)
├── platformio.ini          # Boards + envs + lib_deps
└── *.csv                   # Partition tables
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