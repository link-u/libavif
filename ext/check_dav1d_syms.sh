#!/bin/bash
set -e
cd /mnt/c/linux/git/libavif/ext/dav1d/build/arm64-v8a
ls -la src/libdav1d.a
echo "==== config ===="
grep -E 'CONFIG_WARP|CONFIG_COMPOUND' config.h
echo "==== symbols ===="
# Prefer llvm-nm from NDK if available
NM=nm
if command -v llvm-nm >/dev/null 2>&1; then NM=llvm-nm; fi
for ndk_ver in 27.2.12479018 27.0.12077973 25.2.9519653; do
  NDK_NM=/mnt/c/linux/android-sdk/ndk/${ndk_ver}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm
  if [ -x "$NDK_NM" ]; then NM="$NDK_NM"; break; fi
done
"$NM" src/libdav1d.a 2>/dev/null | grep -E 'obmc_masks|mc_warp_filter|blend_h_8bpc|warp_affine_8x8_8bpc' || echo "(no matching symbols — good if empty for U)"
echo "==== undefined count ===="
"$NM" src/libdav1d.a 2>/dev/null | grep -E ' U dav1d_obmc_masks| U dav1d_mc_warp_filter' || echo "no undefined obmc/warp refs"
