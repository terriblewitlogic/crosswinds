# Art

Every picture in the game is 1-bit black and white, packed into flash by scripts. Three headers
are generated and checked in; edit the sources and rebuild rather than editing them by hand.

| Header | Contents | Built by |
| --- | --- | --- |
| `img_generated_x4.h` | title, harbors, port landmarks, goods, facilities, ship pictures, captains, incidents, regional stamps and the world chart | `tools/assets/build_generated_art_x4.py` |
| `img_ship_classes.h` | five compact ship class sprites | `tools/assets/build_ship_sprites.py` |
| `x4_routes.h` | the sailing lanes, drawn offshore along the chart's coastline | `tools/assets/build_x4_routes.py` |

## Where the art came from

The illustrations were generated with OpenAI's image tool, then converted to black and white with
fixed thresholds and no dithering. The original images are in `assets/source`: `x4/` holds the
harbors, ships, landmarks, goods, captains and seals, `crosswinds/` holds the title, encounter
and region sheets, and `harbor.png`, `title.png`, `ship-class-sheet.png` and `world-map.png` sit
beside them. The prompts and original output paths are recorded in the JSON files in `assets/source/x4`.
Labels, prices and selections are drawn by the renderer, never baked into the pictures.

## Rebuilding

```sh
make art          # ship sprites, then the packed X4 art
make x4-routes    # sailing lanes, then an independent check of them against the chart
```

They need Python 3 with Pillow and NumPy, and SciPy for the routes. Regenerating from the files in
this repository reproduces all three headers byte for byte. Previews and manifests are written to
`build/art-previews`, which git ignores. `x4-routes` reads a preview that `make art` writes, and
`assets/source/x4/routes-baseline.json` holds the original, simpler lane paths; the builder uses it to
report how many of them crossed land.

## Format

Images use LovyanGFX's packed bitmap layout: rows padded to whole bytes, most significant bit first,
and a set bit is black ink. `X4Art` tables in `img_generated_x4.h` point at the bitmaps in flash.
Port landmarks and facilities also have masks, so the paper around a building is transparent while
enclosed white roofs and walls stay solid.

The builder checks the packed art against a 416 KiB budget and fails if it is exceeded. The
current pack is 222 assets and 405,665 bytes. `assets/source/x4/README.md` has the asset API and
more provenance detail.
