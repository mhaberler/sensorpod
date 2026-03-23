# Kelvin SensorPod

A PlatformIO-based firmware for ESP32 sensor devices with WiFi, MQTT, and I2C sensor integration.

## Features

- **Multi-Board Support**: Compatible with M5Stack and Seeed XIAO ESP32 variants
- **Connectivity**: WiFi, MQTT (over TCP and WebSocket), BLE scanning
- **Sensors**: I2C sensor support for environmental and environmental monitoring
- **Debugging**: Built-in UART debugging with configurable log levels

## Supported Boards

### Display Models
- **M5Stack Core2** - 320x240 display, large form factor
- **M5Stack Tab5-P4** - 7.0" display, tablet-like
- **M5Stack CoreS3** - 2.4" display with touch
- **M5Stack NanoC6** - Compact with I2C sensors

### Headless Models
- **M5Stack AtomS3** - Compact development board
- **M5Stack Stamp S3** - Ultra-small form factor
- **Seeed XIAO ESP32-C6** - Low-power variant
- **ESP32-C5 DevKit** - Development board
- **ESP32-P4 Waveshare DevKit** - High-performance variant

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

Key libraries managed by PlatformIO:

- **M5Unified**: Board abstraction layer for M5Stack devices
- **M5GFX**: Graphics library for M5Stack displays
- **ArduinoJson**: JSON parsing and generation
- **PicoMQTT**: Lightweight MQTT client
- **PicoWebsocket**: WebSocket implementation
- **BTHomeDecoder**: Bluetooth Home decoder for sensors
- **Adafruit_VL53L0X**: driver for the VL53L0X time-of-flight distance sensor

## License

MIT
