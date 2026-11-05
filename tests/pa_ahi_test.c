/*
 * GFOpenPortAudio's test on the Amiga: the AHI host API's device, a sine
 * played for two seconds through a callback stream (the frames it asked for
 * against the time it took), a stream that ends itself (paComplete) and its
 * finished callback, stop and abort, and a second of recording when the
 * AHI mode can record. Prints "ok" or "FAIL" for each and a count.
 *
 * Copyright (c) 2026 AmiCradle Team
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "portaudio.h"

static int s_pass, s_fail;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok) s_pass++; else s_fail++;
}

typedef struct {
    double phase;
    unsigned long frames;       /* given to the callback */
    unsigned long stopAfter;    /* paComplete once this many (0: never) */
    long nonZero;               /* recorded samples that are not 0 */
    int finished;
} Sine;

static int SineCallback(const void *input, void *output, unsigned long frames,
                        const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags flags, void *user)
{
    Sine *s = (Sine *)user;
    float *out = (float *)output;
    unsigned long i;
    (void)input; (void)timeInfo; (void)flags;
    for (i = 0; i < frames; i++) {
        float v = (float)(0.2 * sin(s->phase));
        s->phase += 2.0 * 3.14159265358979 * 440.0 / 44100.0;
        if (s->phase > 2.0 * 3.14159265358979)
            s->phase -= 2.0 * 3.14159265358979;
        *out++ = v;
        *out++ = v;
    }
    s->frames += frames;
    return s->stopAfter && s->frames >= s->stopAfter ? paComplete : paContinue;
}

static int RecordCallback(const void *input, void *output, unsigned long frames,
                          const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags flags, void *user)
{
    Sine *s = (Sine *)user;
    const short *in = (const short *)input;
    unsigned long i;
    (void)output; (void)timeInfo; (void)flags;
    for (i = 0; in && i < frames; i++)
        if (in[i])
            s->nonZero++;
    s->frames += frames;
    return paContinue;
}

static void Finished(void *user)
{
    ((Sine *)user)->finished = 1;
}

static PaStreamParameters Output(void)
{
    PaStreamParameters p;
    memset(&p, 0, sizeof p);
    p.device = Pa_GetDefaultOutputDevice();
    p.channelCount = 2;
    p.sampleFormat = paFloat32;
    p.suggestedLatency = p.device >= 0 ? Pa_GetDeviceInfo(p.device)->defaultLowOutputLatency : 0.1;
    return p;
}

