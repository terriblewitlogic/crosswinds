# Tiny sprite experiments — 7 September 2026

Generated using the built-in `image_gen` tool. These are art studies, not a production asset replacement.

The four dense atlases did not obey the requested cell sizes. A sparse layout guide followed by a targeted refinement produced six examples that fit without spatial resizing.

| Example | Generated ink | Existing slot |
| --- | --- | --- |
| Grain | 15 × 19 | 24 × 24 |
| Porcelain | 22 × 15 | 24 × 24 |
| Kingsfall clocktower | 21 × 45 | 64 × 56 |
| Merchant office | 41 × 32 | 48 × 32 base |
| Warehouse | 43 × 32 | 48 × 32 base |
| Meridian seal | 32 × 32 | 40 × 40 |

Facilities reserve a separate 8-pixel strip above the base for progression flags, as in the current game pipeline.

## Reproduce

Run `python3 tools/assets/build_tiny_sprite_examples.py` from the repository root. Outputs are in `docs/generated/x4/tiny-sprite-examples/`. Open its `index.html` for current/new comparisons, native and integer-zoom views, and a background transparency check.

`selections.json` identifies the selected sources and search rectangles. `manifest.json` in the output directory records exact crop coordinates, measured ink dimensions, output dimensions, and preparation operations. The builder asserts that all thresholded ink pixels survive extraction. None of the six exported sprites are spatially resampled. The 3× browser control is only for inspection.

The selected generated sources have white RGB backgrounds. Preparation thresholds at 168 and makes edge-connected white transparent, while preserving enclosed white details. Original crops and full generated sources are retained separately. The dense atlas originals retain their generated RGBA channels.

## Prompt history

- `prompts.json`: four dense atlas generation prompts.
- `layout-prompt.txt`: exact-size layout guide edit.
- `refinement-prompt.txt`: smaller goods and clocktower edit.

Native exports, the comparison image, all twelve gallery images, both controls, and browser error logs were checked. All six fit, no images failed to load, and no browser errors were reported. These studies have not been installed on the device.
