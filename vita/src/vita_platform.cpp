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

#include <malloc.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <cstdarg>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "glsl_es100.h"
#include "texture_palette.h"

// newlib heap for the game's own allocations. vitaGL takes its memory from
// separate pools (CDRAM first, then RAM above the threshold set at init).
// 192 MB ran out in the quick play character preview (hardware,
// 2026-11-10); 256 MB with extended memory mode (ATTRIBUTE2=12 in the SFO,
// vita/CMakeLists.txt). [verify] tune with the memory reports in gd01.log.
extern "C" {
int _newlib_heap_size_user = 256 * 1024 * 1024;
}

namespace {

constexpr const char* kDataDir = "ux0:data/gd01/";
constexpr const char* kLogPath = "ux0:data/gd01/gd01.log";
constexpr int kScreenWidth = 960, kScreenHeight = 544;
constexpr int kVglRamThreshold = 16 * 1024 * 1024;

bool gVglReady = false;
int gContextToken = 0;  // its address is the one SDL_GLContext handed out

// --- Memory diagnostics --------------------------------------------------
// A failed allocation is written to memory.log with its size. Nothing on
// that path allocates (fixed buffer, sceClib, sceIo), so it works when the
// heap is exhausted. It is a separate file: main.cpp's log FILE has its own
// write position and would overwrite lines appended behind its back.
constexpr const char* kMemoryLogPath = "ux0:data/gd01/memory.log";
constexpr uint64_t kMemoryReportNs = 10'000'000'000ull;  // gd01.log report every 10 s
uint64_t gNextMemoryReport = 0;
size_t gPeakHeapUsed = 0;
// Texture upload counters (paletted textures, below).
unsigned gPaletteCount = 0, gRgbaCount = 0;
uint64_t gPaletteSavedBytes = 0, gRgbaBytes = 0;

// --- Frame statistics ------------------------------------------------------
// Every 5 s, gd01.log gets the frame rate and, per frame, the CPU time
// between swaps (the game's own work), the time spent in vglSwapBuffers
// (waiting for the GPU / vblank: high when GPU bound), the draw calls and
// the vertex/index data uploaded. Hooks: glDrawArrays, glDrawElements,
// glBufferData, glBufferSubData.
constexpr bool kFrameStats = true;
constexpr uint64_t kFrameStatsNs = 5'000'000'000ull;
struct FrameStats {
    uint64_t frames = 0, cpuNs = 0, swapNs = 0, draws = 0, bufferBytes = 0;
};
FrameStats gFrame;
uint64_t gFrameStatsStart = 0, gLastSwapEnd = 0;

using DrawElementsFn = void (*)(GLenum, GLsizei, GLenum, const void*);
using BufferDataFn = void (*)(GLenum, GLsizeiptr, const void*, GLenum);
using BufferSubDataFn = void (*)(GLenum, GLintptr, GLsizeiptr, const void*);
DrawElementsFn realDrawElements = nullptr;
BufferDataFn realBufferData = nullptr;
BufferSubDataFn realBufferSubData = nullptr;

void hookDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    ++gFrame.draws;
    realDrawElements(mode, count, type, indices);
}

void hookBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (data) gFrame.bufferBytes += uint64_t(size);
    realBufferData(target, size, data, usage);
}

void hookBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    gFrame.bufferBytes += uint64_t(size);
    realBufferSubData(target, offset, size, data);
}

void frameStatsSwap(uint64_t swapStart, uint64_t swapEnd) {
    if (gLastSwapEnd) gFrame.cpuNs += swapStart - gLastSwapEnd;
    gFrame.swapNs += swapEnd - swapStart;
    ++gFrame.frames;
    gLastSwapEnd = swapEnd;
    if (!gFrameStatsStart) {
        gFrameStatsStart = swapEnd;
        return;
    }
    if (swapEnd - gFrameStatsStart < kFrameStatsNs) return;
    const double seconds = double(swapEnd - gFrameStatsStart) / 1e9, f = double(gFrame.frames);
    SDL_Log("vita: perf: %.1f fps; per frame: cpu %.1f ms, swap wait %.1f ms, %.0f draws, %.0f KB buffer uploads",
            f / seconds, double(gFrame.cpuNs) / 1e6 / f, double(gFrame.swapNs) / 1e6 / f, double(gFrame.draws) / f,
            double(gFrame.bufferBytes) / 1024.0 / f);
    gFrame = {};
    gFrameStatsStart = swapEnd;
}

