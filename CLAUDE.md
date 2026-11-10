# CLAUDE.md: gd01 PS Vita port

## What this repo is

A workspace for porting **gd-game-01** from Guitar Dream Engine (GDE) to the PS Vita as a
homebrew VPK. gd-game-01 is a clean-room C++20 / SDL3 / OpenGL reimplementation of the
Guitar Hero (PS2, 2005) engine. It loads the player's **own** disc image at run time.

- Upstream: https://github.com/mbracali/prj-gd-engine, cloned into `upstream/` (git-ignored,
  with its own git history). The pinned commit is in `docs/port-plan.md`.
- Upstream's own feasibility study: `upstream/docs/spikes/study_ps_vita.md`. Upstream
  backlog item: **GDALL-024** (not started as of 2026-11-10).
- Plan, audit findings and status: `docs/port-plan.md`. **Read it before working, and update
  it at the end of each task.**
- The user has commented on the maintainer's thread (2026-11-10). Still, nothing is pushed,
  posted or opened upstream by an agent unless the user asks for that specific action.
- `CHANGES.md` must list every patch: add a dated row in the same commit that adds or
  changes a patch in `patches/`.

## Hard rules

1. **No game data in git, ever.** That covers ISOs, rips (`extracted/`), `.ark/.hdr/.vgs/.pss`,
   decoded textures and audio, and screenshots containing game art. Disc images go in
   `original_isos/`, which is git-ignored.
2. **No decompiler output in git.** That covers Ghidra projects and pasted pseudo-C. We port
   GDE's code. We do not reverse-engineer the game ourselves.
3. **Do not edit `upstream/gd-game-01/` in place as our source of truth.** Upstream marks that
   folder as *frozen* (only the owner may change it). We work like this:
   - Vita-only code lives in our `vita/` folder (CMake project, platform layer, GL shim,
     stubs). It compiles the upstream sources from `upstream/gd-game-01/engine/src`.
   - Upstream sources we *must* change are kept as small, reviewable patches in `patches/`
     (`NNNN-short-name.patch`, applied by a script). Prefer `#if defined(__vita__)` guards
     that leave other platforms byte-identical. These patches are what we will offer the
     maintainer later.
4. **License obligations** (PolyForm Noncommercial 1.0.0 + upstream `LICENSE-ADDITIONAL-TERMS.md`):
   - Keep the attribution line "Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine"
     in the README, in release notes, and on the in-game credits/about screen.
   - Never sell the port, paywall it or add ads.
   - Every distributed build ships with its full source. Every changed upstream file gets
     `// Modified by <name> on <date>: <what>` at the top. Root `CHANGES.md` lists the changes
     (start it before the first public build).
   - New source files carry the upstream SPDX header:
     `// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms`
     `// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.`
5. **Branding:** LiveArea / bubble art must be our own. No Guitar Hero logos or game art.
6. **Upstream conventions apply to anything we send upstream:** commits signed off
   (`git commit -s`), no AI co-author lines, match their style, no compiler warnings, and run
   `python3 .github/scripts/repo_guard.py` with 0 problems. This repo's own commits follow
   the normal session attribution.

## Target

| | |
|---|---|
| Console | PS Vita / PS TV, hacked (HENkaku/HENlo, ideally Ensō), with `libshacccg.suprx` installed |
| CPU / GPU | 4x Cortex-A9 @ up to 444 MHz (3 usable) / SGX543MP4+ via sceGxm |
| RAM budget | Aim for **< 300 MB** main RAM. Homebrew gets roughly 256–365 MB. 128 MB CDRAM for video. |
| Screen | 960x544 |
| Toolchain | VitaSDK (arm-vita-eabi), CMake, SDL3 (Vita port), vitaGL |
| Data path | `ux0:data/gd01/` (ISO, saves, config, shader cache) |
| Output | Standalone `eboot.bin` in a VPK. No launcher, no `dlopen`. |

## Dev environment (this PC: Windows 11)

- Missing today: WSL, Docker and CMake. VitaSDK needs a Linux-like environment.
  Recommended: **WSL2 + Ubuntu** with VitaSDK installed via `vdpm`. The alternative is the
  `vitasdk/vitasdk` Docker image. The user has to install WSL themselves (admin rights, reboot).
- Testing: start in the Vita3K emulator, but the fps and memory gates are measured only on
  real hardware.
- Device tooling (from the community guide; set up when we reach hardware):
  - Grarak/psp2-tooling plus dev kernel modules, for debugging and profiling on a retail Vita
  - shoui520/vita-agent-use (MCP bridge), to upload, install, run and collect logs over USB or Wi-Fi
- The user supplies the GH1 PS2 disc image (`original_isos/`) and the console.

## Working style

- Small steps, each verified: first get it to build, then run it in Vita3K, then run it on hardware.
- Log every stub and unsupported path loudly (upstream lesson L1). Don't hide failures.
- Prefer behaviour identical to upstream. Vita-only deviations (no post-FX, dropped FMV,
  button play) must be explicit, labelled and listed in `docs/port-plan.md`.
- When a fact about VitaSDK, vitaGL or SDL3-Vita is unverified, mark it **[verify]** in the docs.
