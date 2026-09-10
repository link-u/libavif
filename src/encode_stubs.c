// Copyright 2026. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

// Decode-only build stubs for APIs excluded from the Android slim build
// (src/write.c and src/sampletransform.c are not compiled).

#include "avif/internal.h"

avifEncoder * avifEncoderCreate(void)
{
    return NULL;
}

void avifEncoderDestroy(avifEncoder * encoder)
{
    (void)encoder;
}

avifResult avifEncoderWrite(avifEncoder * encoder, const avifImage * image, avifRWData * output)
{
    (void)encoder;
    (void)image;
    (void)output;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifEncoderAddImage(avifEncoder * encoder, const avifImage * image, uint64_t durationInTimescales, avifAddImageFlags addImageFlags)
{
    (void)encoder;
    (void)image;
    (void)durationInTimescales;
    (void)addImageFlags;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifEncoderAddImageGrid(avifEncoder * encoder,
                                   uint32_t gridCols,
                                   uint32_t gridRows,
                                   const avifImage * const * cellImages,
                                   avifAddImageFlags addImageFlags)
{
    (void)encoder;
    (void)gridCols;
    (void)gridRows;
    (void)cellImages;
    (void)addImageFlags;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifEncoderFinish(avifEncoder * encoder, avifRWData * output)
{
    (void)encoder;
    (void)output;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifEncoderSetCodecSpecificOption(avifEncoder * encoder, const char * key, const char * value)
{
    (void)encoder;
    (void)key;
    (void)value;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

size_t avifEncoderGetGainMapSizeBytes(avifEncoder * encoder)
{
    (void)encoder;
    return 0;
}

//------------------------------------------------------------------------------
// Sample Transform stubs (src/sampletransform.c excluded)

avifBool avifSampleTransformExpressionIsValid(const avifSampleTransformExpression * tokens, uint32_t numInputImageItems)
{
    (void)tokens;
    (void)numInputImageItems;
    return AVIF_FALSE;
}

avifBool avifSampleTransformExpressionIsEquivalentTo(const avifSampleTransformExpression * a, const avifSampleTransformExpression * b)
{
    (void)a;
    (void)b;
    return AVIF_FALSE;
}

avifResult avifSampleTransformRecipeToExpression(avifSampleTransformRecipe recipe, avifSampleTransformExpression * expression)
{
    (void)recipe;
    (void)expression;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifSampleTransformExpressionToRecipe(const avifSampleTransformExpression * expression, avifSampleTransformRecipe * recipe)
{
    (void)expression;
    (void)recipe;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifImageApplyExpression(avifImage * dstImage,
                                    avifSampleTransformBitDepth bitDepth,
                                    const avifSampleTransformExpression * expression,
                                    uint8_t numInputImageItems,
                                    const avifImage * inputImageItems[],
                                    avifPlanesFlags planes)
{
    (void)dstImage;
    (void)bitDepth;
    (void)expression;
    (void)numInputImageItems;
    (void)inputImageItems;
    (void)planes;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifImageApplyOperations(avifImage * dstImage,
                                    avifSampleTransformBitDepth bitDepth,
                                    uint32_t numTokens,
                                    const avifSampleTransformToken tokens[],
                                    uint8_t numInputImageItems,
                                    const avifImage * inputImageItems[],
                                    avifPlanesFlags planes)
{
    (void)dstImage;
    (void)bitDepth;
    (void)numTokens;
    (void)tokens;
    (void)numInputImageItems;
    (void)inputImageItems;
    (void)planes;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
