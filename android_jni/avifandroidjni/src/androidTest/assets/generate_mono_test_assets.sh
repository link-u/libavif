#!/usr/bin/env bash
# Generate Gray565 instrumentation-test AVIF assets.
#
# Preferred path: compile gen_mono_ramp.c against a libavif build with an AV1
# encoder (aom). Fallback: avifenc if present on PATH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${SCRIPT_DIR}/avif"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../../.." && pwd)"
mkdir -p "${OUT_DIR}"

generate_with_helper() {
  local build_dir="${REPO_ROOT}/build.avifenc"
  local helper="${build_dir}/gen_mono_ramp"
  if [[ ! -x "${helper}" ]]; then
    if [[ ! -f "${build_dir}/libavif_internal.a" ]]; then
      echo "Missing ${build_dir}/libavif_internal.a — build libavif with AVIF_CODEC_AOM=LOCAL first." >&2
      return 1
    fi
    cc -O2 -I "${REPO_ROOT}/include" \
      "${SCRIPT_DIR}/gen_mono_ramp.c" \
      -o "${helper}" \
      "${build_dir}/libavif_internal.a" \
      "${REPO_ROOT}/ext/aom/build.libavif/libaom.a" \
      -lpthread -lm -ldl
  fi
  "${helper}" full "${OUT_DIR}/mono_8bpc_full.avif"
  "${helper}" limited "${OUT_DIR}/mono_8bpc_limited.avif"
  cp -f "${OUT_DIR}/mono_8bpc_full.avif" "${OUT_DIR}/mono_8bpc_ramp_full.avif"
}

generate_with_avifenc() {
  local workdir
  workdir="$(mktemp -d)"
  trap 'rm -rf "${workdir}"' RETURN
  python3 - "${workdir}" <<'PY'
import pathlib, sys
out = pathlib.Path(sys.argv[1]) / "ramp.y"
data = bytearray()
for _ in range(16):
    data.extend(range(256))
out.write_bytes(data)
PY
  if command -v convert >/dev/null 2>&1; then
    convert -size 256x16 -depth 8 gray:"${workdir}/ramp.y" "${workdir}/ramp.png"
  else
    ffmpeg -y -f rawvideo -pix_fmt gray -s 256x16 -i "${workdir}/ramp.y" \
      "${workdir}/ramp.png" >/dev/null 2>&1
  fi
  avifenc --yuv 400 --depth 8 --range limited --min 0 --max 0 --speed 0 \
    "${workdir}/ramp.png" "${OUT_DIR}/mono_8bpc_limited.avif"
  avifenc --yuv 400 --depth 8 --range full --min 0 --max 0 --speed 0 \
    "${workdir}/ramp.png" "${OUT_DIR}/mono_8bpc_full.avif"
  cp -f "${OUT_DIR}/mono_8bpc_full.avif" "${OUT_DIR}/mono_8bpc_ramp_full.avif"
}

if generate_with_helper; then
  :
elif command -v avifenc >/dev/null 2>&1; then
  echo "Using avifenc fallback." >&2
  generate_with_avifenc
else
  echo "Need gen_mono_ramp (libavif+aom) or avifenc." >&2
  exit 1
fi

echo "Wrote:"
ls -la "${OUT_DIR}/mono_8bpc_limited.avif" \
  "${OUT_DIR}/mono_8bpc_full.avif" \
  "${OUT_DIR}/mono_8bpc_ramp_full.avif"