void rawLog(const char* path, const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    int n = sceClibVsnprintf(line, sizeof line - 1, fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n > int(sizeof line) - 2) n = int(sizeof line) - 2;
    line[n++] = '\n';
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0) return;
    sceIoWrite(fd, line, SceSize(n));
    sceIoClose(fd);
}

void logAllocFailure(const char* what, size_t size) {
    const struct mallinfo mi = mallinfo();
    rawLog(kMemoryLogPath, "%s(%u) FAILED: heap in use %u KB, arena %u KB, heap size %u KB", what, unsigned(size),
           unsigned(mi.uordblks >> 10), unsigned(mi.arena >> 10), unsigned(_newlib_heap_size_user >> 10));
}

void reportMemory(const char* when) {
    const struct mallinfo mi = mallinfo();
    if (size_t(mi.uordblks) > gPeakHeapUsed) gPeakHeapUsed = size_t(mi.uordblks);
    SDL_Log("vita: memory [%s]: heap %u / %u MB (peak %u MB); vitaGL free VRAM %u MB, RAM %u MB; "
            "textures: %u paletted (%u MB saved), %u RGBA (%u MB)",
            when, unsigned(mi.uordblks >> 20), unsigned(_newlib_heap_size_user >> 20), unsigned(gPeakHeapUsed >> 20),
            unsigned(vglMemFree(VGL_MEM_VRAM) >> 20), unsigned(vglMemFree(VGL_MEM_RAM) >> 20), gPaletteCount,
            unsigned(gPaletteSavedBytes >> 20), gRgbaCount, unsigned(gRgbaBytes >> 20));
}

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
    bool usesVertexId = false;
};
std::map<GLuint, ShaderInfo> gShaders;
std::map<GLuint, std::vector<GLuint>> gProgramShaders;

// --- Paletted textures -----------------------------------------------------
// gd01 uploads every texture as RGBA8 (the PS2's 4/8-bit palettes expanded),
// and a venue overflowed the video memory: vitaGL then keeps textures in
// main RAM and the frame rate fell to 8-15 (hardware, 2026-11-10). Uploads
// of at most 256 colours become GL_PALETTE8_RGBA8_OES (texture_palette.cpp):
// lossless, a quarter of the size. Those textures keep no mip levels (the
// mipmapped filters fall back to their base filter): a Vita deviation.
constexpr bool kPaletteTextures = true;
constexpr GLsizei kPaletteMinTexels = 32 * 32;  // smaller ones are not worth the CPU time

using BindTextureFn = void (*)(GLenum, GLuint);
using ActiveTextureFn = void (*)(GLenum);
using TexImage2DFn = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using GenerateMipmapFn = void (*)(GLenum);
using TexParameteriFn = void (*)(GLenum, GLenum, GLint);
using DeleteTexturesFn = void (*)(GLsizei, const GLuint*);
BindTextureFn realBindTexture = nullptr;
ActiveTextureFn realActiveTexture = nullptr;
TexImage2DFn realTexImage2D = nullptr;
GenerateMipmapFn realGenerateMipmap = nullptr;
TexParameteriFn realTexParameteri = nullptr;
DeleteTexturesFn realDeleteTextures = nullptr;

