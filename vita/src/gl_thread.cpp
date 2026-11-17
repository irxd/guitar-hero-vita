// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// GL on its own core: see gl_thread.h.
//
// The ring: one producer (the game thread), one consumer (the render
// thread). A command is a header (its replay function and size) followed by
// its arguments and any copied data, 8-byte aligned. A command that does not
// fit before the end of the ring is preceded by a skip marker. The consumer
// moves its read position only after a command has run, so read == write means
// vitaGL is idle.
#include "gl_thread.h"

#include <vitaGL.h>

#include <malloc.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gl_thread_names.h"

namespace vita::glthread {
namespace {

struct Cmd {
    void (*replay)(Cmd*);  // null: a skip marker (the rest of the ring is unused)
    uint32_t size;         // bytes, header included, a multiple of 8
};

constexpr uint32_t kRingBytes = 8u << 20;          // a power of two
constexpr uint32_t kMaxPayload = kRingBytes / 4;  // bigger copies run directly instead
uint8_t* gRing = nullptr;
bool gActive = false;
int gDirectDepth = 0;   // game thread only
uint32_t gWriteLocal = 0;  // game thread's write position (gWrite is what it published)

alignas(64) std::atomic<uint32_t> gWrite{0};
alignas(64) std::atomic<uint32_t> gRead{0};
alignas(64) std::atomic<bool> gRenderSleeping{false};
alignas(64) std::atomic<bool> gGameWaiting{false};
std::atomic<uint32_t> gSwapsDone{0};
uint32_t gSwapsQueued = 0;
SceUID gWake = -1;      // the render thread sleeps on it when the ring is empty
SceUID gProgress = -1;  // the game thread sleeps on it while it waits for the render thread

// Statistics (microseconds): render thread busy (without the swap) and in the
// swap; the game thread's waits (drains for direct calls, frame and ring).
// Running totals, only ever added to by the render thread (report() takes differences).
std::atomic<uint32_t> gBusyUs{0}, gSwapUs{0};
uint32_t gDrainUs = 0, gDrains = 0, gFrameWaitUs = 0, gRingWaitUs = 0, gBigCopies = 0;

inline uint32_t nowUs() { return sceKernelGetProcessTimeLow(); }
inline bool direct() { return !gActive || gDirectDepth > 0; }

// Waits until done() holds. The flag/position pairs are sequentially
// consistent on both sides, so either the render thread sees the flag and
// signals, or this side sees the progress: no lost wake-up (the timeout is a
// guard only).
void publish();

template <class Done>
uint32_t waitUntil(Done done) {
    publish();  // the render thread must see everything queued so far
    if (done()) return 0;
    const uint32_t t0 = nowUs();
    for (int i = 0; i < 64; ++i)
        if (done()) return nowUs() - t0;
    for (;;) {
        gGameWaiting.store(true);
        if (done()) break;
        SceUInt timeout = 5000;
        sceKernelWaitSema(gProgress, 1, &timeout);
    }
    gGameWaiting.store(false);
    return nowUs() - t0;
}

void drain() {
    ++gDrains;
    gDrainUs += waitUntil([] { return gRead.load() == gWriteLocal; });
}

// Commands become visible to the render thread in batches: a wake-up is a
// system call, and one per command cost the game thread more than the GL
// work it moved (hardware, 2026-11-17). Waits publish first (waitUntil).
constexpr uint32_t kBatchCommands = 64;
uint32_t gUnpublished = 0;

void publish() {
    gUnpublished = 0;
    if (gWrite.load(std::memory_order_relaxed) == gWriteLocal) return;
    gWrite.store(gWriteLocal);
    if (gRenderSleeping.load()) sceKernelSignalSema(gWake, 1);
}

// The read position as last seen: the shared one is only loaded when this
// says the ring is full (each load of a line the other core writes is a
// cache miss).
uint32_t gReadSeen = 0;
bool hasRoom(uint32_t bytes) {
    if (gWriteLocal + bytes - gReadSeen <= kRingBytes) return true;
    gReadSeen = gRead.load(std::memory_order_acquire);
    return gWriteLocal + bytes - gReadSeen <= kRingBytes;
}

// Room for `bytes` (rounded up to 8) at the write position; the caller fills
// it and calls commit.
Cmd* reserve(uint32_t bytes) {
    bytes = (bytes + 7u) & ~7u;
    uint32_t pos = gWriteLocal & (kRingBytes - 1);
    if (pos + bytes > kRingBytes) {
        const uint32_t skip = kRingBytes - pos;
        if (!hasRoom(skip)) gRingWaitUs += waitUntil([skip] { return hasRoom(skip); });
        Cmd* marker = reinterpret_cast<Cmd*>(gRing + pos);
        marker->replay = nullptr;
        marker->size = skip;
        gWriteLocal += skip;
        publish();
        pos = 0;
    }
    if (!hasRoom(bytes)) gRingWaitUs += waitUntil([bytes] { return hasRoom(bytes); });
    Cmd* c = reinterpret_cast<Cmd*>(gRing + pos);
    c->size = bytes;
    return c;
}

void commit(Cmd* c) {
    gWriteLocal += c->size;
    if (++gUnpublished >= kBatchCommands) publish();
}

void replaySwap(Cmd*);

int gRenderPriority = 0, gRenderPriorityResult = 0;

int renderThread(void*) {
    // Core 2: core 1 is the movie decoder's, which needs nearly all of it (on
    // core 1 the menu movie dropped a third of its pictures). Core 2 has the
    // movie's colour conversion (~20%), the log writer and the song preview
    // loader. Above the default priority: at equal priority the render thread
    // took turns with the movie threads, and the game thread waited ~16 ms a
    // frame for it (45 FPS menu, hardware 2026-11-17). Its work comes in short
    // bursts; the others get the rest of the core.
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_2);
    gRenderPriority = sceKernelGetThreadCurrentPriority() - 8;
    gRenderPriorityResult = sceKernelChangeThreadPriority(0, gRenderPriority);
    uint32_t r = 0;
    for (;;) {
        uint32_t w = gWrite.load();
        if (w == r) {
            for (int i = 0; i < 256 && w == r; ++i) w = gWrite.load();
            if (w == r) {
                gRenderSleeping.store(true);
                if (gWrite.load() == r) sceKernelWaitSema(gWake, 1, nullptr);
                gRenderSleeping.store(false);
                continue;
            }
        }
        const uint32_t t0 = nowUs();
        const uint32_t swap0 = gSwapUs.load(std::memory_order_relaxed);
        // The read position goes out every 32 commands, after a swap and at
        // the end of the batch, not after every command (the game thread's
        // core would miss on that cache line each time it checks for room).
        uint32_t sinceShared = 0;
        while (r != w) {
            Cmd* c = reinterpret_cast<Cmd*>(gRing + (r & (kRingBytes - 1)));
            const uint32_t size = c->size;
            const bool swap = c->replay == &replaySwap;
            if (c->replay) c->replay(c);
            r += size;
            if (++sinceShared >= 32 || swap || r == w) {
                sinceShared = 0;
                gRead.store(r);
                if (gGameWaiting.load()) sceKernelSignalSema(gProgress, 1);
            }
        }
        const uint32_t swapped = gSwapUs.load(std::memory_order_relaxed) - swap0;
        gBusyUs.fetch_add(nowUs() - t0 - swapped, std::memory_order_relaxed);
    }
    return 0;
}

// --- Direct (synchronous) proxies ---------------------------------------------
// Any vitaGL function: drain, then call on this thread. Counted, so the log
// shows which calls still make the game thread wait every frame.
template <auto Real>
struct Sync;
template <typename R, typename... A, R (*Real)(A...)>
struct Sync<Real> {
    static inline uint32_t calls = 0;
    static R call(A... a) {
        if (gActive && gDirectDepth == 0) ++calls;
        Direct d;
        return Real(a...);
    }
};

// --- Queued proxies -------------------------------------------------------------
// Arguments by value (pointers here are buffer offsets, never client memory).
template <auto Real>
struct Defer;
template <typename... A, void (*Real)(A...)>
struct Defer<Real> {
    struct C : Cmd {
        std::tuple<A...> args;
    };
    static void replay(Cmd* c) { std::apply(Real, static_cast<C*>(c)->args); }
    static void call(A... a) {
        if (direct()) return Real(a...);
        C* c = static_cast<C*>(reserve(sizeof(C)));
        c->replay = &replay;
        new (&c->args) std::tuple<A...>(a...);
        commit(c);
    }
};

// glUniform*v: `count` items of N values, copied.
template <auto Real, int N>
struct DeferUniformV;
template <typename T, void (*Real)(GLint, GLsizei, const T*), int N>
struct DeferUniformV<Real, N> {
    struct C : Cmd {
        GLint loc;
        GLsizei count;
    };
    static void replay(Cmd* c) {
        C* k = static_cast<C*>(c);
        Real(k->loc, k->count, reinterpret_cast<const T*>(k + 1));
    }
    static void call(GLint loc, GLsizei count, const T* v) {
        const size_t bytes = count > 0 ? size_t(count) * N * sizeof(T) : 0;
        if (direct() || !v || bytes == 0 || bytes > kMaxPayload) {
            Direct d;
            return Real(loc, count, v);
        }
        C* c = static_cast<C*>(reserve(uint32_t(sizeof(C) + bytes)));
        c->replay = &replay;
        c->loc = loc, c->count = count;
        std::memcpy(c + 1, v, bytes);
        commit(c);
    }
};

// glUniformMatrix*fv.
template <auto Real, int N>
struct DeferMatrixV;
template <void (*Real)(GLint, GLsizei, GLboolean, const GLfloat*), int N>
struct DeferMatrixV<Real, N> {
    struct C : Cmd {
        GLint loc;
        GLsizei count;
        GLboolean transpose;
    };
    static void replay(Cmd* c) {
        C* k = static_cast<C*>(c);
        Real(k->loc, k->count, k->transpose, reinterpret_cast<const GLfloat*>(k + 1));
    }
    static void call(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* v) {
        const size_t bytes = count > 0 ? size_t(count) * N * sizeof(GLfloat) : 0;
        if (direct() || !v || bytes == 0 || bytes > kMaxPayload) {
            Direct d;
            return Real(loc, count, transpose, v);
        }
        C* c = static_cast<C*>(reserve(uint32_t(sizeof(C) + bytes)));
        c->replay = &replay;
        c->loc = loc, c->count = count, c->transpose = transpose;
        std::memcpy(c + 1, v, bytes);
        commit(c);
    }
};

// Binding shadows: which buffer a draw's pointers refer to. GL_ARRAY_BUFFER is
// global; the element buffer belongs to the vertex array object.
GLuint gArrayBuffer = 0, gVao = 0;
std::unordered_map<GLuint, GLuint> gVaoElements;  // VAO -> element buffer
std::unordered_set<GLuint> gClientArrayVaos;       // VAOs with a client-memory attribute
uint32_t gClientDraws = 0;

void bindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) gArrayBuffer = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) gVaoElements[gVao] = buffer;
    Defer<&::glBindBuffer>::call(target, buffer);
}

