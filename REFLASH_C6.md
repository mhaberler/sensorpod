# Re-flashing the ESP32-C6 co-processor (ESP-Hosted) without WiFi

ESP32-P4 boards (M5Stack Tab5, Waveshare ESP32-P4-WiFi6) have no radio of their
own. WiFi/BLE run on an ESP32-C6 that talks to the P4 over SDIO using
**ESP-Hosted**. The C6 firmware and the ESP-Hosted host library in the Arduino
core must be the **same version**. This procedure re-flashes the C6 from an
image stored in the P4's LittleFS partition. It runs over SDIO only, so it
works when WiFi is broken.

## When you need it

Typical serial log of a version mismatch (seen on a Tab5 whose C6 had
ESP-Hosted 3.0.6 against host 2.12.13):

```
W hostedHasUpdate(): Version on Host is OLDER than version on co-processor
E H_SDIO_DRV: Dropping packet(s) from stream
E H_SDIO_DRV: Failed to push data to rx queue
E rpc_core: Response not received for [0x101](Req_GetMACAddress)
E rpc_core: Response not received for [0x137](Req_WifiApGetStaList)
Guru Meditation Error: Core  1 panic'ed (Load access fault).
```

- Every P4→C6 RPC times out (5 s each), so `wifi_setup()` starts late and the
  board reboots every ~35 s. The panic is ESP-Hosted dereferencing the missing
  RPC response in `rpc_wifi_ap_get_sta_list()`, reached from
  `WiFi.softAPgetStationNum()` in `wifi_loop()`.
- A few `H_SDIO_DRV` drops during WiFi bring-up alone are normal (see
  `AGENT_LEARNINGS.md`); sustained drops plus `rpc_core` timeouts are not.
- "Host is NEWER" is handled automatically when the board is online
  (`updateEspHostedSlave()` in `wifisetup.cpp`). "Host is OLDER", or no
  internet, needs this procedure.

## Procedure

### 1. Find the host library version

```bash
grep -E 'VERSION_(MAJOR|MINOR|PATCH)_1' \
  ~/.platformio/packages/framework-arduinoespressif32-libs/esp32p4/include/espressif__esp_hosted/host/esp_hosted_host_fw_ver.h
```

Example: `2 / 12 / 13` → **2.12.13**. The web UI (`/`) also shows
"esp-hosted esp32c6 host (expected)" vs "slave (found)" when WiFi works.

### 2. Fetch the matching C6 image into `data/`

```bash
mkdir -p data
curl -fL -o data/esp32c6-v2.12.13.bin \
  https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.12.13.bin
xxd -l 16 data/esp32c6-v2.12.13.bin   # must start with e9 …, byte 12 = 0d (C6)
```

The filename must be exactly `<target>-v<major>.<minor>.<patch>.bin` for the
**host** version. The firmware looks for the basename of
`hostedGetUpdateURL()`, so a mismatched image is never written. `data/*.bin`
is gitignored.

### 3. Enable the flasher in the env

In the board's `[env:…]` in `platformio.ini` (already present in
`m5stack-tab5-m5unified-adsb`):

```ini
build_flags =
    …
    -DHOSTED_FLASH_FS ; re-flash C6 from LittleFS image at boot on version mismatch
board_build.filesystem = littlefs ; data/esp32c6-v<host>.bin, pio run -t uploadfs
```

The partition table needs a data partition labelled `spiffs` that holds
~1.2 MB (Tab5: `app3M_spiffs9M_16MB.csv`, 9.9 MB; Waveshare:
`default_16MB.csv`).

### 4. Upload filesystem and firmware

```bash
pio run -e m5stack-tab5-m5unified-adsb -t uploadfs
pio run -e m5stack-tab5-m5unified-adsb -t upload
```

**If the board is crash-looping**, its USB port appears and disappears and
uploads fail with "port is busy or doesn't exist". Either hold **BOOT** and
press **RESET** to enter download mode, or catch the port with esptool, which
then leaves the chip in the ROM bootloader:

