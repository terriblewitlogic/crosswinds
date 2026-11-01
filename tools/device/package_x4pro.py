#!/usr/bin/env python3
"""Package a PlatformIO Pro build as app and complete 16 MB installation images."""
import argparse
import hashlib
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tag', required=True)
parser.add_argument('--build-dir', type=Path, default=Path('.pio/build/x4pro'))
parser.add_argument('--output', type=Path, default=Path('tools/device_build/x4pro'))
args = parser.parse_args()
if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.tag):
    parser.error('tag must contain only letters, digits, dots, underscores or hyphens')
app = (args.build_dir / 'firmware.bin').read_bytes()
factory = (args.build_dir / 'firmware.factory.bin').read_bytes()
if len(app) > 0x640000 or len(factory) > 0x1000000:
    raise SystemExit('Firmware exceeds the app slot or flash size')
if app[0] != 0xe9 or int.from_bytes(app[12:14], 'little') != 9:
    raise SystemExit('Expected an ESP32-S3 application image')
if factory[0x10000:0x10000 + len(app)] != app:
    raise SystemExit('Factory image does not contain this application at 0x10000')
args.output.mkdir(parents=True, exist_ok=True)
stem = f'crosswinds-x4pro-{args.tag}'
files = {
    f'{stem}-app.bin': app,
    f'{stem}-full.bin': factory.ljust(0x1000000, b'\xff'),
}
for name, data in files.items():
    (args.output / name).write_bytes(data)
(args.output / f'{stem}-sha256.txt').write_text(''.join(
    f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name, data in files.items()))
print(f'Packaged {stem}: app {len(app):,} bytes; full 16,777,216 bytes')
