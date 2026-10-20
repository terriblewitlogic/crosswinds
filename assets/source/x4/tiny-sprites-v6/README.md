# Maritime illustration study — 7 September 2026

The user identified that the earlier tiny-sprite trial changed the art style, independently of size. This study uses the existing maritime illustrations as style and subject references rather than asking for generic symbols.

Generated with the built-in `image_gen` tool. No firmware or production sprite sheet was replaced.

## Result and limitation

The selected final pass retains the illustrated perspective, roof forms, windows, loading bay and porcelain decoration more closely. It does **not** meet the exact native-size goal: the generator enlarged all six requested footprints. These are unscaled studies, not drop-in assets.

| Drawing | Generated ink | Intended slot | Fits |
| --- | --- | --- | --- |
| Grain | 32 × 29 | 24 × 24 | No |
| Porcelain | 34 × 26 | 24 × 24 | No |
| Kingsfall | 47 × 70 | 64 × 56 | No |
| Office | 60 × 51 | 48 × 32 base | No |
| Warehouse | 65 × 40 | 48 × 32 base | No |
| Meridian | 44 × 44 | 40 × 40 | No |

The intermediate size-focused attempt fit Kingsfall and Meridian, but the drawings had heavier filled areas. That comparison and its measurements remain in `closest-size-comparison.png` and `closest-size-manifest.json` beside the final gallery. Its source selections are saved here as `closest-size-selections.json`.

## Deliverables

- Gallery: `docs/generated/x4/tiny-sprite-examples/v6/index.html`
- Native side-by-side image: `docs/generated/x4/tiny-sprite-examples/v6/comparison.png`
- Individual unscaled cutouts, original crops and exact pixel audit: same output directory.

Current art is displayed at its game size. New art is displayed at its actual generated size, so it is larger. The gallery clearly identifies this difference. Integer zoom is only for inspection.

## Prompt and source history

1. `prompt.txt`: tiny magenta layout guide plus original illustration reference board. Output `generated-original.png`.
2. `refinement-prompt.txt`: smaller footprints, same illustrated designs. Output `generated-refined.png`.
3. `final-size-prompt.txt`: four size corrections and thinner engraved contours. Output `generated-final.png`.
4. `native-reference-prompt.txt`: refine the actual existing game sprites in place. Output `generated-native-reference.png`; selected for the final style comparison.

The reference board uses original source illustrations, cropped and resized only for reference presentation. The selected exports are never spatially resized. Preparation is a crop, monochrome threshold at 168, edge-connected white transparency, and padding. Enclosed white details remain opaque. Original RGB source crops are retained.

The warehouse uses source cell 1, matching the warehouse illustration; the earlier v5 gallery accidentally compared its warehouse study against shipyard cell 2.

## Reproduce and verify

Run `python3 tools/assets/build_tiny_sprite_examples.py --study v6` from the repository root. The builder checks that extraction preserves every selected thresholded ink pixel and records size overruns rather than clipping them.

Browser inspection confirmed every displayed sprite is 1:1 with its exported pixels and all twelve comparison images loaded. The exports are not ready for the game's fixed-size slots.
