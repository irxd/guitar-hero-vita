// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "movie_worker.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>

#include "mpeg2_neon.h"
#include "video/pss.h"

#if defined(__vita__)
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#endif

namespace vita {
namespace {

// RAII lock on an SDL_Mutex.
struct Lock {
    explicit Lock(SDL_Mutex* m) : m_(m) { SDL_LockMutex(m_); }
    ~Lock() { SDL_UnlockMutex(m_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    SDL_Mutex* m_;
};

constexpr int kStackBytes = 1 << 20;
// When late, still convert a picture at least every kMaxGap display frames, so
// a decoder slower than real time shows a slower picture rate instead of
// freezing on the first picture (hardware, 2026-11-10).
constexpr uint64_t kMaxGap = 4;
constexpr uint64_t kStatsNs = 5'000'000'000ull;
// B pictures are skipped while the newest decoded picture is more than
// kSkipBehind frames behind the clock, until it is back within kResumeBehind.
constexpr uint64_t kSkipBehind = 2, kResumeBehind = 0;

}  // namespace

MovieWorker::MovieWorker(SDL_AudioStream* audio) : audio_(audio) {
    lock_ = SDL_CreateMutex();
    changed_ = SDL_CreateCondition();
    input_.reserve(kInputBytes);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(&MovieWorker::threadMain));
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "gd01 movie");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, kStackBytes);
    thread_ = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (!thread_) {
        error_ = std::string("no movie thread: ") + SDL_GetError();
        done_ = true;
    }
}

MovieWorker::~MovieWorker() {
    {
        Lock l(lock_);
        stop_ = true;
        SDL_BroadcastCondition(changed_);
    }
    if (thread_) SDL_WaitThread(thread_, nullptr);
    SDL_DestroyCondition(changed_);
    SDL_DestroyMutex(lock_);
}

size_t MovieWorker::wantBytes() const {
    Lock l(lock_);
    const size_t buffered = input_.size() - inputPos_;
    return inputEnd_ || buffered >= kInputBytes ? 0 : kInputBytes - buffered;
}

void MovieWorker::feed(const uint8_t* data, size_t size) {
    Lock l(lock_);
    if (inputPos_ > 0 && inputPos_ >= input_.size() / 2) {  // drop what was read
        input_.erase(input_.begin(), input_.begin() + std::ptrdiff_t(inputPos_));
        inputPos_ = 0;
    }
    input_.insert(input_.end(), data, data + size);
    SDL_BroadcastCondition(changed_);
}

void MovieWorker::feedEnd() {
    Lock l(lock_);
    inputEnd_ = true;
    SDL_BroadcastCondition(changed_);
}

bool MovieWorker::take(uint64_t target, std::vector<uint8_t>& rgba, int& width, int& height) {
    Lock l(lock_);
    target_ = target;
    bool got = false;
    while (!ready_.empty() && ready_.front().index <= target) {
        Picture& p = ready_.front();
        rgba.swap(p.rgba);  // the caller's previous buffer comes back for reuse
        width = p.width, height = p.height;
        spare_.push_back(std::move(p.rgba));
        ready_.pop_front();
        got = true;
    }
    if (got) SDL_BroadcastCondition(changed_);  // room in the queue
    return got;
}

bool MovieWorker::started() const {
    Lock l(lock_);
    return started_;
}

bool MovieWorker::finished() const {
    Lock l(lock_);
    return done_ && ready_.empty();
}

std::string MovieWorker::error() const {
    Lock l(lock_);
    return error_;
}

int MovieWorker::threadMain(void* self) {
    static_cast<MovieWorker*>(self)->run();
    return 0;
}

size_t MovieWorker::read(uint8_t* dst, size_t n) {
    Lock l(lock_);
    if (!stop_ && inputPos_ == input_.size() && !inputEnd_) {
        const uint64_t t0 = SDL_GetTicksNS();
        while (!stop_ && inputPos_ == input_.size() && !inputEnd_) SDL_WaitCondition(changed_, lock_);
        readWaitNs_ += SDL_GetTicksNS() - t0;
    }
    if (stop_) return 0;
    const size_t got = std::min(n, input_.size() - inputPos_);
    std::memcpy(dst, input_.data() + inputPos_, got);
    inputPos_ += got;
    return got;  // 0 only at the end of the file
}

