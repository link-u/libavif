// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <android/api-level.h>
#include <android/bitmap.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <cpu-features.h>
#include <dlfcn.h>
#include <jni.h>
#include <sys/system_properties.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>

#include "avif/avif.h"

#ifndef AHARDWAREBUFFER_FORMAT_R8_UNORM
#define AHARDWAREBUFFER_FORMAT_R8_UNORM 0x38  // = 56, API 35
#endif

#define LOG_TAG "avif_jni"
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

#define FUNC(RETURN_TYPE, NAME, ...)                                      \
  extern "C" {                                                            \
  JNIEXPORT RETURN_TYPE Java_org_aomedia_avif_android_AvifDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__);                          \
  }                                                                       \
  JNIEXPORT RETURN_TYPE Java_org_aomedia_avif_android_AvifDecoder_##NAME( \
      JNIEnv* env, jobject thiz, ##__VA_ARGS__)

#define HW_FUNC(RETURN_TYPE, NAME, ...)                                           \
  extern "C" {                                                                    \
  JNIEXPORT RETURN_TYPE                                                           \
  Java_org_aomedia_avif_android_AvifHardwareDecoder_##NAME(JNIEnv* env, jclass clazz, \
                                                           ##__VA_ARGS__);        \
  }                                                                               \
  JNIEXPORT RETURN_TYPE                                                           \
  Java_org_aomedia_avif_android_AvifHardwareDecoder_##NAME(JNIEnv* env, jclass clazz, \
                                                           ##__VA_ARGS__)

#define IGNORE_UNUSED_JNI_PARAMETERS \
  (void) env; \
  (void) thiz

#define IGNORE_UNUSED_HW_JNI_PARAMETERS \
  (void)env;                            \
  (void)clazz

