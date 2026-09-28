#ifndef BOTW_AUDIO_BUFFER_H
#define BOTW_AUDIO_BUFFER_H
#include <stdbool.h>
#include <stdint.h>

enum { BOTW_AUDIO_RING_FRAMES = 16384 };
/* Device-independent stereo jitter buffer. The caller serializes access. */
typedef struct {
    int16_t samples[BOTW_AUDIO_RING_FRAMES * 2];
    uint32_t read, count, reserve, fade_frames, fade_left;
    uint32_t underruns, dropped;
    bool playing;
    int16_t last[2];
} WiiUAudioBuffer;
void wiiu_audio_buffer_init(WiiUAudioBuffer* buffer, uint32_t rate);
void wiiu_audio_buffer_push(WiiUAudioBuffer* buffer, const int16_t* samples,
                            uint32_t frames);
void wiiu_audio_buffer_read(WiiUAudioBuffer* buffer, int16_t* samples,
                            uint32_t frames);
#endif
