// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Desktop check for vita::paletteRgba8: round trips (palette + indices back
// to RGBA must equal the input exactly), the 256-colour limit, and speed.
//   g++ -std=c++20 -O2 -Ivita/src vita/tests/texture_palette_check.cpp vita/src/texture_palette.cpp
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "texture_palette.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

std::vector<uint8_t> image(int w, int h, int colors, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<uint32_t> pal(static_cast<size_t>(colors));
    for (uint32_t& c : pal) c = rng();
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        uint32_t c = pal[rng() % size_t(colors)];
        std::memcpy(&px[i * 4], &c, 4);
    }
    return px;
}

bool roundTrip(const std::vector<uint8_t>& px, int w, int h) {
    std::vector<uint8_t> out;
    if (!vita::paletteRgba8(px.data(), w, h, out)) return false;
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        const size_t index = out[size_t(vita::kPalette8Bytes) + i];
        if (std::memcmp(&out[index * 4], &px[i * 4], 4) != 0) return false;  // palette[index] == original texel
    }
    return out.size() == size_t(vita::kPalette8Bytes) + size_t(w) * size_t(h);
}

}  // namespace

int main() {
    std::printf("paletteRgba8\n");
    expect(roundTrip(image(256, 256, 1, 1), 256, 256), "1 colour, 256x256, exact round trip");
    expect(roundTrip(image(256, 256, 16, 2), 256, 256), "16 colours, exact round trip");
    expect(roundTrip(image(512, 256, 256, 3), 512, 256), "256 colours (the limit), exact round trip");
    expect(roundTrip(image(37, 13, 200, 4), 37, 13), "odd size 37x13, exact round trip");
    {
        std::vector<uint8_t> out;
        auto px = image(256, 256, 300, 5);
        expect(!vita::paletteRgba8(px.data(), 256, 256, out), "300 colours is refused");
    }
    {
        int used = 0;
        std::vector<uint8_t> out;
        auto px = image(64, 64, 100, 6);
        vita::paletteRgba8(px.data(), 64, 64, out, &used);
        expect(used <= 100 && used > 90, "reports the colours used");
    }
    {
        auto px = image(512, 512, 256, 7);
        std::vector<uint8_t> out;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) vita::paletteRgba8(px.data(), 512, 512, out);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
        std::printf("  info: 512x512 / 256 colours: %.2f ms per texture on this PC\n", ms);
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
