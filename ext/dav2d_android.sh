#!/bin/bash

# This script will build dav2d for the default ABI targets supported by android.
# This script works on Linux and macOS. You must pass the path to the android NDK
# as a parameter to this script (host-matching NDK; e.g. the Linux NDK under WSL).
#
# Compatible with Android NDK r27+ (tested with r27 / 27.x). Generates meson
# cross-files that target API 21 for every ABI (matches android_jni minSdk).
#
# The build is configured for 8-bit AV2 only (-Dbitdepths=8) to reduce binary
# size. 10-bit AVIF images will fail to decode with this configuration.
#
# Android NDK: https://developer.android.com/ndk/downloads
#
# Optional env:
#   ANDROID_API  - Android API level for the NDK clang wrappers (default: 21)
#
# x86 / x86_64 asm uses NASM preprocessor function %isidn(), which needs NASM
# 2.16+. Ubuntu 22.04 / common WSL packages ship 2.15.05, which fails with
# "symbol `%isidn' not defined". ARM ABIs use GAS and are unaffected. If NASM
# is older than 2.16, those x86 ABIs are built with -Denable_asm=false.

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

if [ ! -d dav2d ]; then
  git clone --depth 1 --branch 0.0.1 https://code.videolan.org/videolan/dav2d.git
fi
mkdir -p dav2d/build

# dav2d x86 .asm uses %isidn(); that preprocessor function exists in NASM 2.16+.
nasm_supports_isidn() {
  command -v nasm >/dev/null 2>&1 || return 1
  local ver major minor
  ver="$(nasm -v 2>/dev/null | awk '{print $3}')"
  major="${ver%%.*}"
  minor="${ver#*.}"
  minor="${minor%%.*}"
  [ "${major:-0}" -gt 2 ] || { [ "${major:-0}" -eq 2 ] && [ "${minor:-0}" -ge 16 ]; }
}

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
build_one_abi() {
  local abi="$1"
  local cpu_family="$2"
  local cpu="$3"
  local clang_prefix="$4"
  local build_dir="dav2d/build/${abi}"
  local cross_file="${build_dir}/android-cross.meson"

  mkdir -p "${build_dir}"
  write_cross_file "${cross_file}" "${cpu_family}" "${cpu}" "${clang_prefix}"

  local meson_extra=()
  if [ -d "${build_dir}/meson-private" ]; then
    meson_extra+=(--reconfigure)
  fi

  # ARM uses GAS. x86 uses NASM; %isidn() requires 2.16+ (see script header).
  local enable_asm=true
  case "${cpu_family}" in
    x86|x86_64)
      if ! nasm_supports_isidn; then
        echo "dav2d ${abi}: NASM $(nasm -v 2>/dev/null | awk '{print $3}' || echo 'not found') lacks %isidn (need 2.16+); building without x86 asm." >&2
        enable_asm=false
      fi
      ;;
  esac

  PATH="${android_bin}:${PATH}" meson setup --default-library=static --buildtype release \
    --cross-file="${cross_file}" \
    -Db_lto=true -Dlogging=false -Dbitdepths=8 -Denable_tools=false -Denable_tests=false \
    -Denable_docs=false -Denable_examples=false -Denable_asm="${enable_asm}" \
    "${meson_extra[@]}" \
    "${build_dir}" dav2d
  PATH="${android_bin}:${PATH}" meson compile -C "${build_dir}"
}

build_one_abi armeabi-v7a arm arm armv7a-linux-androideabi
build_one_abi arm64-v8a aarch64 aarch64 aarch64-linux-android
build_one_abi x86 x86 i686 i686-linux-android
build_one_abi x86_64 x86_64 x86_64 x86_64-linux-android
