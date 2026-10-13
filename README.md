# PexOfDuty

PexCraft but its a shooting game: a native C desktop shooter adaptation with a
Blockbench MAC-10, Nuketown map, zombies, and armed AI opponents.

## Shooter features

- First-person MAC-10 model with ADS, firing, reload and inspect animations.
- Select the MAC-10 or M16 from the inventory. Selections made in the first
  20 seconds after spawning equip immediately; later selections are queued for
  the next respawn.
- The MAC-10 fires faster with lower per-shot damage; the M16 fires more slowly
  with higher damage and a 30-round magazine.
- LAN deaths respawn after five seconds. Human kills can be skipped; bot kills
  keep the full timer. The death view replays recorded actor positions and the
  killer's pre-death POV, weapon and ADS state; terrain remains live.
- The LAN host can turn bots off in the lobby for player-versus-player matches.
- Players and bots drop a floating, labeled gun square on death. Get within
  3.5 blocks and shoot it to equip the gun immediately; walking over it alone
  does not collect it. Drops expire after 90 seconds and are shared over LAN.
- Each drop has a 50% chance to become **MAC-10 - Atomic Star**, using
  `mac10_legend.bbmodel`. It deals twice normal MAC-10 damage and gives victims
  a pink hit overlay. It is pickup-only and is lost when its holder respawns;
  the selected ordinary loadout is retained.
- Mouse-speed-dependent weapon sway, movement bob, and per-bullet shot audio.
- Nuketown template for newly created local desktop worlds; existing saves are not replaced.
- Six configurable spawn points and armed humanoid bots that patrol, navigate
  the map, pursue the last seen player position, fire bursts, reload and respawn.
- Daytime zombie encounters without sunlight ignition.

Controls: left mouse shoots, right mouse aims, **R** reloads, **I** inspects.
Player movement uses the ordinary PexCraft control bindings.

### Windows LAN team matches

1. Host a local Nuketown world, pause, and choose **Open to LAN**.
2. Other players open **Multiplayer** and select the discovered room on the same LAN.
3. The lobby lists **Red** and **Blue** teams. Anyone can choose a team, with a
   maximum of three humans per team. Empty slots show bots that will join on start.
4. Only the host can choose **Start match**. A five-second countdown appears in
   the lobby before gameplay unlocks. Team changes lock when the countdown starts.
5. Bots fill each team to three members, target opponents, and cannot damage their
   own teammates. Disconnecting humans are replaced by bots during the match.
   Bot models use red or blue team tinting; humans retain their own skin colors.
   Red/blue nametags identify humans and bots only when they are in view and
   visible, never through a wall. Dead players have no nametag.

LAN broadcasts each human's own 64x32 or 64x64 skin, including skin changes and
skins for late joiners. Bots and players whose skin has not arrived use a separate
default texture, not your custom skin. Modern 64x64 skins use separate left-arm
and left-leg UVs and outer clothing layers (classic-width arms).
Bot routing uses longer cross-map A* searches, safe tactical destinations,
steadier repositioning, and earlier blocked-path recovery. Gunshots have reduced
gain for both your weapon and distant players/bots; the sound slider still applies.

Use the same updated build on every computer. Allow it on your **private network**
if Windows Firewall asks. LAN discovery uses UDP port 27566; the host's TCP game
port is shown in chat. **Direct LAN IP** accepts `IPv4:port` if discovery is blocked.
Joining is restricted to the lobby, not an already-started match. This is a custom
Windows PexOfDuty protocol, not a Minecraft server, and has no internet matchmaking.
Downloaded maps use a separate LAN cache, not personal single-player saves.
Headless tests cover networking/team logic; on-screen lobby layout and play feel
still require interactive verification on real LAN computers.

## Build and run on Windows

Use a 64-bit MinGW-compatible toolchain (the current build was tested with LLVM-MinGW),
with zlib, libogg, libvorbis and libvorbisfile headers/static libraries installed.
Set `MINGW64_BIN` to its `bin` folder and run `build_x64.bat`.
The result is `pexcraft_x64.exe`.

