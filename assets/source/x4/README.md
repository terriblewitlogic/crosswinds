# X4 maritime artwork

The current X4 art direction uses restrained maritime pen-and-ink and engraved
illustration: adult proportions, readable silhouettes, quiet white water and
working harbor architecture. Artwork carries no labels, prices, selections,
weather status or other changing gameplay information.

## Source provenance

- `harbor-0.png` through `harbor-5.png` are purpose-made
  monochrome illustrations generated with the built-in OpenAI image tool on
  2026-09-05. The exact prompts and tool provenance are in `prompts.json`.
- `title-v4.png`, `landmarks-v2.png` and `facilities-v2.png` are the accepted
  mature replacements, generated on 2026-09-05. Their exact prompts and source
  paths are in `architecture-title-prompts.json`. The old `title-v3.png` remains
  source history and is not packed.
- `goods-v2.png` contains eight generated commodities; `goods-prompts.json`
  records its provenance. `captains-v2.png` contains sixteen adult portraits
  and `region-stamps-v2.png` contains six regional seals; both are recorded in
  `people-seals-prompts.json`. All sheets were made with the built-in image tool.
- `ship-0.png` through `ship-4.png` are individual illustrations made with the
  same built-in tool on 2026-09-05: sloop, schooner, brig, packet and indiaman.
  The exact prompts and original generated paths are in `ship-prompts.json`.
  Broad white sails, stout black hulls and sparse rigging keep the five classes
  readable at 112 x 88 pixels. The builder preserves each complete silhouette.
- Region ordering follows `content.cpp`: Crown Sea, Sunward Isles, Meridian,
  Monsoon Sea, Lantern Reach, Great West. `x4_presentation.h` provides the
  corresponding place descriptions, project purposes and local story copy.
- The original `assets/source/world-map.png` supplies the established land/sea
  geometry. The builder extracts a simple coastline, retaining the existing
  map's coordinates and east-west wrap. It does not introduce new geography.
- Landmarks, goods, facilities, captains and regional seals are extracted from
  those generated sheets, with no procedural illustration fallback. Facility
  levels use the same generated base with the authorized progression overlays:
  no pennant at level 1, one at level 2 and two at level 3.
- The approved eight incident compositions retain their original code-native
  geometry and generated sloop miniature. All five ships and eight incidents
  are byte-identical to their approved packed versions before the sheet pass.

## Build and inspect

Run `python3 tools/assets/build_generated_art_x4.py` to rebuild the X4-only
header and previews. The build requires every native regional and ship source and fails
if one is missing. It never falls back to converting the earlier color harbor
or title. `--glyphs-only` regenerates the small-art contact sheet without
rewriting the runtime header.

The native line art is fitted with a box filter and converted with fixed
thresholds (168 normally, 190 for tiny goods and 200 for title linework).
Sheet extraction finds white gutters near the intended cell boundaries and
trims exterior white paper, keeping complete subjects. There is no ordered or
error-diffusion dithering. Every runtime
pixel is black or white. The world chart uses only a coastline outline;
gameplay overlays remain code-rendered.

Runtime images use LovyanGFX's packed bitmap convention: row padding to whole
bytes, most significant bit first, one for black ink. `art_at` prints ink only
on a cleared card. Over harbor scenery, use `art_masked_at` with an exterior
mask so enclosed white details remain solid. The opaque `bitmap_at` overload
is for complete backgrounds or intentional white insets. `X4Art` supplies
`data`, `width` and `height` for each image.

| Runtime family | Count | Size |
| --- | ---: | ---: |
| Title | 1 | 800 x 480 |
| World coastline | 1 | 560 x 280 |
| Regional harbors | 6 | 560 x 280 |
| Port landmarks | 24 | 64 x 56 |
| Commodity symbols | 8 | 24 x 24 |
| Facility families, levels 1-3 | 9 | 48 x 40 |
| Ship classes | 5 | 112 x 88 |
| Captain portraits | 16 | 48 x 56 |
| Incident symbols | 8 | 128 x 64 |
| Regional stamps | 6 | 40 x 40 |

Landmarks also have `X4_LANDMARK_MASKS[24]`: border-connected white background
is transparent, while ink and enclosed white building details stay opaque.
Each 64x56 mask uses 448 bytes and preserves the original illustration pixels.
Facilities use the same convention in `X4_FACILITY_MASKS[3][3]`, with one
48x40 mask for each building and level; each mask uses 240 bytes.

The complete set occupies **232,528 bytes** of packed bitmap data, including
24 landmark masks (10,752 bytes) and nine facility masks (2,160 bytes). The builder
and emitted header enforce a 320 KiB ceiling. Pointer tables are immutable;
masked building draws use one fixed 896-byte stack buffer and no heap allocation.

Previews and the packed-data hashes are in `docs/generated/x4`; the contact
sheet is `build/art-previews/glyph-sheet.png`, and `build/art-previews/build-manifest.json` records every asset's
dimensions, byte length, source, SHA-256, extraction crop and thresholds. The former single-harbor symbol
and preview remain aliases for the Crown Sea scene, so existing gallery
tooling can still read them.

## Content and state

Regional project purposes describe the existing qualifying charter cargo:
ironware, ironware, tea, timber and ironware in unlock order. They explicitly
say deliveries occur between open ports. Place descriptions introduce no new
economy or destination restrictions. Captain portrait selection hashes the
immutable name and trait so recruitment previews and hired captains match.
Incident variants consume no simulation randomness. All identities and copy
are presentation-only and require no save migration.
