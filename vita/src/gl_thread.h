// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// GL on its own core. The game thread records its GL calls into a ring and a
// render thread on core 2 replays them into vitaGL, so vitaGL's per-draw work
// (~11 us a draw, 4-5 ms of a song frame) leaves the game thread.
//
// Every GL function the game gets from SDL_GL_GetProcAddress goes through
// proxy(): calls that only set state or draw are queued (pointer arguments
// copied); calls that return something (glGen*, glGet*, glReadPixels, ...)
// wait for the queue to empty and then run on the calling thread, while the
// render thread is idle. vitaGL is never entered by two threads at once.
//
// GHVITA_GL_THREAD=0 (args.txt) keeps everything on the game thread.
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>

namespace vita::glthread {

// Starts the render thread (after vglInitExtended). No-op when turned off.
void start();
bool active();

// The proxy for a vitaGL function (`real` itself when the thread is off).
SDL_FunctionPointer proxy(const char* name, SDL_FunctionPointer real);

// vglSwapBuffers, queued. Returns once the frame before this one is on screen,
// so the game thread is at most one frame ahead of the render thread.
void swap();

// While one exists, the calling (game) thread may call vitaGL directly: the
// queue is drained first, and the proxies call straight through. For the
// Vita layer's own vitaGL calls (shader and texture hooks, screenshots, ...).
class Direct {
public:
    Direct();
    ~Direct();
    Direct(const Direct&) = delete;
    Direct& operator=(const Direct&) = delete;

private:
    bool on_;
};

// One ghvita.log line for the last `frames` frames (render thread busy time,
// game thread waits, the calls that made it wait), then the counters reset.
void report(double frames);

}  // namespace vita::glthread
