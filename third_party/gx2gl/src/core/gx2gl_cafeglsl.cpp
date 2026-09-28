#include "gx2gl_cafeglsl.h"

#include <stdint.h>
#include <stdio.h>

extern "C" {
#include <coreinit/dynload.h>
#include <coreinit/debug.h>
}

typedef void (*gx2gl_cafeglsl_init_fn)(void);
typedef GX2VertexShader *(*gx2gl_cafeglsl_compile_vs_fn)(const char *, char *, int, gx2gl_cafeglsl_flag_t);
typedef GX2PixelShader *(*gx2gl_cafeglsl_compile_ps_fn)(const char *, char *, int, gx2gl_cafeglsl_flag_t);
typedef void (*gx2gl_cafeglsl_free_vs_fn)(GX2VertexShader *);
typedef void (*gx2gl_cafeglsl_free_ps_fn)(GX2PixelShader *);
typedef void (*gx2gl_cafeglsl_destroy_fn)(void);

typedef struct {
    OSDynLoad_Module module;
    gx2gl_cafeglsl_init_fn init;
    gx2gl_cafeglsl_compile_vs_fn compile_vertex_shader;
    gx2gl_cafeglsl_compile_ps_fn compile_pixel_shader;
    gx2gl_cafeglsl_free_vs_fn free_vertex_shader;
    gx2gl_cafeglsl_free_ps_fn free_pixel_shader;
    gx2gl_cafeglsl_destroy_fn destroy;
    uint32_t vertex_shader_count;
    uint32_t pixel_shader_count;
    bool release_requested;
    bool available;
} GX2GLCafeGLSLState;

static GX2GLCafeGLSLState g_cafeglsl_state = {0};
static char g_cafeglsl_last_error[256] = {0};

static void gx2gl_cafeglsl_unload(void) {
    if (!g_cafeglsl_state.available) {
        return;
    }

    g_cafeglsl_state.destroy();
    OSDynLoad_Release(g_cafeglsl_state.module);
    g_cafeglsl_state = (GX2GLCafeGLSLState){0};
}

static void gx2gl_cafeglsl_finish_deferred_release(void) {
    if (g_cafeglsl_state.release_requested &&
        g_cafeglsl_state.vertex_shader_count == 0 &&
        g_cafeglsl_state.pixel_shader_count == 0) {
        gx2gl_cafeglsl_unload();
    }
}

static bool gx2gl_cafeglsl_load_exports(OSDynLoad_Module module) {
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "InitGLSLCompiler",
                             (void **)&g_cafeglsl_state.init) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "CompileVertexShader",
                             (void **)&g_cafeglsl_state.compile_vertex_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "CompilePixelShader",
                             (void **)&g_cafeglsl_state.compile_pixel_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "FreeVertexShader",
                             (void **)&g_cafeglsl_state.free_vertex_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "FreePixelShader",
                             (void **)&g_cafeglsl_state.free_pixel_shader) != OS_DYNLOAD_OK) {
        return false;
    }
    if (OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "DestroyGLSLCompiler",
                             (void **)&g_cafeglsl_state.destroy) != OS_DYNLOAD_OK) {
        return false;
    }

    return true;
}

bool gx2gl_cafeglsl_init(void) {
    static const char *const candidates[] = {
        "glslcompiler",
        "~/wiiu/libs/glslcompiler.rpl",
    };

    if (g_cafeglsl_state.available) {
        g_cafeglsl_state.release_requested = false;
        return true;
    }
    snprintf(g_cafeglsl_last_error, sizeof(g_cafeglsl_last_error),
             "CafeGLSL compiler unavailable.");

    for (unsigned i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
        if (OSDynLoad_Acquire(candidates[i], &g_cafeglsl_state.module) == OS_DYNLOAD_OK) {
            if (gx2gl_cafeglsl_load_exports(g_cafeglsl_state.module)) {
                g_cafeglsl_state.init();
                g_cafeglsl_state.available = true;
                g_cafeglsl_last_error[0] = '\0';
                OSReport("[gx2gl] CafeGLSL runtime compiler loaded from '%s'\n", candidates[i]);
                return true;
            }

            snprintf(g_cafeglsl_last_error, sizeof(g_cafeglsl_last_error),
                     "Loaded CafeGLSL candidate '%s' but required exports were missing.",
                     candidates[i]);
            OSDynLoad_Release(g_cafeglsl_state.module);
            g_cafeglsl_state.module = NULL;
        } else {
            snprintf(g_cafeglsl_last_error, sizeof(g_cafeglsl_last_error),
                     "OSDynLoad_Acquire failed for CafeGLSL candidate '%s'.",
                     candidates[i]);
        }
    }

    OSReport("[gx2gl] CafeGLSL runtime compiler unavailable: %s\n", g_cafeglsl_last_error);
    return false;
}

void gx2gl_cafeglsl_shutdown(void) {
    if (!g_cafeglsl_state.available) {
        return;
    }

    g_cafeglsl_state.release_requested = true;
    gx2gl_cafeglsl_finish_deferred_release();
}

bool gx2gl_cafeglsl_is_available(void) {
    return g_cafeglsl_state.available;
}

GX2VertexShader *gx2gl_cafeglsl_compile_vertex_shader(const char *shader_source,
                                                      char *info_log_out,
                                                      int info_log_max_length,
                                                      gx2gl_cafeglsl_flag_t flags) {
    if (!gx2gl_cafeglsl_init()) {
        if (info_log_out && info_log_max_length > 0) {
            snprintf(info_log_out, (size_t)info_log_max_length, "%s",
                     g_cafeglsl_last_error[0] ? g_cafeglsl_last_error
                                              : "CafeGLSL compiler unavailable.");
        }
        return NULL;
    }

    GX2VertexShader *shader =
        g_cafeglsl_state.compile_vertex_shader(shader_source, info_log_out,
                                               info_log_max_length, flags);
    if (shader) {
        ++g_cafeglsl_state.vertex_shader_count;
    }
    return shader;
}

GX2PixelShader *gx2gl_cafeglsl_compile_pixel_shader(const char *shader_source,
                                                    char *info_log_out,
                                                    int info_log_max_length,
                                                    gx2gl_cafeglsl_flag_t flags) {
    if (!gx2gl_cafeglsl_init()) {
        if (info_log_out && info_log_max_length > 0) {
            snprintf(info_log_out, (size_t)info_log_max_length, "%s",
                     g_cafeglsl_last_error[0] ? g_cafeglsl_last_error
                                              : "CafeGLSL compiler unavailable.");
        }
        return NULL;
    }

    GX2PixelShader *shader =
        g_cafeglsl_state.compile_pixel_shader(shader_source, info_log_out,
                                              info_log_max_length, flags);
    if (shader) {
        ++g_cafeglsl_state.pixel_shader_count;
    }
    return shader;
}

void gx2gl_cafeglsl_free_vertex_shader(GX2VertexShader *shader) {
    if (g_cafeglsl_state.available && shader) {
        g_cafeglsl_state.free_vertex_shader(shader);
        if (g_cafeglsl_state.vertex_shader_count > 0) {
            --g_cafeglsl_state.vertex_shader_count;
        }
        gx2gl_cafeglsl_finish_deferred_release();
    }
}

void gx2gl_cafeglsl_free_pixel_shader(GX2PixelShader *shader) {
    if (g_cafeglsl_state.available && shader) {
        g_cafeglsl_state.free_pixel_shader(shader);
        if (g_cafeglsl_state.pixel_shader_count > 0) {
            --g_cafeglsl_state.pixel_shader_count;
        }
        gx2gl_cafeglsl_finish_deferred_release();
    }
}
