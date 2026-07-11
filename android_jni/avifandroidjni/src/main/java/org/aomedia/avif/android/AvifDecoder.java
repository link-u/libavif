// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

package org.aomedia.avif.android;

import android.graphics.Bitmap;
import java.nio.ByteBuffer;

/**
 * A static utility class for decoding still AVIF images.
 *
 * <p>AVIF Specification: https://aomediacodec.github.io/av1-avif/.
 *
 * <p>For direct {@link android.hardware.HardwareBuffer} output on API 29+, use {@link
 * AvifHardwareDecoder} instead.
 */
@SuppressWarnings("CatchAndPrintStackTrace")
public final class AvifDecoder {
  private AvifDecoder() {}

  static {
    try {
      System.loadLibrary("avif_android");
    } catch (UnsatisfiedLinkError exception) {
      exception.printStackTrace();
    }
  }

  /** Contains information about the AVIF Image. This class is only used for getInfo(). */
  public static class Info {
    public int width;
    public int height;
    public int depth;
    public boolean alphaPresent;
  }

  /**
   * Returns true if the bytes in the buffer seem like an AVIF image.
   *
   * @param buffer The encoded image. buffer.position() must be 0.
   * @return true if the bytes seem like an AVIF image, false otherwise.
   */
  public static boolean isAvifImage(ByteBuffer buffer) {
    return AvifDecoder.isAvifImage(buffer, buffer.remaining());
  }

  private static native boolean isAvifImage(ByteBuffer encoded, int length);

  /**
   * Parses the AVIF header and populates the Info.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param info Output parameter whose fields will be populated.
   * @return true on success and false on failure.
   */
  public static native boolean getInfo(ByteBuffer encoded, int length, Info info);

  /**
   * Decodes the AVIF image into the bitmap.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param bitmap The decoded pixels will be copied into the bitmap.
   *     If the bitmap dimensions do not match the decoded image's dimensions,
   *               then the decoded image will be scaled to match the bitmap's dimensions.
   * @return true on success and false on failure. A few possible reasons for failure are: 1) Input
   *     was not valid AVIF.
   */
  public static boolean decode(ByteBuffer encoded, int length, Bitmap bitmap) {
    return decode(encoded, length, bitmap, 0);
  }

  /**
   * Decodes the AVIF image into the bitmap.
   *
   * @param encoded The encoded AVIF image. encoded.position() must be 0.
   * @param length Length of the encoded buffer.
   * @param bitmap The decoded pixels will be copied into the bitmap.
   *     If the bitmap dimensions do not match the decoded image's dimensions,
   *               then the decoded image will be scaled to match the bitmap's dimensions.
   * @param threads Number of threads to be used for the AVIF decode. Zero means use the library
   *     determined optimal value as the thread count. Negative values mean use the number of CPU
   *     cores as the thread count. For more details, see the documentation for maxThreads variable
   *     in avif.h.
   * @return true on success and false on failure.
   */
  public static native boolean decode(ByteBuffer encoded, int length, Bitmap bitmap, int threads);

  /**
   * Returns a String describing an avifResult enum value.
   *
   * @param result The avifResult value.
   * @return A String containing the description of the avifResult.
   */
  public static native String resultToString(int result);

  /**
   * Returns a String that contains information about the libavif version, underlying codecs and
   * libyuv version (if available).
   */
  public static native String versionString();
}
