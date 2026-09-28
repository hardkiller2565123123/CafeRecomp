#ifndef GL33_TEXTURE_H
#define GL33_TEXTURE_H

#include "core/gl_context.h"
#ifdef __cplusplus
extern "C" {
#include <gx2/sampler.h>
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

void gl_texture_init(void);
void gl_texture_shutdown(void);

void _gl_GenTextures(GLsizei n, GLuint *textures);
void _gl_DeleteTextures(GLsizei n, const GLuint *textures);
GLboolean _gl_IsTexture(GLuint texture);
void _gl_GenSamplers(GLsizei n, GLuint *samplers);
void _gl_DeleteSamplers(GLsizei n, const GLuint *samplers);
GLboolean _gl_IsSampler(GLuint sampler);
void _gl_BindTexture(GLenum target, GLuint texture);
void _gl_BindSampler(GLuint unit, GLuint sampler);
void _gl_ActiveTexture(GLenum texture);
void _gl_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_TexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_TexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_CopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void _gl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void _gl_CompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
void _gl_CompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data);
void _gl_TexParameteri(GLenum target, GLenum pname, GLint param);
void _gl_TexParameterf(GLenum target, GLenum pname, GLfloat param);
void _gl_TexParameteriv(GLenum target, GLenum pname, const GLint *params);
void _gl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void _gl_GetTexParameteriv(GLenum target, GLenum pname, GLint *params);
void _gl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
void _gl_SamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param);
void _gl_SamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param);
void _gl_SamplerParameteri(GLuint sampler, GLenum pname, GLint param);
void _gl_SamplerParameterf(GLuint sampler, GLenum pname, GLfloat param);
void _gl_GetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params);
void _gl_GetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params);
void _gl_GenerateMipmap(GLenum target);
void _gl_TexImage2DMultisample(GLenum target, GLsizei samples,
                               GLenum internalformat, GLsizei width,
                               GLsizei height, GLboolean fixedsamplelocations);
void _gl_TexImage3DMultisample(GLenum target, GLsizei samples,
                               GLenum internalformat, GLsizei width,
                               GLsizei height, GLsizei depth,
                               GLboolean fixedsamplelocations);

void _gl_TexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_TexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void _gl_CopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border);
void _gl_CopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void _gl_CopyTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void _gl_CompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid *data);
void _gl_CompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid *data);
void _gl_CompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLsizei imageSize, const GLvoid *data);
void _gl_CompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid *data);
void _gl_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params);
void _gl_GetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params);
void _gl_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void _gl_GetCompressedTexImage(GLenum target, GLint level, GLvoid *pixels);

void gl_bind_textures(void);

GX2Texture *gl_get_gx2_texture(GLuint id);
GLint gl_get_texture_internal_format(GLuint id);
GLenum gl_get_texture_target(GLuint id);
GLsizei gl_get_texture_samples(GLuint id);
GLboolean gl_get_texture_fixed_sample_locations(GLuint id);
GX2Sampler *gl_get_gx2_sampler(GLuint id, bool use_sampler_obj);

#ifdef __cplusplus
}
#endif

#endif
