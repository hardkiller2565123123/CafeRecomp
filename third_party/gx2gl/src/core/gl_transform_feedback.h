#ifndef GL33_TRANSFORM_FEEDBACK_H
#define GL33_TRANSFORM_FEEDBACK_H

#include "core/gl_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void gl_transform_feedback_init(void);
void gl_transform_feedback_shutdown(void);

void _gl_GenTransformFeedbacks(GLsizei n, GLuint *ids);
void _gl_DeleteTransformFeedbacks(GLsizei n, const GLuint *ids);

gl_uniform_buffer_binding_t *gl_transform_feedback_current_buffer_binding(GLuint index);
GLboolean gl_transform_feedback_current_active(void);
GLboolean gl_transform_feedback_current_active_unpaused(void);
GLboolean gl_transform_feedback_current_active_paused(void);
GLboolean gl_transform_feedback_program_active(GLuint program);
void gl_transform_feedback_unbind_buffer(GLuint buffer);
GLboolean gl_transform_feedback_validate_draw_mode(GLenum mode);

#ifdef __cplusplus
}
#endif

#endif
