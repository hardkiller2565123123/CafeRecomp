#include "gl_framebuffer.h"
#include "gl_buffer.h"
#include "gl_texture.h"
#include "endian/endian.h"
#include "mem/gl_mem.h"
#include "state/gl_state.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <coreinit/cache.h>
#include <gx2/surface.h>
#include <gx2/state.h>
#include <gx2/context.h>
#include <gx2/event.h>
#include <gx2/display.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2r/surface.h>
#include <whb/gfx.h>

void GX2ExpandAAColorBuffer(GX2ColorBuffer *colorBuffer);
void GX2ExpandDepthBuffer(GX2DepthBuffer *depthBuffer);
#ifdef __cplusplus
}
#endif
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif

#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#endif

#define MAX_FRAMEBUFFERS 128
#define MAX_RENDERBUFFERS 256
#define GX2GL_MAX_RENDERBUFFER_SIZE 8192
#define GX2GL_MAX_RENDERBUFFER_SAMPLES GL33_MAX_SAMPLES
#define GX2GL_MAX_FRAMEBUFFER_3D_TEXTURE_SIZE 2048
#define GX2GL_AUX_BUFFER_CLEAR_VALUE 0xCC

typedef enum {
    GL_ATTACHMENT_KIND_NONE = 0,
    GL_ATTACHMENT_KIND_TEXTURE,
    GL_ATTACHMENT_KIND_RENDERBUFFER
} GLAttachmentKind;

typedef struct {
    GLAttachmentKind kind;
    GLuint object;
    GLenum textarget;
    GLint level;
    GLint layer;
    GLboolean layered;
} GLAttachmentRef;

typedef struct {
    bool allocated;
    GX2Surface surface;
    GX2ColorBuffer color_buffer;
} GLTextureColorTarget;

typedef struct {
    bool reserved;
    bool in_use;
    bool is_depth;
    GLint internal_format;
    GLsizei width;
    GLsizei height;
    GLsizei samples;
    GLboolean fixed_sample_locations;
    GX2Surface surface;
    GX2ColorBuffer color_buffer;
    GX2DepthBuffer depth_buffer;
} GLRenderbuffer;

typedef struct {
    bool reserved;
    bool in_use;
    GLAttachmentRef color_attachments[8];
    GLenum draw_buffers[8];
    GLenum read_buffer;
    GLAttachmentRef depth_attachment;
    GLAttachmentRef stencil_attachment;

    GX2ColorBuffer cb[8];
    GLTextureColorTarget texture_targets[8];
    bool color_needs_resolve[8];
    bool color_buffer_owns_aux[8];
    GX2DepthBuffer db;
    bool depth_needs_expand;
    bool dirty;
} GLFramebuffer;

static GLFramebuffer g_framebuffers[MAX_FRAMEBUFFERS];
static GLRenderbuffer g_renderbuffers[MAX_RENDERBUFFERS];
static bool g_default_framebuffer_uses_drc = false;

static bool attachment_ref_present(const GLAttachmentRef *attachment);
static GLRenderbuffer *get_renderbuffer(GLuint id);
static GX2ColorBuffer *get_default_color_buffer(void);
static void clear_attachment_ref(GLAttachmentRef *attachment);
static void free_framebuffer_texture_target(GLFramebuffer *fb, uint32_t index);
static uint32_t texture_level_extent(uint32_t extent, GLint level);
static uint32_t attachment_view_slice(const GLAttachmentRef *attachment);
static uint32_t texture_level_depth_for_attachment(const GLAttachmentRef *attachment,
                                                   const GX2Surface *surface);

static void detach_renderbuffer_from_framebuffer(GLuint framebuffer,
                                                GLuint renderbuffer) {
    GLFramebuffer *fb;
    bool detached = false;

    if (renderbuffer == 0 || framebuffer == 0 ||
        framebuffer >= MAX_FRAMEBUFFERS || !g_framebuffers[framebuffer].in_use) {
        return;
    }

    fb = &g_framebuffers[framebuffer];

    for (uint32_t j = 0; j < 8; ++j) {
        if (fb->color_attachments[j].kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
            fb->color_attachments[j].object == renderbuffer) {
            clear_attachment_ref(&fb->color_attachments[j]);
            fb->color_needs_resolve[j] = false;
            free_framebuffer_texture_target(fb, j);
            detached = true;
        }
    }

    if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
        fb->depth_attachment.object == renderbuffer) {
        clear_attachment_ref(&fb->depth_attachment);
        memset(&fb->db, 0, sizeof(fb->db));
        detached = true;
    }

    if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
        fb->stencil_attachment.object == renderbuffer) {
        clear_attachment_ref(&fb->stencil_attachment);
        memset(&fb->db, 0, sizeof(fb->db));
        detached = true;
    }

    if (detached) {
        fb->dirty = true;
        if (g_gl_context &&
            (g_gl_context->bound_framebuffer == framebuffer ||
             g_gl_context->bound_read_framebuffer == framebuffer)) {
            g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        }
    }
}

static void detach_renderbuffer_from_current_framebuffers(GLuint renderbuffer) {
    if (!g_gl_context || renderbuffer == 0) return;

    detach_renderbuffer_from_framebuffer(g_gl_context->bound_framebuffer,
                                         renderbuffer);
    if (g_gl_context->bound_read_framebuffer != g_gl_context->bound_framebuffer) {
        detach_renderbuffer_from_framebuffer(g_gl_context->bound_read_framebuffer,
                                             renderbuffer);
    }
}

static void mark_framebuffers_for_renderbuffer(GLuint renderbuffer) {
    if (renderbuffer == 0) return;

    for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; ++i) {
        GLFramebuffer *fb = &g_framebuffers[i];
        bool referenced = false;

        if (!fb->in_use) continue;

        for (uint32_t j = 0; j < 8; ++j) {
            referenced = referenced ||
                         (fb->color_attachments[j].kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
                          fb->color_attachments[j].object == renderbuffer);
        }

        referenced = referenced ||
                     (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
                      fb->depth_attachment.object == renderbuffer) ||
                     (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_RENDERBUFFER &&
                      fb->stencil_attachment.object == renderbuffer);

        if (referenced) {
            fb->dirty = true;
            if (g_gl_context &&
                (g_gl_context->bound_framebuffer == i ||
                 g_gl_context->bound_read_framebuffer == i)) {
                g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
            }
        }
    }
}

static bool get_color_attachment_index(GLenum attachment, uint32_t *index) {
    if (attachment < GL_COLOR_ATTACHMENT0 || attachment > GL_COLOR_ATTACHMENT7) {
        return false;
    }
    if (index) {
        *index = (uint32_t)(attachment - GL_COLOR_ATTACHMENT0);
    }
    return true;
}

static bool is_color_attachment_name(GLenum attachment, uint32_t *index) {
    if (attachment < GL_COLOR_ATTACHMENT0) {
        return false;
    }
    if (index) {
        *index = (uint32_t)(attachment - GL_COLOR_ATTACHMENT0);
    }
    return true;
}

static bool is_default_framebuffer_color_name(GLenum buffer) {
    return buffer == GL_FRONT || buffer == GL_BACK || buffer == GL_FRONT_AND_BACK;
}

static GX2RResourceFlags build_framebuffer_surface_flags(GLint internal_format,
                                                         bool is_depth,
                                                         bool sampled_texture) {
    GX2RResourceFlags flags;

    flags = (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_CPU_WRITE |
                                GX2R_RESOURCE_USAGE_CPU_READ |
                                GX2R_RESOURCE_USAGE_GPU_READ |
                                GX2R_RESOURCE_USAGE_GPU_WRITE |
                                GX2R_RESOURCE_USAGE_FORCE_MEM2);

    if (sampled_texture)
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_TEXTURE);

    if (is_depth) {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_DEPTH_BUFFER);
    }
    else {
        flags = (GX2RResourceFlags)(flags | GX2R_RESOURCE_BIND_COLOR_BUFFER);
    }

    (void)internal_format;
    return flags;
}

static void free_surface_storage(GX2Surface *surface) {
    if (!surface) return;
    if (surface->resourceFlags) {
        GX2RDestroySurfaceEx(surface, GX2R_RESOURCE_BIND_NONE);
        memset(surface, 0, sizeof(*surface));
        return;
    }
    if (surface->image) gl_mem_free(GL_MEM_TYPE_MEM2, surface->image);
    if (surface->mipmaps) gl_mem_free(GL_MEM_TYPE_MEM2, surface->mipmaps);
    memset(surface, 0, sizeof(*surface));
}

static void free_color_buffer_aux(GX2ColorBuffer *color_buffer) {
    if (!color_buffer) return;
    if (color_buffer->aaBuffer) gl_mem_free(GL_MEM_TYPE_MEM2, color_buffer->aaBuffer);
    color_buffer->aaBuffer = NULL;
    color_buffer->aaSize = 0;
}

static void free_texture_color_target(GLTextureColorTarget *target) {
    if (!target) return;
    free_color_buffer_aux(&target->color_buffer);
    if (target->allocated) free_surface_storage(&target->surface);
    memset(target, 0, sizeof(*target));
}

static void free_framebuffer_texture_target(GLFramebuffer *fb, uint32_t index) {
    if (!fb || index >= 8) return;
    free_texture_color_target(&fb->texture_targets[index]);
    fb->color_needs_resolve[index] = false;
    if (fb->color_buffer_owns_aux[index]) {
        free_color_buffer_aux(&fb->cb[index]);
    }
    fb->color_buffer_owns_aux[index] = false;
    memset(&fb->cb[index], 0, sizeof(fb->cb[index]));
}

static void invalidate_surface_after_write(const GX2Surface *surface, bool depth) {
    if (!surface) return;
    if (surface->resourceFlags) {
        GX2RInvalidateSurface((GX2Surface *)surface, 0,
                              (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_GPU_WRITE |
                                                  GX2R_RESOURCE_USAGE_GPU_READ));
        return;
    }
    if (surface->image && surface->imageSize) {
        GX2Invalidate((GX2InvalidateMode)((depth ? GX2_INVALIDATE_MODE_DEPTH_BUFFER
                                                : GX2_INVALIDATE_MODE_COLOR_BUFFER) |
                                          GX2_INVALIDATE_MODE_TEXTURE),
                      surface->image, surface->imageSize);
    }
    if (surface->mipmaps && surface->mipmapSize) {
        GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE,
                      surface->mipmaps, surface->mipmapSize);
    }
}

static void invalidate_surface_after_color_write(const GX2Surface *surface) {
    invalidate_surface_after_write(surface, false);
}

static void restore_context_after_surface_op(void) {
    GX2ContextState *state = WHBGfxGetTVContextState();

    if (!state) state = WHBGfxGetDRCContextState();
    if (state) GX2SetContextState(state);
    if (g_gl_context) g_gl_context->dirty_flags = 0xFFFFFFFFu;
}

static bool stage_surface_for_cpu_access(const GX2Surface *source,
                                         GLint internal_format,
                                         bool depth,
                                         GX2Surface *staging_surface) {
    GX2RResourceFlags surface_flags;

    if (!source || !source->image || !staging_surface) return false;

    memset(staging_surface, 0, sizeof(*staging_surface));
    staging_surface->dim = source->dim;
    staging_surface->width = source->width;
    staging_surface->height = source->height;
    staging_surface->depth = source->depth ? source->depth : 1;
    staging_surface->mipLevels = 1;
    staging_surface->format = source->format;
    staging_surface->aa = GX2_AA_MODE1X;
    staging_surface->use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE |
        (depth ? GX2_SURFACE_USE_DEPTH_BUFFER : GX2_SURFACE_USE_COLOR_BUFFER));
    staging_surface->tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    GX2CalcSurfaceSizeAndAlignment(staging_surface);
    surface_flags = build_framebuffer_surface_flags(internal_format, depth, true);

    if (!GX2RCreateSurface(staging_surface, surface_flags)) {
        memset(staging_surface, 0, sizeof(*staging_surface));
        return false;
    }

    GX2CopySurface(source, 0, 0, staging_surface, 0, 0);
    GX2DrawDone();
    restore_context_after_surface_op();
    invalidate_surface_after_write(staging_surface, depth);
    DCInvalidateRange(staging_surface->image, staging_surface->imageSize);
    return true;
}

static bool upload_cpu_staging_surface(GX2Surface *staging_surface,
                                       GX2Surface *destination,
                                       bool depth) {
    if (!staging_surface || !staging_surface->image || !destination ||
        !destination->image) {
        return false;
    }

    DCFlushRange(staging_surface->image, staging_surface->imageSize);
    GX2RInvalidateSurface(staging_surface, 0,
        (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_CPU_WRITE |
                            GX2R_RESOURCE_USAGE_GPU_READ));
    GX2CopySurface(staging_surface, 0, 0, destination, 0, 0);
    GX2DrawDone();
    restore_context_after_surface_op();
    invalidate_surface_after_write(destination, depth);
    return true;
}

static bool init_color_buffer_from_surface_view(GX2ColorBuffer *cb,
                                                const GX2Surface *s,
                                                GLint level,
                                                uint32_t slice,
                                                uint32_t slices) {
    uint32_t aux_size = 0;
    uint32_t aux_alignment = 0;

    if (!cb || !s) return false;

    free_color_buffer_aux(cb);

    memset(cb, 0, sizeof(*cb));
    cb->surface = *s;
    cb->viewMip = (uint32_t)(level < 0 ? 0 : level);
    cb->viewFirstSlice = slice;
    cb->viewNumSlices = slices ? slices : 1u;

    GX2InitColorBufferRegs(cb);
    GX2CalcColorBufferAuxInfo(cb, &aux_size, &aux_alignment);
    if (aux_size) {
        cb->aaBuffer = gl_mem_alloc(GL_MEM_TYPE_MEM2, aux_size, aux_alignment ? aux_alignment : 0x100);
        if (!cb->aaBuffer) {
            memset(cb, 0, sizeof(*cb));
            return false;
        }

        memset(cb->aaBuffer, GX2GL_AUX_BUFFER_CLEAR_VALUE, aux_size);
        cb->aaSize = aux_size;
        DCFlushRange(cb->aaBuffer, cb->aaSize);
        GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU |
                                          GX2_INVALIDATE_MODE_COLOR_BUFFER),
                      cb->aaBuffer, cb->aaSize);
    }

    return true;
}

static bool init_color_buffer_from_surface(GX2ColorBuffer *cb, const GX2Surface *s) {
    return init_color_buffer_from_surface_view(cb, s, 0, 0, 1);
}