Launch from the project root, then create a new local world for Nuketown.
Keep `src/assets/bbench` (including `m16.bbmodel`) and `src/assets/maps/nuketown` available at runtime.
For a shareable build, place copies at `assets/bbench` and `assets/maps/nuketown`
beside the executable. These assets are **not embedded in the EXE**.
Base Minecraft textures and legacy sounds are obtained separately through the game.

Implementation, map-conversion limitations and testing details:
[BLOCK / BREACH native integration](docs/BLOCK_BREACH_NATIVE.md).
Native headless bot tests are in `tests/breach_bots_native_test.c`.
The Windows build and headless logic tests passed; interactive visuals and gameplay
still need testing.

## Credits and asset licensing

- **“MAC-10” by @wasabi** — <https://blockbenchworkshop.com/model/wasabi/mac-10>.
  The creator's page lists **CC BY-NC**: use/adaptation with credit for
  non-commercial projects. The model, its included textures and animations remain
  separate from the code's MIT license. This project integrates the model through
  a native loader and pose system.
- **CoD Nuketown by Mewok** — source map name *Nuketown 2012*; converted into
  PexCraft's native chunk format. Credit does not establish a map redistribution license.
- **PexCraft** — underlying engine and cross-platform client, copyright 2026 cafestock.ir.
- The shot audio was supplied for this project; its original author/license has not
  been established here.

See [CREDITS.md](CREDITS.md) for asset provenance and licensing boundaries.
The root [LICENSE](LICENSE) covers the project code, not all bundled third-party assets.

## Upstream PexCraft information

PexCraft is an experimental custom client written in C. It is trying to work like **Minecraft JE r1.2.5**, with a focus on stability, predictable behavior, and cross-platform support.

> [!IMPORTANT]
> **PexCraft is not affiliated with, supported by, or approved by Mojang AB, Minecraft, or Microsoft in any way.**  
> **PexCraft is not trying to replicate modern Minecraft.**  
> **The target is Minecraft Java Edition r1.2.5-style behavior, while keeping the project stable, portable, and usable on many platforms.**

## Current platform state

- PSP State = barely playable.
- Windows State = playable.
- Linux SDL2 State = playable.
- Android TV State = build target added; uses SDL2/OpenGL ES and TV remote/gamepad input.

## Renderer states

- Direct3D 9 = unusable.
- Direct3D 11 = working.
- OpenGL = working.
- SDL2-OpenGL = working.


## WebAssembly / standalone HTML

An offline single-player browser target is available through `main_wasm.c`. It uses the existing GLES2 compatibility renderer, embeds the verified Minecraft 1.2.5 textures/fonts and the WebAssembly module into one `dist/PexCraft-WASM.html`, stores saves in browser IndexedDB when available, and disables multiplayer.

Build with `./build_wasm.sh` or `make -f Makefile.wasm`. See `docs/WASM_BUILD.md` for resource verification, offline/network guarantees, Telegram-only CI delivery, and browser validation.

## Android TV

See `android_tv/README.md` for the APK build, signing, TV remote mapping, and Google TV channel integration.

## Responsive Save and Quit lifecycle

**Save and Quit to Title** now uses a live progress screen and a staged teardown.
Thread joins and final disk saving run on a below-normal-priority coordinator while
the main thread continues pumping the window and rendering progress. Renderer-owned
textures and world meshes are then released safely on the render thread in bounded
per-frame batches before title music starts.

See `docs/save_and_quit_progress_teardown.md` for the shutdown order, deadlock
protections, and validation notes. The earlier synchronous implementation is retained
only as historical context in `docs/save_and_quit_full_world_teardown.md`.

### Mesh-worker shutdown safety

The quit coordinator now cooperatively cancels active section builds, drops queued
mesh jobs, polls each worker handle independently, and reports per-worker state in
the log. Direct3D 11 buffer creation is no longer performed on a background worker
while the progress screen is presenting; CPU mesh results are uploaded by the
render thread through the existing bounded install path.

See `docs/save_and_quit_mesh_worker_fix.md` for the failure mechanism and changes.
## Linux/OpenGL mesh shutdown correction