namespace {

using AHardwareBuffer_allocate_fn = int (*)(const AHardwareBuffer_Desc*, AHardwareBuffer**);
using AHardwareBuffer_describe_fn = void (*)(const AHardwareBuffer*, AHardwareBuffer_Desc*);
using AHardwareBuffer_isSupported_fn = bool (*)(const AHardwareBuffer_Desc*);
using AHardwareBuffer_lock_fn = int (*)(AHardwareBuffer*, uint64_t, int32_t, const ARect*,
                                        void**);
using AHardwareBuffer_unlock_fn = int (*)(AHardwareBuffer*, int32_t*);
using AHardwareBuffer_release_fn = void (*)(AHardwareBuffer*);
using AHardwareBuffer_toHardwareBuffer_fn = jobject (*)(JNIEnv*, AHardwareBuffer*);

struct HardwareBufferApi {
  void* buffer_library = nullptr;
  void* android_library = nullptr;
  AHardwareBuffer_allocate_fn allocate = nullptr;
  AHardwareBuffer_describe_fn describe = nullptr;
  AHardwareBuffer_isSupported_fn is_supported = nullptr;
  AHardwareBuffer_lock_fn lock = nullptr;
  AHardwareBuffer_unlock_fn unlock = nullptr;
  AHardwareBuffer_release_fn release = nullptr;
  AHardwareBuffer_toHardwareBuffer_fn to_hardware_buffer = nullptr;
  bool available = false;
};

void CloseHardwareBufferLibraries(HardwareBufferApi* api) {
  if (api->buffer_library != nullptr) {
    dlclose(api->buffer_library);
    api->buffer_library = nullptr;
  }
  if (api->android_library != nullptr) {
    dlclose(api->android_library);
    api->android_library = nullptr;
  }
}

void* LoadSymbolFromLibrary(void* library, const char* symbol) {
  if (library == nullptr) {
    return nullptr;
  }
  dlerror();
  void* symbol_address = dlsym(library, symbol);
  if (symbol_address == nullptr) {
    LOGE("Failed to dlsym %s: %s.", symbol, dlerror());
  }
  return symbol_address;
}

int GetDeviceApiLevel() {
  if (android_get_device_api_level != nullptr) {
    const int api_level = android_get_device_api_level();
    if (api_level > 0) {
      return api_level;
    }
  }

  char sdk_version[PROP_VALUE_MAX] = {};
  if (__system_property_get("ro.build.version.sdk", sdk_version) <= 0) {
    return 0;
  }
  return atoi(sdk_version);
}

void LoadHardwareBufferApi(HardwareBufferApi* api) {
  if (GetDeviceApiLevel() < 29) {
    LOGE("HardwareBuffer decode requires API 29+.");
    return;
  }

  // Core AHardwareBuffer symbols are exported from libnativewindow.so and/or
  // libandroid.so depending on the device. The JNI conversion helper
  // AHardwareBuffer_toHardwareBuffer is exported from libandroid.so.
  api->buffer_library = dlopen("libnativewindow.so", RTLD_NOW);
  api->android_library = dlopen("libandroid.so", RTLD_NOW);
  if (api->android_library == nullptr) {
    LOGE("Failed to dlopen libandroid.so: %s.", dlerror());
    CloseHardwareBufferLibraries(api);
    return;
  }

  auto load_buffer_symbol = [&](const char* symbol) -> void* {
    void* symbol_address = LoadSymbolFromLibrary(api->buffer_library, symbol);
    if (symbol_address == nullptr) {
      symbol_address = LoadSymbolFromLibrary(api->android_library, symbol);
    }
    return symbol_address;
  };

  api->allocate = reinterpret_cast<AHardwareBuffer_allocate_fn>(
      load_buffer_symbol("AHardwareBuffer_allocate"));
  api->describe = reinterpret_cast<AHardwareBuffer_describe_fn>(
      load_buffer_symbol("AHardwareBuffer_describe"));
  api->is_supported = reinterpret_cast<AHardwareBuffer_isSupported_fn>(
      load_buffer_symbol("AHardwareBuffer_isSupported"));
  api->lock = reinterpret_cast<AHardwareBuffer_lock_fn>(
      load_buffer_symbol("AHardwareBuffer_lock"));
  api->unlock = reinterpret_cast<AHardwareBuffer_unlock_fn>(
      load_buffer_symbol("AHardwareBuffer_unlock"));
  api->release = reinterpret_cast<AHardwareBuffer_release_fn>(
      load_buffer_symbol("AHardwareBuffer_release"));
  api->to_hardware_buffer =
      reinterpret_cast<AHardwareBuffer_toHardwareBuffer_fn>(LoadSymbolFromLibrary(
          api->android_library, "AHardwareBuffer_toHardwareBuffer"));

  if (api->allocate == nullptr || api->describe == nullptr ||
      api->lock == nullptr || api->unlock == nullptr ||
      api->release == nullptr || api->to_hardware_buffer == nullptr) {
    CloseHardwareBufferLibraries(api);
    return;
  }

  api->available = true;
}

const HardwareBufferApi& GetHardwareBufferApi() {
  static std::once_flag init_flag;
  static HardwareBufferApi api;
  std::call_once(init_flag, []() { LoadHardwareBufferApi(&api); });
  return api;
}

int getThreadCount(int threads);
bool JniExceptionCheck(JNIEnv* env);

// RAII wrapper class that properly frees the decoder related objects on
// destruction.
struct AvifDecoderWrapper {
 public:
  AvifDecoderWrapper() = default;
  // Not copyable or movable.
  AvifDecoderWrapper(const AvifDecoderWrapper&) = delete;
  AvifDecoderWrapper& operator=(const AvifDecoderWrapper&) = delete;

  ~AvifDecoderWrapper() {
    if (decoder != nullptr) {
      avifDecoderDestroy(decoder);
    }
  }

