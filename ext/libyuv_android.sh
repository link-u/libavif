#!/bin/bash

# This script will build libyuv for the default ABI targets supported by
# android. You must pass the path to the android NDK as a parameter to this
# script.
#
# Android NDK: https://developer.android.com/ndk/downloads

set -e

if [ $# -ne 1 ]; then
  echo "Usage: ${0} <path_to_android_ndk>"
  exit 1
fi

# avif branch: LIBYUV_AVIF_PROFILE (I420 Filter, I444 Matrix, H709/JPEG/F709).
git clone -b dav2d --depth 1 https://github.com/link-u/libyuv.git

cd libyuv
: # When changing the commit below to a newer version of libyuv, it is best to make sure it is being used by chromium,
: # because the test suite of chromium provides additional test coverage of libyuv.
: # It can be looked up at https://source.chromium.org/chromium/chromium/src/+/main:DEPS?q=libyuv.

mkdir build
cd build

ABI_LIST="armeabi-v7a arm64-v8a x86 x86_64"
for abi in ${ABI_LIST}; do
  mkdir "${abi}"
  cd "${abi}"
  # CMAKE_DISABLE_FIND_PACKAGE_JPEG: AVIF decode does not need libyuv's MJPEG
  # path; keep HAVE_JPEG undefined (matches LocalLibyuv.cmake FetchContent).
  # CMAKE_INTERPROCEDURAL_OPTIMIZATION so libyuv.a contains LTO bitcode for the
  # Android JNI shared library link. Strip -fuse-ld=gold which NDK r22+ removed.
  cmake ../.. \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_TOOLCHAIN_FILE=${1}/build/cmake/android.toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_DISABLE_FIND_PACKAGE_JPEG=TRUE \
    -DUNIT_TEST=OFF \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DCMAKE_C_LINK_OPTIONS_IPO= \
    -DCMAKE_CXX_LINK_OPTIONS_IPO= \
    -DANDROID_ABI=${abi}
  make yuv
  cd ..
done

cd ../..
