// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// PS Vita: the .pss movies decoded on a worker thread (another core).
//
// On the main thread, MPEG-2 decoding (~25 ms a picture on the Vita's A9)
// plus the RGBA conversion (~12 ms) ran with the menus' own work, and the
// intro movie played at 6-9 FPS (hardware, 2026-11-10). The worker owns its
// own PssMovie: it decodes ahead, converts the pictures that will be shown
// (late ones are only decoded), queues up to kMaxReady of them, and sends
// the movie's PCM straight to the SDL audio stream (thread safe).
//
// The disc is still read on the main thread only (GameData shares one file
// handle): App feeds the file's bytes in chunks while wantBytes() asks for
// them, and the worker's PssMovie reads from that buffer.
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace vita {

class MovieWorker {
public:
    // `audio` may be null (no sound); it must outlive the worker.
    explicit MovieWorker(SDL_AudioStream* audio);
    ~MovieWorker();  // stops and joins the thread
    MovieWorker(const MovieWorker&) = delete;
    MovieWorker& operator=(const MovieWorker&) = delete;

    // Main thread: room left in the input buffer, then the file's next bytes;
    // feedEnd() once the file is exhausted.
    size_t wantBytes() const;
    void feed(const uint8_t* data, size_t size);
    void feedEnd();

    // Main thread: the newest converted picture whose display index is at
    // most `target` (the movie clock's frame), swapped into rgba / width /
    // height. Returns false when there is none yet (keep showing the last).
    bool take(uint64_t target, std::vector<uint8_t>& rgba, int& width, int& height);

    bool started() const;       // the first picture is ready: start the clock
    bool finished() const;      // the movie ended and every picture was taken
    std::string error() const;  // why decoding stopped, empty if it did not fail

private:
    struct Picture {
        uint64_t index = 0;
        int width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };
    static constexpr size_t kInputBytes = 1u << 20;  // ~40 pictures of a typical .pss
    static constexpr size_t kMaxReady = 3;

    static int threadMain(void* self);
    void run();
    size_t read(uint8_t* dst, size_t n);  // the worker PssMovie's reader

    SDL_AudioStream* audio_;
    SDL_Thread* thread_ = nullptr;
    SDL_Mutex* lock_ = nullptr;
    SDL_Condition* changed_ = nullptr;  // any state below changed

    std::vector<uint8_t> input_;
    size_t inputPos_ = 0;
    bool inputEnd_ = false;
    bool stop_ = false;
    bool started_ = false;
    bool done_ = false;
    uint64_t target_ = 0;
    uint64_t readWaitNs_ = 0;  // time the worker waited for the main thread's bytes
    std::deque<Picture> ready_;
    std::vector<std::vector<uint8_t>> spare_;  // RGBA buffers for reuse
    std::string error_;
};

}  // namespace vita
