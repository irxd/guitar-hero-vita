// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// LD_PRELOAD shim for the desktop gd01 build: tracks the GPU memory gd01
// allocates (textures, mip levels, renderbuffers) by GL object, and every
// 5 s (and at exit) writes the live total, a breakdown by size and the
// largest objects to $GD01_GL_PROBE (a text file). Used to find what fills
// the Vita's video memory without a hardware run (tools/gl-probe.sh).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void (*FnPtr)(void);
typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef int GLsizei;

#define MAX_OBJECTS 65536
typedef struct {
    long long bytes;  // all levels
    int w, h;
    GLenum fmt;
    int levels;
    int renderbuffer;
} Obj;
static Obj textures[MAX_OBJECTS], renderbuffers[MAX_OBJECTS];
static GLuint boundTex[32];
static int activeUnit;
static GLuint boundRb;
static long long uploadsTotal;

static int bytesPerPixel(GLenum internal, GLenum format, GLenum type) {
    switch (internal) {
        case 0x881A: return 8;   // GL_RGBA16F
        case 0x8814: return 16;  // GL_RGBA32F
        case 0x822F: return 4;   // GL_RG16F
        case 0x822E: return 4;   // GL_R32F
        case 0x8229: return 1;   // GL_R8
        case 0x822B: return 2;   // GL_RG8
        case 0x81A5: return 2;   // GL_DEPTH_COMPONENT16
        case 0x81A6: return 4;   // GL_DEPTH_COMPONENT24
        case 0x8CAC: return 4;   // GL_DEPTH_COMPONENT32F
        case 0x88F0: return 4;   // GL_DEPTH24_STENCIL8
        case 0x1907: case 0x8051: return 3;  // RGB, RGB8 (Vita pads to 4 anyway)
    }
    (void)format;
    if (type == 0x1406) return 16;  // GL_FLOAT
    if (type == 0x140B) return 8;   // GL_HALF_FLOAT
    return 4;
}

static void report(const char* when) {
    const char* path = getenv("GD01_GL_PROBE");
    if (!path) return;
    FILE* f = fopen(path, "a");
    if (!f) return;
    long long texTotal = 0, rbTotal = 0;
    int texCount = 0;
    long long buckets[5] = {0};  // <64K, <256K, <1M, <4M, >=4M
    int bucketCount[5] = {0};
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (textures[i].bytes) {
            texTotal += textures[i].bytes, ++texCount;
            long long b = textures[i].bytes;
            int k = b < 65536 ? 0 : b < 262144 ? 1 : b < 1048576 ? 2 : b < 4194304 ? 3 : 4;
            buckets[k] += b, ++bucketCount[k];
        }
        rbTotal += renderbuffers[i].bytes;
    }
    fprintf(f, "=== [%s] live textures %d = %.1f MB, renderbuffers %.1f MB, uploaded so far %.1f MB\n", when, texCount,
            texTotal / 1048576.0, rbTotal / 1048576.0, uploadsTotal / 1048576.0);
    const char* names[5] = {"<64K", "64K-256K", "256K-1M", "1M-4M", ">=4M"};
    for (int k = 0; k < 5; ++k) fprintf(f, "    %-9s %5d textures %7.1f MB\n", names[k], bucketCount[k], buckets[k] / 1048576.0);
    // The 15 largest.
    int shown[15];
    for (int n = 0; n < 15; ++n) {
        int best = -1;
        for (int i = 0; i < MAX_OBJECTS; ++i) {
            int skip = 0;
            for (int m = 0; m < n; ++m) skip |= shown[m] == i;
            if (!skip && textures[i].bytes && (best < 0 || textures[i].bytes > textures[best].bytes)) best = i;
        }
        if (best < 0) break;
        shown[n] = best;
        fprintf(f, "    tex %5d: %4dx%-4d fmt 0x%04x levels %d  %.2f MB\n", best, textures[best].w, textures[best].h,
                textures[best].fmt, textures[best].levels, textures[best].bytes / 1048576.0);
    }
    fclose(f);
}

