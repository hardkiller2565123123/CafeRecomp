#include "piglit_lite.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gl/gl.h"
#include "gx2gl/present.h"
#include "piglit_manifest.h"

namespace {

struct PiglitUniform {
    const char *name;
    enum Type {
        Int,
        Vec4,
    } type;
    GLint int_value;
    GLfloat vec4_value[4];
};

struct PiglitShaderCase {
    const char *name;
    const char *piglit_case;
    const char *fragment_source;
    PiglitUniform uniforms[2];
    int uniform_count;
    GLfloat expected[4];
};

static const char *kFullscreenVertexShader =
    "#version 330 core\n"
    "layout(location = 0) in vec2 aPosition;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

static const PiglitShaderCase kShaderCases[] = {
    {
        "shaders/ssa/fs-lost-copy-problem",
        "tests/shaders/ssa/fs-lost-copy-problem.shader_test",
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "uniform int count;\n"
        "void main() {\n"
        "    int j = 0;\n"
        "    for (int i = 0; i <= count; ++i) {\n"
        "        j = i;\n"
        "    }\n"
        "    if (j == 6)\n"
        "        FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
        "    else\n"
        "        FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "}\n",
        {{"count", PiglitUniform::Int, 6, {0.0f, 0.0f, 0.0f, 0.0f}}},
        1,
        {0.0f, 1.0f, 0.0f, 1.0f},
    },
    {
        "shaders/ssa/fs-swap-problem",
        "tests/shaders/ssa/fs-swap-problem.shader_test",
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "uniform bool f;\n"
        "void main() {\n"
        "    FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
        "    do {\n"
        "        FragColor.xy = FragColor.yx;\n"
        "    } while (f);\n"
        "}\n",
        {{"f", PiglitUniform::Int, 0, {0.0f, 0.0f, 0.0f, 0.0f}}},
        1,
        {0.0f, 1.0f, 0.0f, 1.0f},
    },
    {
        "shaders/ssa/fs-while-loop-rotate-value",
        "tests/shaders/ssa/fs-while-loop-rotate-value.shader_test",
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "uniform int count;\n"
        "uniform vec4 init_val;\n"
        "void main() {\n"
        "    vec4 out_val = init_val;\n"
        "    int i = 0;\n"
        "    while (i++ < count) {\n"
        "        out_val = out_val.yzwx;\n"
        "    }\n"
        "    FragColor = vec4(out_val.xyz, 1.0);\n"
        "}\n",
        {{"count", PiglitUniform::Int, 3, {0.0f, 0.0f, 0.0f, 0.0f}},
         {"init_val", PiglitUniform::Vec4, 0, {0.25f, 0.5f, 0.75f, 1.0f}}},
        2,
        {1.0f, 0.25f, 0.5f, 1.0f},
    },
};

static void clear_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static bool check_no_error(PiglitReportFunc report, const char *label) {
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) {
        return true;
    }

    report("[FAIL] Piglit %s produced GL error 0x%04X\n", label, error);
    return false;
}

static GLuint compile_shader(PiglitReportFunc report, GLenum type,
                             const char *source, const char *case_name) {
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;
    char info_log[512];
    GLsizei info_length = 0;

    if (shader == 0 || !check_no_error(report, "glCreateShader")) {
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    if (!check_no_error(report, "glShaderSource")) {
        glDeleteShader(shader);
        return 0;
    }

    glCompileShader(shader);
    if (!check_no_error(report, "glCompileShader")) {
        glDeleteShader(shader);
        return 0;
    }

    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!check_no_error(report, "glGetShaderiv(GL_COMPILE_STATUS)")) {
        glDeleteShader(shader);
        return 0;
    }
    if (status == GL_TRUE) {
        return shader;
    }

    memset(info_log, 0, sizeof(info_log));
    glGetShaderInfoLog(shader, sizeof(info_log), &info_length, info_log);
    report("[FAIL] Piglit %s shader compile failed: %s\n", case_name,
           info_log);
    glDeleteShader(shader);
    return 0;
}

static GLuint link_program(PiglitReportFunc report, GLuint vertex_shader,
                           GLuint fragment_shader, const char *case_name) {
    GLuint program = glCreateProgram();
    GLint status = GL_FALSE;
    char info_log[512];
    GLsizei info_length = 0;

    if (program == 0 || !check_no_error(report, "glCreateProgram")) {
        return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    if (!check_no_error(report, "glAttachShader")) {
        glDeleteProgram(program);
        return 0;
    }

    glLinkProgram(program);
    if (!check_no_error(report, "glLinkProgram")) {
        glDeleteProgram(program);
        return 0;
    }

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!check_no_error(report, "glGetProgramiv(GL_LINK_STATUS)")) {
        glDeleteProgram(program);
        return 0;
    }
    if (status == GL_TRUE) {
        return program;
    }

    memset(info_log, 0, sizeof(info_log));
    glGetProgramInfoLog(program, sizeof(info_log), &info_length, info_log);
    report("[FAIL] Piglit %s program link failed: %s\n", case_name,
           info_log);
    glDeleteProgram(program);
    return 0;
}

