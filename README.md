<p align="center">
  <img src="sce_sys/livearea/contents/startup.png" alt="GH Vita logo" width="280">
</p>

# Guitar Hero (PS2) for PS Vita: a Guitar Dream Engine port

A homebrew PS Vita port of Guitar Dream Engine's reimplementation of **Guitar Hero**
(PlayStation 2, 2005), the engine's `gd-game-01`.

> Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine

- Guitar Hero is a trademark of its owners; the name is used only to say which game this
  reimplements. Not affiliated with or endorsed by them or by the Guitar Dream Engine maintainer.
- Contains no original game code or assets. To play, rip your own PS2 disc.
- Licensed under the upstream terms: PolyForm Noncommercial 1.0.0 plus `LICENSE-ADDITIONAL-TERMS.md`.
  The port is non-commercial, and its full source is published with every build.

## Install

You need:

- A hacked PS Vita or PS TV (HENkaku / h-encore / Ensō)
- `libshacccg.suprx` extracted on the console, for example with
  [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D). The game compiles its shaders
  at run time and cannot start without it.
- **Your own** Guitar Hero PS2 disc, ripped to an `.iso` (tested with *Guitar Hero (USA)*).
  No game data is included.

Steps:

1. Download `ghvita-v<version>.vpk` from the
   [Releases](https://github.com/irxd/guitar-hero-vita/releases) page.
2. Copy it to the Vita (USB or FTP in VitaShell) and install it with VitaShell.
3. Copy your `.iso` to `ux0:data/ghvita/` (create the folder if needed).
4. Start **GH Vita** from the LiveArea (tap the lettering).

Settings, saves and the log (`ghvita.log`, attach it to bug reports) are also stored in
`ux0:data/ghvita/`. To update, install the new VPK over the old one: your data is kept.

## Status

**v0.2.0 (alpha)**: 60 FPS by default, a PS2-style button layout using the rear touchpad or a controller's L2 / R2, and a new LiveArea. Tested on a retail PS Vita. Install steps, requirements and known issues: [docs/releases/v0.2.0.md](docs/releases/v0.2.0.md).

Working on the Vita:

- Boot logos, main menu, Quick Play song list with previews, Enhanced Settings
- Songs load and play with the full stage: venue, band, crowd, highway, HUD, pause menu, retry
- 60 FPS by default (crowd off), and a steady 30 FPS with the full crowd (see [Performance](#performance))
- Play on the Vita's buttons, with three layouts in Enhanced Settings → Button Layout:

  | | Green | Red | Yellow | Blue | Orange | Strum |
  |---|---|---|---|---|---|---|
  | Layout 1 (default) | D-pad ← | L | R | ○ | ✕ | D-pad ↑/↓ (optional) |
  | Layout 2 | D-pad ← | D-pad ↑ | △ | ○ | ✕ | L / R (optional) |
  | Layout 3 (PS2) | L2 / rear left | L | R | R2 / rear right | ✕ | D-pad ↑/↓ (optional) |

  A fret press plays the note on its own, so strumming is optional; a strum while holding
  frets plays them again (for fast runs of one note), and never counts as a miss. Single buttons can
  be rebound in the Gamepads screen, which overrides the layout. The rear touchpad's left and
  right halves can be bound there too (Rear Left / Rear Right; they only work in a song).
- Whammy on either analog stick; Star Power on Select
- An external controller (a DualShock or DualSense through ds34vita) plays alongside the
  Vita's own buttons: both control player 1. Its L2 / R2 work (Layout 3 uses them)
- Optional: tap the rear touchpad to strum (Enhanced Settings → Rear Touch Strum) and shake
  the Vita for whammy (Shake for Whammy), both off by default
- LiveArea art and bubble icon ("GH Vita")
- Memory stays stable however many songs you play
- Movies (intro and in-game videos) play in sync with their sound at their full 30
  pictures a second, stretched to the full screen

Testing so far:

- **Partly tested:** Career mode, tutorials, the Gamepads screen

Known limitations:

- **Two-player (face-off) is not supported yet:** with two DualSense controllers on
  ds34vita, the second one stops responding after a while and needs a Bluetooth restart.
  Not yet known whether ds34vita or the port causes it
- **Vita-only differences from the PC version:** no post-processing effects by default
  (bloom, depth of field), no fade at the far end of the highway, no AI upscaling, HDR or
  MIDI guitars

Release builds come from `tools/release-vita.sh` (VPK, full source, notes, checksums).
Full record of every hardware run: [docs/port-plan.md](docs/port-plan.md).

## Performance

Measured on a retail PS Vita (30 FPS: 2026-11-11; 60 FPS: 2026-11-17), with the song
played by autoplay and the camera shots seeded so every run sees the same shots. Test song: *I Love Rock 'n' Roll*,
one of the busiest venues (a large crowd on screen).

Options → Enhanced Settings → Performance & Graphics:

| Performance Mode | Crowd | Result |
|---|---|---|
| 60 FPS (default) | Off (default) | 59.9 FPS on average, every 5 seconds at least 59.4 |
| 60 FPS | On | 56 FPS on average; the widest crowd shots drop to about 47 |
| 30 FPS | On | Steady 30 FPS. After loading, at most 1 late frame in any 5 seconds |
| 30 FPS | Off | Steady 30 FPS, more headroom (17–19 ms of the 33 ms a frame) |
| Notes Only | (not shown) | Locked 60 FPS; just the highway on black, ~3 ms a frame |

- **30 FPS** shows every frame for exactly two screen refreshes, so motion stays even.
  60 FPS is smoother; with the crowd on, the busiest shots still run below it.
- **Crowd On** draws up to 8 people nearest the camera in 3D (4 at 60 FPS) and the rest
  as flat cut-outs; in the widest shots this halves the crowd's cost (8.4 → 4.5 ms a frame)
  against drawing them all in 3D, and the crowd looks just as full. At 60 FPS the cut-outs
  also update a little less often (20 times a second instead of 30), which saves another
  2 ms a frame together with the 4 people (43.7 → 46.0 FPS on average).
- **Notes Only** loads no stage, band or crowd: songs also start faster and use less memory.

Particles FX (sparks, smoke, the highway's hit effects), same song at 60 FPS with the crowd
on, to show the cost (2026-11-12):

| Particles FX | Average FPS | Time a frame |
|---|---|---|
| None | 48 FPS | 19.0 ms |
| Original (default) | 44 FPS | 22.2 ms |
| 2x | 44 FPS | 22.2 ms |

- Original costs about 3 ms a frame. At 30 FPS that still fits (a frame has 33 ms), so the
  30 FPS results above already include it; at 60 FPS, None gives ~4 FPS more.
- 2x looks and costs the same as Original in this song: each effect has its own maximum
  number of particles, and this stage's effects already reach it.

Loading and memory:

| | Result |
|---|---|
| Song audio (a 19 MB track) | Loads in 1.6 s |
| Song list preview | Plays ~0.3 s after you stop on a song; the list never freezes |
| Memory in the menus | Stays at ~40 MB of the 256 MB available, however many songs you play (tested six in a row) |
| Memory during a song | ~80–110 MB |

Details of every hardware run: [docs/port-plan.md](docs/port-plan.md).

## More

- Changes from upstream: [CHANGES.md](CHANGES.md)
- Licence: [LICENSE](LICENSE) and [LICENSE-ADDITIONAL-TERMS.md](LICENSE-ADDITIONAL-TERMS.md) (unchanged from upstream)
- Agent and contributor rules: [CLAUDE.md](CLAUDE.md)
