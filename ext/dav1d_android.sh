#!/bin/bash

# This script will build dav1d for the default ABI targets supported by android.
# This script only works on linux. You must pass the path to the android NDK as
# a parameter to this script.
#
# The build is configured for 8-bit AV1 only (-Dbitdepths=8) to reduce binary
# size. 10/12-bit AVIF images will fail to decode with this configuration.
#
# Android NDK: https://developer.android.com/ndk/downloads
#
# Android JNI uses link-u/dav1d (avif branch) rather than upstream videolan.
# Feel free to update the branch/commit as needed.

set -e

if [ $# -ne 1 ]; then
  echo "Usage: ${0} <path_to_android_ndk>"
  exit 1
fi
git clone -b avif --depth 1 https://github.com/link-u/dav1d.git
mkdir dav1d/build

# This only works on linux and mac.
if [ "$(uname)" == "Darwin" ]; then
  HOST_TAG="darwin"
else
  HOST_TAG="linux"
fi
android_bin="${1}/toolchains/llvm/prebuilt/${HOST_TAG}-x86_64/bin"

ABI_LIST=("armeabi-v7a" "arm64-v8a" "x86" "x86_64")
ARCH_LIST=("arm" "aarch64" "x86" "x86_64")
for i in "${!ABI_LIST[@]}"; do
  abi="${ABI_LIST[i]}"
  # -Db_lto=true so the static archive participates in the Android JNI LTO link.
  PATH=$PATH:${android_bin} meson setup --default-library=static --buildtype release \
    --cross-file="dav1d/package/crossfiles/${ARCH_LIST[i]}-android.meson" \
    -Db_lto=true -Dlogging=false -Dbitdepths=8 -Denable_tools=false -Denable_tests=false \
    -Denable_docs=false \
    -Denable_filmgrain=false \
    -Denable_422_444=false \
    -Denable_frame_delay=false \
    -Denable_superres=false \
    -Denable_warp=false \
    -Denable_compound=false \
    "dav1d/build/${abi}" dav1d
  PATH=$PATH:${android_bin} meson compile -C "dav1d/build/${abi}"
done
