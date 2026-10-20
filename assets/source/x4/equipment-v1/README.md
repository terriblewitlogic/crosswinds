# Parcel and fittings

Eight illustrations generated with the built-in image-generation tool. `prompts.json` records the exact per-subject prompts; `originals.json` records returned file paths; all selected originals are copied as `*-source.png`.

The generator returned larger drawings despite requests for 32–40-pixel subjects. Each received one BOX reduction from its original grayscale crop, then thresholding at 168 and white padding. `manifest.json` records exact source dimensions, crop, fitted dimensions and hashes. Parcel: 24 × 24, matching goods. Fittings: 32 × 32. Runtime parcel sizes are integer enlargements at 48 and 72 pixels. Fittings render at native size.

Rebuild with `python3 tools/assets/build_equipment_x4.py`, then `python3 tools/assets/build_generated_art_x4.py`. Added packed artwork: 968 bytes. All earlier artwork is retained.

Review: `docs/generated/x4/tiny-sprite-examples/equipment/index.html`.
