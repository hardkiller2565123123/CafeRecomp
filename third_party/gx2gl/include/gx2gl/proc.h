#ifndef GX2GL_PROC_H
#define GX2GL_PROC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GX2GL_Proc)(void);

void* GX2GL_GetProcAddress(const char *name);

#ifdef __cplusplus
}
#endif

#endif