static bool probe_all_rgba(PiglitReportFunc report,
                           const PiglitShaderCase &test_case,
                           int width, int height) {
    GLubyte pixels[4 * 4 * 4];
    const int tolerance = 3;

    memset(pixels, 0, sizeof(pixels));
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (!check_no_error(report, "glReadPixels")) {
        return false;
    }

    for (int i = 0; i < width * height; ++i) {
        const GLubyte *p = pixels + i * 4;
        for (int c = 0; c < 4; ++c) {
            int expected = (int)(test_case.expected[c] * 255.0f + 0.5f);
            int diff = (int)p[c] - expected;
            if (diff < 0) {
                diff = -diff;
            }
            if (diff > tolerance) {
                report("[FAIL] Piglit %s probe pixel %d returned {%u,%u,%u,%u}, expected {%d,%d,%d,%d}\n",
                       test_case.name, i, p[0], p[1], p[2], p[3],
                       (int)(test_case.expected[0] * 255.0f + 0.5f),
                       (int)(test_case.expected[1] * 255.0f + 0.5f),
                       (int)(test_case.expected[2] * 255.0f + 0.5f),
                       (int)(test_case.expected[3] * 255.0f + 0.5f));
                return false;
            }
        }
    }

    report("[PASS] Piglit %s matched probe all rgba (%s).\n",
           test_case.name, test_case.piglit_case);
    return true;
}

static bool run_shader_case(PiglitReportFunc report,
                            const PiglitShaderCase &test_case) {
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    bool ok = false;

    clear_gl_errors();

    vertex_shader = compile_shader(report, GL_VERTEX_SHADER,
                                   kFullscreenVertexShader, test_case.name);
    fragment_shader = compile_shader(report, GL_FRAGMENT_SHADER,
                                     test_case.fragment_source,
                                     test_case.name);
    if (vertex_shader == 0 || fragment_shader == 0) {
        goto cleanup;
    }

    program = link_program(report, vertex_shader, fragment_shader,
                           test_case.name);
    if (program == 0) {
        goto cleanup;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    if (!check_no_error(report, "FBO setup")) {
        goto cleanup;
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        report("[FAIL] Piglit %s framebuffer was incomplete.\n",
               test_case.name);
        goto cleanup;
    }

    glViewport(0, 0, 4, 4);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    for (int i = 0; i < test_case.uniform_count; ++i) {
        const PiglitUniform &uniform = test_case.uniforms[i];
        GLint location = glGetUniformLocation(program, uniform.name);
        if (location < 0) {
            report("[FAIL] Piglit %s missing uniform '%s'.\n",
                   test_case.name, uniform.name);
            goto cleanup;
        }
        if (uniform.type == PiglitUniform::Int) {
            glUniform1i(location, uniform.int_value);
        } else {
            glUniform4fv(location, 1, uniform.vec4_value);
        }
    }
    if (!check_no_error(report, "uniform setup")) {
        goto cleanup;
    }

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(0);
    if (!check_no_error(report, "vertex setup")) {
        goto cleanup;
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!check_no_error(report, "glDrawArrays")) {
        goto cleanup;
    }
    glFinish();
    ok = probe_all_rgba(report, test_case, 4, 4);

cleanup:
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
    }
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
    }
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
    }
    if (texture != 0) {
        glDeleteTextures(1, &texture);
    }
    if (program != 0) {
        glDeleteProgram(program);
    }
    if (vertex_shader != 0) {
        glDeleteShader(vertex_shader);
    }
    if (fragment_shader != 0) {
        glDeleteShader(fragment_shader);
    }

    return ok;
}

static bool expect_error(PiglitReportFunc report, const char *operation,
                         GLenum expected);

static bool run_unqualified_varying_link(PiglitReportFunc report) {
    static const char *vertex_source =
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPosition;\n"
        "out vec2 vTexCoord;\n"
        "void main() {\n"
        "    vTexCoord = aPosition * 0.5 + 0.5;\n"
        "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "}\n";
    static const char *fragment_source =
        "#version 330 core\n"
        "in vec2 vTexCoord;\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "void main() { FragColor = vec4(vTexCoord, 0.0, 1.0); }\n";
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;
    bool passed = false;

    clear_gl_errors();
    vertex_shader = compile_shader(report, GL_VERTEX_SHADER, vertex_source,
                                   "spec/glsl-1.30/link-name-varying");
    fragment_shader = compile_shader(report, GL_FRAGMENT_SHADER,
                                     fragment_source,
                                     "spec/glsl-1.30/link-name-varying");
    if (!vertex_shader || !fragment_shader) goto cleanup;

    program = link_program(report, vertex_shader, fragment_shader,
                           "spec/glsl-1.30/link-name-varying");
    passed = program != 0 && expect_error(report, "link-name-varying",
                                          GL_NO_ERROR);

