#ifndef CAFE_SURFACE_BRIDGE_H
#define CAFE_SURFACE_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host-endian words for the 0x74-byte guest GX2Surface. Unchanged on rejection. */
bool cafe_surface_calculate(uint32_t words[29]);
#ifdef __cplusplus
}
#endif
#endif
