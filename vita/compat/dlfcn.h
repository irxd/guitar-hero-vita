// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Vita compatibility: the PS Vita (newlib, static eboot) has no dynamic
// loader. gd-engine-core only uses dladdr() to find the folder of the running
// module, and treats failure as "unknown", so every call here reports failure.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* dli_fname;
    void* dli_fbase;
    const char* dli_sname;
    void* dli_saddr;
} Dl_info;

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0x0
#define RTLD_GLOBAL 0x100

static inline int dladdr(const void* addr, Dl_info* info) {
    (void)addr;
    if (info) {
        info->dli_fname = 0;
        info->dli_fbase = 0;
        info->dli_sname = 0;
        info->dli_saddr = 0;
    }
    return 0;
}
static inline void* dlopen(const char* file, int mode) { (void)file; (void)mode; return 0; }
static inline void* dlsym(void* handle, const char* name) { (void)handle; (void)name; return 0; }
static inline int dlclose(void* handle) { (void)handle; return 0; }
static inline char* dlerror(void) { return (char*)"dynamic loading is not available on PS Vita"; }

#ifdef __cplusplus
}
#endif