This revision fixes the SDL pthread compatibility layer used by Linux/OpenGL.
Zero-time thread waits can now detect completed pthreads, and finite event waits
actually time out. This prevents **Cancelling mesh builders** from waiting forever.
See `docs/linux_opengl_mesh_shutdown_fix.md`.


## Gamepad and TV-remote UI controls

Menus now use spatial D-pad/left-stick focus, so two-column screens navigate by
position rather than button creation order. A/OK activates, B/Back returns, and
sliders use Left/Right (LB/RB for larger steps). Language and texture-pack lists,
Statistics, Achievements, Creative inventory, multiplayer lists, and all text-entry
screens have dedicated couch-friendly controls and on-screen hints. Controller-focused
text fields open the built-in virtual keyboard on every controller platform.

The Xbox UWP target polls physical controllers through `Windows.Gaming.Input` and
keeps CoreWindow Gamepad virtual keys as a media-remote fallback when no physical
controller is connected.

## Build commit information

The supported build scripts run `tools/generate_build_info.py` before compiling.
It reads the checked-out Git commit subject, full SHA, and `origin` repository URL,
then force-includes an ASCII-safe generated header. When Git metadata is unavailable,
the embedded name, SHA, repository, and commit URL remain empty.

On the title screen, click `PexCraft 1.2.5` to show or hide the embedded commit
subject and SHA. Clicking either visible line opens the exact commit URL in the
platform browser when that platform supports external URLs.

## Android / Android TV low-end renderer path

Android terrain sections are uploaded to retained GLES vertex/index buffers when a
worker result is installed. Visible frames only bind and draw those buffers; they no
longer rescan section indices or stream the complete terrain mesh every frame. The
GLES compatibility layer also batches immediate-mode GUI, text, and entity geometry
into one retained dynamic ring buffer and caches unchanged pipeline state.

Terrain lighting uses the packed block/sky-light value already stored in each vertex
and a 16 by 16 shader lightmap, so daylight and gamma changes update one tiny texture
instead of copying and reuploading every visible section. Opaque, alpha-tested, and
two-sided section groups are submitted separately, allowing ordinary opaque terrain
to use early depth rejection and back-face culling without changing block models or
textures.

The Android active world window is 320 blocks wide. This preserves the normal
8-chunk view distance plus its streaming border while reducing the five dense world
arrays from about 405 MiB to about 125 MiB. Mesh worker counts and queues scale down
on low-core shared-memory devices.

## Android GLES title-screen regression repair (v10)

The Android and Android TV compatibility renderer now keeps menu, text, GUI,
entities, and other compatibility geometry on a lightweight one-texture GLES2
shader.  The terrain lightmap shader is used only while drawing retained terrain
meshes.  This avoids the severe title-screen slowdown seen on some low-end
GLES2 drivers after the v9 all-purpose shader change.

The renderer also no longer allocates/orphans a fixed 2 MiB streaming VBO at the
start of every frame.  Deferred compatibility batches perform one exact-size
stream upload when flushed, while retained terrain meshes continue to stay in
persistent GPU buffers.  Android V-Sync now follows the same V-Sync and FPS-limit
condition as the desktop SDL path.

## Android frame-pacing, UI and renderer-resolution controls (v12)

Android mesh generation is deliberately throttled behind the render thread: low
priority workers yield while scanning section layers, queue depths are small, and
only a bounded number of snapshots/results are submitted or installed per frame.
Cancellation is checked throughout block, face and visibility loops, allowing Save
& Quit to abandon active builders promptly instead of remaining on **Cancelling mesh
builders**.

Inventory and Creative screens group item icons by atlas and defer durability/count
text until after icon drawing. Container screens may reuse the low-resolution world
backdrop for a short interval, preventing the complete world from being redrawn for
every UI frame while preserving the normal native-resolution interface.

Android title panoramas are rendered into a 256x256 target at 30 Hz, blurred with a
separable five-tap filter, then aspect-cropped and scaled to fill the full display.
The panorama is never stretched and the menu remains native-resolution.

