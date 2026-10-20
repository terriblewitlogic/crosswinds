# Six prepared maritime illustrations

These are native game bitmaps prepared from the built-in image-generation
study in `../tiny-sprites-v6/`. The game-ready set is integrated by the six
`NATIVE_GLYPH_OVERRIDES` entries in `tools/assets/build_generated_art_x4.py`.

Generation did not satisfy the exact-size requests. Each selected grayscale
crop is reduced **once**, before thresholding at 168, then padded to its game
slot. This is not zero-resize generation. `manifest.json` records the source
hash, crop, input size, fitted size, output size and exact conversion ratios.
The main asset builder copies these prepared pixels without further resizing.

Reproduce from the repository root:

```
python3 tools/assets/build_small_illustrations_x4.py
python3 tools/assets/build_generated_art_x4.py
```

The office and warehouse files are 48 × 32 bases. The normal builder adds the
8-pixel strip and existing level pennants to make the 48 × 40 runtime variants.
It also regenerates the corresponding transparency masks.

Validation: X4 host and device compilation passed; 24 native port renders
matched their illustrations; all 33 retained harbor masks preserved ink and
transparent borders; 29 merchant UI checks passed. Exactly 17 packed assets
changed (the six designs and derived facility variants/masks); the other 100
assets and total 232,528-byte bitmap budget stayed unchanged.

The review is `docs/generated/x4/tiny-sprite-examples/ready/index.html`.
Device firmware is built but not installed: USB was absent during this pass.

The Watch departure capture still shows the existing boat/port overlap at 0%
progress. Comparison with the prior UI-revision capture confirmed this predates
the art update; changed pixels are confined to the Kingsfall landmark rectangle.
This pass does not change voyage placement or claim that overlap is resolved.
