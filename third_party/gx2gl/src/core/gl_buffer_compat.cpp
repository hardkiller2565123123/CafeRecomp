#include "gl_context.h"
#include "gl_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset,
                         GLsizeiptr size) {
    gl_buffer_copy_sub_data(readTarget, writeTarget, readOffset, writeOffset,
                            size);
}

void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                        GLvoid *data) {
    gl_buffer_get_sub_data(target, offset, size, data);
}

void glGetPointerv(GLenum pname, GLvoid **params) {
    (void)pname;
    if (params) {
        *params = NULL;
    }
    _gl_set_error(GL_INVALID_OPERATION);
}

#ifdef __cplusplus
}
#endif
