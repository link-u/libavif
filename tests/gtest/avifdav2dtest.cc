// Copyright 2026
// SPDX-License-Identifier: BSD-2-Clause

#include <fstream>
#include <iostream>
#include <string>
#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using testing::Combine;
using testing::Values;

namespace avif {
namespace {

const char* data_path = nullptr;

bool Dav2dAvailable() {
  return avifCodecName(AVIF_CODEC_CHOICE_DAV2D, AVIF_CODEC_FLAG_CAN_DECODE) !=
         nullptr;
}

bool AvmEncoderAvailable() {
  return avifCodecName(AVIF_CODEC_CHOICE_AVM, AVIF_CODEC_FLAG_CAN_ENCODE) !=
         nullptr;
}

bool FileExists(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return file.good();
}

class Dav2dTest
    : public testing::TestWithParam<
          std::tuple</*width=*/int, /*height=*/int, avifPixelFormat,
                     /*alpha=*/bool>> {};

TEST_P(Dav2dTest, EncodeDecodeGlenwoodMatrix) {
  if (!Dav2dAvailable()) {
    GTEST_SKIP() << "dav2d unavailable, skip test.";
  }
  if (!AvmEncoderAvailable()) {
    GTEST_SKIP() << "AVM encoder unavailable; cannot synthesize av02 samples.";
  }

  const int width = std::get<0>(GetParam());
  const int height = std::get<1>(GetParam());
  const avifPixelFormat format = std::get<2>(GetParam());
  const bool alpha = std::get<3>(GetParam());
  ASSERT_FALSE(format == AVIF_PIXEL_FORMAT_YUV400 && alpha)
      << "Glenwood does not produce YUV400+alpha";

  ImagePtr image = testutil::CreateImage(width, height, /*depth=*/8, format,
                                         alpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV,
                                         AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AVM;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  // AUTO should remap av02 tiles to dav2d.
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);
  EXPECT_EQ(decoded->depth, 8);
  EXPECT_EQ(decoded->yuvFormat, format);
  EXPECT_EQ(decoded->yuvRange, AVIF_RANGE_FULL);
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 36.0);

  decoder->codecChoice = AVIF_CODEC_CHOICE_DAV1D;
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            avifCodecName(AVIF_CODEC_CHOICE_DAV1D, AVIF_CODEC_FLAG_CAN_DECODE)
                ? AVIF_RESULT_DECODE_COLOR_FAILED
                : AVIF_RESULT_NO_CODEC_AVAILABLE);
}

INSTANTIATE_TEST_SUITE_P(Glenwood, Dav2dTest,
                         Combine(/*width=*/Values(12), /*height=*/Values(34),
                                 Values(AVIF_PIXEL_FORMAT_YUV400,
                                        AVIF_PIXEL_FORMAT_YUV444),
                                 /*alpha=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(GlenwoodAlpha444, Dav2dTest,
                         Combine(/*width=*/Values(12), /*height=*/Values(34),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 /*alpha=*/Values(true)));

TEST(Dav2dTest, DecodeOptionalFixtures) {
  if (!Dav2dAvailable()) {
    GTEST_SKIP() << "dav2d unavailable, skip test.";
  }
  ASSERT_NE(data_path, nullptr);
  const char* files[] = {"yuv400_full.avif", "yuv444_full.avif",
                         "yuv444_full_alpha.avif"};
  int decoded_count = 0;
  for (const char* file_name : files) {
    const std::string path = std::string(data_path) + file_name;
    if (!FileExists(path)) {
      continue;
    }
    ImagePtr decoded = testutil::DecodeFile(path);
    ASSERT_NE(decoded, nullptr) << "Failed to decode " << path;
    EXPECT_EQ(decoded->depth, 8);
    decoded_count++;
  }
  if (decoded_count == 0 && !AvmEncoderAvailable()) {
    GTEST_SKIP() << "No AV2 fixtures in tests/data and no AVM encoder.";
  }
}

TEST(Dav2dTest, Av1StillWorksWhenDav2dIsEnabled) {
  if (!testutil::Av1EncoderAvailable() || !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 codec unavailable, skip test.";
  }

  ImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/64, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL,
                            AVIF_RANGE_LIMITED);
  ASSERT_NE(image, nullptr);
  image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);

  decoder->codecChoice = AVIF_CODEC_CHOICE_DAV2D;
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_DECODE_COLOR_FAILED);
}

TEST(Dav2dTest, ReportsDav2dCodec) {
  ASSERT_NE(avifCodecName(AVIF_CODEC_CHOICE_DAV2D, AVIF_CODEC_FLAG_CAN_DECODE),
            nullptr);
  EXPECT_STREQ(avifCodecName(AVIF_CODEC_CHOICE_DAV2D, AVIF_CODEC_FLAG_CAN_DECODE),
               "dav2d");
  EXPECT_EQ(avifCodecName(AVIF_CODEC_CHOICE_DAV2D, AVIF_CODEC_FLAG_CAN_ENCODE),
            nullptr);
}

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
