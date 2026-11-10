// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "mpeg2_neon.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace vita {
namespace {

// c[x][u] = C(u)/2 * cos((2x+1)u*pi/16), built exactly as upstream's table.
struct Table {
    float c[8][8];   // [x][u]
    float ct[8][8];  // transposed: ct[u][x] = c[x][u]
    Table() {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u) {
                c[x][u] = static_cast<float>((u ? 0.5 : 0.5 / std::sqrt(2.0)) *
                                             std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16));
                ct[u][x] = c[x][u];
            }
    }
};

const Table& table() {
    static const Table t;
    return t;
}

// Calls per function (movie worker thread only), for the per-picture stats.
uint32_t gIdctCalls = 0, gMcCalls = 0, gAddCalls = 0;

// Times `runs` passes of f with one timer pair: microseconds per pass.
template <typename F>
double usPer(int runs, F&& f) {
    const uint64_t t0 = SDL_GetTicksNS();
    for (int i = 0; i < runs; ++i) f(i);
    return double(SDL_GetTicksNS() - t0) / 1e3 / runs;
}

inline uint8_t clamp255(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// Upstream's predictBlockT, for the self-test.
[[maybe_unused]] void predictScalar(bool avg, uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int w,
                                    int h, int hx, int hy) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* s0 = src + y * srcStride;
        const uint8_t* s1 = s0 + srcStride;
        uint8_t* d = dst + y * dstStride;
        for (int x = 0; x < w; ++x) {
            int p;
            if (!hx && !hy) p = s0[x];
            else if (hx && !hy) p = (s0[x] + s0[x + 1] + 1) >> 1;
            else if (!hx) p = (s0[x] + s1[x] + 1) >> 1;
            else p = (s0[x] + s0[x + 1] + s1[x] + s1[x + 1] + 2) >> 2;
            d[x] = static_cast<uint8_t>(avg ? (d[x] + p + 1) >> 1 : p);
        }
    }
}

// Upstream's block output loop, for the self-test.
[[maybe_unused]] void addBlockScalar(uint8_t* dst, int stride, const int* resid, bool intra) {
    for (int y = 0; y < 8; ++y) {
        uint8_t* d = dst + y * stride;
        const int* r = resid + y * 8;
        if (intra) for (int x = 0; x < 8; ++x) d[x] = clamp255(r[x]);
        else for (int x = 0; x < 8; ++x) d[x] = clamp255(d[x] + r[x]);
    }
}

#if defined(__ARM_NEON)

// --- Inverse DCT -------------------------------------------------------------

// floor(v + 0.5) to int, clamped to [-256, 255], as upstream's
// std::clamp(int(std::floor(s + 0.5f)), -256, 255).
inline int32x4_t roundClamp(float32x4_t s) {
    const float32x4_t v = vaddq_f32(s, vdupq_n_f32(0.5f));
    int32x4_t i = vcvtq_s32_f32(v);  // toward zero
    // floor: one less where truncation went up (negative non-integers).
    const uint32x4_t up = vcgtq_f32(vcvtq_f32_s32(i), v);
    i = vaddq_s32(i, vreinterpretq_s32_u32(up));  // up lanes are all ones (-1)
    return vminq_s32(vmaxq_s32(i, vdupq_n_s32(-256)), vdupq_n_s32(255));
}

// All arithmetic stays in NEON registers: on the Cortex-A9 every value moved
// between the VFP and NEON pipelines stalls, and a first version that
// converted each coefficient on the VFP was barely faster than scalar code
// (12.7 vs 16.4 us a block, hardware 2026-11-10).
//
// Exactness: the scalar code computes s = 0; s += c * x for every term in
// order, with separate (non-fused) multiply and add. vmla.f32 is also
// non-fused, each lane starts from 0 and adds the same terms in the same
// order. Terms with x = 0 add +-0, which leaves s unchanged, so zero rows can
// be skipped. Values stay far from the denormal range NEON flushes.
#define VITA_MLA(acc, m, v, half, lane) acc = vmlaq_lane_f32(acc, m, vget_##half##_f32(v), lane)

