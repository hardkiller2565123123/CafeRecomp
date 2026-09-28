#ifndef __gles2_gl2_h_
#define __gles2_gl2_h_ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <GLES2/gl2platform.h>

#ifndef GL_APIENTRYP
#define GL_APIENTRYP GL_APIENTRY*
#endif

#ifndef GL_GLES_PROTOTYPES
#define GL_GLES_PROTOTYPES 1
#endif

#include <gl/gl.h>

#ifndef GL_ES_VERSION_2_0
#define GL_ES_VERSION_2_0 1
#endif

#ifdef __cplusplus
}
#endif

#endif
