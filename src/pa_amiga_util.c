/*
 * GFOpenPortAudio: what PortAudio's common code asks of the OS on AmigaOS
 * 3.x (in place of pa_unix_util.c and pa_unix_hostapis.c): memory, the
 * clock, Pa_Sleep and the list of host APIs, which is AHI's alone.
 *
 * Copyright (c) 2026 AmiCradle Team
 */

#include <stdlib.h>
#include <sys/time.h>

#include <dos/dos.h>
#include <proto/dos.h>

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

void *PaUtil_AllocateMemory(long size)
{
    void *p = malloc(size > 0 ? (size_t)size : 1);
    if (p)
        s_blocks++;
    return p;
}

void PaUtil_FreeMemory(void *block)
{
    if (block) {
        free(block);
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
    gettimeofday(&tv, NULL);
    return (PaTime)tv.tv_sec + (PaTime)tv.tv_usec / 1000000.0;
}

/* dos.library's Delay counts 50ths of a second: at least one when asked
 * for any time at all */
void Pa_Sleep(long msec)
{
    if (msec <= 0)
        return;
    Delay((msec * 50 + 999) / 1000);
}
