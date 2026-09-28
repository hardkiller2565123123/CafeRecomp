#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <whb/gfx.h>
#include <whb/proc.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "core/gl_context.h"
#include "core/gl_framebuffer.h"
#include "gx2gl/present.h"
#include "mem/gl_mem.h"
#include "piglit_lite.h"

namespace {

static const char *kResultsPath = "/vol/external01/gx2gl_results.txt";
static const char *kProgressPath = "/vol/external01/gx2gl_progress.txt";
static const char *kCaseLogPath = "/vol/external01/gx2gl_piglit.txt";
static const char *kReportLogPath = "/vol/external01/gx2gl_log.txt";
static const char *kDonePath = "/vol/external01/gx2gl_done.flag";
static FILE *g_report_log;

struct RunnerState {
    PiglitRunStats stats;
    FILE *case_log;
    bool exit_requested;
};

struct DemoVertex {
    GLfloat x;
    GLfloat y;
};

struct DemoRenderer {
    GLuint program;
    GLuint vao;
    GLuint vbo;
    OSTime fps_start;
    uint32_t fps_frames;
    uint32_t fps;
    bool ready;
};

static void report(const char *format, ...) {
    char buffer[1024];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OSReport("%s", buffer);
    if (g_report_log) {
        fputs(buffer, g_report_log);
        fflush(g_report_log);
    }
}

static const char *result_name(PiglitResult result) {
    switch (result) {
        case PIGLIT_RESULT_PASS:
            return "pass";
        case PIGLIT_RESULT_FAIL:
            return "fail";
        case PIGLIT_RESULT_SKIP:
            return "skip";
    }
    return "fail";
}

static void write_summary(const char *path, const char *status,
                          const PiglitRunStats &stats) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }

    fprintf(file, "status=%s\n", status);
    fprintf(file, "suite=piglit\n");
    fprintf(file, "total=%u\n", stats.total);
    fprintf(file, "pass=%u\n", stats.pass);
    fprintf(file, "fail=%u\n", stats.fail);
    fprintf(file, "skip=%u\n", stats.skip);
    fclose(file);
}

static void write_progress(const RunnerState *state, const char *current) {
    FILE *file = fopen(kProgressPath, "wb");
    if (!file) {
        return;
    }

    fprintf(file, "status=running\n");
    fprintf(file, "suite=piglit\n");
    fprintf(file, "total=%u\n", state->stats.total);
    fprintf(file, "pass=%u\n", state->stats.pass);
    fprintf(file, "fail=%u\n", state->stats.fail);
    fprintf(file, "skip=%u\n", state->stats.skip);
    if (current && current[0]) {
        fprintf(file, "current=%s\n", current);
    }
    fclose(file);
}

static bool is_manifest_shader_case(const char *name) {
    return name && strstr(name, ".shader_test") != NULL;
}

static void begin_case(const char *name, void *user_data) {
    RunnerState *state = static_cast<RunnerState *>(user_data);
    if (!is_manifest_shader_case(name)) {
        return;
    }
    if (state->case_log) {
        fprintf(state->case_log, "begin\tname=%s\n", name);
        fflush(state->case_log);
    }
    write_progress(state, name);
}

static void record_result(const char *name, PiglitResult result,
                          const char *detail, void *user_data) {
    RunnerState *state = static_cast<RunnerState *>(user_data);

    ++state->stats.total;
    switch (result) {
        case PIGLIT_RESULT_PASS:
            ++state->stats.pass;
            break;
        case PIGLIT_RESULT_FAIL:
            ++state->stats.fail;
            break;
        case PIGLIT_RESULT_SKIP:
            ++state->stats.skip;
            break;
    }

    const bool log_case =
        result != PIGLIT_RESULT_SKIP || is_manifest_shader_case(name);
    if (state->case_log && log_case) {
        fprintf(state->case_log, "result=%s\tname=%s\tdetail=%s\n",
                result_name(result), name, detail ? detail : "");
        if (result != PIGLIT_RESULT_SKIP ||
            (state->stats.total & 63u) == 0u) {
            fflush(state->case_log);
        }
    }
    if (result != PIGLIT_RESULT_SKIP ||
        (state->stats.total & 63u) == 0u) {
        write_progress(state, NULL);
    }
    if (result == PIGLIT_RESULT_FAIL) {
        report("[%s] %s\n", result_name(result), name);
    }
}

