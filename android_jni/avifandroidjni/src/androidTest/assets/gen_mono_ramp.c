// SPDX-License-Identifier: BSD-2-Clause
// Helper: encode a 256x16 YUV400 ramp AVIF (limited or full range).
#include "avif/avif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <limited|full> <out.avif>\n", argv[0]);
    return 1;
  }
  const int limited = strcmp(argv[1], "limited") == 0;
  if (!limited && strcmp(argv[1], "full") != 0) {
    fprintf(stderr, "range must be limited or full\n");
    return 1;
  }

  const uint32_t width = 256;
  const uint32_t height = 16;
  avifImage* image = avifImageCreate(width, height, 8, AVIF_PIXEL_FORMAT_YUV400);
  if (!image) {
    fprintf(stderr, "avifImageCreate failed\n");
    return 1;
  }
  image->yuvRange = limited ? AVIF_RANGE_LIMITED : AVIF_RANGE_FULL;
  image->colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;

  if (avifImageAllocatePlanes(image, AVIF_PLANES_YUV) != AVIF_RESULT_OK) {
    fprintf(stderr, "avifImageAllocatePlanes failed\n");
    avifImageDestroy(image);
    return 1;
  }

  for (uint32_t y = 0; y < height; ++y) {
    uint8_t* row = image->yuvPlanes[AVIF_CHAN_Y] + y * image->yuvRowBytes[AVIF_CHAN_Y];
    for (uint32_t x = 0; x < width; ++x) {
      row[x] = (uint8_t)x;
    }
  }

  avifRWData raw = AVIF_DATA_EMPTY;
  avifEncoder* encoder = avifEncoderCreate();
  if (!encoder) {
    fprintf(stderr, "avifEncoderCreate failed\n");
    avifImageDestroy(image);
    return 1;
  }
  // Near-lossless so Gray565 golden tests see every code.
  encoder->quality = 100;
  encoder->qualityAlpha = 100;
  encoder->speed = 6;

  avifResult res = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
  if (res != AVIF_RESULT_OK) {
    fprintf(stderr, "avifEncoderAddImage: %s\n", avifResultToString(res));
    avifEncoderDestroy(encoder);
    avifImageDestroy(image);
    return 1;
  }
  res = avifEncoderFinish(encoder, &raw);
  if (res != AVIF_RESULT_OK) {
    fprintf(stderr, "avifEncoderFinish: %s\n", avifResultToString(res));
    avifEncoderDestroy(encoder);
    avifImageDestroy(image);
    return 1;
  }

  FILE* f = fopen(argv[2], "wb");
  if (!f) {
    perror("fopen");
    avifRWDataFree(&raw);
    avifEncoderDestroy(encoder);
    avifImageDestroy(image);
    return 1;
  }
  fwrite(raw.data, 1, raw.size, f);
  fclose(f);
  printf("Wrote %s (%zu bytes)\n", argv[2], raw.size);

  avifRWDataFree(&raw);
  avifEncoderDestroy(encoder);
  avifImageDestroy(image);
  return 0;
}
