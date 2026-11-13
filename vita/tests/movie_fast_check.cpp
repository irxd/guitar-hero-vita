// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Desktop accuracy check of the Vita movie fast paths (vita/src/mpeg2_neon.cpp)
// against the exact ones: the integer inverse DCT against the float IDCT, and
// the fast colour conversion against upstream's 16.16 formula. Uses the scalar
// code the NEON versions are verified against on the console.
//   movie_fast_check        (exit code 1 when a limit is exceeded)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mpeg2_neon.h"

namespace {

uint32_t gSeed = 0x2468ace1u;
uint32_t rnd() { return gSeed = gSeed * 1664525u + 1013904223u; }

double scale(int k) { return k ? 0.5 : 0.5 / std::sqrt(2.0); }
double basis(int x, int u) { return std::cos((2 * x + 1) * u * M_PI / 16); }

// Forward DCT (double) of an 8x8 block, the inverse of upstream's IDCT.
void fdct(const double* in, double* out) {
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u) {
            double s = 0;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) s += in[y * 8 + x] * basis(x, u) * basis(y, v);
            out[v * 8 + u] = s * scale(u) * scale(v);
        }
}

// The IDCT in double precision: the reference both IDCTs approximate.
void idctDouble(const int16_t* in, double* out) {
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            double s = 0;
            for (int v = 0; v < 8; ++v)
                for (int u = 0; u < 8; ++u) s += in[v * 8 + u] * scale(u) * scale(v) * basis(x, u) * basis(y, v);
            out[y * 8 + x] = s;
        }
}

inline uint8_t clamp255(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }

struct Errors {
    long long pixels = 0, differ = 0, sum = 0, sumSq = 0;
    int peak = 0;
    void add(int d) { ++pixels, differ += d != 0, sum += d, sumSq += d * d, peak = std::max(peak, std::abs(d)); }
    double mean() const { return double(sum) / double(pixels); }
    double meanSq() const { return double(sumSq) / double(pixels); }
    void print(const char* what) const {
        std::printf("  %s: %.3f%% of pixels differ, largest difference %d, mean error %+.5f, mean square error %.5f\n",
                    what, 100.0 * double(differ) / double(pixels), peak, mean(), meanSq());
    }
};

// Blocks as a decoder sees them: pixel (intra) or residual (non-intra) blocks
// of several kinds, transformed and quantised with step `q`.
bool checkIdct() {
    long long blocks = 0;
    Errors fastVsFloat, fastVsRef, floatVsRef;
    for (int n = 0; n < 60000; ++n) {
        const bool intra = n & 1;
        const int kind = (n >> 1) % 4;  // 0: noise, 1: smooth gradient, 2: edges, 3: flat with a little noise
        const int q = 1 + int(rnd() % 3) * 7 + (n % 7 == 0 ? 20 : 0);  // 1, 8, 15 (+20)
        double px[64], co[64];
        const int base = int(rnd() % 256), gx = int(rnd() % 33) - 16, gy = int(rnd() % 33) - 16;
        for (int i = 0; i < 64; ++i) {
            const int x = i % 8, y = i / 8;
            int v;
            if (kind == 0) v = int(rnd() % 256);
            else if (kind == 1) v = base + gx * x + gy * y;
            else if (kind == 2) v = (x + y * (gx & 3) > 5 + (gy & 3)) ? base : 255 - base;
            else v = base + int(rnd() % 7) - 3;
            v = std::clamp(v, 0, 255);
            px[i] = intra ? v : v - int(rnd() % 256);  // residual in -255..255
        }
        fdct(px, co);
        int16_t in[64];
        uint8_t mask = 0;
        for (int i = 0; i < 64; ++i) {
            in[i] = int16_t(std::clamp(int(std::lround(co[i] / q)) * q, -2048, 2047));
            if (in[i]) mask |= uint8_t(1u << (i / 8));
        }
        uint8_t pred[64], a[64], b[64];
        for (auto& p : pred) p = uint8_t(rnd() >> 24);
        int resid[64];
        vita::idct8x8Scalar(in, resid, mask);
        for (int i = 0; i < 64; ++i) a[i] = clamp255(intra ? resid[i] : pred[i] + resid[i]);
        std::copy(pred, pred + 64, b);
        vita::idctAddFastScalar(in, b, 8, intra);
        double exact[64];
        idctDouble(in, exact);
        ++blocks;
        for (int i = 0; i < 64; ++i) {
            // An exact tie (x.5, common with coarse quantisers) may round either way.
            const bool tie = std::abs(exact[i] - std::floor(exact[i]) - 0.5) < 1e-7;
            const int r = int(std::floor(exact[i] + 0.5));
            const int ref = clamp255(intra ? r : pred[i] + r);
            fastVsFloat.add(int(b[i]) - int(a[i]));
            if (tie) continue;
            fastVsRef.add(int(b[i]) - ref);
            floatVsRef.add(int(a[i]) - ref);
        }
    }
    std::printf("IDCT, %lld blocks:\n", blocks);
    fastVsFloat.print("integer IDCT against the float IDCT (the exact path)");
    fastVsRef.print("integer IDCT against a double-precision IDCT (ties left out)");
    floatVsRef.print("float IDCT against a double-precision IDCT (ties left out)");
    // Never more than one level from the exact path; against the true IDCT,
    // IEEE 1180-1990's overall limits (peak 1, mean square 0.02, mean 0.0015).
    return fastVsFloat.peak <= 1 && fastVsRef.peak <= 1 && fastVsRef.meanSq() <= 0.02 &&
           std::abs(fastVsRef.mean()) <= 0.0015;
}

// Every (Y, U, V) against upstream's mpeg2ToRgba formula.
bool checkYuv() {
    constexpr int kY = 76309, kRv = 104597, kGu = 25675, kGv = 53279, kBu = 132201;
    int maxDiff = 0;
    long long differ = 0, total = 0;
    for (int Y = 0; Y < 256; ++Y)
        for (int U = 0; U < 256; ++U)
            for (int V = 0; V < 256; ++V) {
                const int c = (Y - 16) * kY + 32768, u = U - 128, v = V - 128;
                const uint8_t exact[3] = {clamp255((c + kRv * v) >> 16), clamp255((c - kGu * u - kGv * v) >> 16),
                                          clamp255((c + kBu * u) >> 16)};
                uint8_t fast[4];
                vita::yuvFastPixel(uint8_t(Y), uint8_t(U), uint8_t(V), fast);
                for (int k = 0; k < 3; ++k) {
                    const int d = std::abs(int(fast[k]) - int(exact[k]));
                    maxDiff = std::max(maxDiff, d), differ += d != 0, ++total;
                }
                if (fast[3] != 255) maxDiff = 999;
            }
    std::printf("Colour, every Y, U, V: %.2f%% of channel values differ from the exact conversion, largest difference %d\n",
                100.0 * double(differ) / double(total), maxDiff);
    return maxDiff <= 2;
}

}  // namespace

int main() {
    const bool idct = checkIdct(), yuv = checkYuv();
    std::printf("%s\n", idct && yuv ? "ok" : "FAILED");
    return idct && yuv ? 0 : 1;
}
