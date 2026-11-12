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
- [x] **Hardware run 11 (2026-11-11): character animation.** `Character::poll` kept every bone in `std::map<std::string, ...>`: `blendChain` built a string-keyed accumulator per call, `updateWorld` (twice a pose) a `std::map<std::string, char>` plus a `std::function` recursion, `pose` and `servos` searched by name. **Patch 0011** builds bone arrays once per character (owners, parents-first order, buffer slots, clip bone pointer -> slot, servo bones) and runs the same arithmetic on them; the first 8 poses of each character also run upstream's code and compare bit for bit (21 characters, 0 differences). Guitarist 9 → 2.3 ms, band 7 → 0.6 ms, crowd 17-21 → 1.5-2.6 ms. **Songs: 22-34 FPS** (29-45 ms CPU a frame).
  - Remaining per frame: **venue draw 17-30 ms** (290-480 draws, only 3.4-5.8 ms inside vitaGL), track 5 ms, venue script 1 ms. Next target: the CPU around each draw in the scene renderer.
- [x] **Hardware run 12 (2026-11-11): scene renderer.** Patch 0012 times `Renderer::drawMesh` as a nested phase: of the old 17-30 ms "venue draw", 9-13 ms was `Renderer::drawMesh` (4.4-6.3 ms of it inside `glDrawElements`, the rest uniforms and state) and 12-19 ms the scene renderer's own work. **Patch 0013** caches the scene renderer's per-draw string work (the `name|material` key built and searched for every draw, child / bone / light lookups by name). Scene renderer 12-19 → 8-14 ms. **Songs: 24-35 FPS** (29-41 ms CPU a frame; the slow end is shots with 600 draws).
  - Remaining per frame: renderer 9-14 ms (half of it GL uniforms and state outside the draw call), scene renderer 8-14 ms, track 4 ms, guitarist 2.3 ms, crowd 1.5-2.5 ms, script 1.1 ms.
  - Ideas kept for later: animation LOD for the crowd (the user's suggestion: far members at half rate; worth < 1 ms now), skip redundant `glUniform` calls, animation on another core.
- [x] **Hardware run 13 (2026-11-11): profiler cost, vitaGL build, transform updates.**
  - **The profiler was part of the cost:** a clock read (`SDL_GetTicksNS`) takes 1.3 us on the Vita, and the per-draw timers read it 4 times per mesh: 3-4 ms of every song frame. Phase timing is now off by default; `GD01_VITA_PHASES=1` (args.txt) times frame phases (nearly free), `=2` also the nested `renderer: draw mesh` phase and every draw call (the old, costly mode). Run 12's renderer numbers were inflated by it.
  - **Own vitaGL build** (`tools/build-vitagl.sh`, linked by `tools/build-vita.sh` when present): vitaGL e1473ff (2026-11-08) with `NO_DEBUG=1 HAVE_VERTEX_LAYOUT_CACHE=1` instead of vdpm's 2bdbe89 (2026-09-23, `NO_DEBUG=1`). No measurable change per draw (~12 us inside `glDrawElements` either way); kept for the newer fixes. vitaGL's uniform setters are a memcpy plus a dirty flag, so skipping redundant `glUniform` calls would save little.
  - **Patch 0014:** `AnimPlayer::updateTransforms` looked up every node's parent by name in a `std::map` on every call (the track calls it ~10 times a frame). Parent indices are now resolved once. Track 4.6 → 3.0 ms, script 1.1 → 0.85 ms.
  - **Songs: 34-48 FPS** (20.5-29.5 ms CPU a frame; GPU wait 0.1-0.2 ms, so still CPU bound). The heaviest shots (520 draws) are at 34-35 FPS.
  - Per frame now (level 1): venue draw 11-20 ms (~32-37 us per mesh, scene renderer + GL), track 3 ms, guitarist 2.2 ms, crowd 1.5-2 ms, script 0.85 ms.
- [x] **Hardware run 14 (2026-11-11): 30 FPS lock.** **Patch 0015:** Max FPS on Vita is 30 (new default), 60 or Unlimited. `Renderer::setFrameCap` sets vitaGL's vblank interval (`eglSwapInterval`: vitaGL's display queue calls `sceDisplayWaitVblankStartMulti(n)`), so 30 shows each frame for exactly two 60 Hz refreshes, with no timer drift. **Songs at 30: 29.7-30.0 FPS**, 4-12 ms to spare per frame; the heaviest shot (525-544 draws, 30-31 ms CPU) is at the edge (27-30). 60 runs unlocked (34-48).
  - One launch hung inside `vglInitExtended` right after a quit through vitacompanion (no crash dump); the relaunch was fine. Watch for it.
