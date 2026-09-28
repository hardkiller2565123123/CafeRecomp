#ifndef GL33_QUERY_H
#define GL33_QUERY_H

#include "gl/gl.h"

#ifdef __cplusplus
extern "C" {
#endif

void gl_query_init(void);
void gl_query_shutdown(void);

void gl_sync_shutdown(void);
void gl_sync_signal_all(void);

#ifdef __cplusplus
}
#endif

#endif
