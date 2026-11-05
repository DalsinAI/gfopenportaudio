/*
 * GFOpenPortAudio: PortAudio's host API for ahi.device on AmigaOS 3.x.
 *
 * One device, ahi.device's unit 0 (the unit AHI's prefs give "music" by
 * default): up to two channels out and two in, 16-bit signed, any rate AHI
 * mixes at. A stream is a process of its own that opens ahi.device, keeps two
 * requests in flight each way (a write is linked to the one before it, so
 * AHI plays them back to back) and runs PortAudio's buffer processor between
 * them, which calls the program's callback and converts its samples.
 * Callback streams only; a blocking stream (no callback) is refused.
 *
 * Copyright (c) 2026 AmiCradle Team
 */

#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>

#include "portaudio.h"
#include "pa_util.h"
#include "pa_allocation.h"
#include "pa_hostapi.h"
#include "pa_stream.h"
#include "pa_cpuload.h"
#include "pa_process.h"

/* ahi.device's request (AHI's devices/ahi.h, interface version 4) */
struct PaAhiRequest {
    struct IOStdReq ahir_Std;
    UWORD ahir_Version;
    UWORD ahir_Pad1;
    ULONG ahir_Private[2];
    ULONG ahir_Type;
    ULONG ahir_Frequency;
    LONG ahir_Volume;
    LONG ahir_Position;
    struct PaAhiRequest *ahir_Link;
};
#define PA_AHI_UNIT 0
#define PA_AHIST_M16S 1UL
#define PA_AHIST_S16S 3UL

#define PA_AHI_MIN_RATE 4000.0
#define PA_AHI_MAX_RATE 96000.0
#define PA_AHI_DEFAULT_FRAMES 1024UL
#define PA_AHI_MAX_FRAMES 16384UL

PaError PaAmigaAhi_Initialize(PaUtilHostApiRepresentation **hostApi, PaHostApiIndex index);

typedef struct {
    PaUtilHostApiRepresentation inheritedHostApiRep;
    PaUtilStreamInterface callbackStreamInterface;
    PaUtilStreamInterface blockingStreamInterface;
    PaUtilAllocationGroup *allocations;
} PaAhiHostApi;

typedef struct {
    PaUtilStreamRepresentation streamRepresentation;
    PaUtilCpuLoadMeasurer cpuLoadMeasurer;
    PaUtilBufferProcessor bufferProcessor;

    int inChannels, outChannels;    /* 0: none that way */
    double rate;
    unsigned long frames;           /* each host buffer */
    PaTime outLatency, inLatency;

    int threadMade;                 /* the stream's process is running (or leaving) */
    volatile int exited;            /* it has left: set under Forbid, as its last act */
    struct Task *task;              /* the stream's thread, for its signals */
    volatile int state;             /* 0 stopped, 1 starting, 2 running, -1 failed to start */
    volatile int stopRequest;       /* 1 stop (play what is queued), 2 abort */
    volatile int finished;          /* the thread has left its loop */
    PaTime startTime;
} PaAhiStream;

/* ---- memory and time the stream thread needs ------------------------------------------------ */

static void SetHostError(long code, const char *text)
{
    PaUtil_SetLastHostErrorInfo(paInDevelopment, code, text);
}

/* ---- the stream's thread --------------------------------------------------------------------- */

struct PaAhiIo {
    struct MsgPort *port;
    struct PaAhiRequest *req[2];
    void *buf[2];
    int opened;
};