cleanup:
    if (program) glDeleteProgram(program);
    if (vertex_shader) glDeleteShader(vertex_shader);
    if (fragment_shader) glDeleteShader(fragment_shader);
    return passed;
}

static bool pixel_matches(const GLubyte *pixel, GLubyte r, GLubyte g,
                          GLubyte b, GLubyte a) {
    return pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == a;
}

static bool run_framebuffer_texture_layer_2d_array(PiglitReportFunc report) {
    GLubyte initial[2 * 4 * 4 * 4];
    GLubyte pixel[4] = {0, 0, 0, 0};
    GLuint texture = 0;
    GLuint fbo = 0;
    GLint layer = -1;
    bool passed = false;

    clear_gl_errors();
    for (int i = 0; i < 4 * 4; ++i) {
        initial[i * 4 + 0] = 255;
        initial[i * 4 + 1] = 0;
        initial[i * 4 + 2] = 0;
        initial[i * 4 + 3] = 255;
    }
    for (int i = 4 * 4; i < 2 * 4 * 4; ++i) {
        initial[i * 4 + 0] = 0;
        initial[i * 4 + 1] = 0;
        initial[i * 4 + 2] = 255;
        initial[i * 4 + 3] = 255;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 4, 4, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, initial);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture,
                              0, 1);
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, &layer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        layer != 1 || !expect_error(report, "2d-array layer attach",
                                    GL_NO_ERROR)) {
        goto cleanup;
    }

    glViewport(0, 0, 4, 4);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_error(report, "2d-array layer clear/read", GL_NO_ERROR) ||
        !pixel_matches(pixel, 0, 255, 0, 255)) {
        report("[FAIL] Piglit framebuffer layer 1 read {%u,%u,%u,%u}.\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
        goto cleanup;
    }

    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture,
                              0, 0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    passed = expect_error(report, "2d-array layer isolation", GL_NO_ERROR) &&
             pixel_matches(pixel, 255, 0, 0, 255);
    if (!passed) {
        report("[FAIL] Piglit framebuffer layer 0 read {%u,%u,%u,%u}.\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
    }

cleanup:
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (texture) glDeleteTextures(1, &texture);
    return passed;
}

static bool run_clear_buffer_color(PiglitReportFunc report) {
    static const GLfloat clear_attachment1[4] =
        {1.0f, 0.25f, 0.0f, 1.0f};
    static const GLfloat masked_clear[4] =
        {0.0f, 0.5f, 1.0f, 0.25f};
    static const GLfloat invalid_clear[4] =
        {0.0f, 0.0f, 0.0f, 0.0f};
    static const GLuint invalid_uint_clear[4] = {0, 0, 0, 0};
    GLubyte initial0[4 * 4 * 4];
    GLubyte initial1[4 * 4 * 4];
    GLubyte pixel[4] = {0, 0, 0, 0};
    GLuint textures[2] = {0, 0};
    GLuint fbo = 0;
    GLenum draw_buffers[2] = {GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT0};
    GLfloat depth = 1.0f;
    bool passed = false;

    clear_gl_errors();
    for (int i = 0; i < 4 * 4; ++i) {
        initial0[i * 4 + 0] = 0;
        initial0[i * 4 + 1] = 0;
        initial0[i * 4 + 2] = 255;
        initial0[i * 4 + 3] = 255;
        initial1[i * 4 + 0] = 0;
        initial1[i * 4 + 1] = 0;
        initial1[i * 4 + 2] = 0;
        initial1[i * 4 + 3] = 255;
    }

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(2, textures);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, i == 0 ? initial0 : initial1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                               GL_TEXTURE_2D, textures[i], 0);
    }
    glDrawBuffers(2, draw_buffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        !expect_error(report, "clearbuffer FBO setup", GL_NO_ERROR)) {
        goto cleanup;
    }

    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearBufferfv(GL_COLOR, 0, clear_attachment1);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_error(report, "glClearBufferfv selected draw buffer",
                      GL_NO_ERROR) ||
        !pixel_matches(pixel, 255, 64, 0, 255)) {
        report("[FAIL] Piglit glClearBufferfv draw buffer 0 read {%u,%u,%u,%u}.\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
        goto cleanup;
    }

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_error(report, "glClearBufferfv untouched attachment",
                      GL_NO_ERROR) ||
        !pixel_matches(pixel, 0, 0, 255, 255)) {
        report("[FAIL] Piglit glClearBufferfv touched wrong attachment {%u,%u,%u,%u}.\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
        goto cleanup;
    }

    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    glClearBufferfv(GL_COLOR, 0, masked_clear);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_error(report, "glClearBufferfv color mask", GL_NO_ERROR) ||
        !pixel_matches(pixel, 255, 128, 0, 64)) {
        report("[FAIL] Piglit glClearBufferfv masked read {%u,%u,%u,%u}.\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
        goto cleanup;
    }

    glClearBufferfv(GL_COLOR, -1, invalid_clear);
    if (!expect_error(report, "glClearBufferfv negative color drawbuffer",
                      GL_INVALID_VALUE)) {
        goto cleanup;
    }

    glClearBufferfv(GL_DEPTH, 1, &depth);
    if (!expect_error(report, "glClearBufferfv depth drawbuffer",
                      GL_INVALID_VALUE)) {
        goto cleanup;
    }

    glClearBufferuiv(GL_DEPTH, 0, invalid_uint_clear);
    passed = expect_error(report, "glClearBufferuiv depth enum",
                          GL_INVALID_ENUM);

cleanup:
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (textures[0] || textures[1]) glDeleteTextures(2, textures);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    return passed;
}

