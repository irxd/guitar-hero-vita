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
- [ ] Base packages in Ubuntu (build-essential, git, cmake, ninja, python3, curl): in progress
- [ ] Install VitaSDK (`vdpm`), CMake ≥ 3.24, SDL3 for Vita, vitaGL (+ vitaShaRK/libmathneon as vitaGL requires)
- [ ] Install the Vita3K emulator. Get `libshacccg.suprx` onto the console and into Vita3K.
- [x] Hacked Vita ready: FW 3.65 + Ensō (version spoofed to 3.74, cosmetic only). Still needed: `libshacccg.suprx` installed.
- [x] GH1 PS2 disc ripped: `original_isos/Guitar Hero (USA).iso` (2,300,313,600 bytes)
- [ ] Confirm a native Linux/Windows build of upstream gd01 runs with the user's ISO (baseline, to check the rip)

### Phase 1: spike, "hello highway" (upstream estimate 1–2 weeks)
- [ ] `vita/CMakeLists.txt`: build the upstream sources with VitaSDK. No gdcore, no Vulkan, no MIDI.
- [ ] Get it compiling. Stub what doesn't build, logging each stub.
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
