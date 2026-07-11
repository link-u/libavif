package org.aomedia.avif.android;

import static com.google.common.truth.Truth.assertThat;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Bitmap.Config;
import android.graphics.ColorSpace;
import android.hardware.HardwareBuffer;
import android.os.Build;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import org.aomedia.avif.android.AvifDecoder.Info;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameter;
import org.junit.runners.Parameterized.Parameters;

/** Instrumentation tests for the libavif JNI API, which will execute on an Android device. */
@RunWith(Parameterized.class)
public class AvifDecoderTest {

  private static class Image {
    public final String directory;
    public final String filename;
    public final int width;
    public final int height;
    public final int depth;
    public final boolean alphaPresent;
    public final int threads;

    public Image(
        String directory,
        String filename,
        int width,
        int height,
        int depth,
        boolean alphaPresent,
        int threads) {
      this.directory = directory;
      this.filename = filename;
      this.width = width;
      this.height = height;
      this.depth = depth;
      this.alphaPresent = alphaPresent;
      this.threads = threads;
    }

    public ByteBuffer getBuffer() throws IOException {
      Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
      String assetPath = Paths.get(directory, filename).toString();
      InputStream is = context.getAssets().open(assetPath);
      ByteBuffer buffer = ByteBuffer.allocateDirect(is.available());
      Channels.newChannel(is).read(buffer);
      buffer.rewind();
      return buffer;
    }
  }

  private static final int AVIF_RESULT_OK = 0;

  private static final float[] SCALE_FACTORS = {0.5f, 1.3f};

  private static final Image[] IMAGES = {
    // Parameter ordering: directory, filename, width, height, depth, alphaPresent, threads.
    new Image("avif", "fox.profile0.10bpc.yuv420.avif", 1204, 800, 10, false, 1),
    new Image("avif", "fox.profile0.10bpc.yuv420.monochrome.avif", 1204, 800, 10, false, 1),
    new Image("avif", "fox.profile0.8bpc.yuv420.avif", 1204, 800, 8, false, 1),
    new Image("avif", "fox.profile0.8bpc.yuv420.monochrome.avif", 1204, 800, 8, false, 1),
    new Image("avif", "fox.profile1.10bpc.yuv444.avif", 1204, 800, 10, false, 1),
    new Image("avif", "fox.profile1.8bpc.yuv444.avif", 1204, 800, 8, false, 1),
    new Image("avif", "fox.profile2.10bpc.yuv422.avif", 1204, 800, 10, false, 1),
    new Image("avif", "fox.profile2.12bpc.yuv420.avif", 1204, 800, 12, false, 1),
    new Image("avif", "fox.profile2.12bpc.yuv420.monochrome.avif", 1204, 800, 12, false, 1),
    new Image("avif", "fox.profile2.12bpc.yuv422.avif", 1204, 800, 12, false, 1),
    new Image("avif", "fox.profile2.12bpc.yuv444.avif", 1204, 800, 12, false, 1),
    new Image("avif", "fox.profile2.8bpc.yuv422.avif", 1204, 800, 8, false, 1),
    new Image("avif", "blue-and-magenta-crop.avif", 180, 100, 8, true, 1),
  };

  @Parameters
  public static List<Object[]> data() throws IOException {
    ArrayList<Object[]> list = new ArrayList<>();
    for (Image image : IMAGES) {
      // Test ARGB_8888 for all files.
      list.add(new Object[] {Config.ARGB_8888, image});
      // For 8bpc files, test RGB_565. For other files, test RGBA_F16.
      Config testConfig = image.depth == 8 ? Config.RGB_565 : Config.RGBA_F16;
      list.add(new Object[] {testConfig, image});
    }
    return list;
  }

  @Parameter(0)
  public Bitmap.Config config;

  @Parameter(1)
  public Image image;

