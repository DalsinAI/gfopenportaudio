#!/bin/sh
# Builds GFOpenPortAudio: PortAudio's common code (fetched at build time)
# with our AHI host API and OS layer as portaudio.library in $OUT/libs; in
# $OUT/sdk what programs build with (libportaudio.a, the stubs that open the
# library; portaudio.h; portaudio-2.0.pc; fd/portaudio_lib.fd); and the tests
# (pa_ahi_test, and PortAudio's own pa_devs and paex_sine examples) in $OUT.
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
SPECS="-specs=$HERE/scripts/m68k-soft-m68040.specs"
# library code: no C library start-up runs in it, so nothing that needs one
# (stdio, malloc, assert), and no builtins turned into calls it lacks
CF="-noixemul -m68040 -O2 $LATE $SPECS -DNDEBUG -DPA_BIG_ENDIAN -DPA_USE_AHI=1 -I$SRC/include -I$SRC/src/common -Wall
    -fno-delete-null-pointer-checks -fno-builtin -fno-tree-loop-distribute-patterns"
O="$WORK/obj"; rm -rf "$O"; mkdir -p "$O"
for f in pa_allocation pa_converters pa_cpuload pa_dither pa_front pa_process pa_ringbuffer pa_stream pa_trace; do
    m68k-amigaos-gcc $CF -c "$SRC/src/common/$f.c" -o "$O/$f.o"
done
for f in pa_amiga_ahi pa_amiga_util; do
    m68k-amigaos-gcc $CF -Wextra -c "$HERE/src/$f.c" -o "$O/$f.o"
done

# portaudio.library: portaudio_lib.c first (its start and RomTag), then
# PortAudio; libnix gives only its plain string calls, its libm soft float
mkdir -p "$OUT/libs"
m68k-amigaos-gcc $CF -fomit-frame-pointer -fno-toplevel-reorder -nostartfiles -o "$OUT/libs/portaudio.library" \
    "$HERE/lib/portaudio_lib.c" "$O"/*.o -lm -lnix -lgcc -lm
UND=$(m68k-amigaos-nm -u "$OUT/libs/portaudio.library" 2>/dev/null || true)
[ -z "$UND" ] || { echo "portaudio.library: calls nothing has: $UND" >&2; exit 1; }
for bad in _malloc _free _vfprintf _pthread_create _gettimeofday ___assert_func; do
    if m68k-amigaos-nm "$OUT/libs/portaudio.library" | grep -q " $bad\$"; then
        echo "portaudio.library: $bad has come into the library" >&2; exit 1
    fi
done
m68k-amigaos-strip "$OUT/libs/portaudio.library" 2>/dev/null || echo "strip failed: portaudio.library left unstripped"

# a program's side: libportaudio.a holds the stubs, which open the library
SDK="$OUT/sdk"; mkdir -p "$SDK/lib/pkgconfig" "$SDK/include" "$SDK/fd"
python3 "$HERE/lib/make-stubs.py" "$WORK/portaudio_stubs.c" "$SDK/fd/portaudio_lib.fd"
m68k-amigaos-gcc -noixemul -m68040 -O2 $LATE $SPECS -I$SRC/include -Wall -c "$WORK/portaudio_stubs.c" -o "$WORK/portaudio_stubs.o"
rm -f "$SDK/lib/libportaudio.a"
m68k-amigaos-ar rcs "$SDK/lib/libportaudio.a" "$WORK/portaudio_stubs.o"
cp "$SRC/include/portaudio.h" "$SDK/include/"
cat > "$SDK/lib/pkgconfig/portaudio-2.0.pc" <<PC
prefix=\${pcfiledir}/../..
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: PortAudio
Description: Portable audio I/O (GFOpenPortAudio: portaudio.library on AHI)
Version: $PA_VERSION
Libs: -L\${libdir} -lportaudio
Cflags: -I\${includedir}
PC

# the tests, through the library like any program
L="$SDK/lib/libportaudio.a -lm"
TF="-noixemul -m68040 -O2 $LATE $SPECS -I$SRC/include -Wall"
m68k-amigaos-gcc $TF -o "$OUT/pa_ahi_test" "$HERE/tests/pa_ahi_test.c" $L
m68k-amigaos-gcc $TF -o "$OUT/pa_devs" "$SRC/examples/pa_devs.c" $L
m68k-amigaos-gcc $TF -o "$OUT/paex_sine" "$SRC/examples/paex_sine.c" $L
# the stove's strip has crashed on these (a host tool fault): keep them whole then
for t in pa_ahi_test pa_devs paex_sine; do
    m68k-amigaos-strip "$OUT/$t" 2>/dev/null || echo "strip failed on $t: left unstripped"
done
ls -l "$OUT" "$OUT/libs" "$SDK/lib/libportaudio.a"
