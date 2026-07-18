#!/bin/bash

# This script will build dav1d for the default ABI targets supported by android.
# This script works on Linux and macOS. You must pass the path to the android NDK
# as a parameter to this script (host-matching NDK; e.g. the Linux NDK under WSL).
#
# Compatible with Android NDK r27+ (tested with r27 / 27.x). NDK r27 dropped
# API levels below 21, so this script generates meson cross-files that target
# API 21 for every ABI (matches android_jni minSdk) instead of using dav1d's
# stock package/crossfiles (x86 still referenced API 19).
#
# The build is configured for 8-bit AV1 only (-Dbitdepths=8) to reduce binary
# size. 10/12-bit AVIF images will fail to decode with this configuration.
#
# Android NDK: https://developer.android.com/ndk/downloads
#
# Android JNI uses link-u/dav1d (avif branch) rather than upstream videolan.
# Feel free to update the branch/commit as needed.
#
# Optional env:
#   ANDROID_API  - Android API level for the NDK clang wrappers (default: 21)

set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: ${0} <path_to_android_ndk>"
  exit 1
fi

NDK="${1}"
API_LEVEL="${ANDROID_API:-21}"

case "$(uname -s)" in
  Darwin)
    case "$(uname -m)" in
      arm64|aarch64) HOST_TAG="darwin-arm64" ;;
      *) HOST_TAG="darwin-x86_64" ;;
    esac
    ;;
  Linux)
    HOST_TAG="linux-x86_64"
    ;;
  *)
    echo "Unsupported host OS: $(uname -s) (use Linux or macOS with a matching NDK)" >&2
    exit 1
    ;;
esac

android_bin="${NDK}/toolchains/llvm/prebuilt/${HOST_TAG}/bin"
if [ ! -d "${android_bin}" ]; then
  echo "NDK toolchain not found at: ${android_bin}" >&2
  echo "Pass a host-matching NDK path (Linux NDK on Linux/WSL, Darwin NDK on macOS)." >&2
  exit 1
fi

if [ ! -d dav1d ]; then
  git clone -b avif --depth 1 https://github.com/link-u/dav1d.git
fi
mkdir -p dav1d/build

write_cross_file() {
  local out="$1"
  local cpu_family="$2"
  local cpu="$3"
  local clang_prefix="$4"
  local c_clang="${android_bin}/${clang_prefix}${API_LEVEL}-clang"
  local cxx_clang="${android_bin}/${clang_prefix}${API_LEVEL}-clang++"

  if [ ! -x "${c_clang}" ]; then
    echo "Compiler not found or not executable: ${c_clang}" >&2
    echo "NDK r27+ requires API >= 21; set ANDROID_API if needed (current: ${API_LEVEL})." >&2
    exit 1
  fi

  cat > "${out}" <<EOF
[binaries]
c = '${c_clang}'
cpp = '${cxx_clang}'
ar = '${android_bin}/llvm-ar'
strip = '${android_bin}/llvm-strip'
pkg-config = 'pkg-config'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'android'
cpu_family = '${cpu_family}'
cpu = '${cpu}'
endian = 'little'
EOF
}

# abi cpu_family cpu clang_prefix (NDK triple before API level)
# e.g. aarch64-linux-android + 21 -> aarch64-linux-android21-clang
build_one_abi() {
  local abi="$1"
  local cpu_family="$2"
  local cpu="$3"
  local clang_prefix="$4"
  local build_dir="dav1d/build/${abi}"
  local cross_file="${build_dir}/android-cross.meson"

  mkdir -p "${build_dir}"
  write_cross_file "${cross_file}" "${cpu_family}" "${cpu}" "${clang_prefix}"

  local meson_extra=()
  if [ -d "${build_dir}/meson-private" ]; then
    meson_extra+=(--reconfigure)
  fi

  # -Db_lto=true so the static archive participates in the Android JNI LTO link.
  PATH="${android_bin}:${PATH}" meson setup --default-library=static --buildtype release \
    --cross-file="${cross_file}" \
    -Db_lto=true -Dlogging=false -Dbitdepths=8 -Denable_tools=false -Denable_tests=false \
    -Denable_docs=false \
    -Denable_filmgrain=false \
    -Denable_422_444=false \
    -Denable_frame_delay=false \
    -Denable_superres=false \
    -Denable_warp=false \
    -Denable_compound=false \
    "${meson_extra[@]}" \
    "${build_dir}" dav1d
  PATH="${android_bin}:${PATH}" meson compile -C "${build_dir}"
}

build_one_abi armeabi-v7a arm arm armv7a-linux-androideabi
build_one_abi arm64-v8a aarch64 aarch64 aarch64-linux-android
build_one_abi x86 x86 i686 i686-linux-android
build_one_abi x86_64 x86_64 x86_64 x86_64-linux-android
