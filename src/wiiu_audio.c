#include "wiiu_audio.h"
#include "wiiu_audio_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

enum { AUDIO_DEVICE_BUFFERS = 3, AUDIO_DEVICE_FRAMES = 512 };
static HWAVEOUT g_wave_out;
static HANDLE g_wake, g_stop, g_thread;
static SRWLOCK g_lock = SRWLOCK_INIT;
static WiiUAudioBuffer g_buffer;
static WAVEHDR g_headers[AUDIO_DEVICE_BUFFERS];
static int16_t g_samples[AUDIO_DEVICE_BUFFERS][AUDIO_DEVICE_FRAMES * 2];
static bool g_queued[AUDIO_DEVICE_BUFFERS], g_muted;
static ULONGLONG g_stats_time;
static uint64_t g_stats_frames;

/* Guest mixing stays on the emulation thread. This thread owns the device
   queue, supplying smooth silence/recovery when rendering delays the mixer. */
static DWORD WINAPI output_thread(void* unused) {
    (void)unused;
    HANDLE events[2] = {g_stop, g_wake};
    for (;;) {
        if (WaitForSingleObject(g_stop, 0) == WAIT_OBJECT_0) break;
        for (uint32_t i = 0; i < AUDIO_DEVICE_BUFFERS; ++i) {
            if (g_queued[i] && !(g_headers[i].dwFlags & WHDR_DONE)) continue;
            AcquireSRWLockExclusive(&g_lock);
            wiiu_audio_buffer_read(&g_buffer, g_samples[i], AUDIO_DEVICE_FRAMES);
            ReleaseSRWLockExclusive(&g_lock);
            MMRESULT result = waveOutWrite(g_wave_out, &g_headers[i], sizeof(g_headers[i]));
            if (result != MMSYSERR_NOERROR) {
                fprintf(stderr, "audio: waveOutWrite failed (%u)\n", (unsigned)result);
                return 1;
            }
            g_queued[i] = true;
        }
        if (WaitForMultipleObjects(2, events, FALSE, 20) == WAIT_OBJECT_0) break;
    }
    return 0;
}

bool wiiu_audio_init(uint32_t sample_rate) {
    if (g_wave_out || g_muted) return true;
    const char* mute = getenv("BOTW_MUTE");
    if (mute && strcmp(mute, "1") == 0) {
        g_muted = true;
        fprintf(stderr, "audio: host output muted (guest audio remains active)\n");
        return true;
    }
    if (!sample_rate || sample_rate > 192000u) return false;
    g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_stop = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_wake || !g_stop) goto failed;
    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = sample_rate * 4;
    MMRESULT result = waveOutOpen(&g_wave_out, WAVE_MAPPER, &format,
                                  (DWORD_PTR)g_wake, 0, CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        g_wave_out = NULL;
        fprintf(stderr, "audio: waveOutOpen failed (%u)\n", (unsigned)result);
        goto failed;
    }
    memset(g_headers, 0, sizeof(g_headers));
    memset(g_queued, 0, sizeof(g_queued));
    wiiu_audio_buffer_init(&g_buffer, sample_rate);
    g_stats_time = GetTickCount64();
    g_stats_frames = 0;
    for (uint32_t i = 0; i < AUDIO_DEVICE_BUFFERS; ++i) {
        g_headers[i].lpData = (LPSTR)g_samples[i];
        g_headers[i].dwBufferLength = sizeof(g_samples[i]);
        result = waveOutPrepareHeader(g_wave_out, &g_headers[i], sizeof(g_headers[i]));
        if (result != MMSYSERR_NOERROR) goto failed;
    }
    g_thread = CreateThread(NULL, 0, output_thread, NULL, 0, NULL);
    if (!g_thread) goto failed;
    SetThreadPriority(g_thread, THREAD_PRIORITY_ABOVE_NORMAL);
    fprintf(stderr, "audio: native Windows output opened (%u Hz stereo, independent playback, adaptive reserve)\n", sample_rate);
    return true;
failed:
    wiiu_audio_shutdown();
    return false;
}

void wiiu_audio_submit_stereo(const int16_t* samples, uint32_t frame_count) {
    if (!g_wave_out || !samples || !frame_count) return;
    AcquireSRWLockExclusive(&g_lock);
    wiiu_audio_buffer_push(&g_buffer, samples, frame_count);
    uint32_t underruns = g_buffer.underruns, dropped = g_buffer.dropped;
    uint32_t queued = g_buffer.count, reserve = g_buffer.reserve;
    ReleaseSRWLockExclusive(&g_lock);
    SetEvent(g_wake);
    g_stats_frames += frame_count;
    ULONGLONG now = GetTickCount64();
    if (now - g_stats_time >= 5000u) {
        fprintf(stderr, "audio: delivery %.0f frames/sec underruns=%u dropped_frames=%u queued=%u reserve=%u\n",
                (double)g_stats_frames * 1000.0 / (double)(now - g_stats_time),
                underruns, dropped, queued, reserve);
        g_stats_time = now; g_stats_frames = 0;
    }
}

void wiiu_audio_shutdown(void) {
    g_muted = false;
    if (g_thread) {
        SetEvent(g_stop);
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread); g_thread = NULL;
    }
    if (g_wave_out) {
        waveOutReset(g_wave_out);
        for (uint32_t i = 0; i < AUDIO_DEVICE_BUFFERS; ++i)
            if (g_headers[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(g_wave_out, &g_headers[i], sizeof(g_headers[i]));
        waveOutClose(g_wave_out); g_wave_out = NULL;
    }
    if (g_wake) { CloseHandle(g_wake); g_wake = NULL; }
    if (g_stop) { CloseHandle(g_stop); g_stop = NULL; }
}
#else
bool wiiu_audio_init(uint32_t sample_rate) { (void)sample_rate; return false; }
void wiiu_audio_submit_stereo(const int16_t* samples, uint32_t frame_count) { (void)samples; (void)frame_count; }
void wiiu_audio_shutdown(void) {}
#endif
