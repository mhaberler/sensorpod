# SensorPod

A PlatformIO-based firmware for ESP32 sensor devices with WiFi, MQTT, and I2C sensor integration.

It is a demonstration how to use [Sensor Logger](https://www.tszheichoi.com/sensorlogger) to record sensors which are beyond the capabilities of mobiles. In this example we use a time-of-flight distance sensor.

## Hardware example

<div style="display: flex; justify-content: center;">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/hardware.jpg" alt="Placeholder" style="max-width: 300px; max-height: 300px;">
</div>

## How SensorPod talks to Sensor Logger

SensorPod can connect to a WiFi access point. It  simultaneously can act as an access point itself, giving three possible scenarios:

1. the mobile is a personal hotspot and SensorPod connects to it
2. the mobile as well as SensorPod connect to some common WiFi acces point
3. the mobile connects as WiFi client to the SensorPod WiFi access point.

SensorPod runs an MQTT broker which publishes sensor updates:

- MQTT-over-TCP: port 1883, no TLS
- MQTT-over-Websockets: port 8883, no TLS

The broker's IP address is:

- sensorpod.local (resolved via mDNS if SensorPod is a WiFi client - recommended for iOS)
- 192.168.4.1 (if SensorPod used as WiFi access point - recommended for Android)

On recording start, Sensor Logger connects to this broker and subscribes (typically to topic '#' - all topics) - now Sensor Logger can run in the background logging arbitrary sensors including Bluetooth sensors which are not supported natively.

## How to configure Sensor Logger manually

here are screenshots from the Settings -> Data Streaming page:

<div align="center">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/broker-ios.jpg" width="30%"> &emsp; &emsp;&emsp; &emsp;&emsp; &emsp;
	<img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/broker-android.jpg" width="30%">
</div>

If SensorPod and SensorLogger already are WiFi-connected, try the 'Test Push' button - it should show a success: "Message sent"

## Configure Sensor Logger via  link or QR Code

This is easier than typing in manually and reflects the above configurations

### iOS

[Click this link](https://sensorlogger.app/link/config/KLUv/WDgA90OAAbcVSUgrbYBLLDDTA4lVcT6E9wSRNwFapzXb2gzVH5dt5sqOzIAMIBxRQBMAEcAzr3ZvJPQPti3rUGSXdUz/2peJNmV7vvc+eVCriJ3cxJk99pYIdn1PI/ARk+2/hQCyss+SuH2/lrcGskKKPf1GZodrmdFz+OAHg+n4TgKGo7Ao8FgQFC+1sT/KtmVaZ74pSz8CuN60/uvkcsuJLsWVXtlafHu2mFfLYtvXYSnob2WSn4hRiV3jRZLHq7rykZuAR8lwLSU6+aUxpPlb6axU+uQm6kthNx6MRh+UDYX+f19shxwOBwJ5TfHZPm08XO4ufgfYTk/VwsXuNFlPBm73gUqWerrhfEEV8JKQrkZnYDE3UyyJGhkayYY6CDUxkA5yyksmPK48u1qcu6dLCfRNEEkS8XgQTI1PERSwhRNkElAJEEoDweUhImlIoksllIyOgpHrWaDfQ5C0wEyIBCGLErbpGR2DSI2Y7FpgwTkM4xjNO7ghK9j+67whqQifJJs3GQYGatcWJXgZiNVo3SQay2RDA+lYdHDdV6WCwna6WnzJCbMavMUEwjrnICjGtAwgFED5puS2TAMhec+Ygx4HZZOyvLMZwxQFtfhGKTBCjqoXntDpJHXgbBC4PA=) and import the config into Sensor Logger or scan this QR Code:

<div style="display: flex; justify-content: center;">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/qrcode-ios.jpg" alt="Placeholder" style="max-width: 300px; max-height: 300px;">
</div>

### Android

[Click this link](https://sensorlogger.app/link/config/KLUv/WBZBEUQAKaeXCUgregDjDuTsdme5pqesGWQPANRkfpbRQUwMru3iVusiACEAMQBTQBTAFcAkY/7mrIYO+fWaP5NaCMsXOeibBvrmX8zCMm2SfeF7gxzI1sYt3tT5PfqeCHbxnEcgcU82zpUCCgxCy8at/f34lZIVkC5r8/Q7HE9KwKLvYxn49dbuWRLfcU0HMdxQI8H42E4DB6GwONpNCAoYWfYv5Vtk2mm+J1sfKt6ren9VwhmK9k2C8u98rR4f+2wr5bFt26C82jvpZHfWOnkrtjFAZSDhwjV8CBNCpRERZIAaYomDwgTBYmFEpEsl04yskcw9Go+WPggsn4pAai9vHZ+yTxbViJUaii3MbLrrSqGULqH8Tv8bDngcHwzli0fBsG/xOX8bFUwMZuEMItYli0XwxdU95qrfXXZkmNC6ZeomtBIwRL3eq8sZkuv4oPYpb5NyShFhO1msmXBk51JqvhiVMfw73oLC55Atnzbop57Z0uJRFHRZAk6IBCmqMpuB+MI1LWHGpxVQ/TAwc4ZFKedXYIoeFFwmiAB74WBTZEdHOx1Wr8rvQE4hDeSK5sMF2sVSqni+2wi66ADuiNAvSUSwENZWPRynZflggTtVNp8ExPG2jzFBMI6K+CgBjQOYKgBc03JbBhG4TkfsQe8DEsnZXniMwYqi9PhmKTBGjporz1CpMHreFghcHg=) and import the config into Sensor Logger or scan this QR Code:

<div style="display: flex; justify-content: center;">
  <img src="https://raw.githubusercontent.com/mhaberler/sensorpod/master/assets/qrcode-android.png" alt="Placeholder" style="max-width: 300px; max-height: 300px;">
</div>

## Supported Boards

- **[M5Stack Tab5-P4](https://docs.m5stack.com/en/core/Tab5)**
- **[M5Stack CoreS3](https://docs.m5stack.com/en/core/cores3)**
- **[M5Stack NanoC6](https://docs.m5stack.com/en/core/nanoc6)**
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

### Build for Specific Board

```bash
# Build for Seed Xiao esp32c6
pio run -e seed_xiao_esp32c6

# List all available environments
pio env list
```

### Upload Firmware

```bash
# Upload to board
pio run -e pio run -e seed_xiao_esp32c6 --target upload

# Monitor serial output
pio run -e pio run -e seed_xiao_esp32c6 --target upload --target monitor
```

## Configuration

### WiFi Credentials

Those are set at build time, so decide on the scenario - my recommendation:

- iOS: enable mobile hotspot, have SensorPod connect to the mobile Hotspot
    - no common access point needed
    - mobile still has Internet connectivity via  mobile data or as a WiFi client
    - broker IP address resolved by MDNS.
- Android: connect to SensorPod WiFi AP
    - no common access point needed
    - broker IP address known in advance - always 192.168.4.1
    - MDNS not usable - Android implementation is deficient
    - mobile loses Internet connectivity while connected to SensorPod WiFi AP

Set environment variables before building:

```bash
export WIFI_SSID2="your_ssid"
export WIFI_PASSWORD2="your_password"

pio run -e pio run -e seed_xiao_esp32c6

```

Or edit `platformio.ini` credentials section directly.

### Build Flags

Key configuration options in `platformio.ini`:

```ini
# Debug level (0-5): 0=none, 5=verbose
-DCORE_DEBUG_LEVEL=3

# MQTT port and WebSocket port
-DMQTT_PORT=1883
-DMQTTWS_PORT=8883
```

## Project Structure

```
sensorpod/
├── src/
│   ├── main.cpp           # Application entry point
│   ├── i2cio.cpp/.hpp     # I2C sensor interface
│   ├── mqtt.cpp           # MQTT client implementation
│   ├── BLEScanner.cpp     # Bluetooth scanning
│   ├── wifisetup.cpp      # WiFi configuration
│   └── ringbuffer.hpp     # Data buffering utility
├── platformio.ini         # Project configuration
└── include/               # Header files and configuration
```

## Dependencies

- **[M5Unified](https://github.com/m5stack/M5Unified )**: Board abstraction layer for M5Stack devices
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)**: JSON parsing and generation
- **[PicoMQTT](https://github.com/mlesniew/PicoMQTT.git)**: Lightweight MQTT broker
- **[PicoWebsockets](https://github.com/mlesniew/PicoMQTT.git)**: WebSocket implementation
- **[BTHomeDecoder](https://github.com/fredriknk/BTHomeDecoder.git)**: [BTHome V2](https://bthome.io/) decoder for sensors
- **[Adafruit_VL53L0X](https://github.com/adafruit/Adafruit_VL53L0X)**: driver for the VL53L0X time-of-flight distance sensor

## License

MIT