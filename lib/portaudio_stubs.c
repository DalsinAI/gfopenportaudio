/* portaudio_stubs.c: written by make-stubs.py, do not edit.
 * A program's side of portaudio.library: each call opens the library the
 * first time and calls its vector as a C function (the arguments go on
 * the stack); the library closes when the program exits.
 * Copyright (c) 2026 AmiCradle Team */
#include <stdlib.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include "portaudio.h"

struct Library *PortAudioBase;
static const char missing[] = "portaudio.library 19 could not be opened";
#define MISSING missing

static void PaLibClose(void)
{
    if (PortAudioBase)
        CloseLibrary(PortAudioBase);
    PortAudioBase = NULL;
}

static int PaLib(void)
{
    if (!PortAudioBase) {
        PortAudioBase = OpenLibrary((CONST_STRPTR)"portaudio.library", 19);
        if (PortAudioBase)
            atexit(PaLibClose);
    }
    return PortAudioBase != NULL;
}

#define LVO(o) ((char *)PortAudioBase - (o))

int Pa_GetVersion(void)
{
    if (!PaLib())
        return 0;
    return ((int (*)(void))LVO(30))();
}

const char * Pa_GetVersionText(void)
{
    if (!PaLib())
        return MISSING;
    return ((const char * (*)(void))LVO(36))();
}

const PaVersionInfo * Pa_GetVersionInfo(void)
{
    if (!PaLib())
        return NULL;
    return ((const PaVersionInfo * (*)(void))LVO(42))();
}

const char * Pa_GetErrorText(PaError errorCode)
{
    if (!PaLib())
        return MISSING;
    return ((const char * (*)(PaError errorCode))LVO(48))(errorCode);
}

PaError Pa_Initialize(void)
{
    if (!PaLib())
        return paHostApiNotFound;
    return ((PaError (*)(void))LVO(54))();
}

PaError Pa_Terminate(void)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(void))LVO(60))();
}

PaHostApiIndex Pa_GetHostApiCount(void)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaHostApiIndex (*)(void))LVO(66))();
}

PaHostApiIndex Pa_GetDefaultHostApi(void)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaHostApiIndex (*)(void))LVO(72))();
}

const PaHostApiInfo * Pa_GetHostApiInfo(PaHostApiIndex hostApi)
{
    if (!PaLib())
        return NULL;
    return ((const PaHostApiInfo * (*)(PaHostApiIndex hostApi))LVO(78))(hostApi);
}

PaHostApiIndex Pa_HostApiTypeIdToHostApiIndex(PaHostApiTypeId type)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaHostApiIndex (*)(PaHostApiTypeId type))LVO(84))(type);
}

PaDeviceIndex Pa_HostApiDeviceIndexToDeviceIndex(PaHostApiIndex hostApi, int hostApiDeviceIndex)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaDeviceIndex (*)(PaHostApiIndex hostApi, int hostApiDeviceIndex))LVO(90))(hostApi, hostApiDeviceIndex);
}

const PaHostErrorInfo * Pa_GetLastHostErrorInfo(void)
{
    if (!PaLib())
        return NULL;
    return ((const PaHostErrorInfo * (*)(void))LVO(96))();
}

PaDeviceIndex Pa_GetDeviceCount(void)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaDeviceIndex (*)(void))LVO(102))();
}

PaDeviceIndex Pa_GetDefaultInputDevice(void)
{
    if (!PaLib())
        return paNoDevice;
    return ((PaDeviceIndex (*)(void))LVO(108))();
}

PaDeviceIndex Pa_GetDefaultOutputDevice(void)
{
    if (!PaLib())
        return paNoDevice;
    return ((PaDeviceIndex (*)(void))LVO(114))();
}

const PaDeviceInfo * Pa_GetDeviceInfo(PaDeviceIndex device)
{
    if (!PaLib())
        return NULL;
    return ((const PaDeviceInfo * (*)(PaDeviceIndex device))LVO(120))(device);
}

