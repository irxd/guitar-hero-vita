# gd01 → PS Vita: port plan

Last updated: 2026-11-10

## Upstream pin

| | |
|---|---|
| Repo | https://github.com/mbracali/prj-gd-engine |
| Commit | `929477cafaa3675a309acf48c9e573cc563d906b` (2026-11-09, "Merge pull request #75 from mbracali/develop") |
| gd-game-01 version | 1.1.11 (`game-info.yaml`: ready_to_play, completeness 0.99) |
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
- Paths: `platform/paths.cpp` → `ux0:data/gd01/`.

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
- [ ] Install the Vita3K emulator. Get `libshacccg.suprx` onto the console and into Vita3K.
- [x] Hacked Vita ready: FW 3.65 + Ensō (version spoofed to 3.74, cosmetic only). `libshacccg.suprx` is installed in both `ur0:data/` (where vitaShaRK looks) and `ur0:data/external/` (where vitaGL r1488 looks; both checked with `strings` on the libraries).
- [x] GH1 PS2 disc ripped: `original_isos/Guitar Hero (USA).iso` (2,300,313,600 bytes)
- [x] Confirm a native Linux build of upstream gd01 runs with the user's ISO. 2026-11-10: `tools/build-desktop.sh` builds the unmodified upstream for Linux in WSL, and `tools/run-desktop.sh` runs it through WSLg. The user confirmed the menus work and a song plays. Rendering is software (llvmpipe, GL 4.5 Mesa 26.0.8), audio goes through PulseAudio.

### Phase 1: spike, "hello highway" (upstream estimate 1–2 weeks)
- [x] `vita/CMakeLists.txt` builds the **unmodified** upstream sources with VitaSDK (2026-11-10). It produces `gd01.vpk` (2.9 MB) with **0 warnings** and **no upstream patches so far**.
  - gd-engine-core can't be dropped: `renderer.h` → `core_renderer.h` → `gd/core/render/device.h`, and `app.cpp`/`mixer.cpp` use `gd/core/audio/output.h`. So we build a **reduced gdcore**: `gdcore_render` (device.cpp, spirv_reflect, gl backend; `GDCORE_RENDER_SDLGPU=0 VULKAN=0`) and `gdcore_audio` (output.cpp only, no miniaudio/OpenAL/FAudio backends).
  - The SPIR-V headers (`core_renderer_spirv.h`, `core_post_fx_hdr_spirv.h`) come from host glslang in the desktop build. They're platform-independent data, so `tools/build-vita.sh` copies them from `/root/build/gd01-linux`. The desktop build must run first.
  - `vita/compat/dlfcn.h`: `output.cpp` uses `dladdr` only to find the module's folder, so the stub reports failure (no dynamic loader on Vita).
  - `midi_in.cpp` compiles as its stub (no `GD_RTMIDI_API`). `vk_upscaler_stub.cpp` replaces the Vulkan upscaler, as on Android.
  - The icons are GDE's own art (`res/gd01_icon*.png`), embedded with upstream's `embed_file.cmake`.
  - Title ID placeholder: `GDDE00001`.
- [x] **vitaGL bring-up, built (2026-11-10), untested on hardware.** `vita/src/vita_platform.cpp` plus `-Wl,--wrap=` for 10 SDL symbols. Still no upstream patch.
  - `SDL_CreateWindow` → 960x544 without OPENGL/RESIZABLE/HIGH_PIXEL_DENSITY. `SDL_GL_CreateContext` → `vglInitExtended(0, 960, 544, 16 MB, no MSAA)` once. `SwapWindow` → `vglSwapBuffers`. `GetProcAddress` → `vglGetProcAddress`. MakeCurrent, DestroyContext and SetSwapInterval are no-ops. ExtensionSupported checks vitaGL's GL_EXTENSIONS.
  - GL audit: gd01 + gdcore look up 86 GL functions. vitaGL r1488 exports all except `glFenceSync`, `glClientWaitSync`, `glDeleteSync` (stand-ins: glFinish, then "already signalled") and `glQueryCounter`, `glGetQueryObjectui64v` (stand-ins: no timings, 0). Each stand-in logs once.
  - vitaGL reports "OpenGL ES 2.0 VitaGL", so `gl.cpp` takes its GLES path (shader `#version 330 core` → `#version 300 es`). **[verify]** how vitaGL's GLSL translator handles GLSL ES 3.00 (`in`/`out`, `layout`, `flat`, `texelFetch`).
  - Paths: `SDL_GetBasePath` and `SDL_GetPrefPath` → `ux0:data/gd01/`. `loadRememberedPath()` finds an `.iso` in the base path, and config, career and enhanced settings go there too.
  - Log: a constructor before `main` sets `GD01_LOG=ux0:data/gd01/gd01.log`. main.cpp's `openLogFile` flushes every line, so a crash still leaves the log up to the last line.
  - Clocks: CPU 444, bus 222, GPU 222, xbar 166 MHz. newlib heap is 192 MB **[verify/tune]**.
  - Useful later: `runShaderTest()` (app.cpp) compiles every program and lists failures. It's only reachable through a command-line flag today; expose it on Vita (e.g. hold a button at boot).
