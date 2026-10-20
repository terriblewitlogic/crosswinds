#!/usr/bin/env python3
"""Prepare generated parcel/fitting art once at its packed native dimensions."""
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/source/x4/equipment-v1'


def main():
    records = json.loads((SOURCE / 'originals.json').read_text())
    for r in records:
        source = SOURCE / (r['id'] + '-source.png')
        rgba = Image.open(source).convert('RGBA')
        paper = Image.new('RGBA', rgba.size, 'white')
        paper.alpha_composite(rgba)
        gray = paper.convert('L')
        crop = gray.point(lambda v: 255 if v < 200 else 0).getbbox()
        ink = gray.crop(crop)
        original_size = ink.size
        scale = min(1, (r['size'] - 2) / max(ink.size))
        fitted = tuple(max(1, round(v * scale)) for v in ink.size)
        if fitted != ink.size:
            ink = ink.resize(fitted, Image.Resampling.BOX)
        ink = ink.point(lambda v: 0 if v < 168 else 255)
        native = Image.new('L', (r['size'], r['size']), 255)
        native.paste(ink, ((r['size'] - ink.width)//2, (r['size'] - ink.height)//2))
        assert native.getextrema() == (0,255)
        native.save(SOURCE / (r['id'] + '.png'))
        r.update(crop=crop, sourceInkSize=original_size, fittedSize=fitted,
                 reductionCount=int(scale < 1), threshold=168,
                 sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest())
    (SOURCE / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
    print('Prepared one parcel at 24 × 24 and seven fittings at 32 × 32.')


if __name__ == '__main__':
    main()