typedef void (*BindTexFn)(GLenum, GLuint);
typedef void (*ActiveTexFn)(GLenum);
typedef void (*TexImageFn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*DeleteTexFn)(GLsizei, const GLuint*);
typedef void (*GenMipFn)(GLenum);
typedef void (*BindRbFn)(GLenum, GLuint);
typedef void (*RbStorageFn)(GLenum, GLenum, GLsizei, GLsizei);
static BindTexFn realBindTex;
static ActiveTexFn realActiveTex;
static TexImageFn realTexImage;
static DeleteTexFn realDeleteTex;
static GenMipFn realGenMip;
static BindRbFn realBindRb;
static RbStorageFn realRbStorage;

static void hBindTex(GLenum t, GLuint id) {
    if (t == 0x0DE1) boundTex[activeUnit & 31] = id;  // GL_TEXTURE_2D
    realBindTex(t, id);
}
static void hActiveTex(GLenum unit) {
    activeUnit = (int)(unit - 0x84C0);
    realActiveTex(unit);
}
static void hTexImage(GLenum t, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type,
                      const void* px) {
    GLuint id = boundTex[activeUnit & 31];
    if (t == 0x0DE1 && id < MAX_OBJECTS) {
        long long b = (long long)w * h * bytesPerPixel((GLenum)internal, format, type);
        if (level == 0) textures[id].bytes = 0, textures[id].levels = 0, textures[id].w = w, textures[id].h = h,
                        textures[id].fmt = (GLenum)internal;
        textures[id].bytes += b, ++textures[id].levels;
        if (px) uploadsTotal += b;
    }
    realTexImage(t, level, internal, w, h, border, format, type, px);
}
static void hDeleteTex(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i)
        if (ids[i] < MAX_OBJECTS) memset(&textures[ids[i]], 0, sizeof(Obj));
    realDeleteTex(n, ids);
}
static void hGenMip(GLenum t) {
    GLuint id = boundTex[activeUnit & 31];
    if (t == 0x0DE1 && id < MAX_OBJECTS && textures[id].levels == 1)
        textures[id].bytes += textures[id].bytes / 3, textures[id].levels = 99;  // full chain ~ +1/3
    realGenMip(t);
}
static void hBindRb(GLenum t, GLuint id) {
    boundRb = id;
    realBindRb(t, id);
}
static void hRbStorage(GLenum t, GLenum internal, GLsizei w, GLsizei h) {
    if (boundRb < MAX_OBJECTS)
        renderbuffers[boundRb].bytes = (long long)w * h * bytesPerPixel(internal, 0, 0), renderbuffers[boundRb].w = w,
        renderbuffers[boundRb].h = h;
    realRbStorage(t, internal, w, h);
}

FnPtr SDL_GL_GetProcAddress(const char* proc) {
    static FnPtr (*real)(const char*);
    if (!real) real = (FnPtr(*)(const char*))dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress");
    FnPtr fn = real(proc);
    if (!fn) return fn;
#define HOOK(name, var, hook, type)          \
    if (strcmp(proc, name) == 0) {           \
        var = (type)fn;                      \
        return (FnPtr)hook;                  \
    }
    HOOK("glBindTexture", realBindTex, hBindTex, BindTexFn)
    HOOK("glActiveTexture", realActiveTex, hActiveTex, ActiveTexFn)
    HOOK("glTexImage2D", realTexImage, hTexImage, TexImageFn)
    HOOK("glDeleteTextures", realDeleteTex, hDeleteTex, DeleteTexFn)
    HOOK("glGenerateMipmap", realGenMip, hGenMip, GenMipFn)
    HOOK("glBindRenderbuffer", realBindRb, hBindRb, BindRbFn)
    HOOK("glRenderbufferStorage", realRbStorage, hRbStorage, RbStorageFn)
    return fn;
}

// Reports every 5 s from the swap.
int SDL_GL_SwapWindow(void* window) {
    static int (*real)(void*);
    static time_t next;
    if (!real) real = (int (*)(void*))dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    time_t now = time(NULL);
    if (now >= next) {
        if (next) report("periodic");
        next = now + 5;
    }
    return real(window);
}

__attribute__((destructor)) static void atExit(void) { report("exit"); }