static bool ensure_texture_color_target(GLFramebuffer *fb, uint32_t index, const GX2Texture *texture) {
    GLTextureColorTarget *target;
    GX2Surface *surface;
    GX2RResourceFlags surface_flags;
    const GLAttachmentRef *attachment;
    uint32_t depth;
    uint32_t slice;

    if (!fb || index >= 8 || !texture || !texture->surface.image) return false;

    attachment = &fb->color_attachments[index];
    depth = texture_level_depth_for_attachment(attachment, &texture->surface);
    slice = attachment_view_slice(attachment);
    if (slice >= depth) return false;

    target = &fb->texture_targets[index];
    surface = &target->surface;
    if (target->allocated &&
        surface->dim == texture->surface.dim &&
        surface->width == texture_level_extent(texture->surface.width, attachment->level) &&
        surface->height == texture_level_extent(texture->surface.height, attachment->level) &&
        surface->depth == depth &&
        surface->format == texture->surface.format) {
        return true;
    }

    free_texture_color_target(target);

    memset(surface, 0, sizeof(*surface));
    surface->dim = texture->surface.dim;
    surface->width = texture_level_extent(texture->surface.width, attachment->level);
    surface->height = texture_level_extent(texture->surface.height, attachment->level);
    surface->depth = depth;
    surface->mipLevels = 1;
    surface->format = texture->surface.format;
    surface->aa = GX2_AA_MODE1X;
    surface->use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
    surface->tileMode = GX2_TILE_MODE_DEFAULT;
    GX2CalcSurfaceSizeAndAlignment(surface);
    surface_flags = build_framebuffer_surface_flags(
        gl_get_texture_internal_format(fb->color_attachments[index].object), false, true);

    if (!GX2RCreateSurface(surface, surface_flags)) {
        memset(surface, 0, sizeof(*surface));
        return false;
    }

    if (surface->image && surface->imageSize)
        memset(surface->image, 0, surface->imageSize);
    if (surface->mipmaps && surface->mipmapSize)
        memset(surface->mipmaps, 0, surface->mipmapSize);

    if (!init_color_buffer_from_surface_view(&target->color_buffer, surface,
                                             0, slice, 1)) {
        free_surface_storage(surface);
        return false;
    }

    target->allocated = true;
    return true;
}

static void resolve_framebuffer_texture_targets(GLFramebuffer *fb) {
    if (!fb) return;
    for (uint32_t i = 0; i < 8; ++i) {
        GX2Texture *texture;
        GLTextureColorTarget *target;
        if (!fb->color_needs_resolve[i]) continue;
        if (fb->color_attachments[i].kind != GL_ATTACHMENT_KIND_TEXTURE) {
            fb->color_needs_resolve[i] = false;
            continue;
        }

        texture = gl_get_gx2_texture(fb->color_attachments[i].object);
        if (!texture || !texture->surface.image) {
            fb->color_needs_resolve[i] = false;
            continue;
        }

        target = &fb->texture_targets[i];
        GX2DrawDone();
        if (texture->surface.aa != GX2_AA_MODE1X) {
            GX2ExpandAAColorBuffer(&fb->cb[i]);
            GX2DrawDone();
            restore_context_after_surface_op();
        } else if (target->allocated && target->surface.image) {
            uint32_t slice = attachment_view_slice(&fb->color_attachments[i]);
            GX2CopySurface(&target->surface, 0, slice,
                           &texture->surface,
                           (uint32_t)fb->color_attachments[i].level,
                           slice);
            GX2DrawDone();
            restore_context_after_surface_op();
        }
        invalidate_surface_after_color_write(&texture->surface);
        fb->color_needs_resolve[i] = false;
    }
}

static void init_draw_buffer_defaults(GLFramebuffer *fb, bool is_default) {
    if (!fb) return;
    for (uint32_t i = 0; i < 8; ++i) fb->draw_buffers[i] = GL_NONE;
    fb->draw_buffers[0] = is_default ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

static void init_read_buffer_default(GLFramebuffer *fb, bool is_default) {
    if (!fb) return;
    fb->read_buffer = is_default ? GL_BACK : GL_COLOR_ATTACHMENT0;
}

static bool is_framebuffer_target(GLenum target) {
    return target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER;
}

static bool is_cube_map_face_target(GLenum target) {
    return target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
           target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
}

static uint32_t cube_map_face_slice(GLenum target) {
    return is_cube_map_face_target(target)
               ? (uint32_t)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X)
               : 0u;
}

static uint32_t texture_level_extent(uint32_t extent, GLint level) {
    uint32_t shifted;

    if (level <= 0) return extent ? extent : 1u;
    shifted = extent >> (uint32_t)level;
    return shifted ? shifted : 1u;
}

static GLint max_texture_level_for_size(uint32_t size) {
    GLint level = 0;

    while (size > 1u) {
        size >>= 1u;
        ++level;
    }
    return level;
}

static uint32_t attachment_view_slice(const GLAttachmentRef *attachment) {
    if (!attachment || attachment->kind != GL_ATTACHMENT_KIND_TEXTURE) return 0u;
    if (attachment->layered) return 0u;
    if (attachment->textarget == GL_TEXTURE_3D ||
        attachment->textarget == GL_TEXTURE_1D_ARRAY ||
        attachment->textarget == GL_TEXTURE_2D_ARRAY ||
        attachment->textarget == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        return (uint32_t)attachment->layer;
    return cube_map_face_slice(attachment->textarget);
}

static uint32_t texture_level_depth_for_attachment(const GLAttachmentRef *attachment,
                                                   const GX2Surface *surface) {
    if (!attachment || !surface) return 1u;
    if (attachment->textarget == GL_TEXTURE_3D) {
        return texture_level_extent(surface->depth, attachment->level);
    }
    if (attachment->textarget == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        return surface->depth ? surface->depth : 1u;
    }
    if (attachment->textarget == GL_TEXTURE_1D_ARRAY ||
        attachment->textarget == GL_TEXTURE_2D_ARRAY) {
        return surface->depth ? surface->depth : 1u;
    }
    if (attachment->textarget == GL_TEXTURE_CUBE_MAP) {
        return surface->depth ? surface->depth : 6u;
    }
    if (is_cube_map_face_target(attachment->textarget)) {
        return surface->depth ? surface->depth : 6u;
    }
    return 1u;
}

static uint32_t attachment_view_count(const GLAttachmentRef *attachment,
                                      const GX2Surface *surface) {
    return attachment && attachment->layered
               ? texture_level_depth_for_attachment(attachment, surface)
               : 1u;
}

static bool attachment_view_exists(const GLAttachmentRef *attachment,
                                   const GX2Texture *texture) {
    uint32_t depth;

    if (!attachment_ref_present(attachment) || !texture ||
        !texture->surface.image || attachment->level < 0 ||
        (uint32_t)attachment->level >= texture->surface.mipLevels) {
        return false;
    }

    depth = texture_level_depth_for_attachment(attachment, &texture->surface);
    return attachment_view_slice(attachment) < depth;
}

static bool build_texture_attachment_read_surface(const GLAttachmentRef *attachment,
                                                  const GX2Surface *source,
                                                  GX2Surface *view) {
    GX2Surface level_surface;
    uint8_t *level_base;
    uint32_t depth;
    uint32_t slice;
    uint32_t slice_size;

    if (!attachment || !source || !view || !source->image ||
        attachment->level < 0 ||
        (uint32_t)attachment->level >= source->mipLevels) {
        return false;
    }

    depth = texture_level_depth_for_attachment(attachment, source);
    slice = attachment_view_slice(attachment);
    if (depth == 0 || slice >= depth) return false;

    level_base = attachment->level == 0
                     ? (uint8_t *)source->image
                     : (source->mipmaps
                            ? (uint8_t *)source->mipmaps +
                                  source->mipLevelOffset[attachment->level - 1]
                            : NULL);
    if (!level_base) return false;

    memset(&level_surface, 0, sizeof(level_surface));
    level_surface.dim = source->dim;
    level_surface.width = texture_level_extent(source->width, attachment->level);
    level_surface.height = texture_level_extent(source->height, attachment->level);
    level_surface.depth = depth;
    level_surface.mipLevels = 1;
    level_surface.format = source->format;
    level_surface.aa = source->aa;
    level_surface.use = source->use;
    level_surface.tileMode = source->tileMode;
    GX2CalcSurfaceSizeAndAlignment(&level_surface);

    slice_size = depth > 0 ? level_surface.imageSize / depth
                           : level_surface.imageSize;
    *view = level_surface;
    view->depth = 1;
    view->image = level_base + (size_t)slice * (size_t)slice_size;
    view->imageSize = slice_size;
    view->mipmaps = NULL;
    view->mipmapSize = 0;
    view->resourceFlags = (GX2RResourceFlags)0;
    return true;
}

static GLuint get_bound_framebuffer_for_target(GLenum target) {
    switch (target) {
    case GL_FRAMEBUFFER:
    case GL_DRAW_FRAMEBUFFER:
        return g_gl_context ? g_gl_context->bound_framebuffer : 0;
    case GL_READ_FRAMEBUFFER:
        return g_gl_context ? g_gl_context->bound_read_framebuffer : 0;
    default:
        return 0;
    }
}

static void clear_attachment_ref(GLAttachmentRef *attachment) {
    if (!attachment) return;
    attachment->kind = GL_ATTACHMENT_KIND_NONE;
    attachment->object = 0;
    attachment->textarget = 0;
    attachment->level = 0;
    attachment->layer = 0;
    attachment->layered = GL_FALSE;
}

static bool attachment_ref_present(const GLAttachmentRef *attachment) {
    return attachment && attachment->kind != GL_ATTACHMENT_KIND_NONE && attachment->object != 0;
}

static void set_texture_attachment_ref(GLAttachmentRef *attachment,
                                       GLuint texture,
                                       GLenum textarget,
                                       GLint level,
                                       GLint layer,
                                       GLboolean layered) {
    if (!attachment) return;
    attachment->kind = GL_ATTACHMENT_KIND_TEXTURE;
    attachment->object = texture;
    attachment->textarget = textarget;
    attachment->level = level;
    attachment->layer = layer;
    attachment->layered = layered;
}

static void set_renderbuffer_attachment_ref(GLAttachmentRef *attachment,
                                           GLuint renderbuffer) {
    if (!attachment) return;
    attachment->kind = GL_ATTACHMENT_KIND_RENDERBUFFER;
    attachment->object = renderbuffer;
    attachment->textarget = 0;
    attachment->level = 0;
    attachment->layer = 0;
    attachment->layered = GL_FALSE;
}

static void init_framebuffer_object(GLFramebuffer *fb, bool is_default) {
    if (!fb) return;
    memset(fb, 0, sizeof(*fb));
    fb->in_use = true;
    fb->reserved = false;
    fb->dirty = true;
    init_draw_buffer_defaults(fb, is_default);
    init_read_buffer_default(fb, is_default);
}

static void reserve_framebuffer_name(GLFramebuffer *fb) {
    if (!fb) return;
    memset(fb, 0, sizeof(*fb));
    fb->reserved = true;
}

static bool framebuffer_name_is_valid(GLuint id) {
    return id == 0 ||
           (id < MAX_FRAMEBUFFERS &&
            (g_framebuffers[id].reserved || g_framebuffers[id].in_use));
}

static void init_renderbuffer_object(GLRenderbuffer *rb) {
    if (!rb) return;
    memset(rb, 0, sizeof(*rb));
    rb->in_use = true;
    rb->reserved = false;
}

static void reserve_renderbuffer_name(GLRenderbuffer *rb) {
    if (!rb) return;
    memset(rb, 0, sizeof(*rb));
    rb->reserved = true;
}

static bool renderbuffer_name_is_valid(GLuint id) {
    return id == 0 ||
           (id < MAX_RENDERBUFFERS &&
            (g_renderbuffers[id].reserved || g_renderbuffers[id].in_use));
}

static GLAttachmentRef *get_attachment_ref(GLFramebuffer *fb, GLenum attachment) {
    if (!fb) return NULL;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) return &fb->color_attachments[attachment - GL_COLOR_ATTACHMENT0];
    if (attachment == GL_DEPTH_ATTACHMENT) return &fb->depth_attachment;
    if (attachment == GL_STENCIL_ATTACHMENT) return &fb->stencil_attachment;
    return NULL;
}

static GLRenderbuffer *get_renderbuffer(GLuint id) {
    if (id == 0 || id >= MAX_RENDERBUFFERS || !g_renderbuffers[id].in_use) return NULL;
    return &g_renderbuffers[id];
}

static bool is_depth_internal_format(GLint internal_format) {
    switch (internal_format) {
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT32F: case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8: return true;
    default: return false;
    }
}

static bool is_stencil_internal_format(GLint internal_format) {
    return internal_format == GL_DEPTH_STENCIL || internal_format == GL_DEPTH24_STENCIL8;
}

static GLint renderbuffer_component_bits(GLint internal_format, GLenum component) {
    switch (internal_format) {
    case 1:
    case GL_RED:
    case GL_R8:
        return component == GL_RENDERBUFFER_RED_SIZE ? 8 : 0;
    case 2:
    case GL_RG:
    case GL_RG8:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE) ? 8 : 0;
    case 3:
    case GL_RGB:
    case GL_RGB8:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE) ? 8 : 0;
    case 4:
    case GL_RGBA:
    case GL_RGBA8:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE ||
                component == GL_RENDERBUFFER_ALPHA_SIZE) ? 8 : 0;
    case GL_RGBA16F:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE ||
                component == GL_RENDERBUFFER_ALPHA_SIZE) ? 16 : 0;
    case GL_RGBA32F:
        return (component == GL_RENDERBUFFER_RED_SIZE ||
                component == GL_RENDERBUFFER_GREEN_SIZE ||
                component == GL_RENDERBUFFER_BLUE_SIZE ||
                component == GL_RENDERBUFFER_ALPHA_SIZE) ? 32 : 0;
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
        return component == GL_RENDERBUFFER_DEPTH_SIZE ? 32 : 0;
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        if (component == GL_RENDERBUFFER_DEPTH_SIZE) return 24;
        if (component == GL_RENDERBUFFER_STENCIL_SIZE) return 8;
        return 0;
    default:
        return 0;
    }
}

static GLint attachment_component_bits(GLint internal_format, GLenum pname) {
    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_RED_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_GREEN_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_BLUE_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_ALPHA_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_DEPTH_SIZE);
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
        return renderbuffer_component_bits(internal_format, GL_RENDERBUFFER_STENCIL_SIZE);
    default:
        return 0;
    }
}

static GLenum attachment_component_type(GLint internal_format) {
    switch (internal_format) {
    case GL_RGBA16F:
    case GL_RGBA32F:
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT32F:
        return GL_FLOAT;
    case 1:
    case 2:
    case 3:
    case 4:
    case GL_RED:
    case GL_RG:
    case GL_RGB:
    case GL_RGBA:
    case GL_R8:
    case GL_RG8:
    case GL_RGB8:
    case GL_RGBA8:
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
        return GL_UNSIGNED_NORMALIZED;
    default:
        return GL_NONE;
    }
}

static GX2LogicOp map_framebuffer_logic_op(GLenum op) {
    switch (op) {
    case GL_CLEAR:         return GX2_LOGIC_OP_CLEAR;
    case GL_SET:           return GX2_LOGIC_OP_SET;
    case GL_COPY:          return GX2_LOGIC_OP_COPY;
    case GL_COPY_INVERTED: return GX2_LOGIC_OP_INV_COPY;
    case GL_NOOP:          return GX2_LOGIC_OP_NOP;
    case GL_INVERT:        return GX2_LOGIC_OP_INV;
    case GL_AND:           return GX2_LOGIC_OP_AND;
    case GL_NAND:          return GX2_LOGIC_OP_NOT_AND;
    case GL_OR:            return GX2_LOGIC_OP_OR;
    case GL_NOR:           return GX2_LOGIC_OP_NOR;
    case GL_XOR:           return GX2_LOGIC_OP_XOR;
    case GL_EQUIV:         return GX2_LOGIC_OP_EQUIV;
    case GL_AND_REVERSE:   return GX2_LOGIC_OP_REV_AND;
    case GL_AND_INVERTED:  return GX2_LOGIC_OP_INV_AND;
    case GL_OR_REVERSE:    return GX2_LOGIC_OP_REV_OR;
    case GL_OR_INVERTED:   return GX2_LOGIC_OP_INV_OR;
    default:               return GX2_LOGIC_OP_COPY;
    }
}

