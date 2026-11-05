# GFOpenPortAudio

PortAudio for AmigaOS 3 (68040), with a host API of our own for
`ahi.device`. Programs written for PortAudio, such as Audacity, play and
record through AHI.

- One device, AHI unit 0: up to two channels out and two in, 16-bit, at any
  rate AHI mixes at.
- Each stream runs in a task of its own with two AHI requests in flight
  each way, so playback does not stop between buffers.
- Callback streams. Blocking streams (no callback) are refused.

PortAudio itself is fetched at build time at the version in `pa.lock`
(checked by SHA-256) and is never kept here. This repository holds only the
AHI host API (`src/`), its test (`tests/`) and the build.

## Build

    STOVE=$HOME/AmiCradle/stoves/os32-gcc16-open6 OUT=out ./build-amiga.sh

`out/sdk` gets `lib/libportaudio.a`, `include/portaudio.h` and
`lib/pkgconfig/portaudio-2.0.pc`. `out` gets `pa_ahi_test` (our test) and
PortAudio's `pa_devs` and `paex_sine` examples.

Built for the 68040 with soft float.

Designed, tested and delivered by The AmiCradle Team.
