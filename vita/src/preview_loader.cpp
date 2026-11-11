// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "preview_loader.h"

#include <psp2/kernel/threadmgr.h>

#include <exception>
#include <utility>

#include "data/vgs.h"

namespace vita {

PreviewLoader::PreviewLoader() {
    lock_ = SDL_CreateMutex();
    wake_ = SDL_CreateCondition();
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(&PreviewLoader::threadMain));
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "gd01 preview");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, this);
    thread_ = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (!thread_) SDL_Log("vita: no preview thread (%s): previews are built on the main thread", SDL_GetError());
}

PreviewLoader::~PreviewLoader() {
    SDL_LockMutex(lock_);
    quit_ = true;
    SDL_SignalCondition(wake_);
    SDL_UnlockMutex(lock_);
    if (thread_) SDL_WaitThread(thread_, nullptr);
    SDL_DestroyCondition(wake_);
    SDL_DestroyMutex(lock_);
}

uint64_t PreviewLoader::request(Build build, std::string what) {
    SDL_LockMutex(lock_);
    const uint64_t ticket = ++nextTicket_;
    wantTicket_ = ticket;
    want_ = std::move(build);
    wantWhat_ = std::move(what);
    SDL_SignalCondition(wake_);
    SDL_UnlockMutex(lock_);
    if (!thread_) run();  // no worker: build it now
    return ticket;
}

std::unique_ptr<VgsStream> PreviewLoader::take(uint64_t ticket) {
    SDL_LockMutex(lock_);
    std::unique_ptr<VgsStream> out;
    if (readyTicket_ == ticket) out = std::move(ready_);
    else if (readyTicket_ < ticket) ready_.reset();  // an older one
    SDL_UnlockMutex(lock_);
    return out;
}

int PreviewLoader::threadMain(void* self) {
    // Core 2: core 0 is the main thread's, core 1 the movie worker's.
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
    static_cast<PreviewLoader*>(self)->run();
    return 0;
}

// Builds the waiting request; with a thread, loops until quit.
void PreviewLoader::run() {
    SDL_LockMutex(lock_);
    for (;;) {
        while (thread_ && !quit_ && wantTicket_ == 0) SDL_WaitCondition(wake_, lock_);
        if (quit_ || wantTicket_ == 0) break;
        const uint64_t ticket = std::exchange(wantTicket_, 0);
        Build build = std::move(want_);
        const std::string what = std::move(wantWhat_);
        SDL_UnlockMutex(lock_);
        std::unique_ptr<VgsStream> stream;
        const uint64_t t0 = SDL_GetTicksNS();
        try {
            stream = build();
        } catch (const std::exception& ex) {
            SDL_Log("preview %s: %s", what.c_str(), ex.what());
        }
        SDL_Log("vita: preview %s built in %.0f ms", what.c_str(), double(SDL_GetTicksNS() - t0) / 1e6);
        SDL_LockMutex(lock_);
        if (ticket > readyTicket_) readyTicket_ = ticket, ready_ = std::move(stream);
        if (!thread_) break;
    }
    SDL_UnlockMutex(lock_);
}

}  // namespace vita
