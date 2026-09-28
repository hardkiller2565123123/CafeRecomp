#include "gl_mem.h"
#include "Platform/WiiU_Log.hpp"

#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef GX2GL_ENABLE_VERBOSE_FILE_LOGS
#define GX2GL_ENABLE_VERBOSE_FILE_LOGS 0
#endif

typedef struct {
    uint32_t init_count;
    uint32_t live_allocations;
} GLMemState;

static GLMemState g_mem_state;

static void log_mem_step(const char *format, ...) {
#if GX2GL_ENABLE_VERBOSE_FILE_LOGS
    char buffer[512];
    va_list args;
    FILE *log_file;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    WiiU_Log("%s", buffer);

    log_file = fopen(WiiU_GetGX2GLInitLogPath(), "a");
    if (log_file) {
        fprintf(log_file, "[gx2gl-mem] %s\n", buffer);
        fclose(log_file);
    }
#else
    (void)format;
#endif
}

static uint32_t normalize_alignment(uint32_t align) {
    uint32_t normalized = align < sizeof(void *) ? (uint32_t)sizeof(void *) : align;

    if ((normalized & (normalized - 1u)) == 0) {
        return normalized;
    }

    normalized--;
    normalized |= normalized >> 1;
    normalized |= normalized >> 2;
    normalized |= normalized >> 4;
    normalized |= normalized >> 8;
    normalized |= normalized >> 16;
    normalized++;
    return normalized;
}

static const char *mem_type_name(gl_mem_type_t type) {
    switch (type) {
    case GL_MEM_TYPE_MEM1: return "MEM1";
    case GL_MEM_TYPE_MEM2: return "MEM2";
    default: return "unknown";
    }
}

void gl_mem_init(void) {
    g_mem_state.init_count++;
    log_mem_step("gl_mem_init: count=%u", (unsigned int)g_mem_state.init_count);
}

void gl_mem_shutdown(void) {
    if (g_mem_state.init_count == 0) {
        return;
    }

    g_mem_state.init_count--;
    log_mem_step("gl_mem_shutdown: count=%u live=%u",
                 (unsigned int)g_mem_state.init_count,
                 (unsigned int)g_mem_state.live_allocations);
}

void* gl_mem_alloc(gl_mem_type_t type, size_t size, uint32_t align) {
    uint32_t normalized_align;
    void *ptr;

    if (size == 0) {
        return NULL;
    }

    normalized_align = normalize_alignment(align);
    if (normalized_align == 0) {
        log_mem_step("gl_mem_alloc: alignment overflow type=%s size=%u align=%u",
                     mem_type_name(type), (unsigned int)size, (unsigned int)align);
        return NULL;
    }

    ptr = memalign((size_t)normalized_align, size);
    if (!ptr) {
        log_mem_step("gl_mem_alloc: failed type=%s size=%u align=%u normalized=%u",
                     mem_type_name(type), (unsigned int)size,
                     (unsigned int)align, (unsigned int)normalized_align);
        return NULL;
    }

    g_mem_state.live_allocations++;
    return ptr;
}

void gl_mem_free(gl_mem_type_t type, void* ptr) {
    (void)type;

    if (!ptr) {
        return;
    }

    if (g_mem_state.live_allocations > 0) {
        g_mem_state.live_allocations--;
    }
    free(ptr);
}
