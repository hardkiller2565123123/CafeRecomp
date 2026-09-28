#include "gx2gl/gx2gl_sdl.h"

#include "core/gl_context.h"
#include "core/gl_framebuffer.h"
#include "gx2gl/proc.h"
#include "gx2gl/present.h"
#include "mem/gl_mem.h"

#include <gx2/event.h>
#include <gx2/swap.h>
#include <proc_ui/procui.h>
#include <whb/gfx.h>
#include <whb/proc.h>

typedef enum {
    GX2GL_RENDER_TARGET_NONE = 0,
    GX2GL_RENDER_TARGET_TV,
    GX2GL_RENDER_TARGET_DRC
} GX2GLRenderTarget;

typedef struct {
    int library_refs;
    int context_refs;
    int proc_owned;
    int proc_ready;
    int gfx_owned;
    int gfx_ready;
    int mem_ready;
    int defaults_ready;
    int frame_active;
    int frame_tv_active;
    int frame_drc_active;
    int swap_interval;
    int automatic_drc_enabled;
    int default_target_drc;
    gl_context_t *context;
} GX2GLSdlState;

static GX2GLSdlState g_gx2gl_sdl;

static int gx2gl_whb_graphics_initialized(void)
{
    return WHBGfxGetTVContextState() != NULL ||
           WHBGfxGetDRCContextState() != NULL;
}

static int gx2gl_tv_target_ready(void)
{
    GX2ColorBuffer *color = WHBGfxGetTVColourBuffer();
    return WHBGfxGetTVContextState() != NULL && color != NULL &&
           color->surface.image != NULL && color->surface.width > 0 &&
           color->surface.height > 0;
}

static int gx2gl_drc_target_ready(void)
{
    GX2ColorBuffer *color = WHBGfxGetDRCColourBuffer();
    return WHBGfxGetDRCContextState() != NULL && color != NULL &&
           color->surface.image != NULL && color->surface.width > 0 &&
           color->surface.height > 0;
}

static void gx2gl_init_defaults(void)
{
    if (g_gx2gl_sdl.defaults_ready) return;

    g_gx2gl_sdl.swap_interval = 1;
    g_gx2gl_sdl.automatic_drc_enabled = 1;
    g_gx2gl_sdl.default_target_drc = 0;
    g_gx2gl_sdl.defaults_ready = 1;
}

static GX2GLRenderTarget gx2gl_default_render_target(void)
{
    gx2gl_init_defaults();

    if (g_gx2gl_sdl.default_target_drc) {
        if (gx2gl_drc_target_ready()) return GX2GL_RENDER_TARGET_DRC;
        if (gx2gl_tv_target_ready()) return GX2GL_RENDER_TARGET_TV;
        return GX2GL_RENDER_TARGET_NONE;
    }

    if (gx2gl_tv_target_ready()) return GX2GL_RENDER_TARGET_TV;
    if (gx2gl_drc_target_ready()) return GX2GL_RENDER_TARGET_DRC;
    return GX2GL_RENDER_TARGET_NONE;
}

static int gx2gl_ensure_proc_ready(void)
{
    if (g_gx2gl_sdl.proc_ready) return 0;

    if (!ProcUIIsRunning()) {
        WHBProcInit();
        g_gx2gl_sdl.proc_owned = 1;
    }
    g_gx2gl_sdl.proc_ready = 1;
    return 0;
}

static int gx2gl_ensure_graphics_ready(void)
{
    gx2gl_init_defaults();

    if (gx2gl_ensure_proc_ready() != 0) return -1;

    if (!g_gx2gl_sdl.gfx_ready) {
        if (!gx2gl_whb_graphics_initialized()) {
            if (!WHBGfxInit()) return -1;
            g_gx2gl_sdl.gfx_owned = 1;
        }
        if (!gx2gl_whb_graphics_initialized()) {
            return -1;
        }
        GX2SetSwapInterval((uint32_t)g_gx2gl_sdl.swap_interval);
        g_gx2gl_sdl.gfx_ready = 1;
    }

    if (!g_gx2gl_sdl.mem_ready) {
        gl_mem_init();
        g_gx2gl_sdl.mem_ready = 1;
    }

    return 0;
}

