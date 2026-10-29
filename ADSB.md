# ADS-B receiver integration

Connects a Rextron 89001090 ADS-B receiver module to the
`esp32p4_waveshare_devkit` env (Waveshare ESP32-P4-WiFi6). Frames are
decoded on-device (Mode-S DF17/DF18) and published per aircraft as
readsb aircraft.json-style JSON objects to
`{hostname}/ADSB/{icao}`.

## Module

Rextron 89001090, TTL UART, fixed 921600 Bd / 8N1, no flow control.
Outputs ASCII Mode-S extended-squitter frames: `*<hex>;` (~31 bytes/line),
up to ~100 aircraft concurrently. 3V3 supply, ~65-89 mA.

Connector (2x5, 2mm): `3V3, RxD1, TxD1, 1PPS, GND` (host side, pins 1/3/5/9).
`TxD1` = module output → wire to ESP32 RX. `RxD1` = module input (commands).

## Wiring

| Module pin | ESP32-P4 GPIO |
|---|---|
| 3V3 | 3V3 |
| GND | GND |
| TxD1 (module out) | GPIO52 (ESP32 RX) |
| RxD1 (module in) | GPIO53 (ESP32 TX) |

GPIO52/53 chosen as free pins on the 40-pin header, not strapping pins
(P4 strapping = GPIO34-38), routed via GPIO matrix.

## Firmware design

Build-flag gated (`-DADSB_UART`, only set in `esp32p4_waveshare_devkit` env)
— compiled out entirely elsewhere.

### UART ingest (`src/adsb.cpp`)

- Dedicated FreeRTOS reader task (prio 5) blocks on `Serial1.read()`,
  immune to main-loop stalls (HTTP/OTA/mDNS). `HardwareSerial` RX buffer
  set to 16 KB as a first cushion.
- Task frames complete `*<hex>;` lines and pushes the ASCII hex payload
  into a FreeRTOS `RingbufHandle_t` (8 KB, `RINGBUF_TYPE_NOSPLIT`).
- `adsb_loop()`, called from the main `loop()`, drains the ring buffer
  non-blocking, converts hex→binary, and hands the 14-byte frame to the
  decoder — all in loop context (no separate decode task/mutex).

Why a separate reader task instead of draining from `loop()` directly: at
921600 Bd the wire never truly saturates (~20 KB/s realistic vs 92 KB/s
max), but `loop()` can stall for 100+ ms during OTA writes or a slow HTTP
response — long enough to overflow a buffer sized for the average case.
The reader task decouples UART draining from those main-loop stalls.

### Mode-S decode

- `lib/libmodes/` — vendored [Displacer/libmodes](https://github.com/Displacer/libmodes)
  @`7ff29c93` (BSD-2-Clause, a refactor of antirez/dump1090's decoder).
  Entry point `mode_s_decode()` takes an already-demodulated 14-byte frame
  directly — no demodulator/magnitude-vector code is used or built.
  `mode_s_init()` is skipped (it only builds a 33 KB I/Q magnitude LUT for
  the demodulator); config fields (`fix_errors=0`, `aggressive=0`,
  `check_crc=1`) are set directly. CRC is check-only, no bit-error
  correction — the module already CRC-filters before sending; correction
  would just repair frames it already vouched for.
- Two local patches on top of upstream (see comment at top of
  `lib/libmodes/include/mode-s.h`):
  1. DF18 (CF 0/1/2/6, ADS-B/TIS-B rebroadcast) aliased into the DF17
     ME-decode path — upstream only handled DF17, but the module also
     emits DF18.
  2. ME19 (airborne velocity) subtype 1/2: `geo_minus_baro` field added
     to `struct mode_s_msg`, giving geometric altitude via
     `alt_baro + (geo_minus_baro, signed)`.
  3. **Deferred, not decoded**: ME28 (emergency/status), ME31
     (operational status / NIC / NAC quality indicators).
- `src/cpr.c` / `src/cpr.h` — readsb's CPR position solver, ported
  **verbatim** (GPL-3, same license as this repo). Only
  `decodeCPRairborne()` is used: global position solve from an even/odd
  frame pair, needs no receiver reference position — right fit for a
  balloon-borne, non-stationary receiver. Pairing window: 10 s.

### Aircraft table & publish (`src/adsb.cpp`)

- Static table, 100 entries (module's stated max concurrent aircraft),
  keyed by ICAO address, linear scan.
- Evicted after 60 s without a message; eviction publishes an **empty
  payload** to the aircraft's topic as a tombstone, then the slot is
  freed.
- Published per-aircraft, at most once per second, only when new
  messages arrived since the last publish for that aircraft (on-change,
  not a fixed heartbeat).
- Topic: `{hostname}/ADSB/{icao}` (lowercase hex, e.g. `sensorpod/ADSB/40621d`).
  Chosen over one aggregate `aircraft.json`-style document because
  PicoMQTT's default `PICOMQTT_MAX_MESSAGE_SIZE` is 1024 bytes — a
  100-aircraft aggregate would run ~20 KB and get truncated by consumers
  using the default.
- JSON schema: readsb `aircraft.json` field names, subset —
  `hex`, `flight`, `category`, `alt_baro`, `alt_geom`, `gs`, `track`,
  `baro_rate`, `squawk`, `lat`, `lon`, `seen_pos`, `seen`, `messages`.
  Fields are omitted (not zero-filled) until decoded at least once.
- Receiver-wide counters (frames seen, CRC failures, decoded, ring
  buffer drops, live aircraft count) are folded into the existing 1 Hz
  `{hostname}/status` publish (`src/main.cpp`), not a separate topic.

## Status

Both `m5stack-nanoc6` (default) and `esp32p4_waveshare_devkit` build clean.
Decoder verified standalone against known-good DF17 test frames (CRC ok,
ICAO/altitude/callsign/CPR position all correct). **Not yet tested against
the physical module or over MQTT on device.**

## Deferred / not implemented

- ME28 (emergency status) and ME31 (NIC/NAC/SIL quality indicators) —
  no consumer yet; add later if needed.
- `squawk` — DF17/18 doesn't carry squawk (that's DF5/DF21); the module
  only emits DF17/18, so this field is always omitted. Kept in the
  schema for readsb compatibility.
- Local/relative CPR and any receiver-reference-position logic — this is
  a balloon-borne, moving receiver; a position feed may be added later
  to enable range gating, but isn't required for the global airborne CPR
  solve currently used.
- No 2-bit ("aggressive") CRC error correction — deliberately off.

## Remaining verification

1. Wire module to GPIO52/53, confirm decoded aircraft objects arrive on
   `{hostname}/ADSB/{icao}` (subscribe with `{hostname}/ADSB/#`).
2. Confirm `seen`/`seen_pos` age correctly and tombstones (empty payload)
   are published ~60 s after an aircraft goes quiet.
3. Confirm `{hostname}/status` carries the `adsb_*` counters.
4. Stress test: trigger OTA upload / repeated `/` page loads while
   receiving; confirm `adsb_dropped_overflow` stays 0.