static bool get_renderbuffer_format_info(GLenum internalformat, GX2SurfaceFormat *gx2_format, GX2SurfaceUse *surface_use, bool *is_depth) {
    if (!gx2_format || !surface_use || !is_depth) return false;
    switch (internalformat) {
    case 1: case GL_RED: case GL_R8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case 2: case GL_RG: case GL_RG8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case 3: case GL_RGB: case GL_RGB8:
    case 4: case GL_RGBA: case GL_RGBA8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case GL_RGBA16F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case GL_RGBA32F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
        *is_depth = false; return true;
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT32F:
        *gx2_format = GX2_SURFACE_FORMAT_FLOAT_R32;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
        *is_depth = true; return true;
    case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8:
        *gx2_format = GX2_SURFACE_FORMAT_UNORM_R24_X8;
        *surface_use = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER);
        *is_depth = true; return true;
    default: return false;
    }
}

static GX2ChannelMask build_color_channel_mask(void) {
    uint8_t mask = 0;
    if (!g_gl_context) return (GX2ChannelMask)0;
    if (g_gl_context->color_mask[0]) mask |= GX2_CHANNEL_MASK_R;
    if (g_gl_context->color_mask[1]) mask |= GX2_CHANNEL_MASK_G;
    if (g_gl_context->color_mask[2]) mask |= GX2_CHANNEL_MASK_B;
    if (g_gl_context->color_mask[3]) mask |= GX2_CHANNEL_MASK_A;
    return (GX2ChannelMask)mask;
}

static void apply_framebuffer_output_state(GLFramebuffer *fb, bool is_default) {
    GX2ChannelMask masks[8] = {(GX2ChannelMask)0};
    GX2ChannelMask color_mask;
    uint8_t active_target_mask = 0;
    uint32_t active_target_count = 0;
    if (!g_gl_context || !fb) return;
    color_mask = build_color_channel_mask();
    for (uint32_t i = 0; i < 8; ++i) {
        bool enabled = false;
        uint32_t attachment_index = 0;
        if (is_default) enabled = (i == 0 && fb->draw_buffers[0] == GL_BACK);
        else if (get_color_attachment_index(fb->draw_buffers[i], &attachment_index) &&
                 attachment_ref_present(&fb->color_attachments[attachment_index])) enabled = true;
        if (enabled && color_mask != 0) {
            masks[i] = color_mask;
            active_target_mask |= (uint8_t)(1u << i);
            ++active_target_count;
        }
    }
    GX2SetTargetChannelMasks(masks[0], masks[1], masks[2], masks[3], masks[4], masks[5], masks[6], masks[7]);
    GX2SetColorControl(map_framebuffer_logic_op(g_gl_context->logic_op),
                       g_gl_context->blend_enabled ? active_target_mask : 0,
                       active_target_count > 1 ? GX2_ENABLE : GX2_DISABLE,
                       active_target_mask != 0 ? GX2_ENABLE : GX2_DISABLE);
}

static void init_depth_buffer_from_surface_view(GX2DepthBuffer *db,
                                                const GX2Surface *s,
                                                GLint level,
                                                uint32_t slice,
                                                uint32_t slices) {
    memset(db, 0, sizeof(*db));
    db->surface = *s;
    db->viewMip = (uint32_t)(level < 0 ? 0 : level);
    db->viewFirstSlice = slice;
    db->viewNumSlices = slices ? slices : 1u;
    db->depthClear = 1.0f;
    GX2InitDepthBufferRegs(db);
}

static void init_depth_buffer_from_surface(GX2DepthBuffer *db, const GX2Surface *s) {
    init_depth_buffer_from_surface_view(db, s, 0, 0, 1);
}

typedef struct {
    GLsizei width;
    GLsizei height;
    GLsizei samples;
    GLboolean fixed_sample_locations;
    GLint internal_format;
    bool depth_renderable;
    bool stencil_renderable;
} GLAttachmentImageInfo;

static bool get_attachment_image_info(const GLAttachmentRef *attachment,
                                      GLAttachmentImageInfo *info) {
    if (!attachment_ref_present(attachment) || !info) return false;

    if (attachment->kind == GL_ATTACHMENT_KIND_TEXTURE) {
        GX2Texture *texture = gl_get_gx2_texture(attachment->object);
        if (!attachment_view_exists(attachment, texture)) return false;
        info->width = (GLsizei)texture_level_extent(texture->surface.width,
                                                    attachment->level);
        info->height = (GLsizei)texture_level_extent(texture->surface.height,
                                                     attachment->level);
        info->samples = gl_get_texture_samples(attachment->object);
        info->fixed_sample_locations =
            gl_get_texture_fixed_sample_locations(attachment->object);
        info->internal_format = gl_get_texture_internal_format(attachment->object);
        info->depth_renderable = is_depth_internal_format(info->internal_format);
        info->stencil_renderable = is_stencil_internal_format(info->internal_format);
        return true;
    }

    if (attachment->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        GLRenderbuffer *renderbuffer = get_renderbuffer(attachment->object);
        if (!renderbuffer) return false;
        info->width = renderbuffer->width;
        info->height = renderbuffer->height;
        info->samples = renderbuffer->samples;
        info->fixed_sample_locations = GL_TRUE;
        info->internal_format = renderbuffer->internal_format;
        info->depth_renderable = is_depth_internal_format(info->internal_format);
        info->stencil_renderable = is_stencil_internal_format(info->internal_format);
        return true;
    }

    return false;
}

static GLsizei framebuffer_sample_count(GLuint framebuffer) {
    GLFramebuffer *fb;
    GLAttachmentImageInfo info;

    if (framebuffer == 0 || framebuffer >= MAX_FRAMEBUFFERS) return 0;
    fb = &g_framebuffers[framebuffer];
    for (uint32_t i = 0; i < 8; ++i) {
        if (get_attachment_image_info(&fb->color_attachments[i], &info)) {
            return info.samples;
        }
    }
    if (get_attachment_image_info(&fb->depth_attachment, &info)) {
        return info.samples;
    }
    if (get_attachment_image_info(&fb->stencil_attachment, &info)) {
        return info.samples;
    }
    return 0;
}

static float decode_gpu_float32(const uint8_t *ptr) {
    uint32_t word;
    float value;

    memcpy(&word, ptr, sizeof(word));
    word = GPU_TO_CPU_32(word);
    memcpy(&value, &word, sizeof(value));
    return value;
}

static void encode_gpu_float32(uint8_t *ptr, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    word = CPU_TO_GPU_32(word);
    memcpy(ptr, &word, sizeof(word));
}

static float decode_gpu_half_float(const uint8_t *ptr) {
    uint16_t word;
    uint32_t sign;
    uint32_t exponent;
    uint32_t mantissa;
    uint32_t out_word;
    float value;

    memcpy(&word, ptr, sizeof(word));
    word = GPU_TO_CPU_16(word);

    sign = (uint32_t)(word & 0x8000u) << 16;
    exponent = (word >> 10) & 0x1Fu;
    mantissa = word & 0x03FFu;

    if (exponent == 0) {
        if (mantissa == 0) {
            out_word = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x03FFu;
            out_word = sign | ((exponent + (127u - 15u)) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        out_word = sign | 0x7F800000u | (mantissa << 13);
    } else {
        out_word = sign | ((exponent + (127u - 15u)) << 23) | (mantissa << 13);
    }

    memcpy(&value, &out_word, sizeof(value));
    return value;
}

static uint8_t float_to_unorm8(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static GX2ColorBuffer *get_read_color_buffer(void) {
    uint32_t attachment_index = 0;
    GLuint fbo;

    if (!g_gl_context) return NULL;

    fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        if (g_framebuffers[0].read_buffer != GL_BACK) return NULL;
        return get_default_color_buffer();
    }

    if (fbo >= MAX_FRAMEBUFFERS || !g_framebuffers[fbo].in_use) return NULL;

    if (g_gl_context->bound_framebuffer == fbo) {
        gl_bind_framebuffers();
    } else {
        GLFramebuffer *fb = &g_framebuffers[fbo];
        if (fb->dirty) {
            for (uint32_t i = 0; i < 8; ++i) {
                if (!attachment_ref_present(&fb->color_attachments[i])) {
                    free_framebuffer_texture_target(fb, i);
                    continue;
                }
                if (fb->color_attachments[i].kind == GL_ATTACHMENT_KIND_TEXTURE) {
                    GX2Texture *t = gl_get_gx2_texture(fb->color_attachments[i].object);
                    free_framebuffer_texture_target(fb, i);
                    if (t && init_color_buffer_from_surface_view(
                                 &fb->cb[i], &t->surface,
                                 fb->color_attachments[i].level,
                                 attachment_view_slice(&fb->color_attachments[i]),
                                 attachment_view_count(&fb->color_attachments[i],
                                                       &t->surface))) {
                        fb->color_buffer_owns_aux[i] = true;
                    }
                } else {
                    free_framebuffer_texture_target(fb, i);
                    GLRenderbuffer *rb = get_renderbuffer(fb->color_attachments[i].object);
                    if (rb && !rb->is_depth) {
                        fb->cb[i] = rb->color_buffer;
                        fb->color_buffer_owns_aux[i] = false;
                    }
                }
            }
            memset(&fb->db, 0, sizeof(fb->db));
            if (attachment_ref_present(&fb->depth_attachment)) {
                if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                    GX2Texture *t = gl_get_gx2_texture(fb->depth_attachment.object);
                    if (t) init_depth_buffer_from_surface_view(
                               &fb->db, &t->surface, fb->depth_attachment.level,
                               attachment_view_slice(&fb->depth_attachment),
                               attachment_view_count(&fb->depth_attachment,
                                                     &t->surface));
                } else {
                    GLRenderbuffer *rb = get_renderbuffer(fb->depth_attachment.object);
                    if (rb && rb->is_depth) fb->db = rb->depth_buffer;
                }
            } else if (attachment_ref_present(&fb->stencil_attachment)) {
                if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                    GX2Texture *t = gl_get_gx2_texture(fb->stencil_attachment.object);
                    if (t) init_depth_buffer_from_surface_view(
                               &fb->db, &t->surface, fb->stencil_attachment.level,
                               attachment_view_slice(&fb->stencil_attachment),
                               attachment_view_count(&fb->stencil_attachment,
                                                     &t->surface));
                } else {
                    GLRenderbuffer *rb = get_renderbuffer(fb->stencil_attachment.object);
                    if (rb && rb->is_depth) fb->db = rb->depth_buffer;
                }
            }
            fb->dirty = false;
        }
    }

    resolve_framebuffer_texture_targets(&g_framebuffers[fbo]);

    if (g_framebuffers[fbo].read_buffer == GL_NONE) return NULL;
    if (!get_color_attachment_index(g_framebuffers[fbo].read_buffer, &attachment_index)) {
        return NULL;
    }

    if (!attachment_ref_present(&g_framebuffers[g_gl_context->bound_read_framebuffer].color_attachments[attachment_index])) {
        return NULL;
    }
    return &g_framebuffers[g_gl_context->bound_read_framebuffer].cb[attachment_index];
}

static GLint get_read_color_internal_format(void) {
    GLuint fbo;
    uint32_t attachment_index = 0;

    if (!g_gl_context) return GL_RGBA8;

    fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        return GL_RGBA8;
    }
    if (fbo >= MAX_FRAMEBUFFERS || !g_framebuffers[fbo].in_use) {
        return GL_RGBA8;
    }
    if (!get_color_attachment_index(g_framebuffers[fbo].read_buffer, &attachment_index)) {
        return GL_RGBA8;
    }

    if (g_framebuffers[fbo].color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
        return gl_get_texture_internal_format(g_framebuffers[fbo].color_attachments[attachment_index].object);
    }
    if (g_framebuffers[fbo].color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        GLRenderbuffer *rb = get_renderbuffer(g_framebuffers[fbo].color_attachments[attachment_index].object);
        return rb ? rb->internal_format : GL_RGBA8;
    }

    return GL_RGBA8;
}

static GX2ColorBuffer *get_default_color_buffer(void) {
    GX2ColorBuffer *preferred = g_default_framebuffer_uses_drc ? WHBGfxGetDRCColourBuffer()
                                                               : WHBGfxGetTVColourBuffer();
    if (preferred && preferred->surface.image && preferred->surface.width > 0 &&
        preferred->surface.height > 0) {
        return preferred;
    }
    preferred = g_default_framebuffer_uses_drc ? WHBGfxGetTVColourBuffer()
                                               : WHBGfxGetDRCColourBuffer();
    return preferred && preferred->surface.image ? preferred : NULL;
}

static GX2DepthBuffer *get_default_depth_buffer(void) {
    GX2DepthBuffer *preferred = g_default_framebuffer_uses_drc ? WHBGfxGetDRCDepthBuffer()
                                                               : WHBGfxGetTVDepthBuffer();
    if (preferred && preferred->surface.image && preferred->surface.width > 0 &&
        preferred->surface.height > 0) {
        return preferred;
    }
    preferred = g_default_framebuffer_uses_drc ? WHBGfxGetTVDepthBuffer()
                                               : WHBGfxGetDRCDepthBuffer();
    return preferred && preferred->surface.image ? preferred : NULL;
}

void gl_framebuffer_init(void) {
    memset(g_framebuffers, 0, sizeof(g_framebuffers));
    memset(g_renderbuffers, 0, sizeof(g_renderbuffers));
    g_default_framebuffer_uses_drc = false;
    init_framebuffer_object(&g_framebuffers[0], true);
}

void gl_framebuffer_shutdown(void) {
    for (uint32_t i = 1; i < MAX_FRAMEBUFFERS; ++i) {
        GLFramebuffer *fb = &g_framebuffers[i];
        if (!fb->reserved && !fb->in_use) continue;
        if (fb->in_use) {
            resolve_framebuffer_texture_targets(fb);
            for (uint32_t j = 0; j < 8; ++j) {
                free_framebuffer_texture_target(fb, j);
            }
        }
    }
    for (uint32_t i = 1; i < MAX_RENDERBUFFERS; ++i) {
        GLRenderbuffer *rb = &g_renderbuffers[i];
        if (!rb->reserved && !rb->in_use) continue;
        if (rb->in_use) {
            free_color_buffer_aux(&rb->color_buffer);
            free_surface_storage(&rb->surface);
        }
    }
    memset(g_framebuffers, 0, sizeof(g_framebuffers));
    memset(g_renderbuffers, 0, sizeof(g_renderbuffers));
    g_default_framebuffer_uses_drc = false;
}

