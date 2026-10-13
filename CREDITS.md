# Credits and third-party assets

## MAC-10 model

**“MAC-10” by @wasabi** — <https://blockbenchworkshop.com/model/wasabi/mac-10>

Source asset: `src/assets/bbench/mac-10.bbmodel`, with texture backups
`tex0.png` through `tex3.png`. The model includes its textures and animation data.
PexOfDuty loads it through `src/libbbench` and adapts its poses to the native
first-person renderer, ADS and shooting behavior.

The creator's Blockbench Workshop page lists **CC BY-NC** and says:
“Use and adapt it with credit in personal, non-commercial projects.”
No specific Creative Commons license version is asserted here because the
inspected page did not identify one. The model is not relicensed under MIT.
Commercial use requires appropriate permission from the rights holder.

## Nuketown map

**CoD Nuketown by Mewok**, source level name **Nuketown 2012**.

Converted terrain: `src/assets/maps/nuketown`.
Conversion script: `tools/convert_nuketown.py`.
The conversion recenters/crops the map and substitutes unsupported modern blocks.
The source Minecraft world is not changed. Source commands, entities, inventories
and sign text are not imported.

A source redistribution license has not been established in this project.
The map is not covered by the code's MIT license; obtain any permissions required
for your intended use or redistribution.

## Shot audio

`mac10_oneshot.ogg` and `mac10_rapidfire.ogg` were supplied for this project.
Original author and licensing information have not been established here.
They are not represented as MIT-licensed assets.

## Code and engine

PexCraft: copyright (c) 2026 cafestock.ir; see the root `LICENSE`.
`src/libbbench` identifies its portable Blockbench loader as MIT-licensed.
Preserve existing notices in upstream and third-party source files.

Minecraft/Mojang/Microsoft names and assets belong to their respective owners.
This project is not affiliated with or endorsed by them. Base Minecraft resources
are not included in the source publication as a downloaded resource cache.