- [x] **Hardware run 15 (2026-11-11): view culling.** Benchmarks are now repeatable: `GD01_VITA_SEED=1` (patch 0017) fixes the camera's shot choice (`--args 'GD01_VITA_PHASES=1 GD01_VITA_SEED=1 --song smokeonthewater --autoplay'`, with `max_fps` 60 in enhanced.json for unlocked numbers). Note: `SDL_getenv` does not see args.txt variables (SDL copies the environment earlier); use `std::getenv`.
  - **Patch 0016:** the scene renderer had no view culling. Each non-skinned mesh now gets a bounding sphere from its vertices, tested against the six clip planes of its MVP. Same shots, CPU per frame off → on: 29.6 → 28.4 ms (516 → 497 draws), 21.8 → 20.2, 28.4 → 25.8, 26.1 → 24.0 ms. Little gain in the widest shot (only ~15 meshes off screen).
  - Unlocked: 35-49 FPS; locked 30 has ~5 ms to spare in the heaviest shot.
- [x] **Hardware run 16 (2026-11-11): second benchmark song, crowd LOD.** `--song iloverockandroll` (seed 1) is heavier than smokeonthewater: wide shots of 590-660 draws at 33-36 ms (27-30 FPS unlocked). Patch 0009 now splits "venue draw" into crowd impostors / stage / band / crowd / rest: in those shots the crowd is ~14 ms (impostors 3.7, 3D members 8-8.8, animation ~2).
  - **Patch 0018** (the user's idea): Max FPS 60 leaves out the crowd (no animation, no draw): **44-56 FPS** on this song (17-20 ms CPU). At 30 the crowd stays; its six impostors are re-rendered half per frame (3.7 → 1.9 ms): **29.3-30.0 FPS**, the widest shots at the edge (31.7-33.7 ms CPU).
  - Deviations (Vita): no crowd at Max FPS 60; impostor crowd animates at 15 Hz at Max FPS 30.
- [x] **Hardware run 17 (2026-11-11): frame spikes.** The perf line now has the slowest frame and the count over 34 ms (a frame that misses the 30 fps vblank pair is shown for a third refresh: the user's "split-second 23 fps"); at `GD01_VITA_PHASES=1` each slow frame logs its four longest phases.
  - **Log writes stalled the game:** main.cpp fflushes gd01.log after every line (and SDL's own output follows); on the memory card a line blocked 20-60 ms, so every 5 s perf line made a hitch, also in normal play. `vita_platform.cpp` wraps `SDL_SetLogOutputFunction`: the game's log function runs on a writer thread (core 2), drained before it is replaced at exit. Still ~17 ms late after a write (card contention), so the periodic perf and memory lines are only written when `GD01_VITA_PHASES` >= 1.
  - Patch 0018: the 3D crowd members animate half per frame too (crowd animation 2.0-2.3 → 0.9-1.3 ms). I Love Rock and Roll at 30: most 5 s windows have 0-1 frames over 34 ms (before: 1-13).
  - **Still over budget:** the widest crowd shot (650 draws, 34.5 ms): 3D crowd draw 8.3 ms. A skinned mesh draws with at most 4 bones, so each crowd member is many small draws. Options: merge each member's skinned pieces into fewer draws with a bigger bone palette (no visual change, larger job), or cap the 3D members drawn (visible change).
- [x] **Hardware run 18 (2026-11-11): merged characters (tried), GPU waits, lighting.**
  - Per phase, level 1 now counts draws. A character is ~11 skinned pieces (4 bones each); the widest shot draws ~23 crowd members as 259 draws, the band ~130.
  - **Merging (patch 0019, experimental, off):** consecutive pieces with the same draw settings concatenated into one mesh with an 8-16 bone palette; each vertex's colour bytes (white on skinned meshes) hold its palette indices; `vita_platform.cpp` rewrites the scene shader to `uBoneRows`. Draws fell 259 → 136 (crowd) and 135 → 60 (band); a CPU self-check matched the original skinning exactly. **But on the GPU:** indexing the palette by a per-vertex index skinned some band members wrongly (stretched, dark: the user saw it), and the index-free blended form (sum over slots) crashed libshacccg in `glLinkProgram`. Merging is off unless `GD01_VITA_MERGE=1`; upstream's shader is untouched without it. Also the time was mostly CPU per piece, not per draw (crowd 8.5 → 8.2 ms with half the draws).
  - **GPU waits on off-screen targets:** vitaGL allows 1 scene in flight per FBO render target; each crowd impostor's scene start waited for the GPU's previous one, up to 7 ms a frame in busy shots. `GD01_VITA_FBO_SCENES` now defaults to 4: impostors <= 2 ms.
  - **Lighting per draw:** `lightingFor` allocated a vector and recomputed the lights for every piece; it now reuses the result for consecutive pieces with the same material (patch 0019).
  - Debug: `GD01_VITA_SHOT=<frame>,...` writes raw screenshots (`ux0:data/gd01/shot_<frame>.rgba`); never committed (game art). A missing uniform name is logged once.
  - I Love Rock and Roll at 30 (seed 1): most 5 s windows 0-1 frames over 34 ms; the widest crowd shot 55 (was 137).
- [x] **Hardware run 19 (2026-11-11): Performance Mode, Crowd Reduced.**
  - Enhanced Settings on the PS Vita (patch 0015): **Performance Mode** (the `max_fps` key) 30 FPS / 60 FPS (No Crowd) / Notes Only; **Crowd** Full / Reduced (default Reduced). Unlimited stays valid in enhanced.json for benchmarks, without a menu entry.
  - **Crowd Reduced** (patch 0018): at most 8 3D members, on the shot region's spots nearest the camera (in front of it first); the other spots keep their flat sprites, so the crowd stays as dense. Widest shot's crowd draw 8.4 ms (262 draws) -> 4.5 ms (172). I Love Rock and Roll at 30, seed 1: frames over 34 ms 64 -> 26 per run, 13-14 of them in the loading window; every later 5 s window 0-1 (the 1s line up with the periodic report of a `GD01_VITA_PHASES` run).
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
  - **Next:** the CPU work per character piece in the scene renderer (state building, ~30 us a piece); a merged path that the shader compiler accepts (CPU-skinned merged crowd, or bones in a vertex texture); then the per-mesh cost in the scene renderer (state building, string mesh names in the track) and animation on a second core, the only route toward 60.
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
- [ ] Input map, default (user's layout, 2026-11-11, patch 0004): Green = D-pad ←, Red = L, Yellow = R, Blue = ○, Orange = ✕; strum = D-pad up/down; Star Power = Select (+ optional tilt via the motion sensor); whammy = right stick; pause = Start. Optional alternatives: front-touch fret zones, rear-touch strum. All remappable via the GAMEPADS screen. Labelled as a "handheld mode".
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
- 2026-11-11: Hardware run 11: index skeleton (patch 0011): songs 14-17 → 22-34 FPS. Next: venue draw CPU per draw.
- 2026-11-11: Hardware run 12: scene renderer caches (patch 0013): songs 22-34 → 24-35 FPS. Next: GL uniform and state cost per draw.
- 2026-11-11: Hardware run 13: profiler off by default (it cost 3-4 ms), own vitaGL build, AnimPlayer parent indices (patch 0014): songs 29-43 → 34-48 FPS.
- 2026-11-11: Hardware run 14: Max FPS 30 (default) paced by vblank interval (patch 0015): songs hold 29.7-30.0 FPS; heaviest shot at the edge.
- 2026-11-11: Hardware run 15: view culling (patch 0016) saves 1-2.6 ms a frame; repeatable shots with GD01_VITA_SEED (patch 0017).
- 2026-11-11: Hardware run 16: iloverockandroll benchmark; crowd off at Max FPS 60 (44-56 FPS), impostors at half rate at 30 (holds 29.3-30.0) (patch 0018).
- 2026-11-11: Hardware run 17: spike logging; log writes moved to a thread and periodic reports made opt-in (they caused a hitch every 5 s); crowd members animate at half rate (patch 0018).
- 2026-11-11: Hardware run 18: merged characters built but off (GPU skinning errors / shader compiler crash); FBO scenes in flight 1 -> 4 (impostor GPU waits); lighting reuse. Widest crowd shot 137 -> 55 slow frames per 5 s.
- 2026-11-11: Hardware run 19: Performance Mode (30 / 60 No Crowd / Notes Only) and Crowd Full / Reduced; Reduced halves the widest shot's crowd (8.4 -> 4.5 ms), slow frames 64 -> 26 per run; Notes Only a locked 60.
- 2026-11-11: Crowd setting is a check: on = 8-member crowd at 30 and 60, off = none (60 + crowd 32-55 FPS, 60 without 47-58).
- 2026-11-11: Default frets changed to the user's layout: D-pad Left, L, R, Circle, Cross (patch 0004).
- 2026-11-11: Hardware run 20: song previews on a worker thread, reading only their window (no list freeze, 2 MB instead of 18-36 MB); GPU textures and meshes freed with their SceneRenderer (the heap no longer grows per song).
- 2026-11-11: Disc reads with sceIoPread: ~3x faster (song audio 5.0 -> 1.6 s, previews ~0.3 s).
- 2026-11-12: D-pad Left no longer also selects in the menus (patch 0004).
- 2026-11-12: Store lists wrap correctly when scrolling up from the first item (script `mod`, patch 0022).