int main(void)
{
    PaError err;
    PaStream *stream = NULL;
    PaStreamParameters out;
    Sine sine;
    PaTime t0, t1;
    int i, n;

    /* each line as it is printed: a run that stops part way shows where */
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("     %s\n", Pa_GetVersionText());
    err = Pa_Initialize();

    check(err == paNoError, "Pa_Initialize");
    if (err != paNoError) {
        printf("  %s\n", Pa_GetErrorText(err));
        return 1;
    }
    n = Pa_GetDeviceCount();
    for (i = 0; i < n; i++) {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
        printf("     device %d: %s (%s) in %d out %d, %.0f Hz\n", i, d->name,
               Pa_GetHostApiInfo(d->hostApi)->name, d->maxInputChannels, d->maxOutputChannels,
               d->defaultSampleRate);
    }
    check(n >= 1 && Pa_GetDefaultOutputDevice() >= 0, "an AHI output device");
    if (n < 1) {
        Pa_Terminate();
        printf("pa_ahi_test: %d passed, %d failed\n", s_pass, s_fail);
        return 1;
    }

    /* two seconds of a 440 Hz sine, in floats the buffer processor turns to 16 bits */
    out = Output();
    check(Pa_IsFormatSupported(NULL, &out, 44100.0) == paFormatIsSupported, "Pa_IsFormatSupported 44100 stereo");
    memset(&sine, 0, sizeof sine);
    err = Pa_OpenStream(&stream, NULL, &out, 44100.0, paFramesPerBufferUnspecified, paClipOff, SineCallback, &sine);
    check(err == paNoError, "Pa_OpenStream output");
    if (err == paNoError) {
        const PaStreamInfo *info = Pa_GetStreamInfo(stream);
        printf("     output latency %.3f s\n", info ? info->outputLatency : -1.0);
        Pa_SetStreamFinishedCallback(stream, Finished);
        t0 = Pa_GetStreamTime(stream);
        check(Pa_StartStream(stream) == paNoError, "Pa_StartStream");
        check(Pa_IsStreamActive(stream) == 1, "the stream is active");
        Pa_Sleep(2000);
        t1 = Pa_GetStreamTime(stream);
        check(Pa_StopStream(stream) == paNoError, "Pa_StopStream");
        printf("     %lu frames asked for in %.2f s (%.0f a second), cpu load %.3f\n", sine.frames, t1 - t0,
               sine.frames / (t1 - t0), Pa_GetStreamCpuLoad(stream));
        /* the frames asked for keep pace with the clock: 44100 a second, give or
         * take the two buffers queued ahead */
        check(sine.frames > 44100.0 * (t1 - t0) * 0.9 && sine.frames < 44100.0 * (t1 - t0) * 1.1 + 8192,
              "the callback keeps time with 44100 Hz");
        check(Pa_IsStreamStopped(stream) == 1 && sine.finished, "stopped, and the finished callback ran");
        check(Pa_CloseStream(stream) == paNoError, "Pa_CloseStream");
    }

    /* a stream that ends itself after half a second */
    memset(&sine, 0, sizeof sine);
    sine.stopAfter = 22050;
    err = Pa_OpenStream(&stream, NULL, &out, 44100.0, 512, paClipOff, SineCallback, &sine);
    check(err == paNoError, "Pa_OpenStream 512 frames a buffer");
    if (err == paNoError) {
        Pa_SetStreamFinishedCallback(stream, Finished);
        Pa_StartStream(stream);
        for (i = 0; i < 100 && Pa_IsStreamActive(stream) == 1; i++)
            Pa_Sleep(50);
        check(Pa_IsStreamActive(stream) == 0 && sine.finished && sine.frames >= 22050,
              "paComplete ends the stream by itself");
        Pa_StopStream(stream);
        /* started again, then aborted at once */
        memset(&sine, 0, sizeof sine);
        check(Pa_StartStream(stream) == paNoError && Pa_AbortStream(stream) == paNoError, "start again, then abort");
        check(Pa_CloseStream(stream) == paNoError, "Pa_CloseStream after abort");
    }

    /* blocking streams are refused, not hung */
    err = Pa_OpenStream(&stream, NULL, &out, 44100.0, 512, paClipOff, NULL, NULL);
    check(err != paNoError, "a blocking stream is refused");
    if (err == paNoError)
        Pa_CloseStream(stream);

    /* a second of recording, mono 16 bits, if the mode records */
    {
        PaStreamParameters in;
        memset(&in, 0, sizeof in);
        in.device = Pa_GetDefaultInputDevice();
        in.channelCount = 1;
        in.sampleFormat = paInt16;
        in.suggestedLatency = 0.1;
        memset(&sine, 0, sizeof sine);
        err = Pa_OpenStream(&stream, &in, NULL, 44100.0, paFramesPerBufferUnspecified, 0, RecordCallback, &sine);
        check(err == paNoError, "Pa_OpenStream input");
        if (err == paNoError) {
            PaTime r0 = Pa_GetStreamTime(stream), r1;
            err = Pa_StartStream(stream);
            if (err == paNoError) {
                Pa_Sleep(1000);
                r1 = Pa_GetStreamTime(stream);
                Pa_StopStream(stream);
                printf("     recorded %lu frames in %.2f s, %ld not silent\n", sine.frames, r1 - r0, sine.nonZero);
                check(sine.frames > 44100.0 * (r1 - r0) * 0.5 && sine.frames < 44100.0 * (r1 - r0) * 1.1 + 8192,
                      "recording keeps time with 44100 Hz");
            } else {
                /* a mode that cannot record: refused at start, with the reason */
                printf("     recording refused: %s (%s)\n", Pa_GetErrorText(err),
                       Pa_GetLastHostErrorInfo()->errorText);
                check(err == paUnanticipatedHostError, "a mode that cannot record is refused at start");
            }
            Pa_CloseStream(stream);
        }
    }

    check(Pa_Terminate() == paNoError, "Pa_Terminate");
    printf("pa_ahi_test: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
