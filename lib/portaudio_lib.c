/*
 * portaudio.library: GFOpenPortAudio (PortAudio with its AHI host API) as an
 * AmigaOS shared library, so programs share one copy and pick up a new one
 * without being linked again.
 *
 * The calls are PortAudio's own C functions, in portaudio_lib.fd's order. They
 * take their arguments on the stack, as a C call does: the program's side
 * (libportaudio.a, portaudio_stubs.c) calls the vector as a C function, and
 * the library's code needs no base in a6 (its data is its own, absolute).
 * PortAudio's state (Pa_Initialize's count, the host API, the streams) is the
 * library's: programs that use it at once share the one AHI device.
 *
 * Copyright (c) 2026 AmiCradle Team
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <proto/exec.h>

#include "portaudio.h"

#ifndef REG
#define REG(r, decl) register decl __asm(#r)
#endif

#define LIB_VERSION  19
#define LIB_REVISION 7

struct PaLibBase {
    struct Library lib;
    BPTR seglist;
};

/* Set by lib_init, read-only after */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Device *TimerBase;
static struct timerequest s_timer;

/* libnix's soft float (libm020) does its sums through the mathieee
 * libraries, through these bases; a program's start-up opens them, and a
 * library has none, so lib_init opens them itself. Without them the first
 * double sum (Pa_IsFormatSupported's rate check) called through a NULL base
 * and hung. lib_open opens them, for the first program that opens it, not
 * lib_init: lib_init runs in ramlib, and loading another library from disk
 * there waits on ramlib itself. */
extern struct Library *MathIeeeDoubBasBase, *MathIeeeDoubTransBase,
                      *MathIeeeSingBasBase, *MathIeeeSingTransBase;
static struct Library **const s_mathBases[] = {
    &MathIeeeDoubBasBase, &MathIeeeDoubTransBase, &MathIeeeSingBasBase, &MathIeeeSingTransBase
};
static const char *const s_mathNames[] = {
    "mathieeedoubbas.library", "mathieeedoubtrans.library",
    "mathieeesingbas.library", "mathieeesingtrans.library"
};
static int s_timerOpen;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file (-fno-toplevel-reorder), and the file first in the link. */
int start(void) { return -1; }

static const char lib_name[] = "portaudio.library";
static const char lib_id[] = "portaudio.library 19.7 (05.11.2026) GFOpenPortAudio, AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: portaudio.library 19.7 (05.11.2026) GFOpenPortAudio, AmiCradle Team";

/* ---- housekeeping ---------------------------------------------------- */

static void lib_free(struct PaLibBase *base)
{
    if (s_timerOpen)
        CloseDevice((struct IORequest *)&s_timer);
    s_timerOpen = 0;
    TimerBase = NULL;
    {
        unsigned i;
        for (i = 0; i < sizeof s_mathBases / sizeof s_mathBases[0]; i++) {
            if (*s_mathBases[i])
                CloseLibrary(*s_mathBases[i]);
            *s_mathBases[i] = NULL;
        }
    }
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
}

static struct Library *lib_init(REG(d0, struct PaLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    /* the clock (PaUtil_GetTime): timer.device's GetSysTime */
    if (DOSBase && OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&s_timer, 0) == 0) {
        s_timerOpen = 1;
        TimerBase = s_timer.tr_node.io_Device;
    }
    if (!DOSBase || !TimerBase) {
        lib_free(base);
        return NULL;
    }
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct PaLibBase *base))
{
    unsigned i;
    for (i = 0; i < sizeof s_mathBases / sizeof s_mathBases[0]; i++)
        if (!*s_mathBases[i] && !(*s_mathBases[i] = OpenLibrary((CONST_STRPTR)s_mathNames[i], 37)))
            return NULL;
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_expunge(REG(a6, struct PaLibBase *base));

static BPTR lib_close(REG(a6, struct PaLibBase *base))
{
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0) {
        /* the last program has gone: what it left initialised goes too */
        while (Pa_Terminate() == paNoError)
            ;
        if (base->lib.lib_Flags & LIBF_DELEXP)
            return lib_expunge(base);
    }
    return 0;
}

static BPTR lib_expunge(REG(a6, struct PaLibBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    lib_free(base);
    return seglist;
}

static ULONG lib_null(void)
{
    return 0;
}

/* ---- PortAudio's debug output: none from a library ------------------- */

typedef void (*PaUtilLogCallback)(const char *log);

void PaUtil_SetDebugPrintFunction(PaUtilLogCallback cb)
{
    (void)cb;
}

void PaUtil_DebugPrint(const char *format, ...)
{
    (void)format;
}

/* ---- the calls, in portaudio_lib.fd's order --------------------------- */

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)Pa_GetVersion,                        /*  -30 */
    (APTR)Pa_GetVersionText,                    /*  -36 */
    (APTR)Pa_GetVersionInfo,                    /*  -42 */
    (APTR)Pa_GetErrorText,                      /*  -48 */
    (APTR)Pa_Initialize,                        /*  -54 */
    (APTR)Pa_Terminate,                         /*  -60 */
    (APTR)Pa_GetHostApiCount,                   /*  -66 */
    (APTR)Pa_GetDefaultHostApi,                 /*  -72 */
    (APTR)Pa_GetHostApiInfo,                    /*  -78 */
    (APTR)Pa_HostApiTypeIdToHostApiIndex,       /*  -84 */
    (APTR)Pa_HostApiDeviceIndexToDeviceIndex,   /*  -90 */
    (APTR)Pa_GetLastHostErrorInfo,              /*  -96 */
    (APTR)Pa_GetDeviceCount,                    /* -102 */
    (APTR)Pa_GetDefaultInputDevice,             /* -108 */
    (APTR)Pa_GetDefaultOutputDevice,            /* -114 */
    (APTR)Pa_GetDeviceInfo,                     /* -120 */
    (APTR)Pa_IsFormatSupported,                 /* -126 */
    (APTR)Pa_OpenStream,                        /* -132 */
    (APTR)Pa_OpenDefaultStream,                 /* -138 */
    (APTR)Pa_CloseStream,                       /* -144 */
    (APTR)Pa_SetStreamFinishedCallback,         /* -150 */
    (APTR)Pa_StartStream,                       /* -156 */
    (APTR)Pa_StopStream,                        /* -162 */
    (APTR)Pa_AbortStream,                       /* -168 */
    (APTR)Pa_IsStreamStopped,                   /* -174 */
    (APTR)Pa_IsStreamActive,                    /* -180 */
    (APTR)Pa_GetStreamInfo,                     /* -186 */
    (APTR)Pa_GetStreamTime,                     /* -192 */
    (APTR)Pa_GetStreamCpuLoad,                  /* -198 */
    (APTR)Pa_ReadStream,                        /* -204 */
    (APTR)Pa_WriteStream,                       /* -210 */
    (APTR)Pa_GetStreamReadAvailable,            /* -216 */
    (APTR)Pa_GetStreamWriteAvailable,           /* -222 */
    (APTR)Pa_GetSampleSize,                     /* -228 */
    (APTR)Pa_Sleep,                             /* -234 */
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct PaLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};