void bindVertexArray(GLuint vao) {
    gVao = vao;
    Defer<&::glBindVertexArray>::call(vao);
}

template <void (*Real)(GLsizei, const GLuint*)>
struct DeferDelete {
    struct C : Cmd {
        GLsizei n;
    };
    static void replay(Cmd* c) {
        C* k = static_cast<C*>(c);
        Real(k->n, reinterpret_cast<const GLuint*>(k + 1));
    }
    static void call(GLsizei n, const GLuint* names) {
        const size_t bytes = n > 0 && names ? size_t(n) * sizeof(GLuint) : 0;
        if (direct() || bytes == 0 || bytes > kMaxPayload) {
            Direct d;
            return Real(n, names);
        }
        C* c = static_cast<C*>(reserve(uint32_t(sizeof(C) + bytes)));
        c->replay = &replay;
        c->n = n;
        std::memcpy(c + 1, names, bytes);
        commit(c);
    }
};

void deleteBuffers(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; names && i < n; ++i) {
        if (gArrayBuffer == names[i]) gArrayBuffer = 0;
        for (auto& [vao, ebo] : gVaoElements)
            if (ebo == names[i]) ebo = 0;
    }
    DeferDelete<&::glDeleteBuffers>::call(n, names);
}

void deleteVertexArrays(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; names && i < n; ++i) {
        gVaoElements.erase(names[i]);
        gClientArrayVaos.erase(names[i]);
        if (gVao == names[i]) gVao = 0;
    }
    DeferDelete<&::glDeleteVertexArrays>::call(n, names);
}

void vertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {
    if (!direct() && gArrayBuffer == 0 && pointer) {
        // Client memory: draws from this VAO run directly from now on.
        if (gClientArrayVaos.insert(gVao).second)
            SDL_Log("vita: gl thread: VAO %u has a client-memory attribute; its draws run on the game thread", gVao);
        Direct d;
        return glVertexAttribPointer(index, size, type, normalized, stride, pointer);
    }
    Defer<&::glVertexAttribPointer>::call(index, size, type, normalized, stride, pointer);
}

void drawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!direct() && gClientArrayVaos.count(gVao)) {
        ++gClientDraws;
        Direct d;
        return glDrawArrays(mode, first, count);
    }
    Defer<&::glDrawArrays>::call(mode, first, count);
}

void drawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    if (!direct()) {
        const auto it = gVaoElements.find(gVao);
        if (it == gVaoElements.end() || it->second == 0 || gClientArrayVaos.count(gVao)) {
            ++gClientDraws;  // client indices or attributes: read now
            Direct d;
            return glDrawElements(mode, count, type, indices);
        }
    }
    Defer<&::glDrawElements>::call(mode, count, type, indices);
}

// glBufferData / glBufferSubData: the data copied.
struct BufferDataCmd : Cmd {
    GLenum target, usage;
    GLsizeiptr size;
    bool hasData;
};
void replayBufferData(Cmd* c) {
    BufferDataCmd* k = static_cast<BufferDataCmd*>(c);
    glBufferData(k->target, k->size, k->hasData ? static_cast<const void*>(k + 1) : nullptr, k->usage);
}
void bufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    const size_t bytes = data && size > 0 ? size_t(size) : 0;
    if (direct() || bytes > kMaxPayload) {
        if (bytes > kMaxPayload) ++gBigCopies;
        Direct d;
        return glBufferData(target, size, data, usage);
    }
    BufferDataCmd* c = static_cast<BufferDataCmd*>(reserve(uint32_t(sizeof(BufferDataCmd) + bytes)));
    c->replay = &replayBufferData;
    c->target = target, c->usage = usage, c->size = size, c->hasData = bytes > 0;
    if (bytes) std::memcpy(c + 1, data, bytes);
    commit(c);
}