/*
 * The API cases below are adapted from Piglit's
 * tests/spec/arb_texture_multisample tests. Piglit's MIT license applies.
 * Copyright 2013 Intel Corporation and Piglit contributors.
 */

static bool expect_error(PiglitReportFunc report, const char *operation,
                         GLenum expected) {
    const GLenum actual = glGetError();
    if (actual == expected) {
        return true;
    }
    report("[FAIL] Piglit %s returned 0x%04X, expected 0x%04X.\n",
           operation, actual, expected);
    return false;
}

static bool run_multisample_minmax(PiglitReportFunc report) {
    const GLenum names[] = {
        GL_MAX_SAMPLE_MASK_WORDS,
        GL_MAX_COLOR_TEXTURE_SAMPLES,
        GL_MAX_DEPTH_TEXTURE_SAMPLES,
        GL_MAX_INTEGER_SAMPLES,
    };

    clear_gl_errors();
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        GLint value = 0;
        glGetIntegerv(names[i], &value);
        if (!expect_error(report, "arb_texture_multisample/minmax",
                          GL_NO_ERROR) || value < 1) {
            report("[FAIL] Piglit minmax token 0x%04X returned %d.\n",
                   names[i], value);
            return false;
        }
    }
    return true;
}

static bool run_multisample_sample_mask(PiglitReportFunc report) {
    GLint enabled = GL_TRUE;

    clear_gl_errors();
    if (glIsEnabled(GL_SAMPLE_MASK) != GL_FALSE ||
        !expect_error(report, "glIsEnabled(GL_SAMPLE_MASK)", GL_NO_ERROR)) {
        return false;
    }
    glGetIntegerv(GL_SAMPLE_MASK, &enabled);
    return expect_error(report, "glGetIntegerv(GL_SAMPLE_MASK)", GL_NO_ERROR) &&
           enabled == GL_FALSE;
}

static bool run_multisample_sample_mask_value(PiglitReportFunc report) {
    GLint words = 0;

    clear_gl_errors();
    glGetIntegerv(GL_MAX_SAMPLE_MASK_WORDS, &words);
    if (!expect_error(report, "GL_MAX_SAMPLE_MASK_WORDS", GL_NO_ERROR) ||
        words < 1) {
        return false;
    }
    for (GLint i = 0; i < words; ++i) {
        GLint mask = 0;
        glGetIntegeri_v(GL_SAMPLE_MASK_VALUE, (GLuint)i, &mask);
        if (!expect_error(report, "glGetIntegeri_v(GL_SAMPLE_MASK_VALUE)",
                          GL_NO_ERROR) || (GLuint)mask != ~0u) {
            return false;
        }
    }

    GLint ignored = 0;
    glGetIntegeri_v(GL_SAMPLE_MASK_VALUE, (GLuint)words, &ignored);
    return expect_error(report, "out-of-range GL_SAMPLE_MASK_VALUE",
                        GL_INVALID_VALUE);
}

static bool run_multisample_texstate(PiglitReportFunc report) {
    GLuint texture = 0;
    GLint samples = -1;
    GLint fixed = GL_FALSE;

    clear_gl_errors();
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_SAMPLES, &samples);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                             GL_TEXTURE_FIXED_SAMPLE_LOCATIONS, &fixed);
    const bool passed = expect_error(report, "arb_texture_multisample/texstate",
                                     GL_NO_ERROR) &&
                        samples == 0 && fixed == GL_TRUE;
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
    return passed;
}

