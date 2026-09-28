#ifndef SM3DW_WIIU_AUDIO_H
#define SM3DW_WIIU_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

bool wiiu_audio_init(uint32_t sample_rate);
void wiiu_audio_submit_stereo(const int16_t* samples, uint32_t frame_count);
void wiiu_audio_shutdown(void);

#endif
