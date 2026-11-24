# gd01 → PS Vita: port plan

Last updated: 2026-11-15

## Upstream pin

| | |
|---|---|
| Repo | https://github.com/mbracali/prj-gd-engine |
| Commit | `6e76ea60262b4164cb886f5ce446dd7bcd540b80` (2026-11-13, "Merge pull request #110 from mbracali/develop"); before 2026-11-14: `929477ca` |
| gd-game-01 version | 1.1.13 (release 1.1.15) |
| Upstream Vita study | `docs/spikes/study_ps_vita.md` (2026-10-28) |
| Upstream backlog | GDALL-024 "PS Vita (gd01 only, homebrew VPK)", not started |

Re-clone: `git clone https://github.com/mbracali/prj-gd-engine.git upstream && git -C upstream checkout <commit>`

## Audit of gd-game-01 (2026-11-10, at the pinned commit)

### Size and layout
- About 37.7k lines of C++ in `gd-game-01/engine/src` (app, ui, render, scene, game, audio, video, data, script, input, platform).
- Few external dependencies. The engine has its own ISO9660 reader, inflate, MPEG-2/PSS video decoder, ADPCM/SPU audio, MIDI and DTA parsers. Only SDL3 is required.
- **The ISO is streamed** (`data/iso9660.cpp` uses seek + `fread`), not loaded whole. Good for memory.

### Build (`engine/CMakeLists.txt`)
- C++20, CMake ≥ 3.24, `find_package(SDL3)`. **[verify]** that VitaSDK's GCC has enough C++20 support for this code.
- An **Android branch already exists** and is the model for a `VITA` branch: SDL3 built from source, `vk_upscaler.cpp` swapped for `vk_upscaler_stub.cpp`, `GD01_DLSS=0`, no RtMidi.
- Desktop-only parts to drop on Vita: RtMidi (`input/midi_in.cpp`), FidelityFX/volk/Vulkan/DLSS, `hdr_output.cpp`, `gpu_select.cpp` (likely), `app_icon.cpp`.
- **gd-engine-core is linked unconditionally** (`gdcore`) and pulls glslang, SPIRV-Tools, KTX, SDL3_image, Vulkan headers, volk and VMA, with SPIR-V compiled at build time (`gdcore_spirv_shaders`). The game runs its own "local" code unless the player picks a core backend. **On Vita: build without gdcore.** Exclude `core_renderer.cpp` / `core_post_fx.cpp` and stub the backend selection. This is likely the first upstream patch.

### Rendering (the main job)
- About 550 direct GL calls: `renderer.cpp` 366, `post_fx.cpp` 136, `vk_upscaler.cpp` 49 (dropped), `core_renderer.cpp` 2.
- GL functions are **loaded through pointers** in `render/gl.cpp` (`GD01_GL_FUNCTIONS` X-macro), which already has a `glShaderSource` hook that rewrites `#version 330 core` to `#version 300 es`. **This is the seam for Vita**: bind the pointers to vitaGL and add a GLSL 330 → vitaGL-compatible rewrite in the same hook.
- Shaders: 26 × `#version 330` and 2 × `#version 300 es`, written as inline strings. Features that need work for vitaGL (closer to GLSL ES 1.00 / GL 2.x):
  - `layout(location=…)` × 27: strip them and use `glBindAttribLocation` instead
  - `in`/`out` instead of `attribute`/`varying`, and `out vec4` instead of `gl_FragColor`: rewrite
  - `flat` × 6: GLES2 has no flat interpolation, so restructure
  - `textureLod` × 3, `texelFetch` × 1, `uint` × 2: replace or emulate
  - **[verify]** how far vitaGL's GLSL translator already handles 3.30 syntax by itself
- GL 3.x API calls that need checking against vitaGL: VAOs (`glGenVertexArrays` ×4), `glBlitFramebuffer`, `glMapBufferRange`, `glFenceSync`/`glClientWaitSync`, renderbuffers, `glReadPixels`, float render targets. **[verify]** each one against vitaGL.
- FBO use: `renderer.cpp` binds framebuffers 11 times, `post_fx.cpp` 2 times. Post-FX is off by default on Vita, and render straight to the backbuffer where possible.

### Platform
- About 15 `#ifdef __ANDROID__` and 15 `#ifdef _WIN32` sites. Each needs a `__vita__` decision.
- `std::filesystem` is used in `app.cpp`, `game_data.cpp`, `paths.cpp`, `app_icon.cpp`, and in `gpu_select.cpp` (10 uses). **[verify]** that newlib/VitaSDK supports it, or wrap it.
- Threads: `std::thread` only in `android_data.cpp` (from the grep). Check the audio and video paths for SDL threads.
- Paths: `platform/paths.cpp` → `ux0:data/ghvita/`.

### Audio and video
- Audio goes through the engine's own mixer on SDL3 audio. SDL3-Vita audio **[verify]** latency.
- FMV: the engine's **own MPEG-2 decoder** (`video/mpeg2.cpp`, about 1k lines, software). It may be too slow on the A9. The fallback is to skip FMV on Vita (the hardware decoder is AVC only).

## Phases

### Phase 0: environment (blocked on the user)
- [x] Install WSL2 + Ubuntu on this PC. 2026-11-10: the user turned on SVM in the BIOS, and Ubuntu was installed on WSL2 (`wsl --install -d Ubuntu --no-launch`, no default user; tooling runs as root via `wsl -d Ubuntu -u root`).
- [x] Base packages in Ubuntu 26.04.1: GCC 15.2, CMake 4.2.3, ninja, git, python3
- [x] VitaSDK via `tools/setup-vitasdk.sh`: release channel **2026.08** (sequence 2026102473, toolchain sdk-snapshot-20260825), `arm-vita-eabi-gcc 15.2.0` (so C++20 is fine). Packages: sdl3 3.4.16, vitaGL r1488 (g2bdbe89), vitaShaRK 1.7, libmathneon, zlib, taihen, kubridge.
  - Gotcha: vdpm was rewritten (pacman client + signed channels). The repo's `install-all.sh` is stale, so call the installed `vdpm` binary instead.
  - Gotcha: run WSL commands from PowerShell, not Git Bash (Git Bash rewrites `/mnt/...` paths).
  - **[verify]** how SDL3-Vita's window/video coexists with vitaGL. The SDL2 world ships a special `sdl2_vitagl`; for SDL3, plan to use SDL only for input/audio and initialise vitaGL directly.
- [ ] ~~Install the Vita3K emulator~~ (skipped: all testing runs on the console). `libshacccg.suprx` is on the console (below).
- [x] Hacked Vita ready: FW 3.65 + Ensō (version spoofed to 3.74, cosmetic only). `libshacccg.suprx` is installed in both `ur0:data/` (where vitaShaRK looks) and `ur0:data/external/` (where vitaGL r1488 looks; both checked with `strings` on the libraries).
- [x] GH1 PS2 disc ripped: `original_isos/Guitar Hero (USA).iso` (2,300,313,600 bytes)
- [x] Confirm a native Linux build of upstream gd01 runs with the user's ISO. 2026-11-10: `tools/build-desktop.sh` builds the unmodified upstream for Linux in WSL, and `tools/run-desktop.sh` runs it through WSLg. The user confirmed the menus work and a song plays. Rendering is software (llvmpipe, GL 4.5 Mesa 26.0.8), audio goes through PulseAudio.

### Phase 1: spike, "hello highway" (upstream estimate 1–2 weeks)
- [x] `vita/CMakeLists.txt` builds the **unmodified** upstream sources with VitaSDK (2026-11-10). It produces `ghvita.vpk` (2.9 MB) with **0 warnings** and **no upstream patches so far**.
  - gd-engine-core can't be dropped: `renderer.h` → `core_renderer.h` → `gd/core/render/device.h`, and `app.cpp`/`mixer.cpp` use `gd/core/audio/output.h`. So we build a **reduced gdcore**: `gdcore_render` (device.cpp, spirv_reflect, gl backend; `GDCORE_RENDER_SDLGPU=0 VULKAN=0`) and `gdcore_audio` (output.cpp only, no miniaudio/OpenAL/FAudio backends).
  - The SPIR-V headers (`core_renderer_spirv.h`, `core_post_fx_hdr_spirv.h`) come from host glslang in the desktop build. They're platform-independent data, so `tools/build-vita.sh` copies them from `/root/build/gd01-linux`. The desktop build must run first.
  - `vita/compat/dlfcn.h`: `output.cpp` uses `dladdr` only to find the module's folder, so the stub reports failure (no dynamic loader on Vita).
  - `midi_in.cpp` compiles as its stub (no `GD_RTMIDI_API`). `vk_upscaler_stub.cpp` replaces the Vulkan upscaler, as on Android.
  - The icons are GDE's own art (`res/gd01_icon*.png`), embedded with upstream's `embed_file.cmake`.
  - Title ID: `GHVT00001` since 2026-11-15 (was the placeholder `GDDE00001`; the old bubble must be deleted by hand, the data folder is not tied to it).
- [x] **vitaGL bring-up, built (2026-11-10), untested on hardware.** `vita/src/vita_platform.cpp` plus `-Wl,--wrap=` for 10 SDL symbols. Still no upstream patch.
  - `SDL_CreateWindow` → 960x544 without OPENGL/RESIZABLE/HIGH_PIXEL_DENSITY. `SDL_GL_CreateContext` → `vglInitExtended(0, 960, 544, 16 MB, no MSAA)` once. `SwapWindow` → `vglSwapBuffers`. `GetProcAddress` → `vglGetProcAddress`. MakeCurrent, DestroyContext and SetSwapInterval are no-ops. ExtensionSupported checks vitaGL's GL_EXTENSIONS.
  - GL audit: gd01 + gdcore look up 86 GL functions. vitaGL r1488 exports all except `glFenceSync`, `glClientWaitSync`, `glDeleteSync` (stand-ins: glFinish, then "already signalled") and `glQueryCounter`, `glGetQueryObjectui64v` (stand-ins: no timings, 0). Each stand-in logs once.
  - vitaGL reports "OpenGL ES 2.0 VitaGL", so `gl.cpp` takes its GLES path (shader `#version 330 core` → `#version 300 es`). **[verify]** how vitaGL's GLSL translator handles GLSL ES 3.00 (`in`/`out`, `layout`, `flat`, `texelFetch`).
  - Paths: `SDL_GetBasePath` and `SDL_GetPrefPath` → `ux0:data/ghvita/`. `loadRememberedPath()` finds an `.iso` in the base path, and config, career and enhanced settings go there too.
  - Renamed on 2026-11-14 from `gd01` to `ghvita`: the data folder (`ux0:data/gd01/` → `ux0:data/ghvita/`), the log (`ghvita.log`), the CMake target and ELF (`ghvita`), the VPK (`ghvita.vpk`), thread names, and our own environment variables (`GD01_VITA_*` → `GHVITA_*`, e.g. `GHVITA_PHASES`). On start the game moves an old `gd01` folder to `ghvita` (`sceIoRename`; skipped if `ghvita` already exists) and deletes the stale `gd01.log`. Unchanged: the env vars upstream reads (`GD01_LOG`, `GD01_FRAMELOG`, `GD01_LOG_SHOTS`), compile-time macros (`GD01_VITA`, `GD01_VITA_PHASE`), CMake variables, the title ID and the build folder `/root/build/gd01-vita`. `config.txt` keeps the ISO as a full path, and that path wins over the `.iso` search, so the start-up step also rewrites `ux0:data/gd01/` to `ux0:data/ghvita/` in it (`fixOldConfigPath`, every start). Checked on hardware 2026-11-14: the old folder moved with the ISO, saves and settings, no `gd01.log` left (run 20261114-225700); the first run said the ISO was missing, after the `config.txt` fix it loaded 56 songs from `ux0:data/ghvita/` (run 20261114-230138). Reinstall the VPK (the old one was `gd01.vpk`, same title ID).
  - Log: a constructor before `main` sets `GD01_LOG=ux0:data/ghvita/ghvita.log`. main.cpp's `openLogFile` flushes every line, so a crash still leaves the log up to the last line.
  - Clocks: CPU 444, bus 222, GPU 222, xbar 166 MHz. newlib heap is 192 MB **[verify/tune]**.
  - Useful later: `runShaderTest()` (app.cpp) compiles every program and lists failures. It's only reachable through a command-line flag today; expose it on Vita (e.g. hold a button at boot).
- [x] **First hardware runs (2026-11-10):**
  1. vitaGL starts: "SGX543MP4+ / OpenGL ES 2.0 VitaGL", 83 MB VRAM free. The "OpenGL logo" the user saw is vitaGL's own splash thread. The game crashed right after `OpenGL: ...`.
  2. With shader tracing: both shaders of the first program report compile "ok", then `glLinkProgram` crashes. The core dump (`tools/parse-core.sh`) shows a data abort in SceGxm, with LR in `glLinkProgram+0x7fa` right after `sceGxmProgramGetParameterCount(NULL)`. So vitaGL turned the GLSL ES 3.00 sources into **null GXM programs while reporting success**.
