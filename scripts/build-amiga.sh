#!/bin/sh
# Builds GFOpenPortAudio: PortAudio's common code (fetched at build time)
# with our AHI host API and OS layer, as libportaudio.a, portaudio.h and
# portaudio-2.0.pc in $OUT/sdk, and the tests (pa_ahi_test, and PortAudio's
# own pa_devs and paex_sine examples) in $OUT.
#   TOOLCHAIN=.../prefix WORK=build OUT=out scripts/build-amiga.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$HERE/pa.lock"
WORK=${WORK:-$HERE/build}; OUT=${OUT:-$WORK/out}
[ -n "${TOOLCHAIN:-}" ] && PATH="$TOOLCHAIN/bin:$PATH"
mkdir -p "$WORK" "$OUT"

T="$WORK/pa.tgz"; SRC="$WORK/portaudio-$PA_VERSION"
if [ ! -f "$SRC/.unpacked" ]; then
    curl -sSL --fail -o "$T" "$PA_URL"
    SUM=$(sha256sum "$T" | cut -d' ' -f1)
    echo "portaudio $PA_VERSION sha256 $SUM"
    if [ -n "$PA_SHA256" ] && [ "$SUM" != "$PA_SHA256" ]; then
        echo "portaudio: the download's sha256 is not the one in pa.lock" >&2
        exit 1
    fi
    rm -rf "${SRC:?}"; mkdir -p "$SRC"
    tar xzf "$T" -C "$SRC" --strip-components=1
    touch "$SRC/.unpacked"
fi

LATE=""
if m68k-amigaos-gcc -fno-late-combine-instructions -S -o /dev/null -x c /dev/null 2>/dev/null; then LATE="-fno-late-combine-instructions"; fi
CF="-noixemul -m68040 -O2 $LATE -DPA_BIG_ENDIAN -DPA_USE_AHI=1 -I$SRC/include -I$SRC/src/common -Wall"
SPECS="-specs=$HERE/scripts/m68k-soft-m68040.specs"
O="$WORK/obj"; rm -rf "$O"; mkdir -p "$O"
for f in pa_allocation pa_converters pa_cpuload pa_debugprint pa_dither pa_front pa_process pa_ringbuffer pa_stream pa_trace; do
    m68k-amigaos-gcc $CF -c "$SRC/src/common/$f.c" -o "$O/$f.o"
done
for f in pa_amiga_ahi pa_amiga_util; do
    m68k-amigaos-gcc $CF -Wextra -c "$HERE/src/$f.c" -o "$O/$f.o"
done
SDK="$OUT/sdk"; mkdir -p "$SDK/lib/pkgconfig" "$SDK/include"
rm -f "$SDK/lib/libportaudio.a"
m68k-amigaos-ar rcs "$SDK/lib/libportaudio.a" "$O"/*.o
cp "$SRC/include/portaudio.h" "$SDK/include/"
cat > "$SDK/lib/pkgconfig/portaudio-2.0.pc" <<PC
prefix=\${pcfiledir}/../..
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: PortAudio
Description: Portable audio I/O (GFOpenPortAudio: the AHI host API)
Version: $PA_VERSION
Libs: -L\${libdir} -lportaudio -lpthread
Cflags: -I\${includedir}
PC

L="$SDK/lib/libportaudio.a -lpthread -lm"
m68k-amigaos-gcc $CF $SPECS -o "$OUT/pa_ahi_test" "$HERE/tests/pa_ahi_test.c" $L
m68k-amigaos-gcc $CF $SPECS -o "$OUT/pa_devs" "$SRC/examples/pa_devs.c" $L
m68k-amigaos-gcc $CF $SPECS -o "$OUT/paex_sine" "$SRC/examples/paex_sine.c" $L
# the stove's strip has crashed on these (a host tool fault): keep them whole then
for t in pa_ahi_test pa_devs paex_sine; do
    m68k-amigaos-strip "$OUT/$t" 2>/dev/null || echo "strip failed on $t: left unstripped"
done
ls -l "$OUT" "$SDK/lib/libportaudio.a"
