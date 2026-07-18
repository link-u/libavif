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
   * If either is zero or negative, the encoded image dimensions are used (partial scaling is not
   * supported).
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param targetWidth Desired output width when scaling; ignored unless {@code targetHeight} is
   *     also positive.
   * @param targetHeight Desired output height when scaling; ignored unless {@code targetWidth} is
   *     also positive.
   * @param threads Ignored. Decoding always uses a single thread (maxThreads=1)
   *     to reduce RAM. Kept for API compatibility.
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
   * <p>When {@code allowGray565} is {@code true} and the image is 8-bit monochrome ({@code YUV400})
   * without an alpha plane, the returned buffer's {@link HardwareBuffer#getFormat()} is {@link
   * HardwareBuffer#RGB_565} (packed grayscale — see below). Otherwise the format is {@link
   * HardwareBuffer#RGBA_8888} as in the overload without {@code allowGray565}. If {@code RGB_565}
   * allocation fails, the decoder falls back to {@code RGBA_8888}.
   *
   * <p><b>Gray565 contract:</b> {@link HardwareBuffer#RGB_565} here is <em>not</em> a true color
   * RGB565 image. It is an 8-bit grayscale value packed into the RGB565 bit fields:
   *
   * <ul>
   *   <li>Encoding: {@code Y = 4 * G6 + (R5 & 3)}, {@code B5 = 0} (R is MSB: {@code R<<11 | G<<5 |
   *       B}).
   *   <li>Display: callers <em>must</em> apply a restore {@link android.graphics.ColorMatrix} that
   *       maps each of R,G,B to {@code 31/255·r + 252/255·g}. Drawing without that matrix looks like
   *       greenish noise, not grayscale.
   * </ul>
   *
   * @param allowGray565 When {@code true}, opt in to Gray565 ({@code RGB_565}) packing for eligible
   *     monochrome images.
   * @see #decodeToHardwareBuffer(ByteBuffer, int, int, int, int)
   */
  @Nullable
  public static HardwareBuffer decodeToHardwareBuffer(
      ByteBuffer encoded,
      int length,
      int targetWidth,
      int targetHeight,
      int threads,
      boolean allowGray565) {
    return decodeToHardwareBufferNative(
        encoded, length, targetWidth, targetHeight, threads, allowGray565);
  }

  /**
   * Decodes the AVIF image into an {@link HardwareBuffer} at the encoded image dimensions.
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
   * Otherwise the encoded image dimensions are used.
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
   * @param allowGray565 When {@code true}, opt in to Gray565 output for eligible monochrome frames.
   *     See {@link #decodeToHardwareBuffer(ByteBuffer, int, int, int, int, boolean)} for format
   *     selection rules and display responsibilities.
   */
  @Nullable
  public static HardwareBuffer nextFrameHardwareBuffer(
      AvifDecoder decoder, int targetWidth, int targetHeight, boolean allowGray565) {
    if (decoder == null || !decoder.isAlive()) {
      return null;
    }
    return nextFrameHardwareBufferNative(
        decoder.getNativeDecoderHandle(), targetWidth, targetHeight, allowGray565);
  }

  /** Decodes the next frame at the encoded image dimensions. */
  @Nullable
  public static HardwareBuffer nextFrameHardwareBuffer(AvifDecoder decoder) {
    return nextFrameHardwareBuffer(decoder, 0, 0, false);
  }

  /**
   * Decodes the nth frame of an animated AVIF into an {@link HardwareBuffer}.
   *
   * <p>Scaling applies only when both {@code targetWidth} and {@code targetHeight} are positive.
   * Otherwise the encoded image dimensions are used.
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
   * @param allowGray565 When {@code true}, opt in to Gray565 output for eligible monochrome frames.
   *     See {@link #decodeToHardwareBuffer(ByteBuffer, int, int, int, int, boolean)} for format
   *     selection rules and display responsibilities.
   */
  @Nullable
  public static HardwareBuffer nthFrameHardwareBuffer(
      AvifDecoder decoder, int n, int targetWidth, int targetHeight, boolean allowGray565) {
    if (decoder == null || !decoder.isAlive()) {
      return null;
    }
    return nthFrameHardwareBufferNative(
        decoder.getNativeDecoderHandle(), n, targetWidth, targetHeight, allowGray565);
  }

  /** Decodes the nth frame at the encoded image dimensions. */
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
      boolean allowGray565);

  private static native HardwareBuffer nextFrameHardwareBufferNative(
      long nativeDecoderHandle, int targetWidth, int targetHeight, boolean allowGray565);

  private static native HardwareBuffer nthFrameHardwareBufferNative(
      long nativeDecoderHandle, int n, int targetWidth, int targetHeight, boolean allowGray565);
}
