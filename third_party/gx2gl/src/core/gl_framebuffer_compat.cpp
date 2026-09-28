#include "gl_context.h"
#include "gl_framebuffer.h"
#include "gl_texture.h"

#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#endif

static bool is_wrapper_framebuffer_target(GLenum target) {
  return target == GL_FRAMEBUFFER ||
         target == GL_DRAW_FRAMEBUFFER ||
         target == GL_READ_FRAMEBUFFER;
}

static bool is_wrapper_framebuffer_attachment(GLenum attachment) {
  return (attachment >= GL_COLOR_ATTACHMENT0 &&
          attachment <= GL_COLOR_ATTACHMENT7) ||
         attachment == GL_DEPTH_ATTACHMENT ||
         attachment == GL_STENCIL_ATTACHMENT ||
         attachment == GL_DEPTH_STENCIL_ATTACHMENT;
}

void glFramebufferTextureLayer(GLenum target, GLenum attachment,
                               GLuint texture, GLint level, GLint layer) {
  _gl_FramebufferTextureLayer(target, attachment, texture, level, layer);
}

void glFramebufferTexture1D(GLenum target, GLenum attachment,
                            GLenum textarget, GLuint texture, GLint level) {
  if (!is_wrapper_framebuffer_target(target) ||
      !is_wrapper_framebuffer_attachment(attachment) ||
      textarget != GL_TEXTURE_1D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (level < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (texture != 0 && !_gl_IsTexture(texture)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_set_error(GL_INVALID_OPERATION);
}

void glFramebufferTexture3D(GLenum target, GLenum attachment,
                            GLenum textarget, GLuint texture, GLint level,
                            GLint zoffset) {
  if (!is_wrapper_framebuffer_target(target) ||
      !is_wrapper_framebuffer_attachment(attachment) ||
      textarget != GL_TEXTURE_3D) {
    _gl_set_error(GL_INVALID_ENUM);
    return;
  }
  if (level < 0 || zoffset < 0) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  if (texture != 0 && !_gl_IsTexture(texture)) {
    _gl_set_error(GL_INVALID_VALUE);
    return;
  }
  _gl_FramebufferTextureLayer(target, attachment, texture, level, zoffset);
}
