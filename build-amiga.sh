#!/bin/sh
# Builds GFOpenPortAudio with a stove: PortAudio (fetched at build time, at
# the version pa.lock gives) with our AHI host API as portaudio.library in
# $OUT/libs, the SDK programs build with in $OUT/sdk, and the tests in $OUT.
#   STOVE=.../stoves/os32-gcc16-open6 OUT=out ./build-amiga.sh
# Copyright (c) 2026 AmiCradle Team
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open6"}
TOOLCHAIN="$STOVE/prefix" WORK="${WORK:-$HERE/build}" OUT="${OUT:-$HERE/build/out}" \
    sh "$HERE/scripts/build-amiga.sh"
