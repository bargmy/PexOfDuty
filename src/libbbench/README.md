# libbbench (C99)

`libbbench` is a small **runtime `.bbmodel` loader and animation evaluator**, bundled here for PexCraft. It has no GPU, SDL, or operating-system dependencies. The PexCraft integration is in `../game/breach_shooter.c`.

## Supported features

- JSON `.bbmodel` files exported from Blockbench (tested with `mac-10 (5).bbmodel`)
- Cube elements (including per-cube pivot, rotation, inflate, and six independent textured faces)
- Nested bone/outliner hierarchy and native pivots
- Embedded base64 PNG textures, decoded as PNG **bytes** for the host to upload
- Animation clips, position, Euler rotation, scale keyframes, clip length and loop/hold, linear and step interpolation, Catmull-Rom approximation
- Pose blending, clip lookup by full name or short suffix
- Limits on JSON input size, cubes, bones, clips, tracks, and keyframes

**Not supported:** arbitrary Blockbench polygon meshes, expressions/scripts, particle/effects timelines, texture animations, and exact irregular-time Catmull-Rom curves. This is not the full Blockbench application or the Minecraft Bedrock animation runtime.

## API

```c
#include "libbbench.h"
BBModel *model = calloc(1, sizeof(*model));
BBPose *pose = malloc(sizeof(*pose));
if (bb_load_file(model, "mac-10.bbmodel")) {
    int inspect = bb_find_clip(model, "inspect");
    bb_sample_pose(model, inspect, 1.2f, pose);
    /* draw cubes with your own renderer, applying pose transforms */
}
bb_free(model);
free(pose);
free(model);
```

`BBPose` holds per-node position offsets, rotation offsets in degrees, and scale. Coordinates in the loaded model use Blockbench pixel units. Divide by 16 to obtain Minecraft world units. PNG bytes in `BBTexture` are owned by the model and released by `bb_free`; they have **not** been decompressed into RGBA.

`bb_load_memory()` is available for filesystem-free targets. The loader itself can be compiled as a separate C99 translation unit; the desktop PexCraft builds include it in their existing unity translation unit.

### Standalone test

```sh
gcc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
  src/libbbench/libbbench.c src/libbbench/test_libbbench.c \
  -lm -o libbbench-test
./libbbench-test src/assets/bbench/mac-10.bbmodel
```

This test checks JSON/model/animation parsing without SDL or a graphics card. It is not an end-to-end graphical test.

## Using a different gun model

Replace `src/assets/bbench/mac-10.bbmodel` with a new Blockbench export. The native desktop game reads that `.bbmodel` on the next launch. It looks up the named animation suffixes `draw`, `ads`, `auto_fire`, `reload`, `reload_empty`, and `inspect`. If animations are renamed, update `../game/breach_shooter.c`.

The PexCraft desktop texture bridge uses its existing PNG decoder and temporarily writes embedded PNGs to the working directory for upload, then immediately deletes them. The `tex0.png`–`tex3.png` files in `src/assets/bbench/` are backups if the temporary write is blocked. If you change the textures, replace those backups too. Embedded `steve.png` in this gun is intentionally overridden by PexCraft's **selected player skin** at render time.

## Licensing

MIT; included as part of the MIT-licensed PexCraft fork. The original archive's `LICENSE` applies.
