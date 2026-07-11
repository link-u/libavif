// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

package org.aomedia.avif.android;

import android.hardware.HardwareBuffer;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import java.nio.ByteBuffer;

/**
 * AVIF decoder entry points that write directly into {@link HardwareBuffer} objects.
 *
 * <p>This class is separate from {@link AvifDecoder} so that apps with {@code minSdkVersion} below
 * 26 can still load and use {@link AvifDecoder} on older devices. Only call into this class on API
 * 29+ after checking {@code Build.VERSION.SDK_INT}.
 */
@RequiresApi(29)
public final class AvifHardwareDecoder {
  private AvifHardwareDecoder() {}

  static {
    try {
      System.loadLibrary("avif_android");
    } catch (UnsatisfiedLinkError exception) {
      exception.printStackTrace();
    }
  }

  /**
   * Decodes the AVIF image into an {@link HardwareBuffer} with RGBA_8888 pixels.
   *
   * <p>The returned buffer can be wrapped as a hardware {@link android.graphics.Bitmap} via {@link
   * android.graphics.Bitmap#wrapHardwareBuffer(HardwareBuffer,
   * android.graphics.ColorSpace)}.
   *
   * <p>Scaling applies only when both {@code targetWidth} and {@code targetHeight} are positive.
   * If either is zero or negative, the cropped image dimensions are used (partial scaling is not
   * supported).
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param targetWidth Desired output width when scaling; ignored unless {@code targetHeight} is
   *     also positive.
   * @param targetHeight Desired output height when scaling; ignored unless {@code targetWidth} is
   *     also positive.
   * @param threads Number of threads to be used for the AVIF decode.
   * @return a HardwareBuffer on success, or null on failure.
   */
  @Nullable
  public static HardwareBuffer decodeToHardwareBuffer(
      ByteBuffer encoded, int length, int targetWidth, int targetHeight, int threads) {
    return decodeToHardwareBufferNative(encoded, length, targetWidth, targetHeight, threads);
  }

  /**
   * Decodes the AVIF image into an {@link HardwareBuffer} at the cropped image dimensions.
   *
   * @see #decodeToHardwareBuffer(ByteBuffer, int, int, int, int)
   */
  @Nullable
  public static HardwareBuffer decodeToHardwareBuffer(ByteBuffer encoded, int length, int threads) {
    return decodeToHardwareBuffer(encoded, length, 0, 0, threads);
  }

  /**
   * Decodes the next frame of an animated AVIF into an {@link HardwareBuffer}.
   *
   * <p>Scaling applies only when both {@code targetWidth} and {@code targetHeight} are positive.
   * Otherwise the cropped image dimensions are used.
   *
   * @param decoder A live {@link AvifDecoder} instance created via {@link AvifDecoder#create}. Do
   *     not call {@link AvifDecoder#release()} until this method returns.
   */
  @Nullable
  public static HardwareBuffer nextFrameHardwareBuffer(
      AvifDecoder decoder, int targetWidth, int targetHeight) {
    if (decoder == null || !decoder.isAlive()) {
      return null;
    }
    return nextFrameHardwareBufferNative(
        decoder.getNativeDecoderHandle(), targetWidth, targetHeight);
  }

  /** Decodes the next frame at the cropped image dimensions. */
  @Nullable
  public static HardwareBuffer nextFrameHardwareBuffer(AvifDecoder decoder) {
    return nextFrameHardwareBuffer(decoder, 0, 0);
  }

  /**
   * Decodes the nth frame of an animated AVIF into an {@link HardwareBuffer}.
   *
   * <p>Scaling applies only when both {@code targetWidth} and {@code targetHeight} are positive.
   * Otherwise the cropped image dimensions are used.
   *
   * @param decoder A live {@link AvifDecoder} instance created via {@link AvifDecoder#create}. Do
   *     not call {@link AvifDecoder#release()} until this method returns.
   * @param n The zero-based index of the frame to be decoded.
   */
  @Nullable
  public static HardwareBuffer nthFrameHardwareBuffer(
      AvifDecoder decoder, int n, int targetWidth, int targetHeight) {
    if (decoder == null || !decoder.isAlive()) {
      return null;
    }
    return nthFrameHardwareBufferNative(
        decoder.getNativeDecoderHandle(), n, targetWidth, targetHeight);
  }

  /** Decodes the nth frame at the cropped image dimensions. */
  @Nullable
  public static HardwareBuffer nthFrameHardwareBuffer(AvifDecoder decoder, int n) {
    return nthFrameHardwareBuffer(decoder, n, 0, 0);
  }

  private static native HardwareBuffer decodeToHardwareBufferNative(
      ByteBuffer encoded, int length, int targetWidth, int targetHeight, int threads);

  private static native HardwareBuffer nextFrameHardwareBufferNative(
      long nativeDecoderHandle, int targetWidth, int targetHeight);

  private static native HardwareBuffer nthFrameHardwareBufferNative(
      long nativeDecoderHandle, int n, int targetWidth, int targetHeight);
}
