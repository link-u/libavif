// Copyright 2021 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include <limits.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes" // "this function declaration is not a prototype"
// The newline at the end of libyuv/version.h was accidentally deleted in version 1792 and restored
// in version 1813:
// https://chromium-review.googlesource.com/c/libyuv/libyuv/+/3183182
// https://chromium-review.googlesource.com/c/libyuv/libyuv/+/3527834
#pragma clang diagnostic ignored "-Wnewline-eof" // "no newline at end of file"
#endif
#include <libyuv.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// Match the default bilinear filtering used when Compose scales images while
// avoiding the higher cost of kFilterBox.
#define AVIF_LIBYUV_FILTER_MODE kFilterBilinear

avifResult avifImageScaleWithLimit(avifImage * image,
                                   uint32_t dstWidth,
                                   uint32_t dstHeight,
                                   uint32_t imageSizeLimit,
                                   uint32_t imageDimensionLimit,
                                   avifDiagnostics * diag)
{
    if ((image->width == dstWidth) && (image->height == dstHeight)) {
        // Nothing to do
        return AVIF_RESULT_OK;
    }

    if ((dstWidth == 0) || (dstHeight == 0)) {
        avifDiagnosticsPrintf(diag, "avifImageScaleWithLimit requested invalid dst dimensions [%ux%u]", dstWidth, dstHeight);
        return AVIF_RESULT_INVALID_ARGUMENT;
    }
    if (avifDimensionsTooLarge(dstWidth, dstHeight, imageSizeLimit, imageDimensionLimit)) {
        avifDiagnosticsPrintf(diag, "avifImageScaleWithLimit requested dst dimensions that are too large [%ux%u]", dstWidth, dstHeight);
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    // Android slim: 8-bit planes only (no ScalePlane_12 / ScalePlane_16).
    if (image->depth != 8) {
        avifDiagnosticsPrintf(diag, "avifImageScaleWithLimit supports 8-bit images only (depth %u)", image->depth);
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    uint8_t * srcYUVPlanes[AVIF_PLANE_COUNT_YUV];
    uint32_t srcYUVRowBytes[AVIF_PLANE_COUNT_YUV];
    for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
        srcYUVPlanes[i] = image->yuvPlanes[i];
        image->yuvPlanes[i] = NULL;
        srcYUVRowBytes[i] = image->yuvRowBytes[i];
        image->yuvRowBytes[i] = 0;
    }
    const avifBool srcImageOwnsYUVPlanes = image->imageOwnsYUVPlanes;
    image->imageOwnsYUVPlanes = AVIF_FALSE;

    uint8_t * srcAlphaPlane = image->alphaPlane;
    image->alphaPlane = NULL;
    uint32_t srcAlphaRowBytes = image->alphaRowBytes;
    image->alphaRowBytes = 0;
    const avifBool srcImageOwnsAlphaPlane = image->imageOwnsAlphaPlane;
    image->imageOwnsAlphaPlane = AVIF_FALSE;

    const uint32_t srcWidth = image->width;
    const uint32_t srcHeight = image->height;
    const uint32_t srcUVWidth = avifImagePlaneWidth(image, AVIF_CHAN_U);
    const uint32_t srcUVHeight = avifImagePlaneHeight(image, AVIF_CHAN_U);
    image->width = dstWidth;
    image->height = dstHeight;

    avifResult result = AVIF_RESULT_OK;
    if (srcYUVPlanes[0] || srcAlphaPlane) {
        // A simple conservative check to avoid integer overflows in libyuv's ScalePlane().
        if (srcWidth > 16384) {
            avifDiagnosticsPrintf(diag, "avifImageScaleWithLimit requested invalid width scale for libyuv [%u -> %u]", srcWidth, dstWidth);
            result = AVIF_RESULT_NOT_IMPLEMENTED;
            goto cleanup;
        }
        if (srcHeight > 16384) {
            avifDiagnosticsPrintf(diag, "avifImageScaleWithLimit requested invalid height scale for libyuv [%u -> %u]", srcHeight, dstHeight);
            result = AVIF_RESULT_NOT_IMPLEMENTED;
            goto cleanup;
        }
    }

    if (srcYUVPlanes[0]) {
        const avifResult allocationResult = avifImageAllocatePlanes(image, AVIF_PLANES_YUV);
        if (allocationResult != AVIF_RESULT_OK) {
            avifDiagnosticsPrintf(diag, "Allocation of YUV planes failed: %s", avifResultToString(allocationResult));
            result = AVIF_RESULT_OUT_OF_MEMORY;
            goto cleanup;
        }

        for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
            if (!srcYUVPlanes[i]) {
                continue;
            }

            const uint32_t srcW = (i == AVIF_CHAN_Y) ? srcWidth : srcUVWidth;
            const uint32_t srcH = (i == AVIF_CHAN_Y) ? srcHeight : srcUVHeight;
            const uint32_t dstW = avifImagePlaneWidth(image, i);
            const uint32_t dstH = avifImagePlaneHeight(image, i);
            uint8_t * const srcPlane = srcYUVPlanes[i];
            const uint32_t srcStride = srcYUVRowBytes[i];
            uint8_t * const dstPlane = image->yuvPlanes[i];
            const uint32_t dstStride = image->yuvRowBytes[i];
#if LIBYUV_VERSION >= 1880
            const int failure = ScalePlane(srcPlane, srcStride, srcW, srcH, dstPlane, dstStride, dstW, dstH, AVIF_LIBYUV_FILTER_MODE);
            if (failure) {
                avifDiagnosticsPrintf(diag, "ScalePlane() failed (%d)", failure);
                result = (failure == 1) ? AVIF_RESULT_OUT_OF_MEMORY : AVIF_RESULT_UNKNOWN_ERROR;
                goto cleanup;
            }
#else
            ScalePlane(srcPlane, srcStride, srcW, srcH, dstPlane, dstStride, dstW, dstH, AVIF_LIBYUV_FILTER_MODE);
#endif
        }
    }

    if (srcAlphaPlane) {
        const avifResult allocationResult = avifImageAllocatePlanes(image, AVIF_PLANES_A);
        if (allocationResult != AVIF_RESULT_OK) {
            avifDiagnosticsPrintf(diag, "Allocation of alpha plane failed: %s", avifResultToString(allocationResult));
            result = AVIF_RESULT_OUT_OF_MEMORY;
            goto cleanup;
        }

        uint8_t * const srcPlane = srcAlphaPlane;
        const uint32_t srcStride = srcAlphaRowBytes;
        uint8_t * const dstPlane = image->alphaPlane;
        const uint32_t dstStride = image->alphaRowBytes;
#if LIBYUV_VERSION >= 1880
        const int failure =
            ScalePlane(srcPlane, srcStride, srcWidth, srcHeight, dstPlane, dstStride, dstWidth, dstHeight, AVIF_LIBYUV_FILTER_MODE);
        if (failure) {
            avifDiagnosticsPrintf(diag, "ScalePlane() failed (%d)", failure);
            result = (failure == 1) ? AVIF_RESULT_OUT_OF_MEMORY : AVIF_RESULT_UNKNOWN_ERROR;
            goto cleanup;
        }
#else
        ScalePlane(srcPlane, srcStride, srcWidth, srcHeight, dstPlane, dstStride, dstWidth, dstHeight, AVIF_LIBYUV_FILTER_MODE);
#endif
    }

cleanup:
    if (srcYUVPlanes[0] && srcImageOwnsYUVPlanes) {
        for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
            avifFree(srcYUVPlanes[i]);
        }
    }
    if (srcAlphaPlane && srcImageOwnsAlphaPlane) {
        avifFree(srcAlphaPlane);
    }
    return result;
}

avifResult avifImageScale(avifImage * image, uint32_t dstWidth, uint32_t dstHeight, avifDiagnostics * diag)
{
    avifDiagnosticsClearError(diag);
    return avifImageScaleWithLimit(image, dstWidth, dstHeight, AVIF_DEFAULT_IMAGE_SIZE_LIMIT, AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT, diag);
}
