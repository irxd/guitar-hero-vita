// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// PS Vita: NEON versions of the MPEG-2 decoder's inner loops (video/mpeg2.cpp),
// hooked in by patch 0007.
//
// A 640x448 movie picture took ~90 ms to decode on the Vita's A9 (hardware,
// 2026-11-10), against a 33 ms budget. Each function here computes exactly
// what upstream's scalar loop computes (same sums in the same order for the
// float IDCT; exact integer identities for the pixel loops), four to sixteen
// lanes at a time. On first use each one is checked against a scalar copy;
// if any result differs it declines (returns false) and the caller runs
// upstream's code.
#pragma once

#include <cstddef>
#include <cstdint>

namespace vita {

// 8x8 inverse DCT. in: 64 coefficients, raster order; rowMask bit y set when
// row y has a nonzero coefficient. out: 64 residuals clamped to [-256, 255].
bool idct8x8Neon(const int16_t* in, int* out, uint8_t rowMask);
void idct8x8Scalar(const int16_t* in, int* out, uint8_t rowMask);  // reference, for tests

// Half-pel motion compensation of a w x h block (w 8 or 16): dst = prediction
// from src at half-pel offsets (hx, hy), or averaged into dst when avg.
bool predictNeon(bool avg, uint8_t* dst, int dstStride, const uint8_t* src, int srcStride, int w, int h, int hx,
                 int hy);

// Block output: 8x8 residuals written (intra) or added (non-intra) to dst,
// clamped to 0..255.
bool addBlockNeon(uint8_t* dst, int stride, const int* resid, bool intra);

// mpeg2ToRgba: 4:2:0 planes to RGBA (BT.601 limited range), `blend` averaging
// each line with the next (deinterlace of interlaced pictures).
bool yuvToRgbaNeon(const uint8_t* y, int lumaStride, const uint8_t* cb, const uint8_t* cr, int chromaStride, int width,
                   int height, bool progressive, bool blend, uint8_t* dst, size_t dstStride);

// Late-picture policy (movie worker): while set, the decoder skips B
// pictures (not references) and keeps their display indices.
void setMpeg2SkipBPictures(bool skip);
bool mpeg2SkipBPictures();
void countSkippedBPicture();
uint32_t takeSkippedBPictures();  // since the last call

// Calls to the first three functions above since the last call, then reset (movie
// worker thread only). The self-tests log each one's cost per call.
void takeMpeg2NeonCalls(uint32_t& idct, uint32_t& mc, uint32_t& add);

}  // namespace vita
