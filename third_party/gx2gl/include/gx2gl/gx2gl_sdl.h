#ifndef GX2GL_SDL_H
#define GX2GL_SDL_H

#include "gx2gl/present.h"

#ifdef __cplusplus
extern "C" {
#endif

void* GX2GL_GetSDLProcAddress(const char *proc);
int GX2GL_SwapWindow(void);

#ifdef __cplusplus
}
#endif

#endif