#ifdef __cplusplus
extern "C" {
#endif

void gl_framebuffer_set_default_target_drc(GLboolean use_drc) {
    g_default_framebuffer_uses_drc = use_drc ? true : false;
    if (g_gl_context && g_gl_context->bound_framebuffer == 0)
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_GenFramebuffers(GLsizei n, GLuint *fbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !fbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    int count = 0;
    for (int i = 1; i < MAX_FRAMEBUFFERS && count < n; i++) {
        if (!g_framebuffers[i].reserved && !g_framebuffers[i].in_use) {
            reserve_framebuffer_name(&g_framebuffers[i]);
            fbs[count++] = i;
        }
    }
    if (count < n) {
        for (int i = count; i < n; ++i) fbs[i] = 0;
        _gl_set_error(GL_OUT_OF_MEMORY);
    }
}

void _gl_GenRenderbuffers(GLsizei n, GLuint *rbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !rbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    int count = 0;
    for (int i = 1; i < MAX_RENDERBUFFERS && count < n; i++) {
        if (!g_renderbuffers[i].reserved && !g_renderbuffers[i].in_use) {
            reserve_renderbuffer_name(&g_renderbuffers[i]);
            rbs[count++] = i;
        }
    }
    if (count < n) {
        for (int i = count; i < n; ++i) rbs[i] = 0;
        _gl_set_error(GL_OUT_OF_MEMORY);
    }
}

GLboolean _gl_IsRenderbuffer(GLuint rb) { return get_renderbuffer(rb) ? GL_TRUE : GL_FALSE; }
void _gl_DeleteRenderbuffers(GLsizei n, const GLuint *rbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !rbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        GLuint id = rbs[i];
        if (id > 0 && id < MAX_RENDERBUFFERS &&
            (g_renderbuffers[id].reserved || g_renderbuffers[id].in_use)) {
            if (g_gl_context->bound_renderbuffer == id) {
                g_gl_context->bound_renderbuffer = 0;
            }
            if (g_renderbuffers[id].in_use) {
                detach_renderbuffer_from_current_framebuffers(id);
                free_color_buffer_aux(&g_renderbuffers[id].color_buffer);
                free_surface_storage(&g_renderbuffers[id].surface);
            }
            memset(&g_renderbuffers[id], 0, sizeof(GLRenderbuffer));
        }
    }
}

GLboolean _gl_IsFramebuffer(GLuint fb) {
    return (fb > 0 && fb < MAX_FRAMEBUFFERS && g_framebuffers[fb].in_use)
               ? GL_TRUE
               : GL_FALSE;
}
void _gl_DeleteFramebuffers(GLsizei n, const GLuint *fbs) {
    if (!g_gl_context) return;
    if (n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !fbs) { _gl_set_error(GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        GLuint id = fbs[i];
        if (id > 0 && id < MAX_FRAMEBUFFERS &&
            (g_framebuffers[id].reserved || g_framebuffers[id].in_use)) {
            if (g_gl_context->bound_framebuffer == id) {
                g_gl_context->bound_framebuffer = 0;
                g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
            }
            if (g_gl_context->bound_read_framebuffer == id) {
                g_gl_context->bound_read_framebuffer = 0;
            }
            if (g_framebuffers[id].in_use) {
                resolve_framebuffer_texture_targets(&g_framebuffers[id]);
                for (uint32_t j = 0; j < 8; ++j) {
                    free_framebuffer_texture_target(&g_framebuffers[id], j);
                }
            }
            memset(&g_framebuffers[id], 0, sizeof(GLFramebuffer));
        }
    }
}

void _gl_BindFramebuffer(GLenum target, GLuint fb) {
    GLuint previous_draw_fb;
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (!framebuffer_name_is_valid(fb)) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (fb > 0 && !g_framebuffers[fb].in_use) {
        init_framebuffer_object(&g_framebuffers[fb], false);
    }
    previous_draw_fb = g_gl_context->bound_framebuffer;
    if ((target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) && previous_draw_fb != fb && previous_draw_fb < MAX_FRAMEBUFFERS)
        resolve_framebuffer_texture_targets(&g_framebuffers[previous_draw_fb]);
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) { g_gl_context->bound_framebuffer = fb; g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER; }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) { g_gl_context->bound_read_framebuffer = fb; }
}