PaError Pa_IsFormatSupported(const PaStreamParameters *inputParameters, const PaStreamParameters *outputParameters, double sampleRate)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(const PaStreamParameters *inputParameters, const PaStreamParameters *outputParameters, double sampleRate))LVO(126))(inputParameters, outputParameters, sampleRate);
}

PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *inputParameters, const PaStreamParameters *outputParameters, double sampleRate, unsigned long framesPerBuffer, PaStreamFlags streamFlags, PaStreamCallback *streamCallback, void *userData)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream **stream, const PaStreamParameters *inputParameters, const PaStreamParameters *outputParameters, double sampleRate, unsigned long framesPerBuffer, PaStreamFlags streamFlags, PaStreamCallback *streamCallback, void *userData))LVO(132))(stream, inputParameters, outputParameters, sampleRate, framesPerBuffer, streamFlags, streamCallback, userData);
}

PaError Pa_OpenDefaultStream(PaStream **stream, int numInputChannels, int numOutputChannels, PaSampleFormat sampleFormat, double sampleRate, unsigned long framesPerBuffer, PaStreamCallback *streamCallback, void *userData)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream **stream, int numInputChannels, int numOutputChannels, PaSampleFormat sampleFormat, double sampleRate, unsigned long framesPerBuffer, PaStreamCallback *streamCallback, void *userData))LVO(138))(stream, numInputChannels, numOutputChannels, sampleFormat, sampleRate, framesPerBuffer, streamCallback, userData);
}

PaError Pa_CloseStream(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream))LVO(144))(stream);
}

PaError Pa_SetStreamFinishedCallback(PaStream *stream, PaStreamFinishedCallback *streamFinishedCallback)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream, PaStreamFinishedCallback *streamFinishedCallback))LVO(150))(stream, streamFinishedCallback);
}

PaError Pa_StartStream(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream))LVO(156))(stream);
}

PaError Pa_StopStream(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream))LVO(162))(stream);
}

PaError Pa_AbortStream(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream))LVO(168))(stream);
}

PaError Pa_IsStreamStopped(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream))LVO(174))(stream);
}

PaError Pa_IsStreamActive(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream))LVO(180))(stream);
}

const PaStreamInfo * Pa_GetStreamInfo(PaStream *stream)
{
    if (!PaLib())
        return NULL;
    return ((const PaStreamInfo * (*)(PaStream *stream))LVO(186))(stream);
}

PaTime Pa_GetStreamTime(PaStream *stream)
{
    if (!PaLib())
        return 0;
    return ((PaTime (*)(PaStream *stream))LVO(192))(stream);
}

double Pa_GetStreamCpuLoad(PaStream *stream)
{
    if (!PaLib())
        return 0;
    return ((double (*)(PaStream *stream))LVO(198))(stream);
}

PaError Pa_ReadStream(PaStream *stream, void *buffer, unsigned long frames)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream, void *buffer, unsigned long frames))LVO(204))(stream, buffer, frames);
}

PaError Pa_WriteStream(PaStream *stream, const void *buffer, unsigned long frames)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaStream *stream, const void *buffer, unsigned long frames))LVO(210))(stream, buffer, frames);
}

signed long Pa_GetStreamReadAvailable(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((signed long (*)(PaStream *stream))LVO(216))(stream);
}

signed long Pa_GetStreamWriteAvailable(PaStream *stream)
{
    if (!PaLib())
        return paNotInitialized;
    return ((signed long (*)(PaStream *stream))LVO(222))(stream);
}

PaError Pa_GetSampleSize(PaSampleFormat format)
{
    if (!PaLib())
        return paNotInitialized;
    return ((PaError (*)(PaSampleFormat format))LVO(228))(format);
}

void Pa_Sleep(long msec)
{
    if (PaLib())
        ((void (*)(long msec))LVO(234))(msec);
}
