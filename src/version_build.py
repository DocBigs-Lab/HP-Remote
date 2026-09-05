Import("env")
import os
import datetime

project_dir = env.subst("$PROJECT_DIR")
version_file = os.path.join(project_dir, "src", "version.txt")

try:
    with open(version_file, "r") as f:
        version = f.read().strip()
except Exception:
    version = "dev"

# Build-Timestamp bei jedem Build neu
build_ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")

print(f"HP-Remote firmware version: {version} (build {build_ts})")

# FW_VERSION + FW_BUILD in einen generierten Header schreiben.
# Da sich der Inhalt (build_ts) bei jedem Build ändert, erkennt PlatformIO
# über die normale Header-Abhängigkeit, dass main.cpp neu kompiliert werden
# muss → firmware.bin (und damit OTA/merged) ist garantiert aktuell.
# (CPPDEFINES allein lösen KEINEN zuverlässigen Rebuild aus.)
header_path = os.path.join(project_dir, "src", "build_info.h")
header_content = (
    "#pragma once\n"
    "// AUTO-GENERIERT von version_build.py – nicht manuell editieren!\n"
    f'#define FW_VERSION "{version}"\n'
    f'#define FW_BUILD   "{build_ts}"\n'
)

# Nur schreiben wenn sich der Inhalt geändert hat (vermeidet unnötige Rebuilds
# wenn im selben Sekundentakt mehrfach gebaut wird – aber build_ts ändert sich
# i.d.R. ohnehin).
old = ""
if os.path.exists(header_path):
    with open(header_path, "r") as f:
        old = f.read()

if old != header_content:
    with open(header_path, "w") as f:
        f.write(header_content)
    print(f"build_info.h aktualisiert: {version} / {build_ts}")
