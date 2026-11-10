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
