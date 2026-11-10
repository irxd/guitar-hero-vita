// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// PS Vita platform layer for gd01. The upstream sources stay unmodified: the
// linker redirects (-Wl,--wrap=<symbol>, see vita/CMakeLists.txt) the SDL
// calls listed below to the __wrap_ functions in this file.
//
//   SDL_CreateWindow         -> a plain 960x544 SDL window (vitaGL owns the display)
//   SDL_GL_CreateContext     -> vglInitExtended on first use, a dummy context handle
//   SDL_GL_DestroyContext    -> no-op (the vitaGL context lives for the whole run)
//   SDL_GL_MakeCurrent       -> no-op
//   SDL_GL_SwapWindow        -> vglSwapBuffers
//   SDL_GL_SetSwapInterval   -> accepted (vitaGL waits for vblank itself)
//   SDL_GL_GetProcAddress    -> vglGetProcAddress, plus stand-ins for the five
//                               functions vitaGL lacks (logged once each)
//   SDL_GL_ExtensionSupported-> vitaGL's GL_EXTENSIONS string
//   SDL_GetBasePath          -> ux0:data/gd01/ (the ISO is picked up there)
//   SDL_GetPrefPath          -> ux0:data/gd01/ (config, career, enhanced settings)
//
// Before main: the data folder is created, GD01_LOG points the log at
// ux0:data/gd01/gd01.log (main.cpp's openLogFile), and the clocks go to max.
#include <vitaGL.h>

#include <SDL3/SDL.h>

#include <psp2/io/stat.h>
#include <psp2/power.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

// newlib heap for the game's own allocations. vitaGL takes its memory from
// separate pools (CDRAM first, then RAM above the threshold set at init).
// [verify] tune against the 300 MB budget once the venue loads on hardware.
extern "C" {
int _newlib_heap_size_user = 192 * 1024 * 1024;
}

namespace {

constexpr const char* kDataDir = "ux0:data/gd01/";
constexpr const char* kLogPath = "ux0:data/gd01/gd01.log";
constexpr int kScreenWidth = 960, kScreenHeight = 544;
constexpr int kVglRamThreshold = 16 * 1024 * 1024;

bool gVglReady = false;
int gContextToken = 0;  // its address is the one SDL_GLContext handed out

// --- Stand-ins for GL functions vitaGL does not have ---------------------
// Fence sync objects: vitaGL has none. glFinish makes "signalled" true.
constexpr unsigned kGlAlreadySignaled = 0x911A;
void* standinFenceSync(unsigned /*condition*/, unsigned /*flags*/) {
    glFinish();
    return reinterpret_cast<void*>(uintptr_t(1));
}
unsigned standinClientWaitSync(void* /*sync*/, unsigned /*flags*/, uint64_t /*timeout*/) { return kGlAlreadySignaled; }
void standinDeleteSync(void* /*sync*/) {}
// GPU timer queries (performance overlay only): no timings on Vita, 0 ns.
void standinQueryCounter(unsigned /*id*/, unsigned /*target*/) {}
void standinGetQueryObjectui64v(unsigned /*id*/, unsigned /*pname*/, uint64_t* params) {
    if (params) *params = 0;
}

struct Standin {
    const char* name;
    SDL_FunctionPointer fn;
    bool logged;
};
Standin gStandins[] = {
    {"glFenceSync", reinterpret_cast<SDL_FunctionPointer>(standinFenceSync), false},
    {"glClientWaitSync", reinterpret_cast<SDL_FunctionPointer>(standinClientWaitSync), false},
    {"glDeleteSync", reinterpret_cast<SDL_FunctionPointer>(standinDeleteSync), false},
    {"glQueryCounter", reinterpret_cast<SDL_FunctionPointer>(standinQueryCounter), false},
    {"glGetQueryObjectui64v", reinterpret_cast<SDL_FunctionPointer>(standinGetQueryObjectui64v), false},
};

// --- Shader tracing (bring-up aid) ----------------------------------------
// Logs every compile/link with its result and the free memory, and saves
// each shader's final source (after gl.cpp's GLES rewrite) to
// ux0:data/gd01/shaders/<id>.glsl, so a crash or failure points at one
// shader. Turn off once the shaders are known to work on vitaGL.
constexpr bool kTraceShaders = true;
constexpr const char* kShaderDir = "ux0:data/gd01/shaders";

using ShaderSourceFn = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using CompileShaderFn = void (*)(GLuint);
using LinkProgramFn = void (*)(GLuint);
ShaderSourceFn realShaderSource = nullptr;
CompileShaderFn realCompileShader = nullptr;
LinkProgramFn realLinkProgram = nullptr;

void logMemory(const char* when) {
    SDL_Log("vita:   [%s] free VRAM %u KB, RAM %u KB, phycont %u KB, newlib %u KB", when,
            unsigned(vglMemFree(VGL_MEM_VRAM) >> 10), unsigned(vglMemFree(VGL_MEM_RAM) >> 10),
            unsigned(vglMemFree(VGL_MEM_PHYCONT) >> 10), unsigned(vglMemFree(VGL_MEM_EXTERNAL) >> 10));
}

void traceShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    size_t total = 0;
    char path[128];
    SDL_snprintf(path, sizeof path, "%s/%u.glsl", kShaderDir, shader);
    if (SDL_IOStream* out = SDL_IOFromFile(path, "wb")) {
        for (GLsizei i = 0; i < count; ++i) {
            const size_t n = lengths && lengths[i] >= 0 ? size_t(lengths[i]) : std::strlen(strings[i]);
            SDL_WriteIO(out, strings[i], n);
            total += n;
        }
        SDL_CloseIO(out);
    }
    SDL_Log("vita: glShaderSource shader %u: %u bytes -> %s", shader, unsigned(total), path);
    realShaderSource(shader, count, strings, lengths);
}