struct BufferSubDataCmd : Cmd {
    GLenum target;
    GLintptr offset;
    GLsizeiptr size;
};
void replayBufferSubData(Cmd* c) {
    BufferSubDataCmd* k = static_cast<BufferSubDataCmd*>(c);
    glBufferSubData(k->target, k->offset, k->size, k + 1);
}
void bufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    const size_t bytes = size > 0 ? size_t(size) : 0;
    if (direct() || !data || bytes == 0 || bytes > kMaxPayload) {
        if (bytes > kMaxPayload) ++gBigCopies;
        Direct d;
        return glBufferSubData(target, offset, size, data);
    }
    BufferSubDataCmd* c = static_cast<BufferSubDataCmd*>(reserve(uint32_t(sizeof(BufferSubDataCmd) + bytes)));
    c->replay = &replayBufferSubData;
    c->target = target, c->offset = offset, c->size = size;
    std::memcpy(c + 1, data, bytes);
    commit(c);
}

// Texture uploads: the pixels copied. The menu movie re-uploads its picture
// every frame; run directly, each upload waited for the render thread's
// vblank (~16 ms, hardware 2026-11-17). Formats whose size is not known here
// and unpack settings other than the alignment run directly.
GLint gUnpackAlignment = 4;
bool gUnpackExotic = false;

void pixelStorei(GLenum pname, GLint param) {
    if (pname == GL_UNPACK_ALIGNMENT) gUnpackAlignment = param;
    else if (pname != GL_PACK_ALIGNMENT && param != 0) gUnpackExotic = true;
    Defer<&::glPixelStorei>::call(pname, param);
}

size_t pixelBytes(GLsizei w, GLsizei h, GLenum format, GLenum type) {
    if (w <= 0 || h <= 0 || gUnpackExotic) return 0;
    size_t bpp = 0;
    if (type == GL_UNSIGNED_BYTE) {
        switch (format) {
            case GL_RGBA: bpp = 4; break;
            case GL_RGB: bpp = 3; break;
            case GL_LUMINANCE_ALPHA: bpp = 2; break;
            case GL_LUMINANCE:
            case GL_ALPHA: bpp = 1; break;
            default: return 0;
        }
    } else if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1) {
        bpp = 2;
    } else {
        return 0;
    }
    const size_t align = gUnpackAlignment > 0 ? size_t(gUnpackAlignment) : 4;
    const size_t row = (size_t(w) * bpp + align - 1) / align * align;
    return row * size_t(h - 1) + size_t(w) * bpp;  // the last row is not padded
}

