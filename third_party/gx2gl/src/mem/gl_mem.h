#ifndef GL33_MEM_H
#define GL33_MEM_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    GL_MEM_TYPE_MEM1 = 0,
    GL_MEM_TYPE_MEM2 = 1
} gl_mem_type_t;

void gl_mem_init(void);
void gl_mem_shutdown(void);

void* gl_mem_alloc(gl_mem_type_t type, size_t size, uint32_t align);
void gl_mem_free(gl_mem_type_t type, void* ptr);

#endif
