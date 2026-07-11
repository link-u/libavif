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
#include <memory>
#include <mutex>
#include <new>

#include "avif/avif.h"

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
  (void)env;                         \
  (void)thiz

#define IGNORE_UNUSED_HW_JNI_PARAMETERS \
  (void)env;                            \
  (void)clazz

namespace {

using AHardwareBuffer_allocate_fn = int (*)(const AHardwareBuffer_Desc*, AHardwareBuffer**);
using AHardwareBuffer_describe_fn = void (*)(const AHardwareBuffer*, AHardwareBuffer_Desc*);
using AHardwareBuffer_lock_fn = int (*)(AHardwareBuffer*, uint64_t, int32_t, const ARect*,
                                        void**);
using AHardwareBuffer_unlock_fn = int (*)(AHardwareBuffer*, int32_t*);
using AHardwareBuffer_release_fn = void (*)(AHardwareBuffer*);
using AHardwareBuffer_toHardwareBuffer_fn = jobject (*)(JNIEnv*, AHardwareBuffer*);

struct HardwareBufferApi {
  void* android_library = nullptr;
  void* nativewindow_library = nullptr;
  AHardwareBuffer_allocate_fn allocate = nullptr;
  AHardwareBuffer_describe_fn describe = nullptr;
  AHardwareBuffer_lock_fn lock = nullptr;
  AHardwareBuffer_unlock_fn unlock = nullptr;
  AHardwareBuffer_release_fn release = nullptr;
  AHardwareBuffer_toHardwareBuffer_fn to_hardware_buffer = nullptr;
  bool available = false;
};

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

  api->android_library = dlopen("libandroid.so", RTLD_NOW);
  if (api->android_library == nullptr) {
    LOGE("Failed to dlopen libandroid.so: %s.", dlerror());
    return;
  }

  api->nativewindow_library = dlopen("libnativewindow.so", RTLD_NOW);
  if (api->nativewindow_library == nullptr) {
    LOGE("Failed to dlopen libnativewindow.so: %s.", dlerror());
    dlclose(api->android_library);
    api->android_library = nullptr;
    return;
  }

#define LOAD_ANDROID_SYMBOL(name)                                                     \
  api->name = reinterpret_cast<AHardwareBuffer_##name##_fn>(                          \
      dlsym(api->android_library, "AHardwareBuffer_" #name));                         \
  if (api->name == nullptr) {                                                         \
    LOGE("Failed to dlsym AHardwareBuffer_" #name ": %s.", dlerror());                \
    dlclose(api->nativewindow_library);                                               \
    dlclose(api->android_library);                                                      \
    api->nativewindow_library = nullptr;                                                \
    api->android_library = nullptr;                                                     \
    return;                                                                           \
  }

  LOAD_ANDROID_SYMBOL(allocate);
  LOAD_ANDROID_SYMBOL(describe);
  LOAD_ANDROID_SYMBOL(lock);
  LOAD_ANDROID_SYMBOL(unlock);
  LOAD_ANDROID_SYMBOL(release);

#undef LOAD_ANDROID_SYMBOL

  api->to_hardware_buffer = reinterpret_cast<AHardwareBuffer_toHardwareBuffer_fn>(
      dlsym(api->nativewindow_library, "AHardwareBuffer_toHardwareBuffer"));
  if (api->to_hardware_buffer == nullptr) {
    LOGE("Failed to dlsym AHardwareBuffer_toHardwareBuffer: %s.", dlerror());
    dlclose(api->nativewindow_library);
    dlclose(api->android_library);
    api->nativewindow_library = nullptr;
    api->android_library = nullptr;
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

  avifRGBImage rgb_image;
  avifRGBImageSetDefaults(&rgb_image, image);
  rgb_image.format = format;
  rgb_image.depth = depth;
  rgb_image.isFloat = is_float;
  rgb_image.pixels = static_cast<uint8_t*>(pixels);
  rgb_image.rowBytes = row_bytes;
  // Android always sees the Bitmaps as premultiplied with alpha when it renders
  // them:
  // https://developer.android.com/reference/android/graphics/Bitmap#setPremultiplied(boolean)
  rgb_image.alphaPremultiplied = AVIF_TRUE;
  res = avifImageYUVToRGB(image, &rgb_image);
  if (res != AVIF_RESULT_OK) {
    LOGE("Failed to convert YUV Pixels to RGB. Status: %d", res);
  }
  return res;
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
                                      uint32_t dst_width, uint32_t dst_height) {
  const HardwareBufferApi& hw_api = GetHardwareBufferApi();
  if (!hw_api.available) {
    return nullptr;
  }

  AHardwareBuffer_Desc desc = {};
  desc.width = dst_width;
  desc.height = dst_height;
  desc.layers = 1;
  desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  desc.usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

  AHardwareBuffer* hw_buffer = nullptr;
  if (hw_api.allocate(&desc, &hw_buffer) != 0) {
    LOGE("AHardwareBuffer_allocate failed.");
    return nullptr;
  }
  hw_api.describe(hw_buffer, &desc);

  void* pixels = nullptr;
  if (hw_api.lock(hw_buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY, -1, nullptr,
                  &pixels) != 0) {
    LOGE("AHardwareBuffer_lock failed.");
    hw_api.release(hw_buffer);
    return nullptr;
  }

  const avifResult res = AvifImageToRGBBuffer(
      decoder, pixels, dst_width, dst_height,
      static_cast<size_t>(desc.stride) * 4, AVIF_RGB_FORMAT_RGBA, 8,
      AVIF_FALSE);
  hw_api.unlock(hw_buffer, nullptr);
  if (res != AVIF_RESULT_OK) {
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
                               int threads) {
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
  return AvifImageToJavaHardwareBuffer(env, &decoder, dst_width, dst_height);
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

HW_FUNC(jobject, decodeToHardwareBufferNative, jobject encoded, int length,
        jint target_width, jint target_height, jint threads) {
  IGNORE_UNUSED_HW_JNI_PARAMETERS;
  return DecodeToHardwareBuffer(env, encoded, length, target_width,
                                target_height, threads);
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
