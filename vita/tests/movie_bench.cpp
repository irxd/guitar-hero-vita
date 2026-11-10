// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Desktop benchmark of gd01's movie path on the user's own disc image:
// PssMovie demux + Mpeg2Decoder decode, then mpeg2ToRgba, timed apart, with
// the picture-type mix. Lists the disc's videos when no movie is given.
//   movie_bench <iso> [videos/<name>.pss] [frames]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "data/game_data.h"
#include "video/pss.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <iso> [videos/<name>.pss] [frames]\n", argv[0]);
        return 2;
    }
    GameData data(argv[1]);
    if (argc < 3) {
        for (const std::string& p : data.paths())
            if (p.rfind("videos/", 0) == 0) std::printf("%s\n", p.c_str());
        return 0;
    }
    const std::string path = argv[2];
    const int maxFrames = argc > 3 ? std::atoi(argv[3]) : 300;
    uint64_t pos = 0;
    PssMovie movie([&](uint8_t* dst, size_t n) {
        size_t got = data.readRange(path, pos, dst, n);
        pos += got;
        return got;
    });

    using Clock = std::chrono::steady_clock;
    double decodeMs = 0, convertMs = 0;
    std::map<char, int> types;
    std::vector<uint8_t> rgba;
    int frames = 0, w = 0, h = 0, interlaced = 0;
    std::vector<int16_t> pcm;
    while (frames < maxFrames) {
        const auto t0 = Clock::now();
        const Mpeg2Frame* f = movie.nextFrame();
        const auto t1 = Clock::now();
        if (!f) break;
        movie.takeAudio(pcm);
        pcm.clear();
        w = f->width, h = f->height;
        rgba.resize(size_t(w) * size_t(h) * 4);
        mpeg2ToRgba(*f, rgba.data(), size_t(w) * 4, !f->progressive);
        const auto t2 = Clock::now();
        decodeMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        convertMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
        ++types[f->type];
        interlaced += f->progressive ? 0 : 1;
        ++frames;
    }
    if (!frames) {
        std::printf("no frames decoded\n");
        return 1;
    }
    std::printf("%s: %d frames %dx%d (%d interlaced), %.1f MB read\n", path.c_str(), frames, w, h, interlaced, double(pos) / 1048576.0);
    std::printf("  types: I %d, P %d, B %d\n", types['I'], types['P'], types['B']);
    std::printf("  per frame: decode %.2f ms, convert %.2f ms (real time needs < %.1f ms total)\n", decodeMs / frames,
                convertMs / frames, 1000.0 / PssMovie::kFrameRate);
    return 0;
}