  avifDecoder* decoder = nullptr;
  avifCropRect crop;
};

// Returns true when `encoded` is a direct ByteBuffer of at least `length`
// bytes and `length` is non-negative. On success, `*out_buffer` is set to the
// direct buffer address and `*out_size` is set to `(size_t)length`. On
// failure, logs via LOGE() and returns false; callers should propagate a
// clean failure (false / 0 / nullptr) to the Java layer.
bool ValidateDirectBuffer(JNIEnv* env, jobject encoded, jint length,
                          const uint8_t** out_buffer, size_t* out_size) {
  if (length < 0) {
    LOGE("AVIF JNI: negative length (%d) rejected.", length);
    return false;
  }
  const jlong capacity = env->GetDirectBufferCapacity(encoded);
  if (capacity < 0) {
    LOGE("AVIF JNI: encoded is not a direct ByteBuffer.");
    return false;
  }
  if (static_cast<jlong>(length) > capacity) {
    LOGE("AVIF JNI: length (%d) exceeds direct buffer capacity (%lld).",
         length, static_cast<long long>(capacity));
    return false;
  }
  const void* const address = env->GetDirectBufferAddress(encoded);
  if (address == nullptr) {
    LOGE("AVIF JNI: GetDirectBufferAddress returned null.");
    return false;
  }
  *out_buffer = static_cast<const uint8_t*>(address);
  *out_size = static_cast<size_t>(length);
  return true;
}

bool CreateDecoderAndParse(AvifDecoderWrapper* const decoder,
                           const uint8_t* const buffer, size_t length,
                           int threads) {
  decoder->decoder = avifDecoderCreate();
  if (decoder->decoder == nullptr) {
    LOGE("Failed to create AVIF Decoder.");
    return false;
  }
  decoder->decoder->maxThreads = threads;
  decoder->decoder->ignoreXMP = AVIF_TRUE;
  decoder->decoder->ignoreExif = AVIF_TRUE;

  // Turn off libavif's 'clap' (clean aperture) property validation. This allows
  // us to detect and ignore streams that have an invalid 'clap' property
  // instead failing.
  decoder->decoder->strictFlags &= ~AVIF_STRICT_CLAP_VALID;
  // Allow 'pixi' (pixel information) property to be missing. Older versions of
  // libheif did not add the 'pixi' item property to AV1 image items (See
  // crbug.com/1198455).
  decoder->decoder->strictFlags &= ~AVIF_STRICT_PIXI_REQUIRED;

  avifResult res = avifDecoderSetIOMemory(decoder->decoder, buffer, length);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to set AVIF IO to a memory reader.");
    return false;
  }
  res = avifDecoderParse(decoder->decoder);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to parse AVIF image: %s.", avifResultToString(res));
    return false;
  }

  avifDiagnostics diag;
  // If the image does not have a valid 'clap' property, then we simply display
  // the whole image.
  // TODO(vigneshv): Handle the case of avifCropRectRequiresUpsampling()
  //                 returning true.
  if (!(decoder->decoder->image->transformFlags & AVIF_TRANSFORM_CLAP) ||
      !avifCropRectFromCleanApertureBox(
          &decoder->crop, &decoder->decoder->image->clap,
          decoder->decoder->image->width, decoder->decoder->image->height,
          &diag) ||
      avifCropRectRequiresUpsampling(&decoder->crop,
                                     decoder->decoder->image->yuvFormat)) {
    decoder->crop.width = decoder->decoder->image->width;
    decoder->crop.height = decoder->decoder->image->height;
    decoder->crop.x = 0;
    decoder->crop.y = 0;
  }
  return true;
}

void GetTargetDimensions(AvifDecoderWrapper* const decoder, int target_width,
                         int target_height, uint32_t* dst_width,
                         uint32_t* dst_height) {
  if (target_width > 0 && target_height > 0) {
    *dst_width = static_cast<uint32_t>(target_width);
    *dst_height = static_cast<uint32_t>(target_height);
  } else {
    *dst_width = decoder->crop.width;
    *dst_height = decoder->crop.height;
  }
}

avifImage* PrepareImageForOutput(AvifDecoderWrapper* const decoder,
                                 uint32_t dst_width, uint32_t dst_height,
                                 avifResult* res,
                                 std::unique_ptr<avifImage, decltype(&avifImageDestroy)>&
                                     cropped_image,
                                 std::unique_ptr<avifImage, decltype(&avifImageDestroy)>&
                                     image_copy) {
  avifImage* image;
  if (decoder->decoder->image->width == decoder->crop.width &&
      decoder->decoder->image->height == decoder->crop.height &&
      decoder->crop.x == 0 && decoder->crop.y == 0) {
    image = decoder->decoder->image;
  } else {
    cropped_image.reset(avifImageCreateEmpty());
    if (cropped_image == nullptr) {
      LOGE("Failed to allocate cropped image.");
      *res = AVIF_RESULT_OUT_OF_MEMORY;
      return nullptr;
    }
    *res = avifImageSetViewRect(cropped_image.get(), decoder->decoder->image,
                                &decoder->crop);
    if (*res != AVIF_RESULT_OK) {
      LOGE("Failed to set crop rectangle. Status: %d", *res);
      return nullptr;
    }
    image = cropped_image.get();
  }
  if (image->width != dst_width || image->height != dst_height) {
    if (!image->imageOwnsYUVPlanes || !image->imageOwnsAlphaPlane) {
      image_copy.reset(avifImageCreateEmpty());
      if (image_copy == nullptr) {
        LOGE("Failed to allocate image for scaling.");
        *res = AVIF_RESULT_OUT_OF_MEMORY;
        return nullptr;
      }
      *res = avifImageCopy(image_copy.get(), image, AVIF_PLANES_ALL);
      if (*res != AVIF_RESULT_OK) {
        LOGE("Failed to make a copy of the image for scaling. Status: %d", *res);
        return nullptr;
      }
      image = image_copy.get();
    }
    avifDiagnostics diag;
    *res = avifImageScale(image, dst_width, dst_height, &diag);
    if (*res != AVIF_RESULT_OK) {
      LOGE("Failed to scale image. Status: %d", *res);
      return nullptr;
    }
  }
  *res = AVIF_RESULT_OK;
  return image;
}