static bool run_multisample_teximage_2d(PiglitReportFunc report) {
    GLuint textures[2] = {0, 0};
    GLint max_samples = 0;
    GLint actual_samples = 0;

    clear_gl_errors();
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &max_samples);
    glGenTextures(2, textures);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textures[0]);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, max_samples, GL_RGB,
                            64, 64, GL_FALSE);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                             GL_TEXTURE_SAMPLES, &actual_samples);
    bool passed = expect_error(report, "2D multisample image", GL_NO_ERROR) &&
                  actual_samples >= max_samples;

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, textures[1]);
    glTexImage2DMultisample(GL_PROXY_TEXTURE_2D_MULTISAMPLE, max_samples,
                            GL_RGB, 64, 64, GL_FALSE);
    passed = expect_error(report, "2D multisample proxy", GL_NO_ERROR) && passed;

    glTexImage2DMultisample(GL_TEXTURE_2D, 4, GL_RGB, 64, 64, GL_FALSE);
    passed = expect_error(report, "2D multisample invalid target",
                          GL_INVALID_ENUM) && passed;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    glDeleteTextures(2, textures);
    return passed;
}

static bool run_multisample_teximage_3d(PiglitReportFunc report) {
    GLuint textures[2] = {0, 0};
    GLint max_samples = 0;
    GLint depth = 0;

    clear_gl_errors();
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &max_samples);
    glGenTextures(2, textures);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, textures[0]);
    glTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, max_samples,
                            GL_RGB, 64, 64, 2, GL_FALSE);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0,
                             GL_TEXTURE_DEPTH, &depth);
    bool passed = expect_error(report, "array multisample image", GL_NO_ERROR) &&
                  depth == 2;

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, textures[1]);
    glTexImage3DMultisample(GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY, max_samples,
                            GL_RGB, 64, 64, 2, GL_FALSE);
    passed = expect_error(report, "array multisample proxy", GL_NO_ERROR) &&
             passed;

    glTexImage3DMultisample(GL_TEXTURE_2D, max_samples, GL_RGB,
                            64, 64, 2, GL_FALSE);
    passed = expect_error(report, "array multisample invalid target",
                          GL_INVALID_ENUM) && passed;
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0);
    glDeleteTextures(2, textures);
    return passed;
}

static GLuint make_ms_texture_2d(GLsizei samples, GLboolean fixed) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA,
                            64, 64, fixed);
    return texture;
}

static bool run_multisample_errors(PiglitReportFunc report) {
    GLuint fbo = 0;
    GLuint textures[2] = {0, 0};
    GLint max_samples = 0;

    clear_gl_errors();
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &max_samples);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(2, textures);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, textures[0]);
    glTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, max_samples,
                            GL_RGBA, 64, 64, 2, GL_TRUE);
    bool passed = expect_error(report, "multisample error setup", GL_NO_ERROR);

    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              textures[0], 0, -1);
    passed = expect_error(report, "negative framebuffer layer",
                          GL_INVALID_VALUE) && passed;

    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, textures[1]);
    glTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0, GL_RGBA,
                            64, 64, 2, GL_TRUE);
    passed = expect_error(report, "zero multisample count",
                          GL_INVALID_VALUE) && passed;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(2, textures);
    return passed;
}

static bool check_fbo_status(PiglitReportFunc report, const char *name,
                             GLenum expected) {
    const GLenum actual = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (actual == expected &&
        expect_error(report, name, GL_NO_ERROR)) {
        return true;
    }
    report("[FAIL] Piglit %s framebuffer status 0x%04X, expected 0x%04X.\n",
           name, actual, expected);
    return false;
}

static bool run_multisample_fb_completeness(PiglitReportFunc report) {
    GLuint fbo = 0;
    GLuint textures[2] = {0, 0};
    GLuint renderbuffer = 0;
    bool passed = true;

    clear_gl_errors();
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    textures[0] = make_ms_texture_2d(4, GL_TRUE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D_MULTISAMPLE, textures[0], 0);
    passed = check_fbo_status(report, "single_msaa_color",
                              GL_FRAMEBUFFER_COMPLETE) && passed;
    glDeleteTextures(1, &textures[0]);

    textures[0] = make_ms_texture_2d(4, GL_TRUE);
    textures[1] = make_ms_texture_2d(4, GL_FALSE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D_MULTISAMPLE, textures[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                           GL_TEXTURE_2D_MULTISAMPLE, textures[1], 0);
    passed = check_fbo_status(report, "mix_fixedmode",
                              GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE) && passed;
    glDeleteTextures(2, textures);

    textures[0] = make_ms_texture_2d(4, GL_FALSE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D_MULTISAMPLE, textures[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                           GL_TEXTURE_2D_MULTISAMPLE, 0, 0);
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 64, 64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                              GL_RENDERBUFFER, renderbuffer);
    passed = check_fbo_status(report, "mix_fixedmode_with_renderbuffer",
                              GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE) && passed;
    glDeleteTextures(1, &textures[0]);

    textures[0] = make_ms_texture_2d(4, GL_TRUE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D_MULTISAMPLE, textures[0], 0);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                              GL_RENDERBUFFER, renderbuffer);
    passed = check_fbo_status(report, "mixed_msaa_and_plain",
                              GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE) && passed;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glDeleteRenderbuffers(1, &renderbuffer);
    glDeleteTextures(1, &textures[0]);
    glDeleteFramebuffers(1, &fbo);
    return passed;
}

