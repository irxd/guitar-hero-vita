// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// RGBA8 -> 8-bit paletted texture (GL_OES_compressed_paletted_texture,
// GL_PALETTE8_RGBA8_OES) for the Vita's video memory. gd01 expands the PS2's
// 4/8-bit paletted textures to RGBA8; a venue then needs well over the
// Vita's ~80 MB of free VRAM (hardware, 2026-11-10). Re-packing any image of
// at most 256 distinct colours is lossless and a quarter of the size.
#pragma once

#include <cstdint>
#include <vector>

namespace vita {

// Builds the GL_PALETTE8_RGBA8_OES data for one level: 256 RGBA8 palette
// entries (1024 bytes, unused ones zero) followed by one index byte per
// texel, rows top to bottom as given. Returns false (and leaves `out`
// unspecified) when the image has more than 256 distinct colours.
bool paletteRgba8(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& out, int* colorsUsed = nullptr);

inline constexpr int kPalette8Bytes = 256 * 4;

}  // namespace vita