void _gl_BindRenderbuffer(GLenum target, GLuint rb) {
    if (!g_gl_context) return;
    if (target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (!renderbuffer_name_is_valid(rb)) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (rb > 0 && !g_renderbuffers[rb].in_use) {
        init_renderbuffer_object(&g_renderbuffers[rb]);
    }
    g_gl_context->bound_renderbuffer = rb;
}

void _gl_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    GLenum texture_target;

    if (!g_gl_context) return;
    if (!is_framebuffer_target(target) ||
        (textarget != GL_TEXTURE_2D &&
         textarget != GL_TEXTURE_2D_MULTISAMPLE &&
         !is_cube_map_face_target(textarget))) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (level < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (texture != 0) {
        if (_gl_IsTexture(texture) != GL_TRUE) { _gl_set_error(GL_INVALID_OPERATION); return; }
        texture_target = gl_get_texture_target(texture);
        if ((textarget == GL_TEXTURE_2D && texture_target != GL_TEXTURE_2D) ||
            (textarget == GL_TEXTURE_2D_MULTISAMPLE &&
             texture_target != GL_TEXTURE_2D_MULTISAMPLE) ||
            (is_cube_map_face_target(textarget) && texture_target != GL_TEXTURE_CUBE_MAP)) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        if (level != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
    }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            set_texture_attachment_ref(&g_framebuffers[fbo].depth_attachment,
                                       texture, textarget, level, 0, GL_FALSE);
            set_texture_attachment_ref(&g_framebuffers[fbo].stencil_attachment,
                                       texture, textarget, level, 0, GL_FALSE);
        }
        g_framebuffers[fbo].dirty = true;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }
    GLAttachmentRef *ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (texture == 0) clear_attachment_ref(ref);
    else set_texture_attachment_ref(ref, texture, textarget, level, 0, GL_FALSE);
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7)
        free_framebuffer_texture_target(&g_framebuffers[fbo], attachment - GL_COLOR_ATTACHMENT0);
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    GLenum texture_target;
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context) return;
    if (!is_framebuffer_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (attachment != GL_DEPTH_STENCIL_ATTACHMENT &&
        !get_attachment_ref(&g_framebuffers[0], attachment)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (texture != 0) {
        if (_gl_IsTexture(texture) != GL_TRUE) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        if (layer < 0 ||
            layer >= GX2GL_MAX_FRAMEBUFFER_3D_TEXTURE_SIZE ||
            level > max_texture_level_for_size(GX2GL_MAX_FRAMEBUFFER_3D_TEXTURE_SIZE)) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        texture_target = gl_get_texture_target(texture);
        if (texture_target != GL_TEXTURE_3D &&
            texture_target != GL_TEXTURE_1D_ARRAY &&
            texture_target != GL_TEXTURE_2D_ARRAY &&
            texture_target != GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
    }

    fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    fb = &g_framebuffers[fbo];
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&fb->depth_attachment);
            clear_attachment_ref(&fb->stencil_attachment);
        } else {
            set_texture_attachment_ref(&fb->depth_attachment, texture,
                                       texture_target, level, layer, GL_FALSE);
            set_texture_attachment_ref(&fb->stencil_attachment, texture,
                                       texture_target, level, layer, GL_FALSE);
        }
        fb->dirty = true;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }

    {
        GLAttachmentRef *ref = get_attachment_ref(fb, attachment);
        if (!ref) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
        if (texture == 0) clear_attachment_ref(ref);
        else set_texture_attachment_ref(ref, texture, texture_target, level,
                                        layer, GL_FALSE);
    }

    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        free_framebuffer_texture_target(fb, attachment - GL_COLOR_ATTACHMENT0);
    }
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb) {
    if (!g_gl_context) return;
    if (!is_framebuffer_target(target) || rbtarget != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (rb != 0 && _gl_IsRenderbuffer(rb) != GL_TRUE) { _gl_set_error(GL_INVALID_OPERATION); return; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (rb == 0) {
            clear_attachment_ref(&g_framebuffers[fbo].depth_attachment);
            clear_attachment_ref(&g_framebuffers[fbo].stencil_attachment);
        } else {
            set_renderbuffer_attachment_ref(&g_framebuffers[fbo].depth_attachment, rb);
            set_renderbuffer_attachment_ref(&g_framebuffers[fbo].stencil_attachment, rb);
        }
        g_framebuffers[fbo].dirty = true;
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }
    GLAttachmentRef *ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (rb == 0) clear_attachment_ref(ref);
    else set_renderbuffer_attachment_ref(ref, rb);
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7)
        free_framebuffer_texture_target(&g_framebuffers[fbo], attachment - GL_COLOR_ATTACHMENT0);
    g_framebuffers[fbo].dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

static void renderbuffer_storage(GLenum target,
                                 GLsizei samples,
                                 GLenum internalformat,
                                 GLsizei width,
                                 GLsizei height) {
    GX2Surface new_surface;
    GX2ColorBuffer new_color_buffer;
    GX2DepthBuffer new_depth_buffer;
    GX2AAMode aa_mode = GX2_AA_MODE1X;
    GLsizei actual_samples = 0;

    if (!g_gl_context || target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (samples < 0 || samples > GX2GL_MAX_RENDERBUFFER_SAMPLES ||
        width < 0 || height < 0 ||
        width > GX2GL_MAX_RENDERBUFFER_SIZE ||
        height > GX2GL_MAX_RENDERBUFFER_SIZE) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    GLuint id = g_gl_context->bound_renderbuffer;
    if (id == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    GLRenderbuffer *rb = &g_renderbuffers[id];
    GX2SurfaceFormat fmt; GX2SurfaceUse use; bool is_depth;
    GX2RResourceFlags surface_flags;
    if (!get_renderbuffer_format_info(internalformat, &fmt, &use, &is_depth)) { _gl_set_error(GL_INVALID_ENUM); return; }

    if (samples > 0) {
        if (samples == 1) {
            aa_mode = GX2_AA_MODE1X;
            actual_samples = 1;
        } else if (samples == 2) {
            aa_mode = GX2_AA_MODE2X;
            actual_samples = 2;
        } else if (samples <= 4) {
            aa_mode = GX2_AA_MODE4X;
            actual_samples = 4;
        } else {
            aa_mode = GX2_AA_MODE8X;
            actual_samples = 8;
        }
    }
    {
        uint32_t numeric_type = ((uint32_t)fmt >> 8) & 0x7u;
        bool integer_format = numeric_type == 1u || numeric_type == 3u;
        if (integer_format && actual_samples > 1) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
    }

    memset(&new_surface, 0, sizeof(new_surface));
    memset(&new_color_buffer, 0, sizeof(new_color_buffer));
    memset(&new_depth_buffer, 0, sizeof(new_depth_buffer));

    if (width == 0 || height == 0) {
        free_color_buffer_aux(&rb->color_buffer);
        free_surface_storage(&rb->surface);
        memset(&rb->color_buffer, 0, sizeof(rb->color_buffer));
        memset(&rb->depth_buffer, 0, sizeof(rb->depth_buffer));
        rb->width = width;
        rb->height = height;
        rb->internal_format = internalformat;
        rb->is_depth = is_depth;
        rb->samples = actual_samples;
        rb->fixed_sample_locations = GL_TRUE;
        mark_framebuffers_for_renderbuffer(id);
        g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
        return;
    }

    new_surface.dim = GX2_SURFACE_DIM_TEXTURE_2D; new_surface.width = width; new_surface.height = height;
    new_surface.depth = 1; new_surface.mipLevels = 1; new_surface.format = fmt; new_surface.aa = aa_mode; new_surface.use = use;
    new_surface.tileMode = GX2_TILE_MODE_DEFAULT;
    GX2CalcSurfaceSizeAndAlignment(&new_surface);
    surface_flags = build_framebuffer_surface_flags(internalformat, is_depth, false);
    if (!GX2RCreateSurface(&new_surface, surface_flags)) {
        memset(&new_surface, 0, sizeof(new_surface));
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }
    if (new_surface.image && new_surface.imageSize)
        memset(new_surface.image, 0, new_surface.imageSize);
    if (new_surface.mipmaps && new_surface.mipmapSize)
        memset(new_surface.mipmaps, 0, new_surface.mipmapSize);
    if (is_depth) init_depth_buffer_from_surface(&new_depth_buffer, &new_surface);
    else if (!init_color_buffer_from_surface(&new_color_buffer, &new_surface)) {
        free_surface_storage(&new_surface);
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    free_color_buffer_aux(&rb->color_buffer);
    free_surface_storage(&rb->surface);
    rb->surface = new_surface;
    rb->color_buffer = new_color_buffer;
    rb->depth_buffer = new_depth_buffer;
    rb->width = width;
    rb->height = height;
    rb->internal_format = internalformat;
    rb->is_depth = is_depth;
    rb->samples = actual_samples;
    rb->fixed_sample_locations = GL_TRUE;
    mark_framebuffers_for_renderbuffer(id);
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

void _gl_RenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    renderbuffer_storage(target, 0, internalformat, width, height);
}

void _gl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels) {
    size_t tight_row_bytes;
    size_t tight_size;
    uint8_t *tight_pixels;
    uint8_t *dst_base;
    GLvoid *resolved_pixels = pixels;
    GLuint pack_buffer;
    GLintptr pack_buffer_offset = 0;
    GLsizeiptr pack_byte_count = 0;
    GLint pack_row_length;
    GLint pack_skip_rows;
    GLint pack_skip_pixels;
    GLint pack_alignment;
    size_t dst_row_pixels;
    size_t dst_row_bytes;
    size_t dst_stride;
    uint64_t required_size;

    if (!g_gl_context) return;
    pack_buffer = g_gl_context->bound_pixel_pack_buffer;
    if (width < 0 || height < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (!pixels && pack_buffer == 0) { _gl_set_error(GL_INVALID_OPERATION); return; }
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) { _gl_set_error(GL_INVALID_ENUM); return; }
    if (_gl_CheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        _gl_set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
        return;
    }
    if (g_gl_context->bound_read_framebuffer != 0 &&
        framebuffer_sample_count(g_gl_context->bound_read_framebuffer) > 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (width == 0 || height == 0) return;
    if (!get_read_color_buffer()) { _gl_set_error(GL_INVALID_OPERATION); return; }

    tight_row_bytes = (size_t)width * 4u;
    tight_size = tight_row_bytes * (size_t)height;
    tight_pixels = (uint8_t *)gl_mem_alloc(GL_MEM_TYPE_MEM2, tight_size, 64);
    if (!tight_pixels) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }

    if (!gl_read_color_pixels_rgba8(x, y, width, height, tight_pixels)) {
        gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    pack_row_length = g_gl_context->pack_row_length > 0 ? g_gl_context->pack_row_length : width;
    pack_skip_rows = g_gl_context->pack_skip_rows > 0 ? g_gl_context->pack_skip_rows : 0;
    pack_skip_pixels = g_gl_context->pack_skip_pixels > 0 ? g_gl_context->pack_skip_pixels : 0;
    pack_alignment = g_gl_context->pack_alignment > 0 ? g_gl_context->pack_alignment : 1;
    if (pack_row_length < width) {
        gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    dst_row_pixels = (size_t)pack_row_length;
    dst_row_bytes = dst_row_pixels * 4u;
    dst_stride = ((dst_row_bytes + (size_t)pack_alignment - 1u) / (size_t)pack_alignment) * (size_t)pack_alignment;
    required_size = (uint64_t)pack_skip_rows * dst_stride +
                    (uint64_t)pack_skip_pixels * 4u +
                    (uint64_t)(height - 1) * dst_stride +
                    (uint64_t)tight_row_bytes;
    if (required_size > (uint64_t)PTRDIFF_MAX) {
        gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (pack_buffer != 0) {
        pack_buffer_offset = (GLintptr)(uintptr_t)pixels;
        pack_byte_count = (GLsizeiptr)required_size;
        if (gl_buffer_get_write_range(pack_buffer, pack_buffer_offset,
                                      pack_byte_count,
                                      &resolved_pixels) != GL_TRUE) {
            gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
            return;
        }
    }
    dst_base = (uint8_t *)resolved_pixels +
               (size_t)pack_skip_rows * dst_stride +
               (size_t)pack_skip_pixels * 4u;

    for (GLsizei row = 0; row < height; ++row) {
        memcpy(dst_base + (size_t)row * dst_stride,
               tight_pixels + (size_t)row * tight_row_bytes,
               tight_row_bytes);
    }

    if (pack_buffer != 0 &&
        gl_buffer_flush_range(pack_buffer, pack_buffer_offset,
                              pack_byte_count) != GL_TRUE) {
        gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
        return;
    }

    gl_mem_free(GL_MEM_TYPE_MEM2, tight_pixels);
}

void _gl_FramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    GLuint fbo;
    GLenum texture_target = 0;
    GLboolean layered = GL_FALSE;

    if (!g_gl_context) return;
    if (!is_framebuffer_target(target)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (attachment != GL_DEPTH_STENCIL_ATTACHMENT &&
        !get_attachment_ref(&g_framebuffers[0], attachment)) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }

    if (texture != 0) {
        if (_gl_IsTexture(texture) != GL_TRUE) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        texture_target = gl_get_texture_target(texture);
        switch (texture_target) {
        case GL_TEXTURE_1D:
        case GL_TEXTURE_2D:
            break;
        case GL_TEXTURE_3D:
        case GL_TEXTURE_CUBE_MAP:
        case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
            layered = GL_TRUE;
            break;
        case GL_TEXTURE_2D_MULTISAMPLE:
            if (level != 0) {
                _gl_set_error(GL_INVALID_VALUE);
                return;
            }
            break;
        default:
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        if (level > max_texture_level_for_size(
                        texture_target == GL_TEXTURE_3D
                            ? GX2GL_MAX_FRAMEBUFFER_3D_TEXTURE_SIZE
                            : GX2GL_MAX_RENDERBUFFER_SIZE)) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
        if (texture_target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY && level != 0) {
            _gl_set_error(GL_INVALID_VALUE);
            return;
        }
    }

    fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }

    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        if (texture == 0) {
            clear_attachment_ref(&fb->depth_attachment);
            clear_attachment_ref(&fb->stencil_attachment);
        } else {
            set_texture_attachment_ref(&fb->depth_attachment, texture,
                                       texture_target, level, 0, layered);
            set_texture_attachment_ref(&fb->stencil_attachment, texture,
                                       texture_target, level, 0, layered);
        }
    } else {
        GLAttachmentRef *ref = get_attachment_ref(fb, attachment);
        if (texture == 0) {
            clear_attachment_ref(ref);
        } else {
            set_texture_attachment_ref(ref, texture, texture_target, level, 0,
                                       layered);
        }
    }

    if (attachment >= GL_COLOR_ATTACHMENT0 &&
        attachment <= GL_COLOR_ATTACHMENT7) {
        free_framebuffer_texture_target(fb,
                                        attachment - GL_COLOR_ATTACHMENT0);
    }
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}

static GLint attachment_internal_format(const GLAttachmentRef *attachment) {
    if (!attachment_ref_present(attachment)) return 0;
    if (attachment->kind == GL_ATTACHMENT_KIND_TEXTURE) {
        return gl_get_texture_internal_format(attachment->object);
    }
    if (attachment->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        GLRenderbuffer *renderbuffer = get_renderbuffer(attachment->object);
        return renderbuffer ? renderbuffer->internal_format : 0;
    }
    return 0;
}

static bool attachment_surface_view(const GLAttachmentRef *attachment,
                                    GX2Surface *view) {
    if (!attachment_ref_present(attachment) || !view) return false;

    if (attachment->kind == GL_ATTACHMENT_KIND_TEXTURE) {
        GX2Texture *texture = gl_get_gx2_texture(attachment->object);
        return texture && build_texture_attachment_read_surface(
                              attachment, &texture->surface, view);
    }

    if (attachment->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        GLRenderbuffer *renderbuffer = get_renderbuffer(attachment->object);
        if (!renderbuffer || !renderbuffer->surface.image) return false;
        *view = renderbuffer->surface;
        return true;
    }

    return false;
}

static bool get_read_color_surface(GX2Surface *view, GLint *internal_format) {
    GLuint framebuffer;
    uint32_t attachment_index;
    GX2ColorBuffer *default_buffer;

    if (!g_gl_context || !view || !internal_format) return false;
    framebuffer = g_gl_context->bound_read_framebuffer;
    if (framebuffer == 0) {
        if (g_framebuffers[0].read_buffer == GL_NONE) return false;
        default_buffer = get_default_color_buffer();
        if (!default_buffer || !default_buffer->surface.image) return false;
        *view = default_buffer->surface;
        *internal_format = GL_RGBA8;
        return true;
    }

    if (framebuffer >= MAX_FRAMEBUFFERS || !g_framebuffers[framebuffer].in_use ||
        g_framebuffers[framebuffer].read_buffer == GL_NONE ||
        !get_color_attachment_index(g_framebuffers[framebuffer].read_buffer,
                                    &attachment_index)) {
        return false;
    }

    (void)get_read_color_buffer();
    *internal_format = attachment_internal_format(
        &g_framebuffers[framebuffer].color_attachments[attachment_index]);
    return *internal_format != 0 && attachment_surface_view(
        &g_framebuffers[framebuffer].color_attachments[attachment_index], view);
}

static bool get_draw_color_surface(uint32_t draw_buffer,
                                   GX2Surface *view,
                                   GLint *internal_format) {
    GLuint framebuffer;
    uint32_t attachment_index;
    GX2ColorBuffer *default_buffer;

    if (!g_gl_context || !view || !internal_format || draw_buffer >= 8) {
        return false;
    }

    framebuffer = g_gl_context->bound_framebuffer;
    if (framebuffer == 0) {
        if (draw_buffer != 0 || g_framebuffers[0].draw_buffers[0] == GL_NONE) {
            return false;
        }
        default_buffer = get_default_color_buffer();
        if (!default_buffer || !default_buffer->surface.image) return false;
        *view = default_buffer->surface;
        *internal_format = GL_RGBA8;
        return true;
    }

    if (framebuffer >= MAX_FRAMEBUFFERS || !g_framebuffers[framebuffer].in_use ||
        !get_color_attachment_index(
            g_framebuffers[framebuffer].draw_buffers[draw_buffer],
            &attachment_index)) {
        return false;
    }

    *internal_format = attachment_internal_format(
        &g_framebuffers[framebuffer].color_attachments[attachment_index]);
    return *internal_format != 0 && attachment_surface_view(
        &g_framebuffers[framebuffer].color_attachments[attachment_index], view);
}

static GX2Surface *get_draw_color_storage(uint32_t draw_buffer,
                                          uint32_t *mip,
                                          uint32_t *slice) {
    GLuint framebuffer;
    uint32_t attachment_index;
    GLAttachmentRef *attachment;

    if (!g_gl_context || !mip || !slice || draw_buffer >= 8) return NULL;
    *mip = 0;
    *slice = 0;
    framebuffer = g_gl_context->bound_framebuffer;
    if (framebuffer == 0) {
        GX2ColorBuffer *buffer = get_default_color_buffer();
        return buffer ? &buffer->surface : NULL;
    }
    if (framebuffer >= MAX_FRAMEBUFFERS ||
        !get_color_attachment_index(
            g_framebuffers[framebuffer].draw_buffers[draw_buffer],
            &attachment_index)) {
        return NULL;
    }

    attachment = &g_framebuffers[framebuffer].color_attachments[attachment_index];
    if (attachment->kind == GL_ATTACHMENT_KIND_TEXTURE) {
        GX2Texture *texture = gl_get_gx2_texture(attachment->object);
        if (!texture) return NULL;
        *mip = (uint32_t)attachment->level;
        *slice = attachment_view_slice(attachment);
        return &texture->surface;
    }
    if (attachment->kind == GL_ATTACHMENT_KIND_RENDERBUFFER) {
        GLRenderbuffer *renderbuffer = get_renderbuffer(attachment->object);
        return renderbuffer ? &renderbuffer->surface : NULL;
    }
    return NULL;
}

static bool get_depth_stencil_surface(bool read,
                                      GLbitfield aspect,
                                      GX2Surface *view,
                                      GLint *internal_format) {
    GLuint framebuffer;
    GLFramebuffer *fb;
    const GLAttachmentRef *attachment;
    GX2DepthBuffer *default_buffer;

    if (!g_gl_context || !view || !internal_format) return false;
    framebuffer = read ? g_gl_context->bound_read_framebuffer
                       : g_gl_context->bound_framebuffer;
    if (framebuffer == 0) {
        default_buffer = get_default_depth_buffer();
        if (!default_buffer || !default_buffer->surface.image) return false;
        *view = default_buffer->surface;
        *internal_format = view->format == GX2_SURFACE_FORMAT_UNORM_R24_X8
                               ? GL_DEPTH24_STENCIL8
                               : GL_DEPTH_COMPONENT32F;
        return aspect != GL_STENCIL_BUFFER_BIT ||
               *internal_format == GL_DEPTH24_STENCIL8;
    }

    if (framebuffer >= MAX_FRAMEBUFFERS || !g_framebuffers[framebuffer].in_use) {
        return false;
    }

    if (read) {
        (void)get_read_color_buffer();
    } else {
        gl_bind_framebuffers();
    }

    fb = &g_framebuffers[framebuffer];
    attachment = aspect == GL_STENCIL_BUFFER_BIT
                     ? &fb->stencil_attachment
                     : &fb->depth_attachment;
    if (!attachment_ref_present(attachment)) return false;
    *internal_format = attachment_internal_format(attachment);
    if (*internal_format == 0 || !attachment_surface_view(attachment, view)) {
        return false;
    }
    if (view->format == GX2_SURFACE_FORMAT_UNORM_R24_X8) {
        *internal_format = GL_DEPTH24_STENCIL8;
    } else if (view->format == GX2_SURFACE_FORMAT_FLOAT_R32) {
        *internal_format = GL_DEPTH_COMPONENT32F;
    }
    return true;
}

static uint16_t encode_half_float_bits(float value) {
    uint32_t word;
    uint32_t sign;
    int32_t exponent;
    uint32_t mantissa;

    memcpy(&word, &value, sizeof(word));
    sign = (word >> 16) & 0x8000u;
    exponent = (int32_t)((word >> 23) & 0xFFu) - 127 + 15;
    mantissa = word & 0x7FFFFFu;

    if (((word >> 23) & 0xFFu) == 0xFFu) {
        if (mantissa != 0) return (uint16_t)(sign | 0x7E00u);
        return (uint16_t)(sign | 0x7C00u);
    }
    if (exponent <= 0) {
        if (exponent < -10) return (uint16_t)sign;
        mantissa |= 0x800000u;
        mantissa = mantissa >> (uint32_t)(1 - exponent);
        if (mantissa & 0x00001000u) mantissa += 0x00002000u;
        return (uint16_t)(sign | (mantissa >> 13));
    }
    if (exponent >= 31) {
        return (uint16_t)(sign | 0x7C00u);
    }
    if (mantissa & 0x00001000u) {
        mantissa += 0x00002000u;
        if (mantissa & 0x00800000u) {
            mantissa = 0;
            ++exponent;
            if (exponent >= 31) return (uint16_t)(sign | 0x7C00u);
        }
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
}

static void encode_gpu_half_float(uint8_t *ptr, float value) {
    uint16_t word = CPU_TO_GPU_16(encode_half_float_bits(value));
    memcpy(ptr, &word, sizeof(word));
}

static float clamp_unit_float(float value) {
    if (value <= 0.0f) return 0.0f;
    if (value >= 1.0f) return 1.0f;
    return value;
}

static uint32_t unorm_bits(float value, uint32_t maximum) {
    return (uint32_t)(clamp_unit_float(value) * (float)maximum + 0.5f);
}

static uint32_t surface_bytes_per_pixel(GX2SurfaceFormat format) {
    switch (format) {
    case GX2_SURFACE_FORMAT_UNORM_R8: return 1;
    case GX2_SURFACE_FORMAT_UNORM_R8_G8:
    case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5:
    case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4:
    case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1: return 2;
    case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8:
    case GX2_SURFACE_FORMAT_FLOAT_R32:
    case GX2_SURFACE_FORMAT_UNORM_R24_X8: return 4;
    case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16: return 8;
    case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32: return 16;
    default: return 0;
    }
}

static uint8_t *surface_texel(GX2Surface *surface, GLint x, GLint y) {
    uint32_t bytes = surface_bytes_per_pixel(surface->format);
    uint32_t surface_y = surface->height - 1u - (uint32_t)y;
    return (uint8_t *)surface->image +
           (((size_t)surface_y * (size_t)surface->pitch + (size_t)x) * bytes);
}

static const uint8_t *surface_texel_const(const GX2Surface *surface,
                                          GLint x, GLint y) {
    return surface_texel((GX2Surface *)surface, x, y);
}

static void read_color_texel(const GX2Surface *surface, GLint internal_format,
                             GLint x, GLint y, float value[4]) {
    const uint8_t *texel = surface_texel_const(surface, x, y);
    value[0] = value[1] = value[2] = 0.0f;
    value[3] = 1.0f;

    switch (surface->format) {
    case GX2_SURFACE_FORMAT_UNORM_R8:
        if (internal_format == GL_ALPHA) {
            value[3] = (float)texel[0] / 255.0f;
        } else if (internal_format == GL_LUMINANCE) {
            value[0] = value[1] = value[2] = (float)texel[0] / 255.0f;
        } else {
            value[0] = (float)texel[0] / 255.0f;
        }
        break;
    case GX2_SURFACE_FORMAT_UNORM_R8_G8:
        if (internal_format == GL_LUMINANCE_ALPHA) {
            value[0] = value[1] = value[2] = (float)texel[0] / 255.0f;
            value[3] = (float)texel[1] / 255.0f;
        } else {
            value[0] = (float)texel[0] / 255.0f;
            value[1] = (float)texel[1] / 255.0f;
        }
        break;
    case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5: {
        uint16_t word;
        memcpy(&word, texel, sizeof(word));
        word = GPU_TO_CPU_16(word);
        value[0] = (float)((word >> 11) & 31u) / 31.0f;
        value[1] = (float)((word >> 5) & 63u) / 63.0f;
        value[2] = (float)(word & 31u) / 31.0f;
        break;
    }
    case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4: {
        uint16_t word;
        memcpy(&word, texel, sizeof(word));
        word = GPU_TO_CPU_16(word);
        value[0] = (float)((word >> 12) & 15u) / 15.0f;
        value[1] = (float)((word >> 8) & 15u) / 15.0f;
        value[2] = (float)((word >> 4) & 15u) / 15.0f;
        value[3] = (float)(word & 15u) / 15.0f;
        break;
    }
    case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1: {
        uint16_t word;
        memcpy(&word, texel, sizeof(word));
        word = GPU_TO_CPU_16(word);
        value[0] = (float)((word >> 11) & 31u) / 31.0f;
        value[1] = (float)((word >> 6) & 31u) / 31.0f;
        value[2] = (float)((word >> 1) & 31u) / 31.0f;
        value[3] = (float)(word & 1u);
        break;
    }
    case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8: {
        uint32_t word;
        memcpy(&word, texel, sizeof(word));
        word = GPU_TO_CPU_32(word);
        value[0] = (float)((word >> 24) & 255u) / 255.0f;
        value[1] = (float)((word >> 16) & 255u) / 255.0f;
        value[2] = (float)((word >> 8) & 255u) / 255.0f;
        value[3] = (float)(word & 255u) / 255.0f;
        break;
    }
    case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16:
        value[0] = decode_gpu_half_float(texel + 0);
        value[1] = decode_gpu_half_float(texel + 2);
        value[2] = decode_gpu_half_float(texel + 4);
        value[3] = decode_gpu_half_float(texel + 6);
        break;
    case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32:
        value[0] = decode_gpu_float32(texel + 0);
        value[1] = decode_gpu_float32(texel + 4);
        value[2] = decode_gpu_float32(texel + 8);
        value[3] = decode_gpu_float32(texel + 12);
        break;
    default:
        break;
    }
}

static void write_color_texel(GX2Surface *surface, GLint internal_format,
                              GLint x, GLint y, const float value[4]) {
    uint8_t *texel = surface_texel(surface, x, y);
    switch (surface->format) {
    case GX2_SURFACE_FORMAT_UNORM_R8:
        texel[0] = (uint8_t)unorm_bits(
            internal_format == GL_ALPHA ? value[3] : value[0], 255u);
        break;
    case GX2_SURFACE_FORMAT_UNORM_R8_G8:
        texel[0] = (uint8_t)unorm_bits(value[0], 255u);
        texel[1] = (uint8_t)unorm_bits(
            internal_format == GL_LUMINANCE_ALPHA ? value[3] : value[1], 255u);
        break;
    case GX2_SURFACE_FORMAT_UNORM_R5_G6_B5: {
        uint16_t word = (uint16_t)((unorm_bits(value[0], 31u) << 11) |
                                   (unorm_bits(value[1], 63u) << 5) |
                                   unorm_bits(value[2], 31u));
        word = CPU_TO_GPU_16(word);
        memcpy(texel, &word, sizeof(word));
        break;
    }
    case GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4: {
        uint16_t word = (uint16_t)((unorm_bits(value[0], 15u) << 12) |
                                   (unorm_bits(value[1], 15u) << 8) |
                                   (unorm_bits(value[2], 15u) << 4) |
                                   unorm_bits(value[3], 15u));
        word = CPU_TO_GPU_16(word);
        memcpy(texel, &word, sizeof(word));
        break;
    }
    case GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1: {
        uint16_t word = (uint16_t)((unorm_bits(value[0], 31u) << 11) |
                                   (unorm_bits(value[1], 31u) << 6) |
                                   (unorm_bits(value[2], 31u) << 1) |
                                   unorm_bits(value[3], 1u));
        word = CPU_TO_GPU_16(word);
        memcpy(texel, &word, sizeof(word));
        break;
    }
    case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8: {
        uint32_t word = (unorm_bits(value[0], 255u) << 24) |
                        (unorm_bits(value[1], 255u) << 16) |
                        (unorm_bits(value[2], 255u) << 8) |
                        unorm_bits(value[3], 255u);
        word = CPU_TO_GPU_32(word);
        memcpy(texel, &word, sizeof(word));
        break;
    }
    case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16:
        encode_gpu_half_float(texel + 0, value[0]);
        encode_gpu_half_float(texel + 2, value[1]);
        encode_gpu_half_float(texel + 4, value[2]);
        encode_gpu_half_float(texel + 6, value[3]);
        break;
    case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32:
        encode_gpu_float32(texel + 0, value[0]);
        encode_gpu_float32(texel + 4, value[1]);
        encode_gpu_float32(texel + 8, value[2]);
        encode_gpu_float32(texel + 12, value[3]);
        break;
    default:
        break;
    }
}

static GLint clamp_texel_coordinate(GLint value, uint32_t extent) {
    if (value < 0) return 0;
    if ((uint32_t)value >= extent) return (GLint)extent - 1;
    return value;
}

static double mapped_source_coordinate(GLint destination,
                                       GLint destination0,
                                       GLint destination1,
                                       GLint source0,
                                       GLint source1) {
    double fraction = ((double)destination + 0.5 - (double)destination0) /
                      ((double)destination1 - (double)destination0);
    return (double)source0 + fraction * ((double)source1 - (double)source0);
}

static void clipped_destination_bounds(GLint destination0, GLint destination1,
                                       uint32_t extent, GLint *first,
                                       GLint *last) {
    int64_t low = destination0 < destination1 ? destination0 : destination1;
    int64_t high = destination0 < destination1 ? destination1 : destination0;
    if (low < 0) low = 0;
    if (high > (int64_t)extent) high = extent;
    if (high < low) high = low;
    *first = (GLint)low;
    *last = (GLint)high;
}

static bool blit_color_surface(const GX2Surface *source,
                               GLint source_internal_format,
                               GX2Surface *destination,
                               GLint destination_internal_format,
                               GLint srcX0, GLint srcY0,
                               GLint srcX1, GLint srcY1,
                               GLint dstX0, GLint dstY0,
                               GLint dstX1, GLint dstY1,
                               GLenum filter) {
    GX2Surface staged_source;
    GX2Surface staged_destination;
    GLint first_x, last_x, first_y, last_y;
    bool success = false;

    memset(&staged_source, 0, sizeof(staged_source));
    memset(&staged_destination, 0, sizeof(staged_destination));
    if (!stage_surface_for_cpu_access(source, source_internal_format, false,
                                      &staged_source) ||
        !stage_surface_for_cpu_access(destination, destination_internal_format,
                                      false, &staged_destination)) {
        goto cleanup;
    }

    clipped_destination_bounds(dstX0, dstX1, staged_destination.width,
                               &first_x, &last_x);
    clipped_destination_bounds(dstY0, dstY1, staged_destination.height,
                               &first_y, &last_y);

    for (GLint y = first_y; y < last_y; ++y) {
        double source_center_y = mapped_source_coordinate(
            y, dstY0, dstY1, srcY0, srcY1);
        if (source_center_y < 0.0 ||
            source_center_y >= (double)staged_source.height) continue;

        for (GLint x = first_x; x < last_x; ++x) {
            double source_center_x = mapped_source_coordinate(
                x, dstX0, dstX1, srcX0, srcX1);
            float value[4];

            if (source_center_x < 0.0 ||
                source_center_x >= (double)staged_source.width) continue;

            if (filter == GL_NEAREST) {
                GLint source_x = (GLint)floor(source_center_x);
                GLint source_y = (GLint)floor(source_center_y);
                if (staged_source.format == staged_destination.format &&
                    source_internal_format == destination_internal_format) {
                    uint32_t bytes = surface_bytes_per_pixel(staged_source.format);
                    memcpy(surface_texel(&staged_destination, x, y),
                           surface_texel_const(&staged_source, source_x, source_y),
                           bytes);
                } else {
                    read_color_texel(&staged_source, source_internal_format,
                                     source_x, source_y, value);
                    write_color_texel(&staged_destination,
                                      destination_internal_format, x, y, value);
                }
            } else {
                double sample_x = source_center_x - 0.5;
                double sample_y = source_center_y - 0.5;
                GLint x0 = (GLint)floor(sample_x);
                GLint y0 = (GLint)floor(sample_y);
                GLint x1 = x0 + 1;
                GLint y1 = y0 + 1;
                float tx = (float)(sample_x - floor(sample_x));
                float ty = (float)(sample_y - floor(sample_y));
                float p00[4], p10[4], p01[4], p11[4];

                x0 = clamp_texel_coordinate(x0, staged_source.width);
                x1 = clamp_texel_coordinate(x1, staged_source.width);
                y0 = clamp_texel_coordinate(y0, staged_source.height);
                y1 = clamp_texel_coordinate(y1, staged_source.height);
                read_color_texel(&staged_source, source_internal_format, x0, y0, p00);
                read_color_texel(&staged_source, source_internal_format, x1, y0, p10);
                read_color_texel(&staged_source, source_internal_format, x0, y1, p01);
                read_color_texel(&staged_source, source_internal_format, x1, y1, p11);
                for (uint32_t component = 0; component < 4; ++component) {
                    float top = p00[component] +
                                (p10[component] - p00[component]) * tx;
                    float bottom = p01[component] +
                                   (p11[component] - p01[component]) * tx;
                    value[component] = top + (bottom - top) * ty;
                }
                write_color_texel(&staged_destination,
                                  destination_internal_format, x, y, value);
            }
        }
    }

    success = upload_cpu_staging_surface(&staged_destination, destination, false);

cleanup:
    free_surface_storage(&staged_source);
    free_surface_storage(&staged_destination);
    return success;
}

static bool blit_depth_stencil_surface(const GX2Surface *source,
                                       GX2Surface *destination,
                                       GLint internal_format,
                                       GLbitfield aspects,
                                       GLint srcX0, GLint srcY0,
                                       GLint srcX1, GLint srcY1,
                                       GLint dstX0, GLint dstY0,
                                       GLint dstX1, GLint dstY1) {
    GX2Surface staged_source;
    GX2Surface staged_destination;
    GLint first_x, last_x, first_y, last_y;
    bool success = false;

    memset(&staged_source, 0, sizeof(staged_source));
    memset(&staged_destination, 0, sizeof(staged_destination));
    if (!stage_surface_for_cpu_access(source, internal_format, true,
                                      &staged_source) ||
        !stage_surface_for_cpu_access(destination, internal_format, true,
                                      &staged_destination)) {
        goto cleanup;
    }

    clipped_destination_bounds(dstX0, dstX1, staged_destination.width,
                               &first_x, &last_x);
    clipped_destination_bounds(dstY0, dstY1, staged_destination.height,
                               &first_y, &last_y);

    for (GLint y = first_y; y < last_y; ++y) {
        double source_center_y = mapped_source_coordinate(
            y, dstY0, dstY1, srcY0, srcY1);
        GLint source_y = (GLint)floor(source_center_y);
        if (source_y < 0 || source_y >= (GLint)staged_source.height) continue;

        for (GLint x = first_x; x < last_x; ++x) {
            double source_center_x = mapped_source_coordinate(
                x, dstX0, dstX1, srcX0, srcX1);
            GLint source_x = (GLint)floor(source_center_x);
            const uint8_t *source_texel;
            uint8_t *destination_texel;

            if (source_x < 0 || source_x >= (GLint)staged_source.width) continue;
            source_texel = surface_texel_const(&staged_source, source_x, source_y);
            destination_texel = surface_texel(&staged_destination, x, y);

            if (internal_format == GL_DEPTH24_STENCIL8) {
                uint32_t source_word;
                uint32_t destination_word;
                const uint32_t depth_mask = 0xFFFFFF00u;
                const uint32_t stencil_mask = 0x000000FFu;
                uint32_t copy_mask = 0;
                memcpy(&source_word, source_texel, sizeof(source_word));
                memcpy(&destination_word, destination_texel,
                       sizeof(destination_word));
                source_word = GPU_TO_CPU_32(source_word);
                destination_word = GPU_TO_CPU_32(destination_word);
                if (aspects & GL_DEPTH_BUFFER_BIT) copy_mask |= depth_mask;
                if (aspects & GL_STENCIL_BUFFER_BIT) copy_mask |= stencil_mask;
                destination_word = (destination_word & ~copy_mask) |
                                   (source_word & copy_mask);
                destination_word = CPU_TO_GPU_32(destination_word);
                memcpy(destination_texel, &destination_word,
                       sizeof(destination_word));
            } else if (aspects & GL_DEPTH_BUFFER_BIT) {
                memcpy(destination_texel, source_texel, 4u);
            }
        }
    }

    success = upload_cpu_staging_surface(&staged_destination, destination, true);

cleanup:
    free_surface_storage(&staged_source);
    free_surface_storage(&staged_destination);
    return success;
}

static bool blit_sample_rules_match(const GX2Surface *source,
                                    const GX2Surface *destination,
                                    GLint srcX0, GLint srcY0,
                                    GLint srcX1, GLint srcY1,
                                    GLint dstX0, GLint dstY0,
                                    GLint dstX1, GLint dstY1) {
    bool source_multisampled = source->aa != GX2_AA_MODE1X;
    bool destination_multisampled = destination->aa != GX2_AA_MODE1X;
    int64_t source_width = (int64_t)srcX1 - srcX0;
    int64_t source_height = (int64_t)srcY1 - srcY0;
    int64_t destination_width = (int64_t)dstX1 - dstX0;
    int64_t destination_height = (int64_t)dstY1 - dstY0;
    if (source_width < 0) source_width = -source_width;
    if (source_height < 0) source_height = -source_height;
    if (destination_width < 0) destination_width = -destination_width;
    if (destination_height < 0) destination_height = -destination_height;
    if (source_multisampled && destination_multisampled &&
        source->aa != destination->aa) return false;
    if ((source_multisampled || destination_multisampled) &&
        (source_width != destination_width ||
         source_height != destination_height)) return false;
    return true;
}

void _gl_BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                         GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                         GLbitfield mask, GLenum filter) {
    GX2Surface source_color;
    GX2Surface destination_colors[8];
    GLint source_color_format = 0;
    GLint destination_color_formats[8] = {0};
    bool destination_color_active[8] = {false};
    GX2Surface source_depth;
    GX2Surface destination_depth;
    GX2Surface source_stencil;
    GX2Surface destination_stencil;
    GLint source_depth_format = 0;
    GLint destination_depth_format = 0;
    GLint source_stencil_format = 0;
    GLint destination_stencil_format = 0;
    bool copy_depth = false;
    bool copy_stencil = false;
    bool color_blit_completed = false;

    if (!g_gl_context) return;
    if (filter != GL_NEAREST && filter != GL_LINEAR) {
        _gl_set_error(GL_INVALID_ENUM);
        return;
    }
    if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                 GL_STENCIL_BUFFER_BIT)) {
        _gl_set_error(GL_INVALID_VALUE);
        return;
    }
    if (filter != GL_NEAREST &&
        (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    if (_gl_CheckFramebufferStatus(GL_READ_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE ||
        _gl_CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
        _gl_set_error(GL_INVALID_FRAMEBUFFER_OPERATION);
        return;
    }

    gl_bind_framebuffers();

    if ((mask & GL_COLOR_BUFFER_BIT) &&
        get_read_color_surface(&source_color, &source_color_format)) {
        if (filter == GL_LINEAR && surface_bytes_per_pixel(source_color.format) == 0) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        for (uint32_t index = 0; index < 8; ++index) {
            destination_color_active[index] = get_draw_color_surface(
                index, &destination_colors[index],
                &destination_color_formats[index]);
            if (!destination_color_active[index]) continue;
            if (surface_bytes_per_pixel(source_color.format) == 0 ||
                surface_bytes_per_pixel(destination_colors[index].format) == 0 ||
                !blit_sample_rules_match(&source_color,
                                         &destination_colors[index],
                                         srcX0, srcY0, srcX1, srcY1,
                                         dstX0, dstY0, dstX1, dstY1)) {
                _gl_set_error(GL_INVALID_OPERATION);
                return;
            }
        }
    }

    if (mask & GL_DEPTH_BUFFER_BIT) {
        copy_depth = get_depth_stencil_surface(
                         true, GL_DEPTH_BUFFER_BIT, &source_depth,
                         &source_depth_format) &&
                     get_depth_stencil_surface(
                         false, GL_DEPTH_BUFFER_BIT, &destination_depth,
                         &destination_depth_format);
        if (copy_depth &&
            (source_depth_format != destination_depth_format ||
             source_depth.format != destination_depth.format ||
             !blit_sample_rules_match(&source_depth, &destination_depth,
                                      srcX0, srcY0, srcX1, srcY1,
                                      dstX0, dstY0, dstX1, dstY1))) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
    }

    if (mask & GL_STENCIL_BUFFER_BIT) {
        copy_stencil = get_depth_stencil_surface(
                           true, GL_STENCIL_BUFFER_BIT, &source_stencil,
                           &source_stencil_format) &&
                       get_depth_stencil_surface(
                           false, GL_STENCIL_BUFFER_BIT, &destination_stencil,
                           &destination_stencil_format);
        if (copy_stencil &&
            (source_stencil_format != destination_stencil_format ||
             source_stencil.format != destination_stencil.format ||
             !blit_sample_rules_match(&source_stencil, &destination_stencil,
                                      srcX0, srcY0, srcX1, srcY1,
                                      dstX0, dstY0, dstX1, dstY1))) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
    }

    if (srcX0 == srcX1 || srcY0 == srcY1 ||
        dstX0 == dstX1 || dstY0 == dstY1 || mask == 0) return;

    if ((mask & GL_COLOR_BUFFER_BIT) && source_color_format != 0 &&
        source_color.aa != GX2_AA_MODE1X) {
        GX2ColorBuffer *source_buffer = get_read_color_buffer();
        if (!source_buffer || srcX0 != 0 || srcY0 != 0 ||
            srcX1 != (GLint)source_color.width ||
            srcY1 != (GLint)source_color.height) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        for (uint32_t index = 0; index < 8; ++index) {
            if (!destination_color_active[index]) continue;
            if (destination_colors[index].aa != GX2_AA_MODE1X ||
                source_color.format != destination_colors[index].format ||
                dstX0 != 0 || dstY0 != 0 ||
                dstX1 != (GLint)destination_colors[index].width ||
                dstY1 != (GLint)destination_colors[index].height) {
                _gl_set_error(GL_INVALID_OPERATION);
                return;
            }
        }
        for (uint32_t index = 0; index < 8; ++index) {
            if (!destination_color_active[index]) continue;
            uint32_t destination_mip;
            uint32_t destination_slice;
            GX2Surface *destination_storage = get_draw_color_storage(
                index, &destination_mip, &destination_slice);
            if (!destination_storage) {
                _gl_set_error(GL_INVALID_OPERATION);
                return;
            }
            GX2ResolveAAColorBuffer(source_buffer, destination_storage,
                                    destination_mip, destination_slice);
            GX2DrawDone();
            restore_context_after_surface_op();
            invalidate_surface_after_color_write(destination_storage);
            gl_framebuffer_mark_bound_color_buffer_dirty(index);
        }
        color_blit_completed = true;
    }

    if ((mask & GL_COLOR_BUFFER_BIT) && source_color_format != 0 &&
        !color_blit_completed) {
        for (uint32_t index = 0; index < 8; ++index) {
            if (destination_color_active[index] &&
                destination_colors[index].aa != GX2_AA_MODE1X) {
                _gl_set_error(GL_INVALID_OPERATION);
                return;
            }
        }
    }

    if ((mask & GL_COLOR_BUFFER_BIT) && source_color_format != 0 &&
        !color_blit_completed) {
        for (uint32_t index = 0; index < 8; ++index) {
            if (!destination_color_active[index]) continue;
            if (!blit_color_surface(&source_color, source_color_format,
                                    &destination_colors[index],
                                    destination_color_formats[index],
                                    srcX0, srcY0, srcX1, srcY1,
                                    dstX0, dstY0, dstX1, dstY1, filter)) {
                _gl_set_error(GL_OUT_OF_MEMORY);
                return;
            }
        }
    }

    if (copy_depth && copy_stencil &&
        source_depth.image == source_stencil.image &&
        destination_depth.image == destination_stencil.image) {
        if (!blit_depth_stencil_surface(
                &source_depth, &destination_depth, source_depth_format,
                GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                srcX0, srcY0, srcX1, srcY1,
                dstX0, dstY0, dstX1, dstY1)) {
            _gl_set_error(GL_OUT_OF_MEMORY);
        }
        return;
    }

    if (copy_depth && !blit_depth_stencil_surface(
                          &source_depth, &destination_depth,
                          source_depth_format, GL_DEPTH_BUFFER_BIT,
                          srcX0, srcY0, srcX1, srcY1,
                          dstX0, dstY0, dstX1, dstY1)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
        return;
    }
    if (copy_stencil && !blit_depth_stencil_surface(
                            &source_stencil, &destination_stencil,
                            source_stencil_format, GL_STENCIL_BUFFER_BIT,
                            srcX0, srcY0, srcX1, srcY1,
                            dstX0, dstY0, dstX1, dstY1)) {
        _gl_set_error(GL_OUT_OF_MEMORY);
    }
}

void _gl_RenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {
    renderbuffer_storage(target, samples, internalformat, width, height);
}

void _gl_DrawBuffer(GLenum buf) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_framebuffer;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (fbo == 0) {
        if (buf != GL_NONE && buf != GL_BACK) {
            _gl_set_error(is_default_framebuffer_color_name(buf) ||
                          is_color_attachment_name(buf, NULL)
                              ? GL_INVALID_OPERATION
                              : GL_INVALID_ENUM);
            return;
        }
    } else if (buf != GL_NONE && !get_color_attachment_index(buf, NULL)) {
        if (is_default_framebuffer_color_name(buf)) {
            _gl_set_error(GL_INVALID_OPERATION);
        } else if (is_color_attachment_name(buf, NULL)) {
            _gl_set_error(GL_INVALID_VALUE);
        } else {
            _gl_set_error(GL_INVALID_ENUM);
        }
        return;
    }
    fb->draw_buffers[0] = buf;
    for (uint32_t i = 1; i < 8; ++i) fb->draw_buffers[i] = GL_NONE;
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}
void _gl_DrawBuffers(GLsizei n, const GLenum *bufs) {
    if (!g_gl_context || n < 0) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 0 && !bufs) { _gl_set_error(GL_INVALID_VALUE); return; }
    if (n > 8) { _gl_set_error(GL_INVALID_VALUE); return; }
    GLuint fbo = g_gl_context->bound_framebuffer;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (fbo == 0 && n != 1) {
        _gl_set_error(GL_INVALID_OPERATION);
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        if (is_default_framebuffer_color_name(bufs[i])) {
            _gl_set_error(GL_INVALID_ENUM);
            return;
        }
        if (fbo == 0) {
            if (bufs[i] != GL_NONE) {
                _gl_set_error(is_color_attachment_name(bufs[i], NULL)
                                  ? GL_INVALID_OPERATION
                                  : GL_INVALID_ENUM);
                return;
            }
        } else if (bufs[i] != GL_NONE && !get_color_attachment_index(bufs[i], NULL)) {
            _gl_set_error(is_color_attachment_name(bufs[i], NULL)
                              ? GL_INVALID_OPERATION
                              : GL_INVALID_ENUM);
            return;
        }
        if (bufs[i] != GL_NONE) {
            for (GLsizei j = 0; j < i; ++j) {
                if (bufs[j] == bufs[i]) {
                    _gl_set_error(GL_INVALID_OPERATION);
                    return;
                }
            }
        }
    }
    for (uint32_t i = 0; i < 8; ++i)
        fb->draw_buffers[i] = (i < (uint32_t)n) ? bufs[i] : GL_NONE;
    fb->dirty = true;
    g_gl_context->dirty_flags |= GL_DIRTY_FRAMEBUFFER;
}
void _gl_ReadBuffer(GLenum src) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_read_framebuffer;
    if (fbo == 0) {
        if (src != GL_NONE && src != GL_BACK) {
            _gl_set_error(is_default_framebuffer_color_name(src) ||
                          is_color_attachment_name(src, NULL)
                              ? GL_INVALID_OPERATION
                              : GL_INVALID_ENUM);
            return;
        }
    } else if (src != GL_NONE && !get_color_attachment_index(src, NULL)) {
        _gl_set_error(is_default_framebuffer_color_name(src) ||
                      is_color_attachment_name(src, NULL)
                          ? GL_INVALID_OPERATION
                          : GL_INVALID_ENUM);
        return;
    }
    g_framebuffers[fbo].read_buffer = src;
}

GLenum _gl_CheckFramebufferStatus(GLenum target) {
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return 0; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) return GL_FRAMEBUFFER_COMPLETE;
    GLFramebuffer *fb = &g_framebuffers[fbo];
    bool has_att = false;
    GLsizei expected_samples = 0;
    bool expected_samples_set = false;
    GLboolean expected_fixed_sample_locations = GL_TRUE;
    bool expected_fixed_sample_locations_set = false;
    for (uint32_t i = 0; i < 8; ++i) {
        GLAttachmentImageInfo info;
        if (!attachment_ref_present(&fb->color_attachments[i])) continue;
        has_att = true;
        if (!get_attachment_image_info(&fb->color_attachments[i], &info) ||
            info.width <= 0 || info.height <= 0 ||
            info.depth_renderable || info.stencil_renderable) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        if (!expected_samples_set) {
            expected_samples = info.samples;
            expected_samples_set = true;
        } else if (info.samples != expected_samples) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
        if (!expected_fixed_sample_locations_set) {
            expected_fixed_sample_locations = info.fixed_sample_locations;
            expected_fixed_sample_locations_set = true;
        } else if (info.fixed_sample_locations !=
                   expected_fixed_sample_locations) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
    }
    if (attachment_ref_present(&fb->depth_attachment)) {
        GLAttachmentImageInfo info;
        has_att = true;
        if (!get_attachment_image_info(&fb->depth_attachment, &info) ||
            info.width <= 0 || info.height <= 0 ||
            !info.depth_renderable) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        if (!expected_samples_set) {
            expected_samples = info.samples;
            expected_samples_set = true;
        } else if (info.samples != expected_samples) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
        if (!expected_fixed_sample_locations_set) {
            expected_fixed_sample_locations = info.fixed_sample_locations;
            expected_fixed_sample_locations_set = true;
        } else if (info.fixed_sample_locations !=
                   expected_fixed_sample_locations) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
    }
    if (attachment_ref_present(&fb->stencil_attachment)) {
        GLAttachmentImageInfo info;
        has_att = true;
        if (!get_attachment_image_info(&fb->stencil_attachment, &info) ||
            info.width <= 0 || info.height <= 0 ||
            !info.stencil_renderable) {
            return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        if (!expected_samples_set) {
            expected_samples = info.samples;
            expected_samples_set = true;
        } else if (info.samples != expected_samples) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
        if (!expected_fixed_sample_locations_set) {
            expected_fixed_sample_locations = info.fixed_sample_locations;
            expected_fixed_sample_locations_set = true;
        } else if (info.fixed_sample_locations !=
                   expected_fixed_sample_locations) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
    }
    if (!has_att) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;

    bool any_layered = false;
    bool any_nonlayered = false;
    GLenum layered_color_target = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        const GLAttachmentRef *ref = &fb->color_attachments[i];
        if (!attachment_ref_present(ref)) continue;
        any_layered |= ref->layered == GL_TRUE;
        any_nonlayered |= ref->layered != GL_TRUE;
        if (ref->layered) {
            if (layered_color_target == 0) {
                layered_color_target = ref->textarget;
            } else if (ref->textarget != layered_color_target) {
                return GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS;
            }
        }
    }
    const GLAttachmentRef *depth_stencil[2] = {
        &fb->depth_attachment, &fb->stencil_attachment
    };
    for (uint32_t i = 0; i < 2; ++i) {
        const GLAttachmentRef *ref = depth_stencil[i];
        if (!attachment_ref_present(ref)) continue;
        any_layered |= ref->layered == GL_TRUE;
        any_nonlayered |= ref->layered != GL_TRUE;
    }
    if (any_layered && any_nonlayered) {
        return GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t attachment_index = 0;
        if (fb->draw_buffers[i] == GL_NONE) continue;
        if (!get_color_attachment_index(fb->draw_buffers[i], &attachment_index) ||
            !attachment_ref_present(&fb->color_attachments[attachment_index])) {
            return GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER;
        }
    }
    if (fb->read_buffer != GL_NONE) {
        uint32_t attachment_index = 0;
        if (!get_color_attachment_index(fb->read_buffer, &attachment_index)) {
            return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER;
        }
        if (!attachment_ref_present(&fb->color_attachments[attachment_index])) {
            return GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER;
        }
    }
    return GL_FRAMEBUFFER_COMPLETE;
}

void _gl_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    if (!params) return;
    if (!g_gl_context || target != GL_RENDERBUFFER) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint id = g_gl_context->bound_renderbuffer;
    GLRenderbuffer *rb = get_renderbuffer(id);
    if (!rb) { _gl_set_error(GL_INVALID_OPERATION); return; }
    switch (pname) {
    case GL_RENDERBUFFER_WIDTH:           *params = rb->width; break;
    case GL_RENDERBUFFER_HEIGHT:          *params = rb->height; break;
    case GL_RENDERBUFFER_INTERNAL_FORMAT: *params = rb->internal_format; break;
    case GL_RENDERBUFFER_SAMPLES:         *params = rb->samples; break;
    case GL_RENDERBUFFER_RED_SIZE:
    case GL_RENDERBUFFER_GREEN_SIZE:
    case GL_RENDERBUFFER_BLUE_SIZE:
    case GL_RENDERBUFFER_ALPHA_SIZE:
    case GL_RENDERBUFFER_DEPTH_SIZE:
    case GL_RENDERBUFFER_STENCIL_SIZE:
        *params = renderbuffer_component_bits(rb->internal_format, pname);
        break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void _gl_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    GLAttachmentRef *ref;
    GLAttachmentImageInfo info;

    if (!params) return;
    if (!is_framebuffer_target(target)) { _gl_set_error(GL_INVALID_ENUM); return; }
    GLuint fbo = get_bound_framebuffer_for_target(target);
    if (fbo == 0) { *params = GL_NONE; return; }
    if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        GLFramebuffer *fb = &g_framebuffers[fbo];
        bool depth_present = attachment_ref_present(&fb->depth_attachment);
        bool stencil_present = attachment_ref_present(&fb->stencil_attachment);
        if (depth_present && stencil_present &&
            (fb->depth_attachment.kind != fb->stencil_attachment.kind ||
             fb->depth_attachment.object != fb->stencil_attachment.object)) {
            _gl_set_error(GL_INVALID_OPERATION);
            return;
        }
        ref = depth_present ? &fb->depth_attachment : &fb->stencil_attachment;
    } else {
        ref = get_attachment_ref(&g_framebuffers[fbo], attachment);
    }
    if (!ref) { _gl_set_error(GL_INVALID_ENUM); return; }
    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
        if (!attachment_ref_present(ref)) *params = GL_NONE;
        else if (ref->kind == GL_ATTACHMENT_KIND_TEXTURE) *params = GL_TEXTURE;
        else *params = GL_RENDERBUFFER;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
        *params = attachment_ref_present(ref) ? (GLint)ref->object : 0;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (!get_attachment_image_info(ref, &info)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        *params = attachment_component_bits(info.internal_format, pname);
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (!get_attachment_image_info(ref, &info)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        *params = attachment_component_type(info.internal_format);
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        *params = GL_LINEAR;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = ref->level;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = is_cube_map_face_target(ref->textarget) ? (GLint)ref->textarget : 0;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = !ref->layered &&
                          (ref->textarget == GL_TEXTURE_3D ||
                           ref->textarget == GL_TEXTURE_1D_ARRAY ||
                           ref->textarget == GL_TEXTURE_2D_ARRAY ||
                           ref->textarget == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
                      ? ref->layer
                      : 0;
        break;
    case GL_FRAMEBUFFER_ATTACHMENT_LAYERED:
        if (!attachment_ref_present(ref)) { _gl_set_error(GL_INVALID_OPERATION); return; }
        if (ref->kind != GL_ATTACHMENT_KIND_TEXTURE) { _gl_set_error(GL_INVALID_ENUM); return; }
        *params = ref->layered;
        break;
    default: _gl_set_error(GL_INVALID_ENUM); break;
    }
}

void gl_bind_framebuffers(void) {
    if (!g_gl_context) return;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) {
        GX2ColorBuffer *default_color = get_default_color_buffer();
        GX2DepthBuffer *default_depth = get_default_depth_buffer();
        if (default_color) GX2SetColorBuffer(default_color, GX2_RENDER_TARGET_0);
        if (default_depth) GX2SetDepthBuffer(default_depth);
        apply_framebuffer_output_state(&g_framebuffers[0], true);
        return;
    }
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (fb->dirty) {
        for (uint32_t i = 0; i < 8; ++i) {
            if (!attachment_ref_present(&fb->color_attachments[i])) {
                free_framebuffer_texture_target(fb, i);
                continue;
            }
            if (fb->color_attachments[i].kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->color_attachments[i].object);
                free_framebuffer_texture_target(fb, i);
                if (t && init_color_buffer_from_surface_view(
                             &fb->cb[i], &t->surface,
                             fb->color_attachments[i].level,
                             attachment_view_slice(&fb->color_attachments[i]),
                             attachment_view_count(&fb->color_attachments[i],
                                                   &t->surface))) {
                    fb->color_buffer_owns_aux[i] = true;
                }
            } else {
                free_framebuffer_texture_target(fb, i);
                GLRenderbuffer *rb = get_renderbuffer(fb->color_attachments[i].object);
                if (rb && !rb->is_depth) {
                    fb->cb[i] = rb->color_buffer;
                    fb->color_buffer_owns_aux[i] = false;
                }
            }
        }
        memset(&fb->db, 0, sizeof(fb->db));
        if (attachment_ref_present(&fb->depth_attachment)) {
            if (fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->depth_attachment.object);
                if (t) init_depth_buffer_from_surface_view(
                           &fb->db, &t->surface, fb->depth_attachment.level,
                           attachment_view_slice(&fb->depth_attachment),
                           attachment_view_count(&fb->depth_attachment,
                                                 &t->surface));
            } else {
                GLRenderbuffer *rb = get_renderbuffer(fb->depth_attachment.object);
                if (rb && rb->is_depth) fb->db = rb->depth_buffer;
            }
        } else if (attachment_ref_present(&fb->stencil_attachment)) {
            if (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) {
                GX2Texture *t = gl_get_gx2_texture(fb->stencil_attachment.object);
                if (t) init_depth_buffer_from_surface_view(
                           &fb->db, &t->surface, fb->stencil_attachment.level,
                           attachment_view_slice(&fb->stencil_attachment),
                           attachment_view_count(&fb->stencil_attachment,
                                                 &t->surface));
            } else {
                GLRenderbuffer *rb = get_renderbuffer(fb->stencil_attachment.object);
                if (rb && rb->is_depth) fb->db = rb->depth_buffer;
            }
        }
        fb->dirty = false;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t attachment_index = 0;
        if (get_color_attachment_index(fb->draw_buffers[i], &attachment_index) &&
            attachment_ref_present(&fb->color_attachments[attachment_index]))
            GX2SetColorBuffer(&fb->cb[attachment_index], (GX2RenderTarget)i);
    }
    GX2SetDepthBuffer(&fb->db);
    apply_framebuffer_output_state(fb, false);
}