void idctNeon(const int16_t* in, int* out, uint8_t rowMask) {
    const Table& t = table();
    const float32x4_t zero = vdupq_n_f32(0.0f);

    // DC only: every output is the same value.
    if (rowMask == 1) {
        const int16x8_t r0 = vld1q_s16(in);
        const uint64x2_t nz = vreinterpretq_u64_s16(vsetq_lane_s16(0, r0, 0));
        if ((vgetq_lane_u64(nz, 0) | vgetq_lane_u64(nz, 1)) == 0) {
            const float32x4_t dc = vdupq_n_f32(float(in[0]));
            const float32x4_t c0 = vdupq_n_f32(t.c[0][0]);  // c[x][0] is the same for every x
            const float32x4_t pass1 = vmlaq_f32(zero, c0, dc);
            const int32x4_t v = roundClamp(vmlaq_f32(zero, c0, pass1));
            for (int i = 0; i < 64; i += 4) vst1q_s32(out + i, v);
            return;
        }
    }

    float tmp[8][8];
    // Two rows at a time: four independent accumulator chains keep the A9's
    // NEON pipeline busy (one chain of 8 dependent vmla waits on latency).
    // Pass 1, row y: tmp[y][x] = sum over u (in order) of c[x][u] * in[y][u].
    float32x4_t ct[8][2];
    for (int u = 0; u < 8; ++u) ct[u][0] = vld1q_f32(t.ct[u]), ct[u][1] = vld1q_f32(t.ct[u] + 4);
#define VITA_PASS1(LO, HI, R03, R47)                                            \
    VITA_MLA(LO, ct[0][0], R03, low, 0);  VITA_MLA(HI, ct[0][1], R03, low, 0);  \
    VITA_MLA(LO, ct[1][0], R03, low, 1);  VITA_MLA(HI, ct[1][1], R03, low, 1);  \
    VITA_MLA(LO, ct[2][0], R03, high, 0); VITA_MLA(HI, ct[2][1], R03, high, 0); \
    VITA_MLA(LO, ct[3][0], R03, high, 1); VITA_MLA(HI, ct[3][1], R03, high, 1); \
    VITA_MLA(LO, ct[4][0], R47, low, 0);  VITA_MLA(HI, ct[4][1], R47, low, 0);  \
    VITA_MLA(LO, ct[5][0], R47, low, 1);  VITA_MLA(HI, ct[5][1], R47, low, 1);  \
    VITA_MLA(LO, ct[6][0], R47, high, 0); VITA_MLA(HI, ct[6][1], R47, high, 0); \
    VITA_MLA(LO, ct[7][0], R47, high, 1); VITA_MLA(HI, ct[7][1], R47, high, 1);
    for (int y = 0; y < 8; y += 2) {
        const bool m0 = rowMask >> y & 1, m1 = rowMask >> (y + 1) & 1;
        if (!m0 && !m1) continue;
        const int16x8_t ra = vld1q_s16(in + y * 8), rb = vld1q_s16(in + y * 8 + 8);
        const float32x4_t a03 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(ra)));
        const float32x4_t a47 = vcvtq_f32_s32(vmovl_s16(vget_high_s16(ra)));
        const float32x4_t b03 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(rb)));
        const float32x4_t b47 = vcvtq_f32_s32(vmovl_s16(vget_high_s16(rb)));
        float32x4_t alo = zero, ahi = zero, blo = zero, bhi = zero;
        if (m0 && m1) {
            VITA_PASS1(alo, ahi, a03, a47)
            VITA_PASS1(blo, bhi, b03, b47)
        } else if (m0) {
            VITA_PASS1(alo, ahi, a03, a47)
        } else {
            VITA_PASS1(blo, bhi, b03, b47)
        }
        // A row whose mask bit is clear is all zero; pass 2 skips it.
        if (m0) vst1q_f32(tmp[y], alo), vst1q_f32(tmp[y] + 4, ahi);
        if (m1) vst1q_f32(tmp[y + 1], blo), vst1q_f32(tmp[y + 1] + 4, bhi);
    }
