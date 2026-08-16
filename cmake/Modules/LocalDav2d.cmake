# Pin to the VideoLAN dav2d 0.0.1 release.
set(AVIF_DAV2D_GIT_REPOSITORY "https://code.videolan.org/videolan/dav2d.git")
set(AVIF_DAV2D_GIT_TAG "0.0.1")

function(avif_build_local_dav2d)
    set(download_step_args)
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/dav2d")
        message(STATUS "libavif(AVIF_CODEC_DAV2D=LOCAL): ext/dav2d found, using as SOURCE_DIR")
        set(source_dir "${AVIF_SOURCE_DIR}/ext/dav2d")
    else()
        message(STATUS "libavif(AVIF_CODEC_DAV2D=LOCAL): ext/dav2d not found, fetching")
        set(source_dir "${FETCHCONTENT_BASE_DIR}/dav2d-src")
        list(APPEND download_step_args GIT_REPOSITORY ${AVIF_DAV2D_GIT_REPOSITORY} GIT_TAG ${AVIF_DAV2D_GIT_TAG} GIT_SHALLOW ON)
    endif()

    find_program(NINJA_EXECUTABLE NAMES ninja ninja-build REQUIRED)
    find_program(MESON_EXECUTABLE meson REQUIRED)

    set(PATH $ENV{PATH})
    if(WIN32)
        string(REPLACE ";" "\$<SEMICOLON>" PATH "${PATH}")
    endif()
    if(ANDROID_TOOLCHAIN_ROOT)
        set(PATH "${ANDROID_TOOLCHAIN_ROOT}/bin$<IF:$<BOOL:${WIN32}>,$<SEMICOLON>,:>${PATH}")
    endif()

    if(ANDROID)
        list(APPEND CMAKE_PROGRAM_PATH "${ANDROID_TOOLCHAIN_ROOT}/bin")

        if(NOT DEFINED ANDROID_PLATFORM_LEVEL AND ANDROID_NATIVE_API_LEVEL)
            set(ANDROID_PLATFORM_LEVEL ${ANDROID_NATIVE_API_LEVEL})
        endif()
        if(NOT ANDROID_PLATFORM_LEVEL OR ANDROID_PLATFORM_LEVEL LESS 21)
            set(ANDROID_PLATFORM_LEVEL 21)
        endif()

        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7-a")
            set(dav1d_android_cpu_family "arm")
            set(dav1d_android_cpu "arm")
            set(dav1d_android_clang_prefix "armv7a-linux-androideabi")
        elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
            set(dav1d_android_cpu_family "aarch64")
            set(dav1d_android_cpu "aarch64")
            set(dav1d_android_clang_prefix "aarch64-linux-android")
        elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            set(dav1d_android_cpu_family "x86_64")
            set(dav1d_android_cpu "x86_64")
            set(dav1d_android_clang_prefix "x86_64-linux-android")
        else()
            set(dav1d_android_cpu_family "x86")
            set(dav1d_android_cpu "i686")
            set(dav1d_android_clang_prefix "i686-linux-android")
        endif()

        set(dav1d_android_toolchain_bin "${ANDROID_TOOLCHAIN_ROOT}/bin")
        set(dav1d_android_c
            "${dav1d_android_toolchain_bin}/${dav1d_android_clang_prefix}${ANDROID_PLATFORM_LEVEL}-clang"
        )
        set(dav1d_android_cpp
            "${dav1d_android_toolchain_bin}/${dav1d_android_clang_prefix}${ANDROID_PLATFORM_LEVEL}-clang++"
        )
        set(dav1d_android_ar "${dav1d_android_toolchain_bin}/llvm-ar")
        set(dav1d_android_strip "${dav1d_android_toolchain_bin}/llvm-strip")

        set(CROSS_FILE "${PROJECT_BINARY_DIR}/crossfile-android-dav2d-${ANDROID_ABI}.meson")
        configure_file("${AVIF_SOURCE_DIR}/cmake/Meson/crossfile-android.meson.in" "${CROSS_FILE}" @ONLY)
    elseif(APPLE)
        if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
            string(TOLOWER "${CMAKE_SYSTEM_NAME}" cross_system_name)
            if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
                set(cross_system_endian "big")
            else()
                set(cross_system_endian "little")
            endif()
            if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
                set(cross_system_processor "aarch64")
            else()
                set(cross_system_processor "${CMAKE_SYSTEM_PROCESSOR}")
            endif()
            if(CMAKE_OSX_DEPLOYMENT_TARGET)
                set(cross_osx_deployment_target "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
            endif()

            set(CROSS_FILE "${PROJECT_BINARY_DIR}/crossfile-apple-dav2d.meson")
            configure_file("cmake/Meson/crossfile-apple.meson.in" "${CROSS_FILE}")
        endif()
    endif()

    if(CROSS_FILE)
        set(EXTRA_ARGS "--cross-file=${CROSS_FILE}")
    endif()

    set(build_dir "${FETCHCONTENT_BASE_DIR}/dav2d-build")
    set(install_dir "${FETCHCONTENT_BASE_DIR}/dav2d-install")

    if(ANDROID_ABI)
        set(build_dir "${build_dir}/${ANDROID_ABI}")
        set(install_dir "${install_dir}/${ANDROID_ABI}")
    endif()
    file(MAKE_DIRECTORY ${install_dir}/include)

    if(ANDROID)
        set(DAV2D_BITDEPTHS_ARG -Dbitdepths=8)
        set(DAV2D_LTO_ARG -Db_lto=true)
    endif()

    # dav2d x86 .asm uses %isidn(), a NASM 2.16+ preprocessor function.
    # Ubuntu 22.04 / WSL typically have 2.15.05, which errors with
    # "symbol `%isidn' not defined". ARM asm uses GAS and is unaffected.
    set(DAV2D_ASM_ARG -Denable_asm=true)
    set(_dav2d_target_x86 FALSE)
    if(ANDROID
       AND (ANDROID_ABI STREQUAL "x86"
            OR ANDROID_ABI STREQUAL "x86_64"
            OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|i686|x86)$")
    )
        set(_dav2d_target_x86 TRUE)
    elseif(
        NOT ANDROID
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x86|i[3-6]86)$"
    )
        set(_dav2d_target_x86 TRUE)
    endif()
    if(_dav2d_target_x86)
        find_program(_DAV2D_NASM nasm)
        set(_dav2d_nasm_ok FALSE)
        if(_DAV2D_NASM)
            execute_process(
                COMMAND "${_DAV2D_NASM}" -v
                OUTPUT_VARIABLE _DAV2D_NASM_V
                ERROR_VARIABLE _DAV2D_NASM_V
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(_DAV2D_NASM_V MATCHES "NASM version ([0-9]+)\\.([0-9]+)")
                set(_dav2d_nasm_major "${CMAKE_MATCH_1}")
                set(_dav2d_nasm_minor "${CMAKE_MATCH_2}")
                if(_dav2d_nasm_major GREATER 2
                   OR (_dav2d_nasm_major EQUAL 2 AND _dav2d_nasm_minor GREATER_EQUAL 16)
                )
                    set(_dav2d_nasm_ok TRUE)
                endif()
            endif()
        endif()
        if(NOT _dav2d_nasm_ok)
            set(DAV2D_ASM_ARG -Denable_asm=false)
            message(
                STATUS
                    "libavif(AVIF_CODEC_DAV2D=LOCAL): NASM 2.16+ required for dav2d x86 asm (%isidn); building without asm"
            )
        endif()
    endif()

    ExternalProject_Add(
        dav2d
        ${download_step_args}
        DOWNLOAD_DIR "${source_dir}"
        LOG_DIR "${build_dir}"
        STAMP_DIR "${build_dir}"
        TMP_DIR "${build_dir}"
        SOURCE_DIR "${source_dir}"
        BINARY_DIR "${build_dir}"
        INSTALL_DIR "${install_dir}"
        LIST_SEPARATOR |
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND
            ${CMAKE_COMMAND} -E env "PATH=${PATH}" ${MESON_EXECUTABLE} setup --buildtype=release --default-library=static
            --prefix=<INSTALL_DIR> --libdir=lib ${DAV2D_ASM_ARG} -Denable_tools=false -Denable_examples=false
            -Denable_tests=false -Denable_docs=false ${DAV2D_BITDEPTHS_ARG} ${DAV2D_LTO_ARG} ${EXTRA_ARGS} <SOURCE_DIR>
        BUILD_COMMAND ${CMAKE_COMMAND} -E env "PATH=${PATH}" ${NINJA_EXECUTABLE} -C <BINARY_DIR>
        INSTALL_COMMAND ${CMAKE_COMMAND} -E env "PATH=${PATH}" ${NINJA_EXECUTABLE} -C <BINARY_DIR> install
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libdav2d.a
    )

    add_library(dav2d::dav2d STATIC IMPORTED)
    set_target_properties(dav2d::dav2d PROPERTIES IMPORTED_LOCATION ${install_dir}/lib/libdav2d.a AVIF_LOCAL ON)
    target_include_directories(dav2d::dav2d INTERFACE "${install_dir}/include")
    target_link_directories(dav2d::dav2d INTERFACE ${install_dir}/lib)
    add_dependencies(dav2d::dav2d dav2d)
endfunction()

set(AVIF_DAV2D_BUILD_DIR "${AVIF_SOURCE_DIR}/ext/dav2d/build")
if(DEFINED ANDROID_ABI)
    set(AVIF_DAV2D_BUILD_DIR "${AVIF_DAV2D_BUILD_DIR}/${ANDROID_ABI}")
endif()
set(LIB_FILENAME "${AVIF_DAV2D_BUILD_DIR}/src/libdav2d${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(NOT EXISTS "${LIB_FILENAME}" AND NOT "${CMAKE_STATIC_LIBRARY_SUFFIX}" STREQUAL ".a")
    set(LIB_FILENAME "${AVIF_DAV2D_BUILD_DIR}/src/libdav2d.a")
endif()
if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_CODEC_DAV2D=LOCAL): compiled library found at ${LIB_FILENAME}")
    add_library(dav2d::dav2d STATIC IMPORTED)
    set_target_properties(dav2d::dav2d PROPERTIES IMPORTED_LOCATION ${LIB_FILENAME} AVIF_LOCAL ON)
    target_include_directories(
        dav2d::dav2d INTERFACE "${AVIF_DAV2D_BUILD_DIR}" "${AVIF_DAV2D_BUILD_DIR}/include"
                               "${AVIF_DAV2D_BUILD_DIR}/include/dav2d" "${AVIF_SOURCE_DIR}/ext/dav2d/include"
    )
else()
    message(STATUS "libavif(AVIF_CODEC_DAV2D=LOCAL): compiled library not found at ${LIB_FILENAME}; using ExternalProject")

    avif_build_local_dav2d()
endif()

if(EXISTS "${AVIF_SOURCE_DIR}/ext/dav2d")
    set_target_properties(dav2d::dav2d PROPERTIES FOLDER "ext/dav2d")
endif()