static int process_proc_ui(void *user_data) {
    RunnerState *state = static_cast<RunnerState *>(user_data);
    if (WHBProcIsRunning()) {
        return 1;
    }
    state->exit_requested = true;
    return 0;
}

static void write_done_flag(void) {
    FILE *file = fopen(kDonePath, "wb");
    if (file) {
        fputs("done\n", file);
        fclose(file);
    }
}

static void clear_runner_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static GLuint compile_demo_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;
    char log[512];

    if (!shader) {
        report("PIGLIT: demo shader allocation failed\n");
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) {
        return shader;
    }

    log[0] = '\0';
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    report("PIGLIT: demo shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

static bool init_demo_renderer(DemoRenderer *renderer) {
    static const char *kVertexShader =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPosition;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "}\n";
    static const char *kFragmentShader =
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
        "}\n";
    static const DemoVertex kTriangle[] = {
        {-0.86f, -0.72f},
        { 0.86f, -0.72f},
        { 0.00f,  0.78f},
    };
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLint link_status = GL_FALSE;
    char log[512];

    if (!renderer) {
        return false;
    }
    if (renderer->ready) {
        return true;
    }

    clear_runner_gl_errors();
    memset(renderer, 0, sizeof(*renderer));

    vertex_shader = compile_demo_shader(GL_VERTEX_SHADER, kVertexShader);
    fragment_shader = compile_demo_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vertex_shader || !fragment_shader) {
        goto fail;
    }

    renderer->program = glCreateProgram();
    glAttachShader(renderer->program, vertex_shader);
    glAttachShader(renderer->program, fragment_shader);
    glLinkProgram(renderer->program);
    glGetProgramiv(renderer->program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        log[0] = '\0';
        glGetProgramInfoLog(renderer->program, sizeof(log), NULL, log);
        report("PIGLIT: demo program link failed: %s\n", log);
        goto fail;
    }

    glGenVertexArrays(1, &renderer->vao);
    glGenBuffers(1, &renderer->vbo);
    glBindVertexArray(renderer->vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kTriangle), kTriangle,
                 GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(DemoVertex),
                          (const GLvoid *)0);
    glEnableVertexAttribArray(0);
    if (glGetError() != GL_NO_ERROR) {
        report("PIGLIT: demo GL setup produced an error\n");
        goto fail;
    }

    renderer->fps_start = OSGetTime();
    renderer->fps = 0;
    renderer->ready = true;

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return true;

fail:
    if (vertex_shader) glDeleteShader(vertex_shader);
    if (fragment_shader) glDeleteShader(fragment_shader);
    if (renderer->vbo) glDeleteBuffers(1, &renderer->vbo);
    if (renderer->vao) glDeleteVertexArrays(1, &renderer->vao);
    if (renderer->program) glDeleteProgram(renderer->program);
    memset(renderer, 0, sizeof(*renderer));
    return false;
}

static void shutdown_demo_renderer(DemoRenderer *renderer) {
    if (!renderer) return;
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (renderer->vbo) glDeleteBuffers(1, &renderer->vbo);
    if (renderer->vao) glDeleteVertexArrays(1, &renderer->vao);
    if (renderer->program) glDeleteProgram(renderer->program);
    memset(renderer, 0, sizeof(*renderer));
}

static void update_demo_fps(DemoRenderer *renderer) {
    OSTime now;
    uint64_t elapsed_ms;

    if (!renderer || !renderer->ready) return;
    ++renderer->fps_frames;
    now = OSGetTime();
    elapsed_ms = OSTicksToMilliseconds(now - renderer->fps_start);
    if (elapsed_ms >= 500) {
        renderer->fps =
            (uint32_t)(((uint64_t)renderer->fps_frames * 1000ull +
                        elapsed_ms / 2ull) / elapsed_ms);
        renderer->fps_frames = 0;
        renderer->fps_start = now;
    }
}