void traceCompileShader(GLuint shader) {
    SDL_Log("vita: glCompileShader shader %u ...", shader);
    logMemory("before");
    realCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    SDL_Log("vita: glCompileShader shader %u: %s", shader, ok ? "ok" : "FAILED");
    if (!ok) {
        char info[1024] = {};
        glGetShaderInfoLog(shader, sizeof info - 1, nullptr, info);
        SDL_Log("vita:   info log: %s", info);
    }
}

void traceLinkProgram(GLuint program) {
    SDL_Log("vita: glLinkProgram program %u ...", program);
    realLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    SDL_Log("vita: glLinkProgram program %u: %s", program, ok ? "ok" : "FAILED");
    logMemory("after link");
}

void initVitaGl() {
    if (gVglReady) return;
    SDL_Log("vita: vglInitExtended(%dx%d, RAM threshold %d MB)", kScreenWidth, kScreenHeight, kVglRamThreshold >> 20);
    vglInitExtended(0, kScreenWidth, kScreenHeight, kVglRamThreshold, SCE_GXM_MULTISAMPLE_NONE);
    gVglReady = true;
    SDL_Log("vita: vitaGL ready: %s / %s; free VRAM %u KB, RAM %u KB",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            unsigned(vglMemFree(VGL_MEM_VRAM) >> 10), unsigned(vglMemFree(VGL_MEM_RAM) >> 10));
}

__attribute__((constructor)) void vitaEarlyInit() {
    sceIoMkdir("ux0:data/gd01", 0777);
    setenv("GD01_LOG", kLogPath, 0);  // main.cpp opens it before anything logs
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
}

}  // namespace

extern "C" {

SDL_Window* __real_SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags);

SDL_Window* __wrap_SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags) {
    const SDL_WindowFlags dropped = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Log("vita: SDL_CreateWindow(%dx%d, flags 0x%llx) -> %dx%d, flags 0x%llx", w, h,
            static_cast<unsigned long long>(flags), kScreenWidth, kScreenHeight,
            static_cast<unsigned long long>(flags & ~dropped));
    SDL_Window* window = __real_SDL_CreateWindow(title, kScreenWidth, kScreenHeight, flags & ~dropped);
    if (!window) SDL_Log("vita: SDL_CreateWindow failed: %s", SDL_GetError());
    return window;
}

SDL_GLContext __wrap_SDL_GL_CreateContext(SDL_Window* /*window*/) {
    initVitaGl();
    return reinterpret_cast<SDL_GLContext>(&gContextToken);
}

bool __wrap_SDL_GL_DestroyContext(SDL_GLContext /*context*/) { return true; }

bool __wrap_SDL_GL_MakeCurrent(SDL_Window* /*window*/, SDL_GLContext /*context*/) { return true; }

bool __wrap_SDL_GL_SwapWindow(SDL_Window* /*window*/) {
    vglSwapBuffers(GL_FALSE);
    return true;
}

bool __wrap_SDL_GL_SetSwapInterval(int interval) {
    SDL_Log("vita: SDL_GL_SetSwapInterval(%d) accepted (vitaGL syncs to vblank)", interval);
    return true;
}

SDL_FunctionPointer __wrap_SDL_GL_GetProcAddress(const char* name) {
    if (!name) return nullptr;
    for (Standin& s : gStandins) {
        if (std::strcmp(name, s.name) == 0) {
            if (!s.logged) SDL_Log("vita: GL %s is a stand-in (vitaGL lacks it)", name);
            s.logged = true;
            return s.fn;
        }
    }
    auto fn = reinterpret_cast<SDL_FunctionPointer>(vglGetProcAddress(name));
    if (!fn) {
        SDL_Log("vita: GL %s is missing from vitaGL", name);
        return fn;
    }
    if (kTraceShaders) {
        if (std::strcmp(name, "glShaderSource") == 0) {
            realShaderSource = reinterpret_cast<ShaderSourceFn>(fn);
            sceIoMkdir(kShaderDir, 0777);
            return reinterpret_cast<SDL_FunctionPointer>(traceShaderSource);
        }
        if (std::strcmp(name, "glCompileShader") == 0) {
            realCompileShader = reinterpret_cast<CompileShaderFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(traceCompileShader);
        }
        if (std::strcmp(name, "glLinkProgram") == 0) {
            realLinkProgram = reinterpret_cast<LinkProgramFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(traceLinkProgram);
        }
    }
    return fn;
}

bool __wrap_SDL_GL_ExtensionSupported(const char* extension) {
    if (!gVglReady || !extension) return false;
    const char* all = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!all) return false;
    const size_t n = std::strlen(extension);
    for (const char* p = std::strstr(all, extension); p; p = std::strstr(p + 1, extension))
        if ((p == all || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return true;
    return false;
}

const char* __wrap_SDL_GetBasePath(void) { return kDataDir; }

char* __wrap_SDL_GetPrefPath(const char* /*org*/, const char* /*app*/) { return SDL_strdup(kDataDir); }

}  // extern "C"
