// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// LD_PRELOAD shim for the desktop gd01 build: saves every shader source the
// game hands to glShaderSource into $GD01_SHADER_DUMP/<n>.orig.glsl. Run with
// GD01_GLES=1 and --shadertest to get the exact GLSL ES 3.00 sources the Vita
// build sees, then check them all on the desktop (tools/dump-shaders.sh).
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*FnPtr)(void);
typedef void (*ShaderSourceFn)(unsigned, int, const char* const*, const int*);

static ShaderSourceFn realShaderSource;
static int counter;

static void dumpShaderSource(unsigned shader, int count, const char* const* strings, const int* lengths) {
    const char* dir = getenv("GD01_SHADER_DUMP");
    if (dir) {
        char path[512];
        snprintf(path, sizeof path, "%s/%d.orig.glsl", dir, ++counter);
        FILE* f = fopen(path, "wb");
        if (f) {
            for (int i = 0; i < count; ++i)
                fwrite(strings[i], 1, lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]), f);
            fclose(f);
        }
    }
    realShaderSource(shader, count, strings, lengths);
}

FnPtr SDL_GL_GetProcAddress(const char* proc) {
    static FnPtr (*real)(const char*);
    if (!real) real = (FnPtr(*)(const char*))dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress");
    FnPtr fn = real(proc);
    if (fn && strcmp(proc, "glShaderSource") == 0) {
        realShaderSource = (ShaderSourceFn)fn;
        return (FnPtr)dumpShaderSource;
    }
    return fn;
}
