package org.aomedia.avif.android;

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;
import static org.junit.Assume.assumeTrue;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Bitmap.Config;
import android.graphics.Canvas;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.ColorSpace;
import android.graphics.Paint;
import android.hardware.HardwareBuffer;
import android.os.Build;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * HardwareBuffer R8 (GL ES written, 1 byte/pixel) output tests for {@link AvifHardwareDecoder}.
 *
 * <p>{@code HardwareBuffer.R_8} and {@code Build.VERSION_CODES.TIRAMISU} are API 33 symbols that
 * are not visible with this module's {@code compileSdk}, so the raw values are used. Every R8
 * assertion is skipped below API 33, where the JNI layer never selects R8. Generate mono assets
 * with {@code generate_mono_test_assets.sh} before running.
 */
@RunWith(JUnit4.class)
public class AvifHardwareDecoderR8Test {

  /** {@code HardwareBuffer.R_8} (API 33). */
  private static final int HARDWARE_BUFFER_R_8 = 0x38;

  /** {@code Build.VERSION_CODES.TIRAMISU}. */
  private static final int MIN_R8_API_LEVEL = 33;

  // R8 is sampled by Skia as red-only; copy R into R, G and B to display grayscale.
  private static final ColorMatrix R8_RESTORE_MATRIX =
      new ColorMatrix(
          new float[] {
            1f, 0f, 0f, 0f, 0f,
            1f, 0f, 0f, 0f, 0f,
            1f, 0f, 0f, 0f, 0f,
            0f, 0f, 0f, 1f, 0f
          });

  private static ByteBuffer loadAsset(String assetPath) throws IOException {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    InputStream is = context.getAssets().open(assetPath);
    ByteBuffer buffer = ByteBuffer.allocateDirect(is.available());
    Channels.newChannel(is).read(buffer);
    buffer.rewind();
    return buffer;
  }

  private static HardwareBuffer decodeWithR8(ByteBuffer buffer, boolean allowGray565) {
    return AvifHardwareDecoder.decodeToHardwareBuffer(
        buffer,
        buffer.remaining(),
        0,
        0,
        /* threads= */ 1,
        allowGray565,
        /* allowR8= */ true);
  }

  private static Bitmap decodeSoftwareArgb(ByteBuffer encoded) {
    AvifDecoder.Info info = new AvifDecoder.Info();
    encoded.rewind();
    assertThat(AvifDecoder.getInfo(encoded, encoded.remaining(), info)).isTrue();
    Bitmap bitmap = Bitmap.createBitmap(info.width, info.height, Config.ARGB_8888);
    encoded.rewind();
    assertThat(AvifDecoder.decode(encoded, encoded.remaining(), bitmap)).isTrue();
    return bitmap;
  }

  private static Bitmap restoreR8ToSoftware(HardwareBuffer hardwareBuffer) {
    Bitmap hardwareBitmap =
        Bitmap.wrapHardwareBuffer(hardwareBuffer, ColorSpace.get(ColorSpace.Named.SRGB));
    assertWithMessage("Bitmap.wrapHardwareBuffer(R_8) returned null")
        .that(hardwareBitmap)
        .isNotNull();
    Bitmap software =
        Bitmap.createBitmap(hardwareBitmap.getWidth(), hardwareBitmap.getHeight(), Config.ARGB_8888);
    Canvas canvas = new Canvas(software);
    Paint paint = new Paint();
    paint.setColorFilter(new ColorMatrixColorFilter(R8_RESTORE_MATRIX));
    canvas.drawBitmap(hardwareBitmap, 0f, 0f, paint);
    hardwareBitmap.recycle();
    return software;
  }

  private static void assertBitmapsNearlyEqual(Bitmap actual, Bitmap expected, int tolerance) {
    assertThat(actual.getWidth()).isEqualTo(expected.getWidth());
    assertThat(actual.getHeight()).isEqualTo(expected.getHeight());
    int width = actual.getWidth();
    int height = actual.getHeight();
    int[] actualPixels = new int[width * height];
    int[] expectedPixels = new int[width * height];
    actual.getPixels(actualPixels, 0, width, 0, 0, width, height);
    expected.getPixels(expectedPixels, 0, width, 0, 0, width, height);
    for (int i = 0; i < actualPixels.length; ++i) {
      int a = actualPixels[i];
      int e = expectedPixels[i];
      assertChannelNear("R", (a >> 16) & 0xff, (e >> 16) & 0xff, tolerance, i);
      assertChannelNear("G", (a >> 8) & 0xff, (e >> 8) & 0xff, tolerance, i);
      assertChannelNear("B", a & 0xff, e & 0xff, tolerance, i);
      assertChannelNear("A", (a >>> 24) & 0xff, (e >>> 24) & 0xff, tolerance, i);
    }
  }

