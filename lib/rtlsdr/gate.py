# Build this library only when -DADSB_RTLSDR is set. lib_ldf_mode = deep
# ignores #ifdef, so the LDF pulls it into every env that compiles
# adsb_rtlsdr.cpp; chips without USB OTG (C6, H2, classic ESP32) have no
# usb/usb_host.h and would fail to compile the driver.
Import("env")

names = set()
for d in env.get("CPPDEFINES", []):
    names.add(d[0] if isinstance(d, (list, tuple)) else str(d).split("=")[0])

if "ADSB_RTLSDR" not in names:
    env["SRC_FILTER"] = ["-<*>"]
