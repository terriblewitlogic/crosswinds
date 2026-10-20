#!/usr/bin/env python3
"""Build packed native monochrome maritime art for the X4.

Regional harbors, title, ships and illustrated sheets use assets/source/x4 sources. A fixed
threshold produces black/white; there is no error-diffusion or ordered dither.
The existing world map supplies coastline geometry only. Approved incident
compositions retain their existing code-native geometry and ship source.

--glyphs-only writes the small-art contact sheet without rebuilding the header.
Full builds require all native sources and enforce 416 KiB packed art (raised
from 320 KiB for the Grey Passenger keepsakes and Sea Atlas landmarks; firmware
is about a third of the 3 MB app partition).
"""

import argparse
import hashlib
import json
from collections import deque
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = ROOT / "assets" / "source"
X4_SOURCE_DIR = SOURCE_DIR / "x4"
PREVIEW_DIR = ROOT / "build" / "art-previews"
HEADER_PATH = ROOT / "img_generated_x4.h"
ART_BUDGET = 416 * 1024
NATIVE_FAMILIES = (("GOOD_DETAILS", "GOOD_DETAIL", 8),
                   ("LANDMARK_DETAILS", "LANDMARK_DETAIL", 24),
                   ("FACILITY_DETAILS", "FACILITY_DETAIL", 3),
                   ("CAPTAIN_DETAILS", "CAPTAIN_DETAIL", 16),
                   ("MAP_SHIPS", "MAP_SHIP", 5),
                   ("DOCK_STAGES", "DOCK_STAGE", 4))
INK, PAPER = 0, 255
SHEET_CROPS = {}
NATIVE_GLYPH_OVERRIDES = {
    (sheet, index): f'{family}-{index}'
    for family, sheet, count in [('good', 'goods-v2.png', 8),
                                  ('landmark', 'landmarks-v2.png', 24),
                                  ('facility', 'facilities-v2.png', 3),
                                  ('region-stamp', 'region-stamps-v2.png', 6)]
    for index in range(count)
}


def glyph_source(filename, index):
    override = NATIVE_GLYPH_OVERRIDES.get((filename, index))
    if override:
        return f'assets/source/x4/small-illustrations-v8/{override}.png'
    return f'assets/source/x4/{filename}#cell-{index}'


def canvas(width, height):
    image = Image.new("L", (width, height), PAPER)
    return image, ImageDraw.Draw(image)


def line(d, points, width=2, fill=INK):
    d.line(points, fill=fill, width=width, joint="curve")


def box(d, bounds, width=2, fill=PAPER):
    d.rectangle(bounds, fill=fill, outline=INK, width=width)


def oval(d, bounds, width=2, fill=PAPER):
    d.ellipse(bounds, fill=fill, outline=INK, width=width)


def poly(d, points, width=2, fill=PAPER):
    d.polygon(points, fill=fill)
    line(d, points + [points[0]], width)


