#!/usr/bin/env python3
"""Save the X4's complete flash before installing firmware; never write the reader."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


FLASH_BYTES = 16 * 1024 * 1024
DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / "device_backups"


def backup(port: str, esptool: str, output: Path) -> Path:
    if not port.strip():
        raise ValueError("An explicit X4 serial port is required")
    # Full flash includes the partition table and the existing LittleFS save,
    # even if an older firmware used a different partition layout.
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    folder = output.resolve() / stamp
    # Keep track of newly-created directory entries so success means both
    # image contents and their names have reached the filesystem.
    sync_dirs = [folder]
    ancestor = folder
    while not ancestor.exists():
        sync_dirs.append(ancestor.parent)
        ancestor = ancestor.parent
    folder.mkdir(parents=True, mode=0o700, exist_ok=False)
    partial = folder / "flash.bin.partial"
    command = [esptool, "--chip", "esp32c3", "--port", port, "--baud", "921600",
               "read-flash", "0x0", hex(FLASH_BYTES), str(partial)]
    log_path = folder / "read-flash.log"
    with log_path.open("wb") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise RuntimeError(f"Flash backup failed; upload must stop. See {log_path}")
    actual = partial.stat().st_size if partial.exists() else 0
    if actual != FLASH_BYTES:
        raise RuntimeError(f"Incomplete flash backup ({actual}/{FLASH_BYTES} bytes); upload must stop. See {folder}")
    digest = hashlib.sha256()
    with partial.open("rb") as flash:
        for block in iter(lambda: flash.read(1024 * 1024), b""):
            digest.update(block)
        checksum = digest.hexdigest()
        os.fsync(flash.fileno())
    flash_path = folder / "flash.bin"
    partial.rename(flash_path)
    manifest = {
        "created_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "port": port, "chip": "esp32c3", "flash_bytes": FLASH_BYTES,
        "sha256": checksum, "file": flash_path.name,
        "command": command, "log": log_path.name,
        "uploaded": False,
    }
    with (folder / "manifest.json").open("w") as record:
        json.dump(manifest, record, indent=2)
        record.write("\n")
        record.flush()
        os.fsync(record.fileno())
    for directory in dict.fromkeys(sync_dirs):
        descriptor = os.open(directory, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    print(f"X4 flash backup: {flash_path}\nSHA256: {checksum}")
    return flash_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--esptool", required=True)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    os.umask(0o077)
    try:
        backup(args.port, args.esptool, args.output)
    except (OSError, RuntimeError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