GLboolean gl_is_draw_color_buffer_enabled(GLuint index) {
    if (!g_gl_context || index >= 8) return GL_FALSE;
    if (g_gl_context->bound_framebuffer == 0) {
        return (index == 0 && g_framebuffers[0].draw_buffers[0] == GL_BACK) ? GL_TRUE : GL_FALSE;
    }

    {
        GLFramebuffer *fb = &g_framebuffers[g_gl_context->bound_framebuffer];
        uint32_t attachment_index = 0;
        return (get_color_attachment_index(fb->draw_buffers[index], &attachment_index) &&
                attachment_ref_present(&fb->color_attachments[attachment_index])) ? GL_TRUE : GL_FALSE;
    }
}
GX2ColorBuffer *gl_get_draw_color_buffer(GLuint index) {
    if (!g_gl_context) return NULL;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) return (index == 0 && g_framebuffers[0].draw_buffers[0] == GL_BACK) ? get_default_color_buffer() : NULL;
    if (index >= 8) return NULL;
    {
        GLFramebuffer *fb = &g_framebuffers[fbo];
        uint32_t attachment_index = 0;
        if (get_color_attachment_index(fb->draw_buffers[index], &attachment_index) &&
            attachment_ref_present(&fb->color_attachments[attachment_index])) return &fb->cb[attachment_index];
    }
    return NULL;
}
GX2DepthBuffer *gl_get_draw_depth_buffer(void) {
    if (!g_gl_context) return NULL;
    GLuint fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0) return get_default_depth_buffer();
    GLFramebuffer *fb = &g_framebuffers[fbo];
    if (attachment_ref_present(&fb->depth_attachment)) return &fb->db;
    return NULL;
}

