import os, subprocess, re, shlex
Import("env")

# Library names come from `custom_inject_lib_versions` in the env section,
# whitespace-separated; quote names containing spaces.
raw = env.GetProjectOption("custom_inject_lib_versions", "")
libs = shlex.split(raw)

def manifest_version(name):
    for lb in env.GetLibBuilders():
        if lb.name == name:
            return (lb._manifest or {}).get("version")
    return None

def properties_version(name):
    # Fallback when only library.properties exists (no library.json).
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"),
                        env.subst("$PIOENV"), name, "library.properties")
    try:
        with open(path) as f:
            for line in f:
                if line.startswith("version="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        return None
    return None

def git_sha(name):
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"),
                        env.subst("$PIOENV"), name)
    try:
        return subprocess.check_output(
            ["git", "-C", path, "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return None

def macro_name(name):
    s = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").upper()
    return f"{s}_VERSION"

# Macros collected by pre:inject_build_info.py; add lib versions, then emit
# everything into a generated header instead of CPPDEFINES (which would
# change every compile command line and force full rebuilds).
macros = env.get("BUILD_INFO_MACROS")
if macros is None:
    print("inject_lib_versions: warning: BUILD_INFO_MACROS missing "
          "(inject_build_info.py did not run?)")
    macros = {}

for name in libs:
    ver = (manifest_version(name) or properties_version(name) or
           git_sha(name) or "unknown")
    macros[macro_name(name)] = ver
    print(f"inject_lib_versions: {macro_name(name)}={ver}")

# BUILD_DATE/BUILD_HOST change on every run; ignoring them in the comparison
# keeps the header (and its 3 includers) from rebuilding when nothing else
# changed. They then reflect the last meaningful build-info change.
VOLATILE = ("BUILD_DATE", "BUILD_HOST")

def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')

lines = [
    "// Auto-generated - do not edit",
    "// (scripts/inject_build_info.py + scripts/inject_lib_versions.py)",
    "#pragma once",
]
for name, value in macros.items():
    lines.append(f'#define {name} "{c_escape(value)}"')
content = "\n".join(lines) + "\n"

# File A: full current values (fresh BUILD_DATE), always written.
build_dir = env.subst("$BUILD_DIR")
os.makedirs(build_dir, exist_ok=True)
with open(os.path.join(build_dir, "build_info_full.hpp"), "w") as f:
    f.write(content)

# File B: the header code includes; only touched when non-volatile content
# differs, so its mtime stays put on no-op builds.
def significant(text):
    skip = tuple(f"#define {k} " for k in VOLATILE)
    return [l for l in text.splitlines() if not l.startswith(skip)]

header_path = os.path.join(env.subst("$PROJECT_INCLUDE_DIR"), "build_info.hpp")
try:
    with open(header_path) as f:
        old = f.read()
except OSError:
    old = None

if old is None or significant(old) != significant(content):
    os.makedirs(os.path.dirname(header_path), exist_ok=True)
    with open(header_path, "w") as f:
        f.write(content)
    print(f"inject_lib_versions: updated {header_path}")
else:
    print("inject_lib_versions: build_info.hpp unchanged")
