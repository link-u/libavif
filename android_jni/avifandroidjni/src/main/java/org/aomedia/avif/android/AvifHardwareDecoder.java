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
    return decodeToHardwareBuffer(encoded, length, targetWidth, targetHeight, threads, false);
  }

  /**
   * Decodes the AVIF image into an {@link HardwareBuffer}.
   *
   * <p>When {@code allowR8} is {@code true} and the image is 8-bit monochrome (YUV400) without
   * alpha on a device running API 35+ that supports {@link HardwareBuffer#R_8 R_8} allocation, the
   * returned buffer's {@link HardwareBuffer#getFormat()} will be {@link HardwareBuffer#R_8} (56).
   * Otherwise the format is {@link HardwareBuffer#RGBA_8888} as in the overload without {@code
   * allowR8}.
   *
   * <p>An {@code R_8} buffer holds a single luminance channel. Drawing it directly (for example via
   * {@link android.graphics.Bitmap#wrapHardwareBuffer}) shows red-tinted intensity; callers must
   * apply a {@link android.graphics.ColorMatrixColorFilter} or equivalent color transform for
   * correct grayscale or RGB display.
   *
   * @param allowR8 When {@code true}, opt in to {@code R_8} output for eligible monochrome images.
   * @see #decodeToHardwareBuffer(ByteBuffer, int, int, int, int)
   */
  @Nullable
  public static HardwareBuffer decodeToHardwareBuffer(
      ByteBuffer encoded,
      int length,
      int targetWidth,
      int targetHeight,
      int threads,
      boolean allowR8) {
    return decodeToHardwareBufferNative(
        encoded, length, targetWidth, targetHeight, threads, allowR8);
  }

  /**
   * Decodes the AVIF image into an {@link HardwareBuffer} at the cropped image dimensions.
   *
   * @see #decodeToHardwareBuffer(ByteBuffer, int, int, int, int)
   */
  @Nullable
  public static HardwareBuffer decodeToHardwareBuffer(ByteBuffer encoded, int length, int threads) {
    return decodeToHardwareBuffer(encoded, length, 0, 0, threads, false);
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
    return nextFrameHardwareBuffer(decoder, targetWidth, targetHeight, false);
  }

  /**
   * Decodes the next frame of an animated AVIF into an {@link HardwareBuffer}.
   *
   * @param allowR8 When {@code true}, opt in to {@code R_8} output for eligible monochrome frames.
   *     See {@link #decodeToHardwareBuffer(ByteBuffer, int, int, int, int, boolean)} for format
   *     selection rules and display responsibilities.
   */
  @Nullable
  public static HardwareBuffer nextFrameHardwareBuffer(
      AvifDecoder decoder, int targetWidth, int targetHeight, boolean allowR8) {
    if (decoder == null || !decoder.isAlive()) {
      return null;
    }
    return nextFrameHardwareBufferNative(
        decoder.getNativeDecoderHandle(), targetWidth, targetHeight, allowR8);
  }

  /** Decodes the next frame at the cropped image dimensions. */
  @Nullable
  public static HardwareBuffer nextFrameHardwareBuffer(AvifDecoder decoder) {
    return nextFrameHardwareBuffer(decoder, 0, 0, false);
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
    return nthFrameHardwareBuffer(decoder, n, targetWidth, targetHeight, false);
  }

  /**
   * Decodes the nth frame of an animated AVIF into an {@link HardwareBuffer}.
   *
   * @param allowR8 When {@code true}, opt in to {@code R_8} output for eligible monochrome frames.
   *     See {@link #decodeToHardwareBuffer(ByteBuffer, int, int, int, int, boolean)} for format
   *     selection rules and display responsibilities.
   */
  @Nullable
  public static HardwareBuffer nthFrameHardwareBuffer(
      AvifDecoder decoder, int n, int targetWidth, int targetHeight, boolean allowR8) {
    if (decoder == null || !decoder.isAlive()) {
      return null;
    }
    return nthFrameHardwareBufferNative(
        decoder.getNativeDecoderHandle(), n, targetWidth, targetHeight, allowR8);
  }

  /** Decodes the nth frame at the cropped image dimensions. */
  @Nullable
  public static HardwareBuffer nthFrameHardwareBuffer(AvifDecoder decoder, int n) {
    return nthFrameHardwareBuffer(decoder, n, 0, 0, false);
  }

  private static native HardwareBuffer decodeToHardwareBufferNative(
      ByteBuffer encoded,
      int length,
      int targetWidth,
      int targetHeight,
      int threads,
      boolean allowR8);

  private static native HardwareBuffer nextFrameHardwareBufferNative(
      long nativeDecoderHandle, int targetWidth, int targetHeight, boolean allowR8);

  private static native HardwareBuffer nthFrameHardwareBufferNative(
      long nativeDecoderHandle, int n, int targetWidth, int targetHeight, boolean allowR8);
}
