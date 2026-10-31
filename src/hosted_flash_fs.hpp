#pragma once

/// Flash the ESP-Hosted co-processor from a LittleFS-bundled image over SDIO
/// (no WiFi needed) when its version differs from the host library's.
/// Call early in setup(), before WiFi/BLE. No-op unless built with
/// -DHOSTED_FLASH_FS on a BOARD_HAS_SDIO_ESP_HOSTED board. Restarts on
/// success.
void hosted_flash_from_fs();