avifResult AvifImageToRGBBufferFromImage(avifImage* image, void* pixels,
                                         size_t row_bytes) {
  avifRGBImage rgb_image;
  avifRGBImageSetDefaults(&rgb_image, image);
  rgb_image.format = AVIF_RGB_FORMAT_RGBA;
  rgb_image.depth = 8;
  rgb_image.pixels = static_cast<uint8_t*>(pixels);
  rgb_image.rowBytes = row_bytes;
  // Android always sees the Bitmaps as premultiplied with alpha when it renders
  // them:
  // https://developer.android.com/reference/android/graphics/Bitmap#setPremultiplied(boolean)
  rgb_image.alphaPremultiplied = AVIF_TRUE;
  const avifResult res = avifImageYUVToRGB(image, &rgb_image);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to convert YUV Pixels to RGB. Status: %d", res);
  }
  return res;
}

avifResult AvifImageToRGBBuffer(AvifDecoderWrapper* const decoder, void* pixels,
                                uint32_t dst_width, uint32_t dst_height,
                                size_t row_bytes, avifRGBFormat format,
                                int depth, avifBool is_float) {
  avifResult res;
  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> cropped_image(
      nullptr, avifImageDestroy);
  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> image_copy(
      nullptr, avifImageDestroy);
  avifImage* image = PrepareImageForOutput(decoder, dst_width, dst_height, &res,
                                           cropped_image, image_copy);
  if (image == nullptr) {
    return res;
  }

  if (format == AVIF_RGB_FORMAT_RGBA && depth == 8 && !is_float) {
    return AvifImageToRGBBufferFromImage(image, pixels, row_bytes);
  }

  avifRGBImage rgb_image;
  avifRGBImageSetDefaults(&rgb_image, image);
  rgb_image.format = format;
  rgb_image.depth = depth;
  rgb_image.isFloat = is_float;
  rgb_image.pixels = static_cast<uint8_t*>(pixels);
  rgb_image.rowBytes = row_bytes;
  rgb_image.alphaPremultiplied = AVIF_TRUE;
  res = avifImageYUVToRGB(image, &rgb_image);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to convert YUV Pixels to RGB. Status: %d", res);
  }
  return res;
}

avifResult AvifImageToGrayBuffer(const HardwareBufferApi& hw_api,
                                 AHardwareBuffer* hw_buffer,
                                 avifImage* image) {
  AHardwareBuffer_Desc desc;
  hw_api.describe(hw_buffer, &desc);

  void* pixels = nullptr;
  if (hw_api.lock(hw_buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY, -1, nullptr,
                  &pixels) != 0) {
    LOGE("AHardwareBuffer_lock failed.");
    return AVIF_RESULT_UNKNOWN_ERROR;
  }

  uint8_t* dst = static_cast<uint8_t*>(pixels);
  const uint8_t* src_y = image->yuvPlanes[AVIF_CHAN_Y];
  const uint32_t width = image->width;
  const uint32_t height = image->height;
  const size_t src_row_bytes = image->yuvRowBytes[AVIF_CHAN_Y];

  if (image->yuvRange == AVIF_RANGE_LIMITED) {
    uint8_t lut[256];
    for (int v = 0; v < 256; ++v) {
      const int e = ((v - 16) * 255 + 109) / 219;
      lut[v] = static_cast<uint8_t>(std::clamp(e, 0, 255));
    }
    for (uint32_t y = 0; y < height; ++y) {
      const uint8_t* src_row = src_y + y * src_row_bytes;
      uint8_t* dst_row = dst + y * desc.stride;
      for (uint32_t x = 0; x < width; ++x) {
        dst_row[x] = lut[src_row[x]];
      }
    }
  } else {
    for (uint32_t y = 0; y < height; ++y) {
      std::memcpy(dst + y * desc.stride, src_y + y * src_row_bytes, width);
    }
  }

  hw_api.unlock(hw_buffer, nullptr);
  return AVIF_RESULT_OK;
}