static int IoOpen(struct PaAhiIo *io, struct PaAhiRequest *first, unsigned long bytes)
{
    memset(io, 0, sizeof *io);
    io->port = CreateMsgPort();
    if (!io->port)
        return 0;
    if (first) {
        /* the second direction shares the first one's device */
        io->req[0] = (struct PaAhiRequest *)AllocVec(sizeof(struct PaAhiRequest), MEMF_PUBLIC | MEMF_CLEAR);
        if (!io->req[0])
            return 0;
        CopyMem(first, io->req[0], sizeof(struct PaAhiRequest));
        io->req[0]->ahir_Std.io_Message.mn_ReplyPort = io->port;
    } else {
        io->req[0] = (struct PaAhiRequest *)CreateIORequest(io->port, sizeof(struct PaAhiRequest));
        if (!io->req[0])
            return 0;
        io->req[0]->ahir_Version = 4;
        if (OpenDevice((CONST_STRPTR)"ahi.device", PA_AHI_UNIT, (struct IORequest *)io->req[0], 0) != 0) {
            SetHostError(io->req[0]->ahir_Std.io_Error, "ahi.device could not be opened");
            return 0;
        }
        io->opened = 1;
    }
    io->req[1] = (struct PaAhiRequest *)AllocVec(sizeof(struct PaAhiRequest), MEMF_PUBLIC);
    io->buf[0] = AllocVec(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    io->buf[1] = AllocVec(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!io->req[1] || !io->buf[0] || !io->buf[1])
        return 0;
    CopyMem(io->req[0], io->req[1], sizeof(struct PaAhiRequest));
    return 1;
}

static void IoClose(struct PaAhiIo *io, int owner)
{
    if (io->buf[1]) FreeVec(io->buf[1]);
    if (io->buf[0]) FreeVec(io->buf[0]);
    if (io->req[1]) FreeVec(io->req[1]);
    if (io->req[0]) {
        if (owner) {
            if (io->opened)
                CloseDevice((struct IORequest *)io->req[0]);
            DeleteIORequest((struct IORequest *)io->req[0]);
        } else
            FreeVec(io->req[0]);
    }
    if (io->port) DeleteMsgPort(io->port);
    memset(io, 0, sizeof *io);
}

static void Send(struct PaAhiRequest *r, UWORD cmd, void *buf, unsigned long bytes, ULONG type,
                 double rate, struct PaAhiRequest *link)
{
    r->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
    r->ahir_Std.io_Command = cmd;
    r->ahir_Std.io_Data = buf;
    r->ahir_Std.io_Length = bytes;
    if (cmd == CMD_WRITE)
        r->ahir_Std.io_Offset = 0;
    r->ahir_Type = type;
    r->ahir_Frequency = (ULONG)(rate + 0.5);
    r->ahir_Volume = 0x10000;
    r->ahir_Position = 0x8000;
    r->ahir_Link = link;
    SendIO((struct IORequest *)r);
}

/* Waits for a request; 0 when the stream is told to abort meanwhile */
static int Await(PaAhiStream *s, struct PaAhiRequest *r, ULONG portsig)
{
    while (!CheckIO((struct IORequest *)r)) {
        ULONG got = Wait(portsig | SIGBREAKF_CTRL_C);
        if ((got & SIGBREAKF_CTRL_C) && s->stopRequest == 2)
            return 0;
    }
    WaitIO((struct IORequest *)r);
    return 1;
}

/* A read that came back: 0 when it failed or brought nothing (a mode that
 * cannot record ends every read at once with nothing); a short one is
 * filled out with silence */
static int ReadDone(struct PaAhiRequest *r, void *buf, unsigned long bytes)
{
    const unsigned long got = r->ahir_Std.io_Actual;
    if (r->ahir_Std.io_Error != 0 || got == 0) {
        SetHostError(r->ahir_Std.io_Error ? r->ahir_Std.io_Error : -1,
                     "ahi.device gave nothing to record (does its mode record?)");
        return 0;
    }
    if (got < bytes)
        memset((char *)buf + got, 0, bytes - got);
    return 1;
}

static void StreamRun(PaAhiStream *s)
{
    struct PaAhiIo out, in;
    const unsigned long outBytes = s->frames * (unsigned long)s->outChannels * 2;
    const unsigned long inBytes = s->frames * (unsigned long)s->inChannels * 2;
    const ULONG outType = s->outChannels == 2 ? PA_AHIST_S16S : PA_AHIST_M16S;
    const ULONG inType = s->inChannels == 2 ? PA_AHIST_S16S : PA_AHIST_M16S;
    struct PaAhiRequest *linked = NULL;
    int i = 0, ok = 1, callbackResult = paContinue;
    int reading[2] = { 0, 0 };      /* the input requests in flight */
    int firstReady = 0;             /* the first read is in, buffer 0 */
    ULONG sigs = 0;

    memset(&out, 0, sizeof out);
    memset(&in, 0, sizeof in);
    s->task = FindTask(NULL);
    SetSignal(0, SIGBREAKF_CTRL_C);

    if (s->outChannels)
        ok = IoOpen(&out, NULL, outBytes);
    if (ok && s->inChannels)
        ok = IoOpen(&in, s->outChannels ? out.req[0] : NULL, inBytes);
    if (!ok) {
        IoClose(&in, !s->outChannels);
        IoClose(&out, 1);
        s->state = -1;
        s->finished = 1;
        return;
    }
    if (out.port) sigs |= 1UL << out.port->mp_SigBit;
    if (in.port) sigs |= 1UL << in.port->mp_SigBit;

    if (s->inChannels) {
        /* recording starts with the first read, which says whether the
         * mode records at all; then two stay in flight */
        in.req[0]->ahir_Std.io_Offset = 0;
        Send(in.req[0], CMD_READ, in.buf[0], inBytes, inType, s->rate, NULL);
        WaitIO((struct IORequest *)in.req[0]);
        if (!ReadDone(in.req[0], in.buf[0], inBytes)) {
            IoClose(&in, !s->outChannels);
            IoClose(&out, 1);
            s->state = -1;
            s->finished = 1;
            return;
        }
        in.req[1]->ahir_Std.io_Offset = in.req[0]->ahir_Std.io_Offset;
        Send(in.req[1], CMD_READ, in.buf[1], inBytes, inType, s->rate, NULL);
        reading[1] = 1;
        firstReady = 1;
    }
    s->startTime = PaUtil_GetTime();
    s->state = 2;

    while (s->stopRequest == 0 && callbackResult == paContinue) {
        PaStreamCallbackTimeInfo timeInfo;
        PaStreamCallbackFlags flags = 0;
        unsigned long done;

        if (s->inChannels && firstReady)
            firstReady = 0;
        else if (s->inChannels) {
            if (!Await(s, in.req[i], sigs))
                break;
            reading[i] = 0;
            if (!ReadDone(in.req[i], in.buf[i], inBytes)) {
                callbackResult = paAbort;
                break;
            }
            /* the next read carries on where this one ended */
            in.req[i ^ 1]->ahir_Std.io_Offset = in.req[i]->ahir_Std.io_Offset;
            Send(in.req[i ^ 1], CMD_READ, in.buf[i ^ 1], inBytes, inType, s->rate, NULL);
            reading[i ^ 1] = 1;
        }

        timeInfo.currentTime = PaUtil_GetTime();
        timeInfo.inputBufferAdcTime = timeInfo.currentTime - s->inLatency;
        timeInfo.outputBufferDacTime = timeInfo.currentTime + s->outLatency;

        PaUtil_BeginCpuLoadMeasurement(&s->cpuLoadMeasurer);
        PaUtil_BeginBufferProcessing(&s->bufferProcessor, &timeInfo, flags);
        if (s->inChannels) {
            PaUtil_SetInputFrameCount(&s->bufferProcessor, s->frames);
            PaUtil_SetInterleavedInputChannels(&s->bufferProcessor, 0, in.buf[i], s->inChannels);
        }
        if (s->outChannels) {
            PaUtil_SetOutputFrameCount(&s->bufferProcessor, s->frames);
            PaUtil_SetInterleavedOutputChannels(&s->bufferProcessor, 0, out.buf[i], s->outChannels);
        }
        done = PaUtil_EndBufferProcessing(&s->bufferProcessor, &callbackResult);
        PaUtil_EndCpuLoadMeasurement(&s->cpuLoadMeasurer, done);
        if (callbackResult == paAbort)
            break;

        if (s->outChannels) {
            if (done < s->frames)
                memset((char *)out.buf[i] + done * s->outChannels * 2, 0,
                       (s->frames - done) * s->outChannels * 2);
            Send(out.req[i], CMD_WRITE, out.buf[i], outBytes, outType, s->rate, linked);
            if (linked && !Await(s, linked, sigs))
                break;
            linked = out.req[i];
        }
        i ^= 1;
    }

    /* stop: what is queued plays out; abort: it goes */
    if (linked) {
        if (s->stopRequest == 2 || callbackResult == paAbort)
            AbortIO((struct IORequest *)linked);
        WaitIO((struct IORequest *)linked);
    }
    {
        int k;
        for (k = 0; k < 2; k++)
            if (reading[k]) {
                AbortIO((struct IORequest *)in.req[k]);
                WaitIO((struct IORequest *)in.req[k]);
            }
    }
    IoClose(&in, !s->outChannels);
    IoClose(&out, 1);

    s->finished = 1;
    if (s->streamRepresentation.streamFinishedCallback)
        s->streamRepresentation.streamFinishedCallback(s->streamRepresentation.userData);
    return;
}

/* The stream's process: its stream comes in tc_UserData (set under Forbid
 * before it can run). It says it has gone as its last act, under Forbid, so
 * nothing of it runs once StopThread sees that. */
static void StreamProcess(void)
{
    PaAhiStream *s = (PaAhiStream *)FindTask(NULL)->tc_UserData;
    StreamRun(s);
    Forbid();
    s->exited = 1;
}

static void JoinThread(PaAhiStream *s)
{
    while (!s->exited)
        Delay(1);
}

/* ---- the stream's calls ---------------------------------------------------------------------- */

static PaError StopThread(PaAhiStream *s, int how)
{
    if (!s->threadMade)
        return paNoError;
    s->stopRequest = how;
    if (s->task && !s->finished)
        Signal(s->task, SIGBREAKF_CTRL_C);
    JoinThread(s);
    s->threadMade = 0;
    s->task = NULL;
    s->state = 0;
    return paNoError;
}

static PaError StartStream(PaStream *stream)
{
    PaAhiStream *s = (PaAhiStream *)stream;
    if (s->state > 0 && !s->finished)
        return paStreamIsNotStopped;
    StopThread(s, 2);
    PaUtil_ResetBufferProcessor(&s->bufferProcessor);
    s->stopRequest = 0;
    s->finished = 0;
    s->state = 1;
    s->exited = 0;
    {
        /* PortAudio's callback runs in it: a C++ program's callback wants a
         * big stack. Its priority is its maker's. */
        struct Process *proc;
        Forbid();
        proc = CreateNewProcTags(NP_Entry, (ULONG)StreamProcess, NP_Name, (ULONG)"PortAudio stream",
                                 NP_StackSize, 262144, NP_Priority, (ULONG)(LONG)FindTask(NULL)->tc_Node.ln_Pri,
                                 TAG_END);
        if (proc)
            proc->pr_Task.tc_UserData = s;
        Permit();
        if (!proc)
            s->exited = 1;
    }
    if (s->exited) {
        s->state = 0;
        SetHostError(-1, "the stream's process could not be made");
        return paUnanticipatedHostError;
    }
    s->threadMade = 1;
    while (s->state == 1)
        Delay(1);
    if (s->state < 0) {
        JoinThread(s);
        s->threadMade = 0;
        s->state = 0;
        return paUnanticipatedHostError;
    }
    return paNoError;
}

static PaError StopStream(PaStream *stream)
{
    return StopThread((PaAhiStream *)stream, 1);
}

static PaError AbortStream(PaStream *stream)
{
    return StopThread((PaAhiStream *)stream, 2);
}

static PaError IsStreamStopped(PaStream *stream)
{
    PaAhiStream *s = (PaAhiStream *)stream;
    return !s->threadMade;
}

static PaError IsStreamActive(PaStream *stream)
{
    PaAhiStream *s = (PaAhiStream *)stream;
    return s->threadMade && !s->finished;
}

static PaTime GetStreamTime(PaStream *stream)
{
    (void)stream;
    return PaUtil_GetTime();
}

static double GetStreamCpuLoad(PaStream *stream)
{
    PaAhiStream *s = (PaAhiStream *)stream;
    return PaUtil_GetCpuLoad(&s->cpuLoadMeasurer);
}

static PaError CloseStream(PaStream *stream)
{
    PaAhiStream *s = (PaAhiStream *)stream;
    StopThread(s, 2);
    PaUtil_TerminateBufferProcessor(&s->bufferProcessor);
    PaUtil_TerminateStreamRepresentation(&s->streamRepresentation);
    PaUtil_FreeMemory(s);
    return paNoError;
}

/* blocking streams are refused at open, so these are never reached */
static PaError ReadStream(PaStream *stream, void *buffer, unsigned long frames)
{
    (void)stream; (void)buffer; (void)frames;
    return paCanNotReadFromACallbackStream;
}

static PaError WriteStream(PaStream *stream, const void *buffer, unsigned long frames)
{
    (void)stream; (void)buffer; (void)frames;
    return paCanNotWriteToACallbackStream;
}

static signed long GetStreamReadAvailable(PaStream *stream)
{
    (void)stream;
    return paCanNotReadFromACallbackStream;
}

static signed long GetStreamWriteAvailable(PaStream *stream)
{
    (void)stream;
    return paCanNotWriteToACallbackStream;
}

/* ---- the host API ---------------------------------------------------------------------------- */

static PaError CheckParameters(PaUtilHostApiRepresentation *hostApi, const PaStreamParameters *p,
                               int input, int *channels)
{
    *channels = 0;
    if (!p)
        return paNoError;
    if (p->device == paUseHostApiSpecificDeviceSpecification)
        return paInvalidDevice;
    if (p->device < 0 || p->device >= hostApi->info.deviceCount)
        return paInvalidDevice;
    if (p->channelCount < 1 ||
        p->channelCount > (input ? hostApi->deviceInfos[p->device]->maxInputChannels
                                 : hostApi->deviceInfos[p->device]->maxOutputChannels))
        return paInvalidChannelCount;
    if (p->hostApiSpecificStreamInfo)
        return paIncompatibleHostApiSpecificStreamInfo;
    *channels = p->channelCount;
    return paNoError;
}

static PaError IsFormatSupported(PaUtilHostApiRepresentation *hostApi, const PaStreamParameters *inputParameters,
                                 const PaStreamParameters *outputParameters, double sampleRate)
{
    int inChannels, outChannels;
    PaError err = CheckParameters(hostApi, inputParameters, 1, &inChannels);
    if (err != paNoError)
        return err;
    err = CheckParameters(hostApi, outputParameters, 0, &outChannels);
    if (err != paNoError)
        return err;
    if (sampleRate < PA_AHI_MIN_RATE || sampleRate > PA_AHI_MAX_RATE)
        return paInvalidSampleRate;
    return paFormatIsSupported;
}

static PaError OpenStream(PaUtilHostApiRepresentation *hostApi, PaStream **stream,
                          const PaStreamParameters *inputParameters, const PaStreamParameters *outputParameters,
                          double sampleRate, unsigned long framesPerBuffer, PaStreamFlags streamFlags,
                          PaStreamCallback *streamCallback, void *userData)
{
    PaAhiHostApi *ahi = (PaAhiHostApi *)hostApi;
    PaAhiStream *s;
    PaError err;
    int inChannels, outChannels;
    PaSampleFormat inFormat = paInt16, outFormat = paInt16;
    PaTime latency = 0;
    unsigned long frames;

    err = CheckParameters(hostApi, inputParameters, 1, &inChannels);
    if (err != paNoError)
        return err;
    err = CheckParameters(hostApi, outputParameters, 0, &outChannels);
    if (err != paNoError)
        return err;
    if (!inChannels && !outChannels)
        return paInvalidChannelCount;
    if (sampleRate < PA_AHI_MIN_RATE || sampleRate > PA_AHI_MAX_RATE)
        return paInvalidSampleRate;
    if ((streamFlags & paPlatformSpecificFlags) != 0)
        return paInvalidFlag;
    if (!streamCallback)
        return paBadIODeviceCombination;     /* blocking streams: not yet */

    if (inputParameters) {
        inFormat = inputParameters->sampleFormat;
        latency = inputParameters->suggestedLatency;
    }
    if (outputParameters) {
        outFormat = outputParameters->sampleFormat;
        if (outputParameters->suggestedLatency > latency)
            latency = outputParameters->suggestedLatency;
    }

    /* the host buffer: the program's size, else what the latency asks for
     * (two buffers are queued, so each is half of it) */
    if (framesPerBuffer != paFramesPerBufferUnspecified)
        frames = framesPerBuffer;
    else
        frames = (unsigned long)(latency * sampleRate / 2.0);
    if (frames < 256)
        frames = framesPerBuffer != paFramesPerBufferUnspecified && framesPerBuffer > 0 ? framesPerBuffer
                                                                                       : PA_AHI_DEFAULT_FRAMES;
    if (frames > PA_AHI_MAX_FRAMES)
        frames = PA_AHI_MAX_FRAMES;

    s = (PaAhiStream *)PaUtil_AllocateMemory(sizeof(PaAhiStream));
    if (!s)
        return paInsufficientMemory;
    memset(s, 0, sizeof *s);

    PaUtil_InitializeStreamRepresentation(&s->streamRepresentation, &ahi->callbackStreamInterface,
                                          streamCallback, userData);
    PaUtil_InitializeCpuLoadMeasurer(&s->cpuLoadMeasurer, sampleRate);

    err = PaUtil_InitializeBufferProcessor(&s->bufferProcessor,
                                           inChannels, inFormat, paInt16,
                                           outChannels, outFormat, paInt16,
                                           sampleRate, streamFlags, framesPerBuffer,
                                           frames, paUtilFixedHostBufferSize,
                                           streamCallback, userData);
    if (err != paNoError) {
        PaUtil_FreeMemory(s);
        return err;
    }

    s->inChannels = inChannels;
    s->outChannels = outChannels;
    s->rate = sampleRate;
    s->frames = frames;
    s->outLatency = outChannels ? (2.0 * frames) / sampleRate : 0;
    s->inLatency = inChannels ? (double)frames / sampleRate : 0;
    s->streamRepresentation.streamInfo.inputLatency =
        s->inLatency + (PaTime)PaUtil_GetBufferProcessorInputLatencyFrames(&s->bufferProcessor) / sampleRate;
    s->streamRepresentation.streamInfo.outputLatency =
        s->outLatency + (PaTime)PaUtil_GetBufferProcessorOutputLatencyFrames(&s->bufferProcessor) / sampleRate;
    s->streamRepresentation.streamInfo.sampleRate = sampleRate;

    *stream = (PaStream *)s;
    return paNoError;
}

static void Terminate(PaUtilHostApiRepresentation *hostApi)
{
    PaAhiHostApi *ahi = (PaAhiHostApi *)hostApi;
    if (ahi->allocations) {
        PaUtil_FreeAllAllocations(ahi->allocations);
        PaUtil_DestroyAllocationGroup(ahi->allocations);
    }
    PaUtil_FreeMemory(ahi);
}

/* Is ahi.device there at all: opened and closed again */
static int AhiPresent(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct PaAhiRequest *r;
    int ok = 0;
    if (!port)
        return 0;
    r = (struct PaAhiRequest *)CreateIORequest(port, sizeof(struct PaAhiRequest));
    if (r) {
        r->ahir_Version = 4;
        if (OpenDevice((CONST_STRPTR)"ahi.device", PA_AHI_UNIT, (struct IORequest *)r, 0) == 0) {
            ok = 1;
            CloseDevice((struct IORequest *)r);
        }
        DeleteIORequest((struct IORequest *)r);
    }
    DeleteMsgPort(port);
    return ok;
}

PaError PaAmigaAhi_Initialize(PaUtilHostApiRepresentation **hostApi, PaHostApiIndex hostApiIndex)
{
    PaAhiHostApi *ahi;
    PaDeviceInfo *dev;
    PaError err = paNoError;
    const int present = AhiPresent();

    ahi = (PaAhiHostApi *)PaUtil_AllocateMemory(sizeof(PaAhiHostApi));
    if (!ahi)
        return paInsufficientMemory;
    memset(ahi, 0, sizeof *ahi);
    ahi->allocations = PaUtil_CreateAllocationGroup();
    if (!ahi->allocations) {
        err = paInsufficientMemory;
        goto error;
    }

    *hostApi = &ahi->inheritedHostApiRep;
    (*hostApi)->info.structVersion = 1;
    (*hostApi)->info.type = paInDevelopment;
    (*hostApi)->info.name = "AHI";
    (*hostApi)->info.deviceCount = 0;
    (*hostApi)->info.defaultInputDevice = paNoDevice;
    (*hostApi)->info.defaultOutputDevice = paNoDevice;
    (*hostApi)->deviceInfos = NULL;

    if (present) {
        (*hostApi)->deviceInfos = (PaDeviceInfo **)PaUtil_GroupAllocateMemory(ahi->allocations, sizeof(PaDeviceInfo *));
        dev = (PaDeviceInfo *)PaUtil_GroupAllocateMemory(ahi->allocations, sizeof(PaDeviceInfo));
        if (!(*hostApi)->deviceInfos || !dev) {
            err = paInsufficientMemory;
            goto error;
        }
        memset(dev, 0, sizeof *dev);
        dev->structVersion = 2;
        dev->name = "ahi.device unit 0";
        dev->hostApi = hostApiIndex;
        dev->maxInputChannels = 2;
        dev->maxOutputChannels = 2;
        dev->defaultLowInputLatency = 0.05;
        dev->defaultLowOutputLatency = 0.05;
        dev->defaultHighInputLatency = 0.2;
        dev->defaultHighOutputLatency = 0.2;
        dev->defaultSampleRate = 44100.0;
        (*hostApi)->deviceInfos[0] = dev;
        (*hostApi)->info.deviceCount = 1;
        (*hostApi)->info.defaultInputDevice = 0;
        (*hostApi)->info.defaultOutputDevice = 0;
    }

    (*hostApi)->Terminate = Terminate;
    (*hostApi)->OpenStream = OpenStream;
    (*hostApi)->IsFormatSupported = IsFormatSupported;

    PaUtil_InitializeStreamInterface(&ahi->callbackStreamInterface, CloseStream, StartStream,
                                     StopStream, AbortStream, IsStreamStopped, IsStreamActive,
                                     GetStreamTime, GetStreamCpuLoad,
                                     PaUtil_DummyRead, PaUtil_DummyWrite,
                                     PaUtil_DummyGetReadAvailable, PaUtil_DummyGetWriteAvailable);
    PaUtil_InitializeStreamInterface(&ahi->blockingStreamInterface, CloseStream, StartStream,
                                     StopStream, AbortStream, IsStreamStopped, IsStreamActive,
                                     GetStreamTime, PaUtil_DummyGetCpuLoad,
                                     ReadStream, WriteStream, GetStreamReadAvailable, GetStreamWriteAvailable);
    return paNoError;

error:
    if (ahi->allocations) {
        PaUtil_FreeAllAllocations(ahi->allocations);
        PaUtil_DestroyAllocationGroup(ahi->allocations);
    }
    PaUtil_FreeMemory(ahi);
    return err;
}
