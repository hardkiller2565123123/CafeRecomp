#ifndef GX2GL_TESTS_PIGLIT_LITE_H
#define GX2GL_TESTS_PIGLIT_LITE_H

typedef void (*PiglitReportFunc)(const char *fmt, ...);

typedef enum {
    PIGLIT_RESULT_PASS,
    PIGLIT_RESULT_FAIL,
    PIGLIT_RESULT_SKIP,
} PiglitResult;

typedef struct {
    unsigned int total;
    unsigned int pass;
    unsigned int fail;
    unsigned int skip;
} PiglitRunStats;

typedef void (*PiglitResultFunc)(const char *name, PiglitResult result,
                                 const char *detail, void *user_data);
typedef void (*PiglitBeginFunc)(const char *name, void *user_data);
typedef int (*PiglitContinueFunc)(void *user_data);

PiglitRunStats run_piglit_tests(PiglitReportFunc report,
                                PiglitBeginFunc begin_func,
                                PiglitResultFunc result_func,
                                PiglitContinueFunc continue_func,
                                void *user_data);

#endif