bool ShouldUseR8Output(avifImage* image, bool allow_r8,
                       const HardwareBufferApi& hw_api,
                       AHardwareBuffer_Desc* desc) {
  if (!allow_r8) {
    return false;
  }
  if (image->yuvFormat != AVIF_PIXEL_FORMAT_YUV400) {
    return false;
  }
  if (image->alphaPlane != nullptr) {
    return false;
  }
  if (image->depth != 8) {
    return false;
  }
  if (GetDeviceApiLevel() < 35) {
    return false;
  }

  desc->format = AHARDWAREBUFFER_FORMAT_R8_UNORM;
  if (hw_api.is_supported != nullptr && !hw_api.is_supported(desc)) {
    return false;
  }
  return true;
}

avifResult AvifImageToBitmap(JNIEnv* const env,
                             AvifDecoderWrapper* const decoder,
                             jobject bitmap) {
  AndroidBitmapInfo bitmap_info;
  if (AndroidBitmap_getInfo(env, bitmap, &bitmap_info) < 0) {
    LOGE("AndroidBitmap_getInfo failed.");
    return AVIF_RESULT_UNKNOWN_ERROR;
  }
  // Ensure that the bitmap format is RGBA_8888, RGB_565 or RGBA_F16.
  if (bitmap_info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 &&
      bitmap_info.format != ANDROID_BITMAP_FORMAT_RGB_565 &&
      bitmap_info.format != ANDROID_BITMAP_FORMAT_RGBA_F16) {
    LOGE("Bitmap format (%d) is not supported.", bitmap_info.format);
    return AVIF_RESULT_NOT_IMPLEMENTED;
  }
  void* bitmap_pixels = nullptr;
  if (AndroidBitmap_lockPixels(env, bitmap, &bitmap_pixels) !=
      ANDROID_BITMAP_RESULT_SUCCESS) {
    LOGE("Failed to lock Bitmap.");
    return AVIF_RESULT_UNKNOWN_ERROR;
  }

  avifRGBFormat format = AVIF_RGB_FORMAT_RGBA;
  int depth = 8;
  avifBool is_float = AVIF_FALSE;
  if (bitmap_info.format == ANDROID_BITMAP_FORMAT_RGBA_F16) {
    depth = 16;
    is_float = AVIF_TRUE;
  } else if (bitmap_info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
    format = AVIF_RGB_FORMAT_RGB_565;
  }

  const avifResult res = AvifImageToRGBBuffer(
      decoder, bitmap_pixels, bitmap_info.width, bitmap_info.height,
      bitmap_info.stride, format, depth, is_float);
  AndroidBitmap_unlockPixels(env, bitmap);
  return res;
}

jobject AvifImageToJavaHardwareBuffer(JNIEnv* const env,
                                      AvifDecoderWrapper* const decoder,
                                      uint32_t dst_width, uint32_t dst_height,
                                      bool allow_r8) {
  const HardwareBufferApi& hw_api = GetHardwareBufferApi();
  if (!hw_api.available) {
    LOGE("HardwareBuffer API is unavailable. Check earlier avif_jni logs for "
         "dlopen/dlsym errors.");
    return nullptr;
  }

  avifResult res;
  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> cropped_image(
      nullptr, avifImageDestroy);
  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> image_copy(
      nullptr, avifImageDestroy);
  avifImage* image = PrepareImageForOutput(decoder, dst_width, dst_height, &res,
                                           cropped_image, image_copy);
  if (image == nullptr) {
    return nullptr;
  }

  AHardwareBuffer_Desc desc = {};
  desc.width = dst_width;
  desc.height = dst_height;
  desc.layers = 1;
  desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

  bool use_r8 = ShouldUseR8Output(image, allow_r8, hw_api, &desc);

  AHardwareBuffer* hw_buffer = nullptr;
  if (use_r8) {
    desc.format = AHARDWAREBUFFER_FORMAT_R8_UNORM;
    if (hw_api.allocate(&desc, &hw_buffer) != 0) {
      LOGE("AHardwareBuffer_allocate failed for R8; falling back to RGBA.");
      use_r8 = false;
      hw_buffer = nullptr;
    }
  }
  if (!use_r8) {
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    if (hw_api.allocate(&desc, &hw_buffer) != 0) {
      LOGE("AHardwareBuffer_allocate failed.");
      return nullptr;
    }
  }
  hw_api.describe(hw_buffer, &desc);

  avifResult fill_res;
  if (use_r8) {
    fill_res = AvifImageToGrayBuffer(hw_api, hw_buffer, image);
  } else {
    void* pixels = nullptr;
    if (hw_api.lock(hw_buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY, -1, nullptr,
                    &pixels) != 0) {
      LOGE("AHardwareBuffer_lock failed.");
      hw_api.release(hw_buffer);
      return nullptr;
    }
    fill_res = AvifImageToRGBBufferFromImage(
        image, pixels, static_cast<size_t>(desc.stride) * 4);
    hw_api.unlock(hw_buffer, nullptr);
  }
  if (fill_res != AVIF_RESULT_OK) {
    hw_api.release(hw_buffer);
    return nullptr;
  }

  jobject java_buffer = hw_api.to_hardware_buffer(env, hw_buffer);
  hw_api.release(hw_buffer);
  if (java_buffer == nullptr) {
    LOGE("AHardwareBuffer_toHardwareBuffer failed.");
    if (JniExceptionCheck(env)) {
      return nullptr;
    }
  }
  return java_buffer;
}

