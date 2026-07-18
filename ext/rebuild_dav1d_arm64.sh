#!/bin/bash
set -ex
NDK=/mnt/c/linux/android-sdk/ndk/25.2.9519653
export PATH="$PATH:$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
which meson ninja
cd /mnt/c/linux/git/libavif/ext/dav1d/build/arm64-v8a
ninja -t targets 2>&1 | grep -i 'mc\|dav1d' | head -40
echo "==== explain ===="
ninja -d explain src/libdav1d.a 2>&1 | tail -50
echo "==== rebuild ===="
ninja -C /mnt/c/linux/git/libavif/ext/dav1d/build/arm64-v8a -t clean
ninja -C /mnt/c/linux/git/libavif/ext/dav1d/build/arm64-v8a src/libdav1d.a
ls -la src/libdav1d.a
