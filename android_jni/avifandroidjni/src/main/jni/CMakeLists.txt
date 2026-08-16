# Copyright 2022 Google LLC
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.22)

project(avif_android_jni)

# Setting the same option as libavif here ensures the AVIF_ENABLE_WERROR
# variable is set in the CACHE for add_subdirectory to consume below.
option(AVIF_ENABLE_WERROR "Treat all compiler warnings as errors" OFF)

# Perform a static build of libavif and link it with the jni shared library so
# that we end up with a single libavif_android.so file in the end.
#
# By default, we build libavif with libgav1 as the AV1 decoder. To change that
# behavior, set up the android builds for the desired decoder and change the
# variables below accordingly.

# To override the variables in the libavif subproject:
# https://stackoverflow.com/questions/3766740/overriding-a-default-option-value-in-cmake-from-a-parent-cmakelists-txt
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# To disable dav1d, change the following two variables to OFF.
set(AVIF_CODEC_DAV1D "LOCAL" CACHE STRING "" FORCE)

# AV2 decode via dav2d. Keep AVM off (experimental, heavy).
set(AVIF_CODEC_DAV2D "LOCAL" CACHE STRING "" FORCE)

# To disable libyuv, change this variable to OFF.
set(AVIF_LIBYUV "LOCAL" CACHE STRING "" FORCE)

# To enable libgav1, change the following two variables to ON.
set(AVIF_CODEC_LIBGAV1 OFF CACHE BOOL "" FORCE)

# Android JNI is decode-only: drop write.c / encoder implementation.
set(AVIF_DECODE_ONLY ON CACHE BOOL "" FORCE)
set(AVIF_CODEC_AOM OFF CACHE STRING "" FORCE)
set(AVIF_CODEC_RAV1E OFF CACHE STRING "" FORCE)
set(AVIF_CODEC_SVT OFF CACHE STRING "" FORCE)
set(AVIF_CODEC_AVM OFF CACHE STRING "" FORCE)
set(AVIF_LIBSHARPYUV OFF CACHE STRING "" FORCE)
set(AVIF_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(AVIF_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Enable LTO for non-Debug builds so libavif, libyuv (FetchContent), and the
# JNI shared library are compiled/linked with IPO. dav1d/dav2d must also be
# built with -Db_lto=true (see ext/dav1d_android.sh, ext/dav2d_android.sh,
# LocalDav1d.cmake, LocalDav2d.cmake) for cross-module optimization.
option(AVIF_ANDROID_ENABLE_LTO "Enable LTO/IPO for the Android JNI native build" ON)
if(AVIF_ANDROID_ENABLE_LTO AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT avif_android_ipo_supported OUTPUT avif_android_ipo_error)
    if(avif_android_ipo_supported)
        # CMake may inject -fuse-ld=gold for Clang IPO; NDK r22+ removed gold.
        foreach(_lang C CXX)
            if(DEFINED CMAKE_${_lang}_LINK_OPTIONS_IPO)
                string(REPLACE "-fuse-ld=gold" "" _avif_ipo_link "${CMAKE_${_lang}_LINK_OPTIONS_IPO}")
                set(CMAKE_${_lang}_LINK_OPTIONS_IPO "${_avif_ipo_link}")
            endif()
        endforeach()
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "avif_android_jni: enabling LTO/IPO")
    else()
        message(WARNING "avif_android_jni: LTO/IPO requested but not supported: ${avif_android_ipo_error}")
    endif()
endif()

# The current CMake file lives in:
# $LIBAVIF_ROOT/android_jni/avifandroidjni/src/main/jni.
# In order to build libavif, we need to go up 5 directories. If a different
# libavif checkout is to be used, this path has to be updated accordingly.
add_subdirectory(../../../../.. build)

add_library("avif_android" SHARED "libavif_jni.cc")

target_link_options(avif_android PRIVATE "-Wl,-z,max-page-size=16384")
target_link_libraries(avif_android jnigraphics avif log)