jobject DecodeToHardwareBuffer(JNIEnv* const env, jobject encoded, int length,
                               int target_width, int target_height,
                               int threads, bool allow_r8) {
  const uint8_t* buffer = nullptr;
  size_t size = 0;
  if (!ValidateDirectBuffer(env, encoded, length, &buffer, &size)) {
    return nullptr;
  }
  AvifDecoderWrapper decoder;
  if (!CreateDecoderAndParse(&decoder, buffer, size,
                             getThreadCount(threads))) {
    return nullptr;
  }
  uint32_t dst_width = 0;
  uint32_t dst_height = 0;
  GetTargetDimensions(&decoder, target_width, target_height, &dst_width,
                      &dst_height);
  if (avifDecoderNextImage(decoder.decoder) != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image for HardwareBuffer output.");
    return nullptr;
  }
  return AvifImageToJavaHardwareBuffer(env, &decoder, dst_width, dst_height,
                                       allow_r8);
}

jobject NextFrameToHardwareBuffer(JNIEnv* const env,
                                  AvifDecoderWrapper* const decoder,
                                  int target_width, int target_height,
                                  bool allow_r8) {
  const avifResult decode_result = avifDecoderNextImage(decoder->decoder);
  if (decode_result != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", decode_result);
    return nullptr;
  }
  uint32_t dst_width = 0;
  uint32_t dst_height = 0;
  GetTargetDimensions(decoder, target_width, target_height, &dst_width,
                      &dst_height);
  return AvifImageToJavaHardwareBuffer(env, decoder, dst_width, dst_height,
                                       allow_r8);
}

jobject NthFrameToHardwareBuffer(JNIEnv* const env,
                                 AvifDecoderWrapper* const decoder, uint32_t n,
                                 int target_width, int target_height,
                                 bool allow_r8) {
  const avifResult decode_result = avifDecoderNthImage(decoder->decoder, n);
  if (decode_result != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", decode_result);
    return nullptr;
  }
  uint32_t dst_width = 0;
  uint32_t dst_height = 0;
  GetTargetDimensions(decoder, target_width, target_height, &dst_width,
                      &dst_height);
  return AvifImageToJavaHardwareBuffer(env, decoder, dst_width, dst_height,
                                       allow_r8);
}

avifResult DecodeNextImage(JNIEnv* const env, AvifDecoderWrapper* const decoder,
                           jobject bitmap) {
  avifResult res = avifDecoderNextImage(decoder->decoder);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", res);
    return res;
  }
  return AvifImageToBitmap(env, decoder, bitmap);
}

avifResult DecodeNthImage(JNIEnv* const env, AvifDecoderWrapper* const decoder,
                          uint32_t n, jobject bitmap) {
  avifResult res = avifDecoderNthImage(decoder->decoder, n);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to decode AVIF image. Status: %d", res);
    return res;
  }
  return AvifImageToBitmap(env, decoder, bitmap);
}

int getThreadCount(int threads) {
  if (threads < 0) {
    return android_getCpuCount();
  }
  if (threads == 0) {
    // Empirically, on Android devices with more than 1 core, decoding with 2
    // threads is almost always better than using as many threads as CPU cores.
    return std::min(android_getCpuCount(), 2);
  }
  return threads;
}

// Checks if there is a pending JNI exception that will be thrown when the
// control returns to the java layer. If there is none, it will return false. If
// there is one, then it will clear the pending exception and return true.
// Whenever this function returns true, the caller should treat it as a fatal
// error and return with a failure status as early as possible.
bool JniExceptionCheck(JNIEnv* env) {
  if (!env->ExceptionCheck()) {
    return false;
  }
  env->ExceptionClear();
  return true;
}

}  // namespace

jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return -1;
  }
  return JNI_VERSION_1_6;
}

FUNC(jboolean, isAvifImage, jobject encoded, int length) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  const uint8_t* buffer = nullptr;
  size_t size = 0;
  if (!ValidateDirectBuffer(env, encoded, length, &buffer, &size)) {
    return false;
  }
  const avifROData avif = {buffer, size};
  return avifPeekCompatibleFileType(&avif);
}

#define CHECK_EXCEPTION(ret)                \
  do {                                      \
    if (JniExceptionCheck(env)) return ret; \
  } while (false)

#define FIND_CLASS(var, class_name, ret)         \
  const jclass var = env->FindClass(class_name); \
  CHECK_EXCEPTION(ret);                          \
  if (var == nullptr) return ret

#define GET_FIELD_ID(var, class_name, field_name, signature, ret)          \
  const jfieldID var = env->GetFieldID(class_name, field_name, signature); \
  CHECK_EXCEPTION(ret);                                                    \
  if (var == nullptr) return ret

FUNC(jboolean, getInfo, jobject encoded, int length, jobject info) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  const uint8_t* buffer = nullptr;
  size_t size = 0;
  if (!ValidateDirectBuffer(env, encoded, length, &buffer, &size)) {
    return false;
  }
  AvifDecoderWrapper decoder;
  if (!CreateDecoderAndParse(&decoder, buffer, size, /*threads=*/1)) {
    return false;
  }
  FIND_CLASS(info_class, "org/aomedia/avif/android/AvifDecoder$Info", false);
  GET_FIELD_ID(width, info_class, "width", "I", false);
  GET_FIELD_ID(height, info_class, "height", "I", false);
  GET_FIELD_ID(depth, info_class, "depth", "I", false);
  GET_FIELD_ID(alpha_present, info_class, "alphaPresent", "Z", false);
  env->SetIntField(info, width, decoder.crop.width);
  CHECK_EXCEPTION(false);
  env->SetIntField(info, height, decoder.crop.height);
  CHECK_EXCEPTION(false);
  env->SetIntField(info, depth, decoder.decoder->image->depth);
  CHECK_EXCEPTION(false);
  env->SetBooleanField(info, alpha_present, decoder.decoder->alphaPresent);
  CHECK_EXCEPTION(false);
  return true;
}

FUNC(jboolean, decode, jobject encoded, int length, jobject bitmap,
     jint threads) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  const uint8_t* buffer = nullptr;
  size_t size = 0;
  if (!ValidateDirectBuffer(env, encoded, length, &buffer, &size)) {
    return false;
  }
  AvifDecoderWrapper decoder;
  if (!CreateDecoderAndParse(&decoder, buffer, size,
                             getThreadCount(threads))) {
    return false;
  }
  return DecodeNextImage(env, &decoder, bitmap) == AVIF_RESULT_OK;
}

