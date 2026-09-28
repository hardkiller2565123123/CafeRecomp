#ifndef GL33_SHADER_H
#define GL33_SHADER_H

#include "core/gl_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void gl_shader_init(void);
void gl_shader_shutdown(void);

GLuint _gl_CreateShader(GLenum type);
void _gl_DeleteShader(GLuint shader);
GLboolean _gl_IsShader(GLuint shader);
void _gl_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
void _gl_GetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source);
void _gl_CompileShader(GLuint shader);
GLuint _gl_CreateProgram(void);
void _gl_DeleteProgram(GLuint program);
GLboolean _gl_IsProgram(GLuint program);
void _gl_AttachShader(GLuint program, GLuint shader);
void _gl_DetachShader(GLuint program, GLuint shader);
void _gl_LinkProgram(GLuint program);
void _gl_UseProgram(GLuint program);
void _gl_ValidateProgram(GLuint p);
void _gl_BindAttribLocation(GLuint p, GLuint i, const GLchar* n);
void _gl_GetAttachedShaders(GLuint p, GLsizei m, GLsizei* c, GLuint* s);
void _gl_GetActiveAttrib(GLuint p, GLuint i, GLsizei b, GLsizei* l, GLint* s, GLenum* t, GLchar* n);
void _gl_GetActiveUniform(GLuint p, GLuint i, GLsizei b, GLsizei* l, GLint* s, GLenum* t, GLchar* n);
void _gl_GetShaderiv(GLuint shader, GLenum pname, GLint *params);
void _gl_GetProgramiv(GLuint program, GLenum pname, GLint *params);
void _gl_GetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void _gl_GetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void _gl_GetUniformfv(GLuint program, GLint location, GLfloat *params);
void _gl_GetUniformiv(GLuint program, GLint location, GLint *params);
void _gl_GetUniformuiv(GLuint program, GLint location, GLuint *params);
GLint _gl_GetUniformLocation(GLuint program, const GLchar *name);
GLint _gl_GetAttribLocation(GLuint program, const GLchar *name);
GLuint _gl_GetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName);
void _gl_UniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);
void _gl_WiiULoadShaderGroup(GLuint program, const void* shaderGroup);
void _gl_WiiULoadShaderGroupGFD(GLuint program, GLuint index, const void *gfdData);
void _gl_ReleaseShaderCompiler(void);
void _gl_ShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat, const GLvoid *binary, GLsizei length);
void _gl_GetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision);

void _gl_Uniform1f(GLint location, GLfloat v0);
void _gl_Uniform1fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform1i(GLint location, GLint v0);
void _gl_Uniform1iv(GLint location, GLsizei count, const GLint *value);
void _gl_Uniform2f(GLint location, GLfloat v0, GLfloat v1);
void _gl_Uniform2fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform2i(GLint location, GLint v0, GLint v1);
void _gl_Uniform2iv(GLint location, GLsizei count, const GLint *value);
void _gl_Uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void _gl_Uniform3fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform3i(GLint location, GLint v0, GLint v1, GLint v2);
void _gl_Uniform3iv(GLint location, GLsizei count, const GLint *value);
void _gl_Uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void _gl_Uniform4fv(GLint location, GLsizei count, const GLfloat *value);
void _gl_Uniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
void _gl_Uniform4iv(GLint location, GLsizei count, const GLint *value);
void _gl_Uniform1ui(GLint location, GLuint v0);
void _gl_Uniform2ui(GLint location, GLuint v0, GLuint v1);
void _gl_Uniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2);
void _gl_Uniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3);
void _gl_Uniform1uiv(GLint location, GLsizei count, const GLuint *value);
void _gl_Uniform2uiv(GLint location, GLsizei count, const GLuint *value);
void _gl_Uniform3uiv(GLint location, GLsizei count, const GLuint *value);
void _gl_Uniform4uiv(GLint location, GLsizei count, const GLuint *value);

void _gl_UniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void _gl_UniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

void _gl_GetActiveUniformBlockiv(GLuint p, GLuint i, GLenum n, GLint *v);
void _gl_GetActiveUniformBlockName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n);
void _gl_GetActiveUniformsiv(GLuint p, GLsizei c, const GLuint *i, GLenum n, GLint *v);
void _gl_GetActiveUniformName(GLuint p, GLuint i, GLsizei s, GLsizei *l, GLchar *n);
void _gl_TransformFeedbackVaryings(GLuint program, GLsizei count,
                                    const GLchar *const *varyings,
                                    GLenum bufferMode);
void _gl_GetTransformFeedbackVarying(GLuint program, GLuint index,
                                     GLsizei bufSize, GLsizei *length,
                                     GLsizei *size, GLenum *type,
                                     GLchar *name);
GLboolean gl_program_transform_feedback_ready(GLuint program);
GLuint gl_program_transform_feedback_binding_count(GLuint program);
GLenum gl_program_transform_feedback_buffer_mode(GLuint program);

void gl_bind_shaders(void);
void gl_bind_program_fetch_shader(void);
void gl_bind_program_textures(void);

#ifdef __cplusplus
}
#endif

#endif