#undef VITA_PASS1
    // Pass 2, output row y: out[y][x] = sum over v (in order) of c[y][v] * tmp[v][x].
    for (int y = 0; y < 8; y += 2) {
        const float32x4_t a03 = vld1q_f32(t.c[y]), a47 = vld1q_f32(t.c[y] + 4);
        const float32x4_t b03 = vld1q_f32(t.c[y + 1]), b47 = vld1q_f32(t.c[y + 1] + 4);
        float32x4_t alo = zero, ahi = zero, blo = zero, bhi = zero;
#define VITA_ROW(v, half, lane, A, B)                                         \
    if (rowMask >> v & 1) {                                                   \
        const float32x4_t tl = vld1q_f32(tmp[v]), th = vld1q_f32(tmp[v] + 4); \
        VITA_MLA(alo, tl, A, half, lane);                                     \
        VITA_MLA(ahi, th, A, half, lane);                                     \
        VITA_MLA(blo, tl, B, half, lane);                                     \
        VITA_MLA(bhi, th, B, half, lane);                                     \
    }
        VITA_ROW(0, low, 0, a03, b03) VITA_ROW(1, low, 1, a03, b03) VITA_ROW(2, high, 0, a03, b03)
        VITA_ROW(3, high, 1, a03, b03) VITA_ROW(4, low, 0, a47, b47) VITA_ROW(5, low, 1, a47, b47)
        VITA_ROW(6, high, 0, a47, b47) VITA_ROW(7, high, 1, a47, b47)
#undef VITA_ROW
        vst1q_s32(out + y * 8, roundClamp(alo));
        vst1q_s32(out + y * 8 + 4, roundClamp(ahi));
        vst1q_s32(out + y * 8 + 8, roundClamp(blo));
        vst1q_s32(out + y * 8 + 12, roundClamp(bhi));
    }
}
#undef VITA_MLA

// Pseudo-random coefficient blocks (DC only, sparse and dense) compared
// with the scalar reference. True when all match.
bool idctSelfTest() {
    uint32_t seed = 0x1234567u;
    auto rnd = [&]() { return seed = seed * 1664525u + 1013904223u; };
    static int16_t ins[2000][64];
    static uint8_t masks[2000];
    int a[64], b[64];
    int bad = 0;
    const int kBlocks = 2000;
    for (int n = 0; n < kBlocks; ++n) {
        int16_t* in = ins[n];
        uint8_t& mask = masks[n];
        mask = 0;
        const int density = n % 4;  // 0: DC only, 1: sparse, 2-3: dense
        for (int i = 0; i < 64; ++i) {
            int v = 0;
            if (i == 0) v = int(rnd() >> 20) - 2048;
            else if (density == 1 && (rnd() & 15) == 0) v = int(rnd() >> 24) - 128;
            else if (density >= 2 && (rnd() & 1)) v = int(rnd() >> 21) - 1024;
            in[i] = int16_t(v);
            if (v) mask |= uint8_t(1u << (i / 8));
        }
        idct8x8Scalar(in, a, mask);
        idctNeon(in, b, mask);
        if (!std::equal(a, a + 64, b)) ++bad;
    }
    const double neon = usPer(kBlocks, [&](int i) { idctNeon(ins[i], a, masks[i]); });
    const double scalar = usPer(kBlocks, [&](int i) { idct8x8Scalar(ins[i], a, masks[i]); });
    SDL_Log("vita: NEON IDCT self-test: %d of %d blocks differ%s; %.2f us a block (scalar %.2f)", bad, kBlocks,
            bad ? " (using the scalar code)" : "", neon, scalar);
    return bad == 0;
}

// --- Motion compensation -----------------------------------------------------

// One row of 8 or 16 predicted pixels. vrhadd = (a + b + 1) >> 1 and
// vrshrn #2 = (sum + 2) >> 2: exactly the scalar formulas.
uint8x16_t pred16(const uint8_t* s0, const uint8_t* s1, int hx, int hy) {
    if (!hx && !hy) return vld1q_u8(s0);
    if (hx && !hy) return vrhaddq_u8(vld1q_u8(s0), vld1q_u8(s0 + 1));
    if (!hx) return vrhaddq_u8(vld1q_u8(s0), vld1q_u8(s1));
    const uint8x16_t a = vld1q_u8(s0), b = vld1q_u8(s0 + 1), c = vld1q_u8(s1), d = vld1q_u8(s1 + 1);
    const uint16x8_t lo = vaddq_u16(vaddl_u8(vget_low_u8(a), vget_low_u8(b)), vaddl_u8(vget_low_u8(c), vget_low_u8(d)));
    const uint16x8_t hi =
        vaddq_u16(vaddl_u8(vget_high_u8(a), vget_high_u8(b)), vaddl_u8(vget_high_u8(c), vget_high_u8(d)));
    return vcombine_u8(vrshrn_n_u16(lo, 2), vrshrn_n_u16(hi, 2));
}

