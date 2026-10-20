#!/usr/bin/env python3
"""Prepare six image-generated miniatures with one recorded size conversion."""
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'assets/source/x4/tiny-sprites-v6'
OUT = ROOT / 'assets/source/x4/small-illustrations-v7'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    records = []
    for spec in json.loads((SOURCE / 'selections.json').read_text()):
        source = SOURCE / spec['source']
        region = Image.open(source).convert('L').crop(spec['search_box'])
        bounds = region.point(lambda p: 255 if p < 168 else 0).getbbox()
        if not bounds:
            raise ValueError(f"Empty illustration: {spec['id']}")
        subject = region.crop(bounds)
        width, height = spec['target']
        padding = 2 if width > 48 else 1
        # Resize grayscale once, before monochrome conversion. Never resize
        # the thresholded study PNGs or the packed runtime assets.
        fitted = ImageOps.contain(subject, (width-padding*2, height-padding*2),
                                  Image.Resampling.BOX)
        image = Image.new('L', (width, height), 255)
        image.paste(fitted, ((width-fitted.width)//2, (height-fitted.height)//2))
        image = image.point(lambda p: 255 if p >= 168 else 0)
        image.save(OUT / f"{spec['id']}.png")
        x, y = spec['search_box'][:2]
        records.append(dict(id=spec['id'], source=str(source.relative_to(ROOT)),
                            sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                            crop=[x+bounds[0], y+bounds[1], x+bounds[2], y+bounds[3]],
                            sourceInkSize=list(subject.size), fittedSize=list(fitted.size),
                            runtimeSize=[width,height], resampling='BOX, once before threshold',
                            scaleX=fitted.width/subject.width,
                            scaleY=fitted.height/subject.height, threshold=168,
                            sha256=hashlib.sha256(image.tobytes()).hexdigest()))
    (OUT/'manifest.json').write_text(json.dumps(records, indent=2)+'\n')
    print(f'Prepared {len(records)} illustrations; size conversions recorded in {OUT}/manifest.json')


if __name__ == '__main__':
    main()
