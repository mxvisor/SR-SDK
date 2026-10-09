#!/bin/sh
#
#  Copyright (C) 2026 mxvisor
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy of
#  this software and associated documentation files (the "Software"), to deal in
#  the Software without restriction, including without limitation the rights to
#  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
#  of the Software, and to permit persons to whom the Software is furnished to do
#  so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in all
#  copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE.
#

# SDL2 and SDL2_mixer for device=arm64-android, built with the NDK into the
# sysroot scons/game.py links against:
#
#     <build>/sysroot-android-arm64/usr/{include/SDL2,lib/libSDL2*.so}
#
# SDL's version is the one of the Java side in android/app/src/main/java/
# org/libsdl/app/: SDLActivity refuses to start with any other.  SDL2_mixer
# plays wav, ogg (stb_vorbis), flac (dr_flac) and mp3 (minimp3), all of them
# built in; nothing that needs another library.
#
# usage: android_sdl.sh <build dir>      ANDROID_NDK_HOME=<sdk>/ndk/<version>

set -e

SDL_VERSION=2.30.11
SDL_SHA256=8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f
MIXER_VERSION=2.8.1
MIXER_SHA256=cb760211b056bfe44f4a1e180cc7cb201137e4d1572f2002cc1be728efd22660
API=21

[ $# -eq 1 ] || { echo "usage: $0 <build dir>" >&2; exit 2; }
BUILD=$(cd "$1" 2>/dev/null && pwd) || { mkdir -p "$1"; BUILD=$(cd "$1" && pwd); }
[ -n "$ANDROID_NDK_HOME" ] || { echo "set ANDROID_NDK_HOME to <sdk>/ndk/<version>" >&2; exit 2; }
TOOLCHAIN=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake
[ -f "$TOOLCHAIN" ] || { echo "not an NDK: $ANDROID_NDK_HOME" >&2; exit 2; }

PREFIX=$BUILD/sysroot-android-arm64/usr
WORK=$BUILD/android-sdl
mkdir -p "$WORK"

# fetch <url> <file> <sha256>: downloads once, checks every time
fetch() {
    if [ ! -f "$WORK/$2" ]; then
        curl -fL -o "$WORK/$2.part" "$1"
        mv "$WORK/$2.part" "$WORK/$2"
    fi
    echo "$3  $WORK/$2" | sha256sum -c - >/dev/null || {
        echo "sha256 mismatch: $WORK/$2" >&2; exit 1; }
}

# build <source dir> <cmake options...>
build() {
    src=$WORK/$1; shift
    rm -rf "$src/build-android"
    cmake -S "$src" -B "$src/build-android" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=$API -DANDROID_STL=c++_shared \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_FIND_ROOT_PATH="$PREFIX/.." "$@"
    cmake --build "$src/build-android"
    cmake --install "$src/build-android"
}

fetch https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz \
      SDL2-$SDL_VERSION.tar.gz $SDL_SHA256
fetch https://github.com/libsdl-org/SDL_mixer/releases/download/release-$MIXER_VERSION/SDL2_mixer-$MIXER_VERSION.tar.gz \
      SDL2_mixer-$MIXER_VERSION.tar.gz $MIXER_SHA256
tar -C "$WORK" -xzf "$WORK/SDL2-$SDL_VERSION.tar.gz"
tar -C "$WORK" -xzf "$WORK/SDL2_mixer-$MIXER_VERSION.tar.gz"

build SDL2-$SDL_VERSION -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
build SDL2_mixer-$MIXER_VERSION -DSDL2_DIR="$PREFIX/lib/cmake/SDL2" \
    -DBUILD_SHARED_LIBS=ON -DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_VENDORED=OFF \
    -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_MOD=OFF -DSDL2MIXER_MIDI=OFF \
    -DSDL2MIXER_WAVPACK=OFF

echo "OK: $PREFIX"