  @Test
  public void testDecodeUtilityClass() throws IOException {
    ByteBuffer buffer = image.getBuffer();
    assertThat(buffer).isNotNull();
    assertThat(AvifDecoder.isAvifImage(buffer)).isTrue();
    Info info = new Info();
    assertThat(AvifDecoder.getInfo(buffer, buffer.remaining(), info)).isTrue();
    assertThat(info.width).isEqualTo(image.width);
    assertThat(info.height).isEqualTo(image.height);
    assertThat(info.depth).isEqualTo(image.depth);
    assertThat(info.alphaPresent).isEqualTo(image.alphaPresent);
    Bitmap bitmap = Bitmap.createBitmap(info.width, info.height, config);
    assertThat(bitmap).isNotNull();
    assertThat(AvifDecoder.decode(buffer, buffer.remaining(), bitmap)).isTrue();

    // Test scaling. These tests can be a bit slow on emulators, so only run them when config is
    // ARGB_8888.
    if (config == Config.ARGB_8888) {
      for (float scaleFactor : SCALE_FACTORS) {
        // Scale both width and height.
        bitmap =
            Bitmap.createBitmap(
                (int) (info.width * scaleFactor), (int) (info.height * scaleFactor), config);
        assertThat(bitmap).isNotNull();
        assertThat(AvifDecoder.decode(buffer, buffer.remaining(), bitmap)).isTrue();

        // Scale width only.
        bitmap = Bitmap.createBitmap((int) (info.width * scaleFactor), info.height, config);
        assertThat(bitmap).isNotNull();
        assertThat(AvifDecoder.decode(buffer, buffer.remaining(), bitmap)).isTrue();

        // Scale height only.
        bitmap = Bitmap.createBitmap(info.width, (int) (info.height * scaleFactor), config);
        assertThat(bitmap).isNotNull();
        assertThat(AvifDecoder.decode(buffer, buffer.remaining(), bitmap)).isTrue();
      }
    }
  }

  @Test
  public void testDecodeToHardwareBuffer() throws IOException {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
      return;
    }
    if (config != Config.ARGB_8888) {
      return;
    }
    ByteBuffer buffer = image.getBuffer();
    assertThat(buffer).isNotNull();
    Info info = new Info();
    assertThat(AvifDecoder.getInfo(buffer, buffer.remaining(), info)).isTrue();

    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(buffer, buffer.remaining(), image.threads);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getWidth()).isEqualTo(info.width);
    assertThat(hardwareBuffer.getHeight()).isEqualTo(info.height);
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGBA_8888);
    hardwareBuffer.close();

    for (float scaleFactor : SCALE_FACTORS) {
      int targetWidth = (int) (info.width * scaleFactor);
      int targetHeight = (int) (info.height * scaleFactor);
      hardwareBuffer =
          AvifHardwareDecoder.decodeToHardwareBuffer(
              buffer, buffer.remaining(), targetWidth, targetHeight, image.threads);
      assertThat(hardwareBuffer).isNotNull();
      assertThat(hardwareBuffer.getWidth()).isEqualTo(targetWidth);
      assertThat(hardwareBuffer.getHeight()).isEqualTo(targetHeight);
      Bitmap hardwareBitmap =
          Bitmap.wrapHardwareBuffer(hardwareBuffer, ColorSpace.get(ColorSpace.Named.SRGB));
      assertThat(hardwareBitmap).isNotNull();
      assertThat(hardwareBitmap.getWidth()).isEqualTo(targetWidth);
      assertThat(hardwareBitmap.getHeight()).isEqualTo(targetHeight);
      hardwareBitmap.recycle();
      hardwareBuffer.close();
    }
  }

  @Test
  public void testUtilityFunctions() throws IOException {
    // Test the avifResult value whose value and string representations are least likely to change.
    assertThat(AvifDecoder.resultToString(AVIF_RESULT_OK)).isEqualTo("OK");
    // Ensure that the version string starts with "libavif".
    assertThat(AvifDecoder.versionString()).startsWith("libavif");
    // Ensure that the version string contains "libyuv".
    assertThat(AvifDecoder.versionString()).contains("libyuv");
    // Ensure that the version string contains "dav1d".
    assertThat(AvifDecoder.versionString()).contains("dav1d");
  }
}