uint8x8_t pred8(const uint8_t* s0, const uint8_t* s1, int hx, int hy) {
    if (!hx && !hy) return vld1_u8(s0);
    if (hx && !hy) return vrhadd_u8(vld1_u8(s0), vld1_u8(s0 + 1));
    if (!hx) return vrhadd_u8(vld1_u8(s0), vld1_u8(s1));
    const uint16x8_t sum = vaddq_u16(vaddl_u8(vld1_u8(s0), vld1_u8(s0 + 1)), vaddl_u8(vld1_u8(s1), vld1_u8(s1 + 1)));
    return vrshrn_n_u16(sum, 2);
}

void predictNeonImpl(bool avg, uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int w, int h, int hx,
                     int hy) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* s0 = src + y * srcStride;
        uint8_t* d = dst + y * dstStride;
        if (w == 16) {
            uint8x16_t p = pred16(s0, s0 + srcStride, hx, hy);
            if (avg) p = vrhaddq_u8(vld1q_u8(d), p);
            vst1q_u8(d, p);
        } else {
            uint8x8_t p = pred8(s0, s0 + srcStride, hx, hy);
            if (avg) p = vrhadd_u8(vld1_u8(d), p);
            vst1_u8(d, p);
        }
    }
}

bool predictSelfTest() {
    uint32_t seed = 0x89abcdefu;
    auto rnd = [&]() { return seed = seed * 1664525u + 1013904223u; };
    uint8_t src[18 * 40], a[17 * 17], b[17 * 17];
    int bad = 0, cases = 0;
    for (int n = 0; n < 50; ++n) {
        for (auto& v : src) v = uint8_t(rnd() >> 24);
        for (int w : {8, 16})
            for (int h : {4, 8, 16})
                for (int avg = 0; avg < 2; ++avg)
                    for (int hx = 0; hx < 2; ++hx)
                        for (int hy = 0; hy < 2; ++hy) {
                            for (size_t i = 0; i < sizeof a; ++i) a[i] = b[i] = uint8_t(rnd() >> 24);
                            // Odd strides and offset: unaligned, like the real planes.
                            predictScalar(avg, a, 17, src + 1, 18, w, h, hx, hy);
                            predictNeonImpl(avg, b, 17, src + 1, 18, w, h, hx, hy);
                            ++cases;
                            if (!std::equal(a, a + sizeof a, b)) ++bad;
                        }
    }
    const double neon = usPer(4000, [&](int i) { predictNeonImpl(i & 1, b, 17, src + 1, 18, 16, 16, i >> 1 & 1, i >> 2 & 1); });
    const double scalar = usPer(4000, [&](int i) { predictScalar(i & 1, a, 17, src + 1, 18, 16, 16, i >> 1 & 1, i >> 2 & 1); });
    SDL_Log("vita: NEON motion compensation self-test: %d of %d cases differ%s; %.2f us a 16x16 block (scalar %.2f)", bad,
            cases, bad ? " (using the scalar code)" : "", neon, scalar);
    return bad == 0;
}

// --- Block output --------------------------------------------------------------

// Residuals are in [-256, 255] so they narrow to int16 exactly and d + r fits
// int16; vqmovun_s16 saturates to 0..255, which is clamp255.
void addBlockNeonImpl(uint8_t* dst, int stride, const int* resid, bool intra) {
    for (int y = 0; y < 8; ++y) {
        uint8_t* d = dst + y * stride;
        const int* r = resid + y * 8;
        int16x8_t v = vcombine_s16(vmovn_s32(vld1q_s32(r)), vmovn_s32(vld1q_s32(r + 4)));
        if (!intra) v = vaddq_s16(v, vreinterpretq_s16_u16(vmovl_u8(vld1_u8(d))));
        vst1_u8(d, vqmovun_s16(v));
    }
}