- [x] **First hardware runs (2026-11-10):**
  1. vitaGL starts: "SGX543MP4+ / OpenGL ES 2.0 VitaGL", 83 MB VRAM free. The "OpenGL logo" the user saw is vitaGL's own splash thread. The game crashed right after `OpenGL: ...`.
  2. With shader tracing: both shaders of the first program report compile "ok", then `glLinkProgram` crashes. The core dump (`tools/parse-core.sh`) shows a data abort in SceGxm, with LR in `glLinkProgram+0x7fa` right after `sceGxmProgramGetParameterCount(NULL)`. So vitaGL turned the GLSL ES 3.00 sources into **null GXM programs while reporting success**.
- [ ] **Fix built, untested on hardware:** `vita/src/glsl_es100.cpp` rewrites every shader to GLSL ES 1.00 before vitaGL sees it. `layout(location)` inputs become `attribute`s, bound with `glBindAttribLocation` before the link (hooks on glCreateShader/glAttachShader). in/out become varying, the fragment output becomes `#define … gl_FragColor`, and `texture*()` becomes the `texture2D*()` forms. The two traced shaders pass `glslangValidator` as ES 1.00 (`tools/check-shaders.sh`). Things ES 1.00 lacks (`flat`, `texelFetch`, `uint`, MRT) are logged per shader. Expect those to need more work.
- [x] **Hardware run 3 (2026-11-10): the game boots to the menus.** The GLSL ES 1.00 rewrite works: the first program (1 vertex + 1 fragment shader) compiles and links. Audio comes up as "vita" (1024 frames at 44.1 kHz), the ISO loads (**56 songs**), and the controller is detected ("PSVita Controller"). The timer-query stand-ins are hit in the menus.
- [ ] **Quick play crashes: out of memory.** `char_multi: character nu_metal: std::bad_alloc`, then a data abort in SceLibKernel (the dump's stack only has stale vitaGL values). Source: the character select preview (`ui/select_panels.cpp:~265-300`) caches **up to 6 full Character models** (meshes, clips, textures) at once. That's fine on desktop, too heavy for Vita: this is the Phase 3 "memory diet", hit early.
  - Build 4 (untested): extended memory mode (`ATTRIBUTE2=12` via `VITA_MKSFOEX_FLAGS`), newlib heap 192 → 256 MB. A memory report goes into gd01.log on the first frame and every 10 s (heap used/peak, vitaGL free VRAM/RAM). malloc/calloc/realloc/memalign are wrapped: a failure goes to `ux0:data/gd01/memory.log` with its size, written without allocating (sceClib + sceIo).
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
- [x] **Hardware run 10 (2026-11-10, song profiling through the loop):** `--song smokeonthewater --autoplay` now starts a song with no input: `vita/src/vita_platform.cpp` wraps `main` and reads one-shot arguments from `ux0:data/gd01/args.txt` (`tools/vita-loop.sh --args`; `NAME=value` words become environment variables, e.g. `GD01_VITA_DISPLAY_SCENES`, `GD01_VITA_PARAM_MB`). Frame phases (patches 0008/0009, `vita_profile.h`) log ms per frame, draw calls, time inside vitaGL draw calls and framebuffer readbacks per phase.
  - Before: 10-12 FPS, ~90 ms a frame. The track's `track_fade` grab (`glCopyTexSubImage2D`) waited 21-23 ms for the GPU to finish the venue: **patch 0010** drops it on Vita (deviation: no fade at the highway's far end).
  - The wait then moved to just after the swap: the **`glFenceSync` stand-in called `glFinish()`**, and `Renderer::endFrame` makes a fence every frame, so CPU and GPU never overlapped. It is now a no-op (vitaGL's triple buffering already bounds frames in flight). **Result: 14-17 FPS, 58-70 ms CPU a frame, swap wait 0.1-0.2 ms** (448 internal resolution and native perform the same; 448 stays the default). More GXM scenes per render target made no difference.
  - Remaining per frame (ms): venue crowd 17-21, venue draw 17-26 (only 3.5-5 inside vitaGL; 290-440 draws), guitarist 9, band 7, track 5, venue script 1.2. `Character::poll` (skeletal animation) is behind guitarist, band and most of the crowd.
  - Desktop callgrind of a whole song does not work (valgrind never got past loading); use on-device phases and micro-benchmarks.
  - **Song CPU (next):** 80-110 ms per frame with 300-700 draws. Profile on hardware (psp2-tooling) or by phase timing: scene/venue update, skinning, crowd, draw submission through vitaGL.
- FPS: the engine's own counter is under Options → Enhanced Settings → PERFORMANCE & GRAPHICS → "Show FPS Counter" (`fps_counter` in `ux0:data/gd01/enhanced.json`). `GD01_FRAMELOG` logs the fps every half second.
- Tooling: `tools/parse-core.sh` decodes `ux0:data/psp2core-*.psp2dmp` (the dump must come from the same build). It runs vita-parse-core (Python 2 code) through `tools/parse_core_py3.py` in a venv with pyelftools 0.29. The user approved running this external tool on 2026-11-10.
- [ ] GL shim: bind gl.cpp's pointers to vitaGL, and rewrite the shaders for the highway / track only
- [ ] Load the ISO from `ux0:data/gd01/`, load one song chart, draw the highway, play the audio
- [ ] Run it in Vita3K, then on hardware. Measure fps and RAM.
- **Gate (from upstream):** highway alone runs at 60 fps. Highway + venue runs at ≥ 30 fps. Total RAM ≤ 300 MB.

### Phase 2: render profile (3–4 weeks)
- [ ] Rewrite all remaining shaders: scene, venue, crowd, characters, UI, text
- [ ] Post-FX off by default. Remove the float targets and MRT.
- [ ] Shader cache on `ux0:`

### Phase 3: memory diet (2–3 weeks)
- [ ] Stream the venue archives, free decoded data after GPU upload, cap the texture cache
- [ ] Try P8 (paletted) textures for the PS2 palettised data

### Phase 4: Vita polish and release (1–2 weeks)
- [ ] Input map, default "thumb strum": Green = L, Red = □, Yellow = △, Blue = ○, Orange = R; strum = D-pad up/down; Star Power = Select (+ optional tilt via the motion sensor); whammy = right stick; pause = Start. Optional alternatives: front-touch fret zones, rear-touch strum. All remappable via the GAMEPADS screen. Labelled as a "handheld mode".
- [ ] FMV: keep it if ≥ 30 fps, otherwise skip it with a log line
- [ ] Our own LiveArea assets. Credits screen shows the attribution line.
- [ ] `CHANGES.md`, a VPK build script, GitHub release with source
- [ ] Contact the maintainer, offer the `patches/`, list on VitaDB if they agree

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
- FTP on port 1337 replaces `ux0:app/GDDE00001/eboot.bin`.
- Commands on port 1338: `quit GDDE00001`, `nosleep on`, `launch GDDE00001`, and optional
  input such as `press cross; wait 100ms; release cross`.
- After the run it pulls `gd01.log` / `memory.log` and any new `psp2core-*.psp2dmp`, decodes
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

## Open questions
- ~~Controls~~: the user accepted the default "thumb strum" layout (2026-11-10).
- Dev PC: Gigabyte B450 I AORUS PRO WIFI. SVM is under Tweaker → Advanced CPU Settings (or M.I.T. → Advanced Frequency Settings → Advanced CPU Core Settings on older BIOS). The user is choosing between Option A (SVM + WSL2) and Option B (WSL1, no BIOS change). Option B is acceptable: keep the toolchain and build dir inside the Linux home (`~/`), not `/mnt/d`, for speed. Upgrade later with `wsl --set-version Ubuntu 2`.
- PS TV support (DS3/DS4 controllers, 720p)? Later.
- Is a native desktop gd01 build needed as a reference for visual and behaviour comparison? (Recommended.)

## Log
- 2026-11-10: Workspace created, upstream cloned and pinned, gd01 audited (this document). Next: Phase 0.
- 2026-11-10: SVM enabled, Ubuntu on WSL2, VitaSDK 2026.08 + SDL3 + vitaGL installed. Next: a Vita3K install, a desktop baseline build of gd01, then Phase 1 (`vita/CMakeLists.txt`).
- 2026-11-10: Desktop baseline confirmed by the user. The first Vita VPK builds (0 warnings, no upstream patches). It won't render yet. Next: vitaGL bring-up via `--wrap`, file logging, then the first run on hardware.
- 2026-11-10: Performance plan and hardware loop added (`tools/vita-loop.sh`, vitacompanion over Wi-Fi). Next: the user sets up vitacompanion, then the movie-worker build is tested through the loop.
- 2026-11-10: Hardware run 9: movie decode measured on the Vita through the loop; NEON decoder paths (patch 0007) and B-picture skipping; the intro movie now plays in sync at ~8 pictures a second. Next: faster IDCT and conversion off the decode core, then song CPU.
- 2026-11-10: Hardware run 10: song frame phases; track_fade grab dropped (patch 0010) and the glFenceSync stand-in no longer calls glFinish: songs 10-12 → 14-17 FPS. Next: crowd and character animation CPU, venue draw CPU.
