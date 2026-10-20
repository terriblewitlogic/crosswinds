# Complete illustrated asset pass

Generated with the built-in image-generation tool on September 7, 2026. Six sheets cover 35 remaining designs: six goods, 23 port landmarks, five region seals and one shipyard. The other six approved designs are retained from v7.

`catalog.json` lists all 41 identities and native slots. `prompts.json` records the exact generation prompts and mode; `generation-results.json` links returned originals to the six checked-in `*-generated.png` sources. Layout guides and source references preserve the identity of each subject. Ships, captains and incident coins were outside this pass.

The prompts requested drawings close to the game size, in the established maritime illustration style. Generated ink was still larger than its target. Each new drawing received one BOX reduction from its grayscale crop, followed by monochrome conversion and padding; none were enlarged during preparation. This is not a zero-resize result.

Rebuild:

```sh
python3 tools/assets/build_full_illustrations_x4.py
python3 tools/assets/build_generated_art_x4.py
```

`crop-overrides.json` records threshold adjustments for five darker landmarks. Prepared pixels and the exact crop, size, threshold and source hash for every design are in `../small-illustrations-v8/manifest.json`. The complete comparison is `docs/generated/x4/tiny-sprite-examples/full/index.html`.