GLuint gBoundTexture2D[32] = {};
int gActiveUnit = 0;
std::unordered_set<GLuint> gPaletted;
// Each texture's GL_TEXTURE_MIN_FILTER as gd01 set it (vitaGL has no
// glGetTexParameteriv); absent = the GL default, a mipmapped filter.
std::unordered_map<GLuint, GLint> gMinFilter;
// Textures whose level 0 was uploaded before: re-uploaded ones are dynamic
// (the movie picture, every frame) and stay RGBA8, as scanning and packing
// them each frame cost the main thread time for nothing.
std::unordered_set<GLuint> gUploadedOnce;
bool gPaletteDisabled = false;  // set if vitaGL ever rejects a paletted upload

GLuint boundTexture2D() { return gBoundTexture2D[gActiveUnit & 31]; }

GLint baseFilter(GLint filter) {
    switch (filter) {
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR: return GL_NEAREST;
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR: return GL_LINEAR;
        default: return filter;
    }
}

void hookBindTexture(GLenum target, GLuint texture) {
    if (target == GL_TEXTURE_2D) gBoundTexture2D[gActiveUnit & 31] = texture;
    realBindTexture(target, texture);
}

void hookActiveTexture(GLenum unit) {
    gActiveUnit = int(unit - GL_TEXTURE0);
    realActiveTexture(unit);
}

bool uploadPaletted(GLenum target, GLsizei w, GLsizei h, const void* pixels) {
    static std::vector<uint8_t> blob;  // reused: no allocation churn per upload
    if (!vita::paletteRgba8(static_cast<const uint8_t*>(pixels), w, h, blob)) return false;
    while (glGetError() != GL_NO_ERROR) {}
    glCompressedTexImage2D(target, 0, GL_PALETTE8_RGBA8_OES, w, h, 0, GLsizei(blob.size()), blob.data());
    if (const GLenum err = glGetError(); err != GL_NO_ERROR) {
        SDL_Log("vita: paletted upload %dx%d rejected (GL error 0x%x): paletted textures off", w, h, err);
        gPaletteDisabled = true;
        return false;
    }
    const auto it = gMinFilter.find(boundTexture2D());
    const GLint minFilter = it != gMinFilter.end() ? it->second : GL_NEAREST_MIPMAP_LINEAR;
    if (baseFilter(minFilter) != minFilter) realTexParameteri(target, GL_TEXTURE_MIN_FILTER, baseFilter(minFilter));
    return true;
}

void hookTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format,
                    GLenum type, const void* pixels) {
    const GLuint tex = boundTexture2D();
    if (target == GL_TEXTURE_2D && tex) {
        if (level > 0 && gPaletted.count(tex)) return;  // a paletted texture keeps no mip levels
        if (level == 0) {
            gPaletted.erase(tex);
            const bool dynamic = !gUploadedOnce.insert(tex).second;
            const bool eligible = kPaletteTextures && !gPaletteDisabled && !dynamic && pixels && border == 0 && format == GL_RGBA &&
                                  type == GL_UNSIGNED_BYTE && (internal == GL_RGBA || internal == GL_RGBA8) &&
                                  w * h >= kPaletteMinTexels;
            if (eligible && uploadPaletted(target, w, h, pixels)) {
                gPaletted.insert(tex);
                ++gPaletteCount;
                gPaletteSavedBytes += uint64_t(w) * uint64_t(h) * 3 - vita::kPalette8Bytes;
                return;
            }
            if (pixels) ++gRgbaCount, gRgbaBytes += uint64_t(w) * uint64_t(h) * 4;
        }
    }
    realTexImage2D(target, level, internal, w, h, border, format, type, pixels);
}

void hookGenerateMipmap(GLenum target) {
    if (target == GL_TEXTURE_2D && gPaletted.count(boundTexture2D())) return;
    realGenerateMipmap(target);
}

void hookTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (target == GL_TEXTURE_2D && pname == GL_TEXTURE_MIN_FILTER) {
        const GLuint tex = boundTexture2D();
        gMinFilter[tex] = param;
        if (gPaletted.count(tex)) param = baseFilter(param);
    }
    realTexParameteri(target, pname, param);
}