GLsizei gl_get_draw_sample_count(void) {
    if (!g_gl_context) return 0;
    return framebuffer_sample_count(g_gl_context->bound_framebuffer);
}

void gl_framebuffer_mark_bound_color_dirty(void) {
    GLuint fbo;
    GLFramebuffer *fb;

    if (!g_gl_context) return;
    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0 || fbo >= MAX_FRAMEBUFFERS) return;

    fb = &g_framebuffers[fbo];
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t attachment_index = 0;
        if (get_color_attachment_index(fb->draw_buffers[i], &attachment_index) &&
            fb->color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
            fb->color_needs_resolve[attachment_index] = true;
        }
    }
    if ((fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE ||
         fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) &&
        fb->db.surface.aa != GX2_AA_MODE1X) {
        fb->depth_needs_expand = true;
    }
}

void gl_framebuffer_mark_bound_color_buffer_dirty(GLuint index) {
    GLuint fbo;
    GLFramebuffer *fb;
    uint32_t attachment_index = 0;

    if (!g_gl_context || index >= 8) return;
    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0 || fbo >= MAX_FRAMEBUFFERS) return;

    fb = &g_framebuffers[fbo];
    if (get_color_attachment_index(fb->draw_buffers[index], &attachment_index) &&
        fb->color_attachments[attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
        fb->color_needs_resolve[attachment_index] = true;
    }
    if ((fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE ||
         fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE) &&
        fb->db.surface.aa != GX2_AA_MODE1X) {
        fb->depth_needs_expand = true;
    }
}