void MovieWorker::run() {
    std::vector<int16_t> pcm;
    auto pushAudio = [&](PssMovie& pss) {
        pcm.clear();
        if (pss.takeAudio(pcm) && audio_) SDL_PutAudioStreamData(audio_, pcm.data(), int(pcm.size() * sizeof(int16_t)));
    };
#if defined(__vita__)
    // Core 1: the main thread (menus, GL) keeps core 0 to itself.
    const int initialMask = sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId());
    const int pinned = sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_1);
    SDL_Log("vita: movie worker started (affinity at creation 0x%x; core 1: %s)", unsigned(initialMask),
            pinned < 0 ? "failed" : "ok");
#endif
    // Statistics, logged every kStatsNs.
    uint64_t statStart = SDL_GetTicksNS(), decodeNs = 0, convertNs = 0;
    uint32_t decoded = 0, converted = 0, maxLag = 0;
    auto logStats = [&](bool last) {
        const uint64_t now = SDL_GetTicksNS();
        if (!last && now - statStart < kStatsNs) return;
        uint64_t waitNs;
        {
            Lock l(lock_);
            waitNs = readWaitNs_, readWaitNs_ = 0;
        }
        uint32_t idct = 0, mc = 0, add = 0;
        takeMpeg2NeonCalls(idct, mc, add);
        const uint32_t skipped = takeSkippedBPictures();
        if (decoded) {
            SDL_Log("vita: movie: %u decoded (%.1f ms each, input wait %.1f; per picture %u IDCT, %u MC, %u block out), "
                    "%u B skipped, %u converted (%.1f ms each) in %.1f s; up to %u frames late",
                    decoded, double(decodeNs) / decoded / 1e6, double(waitNs) / decoded / 1e6, idct / decoded,
                    mc / decoded, add / decoded, skipped, converted,
                    converted ? double(convertNs) / converted / 1e6 : 0.0, double(now - statStart) / 1e9, maxLag);
        }
        statStart = now, decodeNs = convertNs = 0, decoded = converted = maxLag = 0;
    };
    uint64_t lastConverted = 0, lastDecoded = 0;
    bool skipB = false;
    try {
        PssMovie pss([this](uint8_t* dst, size_t n) { return read(dst, n); });
        pss.prefetchAudio(24000);  // ~0.5 s queued before playing (0x18000 bytes on the PS2), as App::startMovie
        pushAudio(pss);
        for (;;) {
            {
                Lock l(lock_);
                while (!stop_ && ready_.size() >= kMaxReady) SDL_WaitCondition(changed_, lock_);
                if (stop_) break;
            }
            {
                Lock l(lock_);
                if (started_) {
                    const uint64_t behind = target_ > lastDecoded ? target_ - lastDecoded : 0;
                    if (behind > kSkipBehind) skipB = true;
                    else if (behind <= kResumeBehind) skipB = false;
                }
            }
            setMpeg2SkipBPictures(skipB);
            const uint64_t t0 = SDL_GetTicksNS();
            const Mpeg2Frame* f = pss.nextFrame();
            pushAudio(pss);
            logStats(false);
            if (!f) break;
            decodeNs += SDL_GetTicksNS() - t0, ++decoded;
            lastDecoded = f->index;
            std::vector<uint8_t> buffer;
            {
                Lock l(lock_);
                const bool late = started_ && f->index < target_;
                if (late) maxLag = std::max(maxLag, uint32_t(target_ - f->index));
                // Late pictures would be dropped by take(): decode only (the
                // next ones need them), no conversion, unless kMaxGap frames
                // passed without one. The first one is always shown.
                if (late && f->index < lastConverted + kMaxGap) continue;
                if (!spare_.empty()) buffer = std::move(spare_.back()), spare_.pop_back();
            }
            const uint64_t t1 = SDL_GetTicksNS();
            buffer.resize(size_t(f->width) * size_t(f->height) * 4);
            mpeg2ToRgba(*f, buffer.data(), size_t(f->width) * 4, !f->progressive);
            convertNs += SDL_GetTicksNS() - t1, ++converted;
            lastConverted = f->index;
            Lock l(lock_);
            ready_.push_back(Picture{f->index, f->width, f->height, std::move(buffer)});
            started_ = true;
            SDL_BroadcastCondition(changed_);
        }
        logStats(true);
    } catch (const std::exception& ex) {
        Lock l(lock_);
        error_ = ex.what();
    }
    Lock l(lock_);
    done_ = true;
    SDL_BroadcastCondition(changed_);
}

}  // namespace vita
