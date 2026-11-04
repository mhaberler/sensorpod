#pragma once
// Firmware version string for UI, status JSON and Improv device info:
// the release tag for tag builds, else `git describe` (see
// scripts/inject_build_info.py), else "n/a".
#if __has_include("build_info.hpp")
#include "build_info.hpp"
#endif

#if defined(BUILD_TAG)
#define FW_VERSION BUILD_TAG
#elif defined(BUILD_DESCRIBE)
#define FW_VERSION BUILD_DESCRIBE
#else
#define FW_VERSION "n/a"
#endif