static bool run_multisample_sample_position(PiglitReportFunc report) {
    const GLsizei requested_counts[] = {1, 2, 4, 8};
    bool passed = true;

    clear_gl_errors();
    for (size_t test = 0;
         test < sizeof(requested_counts) / sizeof(requested_counts[0]); ++test) {
        GLuint fbo = 0;
        GLuint texture = make_ms_texture_2d(requested_counts[test], GL_TRUE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D_MULTISAMPLE, texture, 0);
        GLint samples = 0;
        glGetIntegerv(GL_SAMPLES, &samples);
        if (samples < requested_counts[test]) {
            passed = false;
        }
        for (GLint sample = 0; sample < samples; ++sample) {
            GLfloat position[2] = {-1.0f, -1.0f};
            glGetMultisamplefv(GL_SAMPLE_POSITION, (GLuint)sample, position);
            if (position[0] < 0.0f || position[0] > 1.0f ||
                position[1] < 0.0f || position[1] > 1.0f) {
                passed = false;
            }
        }
        passed = expect_error(report, "sample-position query", GL_NO_ERROR) &&
                 passed;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texture);
    }
    return passed;
}

static bool run_sample_mask_execution(PiglitReportFunc report,
                                      bool use_texture) {
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    static const char *fragment_source =
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "uniform vec4 color;\n"
        "void main() { FragColor = color; }\n";
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;
    GLuint ms_fbo = 0;
    GLuint ss_fbo = 0;
    GLuint ms_texture = 0;
    GLuint ms_renderbuffer = 0;
    GLuint ss_texture = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint color = -1;
    GLubyte pixel[4] = {0, 0, 0, 0};
    bool passed = false;

    clear_gl_errors();
    vertex_shader = compile_shader(report, GL_VERTEX_SHADER,
                                   kFullscreenVertexShader,
                                   "sample-mask-execution");
    fragment_shader = compile_shader(report, GL_FRAGMENT_SHADER,
                                     fragment_source,
                                     "sample-mask-execution");
    if (!vertex_shader || !fragment_shader) goto cleanup;
    program = link_program(report, vertex_shader, fragment_shader,
                           "sample-mask-execution");
    if (!program) goto cleanup;

    glGenFramebuffers(1, &ms_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
    if (use_texture) {
        ms_texture = make_ms_texture_2d(4, GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D_MULTISAMPLE, ms_texture, 0);
    } else {
        glGenRenderbuffers(1, &ms_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, ms_renderbuffer);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8,
                                         64, 64);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, ms_renderbuffer);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        report("[FAIL] Piglit sample-mask multisample FBO incomplete.\n");
        goto cleanup;
    }

    glGenFramebuffers(1, &ss_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ss_fbo);
    glGenTextures(1, &ss_texture);
    glBindTexture(GL_TEXTURE_2D, ss_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ss_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        !expect_error(report, "sample-mask FBO setup", GL_NO_ERROR)) {
        goto cleanup;
    }

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(0);

    glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
    glViewport(0, 0, 64, 64);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    color = glGetUniformLocation(program, "color");
    if (color < 0) goto cleanup;

    glEnable(GL_SAMPLE_MASK);
    glSampleMaski(0, 0x3u);
    glUniform4f(color, 1.0f, 0.0f, 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glSampleMaski(0, 0xCu);
    glUniform4f(color, 0.0f, 1.0f, 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_SAMPLE_MASK);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ms_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ss_fbo);
    glBlitFramebuffer(0, 0, 64, 64, 0, 0, 64, 64,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (!expect_error(report, "sample-mask resolve", GL_NO_ERROR)) {
        goto cleanup;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, ss_fbo);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_error(report, "sample-mask readback", GL_NO_ERROR)) {
        goto cleanup;
    }
    passed = pixel[0] >= 124 && pixel[0] <= 131 &&
             pixel[1] >= 124 && pixel[1] <= 131 &&
             pixel[2] <= 3 && pixel[3] >= 252;
    if (!passed) {
        report("[FAIL] Piglit sample-mask resolve returned {%u,%u,%u,%u}.\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
    }

cleanup:
    glDisable(GL_SAMPLE_MASK);
    glSampleMaski(0, ~0u);
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (ss_texture) glDeleteTextures(1, &ss_texture);
    if (ms_texture) glDeleteTextures(1, &ms_texture);
    if (ms_renderbuffer) glDeleteRenderbuffers(1, &ms_renderbuffer);
    if (ss_fbo) glDeleteFramebuffers(1, &ss_fbo);
    if (ms_fbo) glDeleteFramebuffers(1, &ms_fbo);
    if (program) glDeleteProgram(program);
    if (vertex_shader) glDeleteShader(vertex_shader);
    if (fragment_shader) glDeleteShader(fragment_shader);
    return passed;
}