static void draw_demo_target(DemoRenderer *renderer, GLboolean use_drc,
                             GLsizei width, GLsizei height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    gl_framebuffer_set_default_target_drc(use_drc);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_SAMPLE_MASK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.18f, 0.42f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!renderer || !renderer->ready) {
        glFinish();
        return;
    }

    glUseProgram(renderer->program);
    glBindVertexArray(renderer->vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
}

static void present_demo_frame(DemoRenderer *renderer) {
    GX2ColorBuffer *tv = WHBGfxGetTVColourBuffer();
    GX2ColorBuffer *drc = WHBGfxGetDRCColourBuffer();

    if (tv && tv->surface.image) {
        draw_demo_target(renderer, GL_FALSE,
                         (GLsizei)tv->surface.width,
                         (GLsizei)tv->surface.height);
    } else if (drc && drc->surface.image) {
        draw_demo_target(renderer, GL_TRUE,
                         (GLsizei)drc->surface.width,
                         (GLsizei)drc->surface.height);
    }
    GX2GL_Present();
}

static void show_final_result(const char *status, const RunnerState &state) {
    DemoRenderer demo = {};

    report("PIGLIT: status=%s total=%u pass=%u fail=%u skip=%u\n",
           status, state.stats.total, state.stats.pass, state.stats.fail,
           state.stats.skip);
    report("PIGLIT: wrote gx2gl_results.txt and gx2gl_piglit.txt to SD\n");
    report("PIGLIT: drawing GL blue clear with white triangle; HOME/menu exit uses WHBProc\n");

    if (!g_gl_context || !init_demo_renderer(&demo)) {
        report("PIGLIT: GL demo renderer could not start\n");
    }

    while (WHBProcIsRunning()) {
        update_demo_fps(&demo);
        present_demo_frame(&demo);
    }

    shutdown_demo_renderer(&demo);
}

} // namespace

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    remove(kResultsPath);
    remove(kProgressPath);
    remove(kCaseLogPath);
    remove(kReportLogPath);
    remove(kDonePath);

    WHBProcInit();
    g_report_log = fopen(kReportLogPath, "wb");
    report("PIGLIT: booting gx2gl hardware runner\n");
    GX2GL_Context context = GX2GL_CreateContext();

    if (!context) {
        RunnerState failed_state = {};
        PiglitRunStats empty = {};
        report("PIGLIT: context creation failed\n");
        write_summary(kResultsPath, "failed", empty);
        write_done_flag();
        show_final_result("failed", failed_state);
        if (g_report_log) {
            fclose(g_report_log);
            g_report_log = NULL;
        }
        WHBProcShutdown();
        return 1;
    }

    report("PIGLIT: gx2gl OpenGL 3.3 runner\n");
    RunnerState state = {};
    state.case_log = fopen(kCaseLogPath, "wb");
    write_summary(kProgressPath, "running", state.stats);

    const PiglitRunStats returned =
        run_piglit_tests(report, begin_case, record_result, process_proc_ui,
                         &state);
    if (returned.total != state.stats.total ||
        returned.pass != state.stats.pass ||
        returned.fail != state.stats.fail ||
        returned.skip != state.stats.skip) {
        report("PIGLIT: internal result accounting mismatch\n");
        ++state.stats.fail;
        ++state.stats.total;
    }

    if (state.case_log) {
        fclose(state.case_log);
    }
    const char *status = state.stats.fail > 0
                             ? "failed"
                             : (state.exit_requested ? "cancelled" : "complete");
    write_summary(kResultsPath, status, state.stats);
    write_summary(kProgressPath, status, state.stats);
    write_done_flag();

    show_final_result(status, state);
    if (g_report_log) {
        fclose(g_report_log);
        g_report_log = NULL;
    }

    GX2GL_DeleteContext(context);
    WHBProcShutdown();
    return state.exit_requested || state.stats.fail == 0 ? 0 : 1;
}
