# GFOpenPortAudio

PortAudio for AmigaOS 3 (68040) as `portaudio.library`, with a host API of
our own for `ahi.device`. Programs written for PortAudio, such as Audacity,
play and record through AHI, and share the one library in `LIBS:`.

- One device, AHI unit 0: up to two channels out and two in, 16-bit, at any
  rate AHI mixes at.
- Each stream runs in a task of its own with two AHI requests in flight
  each way, so playback does not stop between buffers.
- Callback streams. Blocking streams (no callback) are refused.
- The library's calls are PortAudio's own (`portaudio.h`), in the order of
  `portaudio_lib.fd`, with their arguments on the stack. A program links
  `libportaudio.a`, whose stubs open `portaudio.library` (version 19) on the
  first call and close it when the program exits.

PortAudio itself is fetched at build time at the version in `pa.lock`
(checked by SHA-256) and is never kept here. This repository holds only the
AHI host API and OS layer (`src/`), the library around PortAudio and the
stub maker (`lib/`), the test (`tests/`) and the build.

## Build

    STOVE=$HOME/AmiCradle/stoves/os32-gcc16-open6 OUT=out ./build-amiga.sh

`out/libs` gets `portaudio.library` (copy it to `LIBS:`). `out/sdk` gets
`lib/libportaudio.a` (the stubs), `include/portaudio.h`,
`lib/pkgconfig/portaudio-2.0.pc` and `fd/portaudio_lib.fd`. `out` gets
`pa_ahi_test` (our test) and PortAudio's `pa_devs` and `paex_sine`
examples, which all go through the library.

Built for the 68040 with soft float.

Designed, tested and delivered by The AmiCradle Team.
