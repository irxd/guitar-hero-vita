# Changes from Guitar Dream Engine

Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine
Forked from: https://github.com/mbracali/prj-gd-engine at `929477cafaa3675a309acf48c9e573cc563d906b` (2026-11-09), on 2026-11-10

This repository does not carry a copy of the upstream sources. It holds a
PS Vita build of `gd-game-01` made of:

- **patches** to upstream files (`patches/`), applied to the pinned commit by
  `tools/apply-patches.sh`. Each changed file is marked at its top with
  `// Modified by <name> on <date>: <what was changed>`;
- **new files** (the Vita layer, build scripts, tools, tests), listed below.

All upstream changes are guarded by `#if defined(__vita__)`, so other
platforms build exactly as upstream.

## Changes to upstream files

| Date | Patch | Files | Change |
|---|---|---|---|
| 2026-11-10 | `0001-vita-enhanced-profile.patch` | `gd-game-01/engine/src/app.cpp` | PS Vita Enhanced Settings profile in `profileValues()`: fullscreen, 60 FPS, internal resolution 448, no depth of field (the desktop's 1080p post-processing filled the Vita's video memory) |
| 2026-11-10 | `0002-vita-no-menu-backdrop.patch` | `gd-game-01/engine/src/app.cpp` | PS Vita: `loadBackdrop()` loads no Enhanced Settings backdrop venue (a whole venue and band, about 17 MB of textures, kept for the session) |
| 2026-11-10 | `0003-vita-character-preview-cache.patch` | `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: the character preview cache holds 2 models instead of 6 (6 ran out of memory) |
| 2026-11-10 | `0004-vita-joypad-frets.patch` | `gd-game-01/engine/src/input/input.cpp` | PS Vita: joypad fret layout L, Square, Triangle, Circle, R (the PS2 table uses L2/R2, which the Vita lacks); in a song a menu helper button bound to a fret is only that fret (Circle no longer pauses) |
| 2026-11-10 | `0005-vita-free-venue-before-load.patch` | `gd-game-01/engine/src/app.cpp` | PS Vita: `startSong()` frees the old venue before building the next (after a retry both did not fit in video memory) |
| 2026-11-10 | `0006-vita-movie-worker.patch` | `gd-game-01/engine/src/app.cpp` | PS Vita: `.pss` movies are decoded and converted to RGBA on a worker thread (`vita/src/movie_worker.cpp`) instead of the main thread (the intro played at 6-9 FPS); the disc is still read on the main thread only, and the movie clock starts with the first picture |
| 2026-11-10 | `0007-vita-neon-mpeg2.patch` | `gd-game-01/engine/src/video/mpeg2.cpp` | PS Vita: the MPEG-2 decoder's IDCT, motion compensation, block output and RGBA conversion call NEON versions (`vita/src/mpeg2_neon.cpp`, bit-identical results, self-checked on first use with a fallback to upstream's code); while the movie player is behind, B pictures are skipped and keep their display index (the A9 decodes a 640x448 picture in ~45-55 ms, slower than real time) |
| 2026-11-10 | `0008-vita-frame-phases.patch` | `gd-game-01/engine/src/app.cpp` | PS Vita: frame phase marks (main loop, game logic, venue update and draw, track, present) for the timing report in gd01.log (`vita/src/vita_profile.h`); no effect on other platforms |
| 2026-11-10 | `0009-vita-venue-phases.patch` | `gd-game-01/engine/src/render/venue_renderer.cpp` | PS Vita: phase marks inside the venue update (script, guitarist, band, lights, crowd, camera) |
| 2026-11-10 | `0010-vita-no-track-fade-grab.patch` | `gd-game-01/engine/src/render/track_renderer.cpp` | PS Vita: no `track_fade` screen grab before the highway (vitaGL's copy waits for the GPU to finish the venue: 21-23 ms of every song frame); the highway's far end is not faded into the venue |
| 2026-11-11 | `0011-vita-index-skeleton.patch` | `gd-game-01/engine/src/scene/character.h`, `gd-game-01/engine/src/scene/character.cpp` | PS Vita: characters blend, pose, update world transforms and run their twist servos through bone arrays built once, instead of string-keyed maps searched and rebuilt every frame (2-9 ms per character on the A9); same arithmetic in the same order, the first 8 poses of each character are checked bit for bit against upstream's code, with a fallback to it |
| 2026-11-11 | `0012-vita-renderer-phases.patch` | `gd-game-01/engine/src/render/renderer.cpp` | PS Vita: `Renderer::drawMesh` timed as its own nested phase in the gd01.log report |
| 2026-11-11 | `0013-vita-scene-renderer-caches.patch` | `gd-game-01/engine/src/render/scene_renderer.h`, `gd-game-01/engine/src/render/scene_renderer.cpp` | PS Vita: the scene renderer caches what upstream recomputes with strings on every draw (the mesh + material key, child, bone and light name lookups) by the address of the name string, checked against its text on every hit; only found objects are cached |
| 2026-11-11 | `0014-vita-anim-parent-index.patch` | `gd-game-01/engine/src/scene/anim.h`, `gd-game-01/engine/src/scene/anim.cpp` | PS Vita: `AnimPlayer` resolves each node's parent index once when it is built; `updateTransforms` looked the parent up by name for every node on every call |
| 2026-11-11 | `0015-vita-frame-cap.patch` | `gd-game-01/engine/src/app.cpp`, `gd-game-01/engine/src/render/renderer.cpp`, `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: Max FPS offers 30 (the new Vita default, steady), 60 and Unlimited; `Renderer::setFrameCap` sets vitaGL's vblank interval (2, 1 or 0) instead of pacing with a timer (`vita/src/vita_display.h`) |
| 2026-11-11 | `0016-vita-view-culling.patch` | `gd-game-01/engine/src/render/scene_renderer.h`, `gd-game-01/engine/src/render/scene_renderer.cpp` | PS Vita: a non-skinned mesh whose bounding sphere (computed from its vertices, recomputed when a mesh animation moves them) is wholly outside the view is not drawn; its children still are. `GD01_VITA_CULL=0` turns it off |
| 2026-11-11 | `0017-vita-camera-seed.patch` | `gd-game-01/engine/src/game/venue_camera.cpp` | PS Vita: `GD01_VITA_SEED` seeds the venue camera's shot choice, for repeatable benchmark runs (unset: random as upstream) |
| 2026-11-11 | `0018-vita-crowd-lod.patch` | `gd-game-01/engine/src/render/crowd_renderer.h`, `gd-game-01/engine/src/render/crowd_renderer.cpp`, `gd-game-01/engine/src/render/venue_renderer.cpp` | PS Vita: at Max FPS 60 the crowd is neither animated nor drawn; at 30 the six crowd impostors are re-rendered half per frame (each at 15 Hz) |
| 2026-11-11 | `0009-vita-venue-phases.patch` (updated) | `gd-game-01/engine/src/render/venue_renderer.cpp` | PS Vita: the venue draw is split into crowd impostors / stage / band / crowd / rest phases |
| 2026-11-11 | `0018-vita-crowd-lod.patch` (updated) | `gd-game-01/engine/src/render/crowd_renderer.cpp` | PS Vita: at Max FPS 30 the 3D crowd members are animated half per frame too (each at 15 Hz), the archetypes on their impostor's frame |
| 2026-11-11 | `0019-vita-merged-characters.patch` | `gd-game-01/engine/src/render/renderer.h`, `gd-game-01/engine/src/render/renderer.cpp`, `gd-game-01/engine/src/render/scene_renderer.h`, `gd-game-01/engine/src/render/scene_renderer.cpp`, `gd-game-01/engine/src/scene/character.cpp` | PS Vita: `lightingFor` reuses its result for consecutive draws with the same material and environ within one draw call and no longer allocates per draw. Experimental, off unless `GD01_VITA_MERGE=1`: a bone palette in `Renderer::drawMesh` and merged meshes for a character's consecutive skinned pieces (`Character::draw` brackets its draws with `vitaBeginBatch` / `vitaEndBatch`) |
| 2026-11-11 | `0015-vita-frame-cap.patch` (updated) | `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: the Max FPS help says 60 leaves out the crowd |
| 2026-11-11 | `0015-vita-frame-cap.patch` (updated) | `gd-game-01/engine/src/app.cpp`, `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: Max FPS is shown as Performance Mode: 30 FPS, 60 FPS (No Crowd) or Notes Only (`max_fps` "notes": the song loads no venue and a loaded one is not drawn, at 60). New setting Crowd: Full / Reduced (`crowd`, default reduced) |
| 2026-11-11 | `0018-vita-crowd-lod.patch` (updated) | `gd-game-01/engine/src/render/crowd_renderer.h`, `gd-game-01/engine/src/render/crowd_renderer.cpp` | PS Vita: `CrowdRenderer::setVitaMemberCap`: Crowd Reduced puts at most 8 3D members on the region's spots nearest the camera (in front of it first); the other spots keep their flat sprites |
| 2026-11-11 | `0015-vita-frame-cap.patch`, `0018-vita-crowd-lod.patch` (updated) | `gd-game-01/engine/src/app.cpp`, `gd-game-01/engine/src/ui/select_panels.cpp`, `gd-game-01/engine/src/render/crowd_renderer.h`, `gd-game-01/engine/src/render/crowd_renderer.cpp`, `gd-game-01/engine/src/render/venue_renderer.cpp` | PS Vita: Crowd is a check (`crowd`, default on): on, at most 8 3D members at 30 and 60 FPS; off, no crowd at 30 and 60 (`CrowdRenderer::vitaShown`, no longer tied to the frame rate). The 60 FPS label drops "(No Crowd)" |
| 2026-11-11 | `0015-vita-frame-cap.patch` (updated) | `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: a shorter Crowd help text |
| 2026-11-11 | `0004-vita-joypad-frets.patch` (updated) | `gd-game-01/engine/src/input/input.cpp` | PS Vita: default frets D-pad Left (green), L (red), R (yellow), Circle (blue), Cross (orange); strum stays on D-pad Up / Down; in a song D-pad Left and Cross are only frets (no menu Left / Confirm) |
| 2026-11-11 | `0020-vita-async-song-preview.patch` | `gd-game-01/engine/src/app.cpp`, `gd-game-01/engine/src/data/vgs.h`, `gd-game-01/engine/src/data/vgs.cpp`, `gd-game-01/engine/src/data/iso9660.cpp` | PS Vita: `songPreview` hands the preview to a worker thread (`vita::PreviewLoader`) and `vitaPollPreview` starts it when built; a windowed `VgsStream` constructor reads only the preview's blocks (~2 MB, not the whole 18-36 MB file); `RandomFile::read` is locked so the worker can read the disc image |
| 2026-11-11 | `0021-vita-free-scene-resources.patch` | `gd-game-01/engine/src/render/renderer.h`, `gd-game-01/engine/src/render/renderer.cpp`, `gd-game-01/engine/src/render/scene_renderer.h`, `gd-game-01/engine/src/render/scene_renderer.cpp` | PS Vita: `Renderer::vitaDeleteTexture` / `vitaDeleteMesh`; `~SceneRenderer` frees the textures and meshes it created (upstream frees GL resources only at shutdown: every venue's stayed in memory) |
| 2026-11-12 | `0004-vita-joypad-frets.patch` (updated) | `gd-game-01/engine/src/input/input.cpp` | PS Vita: outside a song a fret bound to a D-pad button is only its direction (D-pad Left no longer also selects in the menus, which take a green fret as select) |
| 2026-11-12 | `0022-vita-script-mod-wrap.patch` | `gd-game-01/engine/src/script/script.cpp` | PS Vita: the script `mod` wraps negatives into the range (`fmod` kept the sign: scrolling up from the first item of a store list gave index -1, shown as "_shop_desc" for $0; the tutorial list wraps the same way). An upstream bug on every platform, guarded for the Vita only |
| 2026-11-12 | `0023-vita-settings-menu.patch` | `gd-game-01/engine/src/app.cpp`, `gd-game-01/engine/src/ui/select_panels.cpp`, `gd-game-01/engine/src/input/input.cpp` | PS Vita: Enhanced Settings without the 33 settings and buttons the Vita cannot use or has no frame time for (Modes presets, resolution, backends, screen mode, upscaling, HDR, mouse, file export / load, anti-aliasing, ambient occlusion, sharpening, sharper sprites, and the post-effects and extra lights); those keep their Vita defaults whatever enhanced.json says. Shake for Whammy shown on the Vita, on its motion sensor (off by default) |
| 2026-11-12 | `0023-vita-settings-menu.patch` (updated) | `gd-game-01/engine/src/input/input.cpp` (and `vita/src/vita_settings.h`) | PS Vita: the No Venue cheat hidden (Performance Mode > Notes Only does the same and loads no stage); Shake for Whammy a little easier (3 m/s² past gravity, was 4) |
| 2026-11-12 | `0024-vita-button-layout.patch` | `gd-game-01/engine/src/app.cpp`, `gd-game-01/engine/src/ui/select_panels.cpp`, `gd-game-01/engine/src/input/input.cpp` | PS Vita: Enhanced Settings > Controls > Button Layout. Layout 1 (default): D-pad Left, L, R, Circle, Cross, strum on D-pad Up / Down; Layout 2: D-pad Left, D-pad Up, Triangle, Circle, Cross, no strum (a gamepad's frets strum). Inputs customized on the Gamepads screen still take precedence |
| 2026-11-12 | `0024-vita-button-layout.patch` (updated) | `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: Button Layout's help draws both layouts: each fret's colour, then its button as a drawn icon (D-pad arm, L / R tab, circle / cross / triangle outline), the chosen layout highlighted; Button Layout shown as a choice, not a check |
| 2026-11-12 | `0024-vita-button-layout.patch` (updated) | `gd-game-01/engine/src/input/input.cpp`, `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: Button Layout 2 strums (optionally) on L / R, only in a song (a strum is also the menus' Up / Down); its help shows the L / R icons |
| 2026-11-12 | `0024-vita-button-layout.patch` (updated) | `gd-game-01/engine/src/ui/select_panels.cpp` | PS Vita: Button Layout's help text shortened (the drawn layouts show the buttons) |
| 2026-11-11 | `0020-vita-async-song-preview.patch` (updated) | `gd-game-01/engine/src/data/iso9660.h`, `gd-game-01/engine/src/data/iso9660.cpp`, `gd-game-01/engine/src/data/vgs.cpp` | PS Vita: `RandomFile::read` uses `sceIoPread` (positional, 4 MB pieces) instead of fseek + fread: ~3x faster disc reads, safe from the preview thread without a lock |

## New files

| Date | Files | What |
|---|---|---|
| 2026-11-10 | `vita/CMakeLists.txt` | VitaSDK build of the upstream gd-game-01 sources with a reduced gd-engine-core (OpenGL backend only, no audio backends, no Vulkan), packaged as a VPK |
| 2026-11-10 | `vita/src/vita_platform.cpp` | Vita platform layer, reached through linker `--wrap` of SDL calls: vitaGL bring-up, stand-ins for 5 GL functions vitaGL lacks, data paths in `ux0:data/gd01/`, logging, memory and frame statistics, shader hooks, `gl_VertexID` emulation, paletted texture uploads |
| 2026-11-10 | `vita/src/glsl_es100.cpp`, `vita/src/glsl_es100.h` | GLSL ES 3.00 → 1.00 rewrite of gd01's shaders for vitaGL's translator |
| 2026-11-10 | `vita/src/texture_palette.cpp`, `vita/src/texture_palette.h` | Lossless RGBA8 → 8-bit paletted texture packing (textures of at most 256 colours) |
| 2026-11-10 | `vita/src/movie_worker.cpp`, `vita/src/movie_worker.h` | Movie decoding thread used by patch 0006 (pinned to core 1, skips B pictures and late conversions while behind, logs decode statistics) |
| 2026-11-10 | `vita/src/mpeg2_neon.cpp`, `vita/src/mpeg2_neon.h` | NEON versions of the MPEG-2 decoder's inner loops used by patch 0007, with self-tests and timings against scalar copies |
| 2026-11-10 | `vita/src/vita_profile.h` | Frame phase timing API used by patches 0008 and 0009 (implemented in `vita_platform.cpp`: per phase ms, draw calls, time inside draw calls and framebuffer readbacks) |
| 2026-11-11 | `vita/src/vita_display.h` | Display pacing API used by patch 0015 (implemented in `vita_platform.cpp` with vitaGL's `eglSwapInterval`) |
| 2026-11-11 | `tools/build-vitagl.sh` | Builds vitaGL from source with the port's build options (linked by `tools/build-vita.sh` when present) |
| 2026-11-11 | `vita/src/preview_loader.h`, `vita/src/preview_loader.cpp` | Song previews built on a worker thread, used by patch 0020 |
| 2026-11-12 | `vita/src/vita_settings.h` | The Enhanced Settings the Vita does not show, used by patch 0023 |
| 2026-11-12 | `vita/src/vita_settings.h` (updated) | Declares `vita::setButtonLayout`, used by patch 0024 |
| 2026-11-10 | `vita/compat/dlfcn.h` | Stub dynamic-loader header (the Vita has none) |
| 2026-11-10 | `vita/tests/` | Desktop checks for the shader rewrite and the palette packing |
| 2026-11-10 | `tools/` | Toolchain setup, desktop and Vita builds, patch stack, shader dump and validation, GPU memory probe, crash dump decoding, movie decode benchmark, hardware build/deploy/run loop (`vita-loop.sh`) |
| 2026-11-10 | `README.md`, `CLAUDE.md`, `docs/port-plan.md` | Project documentation |

## Vita-only differences in behaviour

- No Vulkan upscalers (FSR / DLSS), no HDR, no MIDI input, no launcher.
- Enhanced Settings defaults as in patch 0001; the Enhanced Settings screen has no venue backdrop.
- Button play on the Vita's pad instead of a guitar; touch controls off by default.
- Textures of at most 256 colours are stored paletted and without mip levels (distant surfaces may shimmer slightly).
- Shaders are rewritten to GLSL ES 1.00; the auto-exposure pass reads its 1x1 level with a texture bias.
- Songs: the far end of the highway is not faded into the venue (patch 0010).
- Movies: when the Vita decodes slower than real time, B pictures are skipped, so the video shows fewer pictures per second but stays in sync with its sound.