- [x] **Fix (works on hardware since run 3):** `vita/src/glsl_es100.cpp` rewrites every shader to GLSL ES 1.00 before vitaGL sees it. `layout(location)` inputs become `attribute`s, bound with `glBindAttribLocation` before the link (hooks on glCreateShader/glAttachShader). in/out become varying, the fragment output becomes `#define … gl_FragColor`, and `texture*()` becomes the `texture2D*()` forms. The two traced shaders pass `glslangValidator` as ES 1.00 (`tools/check-shaders.sh`). Things ES 1.00 lacks (`flat`, `texelFetch`, `uint`, MRT) are logged per shader. Expect those to need more work.
- [x] **Hardware run 3 (2026-11-10): the game boots to the menus.** The GLSL ES 1.00 rewrite works: the first program (1 vertex + 1 fragment shader) compiles and links. Audio comes up as "vita" (1024 frames at 44.1 kHz), the ISO loads (**56 songs**), and the controller is detected ("PSVita Controller"). The timer-query stand-ins are hit in the menus.
- [x] **Quick play crashed: out of memory** (fixed by the Vita profile, patch 0001, and later memory work; see runs 4-5 and 20). `char_multi: character nu_metal: std::bad_alloc`, then a data abort in SceLibKernel (the dump's stack only has stale vitaGL values). Source: the character select preview (`ui/select_panels.cpp:~265-300`) caches **up to 6 full Character models** (meshes, clips, textures) at once. That's fine on desktop, too heavy for Vita: this is the Phase 3 "memory diet", hit early.
  - Build 4 (untested): extended memory mode (`ATTRIBUTE2=12` via `VITA_MKSFOEX_FLAGS`), newlib heap 192 → 256 MB. A memory report goes into ghvita.log on the first frame and every 10 s (heap used/peak, vitaGL free VRAM/RAM). malloc/calloc/realloc/memalign are wrapped: a failure goes to `ux0:data/ghvita/memory.log` with its size, written without allocating (sceClib + sceIo).
  - If it still fails: lower the preview cache from 6 models on Vita. That would be the first real upstream patch candidate, e.g. a `#if defined(__vita__)` cap, or better an upstream "low memory" setting.
- [x] **Hardware run 4 (2026-11-10, extended memory + memory reports):** quick play now reaches the song list (laggy) and crashes loading the stage. Enhanced Settings crashes on open. The boot movie after "press any button" is very laggy (skippable by spamming buttons).
  - **Memory diagnosis:** the heap isn't the root problem, **video memory is**. vitaGL had 83 MB of VRAM free at init, **33 MB at the first menu frame** (50 MB gone), and 0 MB at the venue. Then vitaGL spills into the newlib heap (153 → 256 MB) and `memory.log` shows ~87 KB memalign failures with the heap full. The cause: the Vita fell into the **desktop Enhanced Settings profile** (`internal_res` 1080 plus its post-FX render targets).
  - **Fix = first upstream patch** `patches/0001-vita-enhanced-profile.patch`: a `#elif defined(__vita__)` branch in `profileValues()` (next to Android's): fullscreen, max_fps 60, `internal_res` **448** (the PS2's; allowed values are 448/480/720/800/900/1080/1440/2160), `bokeh` none, touch on. It carries the "Modified by irxd" header line. `tools/apply-patches.sh` (idempotent, run by build-vita.sh) applies `patches/NNNN-*.patch`.
  - **Enhanced Settings crash:** a different program's `glLinkProgram` (null GXM program again). Its vertex shader builds a full-screen quad from `gl_VertexID` + bit ops. Fixed in the Vita layer: the 3 known expressions become `mod`/`floor` on a `vita_VertexID` float attribute (bound at `GL_MAX_VERTEX_ATTRIBS - 1`), fed by a 0..255 VBO from a `glDrawArrays` hook while such a program is current (`glUseProgram` hook).
  - **All shaders checked offline:** `tools/dump-shaders.sh` runs the desktop build's `--shadertest` with `GD01_GLES=1` under an LD_PRELOAD shim. That gives **19 programs / 38 sources**, all of which now validate as GLSL ES 1.00 after the rewrite. Besides `gl_VertexID`, the rewrite needed: int `abs()` → `vita_iabs()` (upscale blur), array constructors (FXAA `kSteps`), and `textureLod` in a fragment shader → `texture2D` + bias (auto-exposure; exact for constant UVs). **[verify]** that vitaGL's translator accepts what glslang accepts (dynamic loop bounds, uniform arrays).
  - Boot movie lag: the engine's software MPEG-2 decoder (as the upstream study predicted). There's no built-in skip option. Re-measure with the lighter profile, then decide: optimise, lower decode resolution, or skip FMV on Vita (deviation).
- [x] **Hardware run 5 (2026-11-10, Vita profile + all shaders):** boot logos, main menu, setlist, Enhanced Settings and the in-song pause menu all **render correctly**. The pause screen holds **60 FPS**. But the menus and setlist drop to **2 FPS** at times (input stalls), the stage loads to a blank scene with no audio (stuck, no crash), and touch arrows appear on the right edge.
  - VRAM: 46 MB free at the first frame (was 33), but 0 by the setlist. Then vitaGL spills textures into the newlib heap (65 → 128 → 245 MB), and the GPU reading textures from main RAM is very slow: that's the 2 FPS.
  - Audio: each `VgsStream` reads its **whole compressed file** (`data_->read(path)`) and builds per-channel block offset tables (4 bytes per 16-byte block). The previews fail with `malloc(18,389,216)` / `malloc(1,103,464)` (bad_alloc) and are **retried continuously** while highlighted, each retry reading ~18 MB from the card. That's the setlist stall. The song's own VgsStream then can't be allocated, hence the silent stuck stage. Menu music (`meta_loop1e.vgs`) fails too.
  - Desktop GPU probe (`tools/gl-probe.sh`: desktop build, Vita settings, 960x544, LD_PRELOAD texture tracker): **menus = 55.6 MB textures** (540 textures, mostly 64K-1M RGBA8 with full mip chains) + 2.9 MB renderbuffers. The Enhanced Settings backdrop (a whole "big_club" venue + metal band, loaded at boot, a PC-only extra) is **17.4 MB** of that. Without it: 38.2 MB. (`--skip-boot --song` didn't start a song in that run, so in-song numbers still come from the Vita log.)
  - Patches: **0002** skips `loadBackdrop()` on Vita. **0003** caps the character preview cache at 2 models on Vita (the PS2 original's 6, `FUN_00140fb0`). **0001** no longer turns touch on (the right-edge arrows were the touch overlay). `tools/apply-patches.sh` now checks the whole stack via a throwaway index (0001 and 0002 both touch app.cpp).
  - Next if still short: no mipmaps on Vita (-25%), 16-bit textures where the PS2 palette allows, VGS streaming from the ISO instead of whole-file reads, and stopping preview retries after a failure.
- [x] **Hardware run 6 (2026-11-10, patches 0002/0003): a song loads and plays** (venue, band, highway, HUD and pause all rendered). Menus are healthy now (VRAM 68 MB free at the first frame, heap 22-57 MB, previews and menu music load). In a song it runs at **8-15 FPS**: VRAM 0 and the heap 43 → 197 MB, so vitaGL holds ~140 MB of venue/band/crowd textures in main RAM. Only red, yellow and orange frets work. The song was lost at 47 s.
  - Frets: the Vita pad gets `Profile::Joypad`, the PS2 table (green L2, red L1, yellow R1, blue R2, orange ✕). The Vita has no L2/R2. **Patch 0004** adds a `__vita__` layout: green L, red □, yellow △, blue ○, orange R. Strum on the D-pad, Star Power on Select, whammy on the right stick (X or Y).
  - Textures: vitaGL implements `GL_OES_compressed_paletted_texture` (`gpu_alloc_paletted_texture`). The Vita layer now re-packs every RGBA8 `glTexImage2D` with ≤256 colours (≥32x32) as **`GL_PALETTE8_RGBA8_OES`** (`vita/src/texture_palette.cpp`, lossless, 1/4 the size; desktop round-trip tests pass, ~1.7 ms per 512x512 on the PC, so expect ~10x that on the A9). Paletted textures keep **no mip levels** (generate is skipped and mipmapped min filters fall back to their base filter, tracked per texture since vitaGL lacks `glGetTexParameteriv`): a **Vita deviation**, possible shimmer on distant surfaces. A rejected upload falls back to RGBA8 and switches the feature off. The memory report counts paletted vs RGBA textures and MB saved.
  - If FPS is still low with VRAM no longer full, the bottleneck is CPU/GPU work, not memory: measure next (venue draw calls, crowd, skinning, the audio mixer).
- [x] **Hardware run 7 (2026-11-10, paletted textures + patch 0004):** no allocation failures (`memory.log` is only written on a failure). **583 textures paletted, 38 MB saved**, only 21 left RGBA (2 MB). In a song: VRAM 17 MB free at the start, **sliding to 3 MB** over the song (textures loaded later + vitaGL buffer churn), heap a steady ~108 MB. **~10 FPS** with memory no longer exhausted, so the cost is CPU/GPU work. All 5 frets work. Other issues: ○ (blue fret) also opened the pause menu, and after **Retry** every model was missing except the highway (60 FPS: nothing left to draw).
  - ○ pause: `Input::pollPad` emits built-in menu "helpers" (D-pad left/right, ✕ = Confirm, **○ = Back**), and Back is sent even in a song (pause). **Patch 0004** (amended): on Vita, in a song, a helper button bound to a fret is only that fret.
  - Retry: `startSong` does `venue_ = std::make_unique<VenueRenderer>(...)`, so the new venue is built while the old one is still loaded and both must fit in ~3 MB of free VRAM. **Patch 0005** does `venue_.reset()` first on Vita. (A `glFinish` would help vitaGL reuse the memory sooner, but app.cpp has no GL access and the renderer has no finish method; left out.)
  - **Perf counters** (Vita layer): every 5 s, `vita: perf:` logs fps and, per frame: CPU ms between swaps, ms waiting in `vglSwapBuffers`, draw calls, and KB of buffer uploads. Next: decide CPU-bound (game logic, skinning, crowd, audio mix) vs GPU-bound (vertex lighting loop over 8 lights + skinning in the translated shaders, fill rate, post passes) from these numbers.
- [x] **Hardware run 8 (2026-11-10, patches 0004/0005 + perf counters):** ○ no longer pauses, and Retry reloads the venue correctly. The perf counters show the game is **CPU-bound everywhere**: swap wait is 0.1-6 ms per frame, while CPU time is 80-110 ms per frame in a song (290-700 draws, ~180 KB buffer uploads per frame) and 106-145 ms per frame during the intro movie (11-40 draws).
  - **Movie:** desktop bench (`tools/movie-bench.sh`, `vita/tests/movie_bench.cpp`, the user's ISO): the disc has `videos/wavegroup.pss`, `ghintro.pss`, `character.pss`, `venues.pss`; 640x448, about 1:2:6 I:P:B; per picture **decode 0.9-3.4 ms, RGBA conversion 1.3-1.5 ms on the PC**. On the Vita: about **25 ms decode + 12 ms conversion**, one frame budget (33 ms at 29.97 fps) on the main thread together with the menus. `App::movieFrame` decodes every picture due (references) and converts the newest.
  - **Patch 0006 + `vita/src/movie_worker.cpp`:** a worker thread (SDL3 thread, 1 MB stack) owns its own `PssMovie`, decodes ahead, converts only the pictures that will be shown (late ones are decoded, not converted), queues up to 3, and puts the PCM into the SDL audio stream directly. The disc is still read **on the main thread only** (GameData's `RandomFile` is one shared handle with seek+fread): `movieFrame` feeds up to 8 x 64 KB per frame into the worker's 1 MB buffer. The movie clock starts at the first ready picture. `endMovie` joins the worker before destroying the audio stream. Desktop builds are unchanged (guarded).
  - Vita layer: a texture whose level 0 is uploaded a second time is **dynamic** and stays RGBA8 (the movie picture was scanned and paletted every frame).
  - If the movie is still short of 30 fps: skip B pictures when behind (decoder change), or YUV→RGB in a shader (3 planes upload, no CPU conversion).
- [x] **Hardware run 9 (2026-11-10, first runs through `tools/vita-loop.sh`):** the movie worker kept the menus at 40-60 FPS, but the user saw the **video frozen** (sound fine). New worker statistics showed why: **decode alone took ~90 ms a picture** (estimate was 25 ms), so every picture after the first was late and the worker skipped converting all of them.
  - Callgrind on the desktop bench: the float IDCT (`idct8x8`, plus a libm `floor` per pixel) is ~57% of decode instructions; RGBA conversion is about as much again per shown picture.
  - **Patch 0007 + `vita/src/mpeg2_neon.cpp`:** NEON IDCT, motion compensation, block output and YUV→RGBA. Each is bit-identical to upstream (same float sums in the same order; exact integer identities), self-tested on first use against a scalar copy (fallback if any result differs), with timings in the log. On hardware: IDCT 6.2-6.5 µs a block (scalar 16.4), MC 0.9 µs a 16x16 block (scalar 5.0), block output 0.27 µs (0.41). YUV→RGBA got no faster (26 µs per 32x12 either way): latency-bound on the A9's in-order NEON.
    - Lesson: the first NEON IDCT converted coefficients on the VFP and was barely faster (12.7 µs); VFP↔NEON transfers stall the A9. Keep all arithmetic in NEON registers (`vmlaq_lane_f32`).
    - Lesson: a timer around every 8th call (scaled ×8) measured mostly the timer. Use call counts plus bulk micro-benchmarks.
  - Decode went **90 → 43-55 ms a picture** (~4,800-6,600 IDCT blocks a picture: these movies are mostly intra-coded). Still slower than real time (33 ms), so the worker now **skips B pictures while more than 2 frames behind** (they are not references; the decoder keeps their display index so the clock stays right), and when late still converts at least every 4th picture. Result: the movie stays **within 4 frames of its sound** and shows ~7-8 pictures a second instead of freezing.
  - `vita-elf-create` failed once ("Cannot allocate 4476 bytes for SCE data ... segment 1 overlaps") when the code ended just under a 64 KiB boundary: `vita/CMakeLists.txt` now sets the SDK linker script's `__sce_headroom` to 8 KB.
  - Next for a smooth movie: an integer fast IDCT (Chen-Wang style, ~4x fewer operations, IEEE 1180 accuracy instead of bit-exact), RGBA conversion on core 2 or on the GPU, then decoding fewer B pictures as it catches up.
- [x] **Hardware run 10 (2026-11-10, song profiling through the loop):** `--song smokeonthewater --autoplay` now starts a song with no input: `vita/src/vita_platform.cpp` wraps `main` and reads one-shot arguments from `ux0:data/ghvita/args.txt` (`tools/vita-loop.sh --args`; `NAME=value` words become environment variables, e.g. `GHVITA_DISPLAY_SCENES`, `GHVITA_PARAM_MB`). Frame phases (patches 0008/0009, `vita_profile.h`) log ms per frame, draw calls, time inside vitaGL draw calls and framebuffer readbacks per phase.
  - Before: 10-12 FPS, ~90 ms a frame. The track's `track_fade` grab (`glCopyTexSubImage2D`) waited 21-23 ms for the GPU to finish the venue: **patch 0010** drops it on Vita (deviation: no fade at the highway's far end).
  - The wait then moved to just after the swap: the **`glFenceSync` stand-in called `glFinish()`**, and `Renderer::endFrame` makes a fence every frame, so CPU and GPU never overlapped. It is now a no-op (vitaGL's triple buffering already bounds frames in flight). **Result: 14-17 FPS, 58-70 ms CPU a frame, swap wait 0.1-0.2 ms** (448 internal resolution and native perform the same; 448 stays the default). More GXM scenes per render target made no difference.
  - Remaining per frame (ms): venue crowd 17-21, venue draw 17-26 (only 3.5-5 inside vitaGL; 290-440 draws), guitarist 9, band 7, track 5, venue script 1.2. `Character::poll` (skeletal animation) is behind guitarist, band and most of the crowd.
  - Desktop callgrind of a whole song does not work (valgrind never got past loading); use on-device phases and micro-benchmarks.
- [x] **Hardware run 11 (2026-11-11): character animation.** `Character::poll` kept every bone in `std::map<std::string, ...>`: `blendChain` built a string-keyed accumulator per call, `updateWorld` (twice a pose) a `std::map<std::string, char>` plus a `std::function` recursion, `pose` and `servos` searched by name. **Patch 0011** builds bone arrays once per character (owners, parents-first order, buffer slots, clip bone pointer -> slot, servo bones) and runs the same arithmetic on them; the first 8 poses of each character also run upstream's code and compare bit for bit (21 characters, 0 differences). Guitarist 9 → 2.3 ms, band 7 → 0.6 ms, crowd 17-21 → 1.5-2.6 ms. **Songs: 22-34 FPS** (29-45 ms CPU a frame).
  - Remaining per frame: **venue draw 17-30 ms** (290-480 draws, only 3.4-5.8 ms inside vitaGL), track 5 ms, venue script 1 ms. Next target: the CPU around each draw in the scene renderer.
- [x] **Hardware run 12 (2026-11-11): scene renderer.** Patch 0012 times `Renderer::drawMesh` as a nested phase: of the old 17-30 ms "venue draw", 9-13 ms was `Renderer::drawMesh` (4.4-6.3 ms of it inside `glDrawElements`, the rest uniforms and state) and 12-19 ms the scene renderer's own work. **Patch 0013** caches the scene renderer's per-draw string work (the `name|material` key built and searched for every draw, child / bone / light lookups by name). Scene renderer 12-19 → 8-14 ms. **Songs: 24-35 FPS** (29-41 ms CPU a frame; the slow end is shots with 600 draws).
  - Remaining per frame: renderer 9-14 ms (half of it GL uniforms and state outside the draw call), scene renderer 8-14 ms, track 4 ms, guitarist 2.3 ms, crowd 1.5-2.5 ms, script 1.1 ms.
  - Ideas kept for later: animation LOD for the crowd (the user's suggestion: far members at half rate; worth < 1 ms now), skip redundant `glUniform` calls, animation on another core.
- [x] **Hardware run 13 (2026-11-11): profiler cost, vitaGL build, transform updates.**
  - **The profiler was part of the cost:** a clock read (`SDL_GetTicksNS`) takes 1.3 us on the Vita, and the per-draw timers read it 4 times per mesh: 3-4 ms of every song frame. Phase timing is now off by default; `GHVITA_PHASES=1` (args.txt) times frame phases (nearly free), `=2` also the nested `renderer: draw mesh` phase and every draw call (the old, costly mode). Run 12's renderer numbers were inflated by it.
  - **Own vitaGL build** (`tools/build-vitagl.sh`, linked by `tools/build-vita.sh` when present): vitaGL e1473ff (2026-11-08) with `NO_DEBUG=1 HAVE_VERTEX_LAYOUT_CACHE=1` instead of vdpm's 2bdbe89 (2026-09-23, `NO_DEBUG=1`). No measurable change per draw (~12 us inside `glDrawElements` either way); kept for the newer fixes. vitaGL's uniform setters are a memcpy plus a dirty flag, so skipping redundant `glUniform` calls would save little.
  - **Patch 0014:** `AnimPlayer::updateTransforms` looked up every node's parent by name in a `std::map` on every call (the track calls it ~10 times a frame). Parent indices are now resolved once. Track 4.6 → 3.0 ms, script 1.1 → 0.85 ms.
  - **Songs: 34-48 FPS** (20.5-29.5 ms CPU a frame; GPU wait 0.1-0.2 ms, so still CPU bound). The heaviest shots (520 draws) are at 34-35 FPS.
  - Per frame now (level 1): venue draw 11-20 ms (~32-37 us per mesh, scene renderer + GL), track 3 ms, guitarist 2.2 ms, crowd 1.5-2 ms, script 0.85 ms.
- [x] **Hardware run 14 (2026-11-11): 30 FPS lock.** **Patch 0015:** Max FPS on Vita is 30 (new default), 60 or Unlimited. `Renderer::setFrameCap` sets vitaGL's vblank interval (`eglSwapInterval`: vitaGL's display queue calls `sceDisplayWaitVblankStartMulti(n)`), so 30 shows each frame for exactly two 60 Hz refreshes, with no timer drift. **Songs at 30: 29.7-30.0 FPS**, 4-12 ms to spare per frame; the heaviest shot (525-544 draws, 30-31 ms CPU) is at the edge (27-30). 60 runs unlocked (34-48).
  - One launch hung inside `vglInitExtended` right after a quit through vitacompanion (no crash dump); the relaunch was fine. Watch for it.
- [x] **Hardware run 15 (2026-11-11): view culling.** Benchmarks are now repeatable: `GHVITA_SEED=1` (patch 0017) fixes the camera's shot choice (`--args 'GHVITA_PHASES=1 GHVITA_SEED=1 --song smokeonthewater --autoplay'`, with `max_fps` 60 in enhanced.json for unlocked numbers). Note: `SDL_getenv` does not see args.txt variables (SDL copies the environment earlier); use `std::getenv`.
  - **Patch 0016:** the scene renderer had no view culling. Each non-skinned mesh now gets a bounding sphere from its vertices, tested against the six clip planes of its MVP. Same shots, CPU per frame off → on: 29.6 → 28.4 ms (516 → 497 draws), 21.8 → 20.2, 28.4 → 25.8, 26.1 → 24.0 ms. Little gain in the widest shot (only ~15 meshes off screen).
  - Unlocked: 35-49 FPS; locked 30 has ~5 ms to spare in the heaviest shot.
- [x] **Hardware run 16 (2026-11-11): second benchmark song, crowd LOD.** `--song iloverockandroll` (seed 1) is heavier than smokeonthewater: wide shots of 590-660 draws at 33-36 ms (27-30 FPS unlocked). Patch 0009 now splits "venue draw" into crowd impostors / stage / band / crowd / rest: in those shots the crowd is ~14 ms (impostors 3.7, 3D members 8-8.8, animation ~2).
  - **Patch 0018** (the user's idea): Max FPS 60 leaves out the crowd (no animation, no draw): **44-56 FPS** on this song (17-20 ms CPU). At 30 the crowd stays; its six impostors are re-rendered half per frame (3.7 → 1.9 ms): **29.3-30.0 FPS**, the widest shots at the edge (31.7-33.7 ms CPU).
  - Deviations (Vita): no crowd at Max FPS 60; impostor crowd animates at 15 Hz at Max FPS 30.
- [x] **Hardware run 17 (2026-11-11): frame spikes.** The perf line now has the slowest frame and the count over 34 ms (a frame that misses the 30 fps vblank pair is shown for a third refresh: the user's "split-second 23 fps"); at `GHVITA_PHASES=1` each slow frame logs its four longest phases.
  - **Log writes stalled the game:** main.cpp fflushes ghvita.log after every line (and SDL's own output follows); on the memory card a line blocked 20-60 ms, so every 5 s perf line made a hitch, also in normal play. `vita_platform.cpp` wraps `SDL_SetLogOutputFunction`: the game's log function runs on a writer thread (core 2), drained before it is replaced at exit. Still ~17 ms late after a write (card contention), so the periodic perf and memory lines are only written when `GHVITA_PHASES` >= 1.
  - Patch 0018: the 3D crowd members animate half per frame too (crowd animation 2.0-2.3 → 0.9-1.3 ms). I Love Rock and Roll at 30: most 5 s windows have 0-1 frames over 34 ms (before: 1-13).
  - **Still over budget:** the widest crowd shot (650 draws, 34.5 ms): 3D crowd draw 8.3 ms. A skinned mesh draws with at most 4 bones, so each crowd member is many small draws. Options: merge each member's skinned pieces into fewer draws with a bigger bone palette (no visual change, larger job), or cap the 3D members drawn (visible change).
- [x] **Hardware run 18 (2026-11-11): merged characters (tried), GPU waits, lighting.**
  - Per phase, level 1 now counts draws. A character is ~11 skinned pieces (4 bones each); the widest shot draws ~23 crowd members as 259 draws, the band ~130.
  - **Merging (patch 0019, experimental, off):** consecutive pieces with the same draw settings concatenated into one mesh with an 8-16 bone palette; each vertex's colour bytes (white on skinned meshes) hold its palette indices; `vita_platform.cpp` rewrites the scene shader to `uBoneRows`. Draws fell 259 → 136 (crowd) and 135 → 60 (band); a CPU self-check matched the original skinning exactly. **But on the GPU:** indexing the palette by a per-vertex index skinned some band members wrongly (stretched, dark: the user saw it), and the index-free blended form (sum over slots) crashed libshacccg in `glLinkProgram`. Merging is off unless `GHVITA_MERGE=1`; upstream's shader is untouched without it. Also the time was mostly CPU per piece, not per draw (crowd 8.5 → 8.2 ms with half the draws).
  - **GPU waits on off-screen targets:** vitaGL allows 1 scene in flight per FBO render target; each crowd impostor's scene start waited for the GPU's previous one, up to 7 ms a frame in busy shots. `GHVITA_FBO_SCENES` now defaults to 4: impostors <= 2 ms.
  - **Lighting per draw:** `lightingFor` allocated a vector and recomputed the lights for every piece; it now reuses the result for consecutive pieces with the same material (patch 0019).
  - Debug: `GHVITA_SHOT=<frame>,...` writes raw screenshots (`ux0:data/ghvita/shot_<frame>.rgba`); never committed (game art). A missing uniform name is logged once.
  - I Love Rock and Roll at 30 (seed 1): most 5 s windows 0-1 frames over 34 ms; the widest crowd shot 55 (was 137).
- [x] **Hardware run 19 (2026-11-11): Performance Mode, Crowd Reduced.**
  - Enhanced Settings on the PS Vita (patch 0015): **Performance Mode** (the `max_fps` key) 30 FPS / 60 FPS (No Crowd) / Notes Only; **Crowd** Full / Reduced (default Reduced). Unlimited stays valid in enhanced.json for benchmarks, without a menu entry.
  - **Crowd Reduced** (patch 0018): at most 8 3D members, on the shot region's spots nearest the camera (in front of it first); the other spots keep their flat sprites, so the crowd stays as dense. Widest shot's crowd draw 8.4 ms (262 draws) -> 4.5 ms (172). I Love Rock and Roll at 30, seed 1: frames over 34 ms 64 -> 26 per run, 13-14 of them in the loading window; every later 5 s window 0-1 (the 1s line up with the periodic report of a `GHVITA_PHASES` run).
  - **Notes Only** (patch 0015): the next song loads no venue (less memory, faster load) and a loaded one is not drawn: the highway on black, a locked 60 FPS at ~3 ms CPU a frame. 60 FPS (No Crowd) unchanged: 45-58 FPS in this song.
  - **Crowd as a check** (user's call, same day): on = the 8-member crowd at 30 and 60 FPS; off = no crowd at 30 and 60; Notes Only never has one. I Love Rock and Roll, seed 1, per 5 s window: 30 + crowd on 30.0 FPS, 0-1 frames over 34 ms after loading (crowd 4.5 ms); 30 + crowd off 30.0, cpu 17-19 ms; 60 + crowd on 32-55 FPS (cpu 18-28 ms); 60 + crowd off 47-58 FPS.
  - Hardware loop: the first launch right after uploading a new eboot hung before the first frame twice (no crash dump); the next launch ran. Seen before (run 18). **[verify]** cause.
- [x] **Hardware run 20 (2026-11-11): song list freezes, memory leak per song.**
  - **Song list freeze:** `songPreview` read the song's whole .vgs (18-36 MB) on the main thread: 5.0 s for Ace of Spades (19 MB), the list frozen until the preview played. Patch 0020: a worker thread (`vita/src/preview_loader.cpp`) builds the preview and only its window of the file: every song .vgs alternates its 4 channels one 16-byte block at a time (checked on all 59 songs; the last 4-6 blocks of 15 songs are uneven, so blocks are still assigned by their channel byte), so the window's byte range is computed. Main thread 0.1 ms per selection; the preview plays 0.7-1.4 s later; ~2 MB instead of 18-36 MB. `RandomFile::read` is locked (one shared FILE*).
  - **Memory leak (the user's `malloc(18389216) FAILED` at 191 MB in use):** a test loop of songs started and left grew the heap after leaving 24 -> 41 -> 100 -> 175 MB; the 5th venue failed to load. A temporary allocation tracker (by caller, resolved with addr2line) put 99% in vitaGL's `gpu_alloc_mapped_aligned_for_cpu/gpu` (textures and buffers past the VRAM spill into the heap): upstream's `Renderer` has no delete call and frees GL objects only at shutdown. Patch 0021: `~SceneRenderer` frees what it created. Six songs in a row now leave the heap at 40-41 MB; the next songs render correctly.
  - **Disc reads:** newlib's fseek + fread reached the memory card at ~4 MB/s; `sceIoPread` (patch 0020) reads ~12 MB/s: a 19 MB song .vgs 5.0 -> 1.6 s, previews 0.24-0.46 s (were 0.7-1.4). The user: scrolling is free now.
  - Hardware loop: vitacompanion's synthetic `press` inputs do not reach the game (SDL's Vita joystick does not see them) **[verify]**; menus can only be tested by hand or with in-game test hooks.
  - **D-pad Left confirmed in menus (user, 2026-11-12, Career's hero select):** D-pad Left is the Green fret, and the menus take a green fret as select, so it chose the hero. Patch 0004: outside a song a fret bound to a D-pad button is only its direction. Up/Down only on that screen is upstream's (a vertical list). **[verify]** on hardware: hero select and a Left/Right setting.
  - **Store showed "_shop_desc" for $0 (user, 2026-11-12):** scrolling up from a store list's first item. The script wraps with `{mod {+ itemIdx -1} n}`, and upstream's `mod` is `fmod`, which keeps the sign: index -1, an empty item name. Patch 0022: `mod` wraps into [0, n) on the Vita. Every `mod` in the disc's scripts (cheats, cheat functions, career store, tutorials) wraps an index, so none relies on a negative result. An upstream bug on every platform, worth offering upstream; the original's `mod` is assumed non-negative **[verify]**. Not yet tried on hardware (the Vita was asleep).
  - **Enhanced Settings for the Vita (user's call, 2026-11-12):** patch 0023 leaves out the rows that cannot work or would break the game (the 7 Modes presets set PC resolutions and frame rates, and 1080p filled the video memory in run 5; Resolution, graphics / audio backends, Screen Mode, AI upscaling, HDR, mouse, Export / Load (a desktop folder picker), anti-aliasing, ambient occlusion, sharpening, sharper sprites) and, for now, the post-effects and extra lights (bloom, cinematic colour, colour grading, film grain, chromatic aberration, Star Power blur, bokeh, all band mates' shadows, more lights, light dust: untested, no frame time at 30 FPS; to measure one by one). The list is `vita/src/vita_settings.h`; hidden settings read their Vita defaults whatever enhanced.json holds. Kept: touch, calibration, Performance Mode, Crowd, FPS counter, particles, shaky camera, and the cheats. **Shake for Whammy** shown on the Vita (upstream: Android only), on SDL's Vita sensor driver (accelerometer, sceMotion); off by default. **[verify]** on hardware: the menu, and the shake threshold (4 m/s² past gravity, upstream's phone value).
  - **Shake for Whammy on hardware (user, 2026-11-12):** the accelerometer opened; 23 shakes in one song registered at 4.1-8.8 against the threshold 4, the softer ones only just; threshold lowered to 3 on the Vita. No Venue cheat hidden too (Notes Only covers it).
  - **Particles benchmark (2026-11-12):** I Love Rock and Roll, seed 1, 60 FPS, crowd on, 26 windows of 5 s: None 48.3 FPS avg (cpu 19.0 ms), Original 44.0 (22.2 ms), 2x 43.9 (22.2 ms). Original costs ~3.2 ms a frame of CPU; fits at 30 FPS (the default stays Original). 2x equals Original: emission is doubled but each emitter's `maxParticles` (from the scene file, particles.cpp) already caps it in this venue.
  - **Button Layout (user's call, 2026-11-12):** Enhanced > Controls > Button Layout (patch 0024). Layout 1 (default): D-pad Left, L, R, Circle, Cross, strum on D-pad Up / Down. Layout 2: D-pad Left, D-pad Up, Triangle, Circle, Cross, optional strum on L / R (only in a song: a strum is also the menus' Up / Down; user, same day): a Joypad's `requireStrum` is off by default (a fret press plays the note), so strum is optional on the Vita (the user asked whether it was needed). Menus keep D-pad Up / Down (MenuUp / MenuDown are separate controls; outside a song a D-pad fret is only its direction, patch 0004). The layout sets the Joypad defaults (`Input::bindingsFor`); bindings customized on the Gamepads screen take precedence, and its Reset to Defaults goes back to the chosen layout. Remapping itself (Gamepads > Customize inputs) is upstream's screen, **[verify]** on hardware. Not offering patches upstream (user, 2026-11-12).
  - **Menus scrolled by themselves after the Gamepads screen (user, 2026-11-12):** changing Player 1's "Bind to" with Left / Right moves the Vita pad to another player (`Input::setPlayer` swaps it with the keyboard's player 2) between the press and the release; `UiSystem` keeps one held direction per player for the menus' auto-repeat and released it only for the releasing player, so player 1's Left repeated for ever, on every screen. Patch 0025: on the Vita a release clears that direction for both players. Upstream bug on every platform. **[verify]** on hardware.
  - **No keyboard on the Vita (user's call, 2026-11-12):** upstream's keyboard device took player 2 by default (controllers.json `"keyboard": {"player": 1}`) and was what the Vita pad swapped with on a rebind. Patch 0026: not listed on the Gamepads screen, `keyboardSlot()` -1, no saved keyboard player blocks a slot. Player 2 stays for a second controller: PS TV (two DualShocks) or a Vita with ds3vita / ds4vita / ds34vita.
  - **Ports (SDL 3.4.16 source and ds34vita's main.c, 2026-11-16):** SDL's first controller reads port 1 and falls back to port 0 only when port 1 fails; ports 2-4 are counted once at start (no hot-plug). ds34vita puts its first controller on port 1 *and* merges it into port 0 with the Vita's own buttons (unconditionally; moving it off port 1 is a kernel-only export). So with one DualSense only the DualSense worked. `vita_platform.cpp` now redirects SDL's port 1 read to port 0: player 1 = the Vita's buttons plus the first DualShock/DualSense (as other games), player 2 = a second external controller on port 2 (connected before the game starts). The Vita's own buttons cannot be told apart from the first external controller's (port 0 is OR'd), so Vita vs one DualSense as two players was turned down (user's call, 2026-11-16). Works on hardware: the Vita's buttons and one DualSense both play as player 1 (2026-11-16).
  - **60 FPS profiling (2026-11-12), I Love Rock and Roll, seed 1, 60 FPS, crowd on:** heavy frames ~25-28 ms CPU (~500 draws), light ~18 ms. GL micro-benchmark on hardware (2,000 calls each, in a song): every GL call Renderer::drawMesh makes costs 0.03-0.4 us (glUniform1f 0.06, glUniformMatrix4fv 0.41, state calls 0.07-0.26; ~3 us a draw in all); `SDL_GetTicksNS` costs 1.4 us, `sceKernelGetProcessTimeWide` 0.7, `sceKernelGetProcessTimeLow` 0.33. So: (1) the profiler now reads `sceKernelGetProcessTimeLow` (`phaseNow`, level 2 read the clock ~4 times a draw); (2) **a uniform cache does not pay**: a hook dropping unchanged glUniform* calls skipped ~90% (8,400 of 9,300 a frame) yet cost +4-5 ms a frame (hash lookups dearer than vitaGL's calls); reverted. vitaGL's own glDrawElements is ~11 us a draw (GXM setup; the vertex uniform block is ~1 KB). Level-2 nested phases inside SceneRenderer::drawObject (temporary) put ~5 us a mesh in the mesh lookup, ~4 us in bones for skinned pieces, ~3 us in state + lighting, and ~14 us in the walk itself, but the per-phase overhead (clock read and phase-name lookup) is a large share at that grain; from here, A/B runs at level 1. `tools/build-vita.sh` takes `SKIP_PATCHES=1` for such throwaway builds. Option C (scene renderer CPU work, user's pick): the per-mesh lookups (name resolution, mesh keys, lighting) are already cached (patch 0013); a count in `boneMatrices` showed consecutive skinned pieces never share a skin (0 of 20,000 calls) and share the same bone names in only 2-3%, so a bone-matrix cache has nothing to reuse. What is left there is under ~1 ms and within run-to-run noise. The real levers: fewer draws (crowd members, impostor refresh) or the venue update on a second core.
  - **Option D, step 1 (2026-11-12):** level-2 phases inside the guitarist and Character::poll: the clip chains, walk and IK logic are ~0; `pose()` (sampling, blending, world update, IK, servos) is 3.0 ms a frame over all characters, face 0.3. In the blend each sampled value looked its bone slot up (a hash and a string compare); patch 0029 caches the slots per clip (AnimClip::sample lists channels in a fixed order; the bone pointers are re-checked every call). Benchmark (60 FPS, crowd on, seed 1): CPU 20.5 to 19.9 ms, 46.0 to 46.6 FPS; the index skeleton self-check still matches upstream on every character.
  - **Option D, step 2: the crowd update on core 2 (tried, removed, 2026-11-12):** a one-job worker pinned to core 2 ran `crowd_->tempoEvent` + `crowd_->update` from after the script to "venue: crowd" while the main thread ran the guitarist, band and lights. Checked safe first (crowd members load no data and have no face, the crowd update makes no AnimPlayer lookups, characters are only created in the constructor). It ran with no crash and the crowd wait went 0.4 to 0.0 ms, but the guitarist and band phases each grew 0.1-0.2 ms (shared memory, thread handoff): CPU 19.9 to 19.8 ms, within noise. Removed: a worker thread is a lasting risk for ~0.3 ms. After patches 0028/0029 the venue update left is script 1.0 (the DTA interpreter, not thread-safe), guitarist 2.2, band 0.5, crowd 0.4 ms; the frame is mostly the venue draw (~10-12 ms of single-threaded vitaGL submission and scene walking).
  - **Options A + B (user, 2026-11-12), patch 0028:** at 60 FPS the Crowd setting draws 4 3D members (8 at 30) and the impostors are re-rendered a third a frame (each at 20 Hz; 3D members still animate half per frame, 30 Hz). Same run (seed 1, 26 windows of 5 s): 43.7 to 46.0 FPS on average (30.7-54.5 to 33.3-57.0), CPU 22.5 to 20.5 ms a frame, crowd draw 4.4 to 2.6 ms, slow frames 23 to 20. The user saw almost no difference from the 30 FPS crowd on hardware (same day). Next for 60: the venue update (character animation, ~5 ms) on a second core (option D).
  - **Next:** the CPU work per character piece in the scene renderer (state building, ~30 us a piece); a merged path that the shader compiler accepts (CPU-skinned merged crowd, or bones in a vertex texture); then the per-mesh cost in the scene renderer (state building, string mesh names in the track) and animation on a second core, the only route toward 60.
- FPS: the engine's own counter is under Options → Enhanced Settings → PERFORMANCE & GRAPHICS → "Show FPS Counter" (`fps_counter` in `ux0:data/ghvita/enhanced.json`). `GD01_FRAMELOG` logs the fps every half second.
- Tooling: `tools/parse-core.sh` decodes `ux0:data/psp2core-*.psp2dmp` (the dump must come from the same build). It runs vita-parse-core (Python 2 code) through `tools/parse_core_py3.py` in a venv with pyelftools 0.29. The user approved running this external tool on 2026-11-10.
- [x] GL shim: bind gl.cpp's pointers to vitaGL, and rewrite the shaders (all 19 programs, not only the highway)
- [x] Load the ISO from `ux0:data/ghvita/`, load one song chart, draw the highway, play the audio
- [x] Run it on hardware and measure fps and RAM (Vita3K skipped: every test ran on the console).
- **Gate (from upstream):** highway alone runs at 60 fps. Highway + venue runs at ≥ 30 fps. Total RAM ≤ 300 MB.
  - **Met (2026-11-15, from runs 14-20):** Notes Only holds a locked 60 FPS; highway + venue at Max FPS 30 holds 29.7-30.0; the heap settles at 40-41 MB between songs (256 MB newlib heap, extended memory mode).

### Phase 2: render profile (3–4 weeks)
- [x] Rewrite all remaining shaders: scene, venue, crowd, characters, UI, text (the GLSL ES 1.00 rewrite covers all 19 programs, run 5)
- [ ] Post-FX off by default. Remove the float targets and MRT.
- [ ] Shader cache on `ux0:`

### Phase 3: memory diet (2–3 weeks)
- [ ] Stream the venue archives, free decoded data after GPU upload, cap the texture cache
- [ ] Try P8 (paletted) textures for the PS2 palettised data

### Phase 4: Vita polish and release (1–2 weeks)
- [ ] Input map, default (user's layout, 2026-11-11, patch 0004): Green = D-pad ←, Red = L, Yellow = R, Blue = ○, Orange = ✕; strum = D-pad up/down; Star Power = Select (+ optional tilt via the motion sensor); whammy = either stick (left added 2026-11-15, patch 0037; bindings changed on the Gamepads screen still win); pause = Start. Optional alternatives: front-touch fret zones, rear-touch strum. All remappable via the GAMEPADS screen. Labelled as a "handheld mode".
- [x] In-game texts (2026-11-13, patch 0033): save/load messages reworded by the user (no "GD-Engine"), four GH Vita loading tips instead of the six GD-Engine developer tips, fallback first-screen name "GH Vita". Tips reworded the same day; deployed to the Vita.
- [x] FMV kept (2026-11-13, patches 0031/0032): fast paths in `vita/src/mpeg2_neon.cpp`, movies stretched to the full screen.
  - Before (intro movie, hardware): decode 49-53 ms a picture (5,300-5,800 IDCT blocks at 6.3 us), conversion 21.5 ms, ~8 pictures shown a second, 50-67 B pictures skipped per 5 s.
  - Integer IDCT (LLM as in the IJG's jidctint.c, 32-bit sums, `vmull`/`vmlal`, sparse first pass when rows 4-7 are zero, DC-only shortcut) fused with the block output: **2.0 us a block** (self-test mix). Colour conversion in 16-bit lanes (6 fraction bits, chroma terms computed once per sample): **7 ms a picture**.
  - After: decode 24-28 ms, conversion 7 ms, **all 30 pictures decoded, 22-30 shown a second (about 25)**, no B pictures skipped, at most 1 frame late.
  - 2026-11-14: the conversion moved to its own thread on core 2 (`vita/src/movie_worker.cpp`): the decoder copies the picture's planes into a hand-off slot (well under 1 ms) and decodes the next one, so a picture costs ~25-29 ms on core 1 instead of 31-35. The log line says `converted (... ms each, core 2; hand-off wait ...)`; without the thread it converts inline as before. Hardware (2026-11-14, intro movie): decode 25-29 ms, conversion 7.1 ms on core 2, **150 converted per 5 s (all 30 a second), hand-off wait 0.0, 0 frames late, no B pictures skipped**.
  - Accuracy (`tools/movie-fast-check.sh`, desktop, scalar code the NEON is checked against on the console): IDCT never more than 1 level from the float IDCT (1.4% of pixels differ), mean error -0.0002 against a double-precision IDCT; colour never more than 1 level (5% of channel values differ). `GHVITA_EXACT_MOVIE=1` selects the exact paths.
  - Lesson: `vqrshrn_n_s32` takes shifts up to 16; the second pass's 18 is `vshrn #16` then `vrshr #2` (same value).
  - Left for a full 30: decode + conversion is ~34 ms on core 1 against 33.4 ms. Moving the conversion to core 2 (copy the planes, ~430 KB) would close it. The rest of decode (~17 ms) is bitstream parsing and motion compensation.
- [ ] **Later (asked on Reddit, 2026-11-13): help for fast same-note runs (alt-strum sections).** On the Vita a fret press already plays a note, but a fast run of one note means hammering one button or alternating D-pad ↑/↓.
  1. **Rear touchpad strum: built 2026-11-15 (patch 0036); works on hardware with patch 0039 (joypad-mode strums replay the held frets), user 2026-11-15.** Enhanced Settings > Rear Touch Strum, off by default. SDL3 3.4.16's Vita driver polls the rear pad by default and reports it as touch device 2 "Back" (`SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE`, x/y 0..1 of the pad), with finger ids that can match the front's; upstream's `touchEvent` never looked at the device, so rear touches were taken for screen touches (a finger on the back in a song could press a lane). Now `Input::rearTouchEvent` gets them: nothing outside a song or with the setting off; on, each finger landing with x in 0.15..0.85 emits one Strum for the active player. Logged once per run (`input: rear touch strum`). To check: the edge band (`kRearEdge`) against real grips, accidental strums while holding, and strums against a fret press with Require Strum off (fret press + tap = two strums?). Was: each new finger touch on the rear pad is a strum, so two fingers drummed on the back work like alternate picking. Input-only (SDL3 reports the rear pad as its own touch device). Enhanced Settings toggle, off by default; ignore touches that stay down (grip) and count only fresh taps, maybe only in the middle of the pad.
  2. **Auto-strum assist (maybe, after 1):** while a button is held, notes on the held frets are hit at their exact time. Not fixed-rate rapid fire: GH1 penalises strums with no note, so that would break the combo. Touches gameplay code; label it as an assist, off by default.
  3. Front-screen swipe strum: skipped (thumbs are on the buttons).
  Related idea from the user: Star Power on a rear tap.
- [x] Our own LiveArea assets (2026-11-12): icon0 and startup "GH / VITA" lettering drawn by the user; bg.png and pic0.png are a procedural brick wall, orange-red under a blue-purple wash (pic0 with the icon lettering bottom right) from `tools/make-livearea-art.py` (earlier drafts: official GH1 art, removed; a procedural stage; an Unsplash photo). Packed via `FILE` entries in `vita/CMakeLists.txt`.
- [x] Credits screen shows the attribution line (already in upstream's credits roll, `ui/select_panels.cpp` kGd01Credits). Start screen notice on the Vita: "GH Vita, based on GD-Engine" (patch 0030, 2026-11-13).
- [x] Release v0.1.0 (alpha), released 2026-11-16 (tag v0.1.0 on c1c309d, GitHub pre-release; GHVT00001 clear on VitaDB, checked by the user). Version agreed 2026-11-15. Done: `CHANGES.md`; `tools/release-vita.sh` (clean build, VPK + full source incl. patched upstream, SPIR-V headers and vitaGL source, LGPL-3.0; notes from `docs/releases/v<version>.md`; SHA256SUMS in `dist/`); our version 0.1.0 apart from gd01's (title screen "v0.1.0", gd01's version in the log and controller report, patch 0038; log line `vita: GH Vita ...`); title ID `GHVT00001` (no web hits; VitaDB's list API returned nothing that day, [verify] on VitaDB before listing). Hardware: release VPK installed under the new title ID, D-pad and rear-touch strums confirmed (user, 2026-11-15). Left-stick whammy confirmed too (user, 2026-11-15). Left: re-run `tools/release-vita.sh` (dist/ predates the docs fixes), then tag and GitHub release (the user's call).
- [ ] ~~Offer the `patches/` to the maintainer~~ (not wanted, user 2026-11-12). List on VitaDB: later, the user's call.

## Performance plan (added 2026-11-10)

The game is CPU-bound (run 8: 80-110 ms CPU per frame in a song, swap wait ≈ 0.1-6 ms).
Ideas below come partly from the community notes in
[Yannis100/vita-port-skill](https://github.com/Yannis100/vita-port-skill) (MIT; reviewed,
not installed). Each step is measured on hardware with `tools/vita-loop.sh` before and after,
one change at a time; numbers carry units and conditions (song, scene, seconds into it).

1. **Measure where the CPU time goes** before optimizing. Either phase timers (a Vita-only
   patch stamping update / venue + characters / crowd / draw submission per frame, logged
   with `vita: perf`), or a sampling profile (psp2-tooling, see below). Bound each phase
   against the whole frame.
2. **vitaGL built from source** instead of the vdpm package: `NO_DEBUG=1` first (drops
   per-call error checks), then `HAVE_SHADER_CACHE=1` (first-use shader stutter). Then the
   speed hacks **one at a time**, since each can break rendering: `DRAW_SPEEDHACK`,
   `INDICES_SPEEDHACK`, `CIRCULAR_POOL_SPEEDHACK` (`READBACKS_SPEEDHACK` last). vitaGL is
   LGPL-3.0: the shipped source must say how to rebuild it. **[verify]** the flag names
   against the vitaGL revision we pin.
3. **Thread placement:** main/render thread on core 0, movie worker on core 1, audio on
   core 1 or 2 (`sceKernelChangeThreadCpuAffinityMask`), game-thread priorities in 64..127.
   Cheap. It mainly helps the movie and keeps audio off the render core.
4. **Fewer GL calls per draw:** 300-700 draws per frame through vitaGL's state tracking.
   Look at redundant `glUseProgram` / uniform / bind calls (count them with the existing
   hooks), and at batching static venue meshes.
5. **GL on its own thread** (larger job): record GL calls on the game thread and replay them
   on core 2 through a lock-free ring (KOTOR II port: ~4.5 ms capture per frame vs ~8 ms
   direct). Shared state aligned to 64-byte cache lines. Only if step 1 shows GL submission
   is a large share.
6. **Steady frame pacing:** if a song reaches a steady 30 FPS, cap it at 30 with each frame
   spanning two vblanks (vsync on) instead of an uneven 30-45.
7. **I/O:** GameData reads with seek + fread on one shared handle; `sceIoPread` and cached
   archive reads cut seek cost on the memory card (~10 MB/s).
8. **Precision:** `vglUseLowPrecision` (half floats in shaders) only if we turn out
   GPU-bound; check for visual damage.
9. **Shaders, if GPU-bound later:** no `if` / `?:` in the hot path (step/mix instead), and
   the vertex lighting loop over 8 lights is the first candidate.

### v0.2.0: 60 FPS (started 2026-11-16)

Goal (user, after good feedback on v0.1.1): **a stable 60 FPS in songs without the crowd**,
and if possible with it. Gate per 5 s window after loading, I Love Rock and Roll and Smoke on
the Water, seed 1: ≥ 59.5 FPS and at most 2 frames over 17 ms.

Where we start (runs 19-20, 2026-11-12): Max FPS 60, crowd off 47-58 FPS (17-21 ms CPU a
frame); crowd on 46 FPS on average, heavy frames 25-28 ms (~500 draws). Still CPU bound (swap
wait ~0.1 ms). So without the crowd ~4-5 ms must go from the heaviest frames, with it ~10 ms.
What is left per frame is mostly the venue draw (~10-12 ms of single-threaded scene walk and
vitaGL submission: ~11 us inside vitaGL per draw plus ~3 us of GL calls around it), track
3 ms, particles ~3 ms, guitarist 2.2, script 1.0. Uniform caching and a crowd update thread
were tried and did not pay (see above).

Steps, each measured on hardware against the baseline, one at a time:
0. **Baseline + GPU gate.** Perf line now counts frames over 17 ms. `GHVITA_GPUTIME=1`
   (measuring only) times a `glFinish` after each swap: the GPU's tile work after the CPU is
   done. Under ~16 ms in the heaviest shots means the GPU leaves room for 60, and the CPU is
   the whole job; over it, the GPU needs work too (shaders, overdraw, `vglUseLowPrecision`).
1. **Cheap wins:** vitaGL `SAMPLERS_SPEEDHACK` / `INDICES_SPEEDHACK` / `DRAW_SPEEDHACK=2`
   (safer variant) one at a time (each skips per-draw checks in vitaGL's draw path, e.g. empty
   sampler slots, per-stream index source setup); particles without per-frame vectors and
   string-keyed mesh lookups; LTO for the game.
2. **GL on its own core (main lever).** Every GL entry point already comes through
   `__wrap_SDL_GL_GetProcAddress` (gd01's `gl::` and gdcore's loader, 75 + ~90 names), so a
   proxy in `vita/` can record each call into a ring and a render thread on core 1 replays it
   into vitaGL, with no upstream patch. Calls that return something (`glGen*`,
   `glGetUniformLocation`, `glMapBufferRange`, `glReadPixels`, `glCheckFramebufferStatus`,
   info logs) wait for the ring to drain; pointer arguments are copied. Expected: vitaGL's
   time (~5-7 ms in heavy frames) leaves the main thread. **[verify]** that vitaGL can be
   driven from a thread other than the one that initialised it (GXM contexts are not
   thread-bound, but vitaGL's GC thread and display queue are).
3. **Crowd at 60:** after step 2, measure again; then fewer crowd draws (merged pieces on the
   CPU or bones in a texture, see run 18) if the crowd still does not fit.

**Hardware run 21 (2026-11-16): baseline and GPU.** I Love Rock and Roll, seed 1, Max FPS 60,
`GHVITA_PHASES=1`, 5 s windows:
- Crowd off: 46.9-58.4 FPS, cpu 16.6-18.7 ms, swap wait 0.5-3.4 ms, 63-257 frames over 17 ms
  a window. Per frame: stage 2.8-3.9 ms (73-122 draws), band 4.2-4.5 (125-135), rest 2.4-2.8
  (21-35 draws; particles), track 1.9-3.7, guitarist 2.2, script 0.9, band update 0.5, begin
  frame + present 0.6. About 33-35 us a draw in the scene renderer.
- Crowd on: 32.9-58.4 FPS, cpu 16.9-24.3 ms, swap wait up to 6.9 ms in the widest shots
  (~500 draws).
- **The GPU is at the limit too.** `GHVITA_GPUTIME=1` tail: 15-25 ms a frame crowd off,
  18-31 ms crowd on. The tail is not a clean GPU time (see the next point), but in normal play
  the frame time is about max(cpu, gpu): light shots cpu ≈ gpu ≈ 17 ms (58 FPS), the widest
  crowd shot gpu ≈ 30 ms (cpu 23.6 + wait 6.9; 33 FPS). So CPU work alone cannot reach 60.
- **Fragment bound, but not on shader math:** internal_res 0 (native 960x544, no off-screen
  target or blit; 720 fell back to native too) had a *lower* glFinish tail, yet in normal play
  was slower: 36-57 FPS, swap wait 4-10 ms (48% more pixels). 448 stays. Half-precision
  fragment shaders (`GHVITA_LOWP_FRAG=1`: `vglUseLowPrecision` around each fragment shader's
  compile) changed nothing (~51-52 FPS on average either way, same swap wait); off.
- Next suspect: texture bandwidth. Paletted textures keep **no mip levels** (run 7), so far
  textures are read at full size and miss the texture cache. To test: mipmaps for the venue
  textures (memory: ~+33% of the paletted size, or a mipmapped compressed format).
- **Flat-only crowd at 60 (user's question, same day):** temporary switches in the patched
  source (built with `SKIP_PATCHES=1`, removed after). Crowd on, no 3D members (every spot
  keeps its sprite): widest shot 33 → 34 FPS, crowd draw 2.5 → 0.9 ms CPU, but swap wait still
  7.3-7.8 ms; averages unchanged. Also freezing the six cut-outs after their first render:
  widest shot 36.5 FPS, cpu 20 ms, swap wait still 7.1-7.5 ms. The same shot without the crowd:
  51 FPS, swap wait ~1 ms. So **the flat sprites themselves cost the GPU ~8-10 ms** in wide
  shots (one alpha-blended, alpha-cutout quad per spot, ~90 draws, sampling 128x256 render
  target textures), not the 3D members or the refresh.
  - Note: the alpha cutout is a uniform, so every scene draw runs a shader with a `discard`
    branch, which on SGX costs hidden-surface removal even when it is not taken. A
    no-discard program variant for opaque materials is a candidate GPU win for the whole scene.
- **Patch 0042 (2026-11-17): opaque scene program.** The scene shader's alpha cutout was a
  uniform (`-1` = none), so every draw ran a fragment shader containing `discard`. A second
  program without that line now serves every draw with no cutout (identical output). vitaGL
  numbers uniforms per program (the first try found `uMvp` at a different location and
  switched itself off), so each program keeps its own location set, swapped on a change.
  Same benchmark, 28 windows of 5 s:
  - crowd off: 52.2 → **56.3 FPS** on average, swap wait 1.68 → 0.23 ms, cpu 17.6 ms both;
  - crowd on: 46.1 → **49.4 FPS**, swap wait 2.12 → 0.24 ms; widest shot 33 → 39.6 FPS
    (swap wait 6.9 → 1.1-1.4 ms).
  - The GPU is no longer the limit anywhere in this song: what is left is CPU (frames over
    17 ms per window unchanged, ~158 crowd off). Next: the CPU steps (speed hacks, particles,
    GL thread), then measure the crowd again.
  - Visual check on hardware by the user (2026-11-17, 60 FPS, crowd on): nothing different.
- **vitaGL speed hacks (2026-11-17): no gain, not used.** Read first: gd01's meshes are all
  in VBOs with 32-bit indices from a VBO, so they take the vertex layout cache's hit path, where
  `SAMPLERS_SPEEDHACK` only drops a NULL check per texture unit and `INDICES_SPEEDHACK` one
  compare; `DRAW_SPEEDHACK=2` (`SAFER_DRAW_SPEEDHACK`) only touches client-side arrays over
  32 KB (gd01 streams through a VBO). `DRAW_SPEEDHACK=1` would turn the layout cache off.
  Measured all three together (crowd off, same benchmark): 55.4 FPS, cpu 17.9 ms vs 56.3 /
  17.6 without; noise. Not split further. `tools/build-vitagl.sh` takes `VITAGL_EXTRA="..."`
  for such trials. The remaining vitaGL cost is its own per-draw work (~11 us a draw, ~4-5 ms a
  frame at ~350 draws): the GL thread is what removes it from the main thread.
- **Patch 0043 (2026-11-17): particle quads.** Temporary timers (`GHVITA_PHASES=2`, removed)
  split "venue draw: rest" (2.6 ms): particles 2.2-2.3 ms (1.6 building the quads, 0.7
  uploading ~1,000 quads, ~250 KB a frame), transparent views otherwise ~0.5, floor spot /
  extra lights / flare tests ~0. The particle code built three temporary lists per system each
  frame and uploaded a fresh index list each time. Now the quads go straight into one reused
  buffer (bit-identical vertices) and `Renderer::vitaQuadMesh` keeps the index pattern on the
  GPU. Same benchmark:
  - crowd off: 56.3 → **58.9 FPS**, cpu 17.6 → 16.3 ms, frames over 17 ms per window 158 → 53;
    rest 2.6 → 1.5 ms. (26 windows: the song started later in that run.)
  - crowd on: 49.4 → **51.9 FPS** (worst window 39.4), cpu 19.0 ms.
  - The remaining upload (~230 KB) is the 52-byte vertex (normal and bone weights unused by
    particles); a compact particle layout would roughly halve it (~0.3 ms), not done.
  - vita-loop's "cannot quit the app" before a launch is harmless (nothing running yet).
- **GL thread (2026-11-17): `vita/src/gl_thread.cpp`, no upstream patch.** Every function from
  `SDL_GL_GetProcAddress` gets a proxy (all of vitaGL's lookup table, `gl_thread_names.h`).
  State, uniforms, binds, draws, buffer data, texture uploads and deletes are recorded into an
  8 MB ring (pointer data copied); a render thread replays them. Everything else (`glGen*`,
  `glGet*`, `glReadPixels`, ...) drains the ring and runs on the game thread while the render
  thread is idle, so vitaGL is never entered by two threads at once (it ran fine driven from a
  thread other than the one that initialised it). The swap is queued too; the game thread runs at
  most one frame ahead. Draws from client memory (none in gd01) would run directly. The Vita
  layer's own vitaGL calls (shader and texture hooks, screenshots, memory reports, swap
  interval) sit under `glthread::Direct`. `GHVITA_GL_THREAD=0` turns it off. Measured lessons:
  - One wake-up per command (a semaphore signal whenever the render thread slept) and an
    unpinned game thread made it *slower* (48.6 FPS, frames up to 95 ms). Commands now go out in
    batches of 64; the game thread is pinned to core 0.
  - Both threads touching the shared read position every command cost about as much as the GL
    work moved (55.4 FPS). The game thread now checks room against a cached read position; the
    render thread publishes it every 32 commands, after a swap and at a batch end.
  - On core 1 the render thread took turns with the menu movie's decoder (menu 45 FPS; with
    a higher priority, the movie then lost a third of its pictures). It now runs on core 2
    (movie colour conversion, log writer, preview loader) at default priority - 8: menu 59.9,
    movie 133-150 of 150 pictures shown per 5 s (150 with the thread off).
  - Texture uploads are queued (the menu movie re-uploads its picture each frame: as a direct
    call each one waited ~16 ms for the render thread's vblank).
  Results, Max FPS 60, seed 1, 28 windows of 5 s:
  - I Love Rock and Roll, crowd off: 58.9 → **59.9 FPS** (worst window 59.4), game thread 16.3
    → 15.1 ms, frames over 17 ms per window 53 → **3**. Render thread ~8 ms replay a frame.
  - Smoke on the Water, crowd off: **59.9 FPS** (worst 59.4), 0-9 (mostly 1-3) frames over 17 ms
    per window. The gate (≤ 2 per window) is nearly met; the leftovers are single hitches.
  - Crowd on: 51.9 → **54.7 FPS**; half the windows 58-60; the widest shots ~39 FPS, now GPU
    bound (render thread 11.4 ms replay + 13.9 ms waiting in the swap) with the game thread at
    ~19.7 ms too. Next for the crowd: the sprites' fill cost and the crowd's CPU.
  - Main menu: 59.9 FPS either way.
  - The user checked visuals and timing feel on hardware (2026-11-17): fine.
- **Crowd at 60 (2026-11-17): patches 0044 and 0045.** I Love Rock and Roll, crowd on, worst
  5 s windows (the widest shots) as the measure. Measuring switches (args.txt), one run each:
  | Flat sprites | Worst 5 s | GPU wait (render thread swap) |
  |---|---|---|
  | as upstream (cutout, depth written; batched per block) | 39.4 | 14.5 ms |
  | none (`GHVITA_CROWD_SPRITES=0`) | 51.6 | 0.1 |
  | cutout, not blended | 39.5 | 14.3 |
  | cutout, 64x128 cut-outs | 40.2 | 13.6 |
  | cutout, no depth writes | 37.5 | 16.1 |
  | no cutout, depth written, untextured / textured | 51.4 / 51.4 | 0.1 / 0.2 |
  | no cutout, no depth writes, sorted (the correct order) | 33.8 | 16.1 |
  | the same, untextured | 38.9 | 12.6 |
  | the same, minimal fragment shader (test) | 47.5 | 7.6 |
  - The sprites cover **8-10 screens of pixels a frame** in those shots (measured, temporary
    code): the near ones are large. The cutout's `discard` made them slow, and without it (and
    without depth writes, needed for a correct look) the cost is the fragment shader per pixel:
    its uniform branches (texture / decal / untextured paths) about doubled it. The fast
    no-cutout rows are not usable: the sprites' transparent parts write depth and hide those
    behind.
  - **0044:** the scene program in four variants (cutout / no cutout / no cutout plain texture
    / no cutout untextured), picked per draw in `drawMesh`, the same results. Crowd off
    unchanged (59.9 FPS both songs).
  - **0045:** sprites without the cutout, blended, no depth writes, sorted far to near, runs
    of one block per draw (~30 draws, was ~90). A listed deviation (soft edges; later
    transparent things behind a sprite show through).
  - Result, crowd on: 54.7 → **56.0 FPS** on average, worst window 39.4 → **47.3**; most
    heavy windows are now ~50 FPS with the GPU idle and the **game thread at ~20 ms**: crowd
    update 0.5, impostors 0.9, crowd draw 2.6 (8 3D members ~85 draws + sprite runs), on top
    of band 4.3, guitarist 2.5, stage 2.8, track 2.1. Next for the crowd: game thread time.
- **Game thread in crowd shots (2026-11-17).** Option 1 (fewer 3D members at 60) was already in
  place: 4 at Max FPS 60 since 2026-11-12. A **redundant call filter** in the GL thread
  (`gl_thread.cpp`: per-program uniform values, program, texture binds, enable/disable, depth /
  blend / cull / viewport state; all forgotten after any direct call) drops ~3,950 repeated calls
  a frame: render thread replay ~8 → ~6 ms (crowd off, still 59.9 FPS), but the game thread is
  unchanged (crowd on 55.7 FPS, heavy windows ~20 ms): filtering costs about what recording did.
  - Level-2 profile with temporary timers in the scene walk (mid-song, 313 draws a frame):
    `Renderer::drawMesh` 4.4 ms (~14 us a draw: ~30 GL calls, each through the function pointer,
    the Vita hooks and the proxy), RenderState setup 2.1, mesh lookup (`build`) 1.5, lighting
    0.5, culling 0.4, the rest of the walk ~2.3. No single hot spot left.
  - Candidates: fewer GL calls per draw in `Renderer` itself (state compared there, before the
    call chain); culling whole characters that are off screen (helps close-ups, not the wide
    crowd shots); impostor refresh at 60 in more groups (~0.5 ms, choppier cut-outs).
- **Crowd-off hitches (2026-11-17), Vita layer only (no patch).** Slow frames logged with their
  phases (`GHVITA_SPIKE_MS=17.5`), long drains with their GL function (temporary code). Found:
  - `glGenVertexArrays` / `glGenTextures` mid-song (a mesh or texture made now and then) drained
    the queue: 10-17 ms each, a ~25 ms frame a few times a minute. **Name pools**: the render
    thread keeps 32-64 names of each kind (vertex arrays, buffers, textures, framebuffers),
    topped up at each swap; `glGen*` takes from them (no direct `glGen*` after loading).
  - Paletted texture uploads (`uploadPaletted`) ran under a `Direct` (~10 ms drain each). Now
    queued (`glthread::compressedTexImage2D`, data copied); the first upload still runs directly
    to check vitaGL accepts the format.
  - The log writer (core 2) sat below the render thread's priority: preempted while holding the
    queue's mutex, it made the game thread's next `SDL_Log` wait out the replay (4-8 ms after
    every logged line). Now one step above the render thread.
  - Measured runs only: the 10 s memory report drained the queue and called `vglMemFree` twice
    (7-15 ms): a 45-63 ms frame every 10 s. The periodic report has the heap only now
    (`GHVITA_GPU_MEMORY=1` adds vitaGL's free memory back).
  - Result, crowd off, Max FPS 60 (first window = venue load left out): I Love Rock and Roll
    59.9 FPS, slowest frame per window 17.2-21.9 ms (was 42-48 ms every other window plus
    ~25 ms ones); Smoke on the Water 59.9, 17.1-21.3 ms. No frame over 34 ms. Frames over 17 ms
    per window: median ~3, but 0-2 in under half the windows: heavy camera shots run at
    17.5-19 ms game thread for 10-20 frames in a row (real work, not stalls). Crowd on
    unchanged (55.5 FPS, worst window 47.2).
  - **Batched draw state, tried and dropped.** An upper-bound test (drawMesh without its
    uniforms and state: wrong picture) cut the game thread 15.2 → 12.1 ms. A batch that ran the
    same filter and packed the ~30 calls a draw into one ring command (`Renderer::vitaDrawBatched`)
    gave nothing (15.4-15.8 ms). Split: filter only 14.0, filter and recording 15.4; prefetching
    the ring and a 1 MB ring did not help. The cost moves with the render thread's replay
    (3.5 vs 7 ms in those runs): the two cores share the memory bus, so it is not the call
    chain. What would help: fewer state changes reaching GL at all (draw order by state, merged
    draws), or less game-thread work in heavy shots (guitarist animation 2.4 ms, track 2.1).
- Measuring aids added: perf line counts frames over 17 ms; `GHVITA_GPUTIME=1`;
  `GHVITA_UNHIDE=key,...` (enhanced.json may set those hidden keys); `GHVITA_LOWP_FRAG=1`;
  `GHVITA_SPIKE_MS=<ms>` (slow frame lines from that frame time; default 34);
  `GHVITA_GPU_MEMORY=1` (vitaGL free memory in the periodic memory report).

vitaGL pitfalls to remember:
- `glDrawArrays` over 49152 vertices silently draws garbage: split such draws.
- Fragment shaders get 16 uniform vec4s, vertex shaders 128.
- Don't call `glPixelStorei(GL_UNPACK_ALIGNMENT)`: only `GL_UNPACK_ROW_LENGTH` is
  supported, and any other name leaves a sticky `GL_INVALID_ENUM` that shows up on the next
  upload.
- Vita3K timings are not usable for performance; it is fine for "boots and renders".

## Hardware loop (build → deploy → run → collect)

`tools/vita-loop.sh` drives a retail Vita over Wi-Fi through **vitacompanion 1.08**
(devnoname120, MIT):
- FTP on port 1337 replaces `ux0:app/GHVT00001/eboot.bin`.
- `--install` installs the whole VPK instead (LiveArea / param.sfo changes): unpacked, uploaded to `ux0:data/ghvt_promote` and installed with vitacompanion's `promote`; saves and settings in `ux0:data/ghvita` stay (2026-11-19, ~35 s with launch and log collection).
- Commands on port 1338: `quit GHVT00001`, `nosleep on`, `launch GHVT00001`, and optional
  input such as `press cross; wait 100ms; release cross`.
- After the run it pulls `ghvita.log` / `memory.log` and any new `psp2core-*.psp2dmp`, decodes
  the dumps with `tools/parse-core.sh` against the matching ELF, and writes a perf/memory
  summary to `agents/scratch/runs/<timestamp>/`.

One-time Vita setup (the user does this):
- Copy `vitacompanion_kernel.skprx` and `vitacompanion.suprx` to `ur0:tai/`.
- Add both to `ur0:tai/config.txt`, the kernel module under `*KERNEL` and the user module
  under `*main`, then reboot.
- Install gd01 once from the VPK. Reinstall the VPK whenever `param.sfo` or LiveArea changes;
  the loop only swaps `eboot.bin`.

Usage: `wsl -d Ubuntu -u root -- env VITA_IP=<ip> bash /mnt/d/Personal/guitar-hero-vita/tools/vita-loop.sh --build --wait 90 --input 'press cross; wait 100ms; release cross'`.
`--cmd version` checks the connection.

Alternatives:
- **vita-agent-use** (shoui520, GPL-3.0, firmware 3.65 only; matches ours).
  - Adds screenshots, VPK install, CPU/FPS/memory metrics, and "deploy, run until a log line
    or crash" in one command, through a paired TLS client.
  - It lists vitacompanion as **incompatible**: load one or the other, not both.
  - Worth switching to once we need screenshots in the loop.
- **psp2-tooling** (Grarak, GPL-3.0): per-core CPU sampling profiles (to `perf.data` /
  hotspot) and Razor GPU captures.
  - Needs decrypted **DevKit 3.65 `os0:kd` modules**, which the user must obtain themselves.
  - Those modules are never fetched by an agent and never committed. Its retail-cp mode is
    tested only on a PCH-2000.
  - Optional. Phase timers (performance step 1) give most of the answer without it.

## Vita-only deviations (keep this list current)
- No Vulkan upscalers (FSR/DLSS), no HDR, no MIDI input, no launcher
- Post-FX off by default
- Button play instead of guitar
- Rear touchpad: never a screen touch; optional Rear Touch Strum (off by default, patch 0036)
- Joypad mode: a strum plays the held frets again when that hits a note, never a miss (patch 0039); the original ignores strums there
- Flat crowd sprites without the alpha cutout: blended, sorted far to near, no depth writes (patch 0045). Soft cut-out edges show faintly; something drawn later behind a sprite (a glow, a flare test) is not hidden by it. `GHVITA_CROWD_CUTOUT=1` restores upstream's drawing

## Upstream report queue

General bugs (every platform) found during the port, to report to the maintainer later in one
batch, as with #100. The user decides when; nothing is posted until they ask. Vita-only patches
are not offered.

| Found | Our patch | Bug | Status |
|---|---|---|---|
| 2026-11-14 | 0035 | Re-entering a screen with `retain_last_focus FALSE` (the store's menu) leaves the last visit's item highlighted beside the default one: `Panel::enter()` clears `focus_` before `setFocus`, so the old item's scene state stays Focused. | Fixed on the Vita (checked on hardware); not reported |
| 2026-11-16 | 0040 | Pausing a tutorial leaves only Start working: the `tut_pause` screens pause with `app set_paused` (no-op), not `game set_paused`, so `screen_` stays `Playing`, where `onAction` sends Start to the UI and drops the rest (`tutorial_ && mixer_.paused()`). Resume / restart / quit cannot be chosen. | Fixed on the Vita, works on hardware (2026-11-16); not reported |
| 2026-11-16 | 0041 | With Lefty Flip on, a joypad's fret order flips in the menus too (`Input::actionsOf`, `lefty_joypad`). The South button (orange by default) becomes green (`Fret0`, select) on top of its own `Confirm` helper, so each press selects twice: the Lefty Flip toggle switches straight back on, and `error_cant_delete_profile` re-opens as soon as Continue closes it (pop to delprof, the second select picks the current band again). East likewise backs twice. Reported by a player on Reddit. | Fixed on the Vita, works on hardware (2026-11-16); not reported |
| 2026-11-20 | 0051 | Components disabled by a script stay disabled for good: `disable_component` sets the scene state and nothing resets it when the screen is entered again. career.dta greys out `cm_store.btn` on Easy (the original's rule: no cash on Easy) but never enables it, so after an Easy career the store is locked on every difficulty until a restart. Reported by a player (irxd/guitar-hero-vita#3). | Fixed on the Vita, [verify] on hardware; not reported |
| 2026-11-14 | none | Loading the disc logs `script: unknown object or function 'class'` and `... 'new'`: the script interpreter (`script.cpp`, `Interp::command`) has no `class`/`new` command. Same two lines on the unmodified desktop build; on the Vita since the first runs (2026-11-10). No visible effect found; what the skipped scripts would do is not known. | Upstream behaviour; not ours to fix; maybe mention |

Reported earlier: #100 (script `mod` wrap, held menu direction after Bind to, GPU objects freed
with their SceneRenderer), fixed upstream in gd01 1.1.13.

## Open questions
- ~~Controls~~: the user accepted the default "thumb strum" layout (2026-11-10).
- Dev PC: Gigabyte B450 I AORUS PRO WIFI. SVM is under Tweaker → Advanced CPU Settings (or M.I.T. → Advanced Frequency Settings → Advanced CPU Core Settings on older BIOS). The user is choosing between Option A (SVM + WSL2) and Option B (WSL1, no BIOS change). Option B is acceptable: keep the toolchain and build dir inside the Linux home (`~/`), not `/mnt/d`, for speed. Upgrade later with `wsl --set-version Ubuntu 2`.
- PS TV support (DS3/DS4 controllers, 720p)? Later.
- Two players on a Vita (ds34vita): is the second DualSense dropping a ds34vita limit or ours? Not supported for now (2026-11-16).
- Is a native desktop gd01 build needed as a reference for visual and behaviour comparison? (Recommended.)

## Log
- 2026-11-10: Workspace created, upstream cloned and pinned, gd01 audited (this document). Next: Phase 0.
- 2026-11-10: SVM enabled, Ubuntu on WSL2, VitaSDK 2026.08 + SDL3 + vitaGL installed. Next: a Vita3K install, a desktop baseline build of gd01, then Phase 1 (`vita/CMakeLists.txt`).
- 2026-11-10: Desktop baseline confirmed by the user. The first Vita VPK builds (0 warnings, no upstream patches). It won't render yet. Next: vitaGL bring-up via `--wrap`, file logging, then the first run on hardware.
- 2026-11-10: Performance plan and hardware loop added (`tools/vita-loop.sh`, vitacompanion over Wi-Fi). Next: the user sets up vitacompanion, then the movie-worker build is tested through the loop.
- 2026-11-10: Hardware run 9: movie decode measured on the Vita through the loop; NEON decoder paths (patch 0007) and B-picture skipping; the intro movie now plays in sync at ~8 pictures a second. Next: faster IDCT and conversion off the decode core, then song CPU.
- 2026-11-10: Hardware run 10: song frame phases; track_fade grab dropped (patch 0010) and the glFenceSync stand-in no longer calls glFinish: songs 10-12 → 14-17 FPS. Next: crowd and character animation CPU, venue draw CPU.
- 2026-11-11: Hardware run 11: index skeleton (patch 0011): songs 14-17 → 22-34 FPS. Next: venue draw CPU per draw.
- 2026-11-11: Hardware run 12: scene renderer caches (patch 0013): songs 22-34 → 24-35 FPS. Next: GL uniform and state cost per draw.
- 2026-11-11: Hardware run 13: profiler off by default (it cost 3-4 ms), own vitaGL build, AnimPlayer parent indices (patch 0014): songs 29-43 → 34-48 FPS.
- 2026-11-11: Hardware run 14: Max FPS 30 (default) paced by vblank interval (patch 0015): songs hold 29.7-30.0 FPS; heaviest shot at the edge.
- 2026-11-11: Hardware run 15: view culling (patch 0016) saves 1-2.6 ms a frame; repeatable shots with GHVITA_SEED (patch 0017).
- 2026-11-11: Hardware run 16: iloverockandroll benchmark; crowd off at Max FPS 60 (44-56 FPS), impostors at half rate at 30 (holds 29.3-30.0) (patch 0018).
- 2026-11-11: Hardware run 17: spike logging; log writes moved to a thread and periodic reports made opt-in (they caused a hitch every 5 s); crowd members animate at half rate (patch 0018).
- 2026-11-11: Hardware run 18: merged characters built but off (GPU skinning errors / shader compiler crash); FBO scenes in flight 1 -> 4 (impostor GPU waits); lighting reuse. Widest crowd shot 137 -> 55 slow frames per 5 s.
- 2026-11-11: Hardware run 19: Performance Mode (30 / 60 No Crowd / Notes Only) and Crowd Full / Reduced; Reduced halves the widest shot's crowd (8.4 -> 4.5 ms), slow frames 64 -> 26 per run; Notes Only a locked 60.
- 2026-11-11: Crowd setting is a check: on = 8-member crowd at 30 and 60, off = none (60 + crowd 32-55 FPS, 60 without 47-58).
- 2026-11-11: Default frets changed to the user's layout: D-pad Left, L, R, Circle, Cross (patch 0004).
- 2026-11-11: Hardware run 20: song previews on a worker thread, reading only their window (no list freeze, 2 MB instead of 18-36 MB); GPU textures and meshes freed with their SceneRenderer (the heap no longer grows per song).
- 2026-11-11: Disc reads with sceIoPread: ~3x faster (song audio 5.0 -> 1.6 s, previews ~0.3 s).
- 2026-11-12: D-pad Left no longer also selects in the menus (patch 0004).
- 2026-11-12: Store lists wrap correctly when scrolling up from the first item (script `mod`, patch 0022). Checked on hardware by the user, with the D-pad Left fix.
- 2026-11-12: Enhanced Settings trimmed for the Vita (~50 rows to ~20); Shake for Whammy on the motion sensor (patch 0023).
- 2026-11-12: Upstream issue posted as irxd (the user asked): https://github.com/mbracali/prj-gd-engine/issues/100, offering three general fixes (script `mod` wrap, held menu direction after a rebind, GPU objects freed with their SceneRenderer). Prepared on a local branch `fix/gd-game-01/general-fixes` (worktree `/root/gde-pr` in WSL, from `develop` b3577a1); `--selftest` all charts fully hit. A PR only if the owner wants it; no fork yet.
- 2026-11-14: #100 resolved by the owner, no PR needed: upstream commit `2c506cb5` on `main` (gd01 1.1.13, release 1.1.15; GD01-319..321) ports all three fixes with self-tests. The rebind fix differs from ours: it clears the other player's held direction only when it isn't the releasing player's own, so two players holding a direction still work. The scene fix frees GPU objects on the local backend and releases `track2_` before the renderer. **When we bump the pin** (still `929477ca`), drop patches 0022 (`mod` wrap) and 0025 (held release) and check 0021 (free scene resources) and 0027 (free second highway) against upstream's version. The WSL branch `fix/gd-game-01/general-fixes` (`/root/gde-pr`) is no longer needed.
- 2026-11-14: Pin bumped to `6e76ea60` (gd01 1.1.13). Patches 0022 / 0025 / 0027 dropped (upstream's #100 fixes), 0021 reduced to freeing the merged character meshes, the rest refreshed (0015 and the header lines conflicted, nothing else). New patch 0034: upstream made Vulkan the default backend everywhere except macOS / Android, which included the Vita; it stays `local`. The VPK builds with 0 warnings. **[verify]** on hardware: menus, a song, six songs in a row (heap stable), Gamepads Bind to, store scrolling.
- 2026-11-14: Old WSL worktree `/root/gde-pr` and its branch `fix/gd-game-01/general-fixes` removed (upstream merged #100). Movie colour conversion on its own thread on core 2 (aiming for a full 30 pictures a second); on hardware the intro movie now shows all 30 pictures a second, 0 frames late. CapUnlocker: not needed for this port (the main thread is the bottleneck, and core 2 is idle enough for the movie conversion).
- 2026-11-14: The user checked the rebased build on hardware: songs, Gamepads Bind to and store scrolling all fine. Bug they reported: entering the store's menu again showed two highlighted items (the default first one and the one left from the last visit). Cause: `Panel::enter()` clears `focus_` before `setFocus(default)` when `retain_last_focus` is FALSE, so the old item's scene state stayed Focused. Patch 0035 resets it to Normal. Checked on hardware by the user. An upstream bug on every platform: listed in the upstream report queue, to report with others later (user's call).
- 2026-11-14: Data folder moved to `ux0:data/ghvita/` and our names renamed from gd01 to ghvita (log, VPK, ELF, threads, `GHVITA_*` env vars); an old `gd01` folder is moved on start and `config.txt`'s ISO path rewritten. Checked on hardware (runs 20261114-225700, -230138). Details under Phase 1.
- 2026-11-15: Plan caught up: Phase 1 boxes ticked (its gate is met), Phase 2 shader rewrite ticked, Vita3K dropped. Next: rear touchpad strum (Phase 4, Reddit request).
- 2026-11-15: Rear Touch Strum built (patch 0036; VPK builds, 0 warnings); the rear touchpad no longer acts as the screen. Next: the user tries it on hardware.
- 2026-11-15: Whammy on the left stick too, both layouts (patch 0037; VPK builds, 0 warnings). [verify] on hardware, with Rear Touch Strum.
- 2026-11-15: Version 0.1.0 (patch 0038), title ID GHVT00001, release script and v0.1.0 notes. Next: the user tests the release VPK on hardware.
- 2026-11-15: Hardware (user): Rear Touch Strum taps reached the game (`input: rear touch strum` in the log) but hit nothing, and the D-pad Up / Down strum never did anything either. Cause: the Vita pad plays in joypad mode (Require Strum off), where `Player::strum` returns at once (the original's joypad mode: a fret press is the hit). Patch 0039: in joypad mode a strum plays the held frets again (highest held fret, or the held chord) only when that hits a note not yet hit; otherwise nothing, so it never adds an overstrum (a strum on an already hit note would be one, ending a sustain). Title screen shows only `v0.1.0` (user: too long); the gd01 version stays in the log and the controller report.
- 2026-11-15: Hardware (user): strums work as expected with patch 0039, D-pad and Rear Touch Strum both. README gets an Install section.
- 2026-11-16: Hardware (user): left-stick whammy works; Button Layout 2 tested in a song; Career mode, tutorials and the Gamepads screen partly tested. Bug: in a tutorial's pause menu only Start worked (unpause). Patch 0040 routes the buttons to the tutorial pause screens (an upstream bug, queued). README and release notes list untested items apart from known limitations. Release held until the fix is checked.
- 2026-11-16: Hardware (user): with 0040 the tutorial pause menu moves, but Cross did not select: in a song (`Input::inGame()`, the touch lanes active) Cross is only the orange fret. 0040 now also turns the lanes off on the tutorial pause screens (`App::tutorialPauseMenu()`), so Cross is select as on the normal pause. Built and deployed, [verify] on hardware next session; then re-run `tools/release-vita.sh`.
- 2026-11-16: Hardware (user): with ds34vita and a DualSense only the DualSense worked (SDL reads port 1 first). Port 1 reads now go to port 0 (Vita's buttons plus the first external controller, player 1); a second external controller is player 2. Built and deployed, [verify] on hardware.
- 2026-11-16: Hardware (user): two DualSenses under ds34vita, the second does nothing (cannot pick a character or move a menu). `GD01_PADLOG=1` (vita_platform.cpp: each port's button changes, plus the raw ports 1-4) shows raw port 1 = DualSense 1, port 2 succeeds but its buttons stay 0 the whole run, ports 3-4 not connected. So ds34vita lists the second DualSense on port 2 but passes none of its input; nothing the game can fix. Next: try a DS4/DS3 as the second controller, or another two-controller homebrew, to see if it is ds34vita with two DualSenses. Same day, after a Bluetooth restart and a LiveArea start (no Wi-Fi traffic from our tools): DualSense 2 works on port 2 (menu, a couple of character picks), then port 2 goes silent for good during character select, right as the character models load (the "index skeleton" lines); after that it does not work on the home screen either until Bluetooth is restarted. The first report stopped at guitar select, also a loading point. So ds34vita stops reading the second DualSense, apparently under load; the game only reads port 2 (no thread priorities of ours are raised). ds34vita 1.1 has no issue tracker reports on it. Also open: SDL names every Vita port "PSVita Controller", so both pads share one GUID and the Gamepads screen's per-device settings (player, type, rebinds) apply to both.
- 2026-11-16: Hardware (user): with Wi-Fi off the second DualSense still stops, sometimes already on the main menu. User's call: two-player (face-off) marked **not supported yet** in the README and v0.1.0 notes (Known limitations), cause (ds34vita or the port) still open. Next, when picked up: a DS4 as the second controller, another two-player homebrew with two DualSenses, and the shared "PSVita Controller" GUID.
- 2026-11-16: Hardware (user): Cross selects in the tutorial pause menu (0040 verified); with one DualSense on ds34vita, the Vita's own buttons and the DualSense both control player 1. Release rebuilt with `tools/release-vita.sh`.
- 2026-11-16: Released v0.1.0 (alpha): https://github.com/irxd/guitar-hero-vita/releases/tag/v0.1.0 (pre-release; VPK sha256 d389aa97…c74e, source bf3c9a81…62da). Rebuilt from c1c309d first (the user's README edit came after the earlier build).
- 2026-11-16: VitaDB listing postponed until a more stable release (user's call). Submit via https://www.rinnegatamante.eu/vitadb/#/submit (account needed); no game-art screenshots (LiveArea bubble only).
- 2026-11-16: First player bug report (Reddit, v0.1.0): Lefty Flip could be turned on but not off, Data Settings → delete the only band left the "can't delete" error with no way out, and the notes reportedly stayed green-on-the-left with Lefty Flip on. The game's own scripts (options / mem_card / error, decoded to the scratchpad only) call `options set_lefty` like Stereo, and the error's Continue pops back to delprof. Cause of the first two: the lefty fret flip in the menus turns Cross into a second select (table above). Patch 0041 flips the frets only in a song. Built and deployed, [verify] on hardware: the toggle turns off, the delete error closes, and with Lefty Flip on a song shows green on the right (the mirroring code, `TrackRenderer::laneX`, is unchanged by our patches; not yet seen on hardware).
- 2026-11-16: Hardware (user): 0041 verified. Lefty Flip turns off again, the "can't delete band" error closes with Cross, and a song with Lefty Flip on shows green on the right. (The first test ran the old eboot: `tools/vita-loop.sh --no-launch` also skips the upload; deploy with a launch, or check the eboot's sha256 on the Vita.)
- 2026-11-16: v0.1.1 (bug fix: 0041, Lefty Flip): version 0.1.1, APP_VER 01.01, notes docs/releases/v0.1.1.md (also link the bug report form). Built with `tools/release-vita.sh`.
- 2026-11-16: Released v0.1.1 (alpha): https://github.com/irxd/guitar-hero-vita/releases/tag/v0.1.1 (pre-release, tag v0.1.1 on 9294393; VPK sha256 afedd85d…10e6, source f33a9adc…d0e). The Lefty Flip fix (0041) for the first player report.
- 2026-11-16: v0.2.0 goal set (user): a stable 60 FPS without the crowd, with it if possible. Plan under "v0.2.0: 60 FPS" (baseline + GPU gate, vitaGL speed hacks and particles, GL on its own core, then the crowd). Perf line now counts frames over 17 ms; `GHVITA_GPUTIME=1` times the GPU tail after each swap. Built (0 warnings), not yet run on hardware.
- 2026-11-16: Hardware run 21 (baseline for v0.2.0): at 60 FPS the GPU is at the limit as well as the CPU (light shots both ~17 ms, widest crowd shot GPU ~30 ms). Native resolution is slower than 448 in play (fragment bound); half-precision fragment shaders gain nothing. Next: test mipmaps on venue textures (bandwidth), then the CPU steps. Vita's enhanced.json restored to the user's (max_fps 60).
- 2026-11-16: Flat-only crowd at 60 tested (user's idea): little gain; the sprites themselves cost the GPU ~8-10 ms in wide shots. Experiment switches removed; build back to the committed state (the Vita still runs the experiment eboot until the next deploy; its switches are off unless set in args.txt).
- 2026-11-17: Patch 0042, opaque scene program (no discard for draws without the alpha cutout): GPU no longer the limit; crowd off 52.2 → 56.3 FPS, crowd on 46.1 → 49.4 (widest shot 33 → 39.6). The user checked visuals on hardware (60 FPS, crowd on): nothing different. Next: CPU.
- 2026-11-17: vitaGL SAMPLERS / INDICES / SAFER_DRAW speed hacks, measured together: no gain (55.4 vs 56.3 FPS crowd off), not used; `tools/build-vitagl.sh` takes `VITAGL_EXTRA`. Vita back on the normal build and the user's enhanced.json. Next: particles, then the GL thread.
- 2026-11-17: Patch 0043, particle quads (reused buffer, index pattern kept on the GPU): crowd off 56.3 → 58.9 FPS (cpu 17.6 → 16.3 ms, frames over 17 ms 158 → 53 a window), crowd on 49.4 → 51.9. Vita runs this build; enhanced.json back to the user's (crowd on, fps counter). Next: the user's visual check, then the GL thread.
- 2026-11-17: GL thread (vita/src/gl_thread.cpp): crowd off 58.9 → 59.9 FPS in both gate songs (frames over 17 ms ~1-3 a window), crowd on 51.9 → 54.7 (widest shots GPU bound, ~39). Menu 59.9. Vita runs this build; enhanced.json is the user's (crowd on, fps counter). Next: the user's check (visuals, timing feel), then the crowd.
- 2026-11-17: Crowd: patches 0044 (scene program variants) and 0045 (flat sprites without cutout, blended far to near): crowd on 54.7 → 56.0 FPS, worst window 39.4 → 47.3; crowd off unchanged 59.9. Heavy crowd shots are now game-thread bound (~20 ms). Vita runs this build; enhanced.json is the user's (crowd on, fps counter). Next: the user's visual check of the crowd, then the game thread in crowd shots.
- 2026-11-17: GL thread redundant call filter (~3,950 calls a frame dropped; render replay ~8 → ~6 ms; game thread unchanged). Scene walk profiled (see the v0.2.0 section). Vita runs this build; enhanced.json is the user's (crowd on, fps counter).
- 2026-11-17: Crowd-off hitches: glGen* name pools, queued paletted uploads, log writer above the render thread, periodic memory report without vglMemFree. Slowest frame mid-song 42-48 ms → ≤ 22 ms in both gate songs (59.9 FPS); runs of 17.5-19 ms frames remain in heavy shots. Batched draw state tried, no gain, dropped. Vita runs this build; enhanced.json is the user's (crowd on, fps counter).
- 2026-11-19: Issue #1 (black screen loading a song, v0.1.0, 30 FPS with crowd, once): the log stops at song select previews after 7 song loads, with no error and no allocation failure (the async log may have lost its last lines). Only the first frame had a memory line, so a leak across loads could not be checked. Patch 0046 logs memory at the start and end of each song load. The reporter has not seen it on v0.1.1. Next: in a later release, compare the "song ready" lines across many loads.
- 2026-11-19: Issue #2 (PS2-style frets: L2 L1 R1 R2 Cross). (1) Patch 0047: the rear touchpad halves are bindable pad buttons Rear Left / Rear Right (SDL paddles), active in a song and during a binding capture only; the 15% side edges stay dead [verify grip on hardware]. (2) PS TV DualShock L2 / R2: SDL 3.4.16 maps the triggers from `lt` / `rt` only; the digital L2 / R2 bits (joystick b12 / b13) are now added to the triggers by `SDL_AddGamepadMapping` (vita_platform.cpp). Whether `sceCtrlPeekBufferPositive2` fills `lt` / `rt` or the L2 / R2 bits on a PS TV is **[verify]** (no PS TV here): the reporter's controllers-report.txt after pressing L2 / R2 shows the raw events. DualSense: not supported by the Vita or PS TV system software [verify for plugins]. Built (0 warnings), not yet run. Patch 0048: Button Layout 3 (PS2): L2 / Rear Left, L, R, R2 / Rear Right, Cross, strum on D-pad Up / Down; the help box is taller for three layouts plus a note [verify that it fits on screen]. Next: hardware check (rear halves, grip edges, layout help), then a PS TV check by the reporter.
- 2026-11-24: Issue #6 (PS TV, v0.2.0: C2-12828-1 after ~1 h of play, then a "GPU crash" that turned the console off a few minutes after relaunch). v0.2.0 rebuilt from `dist/ghvita-v0.2.0/` source tarball: eboot.bin byte-identical to the shipped VPK, so the dumps decode exactly.
  - **Dump 1 (C2-12828-1), root cause found:** data abort on the `ghvita gl` thread in vitaGL `glBeginQuery` (`ldrb r1,[r6,#8]`, r6 = 0, target `GL_ANY_SAMPLES_PASSED`). The lens flare occlusion queries (`SceneRenderer::resolveFlares` → `Renderer::testDepthsAsync`, ES path) are generated per venue into `FlareBatch::queries` (3 batches, kFlareLag) and **never deleted**: `~SceneRenderer` frees textures and meshes but not queries. vitaGL has a fixed pool of 128 queries (`MAX_QUERIES_NUM`); when it is empty, `glGenQueries` silently leaves the ids at 0 (the vector's value-initialised slots), and `glBeginQuery(…, 0)` dereferences NULL. So the pool drains song by song until a crash: fits "~1 hour". Upstream leaks the query objects on every ES platform too (crash only where the pool is fixed): a candidate general fix. Not yet fixed; still present in v0.2.2.
  - **Dump 2 (GPU crash):** no thread crashed (gl thread waiting, main thread in the kernel): a GXM / GPU fault, so the dump has no stack to read. The log (from this session only; the 1 h session's log was overwritten on relaunch) ends right after the band characters loaded for song 2 (morethanafeeling), with vitaGL free VRAM 5 MB and free RAM 0 MB at "song ready". Lead: VRAM exhaustion **[verify]**.
  - **Texture counts in the memory line are cumulative uploads** (`gPaletteCount` / `gRgbaCount` are never decremented on delete), so 508 → 828 is not a leak signal. Free VRAM at "song load" (no venue loaded either time) went 66 → 50 MB across one song: a leak, vitaGL's deferred garbage collection, or menu/preview caches; two samples cannot tell. Query leak fixed by patch 0052 (2026-11-24): `~SceneRenderer` deletes `FlareBatch::queries`, and ids of 0 are never begun or read (logged once as "occlusion query pool empty"). Built, 0 new warnings. **Verified on hardware (2026-11-24, run 20261124-071458, 4 songs: sharpdressedman twice, heartfullofblack, godzilla):** the patch logs "N occlusion queries created / freed (M live)". sharpdressedman's venue creates 18 queries (so before the fix the pool of 128 ran out on about the 8th such venue load), heartfullofblack 3, godzilla 18. Each venue's queries are freed when it goes, back to **0 live** every time. No crash, no "pool empty" line. Side finding: the `--song` dev path ends through the engine's own Results screen, whose `backToMenu()` keeps the venue loaded in the menus (free VRAM 0 MB, heap peak 229 / 256 MB at the next song load). Players leave through the disc menus (`leaveGame`, venue freed: 36-37 MB free at load), so only the test harness hits this. Next: make the memory line report live textures; loop one venue N times (Vita3K is enough for vitaGL's accounting) and watch free VRAM at "song load".
- 2026-11-19: LiveArea gate without the black card (user's request): transparent startup.png with only the lettering. Then, from a homebrew VPK the user supplied (not kept): template style `ad0` with no `<gate>` (no Start button) and the image as a frame1 live item targeting `psla:eboot` (tap to start); ours now does the same. Where frame1 sits in `ad0` (centring) [verify on the Vita]. Installed on the Vita by vitacompanion `promote` of the unpacked VPK (ux0:data/ghvt_promote). Build with patches 0046-0048 on the Vita for the user's test.
- 2026-11-19: Patch 0049: Vita defaults Performance Mode 60 FPS, Crowd off (user's request); README performance table now has the 2026-11-17 60 FPS figures. Existing enhanced.json files keep their values. DualSense through ds34vita on the Vita handheld: L2 / R2 work (user's check, 2026-11-19; with the trigger mapping in vita_platform.cpp). A PS TV with a DualShock is still unchecked.
- 2026-11-19: Version 0.2.0 (APP_VER 02.00) and its release notes (docs/releases/v0.2.0.md): 60 FPS default, crowd speed-ups, Button Layout 3, rear touchpad buttons, L2 / R2, LiveArea, song-load memory lines.
- 2026-11-19: Released v0.2.0 (alpha): https://github.com/irxd/guitar-hero-vita/releases/tag/v0.2.0 (pre-release, tag v0.2.0 on 922eb2f; VPK sha256 186ca916…727a, source 9a117f2e…6ec9).
- 2026-11-20: Bug (user): Circle paused the song before the notes appeared, and at any time in Button Layout 3. Cause: `Input::update` sends Circle as the built-in Back helper whenever Circle is not a fret in a song (patch 0004 only drops it while the lanes are on, i.e. once the highway shows; Layout 3 has no Circle fret), and `App::onAction` pauses on Back or Start. Patch 0050: on the Vita the song screen ignores Back; Start (buttons and the touch pause button) still pauses. Built and deployed to the Vita, [verify] on hardware.
- 2026-11-20: Hardware (user): ○ no longer pauses a song (patch 0050 verified). Version 0.2.1 (APP_VER 02.01) and its release notes (docs/releases/v0.2.1.md): a bug-fix build with patch 0050.
- 2026-11-20: Released v0.2.1 (alpha): https://github.com/irxd/guitar-hero-vita/releases/tag/v0.2.1 (pre-release, tag v0.2.1 on 8ef4d4a; VPK sha256 2d61a86f…b955, source 7542c71f…7d7f).
- 2026-11-20: Bug (issue #3, player): after an Easy career the store stayed greyed out on every difficulty until a restart. Cause: career.dta's `enter` runs `disable_component cm_store.btn` on Easy (the store is meant to be closed on Easy only; cash is hidden there too) and has no matching `enable_component`; `PanelObj` keeps scene states across visits. The other `disable_component` users (main.dta poll, options.dta / tutorials.dta / career.dta enter, `init_pic_state`) all set the state again on enter or every poll. Patch 0051: on the Vita, `enter()` enables again the components its script disabled, before the `enter` script runs. Builds with no warnings; [verify] on hardware: Easy career (store grey), back out, Medium career (store opens).
- 2026-11-20: Version 0.2.2 (APP_VER 02.02) and its release notes (docs/releases/v0.2.2.md): a bug-fix build with patch 0051 (issue #3). Released at the user's request before the hardware check.
- 2026-11-20: Released v0.2.2 (alpha): https://github.com/irxd/guitar-hero-vita/releases/tag/v0.2.2 (pre-release, tag v0.2.2 on f34b718; VPK sha256 314eead3…f562, source c2ae6cc7…24fc). [verify] on hardware: store after an Easy career.
- 2026-11-24: The memory line counts **live** textures (`gLiveTextures` in `vita/src/vita_platform.cpp`: added at a level-0 upload, removed at delete; level-0 size only), not every upload ever made. Checked on hardware: at sharpdressedman "song ready" the live textures are 6 MB paletted + 1 MB RGBA while vitaGL's free VRAM fell 77 → 51 MB, so most of a venue's VRAM is not textures (meshes, buffers **[verify]**). Lead for the issue #6 GPU crash.
- 2026-11-24: Version 0.2.3 (APP_VER 02.03) and its release notes (docs/releases/v0.2.3.md): a bug-fix build with patch 0052 (issue #6, verified on hardware) and the live texture memory line.
- 2026-11-24: Released v0.2.3 (alpha): https://github.com/irxd/guitar-hero-vita/releases/tag/v0.2.3 (pre-release, tag v0.2.3 on 56cd420; VPK sha256 8d11915e…2f18, source 5643af14…82b4). Issue #6 reply drafted for the user to post; GPU crash still open (lead: non-texture VRAM per venue).
- 2026-11-24: Issue #6 GPU crash, investigation (not solved):
  - **Not a plain out-of-memory.** vitaGL falls back to the newlib heap when its pools are full (`use_extra_mem` is on by default; `gpu_alloc_mapped_aligned_for_gpu_inner`: VRAM → RAM → PHYCONT → BUDGET → newlib), and the reporter had ~118 MB of heap free at song 2 "ready". Our hardware session ran at 0 MB free VRAM and heap peak 229 MB (the `--song` path) without a GPU crash. If an allocation does fail, vitaGL's `glNamedBufferData` returns with the buffer at address 0 (error handling on, `NO_DEBUG=1` only turns logging off), and the next draw reading it would fault the GPU: the mechanism stays possible, but needs the heap full too.
  - **Not reproduced:** morethanafeeling with `--character classic` (the reporter's song 2) on a fresh start played through on our Vita (run 20261124-073705).
  - **Dump 2, all threads** (scratch script over vita-parse-core, symbolized against the byte-identical v0.2.0 ELF): the `ghvita gl` thread waits inside `vglSwapBuffers` (gxm.c:972, display queue); stack leftovers show a `glBlitFramebuffer` replay before it. The main thread waits in `glthread::Direct` for `glGetQueryObjectuiv`, from `Renderer::depthsPassed` ← `SceneRenderer::resolveFlares` ← `VenueRenderer::draw`. So the GPU stopped in a frame, and nothing on the CPU names the draw. The `GPU_INFO` note holds an SGX register dump (undocumented layout) with values `0x62011058` and `0x620120e8` that look like addresses, outside every dumped segment **[verify]** whether either is a fault address.
  - **Reporter's session differs:** song 1 "ready" had 18 MB free VRAM (ours 51 MB), and 29 MB of RGBA textures had been uploaded in the menus before song 1 (ours ~0). Likely career / character select (character preview models, patch 0003) **[verify]**. Their settings are unknown, and the crash came minutes after relaunching from crash 1 on a PS TV.
  - Next: reproduce their flow on hardware (career or quickplay through character select, sharpdressedman then morethanafeeling, watching the memory lines); ask the reporter for their Enhanced Settings and a v0.2.3 log.
- 2026-11-24: Issue #6 follow-up, **career graphics fault** (user, hardware, v0.2.3 + career mode; screenshots kept out of git): on the `fest` stage, large axis-aligned areas behind the crowd (sky, backdrop, Toxic sign) render solid black or white, changing from frame to frame; later draws (flags, scaffolding, the 3D crowd) still appear on top. Edge at x = 311 in three frames of the same camera, so an object's edge, not a GPU tile.
  - Career log (run 20261124-081425): free VRAM at song load 63 → 57 → 51 → 46 MB over 5 songs, live textures at song load 278 → 307 → 328 → 351 paletted, 51 → 58 → 72 → 84 RGBA. A failed song's retry loads the new venue before the old one is freed: VRAM 0, vitaGL RAM 0, heap peak 213 / 256 MB. eureka "ready" in career: 5 MB free, 525 live paletted; fresh `--song eureka --at fest`: 69 MB, 250.
  - Not reproduced fresh: `fest` + eureka with the default parameter buffer and with `GHVITA_PARAM_MB=32` rendered correctly, and so did a run with `GHVITA_EAT_VRAM_MB=64` (new test switch: holds N MB of VRAM; ready at 5 MB free, first frame at 0 MB VRAM / 0 MB RAM). So low memory alone does not cause it, and the parameter buffer is not it.
  - Leak found: the crowd's 6 impostor render targets (128x256 texture + depth renderbuffer + FBO each) were never freed, so every venue left 6 behind. Fixed by patch 0053. vitaGL limits: 256 FBOs (`BUFFERS_NUM`); 47 render targets (`MAX_RENDER_TARGETS_NUM`, shared up to 8 FBOs each for widths <= 256; with `RECYCLE_RENDERTARGETS` the oldest is destroyed and reused when full). The leak alone would take dozens of venues to fill those, so it is **not shown** to cause the fault. Also leaking: `SceneRenderer::replaceTexture` never deletes the texture it replaces (track surface, career status / winner photos).
  - New diagnostics (`vita/src/vita_platform.cpp`): at each song load, "live textures by era" (era 0 = boot and menus, then one era per song load / ready line) and the commonest sizes among textures left from earlier songs. Next: a career session on this build, noting the first song where the fault appears, to see what still leaks and whether the fault remains.
- 2026-11-24: Career session on patch 0053 (user, hardware, run 20261124-085048; 7 song loads incl. one retry): **no graphics fault seen**. Free VRAM at song load 60 → 56 → 56 → 56 → 53 MB (was falling ~6 MB a song), live textures at song load flat at 358-359 paletted / 63 RGBA, "ready" at 21-24 MB free (was 5-13). Era lines: after song 1 the same 66 textures (< 1 MB, eras 1-2, mostly 32x64 / 64x64) stay and nothing grows. **Retry still spikes:** behindthemask's retry reached VRAM 0 / RAM 0 and heap 176 MB at "ready" (130 normally): the old venue's GPU memory is still held during the new load, likely because vitaGL frees GPU memory only some frames after release (`FRAME_PURGE_FREQ`) and a retry loads with no frames in between **[verify]**. Next: on Vita, flush vitaGL's deferred frees after `venue_.reset()` on a retry.
- 2026-11-24: Patch 0054 + `vita::flushGpuFrees()` (retry spike). Waits for the GL thread and the GPU (`glFinish`), then runs vitaGL's garbage collector over its 4 frame slots through its own semaphores (`gc_mutex`, a vitaGL internal: pinned e1473ff, recheck on update). Verified on hardware (run 20261124-091300): an aceofspades restart flushed 3 → 33 MB free VRAM in 109 ms, "ready" at 14 MB / heap 129 MB (the retry before the fix: 0 MB / 176 MB). The next song after `--song` (venue kept by `backToMenu`) flushed 10 → 40 MB in 82 ms. No hang. Note: that session began with the test's `--song --autoplay`, which keeps autoplay on for the whole session whatever Enhanced Settings says (the user's "autoplay won't turn off"); not a game bug, but vita-loop test runs must not be left running for the user (`--keep-running` only when collecting).
- 2026-11-24: Version 0.2.4 (APP_VER 02.04) and its release notes (docs/releases/v0.2.4.md): patches 0053 (crowd render targets) and 0054 (retry GPU frees), the texture era lines, and the test-only `GHVITA_EAT_VRAM_MB` switch. Both fixes verified on hardware in career sessions.
