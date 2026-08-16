package org.aomedia.avif.android;

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assume.assumeTrue;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Bitmap.Config;
import android.hardware.HardwareBuffer;
import android.os.Build;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import org.aomedia.avif.android.AvifDecoder.Info;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Glenwood 8-bit decode matrix: AV1 YUV400/YUV420 Limited and AV2 YUV400/YUV444 Full.
 *
 * <p>AV2 {@code .avif} fixtures are optional. Generate dav2d-compatible samples and place them
 * under {@code androidTest/assets/avif/} (see that directory's README). Tests skip when a file is
 * absent rather than committing invalid bitstreams.
 */
@RunWith(JUnit4.class)
public class AvifGlenwoodProfileTest {

  private static ByteBuffer loadAsset(String assetPath) throws IOException {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    InputStream is = context.getAssets().open(assetPath);
    ByteBuffer buffer = ByteBuffer.allocateDirect(is.available());
    Channels.newChannel(is).read(buffer);
    buffer.rewind();
    return buffer;
  }

  private static boolean assetExists(String assetPath) {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    try (InputStream is = context.getAssets().open(assetPath)) {
      return is != null;
    } catch (FileNotFoundException e) {
      return false;
    } catch (IOException e) {
      return false;
    }
  }

  private static Info decodeArgb(String assetPath, boolean alpha) throws IOException {
    ByteBuffer buffer = loadAsset(assetPath);
    assertThat(AvifDecoder.isAvifImage(buffer)).isTrue();
    Info info = new Info();
    assertThat(AvifDecoder.getInfo(buffer, buffer.remaining(), info)).isTrue();
    assertThat(info.depth).isEqualTo(8);
    assertThat(info.alphaPresent).isEqualTo(alpha);
    Bitmap bitmap = Bitmap.createBitmap(info.width, info.height, Config.ARGB_8888);
    buffer.rewind();
    assertThat(AvifDecoder.decode(buffer, buffer.remaining(), bitmap)).isTrue();
    return info;
  }

  private static void assertDecodesToArgb(
      String assetPath, int width, int height, boolean alpha) throws IOException {
    Info info = decodeArgb(assetPath, alpha);
    assertThat(info.width).isEqualTo(width);
    assertThat(info.height).isEqualTo(height);
  }

  private static Info assertOptionalAv2Decodes(String assetPath, boolean alpha)
      throws IOException {
    assumeTrue("Missing optional AV2 fixture " + assetPath, assetExists(assetPath));
    return decodeArgb(assetPath, alpha);
  }

  @Test
  public void av1Yuv420Limited() throws IOException {
    assertDecodesToArgb("avif/fox.profile0.8bpc.yuv420.avif", 1204, 800, false);
  }

  @Test
  public void av1Yuv400Limited() throws IOException {
    assertDecodesToArgb("avif/fox.profile0.8bpc.yuv420.monochrome.avif", 1204, 800, false);
  }

  @Test
  public void av1Yuv420LimitedAlpha() throws IOException {
    assertDecodesToArgb("avif/blue-and-magenta-crop.avif", 320, 280, true);
  }

  @Test
  public void av2Yuv400Full() throws IOException {
    assertOptionalAv2Decodes("avif/yuv400_full.avif", false);
  }

  @Test
  public void av2Yuv444Full() throws IOException {
    assertOptionalAv2Decodes("avif/yuv444_full.avif", false);
  }

  @Test
  public void av2Yuv444FullAlpha() throws IOException {
    assertOptionalAv2Decodes("avif/yuv444_full_alpha.avif", true);
  }

  @Test
  public void av2Yuv400FullGray565() throws IOException {
    assumeTrue(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q);
    assumeTrue(assetExists("avif/yuv400_full.avif"));
    ByteBuffer buffer = loadAsset("avif/yuv400_full.avif");
    Info info = new Info();
    assertThat(AvifDecoder.getInfo(buffer, buffer.remaining(), info)).isTrue();
    assertThat(info.alphaPresent).isFalse();
    buffer.rewind();
    HardwareBuffer hardwareBuffer =
        AvifHardwareDecoder.decodeToHardwareBuffer(
            buffer, buffer.remaining(), 0, 0, /* threads= */ 1, /* allowGray565= */ true);
    assertThat(hardwareBuffer).isNotNull();
    assertThat(hardwareBuffer.getFormat()).isEqualTo(HardwareBuffer.RGB_565);
    hardwareBuffer.close();
  }

  @Test
  public void versionStringContainsDav1dAndDav2d() {
    String version = AvifDecoder.versionString();
    assertThat(version).contains("dav1d");
    assertThat(version).contains("dav2d");
  }
}