bool addBlockSelfTest() {
    uint32_t seed = 0x2468aceu;
    auto rnd = [&]() { return seed = seed * 1664525u + 1013904223u; };
    uint8_t a[8 * 9], b[8 * 9];
    int resid[64];
    int bad = 0;
    const int kBlocks = 1000;
    for (int n = 0; n < kBlocks; ++n) {
        for (int& r : resid) r = int(rnd() % 512) - 256;
        for (size_t i = 0; i < sizeof a; ++i) a[i] = b[i] = uint8_t(rnd() >> 24);
        const bool intra = n & 1;
        addBlockScalar(a, 9, resid, intra);
        addBlockNeonImpl(b, 9, resid, intra);
        if (!std::equal(a, a + sizeof a, b)) ++bad;
    }
    const double neon = usPer(4000, [&](int i) { addBlockNeonImpl(b, 9, resid, i & 1); });
    const double scalar = usPer(4000, [&](int i) { addBlockScalar(a, 9, resid, i & 1); });
    SDL_Log("vita: NEON block output self-test: %d of %d blocks differ%s; %.2f us a block (scalar %.2f)", bad, kBlocks,
            bad ? " (using the scalar code)" : "", neon, scalar);
    return bad == 0;
}

// --- YUV 4:2:0 to RGBA ------------------------------------------------------------

struct Planes {
    const uint8_t *y, *cb, *cr;
    int lumaStride, chromaStride, width, height;
    bool progressive;
};

// Upstream's mpeg2ToRgba row (BT.601 limited range, 16.16 fixed point), for
// the self-test and for the last width % 16 pixels of a row.
constexpr int kY = 76309, kRv = 104597, kGu = 25675, kGv = 53279, kBu = 132201;

inline int chromaRow(const Planes& f, int y) { return f.progressive ? y >> 1 : ((y >> 2) << 1) | (y & 1); }

void yuvRowScalar(const Planes& f, int y, bool blend, uint8_t* d, int x0) {
    const int y2 = std::min(y + 1, f.height - 1);
    const int cy = chromaRow(f, y), cy2 = chromaRow(f, y2);
    const uint8_t* ly = f.y + size_t(y) * f.lumaStride;
    const uint8_t* ly2 = f.y + size_t(y2) * f.lumaStride;
    const uint8_t* lu = f.cb + size_t(cy) * f.chromaStride;
    const uint8_t* lv = f.cr + size_t(cy) * f.chromaStride;
    const uint8_t* lu2 = f.cb + size_t(cy2) * f.chromaStride;
    const uint8_t* lv2 = f.cr + size_t(cy2) * f.chromaStride;
    d += size_t(x0) * 4;
    for (int x = x0; x < f.width; ++x) {
        int Y = ly[x], U = lu[x >> 1], V = lv[x >> 1];
        if (blend) {
            Y = (Y + ly2[x] + 1) >> 1;
            U = (U + lu2[x >> 1] + 1) >> 1;
            V = (V + lv2[x >> 1] + 1) >> 1;
        }
        const int c = (Y - 16) * kY + 32768;
        U -= 128;
        V -= 128;
        d[0] = clamp255((c + kRv * V) >> 16);
        d[1] = clamp255((c - kGu * U - kGv * V) >> 16);
        d[2] = clamp255((c + kBu * U) >> 16);
        d[3] = 255;
        d += 4;
    }
}

// Four pixels: (c + k) >> 16 in 32-bit lanes, then saturating narrows to
// 0..255 (exactly clamp255).
inline uint16x4_t channel(int32x4_t v) { return vqmovun_s32(vshrq_n_s32(v, 16)); }

