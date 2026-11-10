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
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "glsl_es100.h"

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

// --- Shader hooks ----------------------------------------------------------
// 1. GLSL ES 3.00 -> 1.00 rewrite (glsl_es100.cpp): vitaGL's translator
//    turns gd01's 3.00 sources into null GXM programs and glLinkProgram
//    crashes (hardware, 2026-11-10). The layout(location) inputs become
//    attributes bound with glBindAttribLocation just before the link.
// 2. Tracing (bring-up aid): every compile/link is logged with its status,
//    info log and free memory; sources go to ux0:data/gd01/shaders/
//    (<id>.orig.glsl as gd01 sent it, <id>.glsl as vitaGL got it).
constexpr bool kRewriteToEs100 = true;
constexpr bool kTraceShaders = true;
constexpr const char* kShaderDir = "ux0:data/gd01/shaders";

using CreateShaderFn = GLuint (*)(GLenum);
using AttachShaderFn = void (*)(GLuint, GLuint);
using ShaderSourceFn = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using CompileShaderFn = void (*)(GLuint);
using LinkProgramFn = void (*)(GLuint);
CreateShaderFn realCreateShader = nullptr;
AttachShaderFn realAttachShader = nullptr;
ShaderSourceFn realShaderSource = nullptr;
CompileShaderFn realCompileShader = nullptr;
LinkProgramFn realLinkProgram = nullptr;

struct ShaderInfo {
    vita::ShaderStage stage = vita::ShaderStage::Unknown;
    std::vector<std::pair<int, std::string>> attributes;
};
std::map<GLuint, ShaderInfo> gShaders;
std::map<GLuint, std::vector<GLuint>> gProgramShaders;

void saveText(const char* path, const std::string& text) {
    if (SDL_IOStream* out = SDL_IOFromFile(path, "wb")) {
        SDL_WriteIO(out, text.data(), text.size());
        SDL_CloseIO(out);
    }
}

GLuint hookCreateShader(GLenum type) {
    const GLuint shader = realCreateShader(type);
    gShaders[shader] = ShaderInfo{};
    gShaders[shader].stage = type == GL_VERTEX_SHADER     ? vita::ShaderStage::Vertex
                             : type == GL_FRAGMENT_SHADER ? vita::ShaderStage::Fragment
                                                          : vita::ShaderStage::Unknown;
    return shader;
}

void hookAttachShader(GLuint program, GLuint shader) {
    gProgramShaders[program].push_back(shader);
    realAttachShader(program, shader);
}

void logMemory(const char* when) {
    SDL_Log("vita:   [%s] free VRAM %u KB, RAM %u KB, phycont %u KB, newlib %u KB", when,
            unsigned(vglMemFree(VGL_MEM_VRAM) >> 10), unsigned(vglMemFree(VGL_MEM_RAM) >> 10),
            unsigned(vglMemFree(VGL_MEM_PHYCONT) >> 10), unsigned(vglMemFree(VGL_MEM_EXTERNAL) >> 10));
}

void hookShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i)
        src.append(strings[i], lengths && lengths[i] >= 0 ? size_t(lengths[i]) : std::strlen(strings[i]));
    ShaderInfo& info = gShaders[shader];
    char path[128];
    if (kTraceShaders) {
        SDL_snprintf(path, sizeof path, "%s/%u.orig.glsl", kShaderDir, shader);
        saveText(path, src);
    }
    if (kRewriteToEs100) {
        vita::Es100Result es = vita::toGlslEs100(src, info.stage);
        info.attributes = std::move(es.attributes);
        for (const std::string& note : es.notes) SDL_Log("vita: shader %u: %s", shader, note.c_str());
        src = std::move(es.source);
    }
    if (kTraceShaders) {
        SDL_snprintf(path, sizeof path, "%s/%u.glsl", kShaderDir, shader);
        saveText(path, src);
        SDL_Log("vita: glShaderSource shader %u (%s): %u bytes, %u attributes -> %s", shader,
                info.stage == vita::ShaderStage::Vertex ? "vertex" : info.stage == vita::ShaderStage::Fragment ? "fragment" : "?",
                unsigned(src.size()), unsigned(info.attributes.size()), path);
    }
    const GLchar* p = src.c_str();
    realShaderSource(shader, 1, &p, nullptr);
}

void hookCompileShader(GLuint shader) {
    if (kTraceShaders) {
        SDL_Log("vita: glCompileShader shader %u ...", shader);
        logMemory("before");
    }
    realCompileShader(shader);
    if (!kTraceShaders) return;
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    char log[1024] = {};
    glGetShaderInfoLog(shader, sizeof log - 1, nullptr, log);
    // vitaGL reported "ok" for sources that came out as null programs, so the
    // info log is printed whatever the status says.
    SDL_Log("vita: glCompileShader shader %u: %s%s%s", shader, ok ? "ok" : "FAILED", log[0] ? "; info log: " : "", log);
}

void hookLinkProgram(GLuint program) {
    for (GLuint shader : gProgramShaders[program]) {
        for (const auto& [location, name] : gShaders[shader].attributes) {
            glBindAttribLocation(program, GLuint(location), name.c_str());
            if (kTraceShaders) SDL_Log("vita:   bind attribute %d = %s", location, name.c_str());
        }
    }
    if (kTraceShaders) SDL_Log("vita: glLinkProgram program %u ...", program);
    realLinkProgram(program);
    if (!kTraceShaders) return;
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
    if (kRewriteToEs100 || kTraceShaders) {
        if (std::strcmp(name, "glCreateShader") == 0) {
            realCreateShader = reinterpret_cast<CreateShaderFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(hookCreateShader);
        }
        if (std::strcmp(name, "glAttachShader") == 0) {
            realAttachShader = reinterpret_cast<AttachShaderFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(hookAttachShader);
        }
        if (std::strcmp(name, "glShaderSource") == 0) {
            realShaderSource = reinterpret_cast<ShaderSourceFn>(fn);
            if (kTraceShaders) sceIoMkdir(kShaderDir, 0777);
            return reinterpret_cast<SDL_FunctionPointer>(hookShaderSource);
        }
        if (std::strcmp(name, "glCompileShader") == 0) {
            realCompileShader = reinterpret_cast<CompileShaderFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(hookCompileShader);
        }
        if (std::strcmp(name, "glLinkProgram") == 0) {
            realLinkProgram = reinterpret_cast<LinkProgramFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(hookLinkProgram);
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
