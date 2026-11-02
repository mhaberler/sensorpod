# rtlsdr (vendored)

RTL-SDR (RTL2832U + R820T/R828D) driver on the ESP-IDF USB Host Library.

Source: https://github.com/jstockdale/T-Display-P4, branch `adsb`,
commit `6fd3692ee2b690512a345c06c292bc4c5acc82d2`,
`main/examples/adsb_receiver/` — files copied unmodified:
`esp_libusb.{c,h}`, `librtlsdr.c`, `rtl-sdr.h`, `rtl-sdr_export.h`,
`rtlsdr_i2c.h`, `reg_field.h`, `tuner_{e4k,fc0012,fc0013,fc2580,r82xx}.{c,h}`.

Local change (`librtlsdr.c`, `rtlsdr_open()`): the E4000 tuner probe,
commented out upstream, is re-enabled and moved after the R820T/R828D
probes (NooElec SMArTee XTR). The FC0012/FC0013/FC2580 probes stay
disabled — those tuners can't usefully reach 1090 MHz.

Lineage: osmocom librtlsdr (Steve Markgraf et al., GPL-2.0-or-later) →
kvhnuke ESP32 port (`esp_libusb` shim) → ADS-B Scope (Blog V4/R828D
support, EP0 STALL tolerance, bias-tee, pre-allocated transfers).

Only ESP32-P4 (USB 2.0 High-Speed host) sustains the 2 MS/s needed for
ADS-B. Used by `src/adsb_rtlsdr.cpp` (`-DADSB_RTLSDR`).