  private static void assertChannelNear(
      String channel, int actual, int expected, int tolerance, int index) {
    assertWithMessage(
            "%s at pixel %s: actual=%s expected=%s tol=%s",
            channel, index, actual, expected, tolerance)
        .that(Math.abs(actual - expected))
        .isAtMost(tolerance);
  }

  /**
   * R8 needs GLES 3.0 + EGLImage support in addition to API 33. A device that lacks either falls
   * back to Gray565 / RGBA by design, which is not a decoder bug; skip pixel checks in that case.
   */
  private static void assumeR8Selected(HardwareBuffer hardwareBuffer) {
    assumeTrue(
        "Device fell back from R8 (format=" + hardwareBuffer.getFormat() + ")",
        hardwareBuffer.getFormat() == HARDWARE_BUFFER_R_8);
  }

  @Test
  public void monoAllowR8_returnsR8() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= MIN_R8_API_LEVEL);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_full.avif");
    HardwareBuffer hardwareBuffer = decodeWithR8(buffer, /* allowGray565= */ false);
    assertThat(hardwareBuffer).isNotNull();
    assumeR8Selected(hardwareBuffer);
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HARDWARE_BUFFER_R_8);
    assertThat(hardwareBuffer.getLayers()).isEqualTo(1);
    hardwareBuffer.close();
  }

  @Test
  public void monoAllowR8_belowApi33_neverReturnsR8() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    assumeTrue(Build.VERSION.SDK_INT < MIN_R8_API_LEVEL);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_full.avif");
    HardwareBuffer hardwareBuffer = decodeWithR8(buffer, /* allowGray565= */ false);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGBA_8888);
    hardwareBuffer.close();
  }

  @Test
  public void monoAllowR8False_allowGray565_returnsRgb565() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_full.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer,
            buffer.remaining(),
            0,
            0,
            /* threads= */ 1,
            /* allowGray565= */ true,
            /* allowR8= */ false);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGB_565);
    hardwareBuffer.close();
  }

  @Test
  public void monoAllowR8AndGray565_neverReturnsRgba8888() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_full.avif");
    HardwareBuffer hardwareBuffer = decodeWithR8(buffer, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    // Either R8 (preferred) or the Gray565 fallback, but never the 4 B/px path.
    assertThat(hardwareBuffer.getFormat()).isAnyOf(HARDWARE_BUFFER_R_8, HardwareBuffer.RGB_565);
    hardwareBuffer.close();
  }

  @Test
  public void colorImageAllowR8_returnsRgba8888() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/fox.profile0.8bpc.yuv420.avif");
    HardwareBuffer hardwareBuffer = decodeWithR8(buffer, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGBA_8888);
    hardwareBuffer.close();
  }

  @Test
  public void alphaImageAllowR8_returnsRgba8888() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/blue-and-magenta-crop.avif");
    HardwareBuffer hardwareBuffer = decodeWithR8(buffer, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGBA_8888);
    hardwareBuffer.close();
  }

  private static void assertR8MatchesSoftwareDecode(String assetPath) throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= MIN_R8_API_LEVEL);
    ByteBuffer buffer = loadAsset(assetPath);
    HardwareBuffer hardwareBuffer = decodeWithR8(buffer, /* allowGray565= */ false);
    assertThat(hardwareBuffer).isNotNull();
    assumeR8Selected(hardwareBuffer);

    Bitmap restored = restoreR8ToSoftware(hardwareBuffer);
    hardwareBuffer.close();
    buffer.rewind();
    Bitmap software = decodeSoftwareArgb(buffer);
    // Limited-range expansion happens in float on the GPU and rounds once; +/-1 covers it.
    assertBitmapsNearlyEqual(restored, software, /* tolerance= */ 1);
    restored.recycle();
    software.recycle();
  }

  @Test
  public void rampFull_r8MatchesSoftwareDecode() throws IOException {
    assertR8MatchesSoftwareDecode("avif/mono_8bpc_ramp_full.avif");
  }

  @Test
  public void limitedRangeMono_r8MatchesSoftwareDecode() throws IOException {
    assertR8MatchesSoftwareDecode("avif/mono_8bpc_limited.avif");
  }

  @Test
  public void fullRangeMono_r8MatchesSoftwareDecode() throws IOException {
    assertR8MatchesSoftwareDecode("avif/mono_8bpc_full.avif");
  }
}
