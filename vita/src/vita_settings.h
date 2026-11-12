// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// PS Vita: the Enhanced Settings the Vita does not show (user's call,
// 2026-11-12). Patch 0023 leaves their rows out of the menu (ui/select_panels.cpp)
// and makes App read their Vita defaults whatever enhanced.json holds (app.cpp).
//   - cannot work, or would break the game: the Modes presets (PC resolutions
//     and frame rates; 1080p filled the video memory, hardware run 5),
//     Resolution, the graphics / audio backends, Screen Mode, AI upscaling,
//     HDR, mouse, the file Export / Load buttons (a desktop folder picker),
//     anti-aliasing, ambient occlusion, sharpening, sharper text and sprites;
//   - full-screen passes or extra draws the 30 FPS budget has no room for
//     (untested; may come back one by one once measured);
//   - the No Venue cheat: Notes Only does the same and loads no stage.
#pragma once

#include <string_view>

namespace vita {

inline bool hiddenSetting(std::string_view key) {
    static constexpr std::string_view kHidden[] = {
        // cannot work, or would break the game
        "mode_steam_deck", "mode_original", "mode_clean", "mode_max_hd", "mode_max_4k", "mode_budget",
        "mode_good_old_days", "modes", "internal_res", "graphics_backend", "audio_backend", "screen_mode",
        "ai_upscaling", "ai_upscaling_mode", "hdr", "mouse", "export", "load_settings", "load_progress",
        "antialiasing", "ssao", "sharpening", "sharp_ui",
        // untested, likely too slow
        "bloom", "cinematic_color", "color_grade", "film_grain", "chromatic", "star_blur", "bokeh",
        "shadows_all", "more_lights", "light_dust",
        // a better option exists: Performance Mode > Notes Only (no stage loaded at all)
        "cheat_no_venue",
    };
    for (std::string_view k : kHidden)
        if (k == key) return true;
    return false;
}

// Enhanced Settings > Button Layout: the default frets of the Vita's own
// buttons (1: D-pad Left, L, R, Circle, Cross, strum on D-pad Up / Down;
// 2: D-pad Left, D-pad Up, Triangle, Circle, Cross, no strum). Set by App,
// defined in input/input.cpp (patch 0024). Inputs customized on the
// Gamepads screen take precedence; its Reset to Defaults goes back to this.
void setButtonLayout(int layout);

}  // namespace vita
