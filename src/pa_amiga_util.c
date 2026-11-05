/*
 * GFOpenPortAudio: what PortAudio's common code asks of the OS on AmigaOS
 * 3.x (in place of pa_unix_util.c and pa_unix_hostapis.c): memory, the
 * clock, Pa_Sleep and the list of host APIs, which is AHI's alone. It is
 * library code (portaudio.library): exec's memory and timer.device's clock,
 * nothing from a C library's start-up.
 *
 * Copyright (c) 2026 AmiCradle Team
 */

#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include "portaudio.h"
#include "pa_util.h"
#include "pa_hostapi.h"

PaError PaAmigaAhi_Initialize(PaUtilHostApiRepresentation **hostApi, PaHostApiIndex index);

PaUtilHostApiInitializer *paHostApiInitializers[] = {
    PaAmigaAhi_Initialize,
    0
};

int paDefaultHostApiIndex = 0;

static volatile int s_blocks;
extern struct Device *TimerBase;   /* portaudio_lib.c */

void *PaUtil_AllocateMemory(long size)
{
    void *p = AllocVec(size > 0 ? (ULONG)size : 1, MEMF_ANY);
    if (p)
        s_blocks++;
    return p;
}

void PaUtil_FreeMemory(void *block)
{
    if (block) {
        FreeVec(block);
        s_blocks--;
    }
}

int PaUtil_CountCurrentlyAllocatedBlocks(void)
{
    return s_blocks;
}

void PaUtil_InitializeClock(void)
{
}

PaTime PaUtil_GetTime(void)
{
    struct timeval tv;
    GetSysTime(&tv);
    return (PaTime)tv.tv_secs + (PaTime)tv.tv_micro / 1000000.0;
}

/* dos.library's Delay counts 50ths of a second: at least one when asked
 * for any time at all */
void Pa_Sleep(long msec)
{
    if (msec <= 0)
        return;
    Delay((msec * 50 + 999) / 1000);
}
