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
 * HardwareBuffer Gray565 packing tests for {@link AvifHardwareDecoder}.
 *
 * <p>Requires API 29+ ({@code Bitmap.wrapHardwareBuffer}). Generate mono assets with {@code
 * generate_mono_test_assets.sh} before running.
 */
@RunWith(JUnit4.class)
public class AvifHardwareDecoderGray565Test {

  // Restores packed Gray565: Y ≈ 31/255·R + 252/255·G (B unused / zero).
  private static final ColorMatrix GRAY565_RESTORE_MATRIX =
      new ColorMatrix(
          new float[] {
            31f / 255f, 252f / 255f, 0f, 0f, 0f,
            31f / 255f, 252f / 255f, 0f, 0f, 0f,
            31f / 255f, 252f / 255f, 0f, 0f, 0f,
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

  private static Bitmap decodeSoftwareArgb(ByteBuffer encoded) {
    AvifDecoder.Info info = new AvifDecoder.Info();
    encoded.rewind();
    assertThat(AvifDecoder.getInfo(encoded, encoded.remaining(), info)).isTrue();
    Bitmap bitmap = Bitmap.createBitmap(info.width, info.height, Config.ARGB_8888);
    encoded.rewind();
    assertThat(AvifDecoder.decode(encoded, encoded.remaining(), bitmap)).isTrue();
    return bitmap;
  }

  private static Bitmap restoreGray565ToSoftware(HardwareBuffer hardwareBuffer) {
    Bitmap hardwareBitmap =
        Bitmap.wrapHardwareBuffer(hardwareBuffer, ColorSpace.get(ColorSpace.Named.SRGB));
    assertThat(hardwareBitmap).isNotNull();
    Bitmap software =
        Bitmap.createBitmap(hardwareBitmap.getWidth(), hardwareBitmap.getHeight(), Config.ARGB_8888);
    Canvas canvas = new Canvas(software);
    Paint paint = new Paint();
    paint.setColorFilter(new ColorMatrixColorFilter(GRAY565_RESTORE_MATRIX));
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

  @Test
  public void monoAllowGray565_returnsRgb565() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_full.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGB_565);
    hardwareBuffer.close();
  }

  @Test
  public void monoAllowGray565False_returnsRgba8888() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_full.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ false);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGBA_8888);
    hardwareBuffer.close();
  }

  @Test
  public void colorImageAllowGray565_returnsRgba8888() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/fox.profile0.8bpc.yuv420.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGBA_8888);
    hardwareBuffer.close();
  }

  @Test
  public void alphaImageAllowGray565_returnsRgba8888() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/blue-and-magenta-crop.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGBA_8888);
    hardwareBuffer.close();
  }

  @Test
  public void rampFull_gray565MatchesSoftwareDecode() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_ramp_full.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGB_565);

    Bitmap restored = restoreGray565ToSoftware(hardwareBuffer);
    hardwareBuffer.close();
    buffer.rewind();
    Bitmap software = decodeSoftwareArgb(buffer);
    assertBitmapsNearlyEqual(restored, software, /* tolerance= */ 1);
    restored.recycle();
    software.recycle();
  }

  @Test
  public void limitedRangeMono_gray565MatchesSoftwareDecode() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_limited.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGB_565);

    Bitmap restored = restoreGray565ToSoftware(hardwareBuffer);
    hardwareBuffer.close();
    buffer.rewind();
    Bitmap software = decodeSoftwareArgb(buffer);
    assertBitmapsNearlyEqual(restored, software, /* tolerance= */ 1);
    restored.recycle();
    software.recycle();
  }

  @Test
  public void fullRangeMono_gray565MatchesSoftwareDecode() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    ByteBuffer buffer = loadAsset("avif/mono_8bpc_full.avif");
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGB_565);

    Bitmap restored = restoreGray565ToSoftware(hardwareBuffer);
    hardwareBuffer.close();
    buffer.rewind();
    Bitmap software = decodeSoftwareArgb(buffer);
    assertBitmapsNearlyEqual(restored, software, /* tolerance= */ 1);
    restored.recycle();
    software.recycle();
  }
}
