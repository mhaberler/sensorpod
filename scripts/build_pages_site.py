#!/usr/bin/env python3
"""Assemble the ESP Web Tools install site for GitHub Pages.

Usage: build_pages_site.py <firmware_dir> <site_src_dir> <out_dir> <version>

<firmware_dir> holds the merged *_firmware*.bin images plus one
<env>.meta.json sidecar per env ({"env", "mcu", "board_name"}) written by
the release build job. Produces <out_dir> with the static page, the bins,
manifests/<env>.json (one per env, single merged part at offset 0) and
boards.json (board picker list).
"""
import glob
import json
import os
import shutil
import sys

CHIP_FAMILY = {
    "esp32": "ESP32",
    "esp32s2": "ESP32-S2",
    "esp32s3": "ESP32-S3",
    "esp32c3": "ESP32-C3",
    "esp32c5": "ESP32-C5",
    "esp32c6": "ESP32-C6",
    "esp32h2": "ESP32-H2",
    "esp32p4": "ESP32-P4",
}


def main(fw_dir, site_src, out_dir, version):
    shutil.copytree(site_src, out_dir, dirs_exist_ok=True)
    os.makedirs(os.path.join(out_dir, "firmware"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "manifests"), exist_ok=True)

    boards = []
    for meta_path in sorted(glob.glob(os.path.join(fw_dir, "*.meta.json"))):
        with open(meta_path) as f:
            meta = json.load(f)
        env = meta["env"]
        bins = glob.glob(os.path.join(fw_dir, f"*_{env}_firmware*.bin"))
        if len(bins) != 1:
            sys.exit(f"{env}: expected 1 merged bin, found {bins}")
        chip = CHIP_FAMILY.get(meta["mcu"])
        if not chip:
            sys.exit(f"{env}: unknown mcu {meta['mcu']!r}")
        bin_name = os.path.basename(bins[0])
        shutil.copy2(bins[0], os.path.join(out_dir, "firmware", bin_name))
        manifest = {
            "name": f"SensorPod ({env})",
            "version": version,
            "new_install_prompt_erase": True,
            "builds": [
                {
                    "chipFamily": chip,
                    "parts": [{"path": f"../firmware/{bin_name}", "offset": 0}],
                }
            ],
        }
        with open(os.path.join(out_dir, "manifests", f"{env}.json"), "w") as f:
            json.dump(manifest, f, indent=2)
        boards.append(
            {
                "env": env,
                "name": meta.get("board_name") or env,
                "chip": chip,
                "manifest": f"manifests/{env}.json",
            }
        )

    if not boards:
        sys.exit("no *.meta.json found")
    with open(os.path.join(out_dir, "boards.json"), "w") as f:
        json.dump({"version": version, "boards": boards}, f, indent=2)
    print(f"site: {len(boards)} boards, version {version}")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:])
