#ifndef GX2GL_WIIU_LOG_HPP
#define GX2GL_WIIU_LOG_HPP

#include <coreinit/debug.h>
#include <stdarg.h>
#include <stdio.h>

static inline void WiiU_Log(const char *format, ...) {
    va_list args;
    char buffer[1024];

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    OSReport("%s\n", buffer);
}

static inline const char *WiiU_GetGX2GLInitLogPath(void) {
    return "gx2gl_init.log";
}

#endif
