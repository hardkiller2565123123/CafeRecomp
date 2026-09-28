#include "wiiu_audio.h"

int main(void) {
    const int16_t samples[8] = {32767, -32767, 1000, -1000, 0, 0, 100, -100};
    for (int pass = 0; pass < 2; ++pass) {
        if (!wiiu_audio_init(32000) || !wiiu_audio_init(32000)) return 1;
        wiiu_audio_submit_stereo(samples, 4);
        wiiu_audio_shutdown();
        wiiu_audio_shutdown();
    }
    return 0;
}
