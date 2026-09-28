#include "wiiu_audio_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"audio buffer line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static WiiUAudioBuffer b;
static int16_t input[BOTW_AUDIO_RING_FRAMES * 2], output[BOTW_AUDIO_RING_FRAMES * 2];
int main(void) {
    for (unsigned i=0;i<BOTW_AUDIO_RING_FRAMES;++i) {
        input[i*2]=12000;input[i*2+1]=-6000;
    }
    wiiu_audio_buffer_init(&b,32000);
    wiiu_audio_buffer_push(&b,input,1024);
    wiiu_audio_buffer_read(&b,output,1024);
    CHECK(b.count==1024 && !b.underruns);
    for(unsigned i=0;i<2048;++i) CHECK(output[i]==0);
    wiiu_audio_buffer_push(&b,input,3072);
    wiiu_audio_buffer_read(&b,output,4096);
    CHECK(!b.count && b.playing && !b.underruns);
    CHECK(output[0]>0 && output[0]<100);
    for(unsigned i=160;i<4096;++i) CHECK(output[i*2]==12000 && output[i*2+1]==-6000);
    /* A renderer stall becomes a 5 ms stereo ramp, not a full-scale edge. */
    wiiu_audio_buffer_read(&b,output,1024);
    CHECK(b.underruns==1 && b.reserve==3072 && !b.playing);
    CHECK(abs(output[0]-12000)<=100 && abs(output[1]+6000)<=100);
    for(unsigned i=1;i<160;++i) CHECK(abs(output[i*2]-output[(i-1)*2])<=100);
    for(unsigned i=160;i<1024;++i) CHECK(!output[i*2] && !output[i*2+1]);
    wiiu_audio_buffer_read(&b,output,1024); CHECK(b.underruns==1);
    wiiu_audio_buffer_push(&b,input,3072);
    wiiu_audio_buffer_read(&b,output,1024);
    CHECK(b.playing && b.count==2048 && output[0]<100);
    /* Healthy boundaries are bit-identical: no repeating fades per block. */
    for(unsigned i=0;i<1024;++i) { input[i*2]=(int16_t)i;input[i*2+1]=(int16_t)-i; }
    wiiu_audio_buffer_push(&b,input,1024);
    wiiu_audio_buffer_read(&b,output,2048);
    wiiu_audio_buffer_read(&b,output,1024);
    CHECK(!memcmp(input,output,1024*2*sizeof(int16_t)));
    wiiu_audio_buffer_push(&b,input,BOTW_AUDIO_RING_FRAMES);
    wiiu_audio_buffer_push(&b,input,100);
    CHECK(b.count==BOTW_AUDIO_RING_FRAMES && b.dropped==100);
    CHECK(b.fade_left==b.fade_frames);
    wiiu_audio_buffer_read(&b,output,BOTW_AUDIO_RING_FRAMES);
    CHECK(!b.count);
    wiiu_audio_buffer_init(&b,32000);
    CHECK(!b.underruns && !b.dropped && !b.playing && !b.last[0] && !b.last[1]);
    puts("Stereo jitter buffer: prefill, lossless playback, starvation/recovery ramps, overflow and reset passed");
    return 0;
}
