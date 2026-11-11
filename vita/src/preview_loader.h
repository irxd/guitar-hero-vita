// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// PS Vita: song previews built on a worker thread.
//
// Upstream's song_preview read the song's whole .vgs (18-36 MB) from the disc
// image on the main thread: the song list froze for a second or more at each
// selection, and with the heap fragmented the 18 MB allocation failed
// (hardware, 2026-11-11). App asks for a preview here and keeps drawing; the
// worker builds the stream (only the preview's window of the file, see
// VgsStream's windowed constructor) and App takes it when it is ready. Only
// the latest request counts: one still loading when another comes is dropped.
//
// The worker reads the disc image through GameData; on the Vita its reads
// are positional (sceIoPread, patch 0020, data/iso9660.cpp), so the main
// thread can read at the same time.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class VgsStream;

namespace vita {

class PreviewLoader {
public:
    using Build = std::function<std::unique_ptr<VgsStream>()>;
    PreviewLoader();
    ~PreviewLoader();  // waits for a build in progress
    PreviewLoader(const PreviewLoader&) = delete;
    PreviewLoader& operator=(const PreviewLoader&) = delete;

    // Replaces any waiting request; returns its ticket.
    uint64_t request(Build build, std::string what);
    // The stream for `ticket` once built (null before, or when it failed:
    // the error is logged). Results of older tickets are dropped.
    std::unique_ptr<VgsStream> take(uint64_t ticket);

private:
    static int threadMain(void* self);
    void run();

    SDL_Thread* thread_ = nullptr;
    SDL_Mutex* lock_ = nullptr;
    SDL_Condition* wake_ = nullptr;
    bool quit_ = false;
    uint64_t nextTicket_ = 0;
    uint64_t wantTicket_ = 0;  // the waiting request (0: none)
    Build want_;
    std::string wantWhat_;
    uint64_t readyTicket_ = 0;
    std::unique_ptr<VgsStream> ready_;
};

}  // namespace vita