void yuvRowNeon(const Planes& f, int y, bool blend, uint8_t* d) {
    const int y2 = std::min(y + 1, f.height - 1);
    const int cy = chromaRow(f, y), cy2 = chromaRow(f, y2);
    const uint8_t* ly = f.y + size_t(y) * f.lumaStride;
    const uint8_t* ly2 = f.y + size_t(y2) * f.lumaStride;
    const uint8_t* lu = f.cb + size_t(cy) * f.chromaStride;
    const uint8_t* lv = f.cr + size_t(cy) * f.chromaStride;
    const uint8_t* lu2 = f.cb + size_t(cy2) * f.chromaStride;
    const uint8_t* lv2 = f.cr + size_t(cy2) * f.chromaStride;
    const int simdEnd = f.width & ~15;
    uint8x16x4_t px;
    px.val[3] = vdupq_n_u8(255);
    for (int x = 0; x < simdEnd; x += 16) {
        uint8x16_t Y = vld1q_u8(ly + x);
        uint8x8_t U = vld1_u8(lu + x / 2), V = vld1_u8(lv + x / 2);
        if (blend) {
            Y = vrhaddq_u8(Y, vld1q_u8(ly2 + x));
            U = vrhadd_u8(U, vld1_u8(lu2 + x / 2));
            V = vrhadd_u8(V, vld1_u8(lv2 + x / 2));
        }
        const uint8x8x2_t uu = vzip_u8(U, U), vv = vzip_u8(V, V);  // each chroma sample twice
        const uint8x8_t ys[2] = {vget_low_u8(Y), vget_high_u8(Y)};
        uint16x8_t r[2], g[2], b[2];
        for (int h = 0; h < 2; ++h) {
            const int16x8_t y16 = vreinterpretq_s16_u16(vmovl_u8(ys[h]));
            const int16x8_t u16 = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(uu.val[h])), vdupq_n_s16(128));
            const int16x8_t v16 = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vv.val[h])), vdupq_n_s16(128));
            uint16x4_t rr[2], gg[2], bb[2];
            for (int q = 0; q < 2; ++q) {
                const int32x4_t yq = vmovl_s16(q ? vget_high_s16(y16) : vget_low_s16(y16));
                const int32x4_t uq = vmovl_s16(q ? vget_high_s16(u16) : vget_low_s16(u16));
                const int32x4_t vq = vmovl_s16(q ? vget_high_s16(v16) : vget_low_s16(v16));
                const int32x4_t c = vmlaq_n_s32(vdupq_n_s32(32768 - 16 * kY), yq, kY);
                rr[q] = channel(vmlaq_n_s32(c, vq, kRv));
                gg[q] = channel(vmlsq_n_s32(vmlsq_n_s32(c, uq, kGu), vq, kGv));
                bb[q] = channel(vmlaq_n_s32(c, uq, kBu));
            }
            r[h] = vcombine_u16(rr[0], rr[1]);
            g[h] = vcombine_u16(gg[0], gg[1]);
            b[h] = vcombine_u16(bb[0], bb[1]);
        }
        px.val[0] = vcombine_u8(vqmovn_u16(r[0]), vqmovn_u16(r[1]));
        px.val[1] = vcombine_u8(vqmovn_u16(g[0]), vqmovn_u16(g[1]));
        px.val[2] = vcombine_u8(vqmovn_u16(b[0]), vqmovn_u16(b[1]));
        vst4q_u8(d + size_t(x) * 4, px);
    }
    if (simdEnd < f.width) yuvRowScalar(f, y, blend, d, simdEnd);
}

void yuvNeonImpl(const Planes& f, bool blend, uint8_t* dst, size_t dstStride) {
    for (int y = 0; y < f.height; ++y) yuvRowNeon(f, y, blend, dst + size_t(y) * dstStride);
}

bool yuvSelfTest() {
    uint32_t seed = 0x13579bdu;
    auto rnd = [&]() { return seed = seed * 1664525u + 1013904223u; };
    // 40 wide: two NEON groups plus an 8-pixel scalar tail; odd strides.
    constexpr int W = 40, H = 12, LS = 43, CS = 23;
    static uint8_t yp[LS * H], up[CS * H / 2], vp[CS * H / 2], a[W * 4 * H], b[W * 4 * H];
    int bad = 0, cases = 0;
    for (int n = 0; n < 64; ++n) {
        for (auto& v : yp) v = uint8_t(rnd() >> 24);
        for (auto& v : up) v = uint8_t(rnd() >> 24);
        for (auto& v : vp) v = uint8_t(rnd() >> 24);
        const Planes f{yp, up, vp, LS, CS, W, H, (n & 1) != 0};
        const bool blend = n & 2;
        for (int y = 0; y < H; ++y) yuvRowScalar(f, y, blend, a + y * W * 4, 0);
        yuvNeonImpl(f, blend, b, W * 4);
        ++cases;
        if (!std::equal(a, a + sizeof a, b)) ++bad;
    }
    const Planes big{yp, up, vp, LS, CS, 32, H, true};
    const double neon = usPer(200, [&](int) { yuvNeonImpl(big, false, b, W * 4); });
    const double scalar = usPer(200, [&](int) {
        for (int y = 0; y < H; ++y) yuvRowScalar(big, y, false, a + y * W * 4, 0);
    });
    SDL_Log("vita: NEON YUV to RGBA self-test: %d of %d pictures differ%s; %.2f us per 32x12 (scalar %.2f)", bad, cases,
            bad ? " (using the scalar code)" : "", neon, scalar);
    return bad == 0;
}

