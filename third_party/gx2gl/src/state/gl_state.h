#ifndef GL33_STATE_H
#define GL33_STATE_H

#include "core/gl_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void gl_flush_state(void);
GLboolean gl_get_multisample_position(GLsizei samples, GLuint index,
                                      GLfloat *position);
GLboolean gl_state_cpu_clear_draw_color_buffer(GLuint index,
                                               const GLfloat *clear_color);

void _gl_ClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void _gl_ClearDepth(GLclampd depth);
void _gl_ClearStencil(GLint s);
void _gl_Clear(GLbitfield mask);
void _gl_Enable(GLenum cap);
void _gl_Disable(GLenum cap);
GLboolean _gl_IsEnabled(GLenum cap);
void _gl_BlendFunc(GLenum sfactor, GLenum dfactor);
void _gl_BlendEquation(GLenum mode);
void _gl_BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
void _gl_BlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
void _gl_BlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void _gl_DepthFunc(GLenum func);
void _gl_DepthMask(GLboolean flag);
void _gl_DepthRange(GLclampd nearVal, GLclampd farVal);
void _gl_StencilFunc(GLenum func, GLint ref, GLuint mask);
void _gl_StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
void _gl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void _gl_StencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
void _gl_StencilMask(GLuint mask);
void _gl_StencilMaskSeparate(GLenum face, GLuint mask);
void _gl_CullFace(GLenum mode);
void _gl_FrontFace(GLenum mode);
void _gl_PolygonMode(GLenum face, GLenum mode);
void _gl_PolygonOffset(GLfloat factor, GLfloat units);
void _gl_Viewport(GLint x, GLint y, GLsizei width, GLsizei height);
void _gl_Scissor(GLint x, GLint y, GLsizei width, GLsizei height);
void _gl_ColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void _gl_LineWidth(GLfloat width);
void _gl_Hint(GLenum target, GLenum mode);
void _gl_SampleCoverage(GLclampf value, GLboolean invert);
void _gl_PixelStorei(GLenum pname, GLint param);
void _gl_LogicOp(GLenum opcode);
void _gl_PointSize(GLfloat size);

#ifdef __cplusplus
}
#endif

#endif
