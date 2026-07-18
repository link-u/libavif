# libavif Android JNI Bindings

This subdirectory contains Android JNI bindings that will enable use of the libavif decoder in Android apps.


## Prerequisites

* [Android SDK](https://developer.android.com/studio#downloads) - For building the JNI bindings. SDK has to be downloaded and set up for Android API target 30.
* [Android NDK](https://developer.android.com/ndk/downloads) - For building the decoder (in the examples, we use dav1d).
* [Gradle](https://gradle.org/).
* CMake.
* Ninja.

## Generate the AAR package

The following steps will generate the AAR package that contains libavif and the JNI wrapper. It can then be used as a dependency for Android apps.

> Note: The instructions in this section will use dav1d as the AV1 decoder. Any other decoder can be used but the `avifandroidjni/src/main/jni/CMakeLists.txt` file has to be updated accordingly.

Step 1 - Checkout libavif

```
$ git clone https://github.com/AOMediaCodec/libavif.git
$ cd libavif
```

Step 2 - Set the SDK and NDK paths in environment variables. (Recommended Android NDK revision: r27c / `27.2.12479018`)

```
$ export ANDROID_SDK_ROOT="/path/to/android/sdk"
$ export ANDROID_NDK_HOME="/path/to/android/ndk"
```

Use a **host-matching** NDK (Linux NDK on Linux/WSL, Darwin NDK on macOS). The Windows
NDK toolchain cannot be used from WSL/Linux.

Step 3 - Checkout and build dav1d (or libgav1)

```
$ cd ext
$ ./dav1d_android.sh "${ANDROID_NDK_HOME}"
$ cd ..
```

`dav1d_android.sh` clones [link-u/dav1d](https://github.com/link-u/dav1d)
(`avif` branch). If `ext/dav1d` is absent, the Android JNI CMake LOCAL path
fetches the same repository/branch via `LocalDav1d.cmake`.

The script generates meson cross-files targeting **API 21** for all ABIs (required for
NDK r27+, which removed API levels below 21). Override with `ANDROID_API` if needed.

The Android dav1d build is configured with `-Dbitdepths=8` (8-bit AV1 only). Re-run the
script after changing this option so that all ABIs are rebuilt.

The Android JNI Release build enables LTO/IPO (`AVIF_ANDROID_ENABLE_LTO`, default ON).
`dav1d_android.sh` and `libyuv_android.sh` build with LTO so those static libraries
participate in the final `libavif_android.so` link. Re-run both scripts after updating
them if you previously built without LTO. Pass `-DAVIF_ANDROID_ENABLE_LTO=OFF` via
Gradle `android.extraCMakeFlags` to disable.

Instrumented tests assume this 8-bit-only dav1d build: 10/12-bit assets are still parsed via
`getInfo`, but decode APIs are expected to fail for those streams. If you rebuild dav1d with full
bitdepths (`-Dbitdepths=8,16`), update the tests accordingly.

If you want to use libgav1 instead:

```
$ cd ext
$ ./libgav1_android.sh "${ANDROID_NDK_HOME}"
$ cd ..
```

Update [CMakeLists.txt](avifandroidjni/src/main/jni/CMakeLists.txt) as follows:
 * Set `AVIF_CODEC_DAV1D` to `OFF`
 * Set `AVIF_CODEC_LIBGAV1` to `LOCAL`.

Step 4 - Checkout and build libyuv

```
$ cd ext
$ ./libyuv_android.sh "${ANDROID_NDK_HOME}"
$ cd ..
```

`libyuv_android.sh` disables JPEG/MJPEG support (`CMAKE_DISABLE_FIND_PACKAGE_JPEG`)
since AVIF decode does not use it.

If you do not want to use libyuv, then update
[CMakeLists.txt](avifandroidjni/src/main/jni/CMakeLists.txt) as follows:
 * Set `AVIF_LIBYUV` to `OFF`.

Step 5 - Build the JNI Wrapper and generate the AAR package

```
$ cd android_jni
$ ./gradlew build assembleRelease
```

If all the steps were completed successfully, the AAR package that contains libavif and the JNI wrapper can be found in `libavif/android_jni/avifandroidjni/build/outputs/aar`. You can now follow the instructions in the next section to include the AAR package as a dependency in your Android project.

## Add an AAR dependency to your Android Project

The instructions on how to add the AAR package as a dependency to your Android project can be found [here](https://developer.android.com/studio/projects/android-library#psd-add-aar-jar-dependency).

## Running Instrumented Tests

Step 1 - Build the library

Make sure to build the library by following the steps under
[Generate the AAR package](#generate-the-aar-package) section above.

These tests assume the default Android dav1d build (`-Dbitdepths=8`). Decode success
cases cover 8-bit images only; 10/12-bit assets verify `getInfo` and clean decode
failure.

Step 2 - Set up a device/emulator

Make sure that a device or an emulator has been set up and is available via
`adb`.

Step 3 - Run the tests

```
$ cd android_jni
$ ./gradlew connectedAndroidTest
```

## Using Android Studio

The entire android_jni directory can be imported as a project into Android Studio.

To build the project from within Android Studio, follow the all the steps from the above section to checkout and build libgav1. After that, the last step is equivalent to invoking the build from Android Studio.

## Maven Releases

Maven hosted version of libavif can be found here:
https://repo1.maven.org/maven2/org/aomedia/avif/android/avif/

## Hardware Buffer Decoding

Use `AvifHardwareDecoder` on API 29+ to decode directly into `HardwareBuffer` objects suitable
for GPU sampling (for example via `Bitmap.wrapHardwareBuffer`).

By default, output uses `RGBA_8888`. Pass `allowGray565 = true` to opt in to **Gray565** packing
(`HardwareBuffer.RGB_565`) when **all** of the following hold:

* The decoded image is 8-bit monochrome (`YUV400`) with no alpha plane.
* `allowGray565` is `true`.

If any condition fails, or if `AHardwareBuffer_allocate` fails for `R5G6B5`, the decoder falls
back to `RGBA_8888`. (`R5G6B5` is a universally supported HardwareBuffer format from API 26; the
caller's API 29 gate for `wrapHardwareBuffer` is sufficient—there is no API 35 requirement.)

`Bitmap.Config.RGB_565` is **not** supported on the software Bitmap decode path; only Gray565 via
`HardwareBuffer` (above) uses the `RGB_565` container.

### Gray565 packing

`RGB_565` here is **not** a true color RGB565 image. It is an 8-bit grayscale value packed into
the RGB565 bit fields (R is the most-significant field):

* Encoding: `Y = 4 * G6 + (R5 & 3)`, `B5 = 0`  
  equivalently `pixel = ((Y & 3) << 11) | ((Y >> 2) << 5)`.
* Limited-range Y (16–235) is expanded to 0–255 in the same LUT that builds the packed value;
  full-range Y is used as-is. Color matrix coefficients (BT.601/709) do not apply on this path.
* Scaling (when requested) is applied with `avifImageScale` on the YUV image **before** packing.

### Display responsibility (required)

Drawing a Gray565 buffer without a restore transform looks like greenish noise, not grayscale.
Callers **must** apply a restore `ColorMatrix` / `ColorMatrixColorFilter` that maps each of R, G,
B to:

`31/255 · r + 252/255 · g`

(where `r`/`g` are the 8-bit expanded R/G channels from the RGB565 sample).

### Example

```java
if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
  HardwareBuffer buffer =
      AvifHardwareDecoder.decodeToHardwareBuffer(
          encoded, encoded.remaining(), 0, 0, threads, /* allowGray565= */ true);
  if (buffer != null) {
    if (buffer.getFormat() == HardwareBuffer.RGB_565) {
      // Apply the Gray565 restore ColorMatrix before drawing.
    }
    buffer.close();
  }
}
```

Range-specific monochrome test assets (`mono_8bpc_limited.avif`, `mono_8bpc_full.avif`,
`mono_8bpc_ramp_full.avif`) can be generated with
[`generate_mono_test_assets.sh`](avifandroidjni/src/androidTest/assets/generate_mono_test_assets.sh).

