# BLOCK / BREACH native PexCraft integration

This is a source-code modification of PexCraft using `src/libbbench` to load the actual Blockbench MAC-10 model and evaluate its animations. No new HUD elements were added.

## Implemented in the desktop game source

- Every **newly created desktop local world** starts from the bundled Nuketown map template. Existing saves are not replaced.
- Local worlds spawn existing native PexCraft zombies near the player, stay at noon, and don't keep vanilla animals. Breach zombies do not ignite in sunlight.
- First-person handheld item/arm rendering is replaced by the 3D MAC-10. Its arm texture uses `pex_java47_local_skin_texture()` (the player's current skin; fallback to Steve).
- Right mouse: smooth ADS with animated pose and world/weapon FOV changes.
- Left mouse: automatic native hitscan shooting, with hip-fire spread larger than ADS, and native zombie damage/death.
- R: reload, using reload/reload_empty clips; I: hold inspect (holds last frame until released).
- Walking bob, gun recoil, and Shift+Ctrl slide impulse/camera weapon movement are included.
- Vanilla block breaking and item using on left/right mouse are blocked in local desktop shooter worlds.
- No extra ammunition/health/score HUD, crosshair, or indicators were introduced. Existing PexCraft UI remains as-is.
- Multiplayer and non-desktop target builds retain PexCraft's original hand/input code (they don't yet have the gun renderer).

## Build and run

From the extracted `PexCraft-main` directory:

**Linux**: install SDL2, SDL2_image, OpenGL/GLU, zlib development headers, then run:

```bash
make -f Makefile.linux_sdl2
./pexcraft_sdl2
```

**Windows**: use PexCraft's existing Windows build scripts/toolchain, e.g. `build_x64.bat`, without changing the resulting binary name. The source integration is inside the Windows unity entry `src/main.c`.

Keep `src/assets/bbench/mac-10.bbmodel` and its texture backups available at runtime, and launch the program with the project root as your working directory. For packaged distribution, copy `src/assets/bbench` to `assets/bbench` beside the executable.

## Default Nuketown map

Map credit: **CoD Nuketown by Mewok** (source level name: Nuketown 2012).

- Run `pexcraft_x64_nuketown.exe` from the project root and create a new local world. The new slot receives its own copy of the map, so ordinary saving does not alter the bundled template or the original Minecraft world.
- The template is `src/assets/maps/nuketown`: 625 native compressed chunk files covering 400 x 400 blocks, with Y=0..255. The spawn is centered near the original spawn; the marker stores PexCraft eye coordinates.
- Keep this folder at runtime. For distribution, copy it to `assets/maps/nuketown` beside the executable (including `chunks.txt` and `breach_map.txt`). Missing templates cancel new-world creation rather than silently generating a flat world.
- Minecraft 1.18.2 materials not present in PexCraft are approximated. Concrete/terracotta use colored wool, quartz uses legacy white/stone materials, and carpets use thin snow layers. Barrier/command blocks are removed. Original commands, entities, chest inventories and sign text are not imported.
- Full counts and substitutions: `src/assets/maps/nuketown/conversion_report.json`.
- Reproduce the conversion with `python tools/convert_nuketown.py "SOURCE_WORLD_FOLDER" --output "NEW_TEMPLATE_FOLDER"`. The converter only reads the source and refuses to overwrite an existing output.

The Windows build and an isolated test of the actual native chunk reader and template installer passed for all 625 chunks. Interactive in-game map rendering has not been verified.

### Spawn points and armed opponents

- `breach_spawns.txt` contains six player/bot spawn points: `-35 65 -90`, `-35 65 -93`, `-35 65 -98`, `48 65 -85`, `48 65 -89`, `48 65 -93`. Y is feet height; PexCraft adds its eye-height offset internally. The optional fourth column is facing yaw.
- New worlds and player respawns select a point away from existing enemies. Existing Nuketown saves without the file fall back to the bundled spawn list.
- Three armed humanoid opponents patrol and use native A* navigation/collision, chase the last seen player position, reposition at close range, and shoot three-round bursts after a short reaction delay. Shots have spread, respect solid walls, and cause ordinary player damage. Bots use 24-round magazines and a two-second reload, respawning in free spawn slots away from the player.
- Bots have visible compact guns and muzzle flashes and use the supplied one-shot audio with distance attenuation. Native zombies remain alongside them.
- Bot state is runtime-only and excluded from saves, so loading a world does not turn bots into normal zombies. Human models reuse PexCraft's Steve skin and biped animation; they are not a full CoD character/weapon animation system.
- `tests/breach_bots_native_test.c` includes the real Windows unity build for headless tests of spawn collision, map navigation/physics, bursts/reloading/damage, wall-blocked shots, player kills, spawning and respawning. Interactive visuals/gameplay still need testing.

## Testing status and limits

The standalone `libbbench` C99 loader compiles cleanly using `-Wall -Wextra -Werror -pedantic` and successfully parses the included model: 35 cubes, 14 bones, 4 textures, 10 animation clips, 134 tracks, 545 keyframes. **A full PexCraft SDL2/Windows graphical build was not possible in the creation environment** because the SDL2 and SDL2_image development headers are absent. The native game integration therefore requires build and in-game verification on your machine. The slide and movement adaptation is intentionally lightweight and does not yet reproduce every subtlety of the former HTML version (such as its original audio mix and full monster-wave rules).

This does not change old saved world's terrain on disk; only the creation flow and local shooter behavior are changed. Back up worlds before testing. Use the original project for ordinary Minecraft gameplay.