struct TexImageCmd : Cmd {
    GLenum target, format, type;
    GLint level, internal, border;
    GLsizei w, h;
    bool hasData;
};
void replayTexImage2D(Cmd* c) {
    TexImageCmd* k = static_cast<TexImageCmd*>(c);
    glTexImage2D(k->target, k->level, k->internal, k->w, k->h, k->border, k->format, k->type,
                 k->hasData ? static_cast<const void*>(k + 1) : nullptr);
}
void texImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type,
                const void* pixels) {
    const size_t bytes = pixels ? pixelBytes(w, h, format, type) : 0;
    if (direct() || (pixels && (bytes == 0 || bytes > kMaxPayload))) {
        if (bytes > kMaxPayload) ++gBigCopies;
        Direct d;
        return glTexImage2D(target, level, internal, w, h, border, format, type, pixels);
    }
    TexImageCmd* c = static_cast<TexImageCmd*>(reserve(uint32_t(sizeof(TexImageCmd) + bytes)));
    c->replay = &replayTexImage2D;
    c->target = target, c->format = format, c->type = type, c->level = level, c->internal = internal, c->border = border;
    c->w = w, c->h = h, c->hasData = bytes > 0;
    if (bytes) std::memcpy(c + 1, pixels, bytes);
    commit(c);
}

struct TexSubImageCmd : Cmd {
    GLenum target, format, type;
    GLint level, x, y;
    GLsizei w, h;
};
void replayTexSubImage2D(Cmd* c) {
    TexSubImageCmd* k = static_cast<TexSubImageCmd*>(c);
    glTexSubImage2D(k->target, k->level, k->x, k->y, k->w, k->h, k->format, k->type, k + 1);
}
void texSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                   const void* pixels) {
    const size_t bytes = pixels ? pixelBytes(w, h, format, type) : 0;
    if (direct() || bytes == 0 || bytes > kMaxPayload) {
        if (bytes > kMaxPayload) ++gBigCopies;
        Direct d;
        return glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
    }
    TexSubImageCmd* c = static_cast<TexSubImageCmd*>(reserve(uint32_t(sizeof(TexSubImageCmd) + bytes)));
    c->replay = &replayTexSubImage2D;
    c->target = target, c->format = format, c->type = type, c->level = level, c->x = x, c->y = y, c->w = w, c->h = h;
    std::memcpy(c + 1, pixels, bytes);
    commit(c);
}

void replaySwap(Cmd*) {
    const uint32_t t0 = nowUs();
    vglSwapBuffers(GL_FALSE);
    gSwapUs.fetch_add(nowUs() - t0, std::memory_order_relaxed);
    gSwapsDone.fetch_add(1);
}

