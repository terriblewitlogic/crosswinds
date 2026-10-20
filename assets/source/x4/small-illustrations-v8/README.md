# Prepared game-size illustrations

41 base designs: 8 goods (24 × 24), 24 landmarks (64 × 56), 3 facilities (48 × 32) and 6 region seals (40 × 40). Facility packing adds an eight-pixel progression flag strip above the building.

35 drawings come from the six built-in image-generation sheets in `../full-illustrations-v8/`. Each was reduced once from its original grayscale crop, then thresholded at 168 (155 for five darker landmarks). Six approved v7 designs retain their exact prepared pixels. `manifest.json` records all operations and hashes.

`tools/assets/build_generated_art_x4.py` packs these exact native pixels, without another resize, and regenerates facility flags and harbor transparency masks. Existing ship, captain and incident assets remain unchanged.