#endif  // __ARM_NEON

}  // namespace

void idct8x8Scalar(const int16_t* in, int* out, uint8_t rowMask) {
    const auto& c = table().c;
    float tmp[64];
    for (int y = 0; y < 8; ++y) {
        float* t = tmp + y * 8;
        if (!(rowMask >> y & 1)) {
            std::fill(t, t + 8, 0.0f);
            continue;
        }
        const int16_t* r = in + y * 8;
        for (int x = 0; x < 8; ++x) {
            float s = 0;
            for (int u = 0; u < 8; ++u) s += c[x][u] * r[u];
            t[x] = s;
        }
    }
    for (int x = 0; x < 8; ++x) {
        float col[8];
        for (int v = 0; v < 8; ++v) col[v] = tmp[v * 8 + x];
        for (int y = 0; y < 8; ++y) {
            float s = 0;
            for (int v = 0; v < 8; ++v) s += c[y][v] * col[v];
            int i = static_cast<int>(std::floor(s + 0.5f));
            out[y * 8 + x] = std::clamp(i, -256, 255);
        }
    }
}

bool idct8x8Neon(const int16_t* in, int* out, uint8_t rowMask) {
#if defined(__ARM_NEON)
    static const bool ok = idctSelfTest();
    if (!ok) return false;
    ++gIdctCalls;
    idctNeon(in, out, rowMask);
    return true;
#else
    (void)in, (void)out, (void)rowMask;
    return false;
#endif
}

bool predictNeon(bool avg, uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int w, int h, int hx,
                 int hy) {
#if defined(__ARM_NEON)
    static const bool ok = predictSelfTest();
    if (!ok || (w != 8 && w != 16)) return false;
    ++gMcCalls;
    predictNeonImpl(avg, dst, dstStride, src, srcStride, w, h, hx, hy);
    return true;
#else
    (void)avg, (void)dst, (void)dstStride, (void)src, (void)srcStride, (void)w, (void)h, (void)hx, (void)hy;
    return false;
#endif
}

bool addBlockNeon(uint8_t* dst, int stride, const int* resid, bool intra) {
#if defined(__ARM_NEON)
    static const bool ok = addBlockSelfTest();
    if (!ok) return false;
    ++gAddCalls;
    addBlockNeonImpl(dst, stride, resid, intra);
    return true;
#else
    (void)dst, (void)stride, (void)resid, (void)intra;
    return false;
#endif
}

bool yuvToRgbaNeon(const uint8_t* y, int lumaStride, const uint8_t* cb, const uint8_t* cr, int chromaStride, int width,
                   int height, bool progressive, bool blend, uint8_t* dst, size_t dstStride) {
#if defined(__ARM_NEON)
    static const bool ok = yuvSelfTest();
    if (!ok) return false;
    yuvNeonImpl(Planes{y, cb, cr, lumaStride, chromaStride, width, height, progressive}, blend, dst, dstStride);
    return true;
#else
    (void)y, (void)lumaStride, (void)cb, (void)cr, (void)chromaStride, (void)width, (void)height, (void)progressive,
        (void)blend, (void)dst, (void)dstStride;
    return false;
#endif
}

namespace {
std::atomic<bool> gSkipB{false};
std::atomic<uint32_t> gSkippedB{0};
}  // namespace

void setMpeg2SkipBPictures(bool skip) { gSkipB.store(skip, std::memory_order_relaxed); }
bool mpeg2SkipBPictures() { return gSkipB.load(std::memory_order_relaxed); }
void countSkippedBPicture() { gSkippedB.fetch_add(1, std::memory_order_relaxed); }
uint32_t takeSkippedBPictures() { return gSkippedB.exchange(0, std::memory_order_relaxed); }

void takeMpeg2NeonCalls(uint32_t& idct, uint32_t& mc, uint32_t& add) {
    idct = gIdctCalls, mc = gMcCalls, add = gAddCalls;
    gIdctCalls = gMcCalls = gAddCalls = 0;
}

}  // namespace vita
