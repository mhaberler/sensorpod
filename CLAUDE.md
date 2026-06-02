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
  elsewhere). 5× click on the button clears creds and restarts (factory
  reset); long-press wipes creds without reboot.
- `src/wifisetup.cpp` — AP + STA bring-up, mDNS announcements
  (`_mqtt._tcp`, `_mqtt-ws._tcp`, `_http._tcp`), HTTP server registration
  (`/` sysinfo HTML, `/data` JSON, `/update` when OTA enabled), sysinfo
  renderers. Owns the global `WebServer http_server(80)`. HTTP runs on AP+STA
  simultaneously, so sysinfo + OTA are reachable regardless of WiFi state.
- `src/mqtt.cpp` — PicoMQTT broker (TCP 1883 + WebSocket 8883), also runs
  on both AP and STA.
- `src/ota.cpp` — web OTA updater. Compiled only when `OTA_WEB_UPDATER` is
  defined (composed in via the `[ota]` build-flag block in `platformio.ini`,
  added to default + release envs). Uses Arduino `Update.h`, expects
  `*_ota.bin` (app-only image, not the merged `*_firmware.bin`), CSRF-checks
  `Origin` vs `Host`, reboots on success.
- `src/http_server.hpp` — shared `WebServer` extern, page CSS, and
  `sysinfo_html` / `sysinfo_json` / `ota_setup` prototypes.
- `src/credstore.hpp` — NVS `Preferences` wrapper for SSID/password.

If older notes reference `BLEScanner.cpp`, `i2cio`, or `ringbuffer`, those
are still aspirational — not present on this branch.

## Build-time injection (scripts/)

`extra_scripts` in `[env]` runs these in order — read them before touching the
build pipeline:

- `pre:inject_build_info.py` — defines `BUILD_SHA`, `BUILD_DATE`, and in CI
  `BUILD_REPO` / `BUILD_TAG` / `BUILD_FIRMWARE_URI`. Also auto-derives
  `SGO_DEFAULT_OWNER` / `SGO_DEFAULT_REPO` from `git remote get-url origin` and
  `SGO_DEFAULT_BIN` from the computed OTA filename. Code consuming these macros
  must `#ifdef`-guard — they're absent outside git/CI.
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
