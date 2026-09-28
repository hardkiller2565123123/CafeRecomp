#include "wiiu_audio_buffer.h"
#include <string.h>

void wiiu_audio_buffer_init(WiiUAudioBuffer* b, uint32_t rate) {
    memset(b, 0, sizeof(*b));
    b->reserve = rate * 64u / 1000u;
    if (!b->reserve) b->reserve = 1;
    if (b->reserve > BOTW_AUDIO_RING_FRAMES / 2)
        b->reserve = BOTW_AUDIO_RING_FRAMES / 2;
    b->fade_frames = rate * 5u / 1000u;
    if (!b->fade_frames) b->fade_frames = 1;
}

void wiiu_audio_buffer_push(WiiUAudioBuffer* b, const int16_t* samples,
                            uint32_t frames) {
    if (!samples || !frames) return;
    if (frames > BOTW_AUDIO_RING_FRAMES) {
        uint32_t skip = frames - BOTW_AUDIO_RING_FRAMES;
        samples += (size_t)skip * 2;
        b->dropped += skip;
        b->fade_left = b->fade_frames;
        frames -= skip;
    }
    if (frames > BOTW_AUDIO_RING_FRAMES - b->count) {
        uint32_t skip = frames - (BOTW_AUDIO_RING_FRAMES - b->count);
        b->read = (b->read + skip) % BOTW_AUDIO_RING_FRAMES;
        b->count -= skip;
        b->dropped += skip;
        /* A discontinuity in a full queue must also be de-clicked. */
        b->fade_left = b->fade_frames;
    }
    uint32_t write = (b->read + b->count) % BOTW_AUDIO_RING_FRAMES;
    for (uint32_t i = 0; i < frames; ++i) {
        b->samples[write * 2] = samples[i * 2];
        b->samples[write * 2 + 1] = samples[i * 2 + 1];
        write = (write + 1) % BOTW_AUDIO_RING_FRAMES;
    }
    b->count += frames;
}

void wiiu_audio_buffer_read(WiiUAudioBuffer* b, int16_t* samples,
                            uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        if (!b->playing && !b->fade_left && b->count >= b->reserve) {
            b->playing = true;
            b->fade_left = b->fade_frames;
        }
        if (b->playing && !b->count) {
            b->playing = false;
            b->fade_left = b->fade_frames;
            ++b->underruns;
            /* Loading jitter can exceed 64 ms. Grow only after starvation,
               bounded at 192 ms at the device's current sample rate. */
            uint32_t maximum = b->fade_frames * 192u / 5u;
            if (maximum > BOTW_AUDIO_RING_FRAMES / 2)
                maximum = BOTW_AUDIO_RING_FRAMES / 2;
            if (b->reserve < maximum) {
                b->reserve += b->fade_frames * 32u / 5u;
                if (b->reserve > maximum) b->reserve = maximum;
            }
        }
        for (uint32_t c = 0; c < 2; ++c) {
            int32_t target = b->playing ? b->samples[b->read * 2 + c] : 0;
            if (b->fade_left)
                target = b->last[c] + (target - b->last[c]) / (int32_t)b->fade_left;
            samples[i * 2 + c] = b->last[c] = (int16_t)target;
        }
        if (b->fade_left) --b->fade_left;
        if (b->playing) {
            b->read = (b->read + 1) % BOTW_AUDIO_RING_FRAMES;
            --b->count;
        }
    }
}
