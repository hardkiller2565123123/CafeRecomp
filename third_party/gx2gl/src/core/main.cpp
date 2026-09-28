#include <gx2/common.h>
#include <gx2/state.h>
#include <whb/proc.h>
#include "mem/gl_mem.h"

int main(int argc, char **argv) {

    WHBProcInit();


    gl_mem_init();


    GX2Init(NULL);


    while (WHBProcIsRunning()) {

    }


    GX2Shutdown();
    gl_mem_shutdown();
    WHBProcShutdown();

    return 0;
}
