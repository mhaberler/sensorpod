# ADS-B receiver integration

Connects a Rextron 89001090 ADS-B receiver module to the
`esp32p4_waveshare_devkit` env (Waveshare ESP32-P4-WiFi6). Frames are
forwarded to MQTT topic `{hostname}/ADSB`.

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

- `src/adsb.hpp` / `src/adsb.cpp`
- Dedicated FreeRTOS reader task (prio 5) blocks on `Serial1.read()`,
  immune to main-loop stalls (HTTP/OTA/mDNS). `HardwareSerial` RX buffer
  set to 16 KB as a first cushion.
- Task frames complete `*...;` lines and pushes them into a FreeRTOS
  `RingbufHandle_t` (8 KB, `RINGBUF_TYPE_NOSPLIT`).
- `adsb_loop()`, called from the main `loop()`, drains the ring buffer
  non-blocking and calls `mqtt_publish("ADSB", frame)` — auto-prefixed to
  `{hostname}/ADSB`.
- Oversize/garbage frames (no `*`/`;` framing) and ring-buffer-full events
  are dropped and counted (`adsb_dropped_lines`, `adsb_dropped_overflow`);
  not yet exposed via `/data` JSON.

Why a separate task instead of draining from `loop()` directly: at
921600 Bd the wire never truly saturates (~20 KB/s realistic vs 92 KB/s
max), but `loop()` can stall for 100+ ms during OTA writes or a slow HTTP
response — long enough to overflow a buffer sized for the average case.
The reader task decouples UART draining from those main-loop stalls.

## Status

Both `m5stack-nanoc6` (default) and `esp32p4_waveshare_devkit` build clean.
**Not yet hardware-tested.**

## Remaining verification

1. Wire module to GPIO52/53, confirm framed lines arrive over MQTT
   (`{hostname}/ADSB`).
2. Stress test: trigger OTA upload / repeated `/` page loads while
   receiving; confirm `adsb_dropped_overflow` stays 0.
3. Optional: send `KP+MODE=0` at boot on TxD1→module's RxD1 to force
   ASCII mode explicitly (module defaults to ASCII already).
