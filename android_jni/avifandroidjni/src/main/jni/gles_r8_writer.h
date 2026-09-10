// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_ANDROID_JNI_GLES_R8_WRITER_H_
#define AVIF_ANDROID_JNI_GLES_R8_WRITER_H_

#include <android/hardware_buffer.h>

#include <cstddef>
#include <cstdint>

namespace avif_jni {

// Uploads an 8-bit single-channel (Y) plane into an AHardwareBuffer allocated
// with AHARDWAREBUFFER_FORMAT_R8_UNORM by rendering through an offscreen
// OpenGL ES 3.0 context.
//
// Rationale: CPU writes (AHardwareBuffer_lock) into R8 buffers are not
// guaranteed to work on every gralloc implementation (several low-end
// drivers reject or mis-stride them), while R8 is a mandatory color-renderable
// format for GLES 3.0. Writing through the GPU therefore works wherever the
// buffer itself can be allocated with GPU_COLOR_OUTPUT usage.
//
// Contract:
// * `hw_buffer` must be a live AHardwareBuffer whose desc.format is
//   AHARDWAREBUFFER_FORMAT_R8_UNORM and whose usage contains
//   AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT (| GPU_SAMPLED_IMAGE).
// * `src_y` points at `height` rows of `width` bytes, `src_row_bytes` apart.
// * When `limited_range` is true, Y in [16, 235] is expanded to [0, 255]
//   (values outside are clamped), matching the CPU Gray565 path.
// * The call blocks (glFinish) until the GPU has written every pixel, so the
//   buffer can be handed to Bitmap.wrapHardwareBuffer immediately.
// * Thread-safe. The caller's current EGL context (if any) is preserved.
//
// Returns false on any EGL/GL failure. Callers must treat that as "try another
// output format" (Gray565 / RGBA), never as fatal.
bool WriteYPlaneToR8HardwareBuffer(AHardwareBuffer* hw_buffer,
                                   const uint8_t* src_y, size_t src_row_bytes,
                                   uint32_t width, uint32_t height,
                                   bool limited_range);

// Returns true when an offscreen GLES 3.0 context with the required EGL/GL
// extensions (EGL_ANDROID_image_native_buffer, EGL_ANDROID_get_native_client_buffer,
// GL_OES_EGL_image) could be created on this device. The result is cached
// after the first call. Cheap to call repeatedly.
bool GlesR8WriterIsAvailable();

}  // namespace avif_jni

#endif  // AVIF_ANDROID_JNI_GLES_R8_WRITER_H_
