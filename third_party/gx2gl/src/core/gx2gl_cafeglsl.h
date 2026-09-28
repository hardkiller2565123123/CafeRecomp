#ifndef GX2GL_CAFEGLSL_H
#define GX2GL_CAFEGLSL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <gx2/shaders.h>

typedef enum {
    GX2GL_CAFEGLSL_FLAG_NONE = 0,
    GX2GL_CAFEGLSL_FLAG_GENERATE_DISASSEMBLY = 1 << 0,
} gx2gl_cafeglsl_flag_t;

bool gx2gl_cafeglsl_init(void);
void gx2gl_cafeglsl_shutdown(void);
bool gx2gl_cafeglsl_is_available(void);

GX2VertexShader *gx2gl_cafeglsl_compile_vertex_shader(const char *shader_source,
                                                      char *info_log_out,
                                                      int info_log_max_length,
                                                      gx2gl_cafeglsl_flag_t flags);
GX2PixelShader *gx2gl_cafeglsl_compile_pixel_shader(const char *shader_source,
                                                    char *info_log_out,
                                                    int info_log_max_length,
                                                    gx2gl_cafeglsl_flag_t flags);
void gx2gl_cafeglsl_free_vertex_shader(GX2VertexShader *shader);
void gx2gl_cafeglsl_free_pixel_shader(GX2PixelShader *shader);

#ifdef __cplusplus
}
#endif

#endif