void hookDeleteTextures(GLsizei n, const GLuint* textures) {
    for (GLsizei i = 0; i < n; ++i) gPaletted.erase(textures[i]), gMinFilter.erase(textures[i]), gUploadedOnce.erase(textures[i]);
    realDeleteTextures(n, textures);
}

// --- gl_VertexID emulation -------------------------------------------------
// Programs whose vertex shader read gl_VertexID (glsl_es100.cpp turned it
// into the vita_VertexID attribute). glDrawArrays feeds that attribute from
// a small buffer of 0, 1, 2, ... while such a program is current.
using UseProgramFn = void (*)(GLuint);
using DrawArraysFn = void (*)(GLenum, GLint, GLsizei);
UseProgramFn realUseProgram = nullptr;
DrawArraysFn realDrawArrays = nullptr;
std::map<GLuint, bool> gProgramUsesVertexId;
GLuint gCurrentProgram = 0;
GLuint gVertexIdBuffer = 0;
constexpr GLsizei kVertexIdCount = 256;
GLuint gVertexIdLocation = 15;

void hookUseProgram(GLuint program) {
    gCurrentProgram = program;
    realUseProgram(program);
}

void hookDrawArrays(GLenum mode, GLint first, GLsizei count) {
    ++gFrame.draws;
    auto it = gProgramUsesVertexId.find(gCurrentProgram);
    if (it == gProgramUsesVertexId.end() || !it->second) {
        realDrawArrays(mode, first, count);
        return;
    }
    if (first < 0 || first + count > kVertexIdCount) {
        SDL_Log("vita: glDrawArrays(%d, %d) needs more vertex ids than %d; not drawn", first, count, kVertexIdCount);
        return;
    }
    if (!gVertexIdBuffer) {
        float ids[kVertexIdCount];
        for (GLsizei i = 0; i < kVertexIdCount; ++i) ids[i] = float(i);
        glGenBuffers(1, &gVertexIdBuffer);
        GLint previous = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        glBindBuffer(GL_ARRAY_BUFFER, gVertexIdBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof ids, ids, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, GLuint(previous));
    }
    GLint previous = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
    glBindBuffer(GL_ARRAY_BUFFER, gVertexIdBuffer);
    glVertexAttribPointer(gVertexIdLocation, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(gVertexIdLocation);
    realDrawArrays(mode, first, count);
    glDisableVertexAttribArray(gVertexIdLocation);
    glBindBuffer(GL_ARRAY_BUFFER, GLuint(previous));
}

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
        info.usesVertexId = es.usesVertexId;
        if (es.usesVertexId) SDL_Log("vita: shader %u: gl_VertexID -> attribute %s", shader, vita::kVertexIdAttribute);
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
    bool usesVertexId = false;
    for (GLuint shader : gProgramShaders[program]) {
        for (const auto& [location, name] : gShaders[shader].attributes) {
            glBindAttribLocation(program, GLuint(location), name.c_str());
            if (kTraceShaders) SDL_Log("vita:   bind attribute %d = %s", location, name.c_str());
        }
        usesVertexId = usesVertexId || gShaders[shader].usesVertexId;
    }
    gProgramUsesVertexId[program] = usesVertexId;
    if (usesVertexId) {
        glBindAttribLocation(program, gVertexIdLocation, vita::kVertexIdAttribute);
        if (kTraceShaders) SDL_Log("vita:   bind attribute %u = %s", gVertexIdLocation, vita::kVertexIdAttribute);
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
    GLint maxAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    if (maxAttribs > 0) gVertexIdLocation = GLuint(maxAttribs - 1);  // gd01 uses 0..4
    SDL_Log("vita: %d vertex attributes; %s at %u", maxAttribs, vita::kVertexIdAttribute, gVertexIdLocation);
    SDL_Log("vita: vitaGL ready: %s / %s; free VRAM %u KB, RAM %u KB",
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            unsigned(vglMemFree(VGL_MEM_VRAM) >> 10), unsigned(vglMemFree(VGL_MEM_RAM) >> 10));
}

__attribute__((constructor)) void vitaEarlyInit() {
    sceIoMkdir("ux0:data/gd01", 0777);
    sceIoRemove(kMemoryLogPath);  // one run per file, like gd01.log
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
    const uint64_t swapStart = SDL_GetTicksNS();
    vglSwapBuffers(GL_FALSE);
    const uint64_t now = SDL_GetTicksNS();
    if (kFrameStats) frameStatsSwap(swapStart, now);
    if (now >= gNextMemoryReport) {
        reportMemory(gNextMemoryReport == 0 ? "first frame" : "periodic");
        gNextMemoryReport = now + kMemoryReportNs;
    }
    return true;
}

void* __real_malloc(size_t size);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* ptr, size_t size);
void* __real_memalign(size_t alignment, size_t size);