Video Settings now includes **Renderer Resolution** on Android and Android TV. It is
saved as a percentage in ten-percent increments from 10 to 100. The maximum is shown
as **Native** and the minimum as **VHS**. PexCraft queries the actual GLES drawable,
calculates both dimensions from the same percentage, renders the 3D world to that
size, and linearly scales it to fill the screen. GUI and text stay at native
resolution. Because only the percentage is stored, moving the same options file
between 720p, 1080p and 4K displays remains valid.

## Android TV remote, Java chunk retention and death-input repair (v13)

Android TV remotes are handled as ordinary D-pad/keyboard navigation rather than
being appended to the SDL gamepad list. Remote input no longer switches prompts,
virtual cursors, chat entry, or inventory layout into console mode. SDL devices
reported as remotes, CEC adapters, virtual devices, or `Generic Gamepad` are ignored
on Android TV, while real Xbox/XInput controllers remain on the controller path.
Mouse, stylus, touchscreen, gamepad, and joystick KeyEvents are excluded from the TV
remote JNI bridge so a mouse primary click cannot also become OK/jump.

Protocol-47 chunk snapshots that arrive outside the current 320-block Android ring
are retained until the multiplayer window reaches them. Visible snapshots are
processed first, newer complete snapshots replace every stale queued copy for the
same coordinate, and queue pressure can no longer silently discard a server chunk
and leave a permanent hole.

Entering the death screen now releases attack/use state and blocks mouse, trigger,
or remote attack controls until the physical buttons have been released once. This
prevents held or synthetic input from resuming as repeated interaction packets after
a server respawn.

## WASM validation and regular Android world-entry repair (v14)

The WASM structural validator now enforces the current Xbox/XInput-only D-pad
shortcuts instead of requiring the removed generic-controller expressions. Browser
builds therefore keep the remote-safe input behavior while passing the pre-build
check.

The regular Android GLES renderer is synchronized with the Android TV retained
terrain API. It now provides ranged terrain-mesh uploads, the seven-argument terrain
pass setup, split opaque/cutout/two-sided layers, and vertex-shader Java lighting.
This fixes the Android universal build errors and removes the stale phone-only
renderer path that failed when a world attempted to install or draw section meshes.

## Android mesh-shutdown completion repair (v15)

Android and Android TV now track pthread completion separately from pthread joining
inside their Win32 compatibility handles. A zero-time `WaitForSingleObject` poll can
therefore observe a worker that has exited, join it exactly once, and let Save & Quit
advance past `Cancelling mesh builders`. Previously the Android shim only reported a
thread as complete after it had already been joined, while the mesh coordinator waited
for that completion report before joining, creating a permanent shutdown loop even
when every mesh worker had finished.

## Android GLES allocation and world-entry crash safety (v16)

Android terrain uploads are now treated as fallible operations. Every VBO/IBO
allocation is checked with `glGetError`, partially-created batches are destroyed,
and the old section remains usable until a complete replacement has uploaded.
A conservative shared-memory and buffer-object budget evicts the farthest section
meshes before new allocations, so vendor GLES drivers are not pushed into native
out-of-memory crashes. Failed uploads are retried later instead of falling back to
unbounded per-frame streaming buffers.

Scaled world framebuffers also validate the device texture/renderbuffer limits and
fall back to direct native rendering for a cooldown period when an attachment cannot
be allocated. Regular Android and Android TV use the same guarded GLES path.

Protocol-47 client settings now advertise the selected render distance instead of a
fixed eight chunks. Pending chunk snapshots have soft/hard native-heap limits; under
pressure PexCraft commits an in-window snapshot immediately rather than discarding
server data, preventing both join-time memory bursts and permanent missing chunks.
Android phone meshing uses one worker and compact queues to avoid simultaneous large
native allocations on memory-constrained devices.

## Android render-distance crash safety

Android now separates the visible render radius from a protected chunk-load
radius. Low render distances still keep a hidden neighbor ring for section mesh
snapshots, cross-chunk lighting, collision queries, and multiplayer chunk
handoff, while those chunks remain culled from view. The Android 20x20 active
chunk ring exposes at most seven visible chunks in each direction and keeps the
outer storage slots as safety data.

World-window remapping no longer places complete mesh/cache tables on Android
thread stacks. The remap scratch is persistent storage, avoiding native stack
overflow on devices with smaller pthread/main-thread stack limits.
