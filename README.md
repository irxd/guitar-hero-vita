# Guitar Hero (PS2) for PS Vita: a Guitar Dream Engine port

A homebrew PS Vita port of Guitar Dream Engine's reimplementation of **Guitar Hero**
(PlayStation 2, 2005), the engine's `gd-game-01`.

> Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine

- Guitar Hero is a trademark of its owners; the name is used only to say which game this
  reimplements. Not affiliated with or endorsed by them or by the Guitar Dream Engine maintainer.
- Contains no original game code or assets. To play, rip your own PS2 disc.
- Licensed under the upstream terms: PolyForm Noncommercial 1.0.0 plus `LICENSE-ADDITIONAL-TERMS.md`.
  The port is non-commercial, and its full source is published with every build.

## Status

**Playable on hardware, not yet released.** No public VPK yet; tested on a retail PS Vita
with the user's own disc image (2026-11-11).

Working on the Vita:

- Boot logos, main menu, Quick Play song list with previews, Enhanced Settings
- Songs load and play with the full stage: venue, band, crowd, highway, HUD, pause menu, retry
- Steady 30 FPS with the full stage, and 60 FPS options (see [Performance](#performance))
- Play on the Vita's buttons: Green D-pad ←, Red L, Yellow R, Blue ○, Orange ✕, strum
  D-pad ↑/↓
- Memory stays stable however many songs you play

Known limitations:

- **Movies** (intro and in-game videos) play in sync with their sound but choppy, at about
  8 pictures a second: the engine decodes MPEG-2 in software, and the Vita's CPU is short of
  real time even with NEON code paths
- **Button remapping** on the Vita is not done yet; the layout above is fixed for now
- **Not tested yet:** Career mode, multiplayer (face-off), tutorials, the Gamepads screen
- **Vita-only differences from the PC version:** no post-processing effects by default
  (bloom, depth of field), no fade at the far end of the highway, no AI upscaling, HDR or
  MIDI guitars

Still to do before a release: button remapping, smoother movies, our own LiveArea art and
the attribution on the credits screen, then a VPK release with its source.
Full record of every hardware run: [docs/port-plan.md](docs/port-plan.md).

## Performance

Measured on a retail PS Vita (2026-11-11), with the song played by autoplay and the
camera shots seeded so every run sees the same shots. Test song: *I Love Rock 'n' Roll*,
one of the busiest venues (a large crowd on screen).

Options → Enhanced Settings → Performance & Graphics:

| Performance Mode | Crowd | Result |
|---|---|---|
| 30 FPS (default) | On (default) | Steady 30 FPS. After loading, at most 1 late frame in any 5 seconds |
| 30 FPS | Off | Steady 30 FPS, more headroom (17–19 ms of the 33 ms a frame) |
| 60 FPS | On | 32–55 FPS; the widest crowd shots drop to the low 30s |
| 60 FPS | Off | 47–58 FPS |
| Notes Only | (not shown) | Locked 60 FPS; just the highway on black, ~3 ms a frame |

- **30 FPS** shows every frame for exactly two screen refreshes, so motion stays even.
  60 FPS is smoother when it holds, but busy stages still run below it.
- **Crowd On** draws up to 8 people nearest the camera in 3D and the rest as flat
  cut-outs; in the widest shots this halves the crowd's cost (8.4 → 4.5 ms a frame)
  against drawing them all in 3D, and the crowd looks just as full.
- **Notes Only** loads no stage, band or crowd: songs also start faster and use less memory.

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