void gl_framebuffer_sync_bound_color_targets(void) {
    GLuint fbo;
    if (!g_gl_context) return;
    fbo = g_gl_context->bound_framebuffer;
    if (fbo == 0 || fbo >= MAX_FRAMEBUFFERS) return;
    resolve_framebuffer_texture_targets(&g_framebuffers[fbo]);
}

void gl_framebuffer_sync_texture_for_sampling(GLuint texture) {
    if (texture == 0) return;
    for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; ++i) {
        GLFramebuffer *fb = &g_framebuffers[i];
        if (!fb->in_use) continue;
        for (uint32_t j = 0; j < 8; ++j) {
            if (fb->color_attachments[j].kind == GL_ATTACHMENT_KIND_TEXTURE &&
                fb->color_attachments[j].object == texture &&
                fb->color_needs_resolve[j]) {
                resolve_framebuffer_texture_targets(fb);
                break;
            }
        }
        if (fb->depth_needs_expand &&
            ((fb->depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE &&
              fb->depth_attachment.object == texture) ||
             (fb->stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE &&
              fb->stencil_attachment.object == texture))) {
            GX2DrawDone();
            GX2ExpandDepthBuffer(&fb->db);
            GX2DrawDone();
            restore_context_after_surface_op();
            fb->depth_needs_expand = false;
        }
    }
}

void gl_framebuffer_mark_texture_dirty(GLuint texture) {
    if (texture == 0) return;
    for (uint32_t i = 0; i < MAX_FRAMEBUFFERS; ++i) {
        if (!g_framebuffers[i].in_use) continue;
        bool fb_dirty = false;
        for (uint32_t j = 0; j < 8; ++j) {
            if (g_framebuffers[i].color_attachments[j].kind == GL_ATTACHMENT_KIND_TEXTURE &&
                g_framebuffers[i].color_attachments[j].object == texture) fb_dirty = true;
        }
        if (g_framebuffers[i].depth_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE &&
            g_framebuffers[i].depth_attachment.object == texture) fb_dirty = true;
        if (g_framebuffers[i].stencil_attachment.kind == GL_ATTACHMENT_KIND_TEXTURE &&
            g_framebuffers[i].stencil_attachment.object == texture) fb_dirty = true;
        if (fb_dirty) g_framebuffers[i].dirty = true;
    }
}

GLboolean gl_read_color_pixels_rgba8(GLint x, GLint y, GLsizei width, GLsizei height, void *pixels) {
    GX2ColorBuffer *cb;
    GX2Surface *surface;
    GX2Surface staging_surface;
    GX2Surface texture_read_surface;
    GX2Surface *read_surface;
    uint8_t *dst;
    GLint internal_format;
    bool staged_surface = false;
    bool texture_attachment_read = false;
    GLuint read_fbo;
    uint32_t texture_attachment_index = 0;

    if (!g_gl_context || !pixels || width < 0 || height < 0) return GL_FALSE;
    if (width == 0 || height == 0) return GL_TRUE;

    cb = get_read_color_buffer();
    if (!cb || !cb->surface.image) return GL_FALSE;

    surface = &cb->surface;
    memset(&texture_read_surface, 0, sizeof(texture_read_surface));
    read_fbo = g_gl_context->bound_read_framebuffer;
    if (read_fbo > 0 && read_fbo < MAX_FRAMEBUFFERS &&
        get_color_attachment_index(g_framebuffers[read_fbo].read_buffer, &texture_attachment_index) &&
        g_framebuffers[read_fbo].color_attachments[texture_attachment_index].kind == GL_ATTACHMENT_KIND_TEXTURE) {
        GLAttachmentRef *attachment =
            &g_framebuffers[read_fbo].color_attachments[texture_attachment_index];
        GX2Texture *texture =
            gl_get_gx2_texture(attachment->object);
        if (texture && build_texture_attachment_read_surface(attachment,
                                                             &texture->surface,
                                                             &texture_read_surface)) {
            surface = &texture_read_surface;
            texture_attachment_read = true;
        }
    }
    internal_format = get_read_color_internal_format();
    dst = (uint8_t *)pixels;
    memset(dst, 0, (size_t)width * (size_t)height * 4u);

    GX2DrawDone();
    memset(&staging_surface, 0, sizeof(staging_surface));
    if (stage_surface_for_cpu_access(surface, internal_format, false,
                                     &staging_surface)) {
        read_surface = &staging_surface;
        staged_surface = true;
    } else {
        read_surface = surface;
        invalidate_surface_after_color_write(read_surface);
        DCInvalidateRange(read_surface->image, read_surface->imageSize);
    }

    for (GLsizei row = 0; row < height; ++row) {
        GLint gl_y = y + row;
        GLint src_y = texture_attachment_read
                          ? (GLint)read_surface->height - 1 - gl_y
                          : gl_y;
        if (src_y < 0 || src_y >= (GLint)read_surface->height) continue;

        for (GLsizei col = 0; col < width; ++col) {
            GLint src_x = x + col;
            uint8_t *dst_texel;

            if (src_x < 0 || src_x >= (GLint)read_surface->width) continue;

            dst_texel = dst + ((size_t)row * (size_t)width + (size_t)col) * 4u;
            switch (read_surface->format) {
            case GX2_SURFACE_FORMAT_UNORM_R8: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image + (size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x;
                if (internal_format == GL_ALPHA) {
                    dst_texel[0] = 0;
                    dst_texel[1] = 0;
                    dst_texel[2] = 0;
                    dst_texel[3] = src_texel[0];
                } else if (internal_format == GL_LUMINANCE) {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = src_texel[0];
                    dst_texel[2] = src_texel[0];
                    dst_texel[3] = 0xFF;
                } else {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = 0;
                    dst_texel[2] = 0;
                    dst_texel[3] = 0xFF;
                }
                break;
            }
            case GX2_SURFACE_FORMAT_UNORM_R8_G8: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    ((size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x) * 2u;
                if (internal_format == GL_LUMINANCE_ALPHA) {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = src_texel[0];
                    dst_texel[2] = src_texel[0];
                    dst_texel[3] = src_texel[1];
                } else {
                    dst_texel[0] = src_texel[0];
                    dst_texel[1] = src_texel[1];
                    dst_texel[2] = 0;
                    dst_texel[3] = 0xFF;
                }
                break;
            }
            case GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    (((size_t)src_y * (size_t)read_surface->pitch) + (size_t)src_x) * 4u;
                uint32_t pixel;
                memcpy(&pixel, src_texel, sizeof(pixel));
                pixel = GPU_TO_CPU_32(pixel);
                dst_texel[0] = (uint8_t)((pixel >> 24) & 0xFFu);
                dst_texel[1] = (uint8_t)((pixel >> 16) & 0xFFu);
                dst_texel[2] = (uint8_t)((pixel >> 8) & 0xFFu);
                dst_texel[3] = (uint8_t)(pixel & 0xFFu);
                break;
            }
            case GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    ((size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x) * 8u;
                dst_texel[0] = float_to_unorm8(decode_gpu_half_float(src_texel + 0));
                dst_texel[1] = float_to_unorm8(decode_gpu_half_float(src_texel + 2));
                dst_texel[2] = float_to_unorm8(decode_gpu_half_float(src_texel + 4));
                dst_texel[3] = float_to_unorm8(decode_gpu_half_float(src_texel + 6));
                break;
            }
            case GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32: {
                const uint8_t *src_texel =
                    (const uint8_t *)read_surface->image +
                    ((size_t)src_y * (size_t)read_surface->pitch + (size_t)src_x) * 16u;
                dst_texel[0] = float_to_unorm8(decode_gpu_float32(src_texel + 0));
                dst_texel[1] = float_to_unorm8(decode_gpu_float32(src_texel + 4));
                dst_texel[2] = float_to_unorm8(decode_gpu_float32(src_texel + 8));
                dst_texel[3] = float_to_unorm8(decode_gpu_float32(src_texel + 12));
                break;
            }
            default:
                return GL_FALSE;
            }
        }
    }

    if (staged_surface) {
        free_surface_storage(&staging_surface);
    }
    return GL_TRUE;
}

#ifdef __cplusplus
}
#endif