static bool run_sample_mask_execution_renderbuffer(PiglitReportFunc report) {
    return run_sample_mask_execution(report, false);
}

static bool run_sample_mask_execution_texture(PiglitReportFunc report) {
    return run_sample_mask_execution(report, true);
}

static bool run_shadered_triangle_probe(PiglitReportFunc report) {
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    static const char *fragment_source =
        "#version 330 core\n"
        "layout(location = 0) out vec4 FragColor;\n"
        "void main() { FragColor = vec4(1.0); }\n";
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;
    GLuint fbo = 0;
    GLuint texture = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLubyte pixel[4] = {0, 0, 0, 0};
    bool passed = false;

    clear_gl_errors();
    vertex_shader = compile_shader(report, GL_VERTEX_SHADER,
                                   kFullscreenVertexShader,
                                   "draw/constant-white-triangle");
    fragment_shader = compile_shader(report, GL_FRAGMENT_SHADER,
                                     fragment_source,
                                     "draw/constant-white-triangle");
    if (!vertex_shader || !fragment_shader) goto cleanup;
    program = link_program(report, vertex_shader, fragment_shader,
                           "draw/constant-white-triangle");
    if (!program) goto cleanup;

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        !expect_error(report, "constant-white-triangle FBO setup",
                      GL_NO_ERROR)) {
        goto cleanup;
    }

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), 0);
    glEnableVertexAttribArray(0);

    glViewport(0, 0, 16, 16);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.18f, 0.42f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!expect_error(report, "constant-white-triangle draw/readback",
                      GL_NO_ERROR)) {
        goto cleanup;
    }

    passed = pixel[0] >= 252 && pixel[1] >= 252 &&
             pixel[2] >= 252 && pixel[3] >= 252;
    if (!passed) {
        report("[FAIL] Piglit draw/constant-white-triangle returned {%u,%u,%u,%u}.\n",
               pixel[0], pixel[1], pixel[2], pixel[3]);
    }

cleanup:
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (texture) glDeleteTextures(1, &texture);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (program) glDeleteProgram(program);
    if (vertex_shader) glDeleteShader(vertex_shader);
    if (fragment_shader) glDeleteShader(fragment_shader);
    return passed;
}

static bool run_get_core_profile_queries(PiglitReportFunc report) {
    GLint major = -1;
    GLint minor = -1;
    GLint profile = 0;
    GLint extension_count = -1;
    GLint old_viewport[4] = {0, 0, 0, 0};
    GLint64 viewport64[4] = {-1, -1, -1, -1};
    bool passed = true;

    clear_gl_errors();
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    passed = expect_error(report, "core profile version queries",
                          GL_NO_ERROR) &&
             major == 3 && minor == 3 &&
             (profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0 && passed;
    if (!passed) {
        report("[FAIL] Piglit get core profile returned %d.%d mask=0x%X.\n",
               major, minor, profile);
    }

    const char *version =
        reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *sl_version =
        reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    passed = expect_error(report, "core profile version strings",
                          GL_NO_ERROR) &&
             version && strncmp(version, "3.3", 3) == 0 &&
             sl_version && strncmp(sl_version, "3.30", 4) == 0 && passed;

    const GLubyte *legacy_extensions = glGetString(GL_EXTENSIONS);
    passed = expect_error(report, "core profile glGetString(GL_EXTENSIONS)",
                          GL_INVALID_ENUM) &&
             legacy_extensions == NULL && passed;

    glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
    passed = expect_error(report, "GL_NUM_EXTENSIONS", GL_NO_ERROR) &&
             extension_count > 0 && passed;
    if (extension_count > 0) {
        passed = glGetStringi(GL_EXTENSIONS, 0) != NULL &&
                 expect_error(report, "glGetStringi first extension",
                              GL_NO_ERROR) && passed;
        passed = glGetStringi(GL_EXTENSIONS, (GLuint)extension_count) == NULL &&
                 expect_error(report, "glGetStringi out of range",
                              GL_INVALID_VALUE) && passed;
    }

    glGetIntegerv(GL_VIEWPORT, old_viewport);
    glViewport(3, 5, 77, 88);
    glGetInteger64v(GL_VIEWPORT, viewport64);
    passed = expect_error(report, "glGetInteger64v(GL_VIEWPORT)",
                          GL_NO_ERROR) &&
             viewport64[0] == 3 && viewport64[1] == 5 &&
             viewport64[2] == 77 && viewport64[3] == 88 && passed;
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2],
               old_viewport[3]);

    return passed;
}

static bool run_gx2gl_context_ownership(PiglitReportFunc report) {
    clear_gl_errors();

    GX2GL_Context reference = GX2GL_CreateContext();
    if (!reference) {
        report("[FAIL] gx2gl could not acquire a second context reference.\n");
        return false;
    }

    GX2GL_DeleteContext(reference);
    return glGetString(GL_VERSION) != NULL &&
           expect_error(report, "context reference release", GL_NO_ERROR);
}

