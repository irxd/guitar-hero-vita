// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "movie_worker.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>

#include "video/pss.h"

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
    while (!stop_ && inputPos_ == input_.size() && !inputEnd_) SDL_WaitCondition(changed_, lock_);
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
            const Mpeg2Frame* f = pss.nextFrame();
            pushAudio(pss);
            if (!f) break;
            std::vector<uint8_t> buffer;
            {
                Lock l(lock_);
                // Late pictures would be dropped by take(): decode only (the
                // next ones need them), no conversion. The first one is shown.
                if (started_ && f->index < target_) continue;
                if (!spare_.empty()) buffer = std::move(spare_.back()), spare_.pop_back();
            }
            buffer.resize(size_t(f->width) * size_t(f->height) * 4);
            mpeg2ToRgba(*f, buffer.data(), size_t(f->width) * 4, !f->progressive);
            Lock l(lock_);
            ready_.push_back(Picture{f->index, f->width, f->height, std::move(buffer)});
            started_ = true;
            SDL_BroadcastCondition(changed_);
        }
    } catch (const std::exception& ex) {
        Lock l(lock_);
        error_ = ex.what();
    }
    Lock l(lock_);
    done_ = true;
    SDL_BroadcastCondition(changed_);
}

}  // namespace vita