static void gx2gl_apply_default_framebuffer_target(void)
{
    if (!g_gx2gl_sdl.context) return;
    gl_framebuffer_set_default_target_drc(g_gx2gl_sdl.default_target_drc ? GL_TRUE
                                                                         : GL_FALSE);
}

static void gx2gl_finish_frame(void)
{
    if (!g_gx2gl_sdl.frame_active) return;

    if (g_gx2gl_sdl.frame_drc_active) {
        WHBGfxFinishRenderDRC();
        g_gx2gl_sdl.frame_drc_active = 0;
    }
    if (g_gx2gl_sdl.frame_tv_active) {
        WHBGfxFinishRenderTV();
        g_gx2gl_sdl.frame_tv_active = 0;
    }
    WHBGfxFinishRender();
    g_gx2gl_sdl.frame_active = 0;
}

int GX2GL_BeginFrame(void)
{
    GX2GLRenderTarget target;

    if (g_gx2gl_sdl.frame_active) return 0;
    if (gx2gl_ensure_graphics_ready() != 0) return -1;

    target = gx2gl_default_render_target();
    if (target == GX2GL_RENDER_TARGET_NONE) return -1;

    gx2gl_apply_default_framebuffer_target();
    WHBGfxBeginRender();
    if (target == GX2GL_RENDER_TARGET_TV) {
        WHBGfxBeginRenderTV();
        g_gx2gl_sdl.frame_tv_active = 1;
    } else {
        WHBGfxBeginRenderDRC();
        g_gx2gl_sdl.frame_drc_active = 1;
    }
    g_gx2gl_sdl.frame_active = 1;
    return 0;
}

int GX2GL_EndFrame(void)
{
    if (g_gl_context) _gl_Flush();
    gx2gl_finish_frame();
    return 0;
}

int GX2GL_LoadLibrary(const char *path)
{
    (void)path;

    gx2gl_init_defaults();
    if (gx2gl_ensure_graphics_ready() != 0) return -1;

    g_gx2gl_sdl.library_refs += 1;
    return 0;
}

void GX2GL_UnloadLibrary(void)
{
    if (g_gx2gl_sdl.library_refs > 0) {
        g_gx2gl_sdl.library_refs -= 1;
    }
    if (g_gx2gl_sdl.library_refs > 0 || g_gx2gl_sdl.context_refs > 0) {
        return;
    }

    gx2gl_finish_frame();

    if (g_gx2gl_sdl.mem_ready) {
        gl_mem_shutdown();
        g_gx2gl_sdl.mem_ready = 0;
    }

    if (g_gx2gl_sdl.gfx_owned) {
        WHBGfxShutdown();
        g_gx2gl_sdl.gfx_owned = 0;
    }
    g_gx2gl_sdl.gfx_ready = 0;

    if (g_gx2gl_sdl.proc_owned) {
        WHBProcShutdown();
        g_gx2gl_sdl.proc_owned = 0;
    }
    g_gx2gl_sdl.proc_ready = 0;
}

GX2GL_Context GX2GL_CreateContext(void)
{
    if (GX2GL_LoadLibrary(NULL) != 0) return NULL;

    if (!g_gx2gl_sdl.context) {
        g_gx2gl_sdl.context = gl_context_create();
        if (!g_gx2gl_sdl.context) {
            GX2GL_UnloadLibrary();
            return NULL;
        }
    }

    g_gx2gl_sdl.context_refs += 1;
    g_gl_context = g_gx2gl_sdl.context;
    gx2gl_apply_default_framebuffer_target();
    if (GX2GL_BeginFrame() != 0) {
        GX2GL_DeleteContext((GX2GL_Context)g_gx2gl_sdl.context);
        return NULL;
    }
    return (GX2GL_Context)g_gx2gl_sdl.context;
}