```bash
until ~/.platformio/penv/bin/esptool --chip esp32p4 \
  -p "$(ls /dev/cu.usbmodem* | head -1)" \
  --before default-reset --after no-reset chip-id; do sleep 0.3; done
```

Then run both uploads with `--upload-port <that port>`. The uploads must not
reset the chip in between; pio's defaults are fine once the ROM bootloader
holds it.

### 5. Watch the boot

```bash
pio device monitor -b 115200
```

Expected:

```
W hosted_flash_from_fs(): hosted fs flash: co-processor 3.0.6 -> 2.12.13 from /esp32c6-v2.12.13.bin (1226976 bytes, attempt 1)
W hosted_flash_from_fs(): hosted fs flash: 10%
…
W hosted_flash_from_fs(): hosted fs flash: 100%
W hosted_flash_from_fs(): hosted fs flash: done, restarting
```

About 11 s for 1.2 MB, then a reboot. `H_SDIO_DRV` drops **during** the flash
are expected. After the reboot:
- no "Version on Host is OLDER/NEWER" line,
- no `H_SDIO_DRV` / `rpc_core` errors,
- AP up within ~8 s (it was ~22 s while mismatched).

Leaving `-DHOSTED_FLASH_FS` and the image in place is harmless: on matching
versions the check costs one ESP-Hosted init and returns.

## How it works

`hosted_flash_from_fs()` (`src/hosted_flash_fs.cpp`) runs early in `setup()`,
before ADS-B/USB host, WiFi and BLE:

1. `wifi_set_hosted_pins()`, then `hostedInitWiFi()`. This brings up the SDIO
   transport only; `esp_wifi` is not started.
2. Compares `hostedGetHostVersion()` with `hostedGetSlaveVersion()`. If they
   are equal, returns.
3. Opens `/<target>-vM.m.p.bin` on LittleFS. If missing, logs a hint and
   returns.
4. NVS attempt counter `device-config/hfl_<M>_<m>_<p>`: at most **2**
   attempts per host version, so a failing flash can't reboot-loop. Clear it
   with `prefs.remove("hfl_2_12_13")`, or erase NVS, to retry.
5. `hostedBeginUpdate()` (3 retries) → `hostedWriteUpdate()` in 2 KB chunks →
   `hostedEndUpdate()` → `hostedActivateUpdate()` → `ESP.restart()`.

The `hosted*Update` calls are the Arduino core's wrappers around ESP-Hosted's
slave OTA RPCs (`esp32-hal-hosted.c`). They use the same SDIO/RPC channel as
WiFi, so the flash runs before anything else touches the co-processor.

## If it doesn't work

- **`hostedBeginUpdate()` fails / first RPC times out:** the SDIO link is too
  broken for in-band OTA. Use M5Stack's hardware restore instead: USB-TTL
  adapter (M5Stack ESP32 Downloader) on the C6's reserved download header plus
  M5Burner, per the
  [Tab5 C6 WiFi restore guide](https://docs.m5stack.com/en/guide/tab5/c6_wifi_restore).
  That flashes M5Stack's own C6 image, whose version may again differ from the
  core; follow up with this procedure to align it.
- **"no filesystem":** wrong `board_build.filesystem`, `uploadfs` not run, or
  no `spiffs`-labelled partition.
- **"already tried 2 times, giving up":** clear the NVS key (see above) after
  fixing the cause.
- **Other P4 boards with custom SDIO pins:** `wifi_set_hosted_pins()` applies
  `BOARD_SDIO_CUSTOM_PINS` / `BOARD_SDIO_ESP_HOSTED_*` before the first init, so
  the flasher uses the same pins as WiFi.

## Related

- `src/hosted_ota.cpp`: online downgrade/upgrade from the Arduino CDN
  (`ESP_HOSTED_DOWNGRADE`, debug).
- `AGENT_LEARNINGS.md`: `H_SDIO_DRV` boot-burst drops and the fixed SDIO queue
  size.
