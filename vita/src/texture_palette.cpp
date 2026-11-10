// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "texture_palette.h"

#include <cstring>

namespace vita {

bool paletteRgba8(const uint8_t* rgba, int width, int height, std::vector<uint8_t>& out, int* colorsUsed) {
    if (!rgba || width <= 0 || height <= 0) return false;
    const size_t texels = size_t(width) * size_t(height);
    out.assign(kPalette8Bytes + texels, 0);
    uint8_t* palette = out.data();
    uint8_t* indices = out.data() + kPalette8Bytes;

    // Open-addressing colour -> index table (512 slots for at most 256 keys).
    constexpr uint32_t kSlots = 512;
    uint32_t keys[kSlots];
    int16_t values[kSlots];
    std::memset(values, 0xff, sizeof values);  // -1: empty
    int count = 0;
    uint32_t lastColor = 0;
    int lastIndex = -1;

    for (size_t i = 0; i < texels; ++i) {
        uint32_t c;
        std::memcpy(&c, rgba + i * 4, 4);
        if (c == lastColor && lastIndex >= 0) {  // runs of one colour are common
            indices[i] = uint8_t(lastIndex);
            continue;
        }
        uint32_t slot = (c * 2654435761u) >> 23;  // top 9 bits: 0..511
        while (values[slot] >= 0 && keys[slot] != c) slot = (slot + 1) & (kSlots - 1);
        int index = values[slot];
        if (index < 0) {
            if (count == 256) return false;
            index = count++;
            keys[slot] = c;
            values[slot] = int16_t(index);
            std::memcpy(palette + index * 4, &c, 4);
        }
        indices[i] = uint8_t(index);
        lastColor = c, lastIndex = index;
    }
    if (colorsUsed) *colorsUsed = count;
    return true;
}

}  // namespace vita
