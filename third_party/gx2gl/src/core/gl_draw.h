#ifndef GL33_DRAW_H
#define GL33_DRAW_H

#include "core/gl_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void _gl_DrawArrays(GLenum mode, GLint first, GLsizei count);
void _gl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
void _gl_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void _gl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instancecount);
void _gl_DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices);
void _gl_DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex);
void _gl_DrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices, GLint basevertex);
void _gl_DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices, GLsizei instancecount, GLint basevertex);
void _gl_MultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount);
void _gl_MultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const*indices, GLsizei drawcount);
void _gl_MultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count, GLenum type, const GLvoid *const*indices, GLsizei drawcount, const GLint *basevertex);
void _gl_PrimitiveRestartIndex(GLuint index);

#ifdef __cplusplus
}
#endif

#endif
