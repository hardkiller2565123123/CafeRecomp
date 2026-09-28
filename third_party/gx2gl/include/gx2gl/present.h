#ifndef GX2GL_PRESENT_H
#define GX2GL_PRESENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* GX2GL_Context;

int GX2GL_LoadLibrary(const char *path);
void GX2GL_UnloadLibrary(void);

GX2GL_Context GX2GL_CreateContext(void);
int GX2GL_MakeCurrent(GX2GL_Context context);
void GX2GL_DeleteContext(GX2GL_Context context);

int GX2GL_BeginFrame(void);
int GX2GL_EndFrame(void);
int GX2GL_Present(void);

int GX2GL_SetSwapInterval(int interval);
int GX2GL_GetSwapInterval(void);
int GX2GL_SetAutomaticDRCEnabled(int enabled);
int GX2GL_GetAutomaticDRCEnabled(void);
int GX2GL_SetDefaultFramebufferTargetDRC(int enabled);
int GX2GL_GetDefaultFramebufferTargetDRC(void);
int GX2GL_CopyToDRC(void);
int GX2GL_MirrorTVToDRC(void);
int GX2GL_MirrorTVToGamePad(void);

#ifdef __cplusplus
}
#endif

#endif
