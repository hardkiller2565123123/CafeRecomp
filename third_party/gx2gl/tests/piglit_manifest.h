#ifndef GX2GL_TESTS_PIGLIT_MANIFEST_H
#define GX2GL_TESTS_PIGLIT_MANIFEST_H

#include "piglit_lite.h"

PiglitRunStats run_piglit_manifest_tests(PiglitReportFunc report,
                                         PiglitBeginFunc begin_func,
                                         PiglitResultFunc result_func,
                                         PiglitContinueFunc continue_func,
                                         void *user_data);

#endif