void* __wrap_malloc(size_t size) {
    void* p = __real_malloc(size);
    if (!p && size) logAllocFailure("malloc", size);
    return p;
}

void* __wrap_calloc(size_t count, size_t size) {
    void* p = __real_calloc(count, size);
    if (!p && count && size) logAllocFailure("calloc", count * size);
    return p;
}

void* __wrap_realloc(void* ptr, size_t size) {
    void* p = __real_realloc(ptr, size);
    if (!p && size) logAllocFailure("realloc", size);
    return p;
}

void* __wrap_memalign(size_t alignment, size_t size) {
    void* p = __real_memalign(alignment, size);
    if (!p && size) logAllocFailure("memalign", size);
    return p;
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
    struct Hook {
        const char* name;
        void** real;
        SDL_FunctionPointer hook;
    };
    if (kFrameStats) {
        const Hook hooks[] = {
            {"glDrawElements", reinterpret_cast<void**>(&realDrawElements), reinterpret_cast<SDL_FunctionPointer>(hookDrawElements)},
            {"glBufferData", reinterpret_cast<void**>(&realBufferData), reinterpret_cast<SDL_FunctionPointer>(hookBufferData)},
            {"glBufferSubData", reinterpret_cast<void**>(&realBufferSubData), reinterpret_cast<SDL_FunctionPointer>(hookBufferSubData)},
        };
        for (const Hook& h : hooks) {
            if (std::strcmp(name, h.name) == 0) {
                *h.real = reinterpret_cast<void*>(fn);
                return h.hook;
            }
        }
    }
    if (kPaletteTextures) {
        const Hook hooks[] = {
            {"glBindTexture", reinterpret_cast<void**>(&realBindTexture), reinterpret_cast<SDL_FunctionPointer>(hookBindTexture)},
            {"glActiveTexture", reinterpret_cast<void**>(&realActiveTexture), reinterpret_cast<SDL_FunctionPointer>(hookActiveTexture)},
            {"glTexImage2D", reinterpret_cast<void**>(&realTexImage2D), reinterpret_cast<SDL_FunctionPointer>(hookTexImage2D)},
            {"glGenerateMipmap", reinterpret_cast<void**>(&realGenerateMipmap), reinterpret_cast<SDL_FunctionPointer>(hookGenerateMipmap)},
            {"glTexParameteri", reinterpret_cast<void**>(&realTexParameteri), reinterpret_cast<SDL_FunctionPointer>(hookTexParameteri)},
            {"glDeleteTextures", reinterpret_cast<void**>(&realDeleteTextures), reinterpret_cast<SDL_FunctionPointer>(hookDeleteTextures)},
        };
        for (const Hook& h : hooks) {
            if (std::strcmp(name, h.name) == 0) {
                *h.real = reinterpret_cast<void*>(fn);
                return h.hook;
            }
        }
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
        if (std::strcmp(name, "glUseProgram") == 0) {
            realUseProgram = reinterpret_cast<UseProgramFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(hookUseProgram);
        }
        if (std::strcmp(name, "glDrawArrays") == 0) {
            realDrawArrays = reinterpret_cast<DrawArraysFn>(fn);
            return reinterpret_cast<SDL_FunctionPointer>(hookDrawArrays);
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
