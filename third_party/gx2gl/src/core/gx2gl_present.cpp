#include "gx2gl/present.h"

#include "core/gl_context.h"

#include <gx2/event.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <whb/gfx.h>

int GX2GL_MirrorTVToDRC(void)
{
    GX2ColorBuffer *tv_color = WHBGfxGetTVColourBuffer();
    GX2ColorBuffer *drc_color = WHBGfxGetDRCColourBuffer();

    if (!tv_color || !drc_color ||
        !WHBGfxGetTVContextState() || !WHBGfxGetDRCContextState() ||
        !tv_color->surface.image || !drc_color->surface.image) {
        return -1;
    }

    if (g_gl_context) {
        _gl_Flush();
    }

    GX2DrawDone();
    GX2CopyColorBufferToScanBuffer(tv_color, GX2_SCAN_TARGET_DRC);
    GX2Flush();
    return 0;
}

int GX2GL_MirrorTVToGamePad(void)
{
    return GX2GL_MirrorTVToDRC();
}
