Import("env")
import os
import shutil
import subprocess
import sys

def post_firmware(source, target, env):
    build_dir   = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")

    # Neuer Zielordner: "docs" im Projektverzeichnis
    docs_dir = os.path.join(project_dir, "docs")
    # Erstellt den Ordner, falls er noch nicht existiert
    os.makedirs(docs_dir, exist_ok=True)

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware   = os.path.join(build_dir, "firmware.bin")

    # ─── 1. OTA-Datei: firmware.bin → HP-Remote-OTA.bin ──────────────────────
    if os.path.exists(firmware):
        ota_out = os.path.join(docs_dir, "HP-Remote-OTA.bin")
        try:
            shutil.copyfile(firmware, ota_out)
            print(f"OTA firmware ready: {ota_out} ({os.path.getsize(ota_out):,} bytes)")
        except Exception as e:
            print(f"OTA copy failed: {e}")
    else:
        print("post_firmware: firmware.bin not ready – skipping OTA copy")

    # ─── 2. Merged-Datei für erstes USB-Flashen ──────────────────────────────
    merged = os.path.join(docs_dir, "HP-Remote-merged.bin")

    boot_app0 = os.path.join(
        env.subst("$PROJECT_PACKAGES_DIR"),
        "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin"
    )

    if not all(os.path.exists(f) for f in [bootloader, partitions, firmware]):
        print("merge_bin: binaries not ready – skipping merge")
        return

    esptool_path = os.path.join(
        env.subst("$PROJECT_PACKAGES_DIR"),
        "tool-esptoolpy", "esptool.py"
    )

    python = sys.executable

    cmd = [
        python, esptool_path,
        "--chip", "esp32s3",
        "merge_bin",
        "-o", merged,
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0000",  bootloader,
        "0x8000",  partitions,
        "0xe000",  boot_app0,
        "0x10000", firmware,
    ]

    print(f"Merging → {merged}")
    try:
        subprocess.run(cmd, check=True)
        print(f"merge_bin OK: {os.path.getsize(merged):,} bytes")
    except Exception as e:
        print(f"merge_bin failed: {e}")


env.AddPostAction("$BUILD_DIR/firmware.bin", post_firmware)
