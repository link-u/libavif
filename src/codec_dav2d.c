// Copyright 2026
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#if defined(_MSC_VER)
#pragma warning(disable : 4201) // nonstandard extension used: nameless struct/union
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc11-extensions" // C11 extension used: nameless struct/union
#endif
#include "dav2d/dav2d.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if !defined(AVIF_ENABLE_AV2)
#error "AVIF_CODEC_DAV2D requires AVIF_ENABLE_AV2"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef DAV2D_ERR
#define DAV2D_ERR(e) (-(e))
#endif
#ifndef DAV2D_MAX_THREADS
#define DAV2D_MAX_THREADS 256
#endif

struct avifCodecInternal
{
    Dav2dContext * dav2dContext;
    Dav2dPicture dav2dPicture;
    avifBool hasPicture;
    avifRange colorRange;
};

static void avifDav2dFreeCallback(const uint8_t * buf, void * cookie)
{
    (void)buf;
    (void)cookie;
}

static void avifDav2dLogCallback(void * cookie, const char * format, va_list ap)
{
    avifCodec * codec = (avifCodec *)cookie;
    vsnprintf(codec->diag->error, AVIF_DIAGNOSTICS_ERROR_BUFFER_SIZE, format, ap);
}

static void dav2dCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->hasPicture) {
        dav2d_picture_unref(&codec->internal->dav2dPicture);
    }
    if (codec->internal->dav2dContext) {
        dav2d_close(&codec->internal->dav2dContext);
    }
    avifFree(codec->internal);
}

static avifChromaSamplePosition dav2dChromaSamplePositionToAvif(uint8_t chr)
{
    if (chr == DAV2D_CHR_LEFT) {
        return AVIF_CHROMA_SAMPLE_POSITION_VERTICAL;
    }
    if (chr == DAV2D_CHR_TOPLEFT) {
        return AVIF_CHROMA_SAMPLE_POSITION_COLOCATED;
    }
    return AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN;
}

