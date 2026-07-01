# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP32 firmware (Arduino framework via PlatformIO) for a sensor pod that bridges
BLE/I2C sensors to the Sensor Logger app over its own MQTT broker. Targets a
fleet of M5Stack boards plus Waveshare/Seeed ESP32-C5/C6/P4 devkits — most
target divergence is encoded in `platformio.ini` env composition, not in `#ifdef`s.

## Build & Flash

PlatformIO-only. Use **[Pioarduino](https://github.com/pioarduino/pioarduino)**
(a PlatformIO fork with ESP32 enhancements):

```bash
brew install pioarduino/pioarduino/pioarduino

pio run                                # build default env
pio run -e <env>                       # build specific env
pio run -e <env> -t upload             # build + flash
pio run -e <env> -t upload -t monitor  # flash + open 115200 serial
pio run -e <env> -t firmware           # merged single-binary image → firmware/
pio run -t compiledb [-e <env>]        # generate compile_commands.json
pio pkg update                         # refresh lib_deps from upstream HEADs
clang-format -i src/*.cpp src/*.hpp    # format C/C++ after edits
```

Default env set in `[platformio]` of `platformio.ini` (currently `m5stack-nanoc6`).
Active board envs: `m5stack-nanoc6` (default), `esp32p4_waveshare_devkit` (both in CI).

Env names live in `[env:*]` sections. CI-built envs listed in `[ci].envs`
(parsed by `.github/workflows/*.yml` via `configparser`) — **update both** when
adding a release-target board.

**Firmware artifacts:**
- `*_firmware.bin` — merged image (bootloader + partitions + app), USB-only flash via esptool
- `*_ota.bin` — app slot only, use for OTA web updater on `/update` page

WiFi creds optional at build time; runtime Improv-WiFi (serial) provisioning
normal. Creds stored in NVS `Preferences` namespace `wifi-creds`.

## Architecture

Multi-file Arduino sketch. Current sources:

- `src/main.cpp` — `setup()`/`loop()`, sensor polling (VL53L0X), MQTT publish,
  OneButton handling. `setup()` brings up M5Unified (where defined), starts
  Improv-WiFi over serial, tries cached creds from NVS, and starts the HTTP
  server on connect. `loop()` watches `WiFi.status()` and falls back to
  Improv-WiFi BLE provisioning after `TIME_TO_CONNECT` (15s on P4, 8s
  elsewhere). Long-press (`longPressErase`) wipes creds and stops STA
  without reboot; AP stays up. Click counts (single/double/multi) set
  `numClicks` but are not wired to any action on this branch (no 5×
  factory reset).
- `src/wifisetup.cpp` — AP + STA bring-up, mDNS announcements
  (`_mqtt._tcp`, `_mqtt-ws._tcp`, `_http._tcp`). Brings up `WebServer http_server(80)`
  via `webserver_setup()`. WiFi runs on AP+STA simultaneously. AP SSID **and**
  PSK are both `hostName + ".local"` (so the SSID doubles as the device
  hostname); WPA2-protected, not open.
- `src/webserver.cpp` — HTTP server lifecycle. `webserver_setup()` registers routes
  (`/` sysinfo HTML, `/data` JSON, `/update` when OTA enabled) and calls `begin()`.
  `webserver_loop()` handles incoming requests.
- `src/content.cpp` — HTML/JSON sysinfo generation (`sysinfo_html`, `sysinfo_json`).
  Uses `hostName` (runtime string) and all injected `BUILD_*` macros.
- `src/mdns_state.hpp` — shared `MdnsAnnounce` struct and `mdns_services[]`/`mdns_count`
  externs, used by both wifisetup and content.
- `src/mqtt.cpp` — PicoMQTT broker (TCP 1883 + WebSocket 8080), also runs
  on both AP and STA.
- `src/ota.cpp` — web OTA updater. Compiled only when `OTA_WEB_UPDATER` is
  defined (composed in via the `[ota]` build-flag block in `platformio.ini`,
  added to default + release envs). Uses Arduino `Update.h`, expects
  `*_ota.bin` (app-only image, not the merged `*_firmware.bin`), CSRF-checks
  `Origin` vs `Host`, reboots on success.
- `src/listenv.cpp` — logs all injected build metadata on startup via `listEnv()`.
- `src/http_server.hpp` — shared `WebServer` extern, page CSS, and function
  prototypes (`sysinfo_html`, `sysinfo_json`, `webserver_setup`, `webserver_loop`, `ota_setup`).
- `src/credstore.hpp` — NVS `Preferences` wrapper for SSID/password.

If older notes reference `BLEScanner.cpp`, `i2cio`, or `ringbuffer`, those
are still aspirational — not present on this branch.

## Dual-Role MQTT (Broker / Client)

Single firmware supports two runtime modes, switchable via web UI without reflash:

- **Broker Mode (default):**
  - Local MQTT hub (PicoMQTT::Server, TCP 1883 + WS 8080)
  - AP always on (SSID + PSK both `hostName + ".local"`, WPA2)
  - STA optional (if WiFi creds exist)
  - Announces via mDNS (`_mqtt._tcp.local`, `_mqtt-ws._tcp.local`, `_http._tcp`)
  - All topics prefixed with `hostName/` (e.g., `sensorpod/VL53L0X`)

- **Client Mode:**
  - Connects to remote broker discovered via mDNS
  - AP always on (SSID + PSK both `hostName + ".local"`, WPA2)
  - STA required (cannot function without WiFi)
  - Discovers available brokers every 10s, retries with exponential backoff (1s→2s→4s→8s→16s→60s cap)
  - Publishes to remote broker, all topics prefixed with `hostName/`

**Role Storage & Switching:**

- Role stored in NVS `Preferences` as boolean (`_broker_mode`)
- Default: Broker (true)
- Web UI (`/`) shows current role with toggle checkbox
- Click "Save & Restart" to switch roles → triggers `ESP.restart()`
- Role persists across reboot

**Key Files:**
- `src/deviceconfig.hpp` — NVS wrapper (`getBrokerMode`, `setBrokerMode`, `getSelectedBrokerHostname`, `setSelectedBrokerHostname`)
- `src/mqtt_device.hpp` — abstract base for polymorphic Broker/Client handling
- `src/mqtt.hpp/cpp` — `CustomMQTTServer` extends `MQTTDevice`, wraps PicoMQTT::Server
- `src/mqtt_client.hpp/cpp` — `MQTTClient` extends `MQTTDevice`, wraps PicoMQTT::Client with deferred connect (pending_host/port), exponential backoff retry, failover detection
- `src/mdns_client.hpp/cpp` — `MDNSClient::discover_mqtt_brokers()` queries mDNS, spawns async task (`start_async_discovery()`) to avoid blocking main loop
- `src/main.cpp` — reads role at boot, initializes appropriate `mqtt_device`, periodically kicks off async mDNS discovery (non-blocking), applies `resolve_broker_host()` to append `.local` to bare hostnames
- `src/wifisetup.cpp` — role-branching WiFi init (Broker: optional STA, Client: required STA)
- `src/webserver.cpp` — `/api/set-role` and `/api/set-broker` endpoints
- `src/content.cpp` — web UI role toggle, broker selection dropdown populated from `/data` JSON (`discovered_brokers` array)
- `src/led.hpp/cpp` — status feedback (GREEN: WiFi+broker OK, ORANGE/SLOW: broker down, RED/FAST: WiFi down)

**Topic Prefixing:**
All MQTT publishes go through `mqtt_publish(const char *topic, const char *payload)` wrapper
(in `src/mqtt.cpp`), which auto-prepends `hostName/` to topic. Ensures consistency across modes.

**Connection & Failover:**

*Deferred Connect:* `mqtt_client.connect()` stores `pending_host/port` and returns immediately; actual TCP connect happens from `loop()` once WiFi has an IP (avoids race condition on boot).

*Retry Logic:* On broker disconnect:
- Exponential backoff: 1s → 2s → 4s → 8s → 16s → 60s (capped)
- Max 5 retry attempts before triggering rediscovery
- Resets backoff on successful reconnection
- Logs all retry attempts for debugging

*Hostname Resolution:* Bare mDNS labels (e.g., `esp32c6-4483D8`, `sensorpod`) get `.local` appended before connecting. FQDNs and IPs pass through unchanged.

*Rediscovery:* When retries exhausted, `main.cpp` kicks off async mDNS discovery (runs on FreeRTOS background task to prevent blocking `improvSerial.handleSerial()`). Connects to first discovered broker.

**Naming & mDNS:**
- Device hostname: set by `WiFi.getHostname()` (usually MAC-derived like `esp32c6-5B0A24`)
- mDNS instance: announced as `{hostname}-{service}` (e.g., `esp32c6-5B0A24-TCP-083AF24483D8`)
- Service type: `_mqtt._tcp.local` for MQTT broker
- All topics in Broker mode: `{hostname}/VL53L0X`, `{hostname}/status`
- All topics in Client mode: same prefix, published to remote broker

**Testing:**
See `validation-checklist.md` for 8 test scenarios. Blockers: A (fresh Broker), B (role switch),
C (Client discovery), G (topic prefixing), H (reboot).

## Build-time injection (scripts/)

`extra_scripts` in `[env]` runs these in order — read them before touching the
build pipeline:

- `pre:inject_build_info.py` — defines:
  - Always: `BUILD_SHA`, `BUILD_DATE`, `BUILD_ENV`, `BUILD_BOARD`, `BUILD_TYPE`,
    `BUILD_MCU`, `BUILD_PARTITIONS`, `BUILD_FLASH_SIZE`, `BUILD_FRAMEWORK`
  - Conditional: `BUILD_BOARD_NAME` (if board JSON has name), `BUILD_VARIANT`
    (if board defines it)
  - CI only: `BUILD_REPO` / `BUILD_TAG` / `BUILD_FIRMWARE_URI`
  - Auto-derived: `SGO_DEFAULT_OWNER` / `SGO_DEFAULT_REPO` from git remote,
    `SGO_DEFAULT_BIN` from computed OTA filename.
  Code consuming CI-only macros must `#ifdef`-guard.
- `inject_lib_versions.py` — turns each name in `custom_inject_lib_versions`
  (e.g. `"Improv WiFi Library" OneButton`) into a `<LIBNAME>_VERSION` define
  scraped from `library.json`.
- `post:generate_merged_firmware.py` — produces the single-file flashable image
  under `firmware/` named via `firmware_naming.merged_bin_filename`. Naming is
  `<project>_<env>_firmware_<version>.bin`; version resolves from
  `custom_firmware_version` → `custom_firmware_version_file` regex →
  `GITHUB_REF_NAME` → empty.
- `post:bump_version.py` — version bumping helper.

## Boards

All board params live in `platformio.ini` — no custom board JSONs. Use stock
pio board names (`m5stack-core-esp32-16M`, `esp32-c5-devkitc1-n16r4`, …) and
override `board_build.*` / `board_upload.*` in the env when the variant
differs (e.g. C5 N16R8 reuses the N16R4 stock board; PSRAM size is
runtime-detected). Partition CSVs at repo root:
`app3M_spiffs9M_16MB.csv` (Tab5), `ota_nofs_4MB.csv` (NanoC6) — wire them via
`board_build.partitions` in the env, not by editing the CSVs casually (wrong
offsets brick devices).

**MCU override gotcha**: `[env]` sets `board_build.mcu = esp32` as a default.
Any non-classic-ESP32 env (C5/C6/H2/P4/S3) MUST set `board_build.mcu = <chip>`
in its base section, or pio silently picks the xtensa-esp32 toolchain against
the chip-specific libs — surfaces as undefined symbols like `HWCDCSerial`
deep inside the BLE lib, not as a clean toolchain error.

## Conventions

- `.clang-format` customized — run `clang-format -i src/*.cpp src/*.hpp` when editing C/C++.
- Two extra ini files as history: `platformio-orig.ini`, `platformio-m5stack.ini`.
  **Not** included; treat as reference, never edit.
- `managed_components/` + `.pio/` generated — never edit.
- `untracked/` is local scratch space.
- `compile_commands.json` is gitignored. Regenerate after env switch or lib update:
  `pio run -t compiledb [-e <env>]`.
- M5Unified vs vendor-specific M5 libs: envs ending in `-m5unified` use the
  unified abstraction (`-DUSE_M5UNIFIED`); plain envs use `m5stack/M5Stack`/`m5stack/M5Atom`/etc.
  Pick the variant the env was built around when adding board code.
- Build flags: `[release]` + `[debug]` base configs in platformio.ini pull flags from
  `[env]` base and sub-blocks like `[ota]`, `[improv]`, `[ghota]`. Read the
  inheritance chain (`extends = …`) when troubleshooting build-flag composition.

## IDE / clangd

Uses `clangd` (not MS C/C++ IntelliSense) — real clang frontend needed for
multi-chip `#ifdef` branches.

- `.clangd` scrubs Xtensa/GCC-only flags + sets `--target=riscv32-esp-elf`
  (default env `m5stack-nanoc6`). Switch triple for Xtensa envs:
  classic ESP32 → `xtensa-esp32-elf`, S3 → `xtensa-esp32s3-elf`.
- Generate `compile_commands.json`: `pio run -t compiledb [-e <env>]`.
  Regenerate after env switch or lib update.
- Disable MS IntelliSense (avoids double-parse):
  `"C_Cpp.intelliSenseEngine": "disabled"`.

## CI

- `.github/workflows/build-firmware.yml` — manual dispatch, matrix over
  `[ci].envs` (or a JSON array input override).
- `.github/workflows/release.yml` — tag-driven release builds.

Both parse `platformio.ini` with Python `configparser`; keep the `[ci]` section
syntax valid even when adding comments.