int GX2GL_MakeCurrent(GX2GL_Context context)
{
    if (context && g_gx2gl_sdl.library_refs <= 0) return -1;
    if (context && (gl_context_t *)context != g_gx2gl_sdl.context) return -1;

    if (context) {
        g_gl_context = (gl_context_t *)context;
        gx2gl_apply_default_framebuffer_target();
        return GX2GL_BeginFrame();
    }

    {
        int result = GX2GL_EndFrame();
        g_gl_context = NULL;
        return result;
    }
}

void GX2GL_DeleteContext(GX2GL_Context context)
{
    gl_context_t *owned_context = g_gx2gl_sdl.context;

    if (!owned_context || (gl_context_t *)context != owned_context ||
        g_gx2gl_sdl.context_refs <= 0) return;

    g_gx2gl_sdl.context_refs -= 1;
    if (g_gx2gl_sdl.context_refs == 0) {
        gl_context_destroy(owned_context);
        gx2gl_finish_frame();
        g_gx2gl_sdl.context = NULL;
        g_gl_context = NULL;
    }

    /* Every successful CreateContext acquires one library reference. */
    GX2GL_UnloadLibrary();
}

int GX2GL_Present(void)
{
    int mirror_to_drc;

    gx2gl_init_defaults();
    if (!g_gl_context || g_gx2gl_sdl.library_refs <= 0) return -1;
    if (GX2GL_BeginFrame() != 0) return -1;

    _gl_Flush();
    mirror_to_drc = !g_gx2gl_sdl.default_target_drc &&
                    g_gx2gl_sdl.automatic_drc_enabled &&
                    g_gx2gl_sdl.frame_tv_active;
    if (mirror_to_drc) {
        WHBGfxFinishRenderTV();
        g_gx2gl_sdl.frame_tv_active = 0;
        GX2GL_CopyToDRC();
    }
    gx2gl_finish_frame();
    return GX2GL_BeginFrame();
}

int GX2GL_SetSwapInterval(int interval)
{
    gx2gl_init_defaults();
    if (interval < 0 || interval > 1) return -1;
    g_gx2gl_sdl.swap_interval = interval;
    if (g_gx2gl_sdl.gfx_ready && gx2gl_whb_graphics_initialized()) {
        GX2SetSwapInterval((uint32_t)interval);
    }
    return 0;
}

int GX2GL_GetSwapInterval(void)
{
    gx2gl_init_defaults();
    if (g_gx2gl_sdl.gfx_ready && gx2gl_whb_graphics_initialized()) {
        return (int)GX2GetSwapInterval();
    }
    return g_gx2gl_sdl.swap_interval;
}

int GX2GL_SetAutomaticDRCEnabled(int enabled)
{
    gx2gl_init_defaults();
    g_gx2gl_sdl.automatic_drc_enabled = enabled ? 1 : 0;
    return 0;
}

int GX2GL_GetAutomaticDRCEnabled(void)
{
    gx2gl_init_defaults();
    return g_gx2gl_sdl.automatic_drc_enabled;
}

int GX2GL_SetDefaultFramebufferTargetDRC(int enabled)
{
    int new_target = enabled ? 1 : 0;

    gx2gl_init_defaults();
    if (g_gx2gl_sdl.default_target_drc == new_target) return 0;

    g_gx2gl_sdl.default_target_drc = new_target;
    gx2gl_apply_default_framebuffer_target();
    if (g_gx2gl_sdl.frame_active) {
        gx2gl_finish_frame();
        return GX2GL_BeginFrame();
    }
    return 0;
}

int GX2GL_GetDefaultFramebufferTargetDRC(void)
{
    gx2gl_init_defaults();
    return g_gx2gl_sdl.default_target_drc;
}

int GX2GL_CopyToDRC(void)
{
    gx2gl_init_defaults();
    if (!g_gl_context || g_gx2gl_sdl.library_refs <= 0) return -1;
    if (gx2gl_ensure_graphics_ready() != 0) return -1;
    return GX2GL_MirrorTVToDRC();
}

void* GX2GL_GetSDLProcAddress(const char *proc)
{
    if (!proc) return NULL;
    return GX2GL_GetProcAddress(proc);
}

int GX2GL_SwapWindow(void)
{
    return GX2GL_Present();
}