static avifBool dav2dCodecGetNextImage(struct avifCodec * codec,
                                       const avifDecodeSample * sample,
                                       avifBool alpha,
                                       avifBool * isLimitedRangeAlpha,
                                       avifImage * image)
{
    if (codec->internal->dav2dContext == NULL) {
        Dav2dSettings dav2dSettings;
        dav2d_default_settings(&dav2dSettings);
        dav2dSettings.max_frame_delay = 1;
        dav2dSettings.n_threads = AVIF_CLAMP(codec->maxThreads, 1, DAV2D_MAX_THREADS);
        dav2dSettings.frame_size_limit = (sizeof(size_t) < 8) ? AVIF_MIN(codec->imageSizeLimit, 8192 * 8192) : codec->imageSizeLimit;
        dav2dSettings.logger.cookie = codec;
        dav2dSettings.logger.callback = avifDav2dLogCallback;
        dav2dSettings.operating_point = codec->operatingPoint;
        dav2dSettings.all_layers = codec->allLayers;

        if (dav2d_open(&codec->internal->dav2dContext, &dav2dSettings) != 0) {
            return AVIF_FALSE;
        }
    }

    avifBool gotPicture = AVIF_FALSE;
    Dav2dPicture nextFrame;
    memset(&nextFrame, 0, sizeof(Dav2dPicture));

    Dav2dData dav2dData;
    if (dav2d_data_wrap(&dav2dData, sample->data.data, sample->data.size, avifDav2dFreeCallback, NULL) != 0) {
        return AVIF_FALSE;
    }

    int res;
    for (;;) {
        if (dav2dData.data) {
            res = dav2d_send_data(codec->internal->dav2dContext, &dav2dData);
            if ((res < 0) && (res != DAV2D_ERR(EAGAIN))) {
                dav2d_data_unref(&dav2dData);
                return AVIF_FALSE;
            }
        }

        res = dav2d_get_picture(codec->internal->dav2dContext, &nextFrame);
        if (res == DAV2D_ERR(EAGAIN)) {
            if (dav2dData.data) {
                continue;
            }
            return AVIF_FALSE;
        } else if (res < 0) {
            if (dav2dData.data) {
                dav2d_data_unref(&dav2dData);
            }
            return AVIF_FALSE;
        } else {
            const uint8_t mlayerId = nextFrame.frame_hdr ? nextFrame.frame_hdr->mlayer_id : 0;
            if ((sample->spatialID != AVIF_SPATIAL_ID_UNSET) && (sample->spatialID != mlayerId)) {
                dav2d_picture_unref(&nextFrame);
            } else {
                gotPicture = AVIF_TRUE;
                break;
            }
        }
    }
    if (dav2dData.data) {
        dav2d_data_unref(&dav2dData);
    }

    Dav2dPicture bufferedFrame;
    memset(&bufferedFrame, 0, sizeof(Dav2dPicture));
    do {
        res = dav2d_get_picture(codec->internal->dav2dContext, &bufferedFrame);
        if (res < 0) {
            if (res != DAV2D_ERR(EAGAIN)
#if defined(DAV2D_EOF)
                && res != DAV2D_EOF
#endif
            ) {
                if (gotPicture) {
                    dav2d_picture_unref(&nextFrame);
                }
                return AVIF_FALSE;
            }
        } else {
            dav2d_picture_unref(&bufferedFrame);
        }
    } while (res == 0);

    if (gotPicture) {
        dav2d_picture_unref(&codec->internal->dav2dPicture);
        codec->internal->dav2dPicture = nextFrame;
        codec->internal->colorRange = AVIF_RANGE_LIMITED;
        if (codec->internal->dav2dPicture.ci && codec->internal->dav2dPicture.ci->color_description_present) {
            codec->internal->colorRange = codec->internal->dav2dPicture.ci->color.range ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED;
        }
        codec->internal->hasPicture = AVIF_TRUE;
    } else {
        if (alpha && codec->internal->hasPicture) {
            // Special case: reuse last alpha frame
        } else {
            return AVIF_FALSE;
        }
    }

    Dav2dPicture * dav2dImage = &codec->internal->dav2dPicture;
    avifBool isColor = !alpha;
    if (isColor) {
        avifPixelFormat yuvFormat = AVIF_PIXEL_FORMAT_NONE;
        switch (dav2dImage->p.layout) {
            case DAV2D_PIXEL_LAYOUT_I400:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
                break;
            case DAV2D_PIXEL_LAYOUT_I420:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
                break;
            case DAV2D_PIXEL_LAYOUT_I422:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV422;
                break;
            case DAV2D_PIXEL_LAYOUT_I444:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
                break;
        }

        image->width = (uint32_t)dav2dImage->p.w;
        image->height = (uint32_t)dav2dImage->p.h;
        image->depth = (uint32_t)dav2dImage->p.bpc;

        image->yuvFormat = yuvFormat;
        image->yuvRange = codec->internal->colorRange;

        if (dav2dImage->ci) {
            if (dav2dImage->ci->chroma_sample_position_present) {
                image->yuvChromaSamplePosition = dav2dChromaSamplePositionToAvif(dav2dImage->ci->chr[0]);
            }
            if (dav2dImage->ci->color_description_present) {
                image->colorPrimaries = (avifColorPrimaries)dav2dImage->ci->color.pri;
                image->transferCharacteristics = (avifTransferCharacteristics)dav2dImage->ci->color.trc;
                image->matrixCoefficients = (avifMatrixCoefficients)dav2dImage->ci->color.mtrx;
            }
        }

        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        int yuvPlaneCount = (yuvFormat == AVIF_PIXEL_FORMAT_YUV400) ? 1 : 3;
        for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
            image->yuvPlanes[yuvPlane] = dav2dImage->data[yuvPlane];
            image->yuvRowBytes[yuvPlane] = (uint32_t)dav2dImage->stride[(yuvPlane == AVIF_CHAN_Y) ? 0 : 1];
        }
        image->imageOwnsYUVPlanes = AVIF_FALSE;
    } else {
        image->width = (uint32_t)dav2dImage->p.w;
        image->height = (uint32_t)dav2dImage->p.h;
        image->depth = (uint32_t)dav2dImage->p.bpc;

        avifImageFreePlanes(image, AVIF_PLANES_A);
        image->alphaPlane = dav2dImage->data[0];
        image->alphaRowBytes = (uint32_t)dav2dImage->stride[0];
        *isLimitedRangeAlpha = (codec->internal->colorRange == AVIF_RANGE_LIMITED);
        image->imageOwnsAlphaPlane = AVIF_FALSE;
    }
    return AVIF_TRUE;
}

const char * avifCodecVersionDav2d(void)
{
    return dav2d_version();
}

avifCodec * avifCodecCreateDav2d(void)
{
    avifCodec * codec = (avifCodec *)avifCalloc(1, sizeof(avifCodec));
    if (codec == NULL) {
        return NULL;
    }
    codec->getNextImage = dav2dCodecGetNextImage;
    codec->destroyInternal = dav2dCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifCalloc(1, sizeof(struct avifCodecInternal));
    if (codec->internal == NULL) {
        avifFree(codec);
        return NULL;
    }
    return codec;
}
