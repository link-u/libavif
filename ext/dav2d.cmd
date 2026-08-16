: # If you want to use a local build of dav2d, you must clone the dav2d repo in this directory first, then set CMake's AVIF_CODEC_DAV2D to LOCAL.
: # Feel free to pin a more specific commit or tag when a dav2d release exists.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # meson and ninja must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

: # When updating the dav2d version, make the same change to dav2d_android.sh and cmake/Modules/LocalDav2d.cmake.
: # x86 asm needs NASM 2.16+ (%isidn). With older NASM, pass -Denable_asm=false.
git clone --depth 1 --branch 0.0.1 https://code.videolan.org/videolan/dav2d.git

: # macOS might require: -Dc_args=-fno-stack-check
meson setup --default-library=static --buildtype release -Denable_tools=false -Denable_tests=false -Denable_examples=false -Denable_docs=false dav2d/build dav2d
meson compile -C dav2d/build
