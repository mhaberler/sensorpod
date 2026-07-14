# C++ preprocessor macros collected by this script (all are `const char*`
# string literals; guard with #ifdef before use). They are NOT injected as
# CPPDEFINES (that would change every compile command line and force full
# rebuilds); instead they are collected in env["BUILD_INFO_MACROS"] and
# emitted into the generated header include/build_info.hpp by
# scripts/inject_lib_versions.py, which runs after this script.
#
#   BUILD_SHA            always, when in a git repo — `git rev-parse --short HEAD`
#   BUILD_DATE           always — UTC ISO-8601 build timestamp
#   BUILD_HOST           always — build origin: "user@host" locally,
#                        "actor@runner (GitHub Actions)" in CI
#   BUILD_REPO           CI only — "$GITHUB_SERVER_URL/$GITHUB_REPOSITORY"
#   BUILD_TAG            CI tag builds — $GITHUB_REF_NAME (matches v?N.N.N…)
#   BUILD_FIRMWARE_URI   CI tag builds — release asset URL for the merged bin
#   BUILD_ENV            always — PlatformIO env name, e.g. "m5stack-nanoc6"
#   BUILD_BOARD          always — board ID, e.g. "esp32-c6-devkitm-1"
#   BUILD_BOARD_NAME     always — human-readable board name, e.g. "M5Stack NanoC6"
#   BUILD_MCU            always — MCU string, e.g. "esp32c6"
#   BUILD_VARIANT        always — board variant, e.g. "m5stack_nanoc6"
#   BUILD_TYPE           always — "debug" or "release"
#   BUILD_PARTITIONS     always — partition file, e.g. "ota_nofs_4MB.csv"
#   BUILD_FLASH_SIZE     always — flash size, e.g. "4MB"
#   BUILD_FRAMEWORK      always — framework name, e.g. "arduino"
#   SGO_DEFAULT_OWNER    if unset + git remote parseable — repo owner
#   SGO_DEFAULT_REPO     if unset + git remote parseable — repo name
#   SGO_DEFAULT_BIN      if unset — ota_bin_filename(env)
#
# Additional defines come from platformio.ini build_flags -
# CORE_DEBUG_LEVEL, AUTOCHECK_INTERVAL, USE_*, etc.).

from __future__ import annotations

import getpass
import os
import re
import socket
import subprocess
from datetime import datetime, timezone
from typing import Any

from SCons.Script import DefaultEnvironment  # type: ignore[import-untyped]

from firmware_naming import (
    merged_bin_filename,
    ota_bin_filename,
    parse_github_remote,
)

env: Any = DefaultEnvironment()


# name -> raw string value; rendered into include/build_info.hpp by
# inject_lib_versions.py (dict preserves insertion order)
build_info_macros: dict[str, str] = {}
env["BUILD_INFO_MACROS"] = build_info_macros


def define_str(name, value):
    build_info_macros[name] = value
    print(f"inject_build_info: {name}={value}")


def is_defined(name):
    for entry in env.get("CPPDEFINES", []):
        key = entry[0] if isinstance(entry, (tuple, list)) else entry
        if key == name:
            return True
    return False


def define_str_if_unset(name, value):
    if is_defined(name):
        return
    define_str(name, value)


def resolve_option(ini_key, board_key, default=""):
    try:
        v = env.GetProjectOption(ini_key)
        if v:
            return v
    except Exception:
        pass
    try:
        return env.BoardConfig().get(board_key, default)
    except Exception:
        return default


def inject_sgo_defaults():
    project_dir = env.subst("$PROJECT_DIR")
    remote = parse_github_remote(project_dir)
    if remote:
        owner, repo = remote
        define_str_if_unset("SGO_DEFAULT_OWNER", owner)
        define_str_if_unset("SGO_DEFAULT_REPO", repo)
    define_str_if_unset("SGO_DEFAULT_BIN", ota_bin_filename(env))


def build_host():
    # CI: prefer GitHub-provided identity over the ephemeral runner's
    # local hostname/user (which are generic, e.g. "runner@fv-az...").
    if os.environ.get("GITHUB_ACTIONS") == "true":
        actor = os.environ.get("GITHUB_ACTOR", "")
        runner = os.environ.get("RUNNER_NAME") or socket.gethostname()
        who = f"{actor}@{runner}" if actor else runner
        return f"{who} (GitHub Actions)"
    try:
        user = getpass.getuser()
    except Exception:
        user = os.environ.get("USER") or os.environ.get("USERNAME") or "unknown"
    return f"{user}@{socket.gethostname()}"


def inject():
    # Git SHA — skip silently if not in a git repo
    try:
        sha = (
            subprocess.check_output(
                ["git", "rev-parse", "--short", "HEAD"],
                stderr=subprocess.DEVNULL,
            )
            .decode()
            .strip()
        )
        if sha:
            define_str("BUILD_SHA", sha)
    except Exception:
        pass

    # Build date — always available
    define_str("BUILD_DATE", datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"))

    # Build host — local "user@host" or CI "actor@runner (GitHub Actions)"
    define_str("BUILD_HOST", build_host())

    # CI-only: repo, tag, firmware URI
    github_repository = os.environ.get("GITHUB_REPOSITORY", "")
    github_server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    github_ref_name = os.environ.get("GITHUB_REF_NAME", "")

    if github_repository:
        define_str("BUILD_REPO", f"{github_server}/{github_repository}")

    tag_match = re.match(r"^v?(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?)$", github_ref_name)
    if tag_match:
        tag = github_ref_name
        define_str("BUILD_TAG", tag)

        if github_repository:
            filename = merged_bin_filename(env)
            uri = f"{github_server}/{github_repository}/releases/download/{tag}/{filename}"
            define_str("BUILD_FIRMWARE_URI", uri)

    # Board & environment metadata
    define_str("BUILD_ENV", env.subst("$PIOENV"))
    define_str("BUILD_BOARD", env.subst("$BOARD"))

    board_name = env.BoardConfig().get("name", "")
    if board_name:
        define_str("BUILD_BOARD_NAME", board_name)

    build_type = env.GetProjectOption("build_type") or "debug"
    define_str("BUILD_TYPE", build_type)

    mcu = resolve_option("board_build.mcu", "build.mcu")
    if mcu:
        define_str("BUILD_MCU", mcu)

    variant = resolve_option("board_build.variant", "build.variant")
    if variant:
        define_str("BUILD_VARIANT", variant)

    partitions = resolve_option("board_build.partitions", "build.arduino.partitions")
    if partitions:
        define_str("BUILD_PARTITIONS", partitions)

    flash_size = env.BoardConfig().get("upload.flash_size", "")
    if flash_size:
        define_str("BUILD_FLASH_SIZE", flash_size)

    define_str("BUILD_FRAMEWORK", env.subst("$PIOFRAMEWORK"))

    inject_sgo_defaults()


inject()