struct Entry {
    const char* name;
    SDL_FunctionPointer fn;
};
#define GHVITA_FN(f) reinterpret_cast<SDL_FunctionPointer>(f)
#define GHVITA_DEFER(name) {#name, GHVITA_FN(&Defer<&::name>::call)}
const Entry kQueued[] = {
    // State and draws, by value.
    GHVITA_DEFER(glActiveTexture), GHVITA_DEFER(glBindTexture), GHVITA_DEFER(glUseProgram),
    GHVITA_DEFER(glEnable), GHVITA_DEFER(glDisable), GHVITA_DEFER(glBlendFunc), GHVITA_DEFER(glBlendFuncSeparate),
    GHVITA_DEFER(glBlendEquation), GHVITA_DEFER(glBlendEquationSeparate), GHVITA_DEFER(glDepthFunc),
    GHVITA_DEFER(glDepthMask), GHVITA_DEFER(glColorMask), GHVITA_DEFER(glCullFace), GHVITA_DEFER(glFrontFace),
    GHVITA_DEFER(glViewport), GHVITA_DEFER(glScissor), GHVITA_DEFER(glClear), GHVITA_DEFER(glClearColor),
    GHVITA_DEFER(glClearDepthf), GHVITA_DEFER(glClearStencil), GHVITA_DEFER(glStencilFunc), GHVITA_DEFER(glStencilOp),
    GHVITA_DEFER(glStencilMask), GHVITA_DEFER(glPolygonOffset), GHVITA_DEFER(glLineWidth),
    GHVITA_DEFER(glTexParameteri), GHVITA_DEFER(glTexParameterf), GHVITA_DEFER(glGenerateMipmap),
    GHVITA_DEFER(glBindFramebuffer), GHVITA_DEFER(glBindRenderbuffer), GHVITA_DEFER(glBlitFramebuffer),
    GHVITA_DEFER(glCopyTexSubImage2D), GHVITA_DEFER(glEnableVertexAttribArray),
    GHVITA_DEFER(glDisableVertexAttribArray), GHVITA_DEFER(glDepthRangef),
    GHVITA_DEFER(glBeginQuery), GHVITA_DEFER(glEndQuery), GHVITA_DEFER(glDeleteProgram), GHVITA_DEFER(glDeleteShader),
    GHVITA_DEFER(glUniform1f), GHVITA_DEFER(glUniform2f), GHVITA_DEFER(glUniform3f), GHVITA_DEFER(glUniform4f),
    GHVITA_DEFER(glUniform1i), GHVITA_DEFER(glUniform2i), GHVITA_DEFER(glUniform3i), GHVITA_DEFER(glUniform4i),
    // Copied data.
    {"glUniform1fv", GHVITA_FN((&DeferUniformV<&::glUniform1fv, 1>::call))},
    {"glUniform2fv", GHVITA_FN((&DeferUniformV<&::glUniform2fv, 2>::call))},
    {"glUniform3fv", GHVITA_FN((&DeferUniformV<&::glUniform3fv, 3>::call))},
    {"glUniform4fv", GHVITA_FN((&DeferUniformV<&::glUniform4fv, 4>::call))},
    {"glUniform1iv", GHVITA_FN((&DeferUniformV<&::glUniform1iv, 1>::call))},
    {"glUniform2iv", GHVITA_FN((&DeferUniformV<&::glUniform2iv, 2>::call))},
    {"glUniform3iv", GHVITA_FN((&DeferUniformV<&::glUniform3iv, 3>::call))},
    {"glUniform4iv", GHVITA_FN((&DeferUniformV<&::glUniform4iv, 4>::call))},
    {"glUniformMatrix2fv", GHVITA_FN((&DeferMatrixV<&::glUniformMatrix2fv, 4>::call))},
    {"glUniformMatrix3fv", GHVITA_FN((&DeferMatrixV<&::glUniformMatrix3fv, 9>::call))},
    {"glUniformMatrix4fv", GHVITA_FN((&DeferMatrixV<&::glUniformMatrix4fv, 16>::call))},
    {"glDeleteTextures", GHVITA_FN(&DeferDelete<&::glDeleteTextures>::call)},
    {"glDeleteFramebuffers", GHVITA_FN(&DeferDelete<&::glDeleteFramebuffers>::call)},
    {"glDeleteRenderbuffers", GHVITA_FN(&DeferDelete<&::glDeleteRenderbuffers>::call)},
    {"glDeleteQueries", GHVITA_FN(&DeferDelete<&::glDeleteQueries>::call)},
    {"glBufferData", GHVITA_FN(&bufferData)},
    {"glBufferSubData", GHVITA_FN(&bufferSubData)},
    {"glTexImage2D", GHVITA_FN(&texImage2D)},
    {"glTexSubImage2D", GHVITA_FN(&texSubImage2D)},
    {"glPixelStorei", GHVITA_FN(&pixelStorei)},
    // With binding shadows.
    {"glBindBuffer", GHVITA_FN(&bindBuffer)},
    {"glBindVertexArray", GHVITA_FN(&bindVertexArray)},
    {"glDeleteBuffers", GHVITA_FN(&deleteBuffers)},
    {"glDeleteVertexArrays", GHVITA_FN(&deleteVertexArrays)},
    {"glVertexAttribPointer", GHVITA_FN(&vertexAttribPointer)},
    {"glDrawArrays", GHVITA_FN(&drawArrays)},
    {"glDrawElements", GHVITA_FN(&drawElements)},
};
#undef GHVITA_DEFER

struct SyncEntry {
    const char* name;
    SDL_FunctionPointer fn;
    uint32_t* calls;
};
#define GHVITA_SYNC(name) {#name, GHVITA_FN(&Sync<&::name>::call), &Sync<&::name>::calls},
const SyncEntry kDirect[] = {GHVITA_VGL_FUNCTIONS(GHVITA_SYNC)};
#undef GHVITA_SYNC
#undef GHVITA_FN

bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("GHVITA_GL_THREAD");
        return !(e && e[0] == '0');
    }();
    return on;
}

}  // namespace