FUNC(jlong, createDecoder, jobject encoded, jint length, jint threads) {
  const uint8_t* buffer = nullptr;
  size_t size = 0;
  if (!ValidateDirectBuffer(env, encoded, length, &buffer, &size)) {
    return 0;
  }
  std::unique_ptr<AvifDecoderWrapper> decoder(new (std::nothrow)
                                                  AvifDecoderWrapper());
  if (decoder == nullptr) {
    return 0;
  }
  if (!CreateDecoderAndParse(decoder.get(), buffer, size,
                             getThreadCount(threads))) {
    return 0;
  }
  FIND_CLASS(avif_decoder_class, "org/aomedia/avif/android/AvifDecoder", 0);
  GET_FIELD_ID(width_id, avif_decoder_class, "width", "I", 0);
  GET_FIELD_ID(height_id, avif_decoder_class, "height", "I", 0);
  GET_FIELD_ID(depth_id, avif_decoder_class, "depth", "I", 0);
  GET_FIELD_ID(alpha_present_id, avif_decoder_class, "alphaPresent", "Z", 0);
  GET_FIELD_ID(frame_count_id, avif_decoder_class, "frameCount", "I", 0);
  GET_FIELD_ID(repetition_count_id, avif_decoder_class, "repetitionCount", "I",
               0);
  GET_FIELD_ID(frame_durations_id, avif_decoder_class, "frameDurations", "[D",
               0);
  env->SetIntField(thiz, width_id, decoder->crop.width);
  CHECK_EXCEPTION(0);
  env->SetIntField(thiz, height_id, decoder->crop.height);
  CHECK_EXCEPTION(0);
  env->SetIntField(thiz, depth_id, decoder->decoder->image->depth);
  CHECK_EXCEPTION(0);
  env->SetBooleanField(thiz, alpha_present_id, decoder->decoder->alphaPresent);
  CHECK_EXCEPTION(0);
  env->SetIntField(thiz, repetition_count_id,
                   decoder->decoder->repetitionCount);
  CHECK_EXCEPTION(0);
  const int frameCount = decoder->decoder->imageCount;
  env->SetIntField(thiz, frame_count_id, frameCount);
  CHECK_EXCEPTION(0);
  // This native array is needed because setting one element at a time to a Java
  // array from the JNI layer is inefficient.
  std::unique_ptr<double[]> native_durations(
      new (std::nothrow) double[frameCount]);
  if (native_durations == nullptr) {
    return 0;
  }
  for (int i = 0; i < frameCount; ++i) {
    avifImageTiming timing;
    if (avifDecoderNthImageTiming(decoder->decoder, i, &timing) !=
        AVIF_RESULT_OK) {
      return 0;
    }
    native_durations[i] = timing.duration;
  }
  jdoubleArray durations = env->NewDoubleArray(frameCount);
  if (durations == nullptr) {
    return 0;
  }
  env->SetDoubleArrayRegion(durations, /*start=*/0, frameCount,
                            native_durations.get());
  CHECK_EXCEPTION(0);
  env->SetObjectField(thiz, frame_durations_id, durations);
  CHECK_EXCEPTION(0);
  return reinterpret_cast<jlong>(decoder.release());
}

#undef GET_FIELD_ID
#undef FIND_CLASS
#undef CHECK_EXCEPTION

FUNC(jint, nextFrame, jlong jdecoder, jobject bitmap) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  return DecodeNextImage(env, decoder, bitmap);
}

FUNC(jint, nextFrameIndex, jlong jdecoder) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  return decoder->decoder->imageIndex + 1;
}

FUNC(jint, nthFrame, jlong jdecoder, jint n, jobject bitmap) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  return DecodeNthImage(env, decoder, n, bitmap);
}

HW_FUNC(jobject, decodeToHardwareBufferNative, jobject encoded, int length,
        jint target_width, jint target_height, jint threads, jboolean allow_r8) {
  IGNORE_UNUSED_HW_JNI_PARAMETERS;
  return DecodeToHardwareBuffer(env, encoded, length, target_width,
                                target_height, threads, allow_r8 != JNI_FALSE);
}

HW_FUNC(jobject, nextFrameHardwareBufferNative, jlong jdecoder, jint target_width,
        jint target_height, jboolean allow_r8) {
  IGNORE_UNUSED_HW_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  if (decoder == nullptr) {
    return nullptr;
  }
  return NextFrameToHardwareBuffer(env, decoder, target_width, target_height,
                                   allow_r8 != JNI_FALSE);
}

HW_FUNC(jobject, nthFrameHardwareBufferNative, jlong jdecoder, jint n,
        jint target_width, jint target_height, jboolean allow_r8) {
  IGNORE_UNUSED_HW_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  if (decoder == nullptr) {
    return nullptr;
  }
  return NthFrameToHardwareBuffer(env, decoder, static_cast<uint32_t>(n),
                                  target_width, target_height,
                                  allow_r8 != JNI_FALSE);
}

FUNC(jstring, resultToString, jint result) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  return env->NewStringUTF(avifResultToString(static_cast<avifResult>(result)));
}

FUNC(jstring, versionString) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  char codec_versions[256];
  avifCodecVersions(codec_versions);
  char libyuv_version[64];
  if (avifLibYUVVersion() > 0) {
    snprintf(libyuv_version, sizeof(libyuv_version), " libyuv: %u.",
             avifLibYUVVersion());
  } else {
    libyuv_version[0] = '\0';
  }
  char version_string[512];
  snprintf(version_string, sizeof(version_string), "libavif: %s. Codecs: %s.%s",
           avifVersion(), codec_versions, libyuv_version);
  return env->NewStringUTF(version_string);
}

FUNC(void, destroyDecoder, jlong jdecoder) {
  IGNORE_UNUSED_JNI_PARAMETERS;
  AvifDecoderWrapper* const decoder =
      reinterpret_cast<AvifDecoderWrapper*>(jdecoder);
  delete decoder;
}