static bool run_gx2gl_swap_interval(PiglitReportFunc report) {
    const int original = GX2GL_GetSwapInterval();
    bool passed = true;

    passed = GX2GL_SetSwapInterval(0) == 0 &&
             GX2GL_GetSwapInterval() == 0 && passed;
    passed = GX2GL_SetSwapInterval(1) == 0 &&
             GX2GL_GetSwapInterval() == 1 && passed;
    passed = GX2GL_SetSwapInterval(-1) == -1 && passed;

    if (original == 0 || original == 1) {
        passed = GX2GL_SetSwapInterval(original) == 0 && passed;
    }
    if (!passed) {
        report("[FAIL] gx2gl swap interval did not round-trip through GX2.\n");
    }
    return passed;
}

struct PiglitApiCase {
    const char *name;
    bool (*run)(PiglitReportFunc report);
};

static const PiglitApiCase kApiCases[] = {
    {"gx2gl/integration/context-ownership", run_gx2gl_context_ownership},
    {"gx2gl/integration/swap-interval", run_gx2gl_swap_interval},
    {"spec/gl-3.3/get-core-profile-identity", run_get_core_profile_queries},
    {"spec/glsl-1.30/link-name-varying", run_unqualified_varying_link},
    {"spec/arb_framebuffer_object/framebuffer-texture-layer-2d-array",
     run_framebuffer_texture_layer_2d_array},
    {"spec/gl-3.0/clearbuffer-color", run_clear_buffer_color},
    {"draw/constant-white-triangle", run_shadered_triangle_probe},
    {"spec/arb_texture_multisample/minmax", run_multisample_minmax},
    {"spec/arb_texture_multisample/sample-mask", run_multisample_sample_mask},
    {"spec/arb_texture_multisample/sample-mask-value",
     run_multisample_sample_mask_value},
    {"spec/arb_texture_multisample/texstate", run_multisample_texstate},
    {"spec/arb_texture_multisample/teximage-2d-multisample",
     run_multisample_teximage_2d},
    {"spec/arb_texture_multisample/teximage-3d-multisample",
     run_multisample_teximage_3d},
    {"spec/arb_texture_multisample/errors", run_multisample_errors},
    {"spec/arb_texture_multisample/fb-completeness",
     run_multisample_fb_completeness},
    {"spec/arb_texture_multisample/sample-position",
     run_multisample_sample_position},
    {"spec/arb_texture_multisample/sample-mask-execution",
     run_sample_mask_execution_renderbuffer},
    {"spec/arb_texture_multisample/sample-mask-execution -tex",
     run_sample_mask_execution_texture},
};

} // namespace

PiglitRunStats run_piglit_tests(PiglitReportFunc report,
                                PiglitBeginFunc begin_func,
                                PiglitResultFunc result_func,
                                PiglitContinueFunc continue_func,
                                void *user_data) {
    PiglitRunStats stats = {};

    if (!report) {
        return stats;
    }

    for (size_t i = 0; i < sizeof(kApiCases) / sizeof(kApiCases[0]); ++i) {
        if (continue_func && !continue_func(user_data)) break;
        if (begin_func) {
            begin_func(kApiCases[i].name, user_data);
        }
        const bool passed = kApiCases[i].run(report);
        const PiglitResult result = passed ? PIGLIT_RESULT_PASS
                                           : PIGLIT_RESULT_FAIL;
        ++stats.total;
        if (passed) {
            ++stats.pass;
            report("[PASS] Piglit %s.\n", kApiCases[i].name);
        } else {
            ++stats.fail;
        }
        if (result_func) {
            result_func(kApiCases[i].name, result,
                        passed ? "passed" : "test failed", user_data);
        }
    }

    for (size_t i = 0; i < sizeof(kShaderCases) / sizeof(kShaderCases[0]); ++i) {
        if (continue_func && !continue_func(user_data)) break;
        if (begin_func) {
            begin_func(kShaderCases[i].name, user_data);
        }
        const bool passed = run_shader_case(report, kShaderCases[i]);
        const PiglitResult result = passed ? PIGLIT_RESULT_PASS
                                           : PIGLIT_RESULT_FAIL;

        ++stats.total;
        if (passed) {
            ++stats.pass;
        } else {
            ++stats.fail;
        }
        if (result_func) {
            result_func(kShaderCases[i].name, result,
                        passed ? "probe matched" : "test failed",
                        user_data);
        }
    }

    const PiglitRunStats manifest =
        run_piglit_manifest_tests(report, begin_func, result_func, continue_func,
                                  user_data);
    stats.total += manifest.total;
    stats.pass += manifest.pass;
    stats.fail += manifest.fail;
    stats.skip += manifest.skip;

    return stats;
}