bool active() { return gActive; }

Direct::Direct() : on_(gActive) {
    if (on_ && gDirectDepth++ == 0) drain();
}

Direct::~Direct() {
    if (on_) --gDirectDepth;
}

void start() {
    if (gActive) return;
    if (!enabled()) {
        SDL_Log("vita: gl thread OFF (GHVITA_GL_THREAD=0): GL runs on the game thread");
        return;
    }
    gRing = static_cast<uint8_t*>(memalign(64, kRingBytes));
    gWake = sceKernelCreateSema("ghvita gl wake", 0, 0, 1, nullptr);
    gProgress = sceKernelCreateSema("ghvita gl progress", 0, 0, 1, nullptr);
    SDL_Thread* thread = nullptr;
    if (gRing && gWake >= 0 && gProgress >= 0) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(renderThread));
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "ghvita gl");
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 512 * 1024);
        thread = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!thread) {
        SDL_Log("vita: gl thread could not start (ring %p, semaphores %d %d: %s); GL stays on the game thread",
                static_cast<void*>(gRing), gWake, gProgress, SDL_GetError());
        return;
    }
    SDL_DetachThread(thread);
    // The game thread on core 0, the render thread on core 2: unpinned, the
    // game thread shared a core with it and slowed down 4-5x in some frames
    // (hardware, 2026-11-17).
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
    gActive = true;
    SDL_Log("vita: gl thread ON: GL calls replayed on core 2 (ring %u MB); GHVITA_GL_THREAD=0 turns it off", kRingBytes >> 20);
}

SDL_FunctionPointer proxy(const char* name, SDL_FunctionPointer real) {
    if (!enabled() || !name || !real) return real;
    for (const Entry& e : kQueued)
        if (std::strcmp(name, e.name) == 0) return e.fn;
    for (const SyncEntry& e : kDirect)
        if (std::strcmp(name, e.name) == 0) return e.fn;
    SDL_Log("vita: gl thread: no proxy for %s; it runs on the game thread unsynchronised", name);
    return real;
}

void swap() {
    if (!gActive) {
        vglSwapBuffers(GL_FALSE);
        return;
    }
    Cmd* c = reserve(sizeof(Cmd));
    c->replay = &replaySwap;
    commit(c);
    ++gSwapsQueued;
    gFrameWaitUs += waitUntil([] { return gSwapsDone.load() + 1 >= gSwapsQueued; });
}

void report(double frames) {
    if (!gActive || frames <= 0) return;
    // The calls that drained the queue, most first.
    std::vector<std::pair<uint32_t, const char*>> top;
    for (const SyncEntry& e : kDirect)
        if (*e.calls) top.push_back({*e.calls, e.name}), *e.calls = 0;
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string calls;
    char part[64];
    for (size_t i = 0; i < top.size() && i < 6; ++i) {
        std::snprintf(part, sizeof part, " %s %.1f;", top[i].second, double(top[i].first) / frames);
        calls += part;
    }
    // Running totals (the render thread adds to them): the change since the last report.
    static uint32_t lastBusy = 0, lastSwap = 0;
    const uint32_t busyNow = gBusyUs.load(), swapNow = gSwapUs.load();
    const uint32_t busy = busyNow - lastBusy, swapUs = swapNow - lastSwap;
    lastBusy = busyNow, lastSwap = swapNow;
    static bool priorityLogged = false;
    if (!priorityLogged) {
        priorityLogged = true;
        SDL_Log("vita: gl thread priority %d (game thread %d): 0x%08x", gRenderPriority, sceKernelGetThreadCurrentPriority(),
                unsigned(gRenderPriorityResult));
    }
    SDL_Log("vita: gl thread: per frame: replay %.1f ms, in swap %.1f ms; game thread waited %.1f ms for the frame, "
            "%.1f ms in %.1f drains, %.1f ms for room; %.1f direct draws, %u big copies; draining calls:%s",
            busy / 1e3 / frames, swapUs / 1e3 / frames, gFrameWaitUs / 1e3 / frames, gDrainUs / 1e3 / frames,
            gDrains / frames, gRingWaitUs / 1e3 / frames, gClientDraws / frames, gBigCopies, calls.empty() ? " none" : calls.c_str());
    gFrameWaitUs = gDrainUs = gDrains = gRingWaitUs = gClientDraws = gBigCopies = 0;
}

}  // namespace vita::glthread