def roof(d, x, y, w, h=8):
    poly(d, [(x, y + h), (x + w // 2, y), (x + w, y + h)], fill=INK)


def windows(d, x, y, count, spacing=8):
    for i in range(count):
        d.rectangle((x + i * spacing, y, x + 3 + i * spacing, y + 4), fill=INK)


def water(d, x, y, width):
    line(d, [(x, y), (x + width, y)])
    line(d, [(x + 6, y + 5), (x + width - 5, y + 5)], 1)


def incident_good_glyph(kind):
    image, d = canvas(24, 24)
    if kind == 0:  # grain sack, wheat stalk
        poly(d, [(8, 5), (16, 5), (19, 17), (17, 21), (6, 21), (4, 17)])
        line(d, [(7, 5), (17, 5)]); line(d, [(12, 9), (12, 18)], 1)
        for y in (10, 13, 16): line(d, [(9, y - 1), (12, y + 1), (15, y - 1)], 1)
    elif kind == 1:  # logs
        for y in (7, 14):
            box(d, (5, y, 18, y + 6)); oval(d, (2, y, 8, y + 6))
            d.point((5, y + 3), fill=INK)
    elif kind == 2:  # cloth bolt
        box(d, (6, 5, 20, 19)); oval(d, (3, 3, 11, 11)); oval(d, (6, 6, 8, 8), 1)
        line(d, [(11, 5), (18, 5), (18, 16), (11, 16)], 1)
        line(d, [(6, 11), (6, 21), (15, 21)], 1)
    elif kind == 3:  # iron tools
        poly(d, [(5, 3), (15, 5), (14, 9), (10, 8), (6, 21), (3, 20), (7, 7), (4, 6)], 1, INK)
        line(d, [(17, 3), (19, 8), (12, 20)], 3)
        line(d, [(20, 3), (21, 8), (18, 9)], 2)
    elif kind == 4:  # tea chest, leaf
        box(d, (3, 5, 21, 21)); line(d, [(3, 8), (21, 8)], 1)
        poly(d, [(8, 16), (9, 11), (16, 10), (15, 16), (8, 17)], 1)
        line(d, [(8, 18), (15, 12)], 1)
    elif kind == 5:  # spice jar
        box(d, (8, 3, 16, 6)); poly(d, [(8, 6), (16, 6), (20, 11), (20, 20), (4, 20), (4, 11)])
        line(d, [(5, 13), (19, 13)], 1)
        for x, y in ((8, 16), (14, 17), (11, 19), (16, 15)):
            d.rectangle((x, y, x + 1, y + 1), fill=INK)
    elif kind == 6:  # medicine case with mortar
        box(d, (3, 7, 21, 21)); box(d, (8, 3, 16, 7))
        d.arc((8, 10, 17, 19), 0, 180, fill=INK, width=2)
        line(d, [(8, 14), (17, 14)], 1); line(d, [(14, 13), (18, 9)], 2)
    else:  # porcelain cup and saucer
        d.arc((5, 5, 17, 19), 0, 180, fill=INK, width=2)
        line(d, [(5, 11), (17, 11)], 2)
        d.arc((15, 10, 22, 17), 265, 100, fill=INK, width=2)
        d.arc((3, 16, 21, 21), 0, 180, fill=INK, width=2)
    return image






def ship_art(kind, width=112, height=88):
    # Keep the entire authored silhouette, including the bow and flag, in view.
    return native_source(X4_SOURCE_DIR / f"ship-{kind}.png", width, height, preserve_subject=True)




def incident_glyph(kind):
    image, d = canvas(128, 64)
    if kind in (0, 1, 2, 6, 7):
        image.paste(ship_art(0, 56, 44), (4 if kind == 7 else 9, 17))
    if kind == 0:
        poly(d, [(80, 46), (120, 46), (112, 56), (88, 56)], 2, INK)
        line(d, [(98, 8), (98, 46)], 2)
        poly(d, [(101, 12), (120, 36), (101, 36)], 1, INK)
        poly(d, [(96, 15), (78, 39), (96, 37)], 1, INK)
    elif kind == 1:
        for x, y, w in ((68, 8, 18), (82, 3, 24), (101, 10, 19)): oval(d, (x, y, x + w, y + 18), 1, INK)
        line(d, [(72, 28), (67, 38)], 2); line(d, [(91, 27), (86, 37)], 2)
        poly(d, [(107, 28), (100, 42), (107, 42), (99, 55), (115, 37), (107, 37)], 1, INK)
    elif kind == 2:
        poly(d, [(69, 52), (82, 27), (96, 52)]); poly(d, [(88, 54), (105, 34), (120, 54)])
        water(d, 65, 57, 57)
    elif kind == 3:
        box(d, (15, 20, 82, 46)); oval(d, (18, 17, 38, 35)); line(d, [(42, 27), (72, 34)], 3)
        line(d, [(17, 46), (17, 55)], 3); line(d, [(80, 45), (80, 55)], 3)
        image.paste(incident_good_glyph(6), (93, 22))
    elif kind == 4:
        box(d, (29, 8, 87, 56)); box(d, (23, 4, 81, 52))
        for y in (15, 24, 33): line(d, [(33, y), (70, y)], 2)
        oval(d, (54, 36, 69, 49)); line(d, [(90, 45), (112, 11)], 3)
    elif kind == 5:
        for x, y in ((19, 15), (49, 9), (79, 17)):
            oval(d, (x, y, x + 17, y + 19))
            poly(d, [(x - 5, y + 37), (x - 1, y + 23), (x + 19, y + 23), (x + 24, y + 37)])
        line(d, [(16, 57), (109, 57)], 2)
    elif kind == 6:
        poly(d, [(78, 43), (117, 43), (108, 52), (87, 52)])
        oval(d, (91, 28, 100, 37)); line(d, [(96, 38), (96, 43)], 2)
        line(d, [(95, 40), (108, 26), (108, 20)], 2); water(d, 74, 56, 46)
    else:
        box(d, (82, 35, 119, 54)); roof(d, 78, 16, 45, 18); image.paste(incident_good_glyph(4), (88, 31))
    return image




def sheet_cuts(projection, divisions):
    """Find the white gutter nearest each expected grid boundary."""
    length = len(projection)
    cuts = [0]
    for i in range(1, divisions):
        expected = round(length * i / divisions)
        radius = max(1, round(length / divisions * 0.25))
        start, end = max(cuts[-1] + 1, expected - radius), min(length, expected + radius + 1)
        values = projection[start:end]
        best = np.flatnonzero(values == values.min()) + start
        cuts.append(int(min(best, key=lambda p: abs(int(p) - expected))))
    return cuts + [length]


@lru_cache(maxsize=8)
def sheet_cells(filename, columns, rows):
    source = Image.open(X4_SOURCE_DIR / filename).convert("L")
    ink = np.asarray(source) < 168
    ys = sheet_cuts(ink.sum(axis=1), rows)
    cells, bounds = [], []
    for row in range(rows):
        xs = sheet_cuts(ink[ys[row]:ys[row + 1], :].sum(axis=0), columns)
        for col in range(columns):
            bounds.append((xs[col], ys[row], xs[col + 1], ys[row + 1]))
            cells.append(source.crop(bounds[-1]))
    SHEET_CROPS[filename] = bounds
    return cells


def sheet_art(filename, columns, rows, index, width, height, padding=2, threshold=168):
    override = NATIVE_GLYPH_OVERRIDES.get((filename, index))
    if override:
        source = ROOT / glyph_source(filename, index)
        image = Image.open(source).convert('L')
        if image.size != (width, height) or not set(image.getdata()).issubset({INK, PAPER}):
            raise ValueError(f'Expected native {width}x{height} monochrome illustration: {source}')
        return image  # already prepared: no second resize or threshold
    cell = sheet_cells(filename, columns, rows)[index]
    bounds = Image.fromarray(np.where(np.asarray(cell) < 168, 255, 0).astype(np.uint8)).getbbox()
    if bounds is None:
        raise ValueError(f"Empty image-generation sheet cell: {filename} {index}")
    # Source sheets can be slightly uneven; trim only exterior white paper.
    subject = cell.crop(bounds)
    fitted = ImageOps.contain(subject, (width - padding * 2, height - padding * 2), Image.Resampling.BOX)
    image = Image.new("L", (width, height), PAPER)
    image.paste(fitted, ((width - fitted.width) // 2, (height - fitted.height) // 2))
    return image.point(lambda value: PAPER if value >= threshold else INK)


def atlas_landmark(index, width=128, height=112, threshold=168):
    # The Sea Atlas page: the port's map landmark cut larger from the same
    # source cell, bypassing the prepared 64x56 native glyph.
    cell = sheet_cells("landmarks-v2.png", 6, 4)[index]
    bounds = Image.fromarray(np.where(np.asarray(cell) < 168, 255, 0).astype(np.uint8)).getbbox()
    subject = cell.crop(bounds)
    fitted = ImageOps.contain(subject, (width - 4, height - 4), Image.Resampling.LANCZOS)
    image = Image.new("L", (width, height), PAPER)
    image.paste(fitted, ((width - fitted.width) // 2, (height - fitted.height) // 2))
    return image.point(lambda value: PAPER if value >= threshold else INK)


def generated_facility(kind, level):
    image, d = canvas(48, 40)
    image.paste(sheet_art("facilities-v2.png", 3, 1, kind, 48, 32, 1), (0, 8))
    # Authorized level overlays: one pennant at level 2, two at level 3.
    if level > 1:
        line(d, [(37, 1), (37, 16)], 1)
        for n in range(level - 1):
            y = 1 + n * 6
            poly(d, [(38, y), (46, y + 3), (38, y + 5)], 1, INK)
    return image


def crosswinds_cell(sheet, cols, rows, index, width, height, threshold=180):
    # Asset conversion only: preserve each generated vignette, fit to native
    # display dimensions and threshold to the panel's one-bit ink format.
    source = Image.open(SOURCE_DIR / 'crosswinds' / sheet).convert('L')
    cw, ch = source.width // cols, source.height // rows
    col, row = index % cols, index // cols
    cell = source.crop((col*cw, row*ch, (col+1)*cw, (row+1)*ch))
    bounds = cell.point(lambda p: 255 if p < 200 else 0).getbbox()
    if bounds: cell = cell.crop(bounds)
    fitted = ImageOps.contain(cell, (width-4, height-4), Image.Resampling.LANCZOS)
    result = Image.new('L', (width,height), 255)
    result.paste(fitted, ((width-fitted.width)//2,(height-fitted.height)//2))
    return result.point(lambda p: 0 if p < threshold else 255)


def glyph_assets():
    ship_sources = [X4_SOURCE_DIR / f"ship-{i}.png" for i in range(5)]
    sheets = [("LANDMARK", "landmarks-v2.png", 6, 4, 64, 56),
              ("GOOD", "goods-v2.png", 4, 2, 24, 24),
              ("CAPTAIN", "captains-v4.png", 4, 4, 48, 56),
              ("REGION_STAMP", "region-stamps-v2.png", 3, 2, 40, 40)]
    required = ship_sources + [X4_SOURCE_DIR / s[1] for s in sheets] + [X4_SOURCE_DIR / "facilities-v2.png"]
    missing = [str(path.relative_to(ROOT)) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("Native X4 illustration sources required (no procedural fallback):\n  " + "\n  ".join(missing))
    assets = [(f"{name}_{i}", sheet_art(filename, cols, rows, i, width, height,
                                      padding=1 if name == "GOOD" else 2,
                                      threshold=190 if name == "GOOD" else 168),
               glyph_source(filename, i))
              for name, filename, cols, rows, width, height in sheets for i in range(cols * rows)]
    assets[32:32] = [(f"SHIP_{i}", ship_art(i), str(path.relative_to(ROOT)))
                     for i, path in enumerate(ship_sources)]
    assets = [(name, crosswinds_cell('regions.png',3,2,int(name.rsplit('_',1)[1]),40,40),
               'assets/source/crosswinds/regions.png#cell-'+name.rsplit('_',1)[1])
              if name.startswith('REGION_STAMP_') else (name, image, source)
              for name,image,source in assets]
    assets[53:53] = [(f"INCIDENT_{i}", crosswinds_cell('encounters.png',4,3,i,128,64),
                       f"assets/source/crosswinds/encounters.png#cell-{i}") for i in range(12)]
    # The Grey Passenger: seven keepsakes for the delivery card, then her
    # portrait for the offer detail (160) and the Parcels row (80). The
    # finely hatched sources read best at a lighter threshold.
    assets += [(f"KEEPSAKE_{i}", crosswinds_cell('keepsakes.png',4,2,i,192,192,140),
                f"assets/source/crosswinds/keepsakes.png#cell-{i}") for i in range(7)]
    assets += [(f"COURIER_{i}", crosswinds_cell('keepsakes.png',4,2,7,size,size,140),
                f"assets/source/crosswinds/keepsakes.png#cell-7@{size}") for i, size in enumerate((160, 80))]
    assets += [(f"ATLAS_LANDMARK_{i}", atlas_landmark(i),
                f"assets/source/x4/landmarks-v2.png#cell-{i}; Sea Atlas 128x112 cut") for i in range(24)]
    assets += [(f"FACILITY_{kind}_{level - 1}", generated_facility(kind, level),
                f"{glyph_source('facilities-v2.png', kind)}; level-{level} pennant overlay")
               for kind in range(3) for level in range(1, 4)]
    for name, filename, size in [('PARCEL_0', 'parcel', 24)] + [
            (f'FITTING_{i}', f'fitting-{i}', 32) for i in range(7)]:
        path = X4_SOURCE_DIR / 'equipment-v1' / (filename + '.png')
        native = Image.open(path).convert('L')
        if native.size != (size, size) or not set(native.getdata()).issubset({INK, PAPER}):
            raise ValueError(f'Expected native monochrome equipment: {path}')
        assets.append((name, native, str(path.relative_to(ROOT))))
    for _, prefix, count in NATIVE_FAMILIES:
        for i in range(count):
            name = f'{prefix}_{i}'
            path = X4_SOURCE_DIR / 'native-size-v9' / (name.lower() + '.png')
            native = Image.open(path).convert('L')
            if not set(native.getdata()).issubset({INK, PAPER}):
                raise ValueError(f'Expected prepared monochrome native art: {path}')
            assets.append((name, native, str(path.relative_to(ROOT))))
    return assets


def native_source(path, width, height, preserve_subject=False, threshold=168):
    source = Image.open(path).convert("L")
    if preserve_subject:
        fitted = ImageOps.pad(source, (width, height), method=Image.Resampling.BOX, color=PAPER)
    else:
        fitted = ImageOps.fit(source, (width, height), method=Image.Resampling.BOX, centering=(0.5, 0.5))
    return fitted.point(lambda value: PAPER if value >= threshold else INK)


def world_chart():
    source = Image.open(SOURCE_DIR / "world-map.png").convert("RGB")
    fitted = ImageOps.fit(source, (560, 280), method=Image.Resampling.BOX)
    rgb = np.asarray(fitted).astype(int)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    sea = (b > r + 10) & (b >= g)
    land_image = Image.fromarray((~sea * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(3))
    land = np.asarray(land_image) > 127
    interior = np.asarray(land_image.filter(ImageFilter.MinFilter(3))) > 127
    coast = land & ~interior
    return Image.fromarray(np.where(coast, INK, PAPER).astype(np.uint8))


def pack_bits(image):
    ink = np.asarray(image) < 128
    height, width = ink.shape
    padded = np.zeros((height, (width + 7) // 8 * 8), dtype=np.uint8)
    padded[:, :width] = ink
    return np.packbits(padded, axis=1, bitorder="big").tobytes()


def exterior_mask(image):
    """Keep ink and enclosed paper opaque; only border-connected paper clears.

    Four-neighbor flood fill runs on the final native bitmap so diagonal ink
    contours remain closed. The returned black pixels pack as opaque mask bits.
    """
    paper = np.asarray(image) >= 128
    height, width = paper.shape
    exterior = np.zeros_like(paper)
    pending = deque()

    def visit(x, y):
        if 0 <= x < width and 0 <= y < height and paper[y, x] and not exterior[y, x]:
            exterior[y, x] = True
            pending.append((x, y))

    for x in range(width):
        visit(x, 0)
        visit(x, height - 1)
    for y in range(height):
        visit(0, y)
        visit(width - 1, y)
    while pending:
        x, y = pending.popleft()
        visit(x - 1, y)
        visit(x + 1, y)
        visit(x, y - 1)
        visit(x, y + 1)
    return Image.fromarray(np.where(exterior, PAPER, INK).astype(np.uint8))


def emit_array(name, data):
    rows = [", ".join(f"0x{v:02X}" for v in data[i:i + 24]) for i in range(0, len(data), 24)]
    return f"static const uint8_t X4_{name}_DATA[{len(data)}] PROGMEM = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def opaque_spans(image):
    """Lossless exterior mask in row spans; avoids another full bitmap per variant."""
    mask = np.asarray(exterior_mask(image)) == INK
    encoded = bytearray()
    for row in mask:
        edges = np.flatnonzero(np.diff(np.r_[False, row, False]))
        encoded.append(len(edges) // 2)
        for a, b in zip(edges[::2], edges[1::2]):
            encoded.extend((int(a), int(b-a)))
    return bytes(encoded)


def native_mask(image):
    spans=opaque_spans(image)
    bitmap=pack_bits(exterior_mask(image))
    return (True,spans) if len(spans)<len(bitmap) else (False,bitmap)


def art_reference(name):
    return f"{{X4_{name}_DATA, X4_{name}_W, X4_{name}_H}}"


def emit_family(label, prefix, count):
    return f"static const X4Art X4_{label}[{count}] = {{\n  " + ",\n  ".join(
        art_reference(f"{prefix}_{i}") for i in range(count)) + "\n};\n"


def preview_glyphs(assets):
    glyph_dir = PREVIEW_DIR / "glyphs"
    glyph_dir.mkdir(parents=True, exist_ok=True)
    cell_w, cell_h, cols = 208, 196, 5
    sheet, d = canvas(cell_w * cols, cell_h * ((len(assets) + cols - 1) // cols))
    for i, (name, image, _) in enumerate(assets):
        image.save(glyph_dir / f"{name.lower()}.png", optimize=True)
        x, y = (i % cols) * cell_w, (i // cols) * cell_h
        sheet.paste(image, (x + (cell_w - image.width) // 2, y + 6))
        d.text((x + 6, y + 176), name, fill=INK)
    sheet.save(PREVIEW_DIR / "glyph-sheet.png", optimize=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glyphs-only", action="store_true", help="write small-art previews without rebuilding the header")
    args = parser.parse_args()
    glyphs = glyph_assets()
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    preview_glyphs(glyphs)
    if args.glyphs_only:
        print(f"wrote {len(glyphs)} small-art assets and build/art-previews/glyph-sheet.png")
        return
    sources = [("TITLE", SOURCE_DIR / "crosswinds" / "title.png", 800, 480)]
    sources += [(f"HARBOR_{i}", X4_SOURCE_DIR / f"harbor-{i}.png", 560, 280) for i in range(6)]
    missing = [str(path.relative_to(ROOT)) for _, path, _, _ in sources if not path.is_file()]
    if missing:
        raise SystemExit("Native X4 sources required (no color-art fallback):\n  " + "\n  ".join(missing))
    assets = [(name, native_source(path, w, h, threshold=200 if name == "TITLE" else 168),
               str(path.relative_to(ROOT))) for name, path, w, h in sources]
    assets.insert(1, ("WORLD_MAP", world_chart(), "assets/source/world-map.png (coastline only)"))
    assets += glyphs
    assets += [(f"LANDMARK_MASK_{name.removeprefix('LANDMARK_')}", exterior_mask(image),
                f"derived:{name}; four-neighbor exterior-paper flood fill")
               for name, image, _ in glyphs if name.startswith("LANDMARK_") and name.removeprefix("LANDMARK_").isdigit()]
    assets += [(f"FACILITY_MASK_{name.removeprefix('FACILITY_')}", exterior_mask(image),
                f"derived:{name}; four-neighbor exterior-paper flood fill")
               for name, image, _ in glyphs if name.startswith("FACILITY_") and "DETAIL" not in name]
    packed = [(name, image, source, pack_bits(image)) for name, image, source in assets]
    native_names = {f'{prefix}_{i}' for _, prefix, count in NATIVE_FAMILIES for i in range(count)}
    native_names.update(f'SHIP_{i}' for i in range(5))
    native_names.add('PARCEL_0')
    masks = [(name, *native_mask(image)) for name, image, _ in assets if name in native_names]
    total = sum(len(data) for _, _, _, data in packed) + sum(len(data) for _, _, data in masks)
    if total > ART_BUDGET: raise SystemExit(f"X4 art budget exceeded: {total:,} > {ART_BUDGET:,} bytes")
    header = ["// Generated by tools/assets/build_generated_art_x4.py. Do not edit by hand.",
              "// Native 1-bit art; rows byte padded, MSB first, set bit = black ink.",
              "// LANDMARK_MASK/FACILITY_MASK set bit = opaque (ink or enclosed white); clear bit = transparent.",
              "// No ordered or error-diffusion dithering. See assets/source/x4/README.md.",
              "#pragma once", "#include <stdint.h>", "#ifndef PROGMEM", "#define PROGMEM", "#endif", "",
              "struct X4Art { const uint8_t* data; uint16_t width, height; };",
              f"static constexpr uint32_t X4_ART_PACKED_BYTES = {total}u;",
              f"static_assert(X4_ART_PACKED_BYTES <= {ART_BUDGET}u, \"X4 packed art exceeds 416 KiB\");", ""]
    manifest = {"format": "1-bit MSB first; one is ink; no dither", "budgetBytes": ART_BUDGET,
                "packedBytes": total, "sourceSheetCrops": SHEET_CROPS,
                "thresholds": {"default": 168, "goods": 190, "title": 200,
                               "nativeIllustrationOverrides": "prepared at 155 or 168; see small-illustrations-v8/manifest.json; packed unchanged"},
                "landmarkMaskFormat": "1-bit MSB first; one is opaque; four-neighbor border-connected white is transparent",
                "facilityMaskFormat": "1-bit MSB first; one is opaque; four-neighbor border-connected white is transparent",
                "nativeMaskBytes": sum(len(data) for _, _, data in masks),
                "nativeSizeManifest": "assets/source/x4/native-size-v9/manifest.json",
                "assets": []}
    for name, image, source, data in packed:
        header += [f"static constexpr uint16_t X4_{name}_W = {image.width};",
                   f"static constexpr uint16_t X4_{name}_H = {image.height};", emit_array(name, data)]
        if source != "code-native": image.save(PREVIEW_DIR / f"{name.lower().replace('_', '-')}.png", optimize=True)
        manifest["assets"].append({"name": name, "width": image.width, "height": image.height,
                                   "bytes": len(data), "source": source, "sha256": hashlib.sha256(data).hexdigest()})
    for label, prefix, count in (("HARBORS", "HARBOR", 6), ("LANDMARKS", "LANDMARK", 24),
                                ("LANDMARK_MASKS", "LANDMARK_MASK", 24),
                                ("GOODS", "GOOD", 8), ("SHIPS", "SHIP", 5), ("CAPTAINS", "CAPTAIN", 16),
                                ("INCIDENTS", "INCIDENT", 12), ("REGION_STAMPS", "REGION_STAMP", 6),
                                ("PARCELS", "PARCEL", 1), ("FITTINGS", "FITTING", 7),
                                ("KEEPSAKES", "KEEPSAKE", 7), ("COURIERS", "COURIER", 2),
                                ("ATLAS_LANDMARKS", "ATLAS_LANDMARK", 24)):
        header.append(emit_family(label, prefix, count))
    for label, prefix, count in NATIVE_FAMILIES:
        header.append(emit_family(label, prefix, count))
    for name, spans, data in masks:
        header.append(emit_array('OPAQUE_' + name, data))
    header.append('struct X4NativeMask { const uint8_t* ink; const uint8_t* rows; bool spans; };')
    header.append('static const X4NativeMask X4_NATIVE_MASKS[] = {\n' + ',\n'.join(
        f'  {{X4_{name}_DATA, X4_OPAQUE_{name}_DATA, {str(spans).lower()}}}' for name, spans, _ in masks) + '\n};')
    for label, prefix in (("FACILITIES", "FACILITY"), ("FACILITY_MASKS", "FACILITY_MASK")):
        header.append(f"static const X4Art X4_{label}[3][3] = {{\n  " + ",\n  ".join(
            "{" + ", ".join(art_reference(f"{prefix}_{kind}_{level}") for level in range(3)) + "}"
            for kind in range(3)) + "\n};\n")
    header += ["// Compatibility with the original single-harbor renderer.",
               "static constexpr uint16_t X4_HARBOR_W = X4_HARBOR_0_W;",
               "static constexpr uint16_t X4_HARBOR_H = X4_HARBOR_0_H;",
               "static const uint8_t* const X4_HARBOR_DATA = X4_HARBOR_0_DATA;", ""]
    assets[2][1].save(PREVIEW_DIR / "harbor.png", optimize=True)
    assets[1][1].save(PREVIEW_DIR / "world-map.png", optimize=True)
    header_temp = HEADER_PATH.with_suffix(".h.tmp")
    header_temp.write_text("\n".join(header), encoding="ascii")
    header_temp.replace(HEADER_PATH)
    (PREVIEW_DIR / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {len(packed)} X4 assets, {total:,}/{ART_BUDGET:,} packed bytes")
    print(f"wrote {HEADER_PATH.relative_to(ROOT)}")


if __name__ == "__main__": main()
