#!/usr/bin/env python3
# Writes portaudio_stubs.c (a program's side of portaudio.library) and
# portaudio_lib.fd from the one list of calls below, in LVO order.
# Copyright (c) 2026 AmiCradle Team
import sys

# (return type, name, arguments, what a call gives when the library is missing)
CALLS = [
    ("int", "Pa_GetVersion", "void", "0"),
    ("const char *", "Pa_GetVersionText", "void", "MISSING"),
    ("const PaVersionInfo *", "Pa_GetVersionInfo", "void", "NULL"),
    ("const char *", "Pa_GetErrorText", "PaError errorCode", "MISSING"),
    ("PaError", "Pa_Initialize", "void", "paHostApiNotFound"),
    ("PaError", "Pa_Terminate", "void", "paNotInitialized"),
    ("PaHostApiIndex", "Pa_GetHostApiCount", "void", "paNotInitialized"),
    ("PaHostApiIndex", "Pa_GetDefaultHostApi", "void", "paNotInitialized"),
    ("const PaHostApiInfo *", "Pa_GetHostApiInfo", "PaHostApiIndex hostApi", "NULL"),
    ("PaHostApiIndex", "Pa_HostApiTypeIdToHostApiIndex", "PaHostApiTypeId type", "paNotInitialized"),
    ("PaDeviceIndex", "Pa_HostApiDeviceIndexToDeviceIndex", "PaHostApiIndex hostApi, int hostApiDeviceIndex", "paNotInitialized"),
    ("const PaHostErrorInfo *", "Pa_GetLastHostErrorInfo", "void", "NULL"),
    ("PaDeviceIndex", "Pa_GetDeviceCount", "void", "paNotInitialized"),
    ("PaDeviceIndex", "Pa_GetDefaultInputDevice", "void", "paNoDevice"),
    ("PaDeviceIndex", "Pa_GetDefaultOutputDevice", "void", "paNoDevice"),
    ("const PaDeviceInfo *", "Pa_GetDeviceInfo", "PaDeviceIndex device", "NULL"),
    ("PaError", "Pa_IsFormatSupported", "const PaStreamParameters *inputParameters, const PaStreamParameters *outputParameters, double sampleRate", "paNotInitialized"),
    ("PaError", "Pa_OpenStream", "PaStream **stream, const PaStreamParameters *inputParameters, const PaStreamParameters *outputParameters, double sampleRate, unsigned long framesPerBuffer, PaStreamFlags streamFlags, PaStreamCallback *streamCallback, void *userData", "paNotInitialized"),
    ("PaError", "Pa_OpenDefaultStream", "PaStream **stream, int numInputChannels, int numOutputChannels, PaSampleFormat sampleFormat, double sampleRate, unsigned long framesPerBuffer, PaStreamCallback *streamCallback, void *userData", "paNotInitialized"),
    ("PaError", "Pa_CloseStream", "PaStream *stream", "paNotInitialized"),
    ("PaError", "Pa_SetStreamFinishedCallback", "PaStream *stream, PaStreamFinishedCallback *streamFinishedCallback", "paNotInitialized"),
    ("PaError", "Pa_StartStream", "PaStream *stream", "paNotInitialized"),
    ("PaError", "Pa_StopStream", "PaStream *stream", "paNotInitialized"),
    ("PaError", "Pa_AbortStream", "PaStream *stream", "paNotInitialized"),
    ("PaError", "Pa_IsStreamStopped", "PaStream *stream", "paNotInitialized"),
    ("PaError", "Pa_IsStreamActive", "PaStream *stream", "paNotInitialized"),
    ("const PaStreamInfo *", "Pa_GetStreamInfo", "PaStream *stream", "NULL"),
    ("PaTime", "Pa_GetStreamTime", "PaStream *stream", "0"),
    ("double", "Pa_GetStreamCpuLoad", "PaStream *stream", "0"),
    ("PaError", "Pa_ReadStream", "PaStream *stream, void *buffer, unsigned long frames", "paNotInitialized"),
    ("PaError", "Pa_WriteStream", "PaStream *stream, const void *buffer, unsigned long frames", "paNotInitialized"),
    ("signed long", "Pa_GetStreamReadAvailable", "PaStream *stream", "paNotInitialized"),
    ("signed long", "Pa_GetStreamWriteAvailable", "PaStream *stream", "paNotInitialized"),
    ("PaError", "Pa_GetSampleSize", "PaSampleFormat format", "paNotInitialized"),
    ("void", "Pa_Sleep", "long msec", None),
]

def names(args):
    if args == "void":
        return ""
    out = []
    for a in args.split(","):
        a = a.strip()
        out.append(a.replace("*", " ").split()[-1])
    return ", ".join(out)

def stubs():
    o = ["/* portaudio_stubs.c: written by make-stubs.py, do not edit.",
         " * A program's side of portaudio.library: each call opens the library the",
         " * first time and calls its vector as a C function (the arguments go on",
         " * the stack); the library closes when the program exits.",
         " * Copyright (c) 2026 AmiCradle Team */",
         "#include <stdlib.h>",
         "#include <exec/libraries.h>",
         "#include <proto/exec.h>",
         '#include "portaudio.h"',
         "",
         "struct Library *PortAudioBase;",
         'static const char missing[] = "portaudio.library 19 could not be opened";',
         "#define MISSING missing",
         "",
         "static void PaLibClose(void)",
         "{",
         "    if (PortAudioBase)",
         "        CloseLibrary(PortAudioBase);",
         "    PortAudioBase = NULL;",
         "}",
         "",
         "static int PaLib(void)",
         "{",
         "    if (!PortAudioBase) {",
         '        PortAudioBase = OpenLibrary((CONST_STRPTR)"portaudio.library", 19);',
         "        if (PortAudioBase)",
         "            atexit(PaLibClose);",
         "    }",
         "    return PortAudioBase != NULL;",
         "}",
         "",
         "#define LVO(o) ((char *)PortAudioBase - (o))",
         ""]
    for i, (ret, name, args, miss) in enumerate(CALLS):
        lvo = 30 + 6 * i
        o.append(f"{ret} {name}({args})")
        o.append("{")
        call = f"(({ret} (*)({args}))LVO({lvo}))({names(args)})"
        if ret == "void":
            o.append(f"    if (PaLib())\n        {call};")
        else:
            o.append(f"    if (!PaLib())\n        return {miss};")
            o.append(f"    return {call};")
        o.append("}")
        o.append("")
    return "\n".join(o)

def fd():
    o = ["* portaudio.library (GFOpenPortAudio), written by make-stubs.py",
         "* The calls take their arguments on the stack, as PortAudio's C functions",
         "* do (portaudio.h): call the vector as a C function. No registers.",
         "##base _PortAudioBase", "##bias 30", "##public"]
    for ret, name, args, miss in CALLS:
        o.append(f"{name}({names(args).replace(' ', '')})()")
    o.append("##end")
    return "\n".join(o) + "\n"

open(sys.argv[1], "w").write(stubs())
open(sys.argv[2], "w").write(fd())
