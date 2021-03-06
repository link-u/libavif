// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_AVIF_H
#define AVIF_AVIF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Constants

#define AVIF_VERSION_MAJOR 0
#define AVIF_VERSION_MINOR 5
#define AVIF_VERSION_PATCH 6
#define AVIF_VERSION (AVIF_VERSION_MAJOR * 10000) + (AVIF_VERSION_MINOR * 100) + AVIF_VERSION_PATCH

typedef int avifBool;
#define AVIF_TRUE 1
#define AVIF_FALSE 0

#define AVIF_QUANTIZER_LOSSLESS 0
#define AVIF_QUANTIZER_BEST_QUALITY 0
#define AVIF_QUANTIZER_WORST_QUALITY 63

#define AVIF_PLANE_COUNT_RGB 3
#define AVIF_PLANE_COUNT_YUV 3

#define AVIF_SPEED_DEFAULT -1
#define AVIF_SPEED_SLOWEST 0
#define AVIF_SPEED_FASTEST 10

enum avifPlanesFlags
{
    AVIF_PLANES_RGB = (1 << 0),
    AVIF_PLANES_YUV = (1 << 1),
    AVIF_PLANES_A = (1 << 2),

    AVIF_PLANES_ALL = 0xff
};

enum avifChannelIndex
{
    // rgbPlanes
    AVIF_CHAN_R = 0,
    AVIF_CHAN_G = 1,
    AVIF_CHAN_B = 2,

    // yuvPlanes - These are always correct, even if UV is flipped when encoded (YV12)
    AVIF_CHAN_Y = 0,
    AVIF_CHAN_U = 1,
    AVIF_CHAN_V = 2
};

// ---------------------------------------------------------------------------
// Version

const char * avifVersion(void);
void avifCodecVersions(char outBuffer[256]);

// ---------------------------------------------------------------------------
// Memory management

void * avifAlloc(size_t size);
void avifFree(void * p);

// ---------------------------------------------------------------------------
// avifResult

typedef enum avifResult
{
    AVIF_RESULT_OK = 0,
    AVIF_RESULT_UNKNOWN_ERROR,
    AVIF_RESULT_INVALID_FTYP,
    AVIF_RESULT_NO_CONTENT,
    AVIF_RESULT_NO_YUV_FORMAT_SELECTED,
    AVIF_RESULT_REFORMAT_FAILED,
    AVIF_RESULT_UNSUPPORTED_DEPTH,
    AVIF_RESULT_ENCODE_COLOR_FAILED,
    AVIF_RESULT_ENCODE_ALPHA_FAILED,
    AVIF_RESULT_BMFF_PARSE_FAILED,
    AVIF_RESULT_NO_AV1_ITEMS_FOUND,
    AVIF_RESULT_DECODE_COLOR_FAILED,
    AVIF_RESULT_DECODE_ALPHA_FAILED,
    AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH,
    AVIF_RESULT_ISPE_SIZE_MISMATCH,
    AVIF_RESULT_NO_CODEC_AVAILABLE,
    AVIF_RESULT_NO_IMAGES_REMAINING,
    AVIF_RESULT_INVALID_EXIF_PAYLOAD
} avifResult;

const char * avifResultToString(avifResult result);

// ---------------------------------------------------------------------------
// avifROData/avifRWData: Generic raw memory storage

typedef struct avifROData
{
    const uint8_t * data;
    size_t size;
} avifROData;

// Note: Use avifRWDataFree() if any avif*() function populates one of these.

typedef struct avifRWData
{
    uint8_t * data;
    size_t size;
} avifRWData;

// clang-format off
// Initialize avifROData/avifRWData on the stack with this
#define AVIF_DATA_EMPTY { NULL, 0 }
// clang-format on

void avifRWDataRealloc(avifRWData * raw, size_t newSize);
void avifRWDataSet(avifRWData * raw, const uint8_t * data, size_t len);
void avifRWDataFree(avifRWData * raw);

// ---------------------------------------------------------------------------
// avifPixelFormat

typedef enum avifPixelFormat
{
    // No pixels are present
    AVIF_PIXEL_FORMAT_NONE = 0,

    AVIF_PIXEL_FORMAT_YUV444,
    AVIF_PIXEL_FORMAT_YUV422,
    AVIF_PIXEL_FORMAT_YUV420,
    AVIF_PIXEL_FORMAT_YV12
} avifPixelFormat;
const char * avifPixelFormatToString(avifPixelFormat format);

typedef struct avifPixelFormatInfo
{
    int chromaShiftX;
    int chromaShiftY;
    int aomIndexU; // maps U plane to AOM-side plane index
    int aomIndexV; // maps V plane to AOM-side plane index
} avifPixelFormatInfo;

void avifGetPixelFormatInfo(avifPixelFormat format, avifPixelFormatInfo * info);

// ---------------------------------------------------------------------------
// avifNclxColorProfile

typedef enum avifNclxColourPrimaries
{
    // This is actually reserved, but libavif uses it as a sentinel value.
    AVIF_NCLX_COLOUR_PRIMARIES_UNKNOWN = 0,

    AVIF_NCLX_COLOUR_PRIMARIES_BT709 = 1,
    AVIF_NCLX_COLOUR_PRIMARIES_BT1361_0 = 1,
    AVIF_NCLX_COLOUR_PRIMARIES_IEC61966_2_1 = 1,
    AVIF_NCLX_COLOUR_PRIMARIES_SRGB = 1,
    AVIF_NCLX_COLOUR_PRIMARIES_SYCC = 1,
    AVIF_NCLX_COLOUR_PRIMARIES_IEC61966_2_4 = 1,
    AVIF_NCLX_COLOUR_PRIMARIES_UNSPECIFIED = 2,
    AVIF_NCLX_COLOUR_PRIMARIES_BT470_6M = 4,
    AVIF_NCLX_COLOUR_PRIMARIES_BT601_7_625 = 5,
    AVIF_NCLX_COLOUR_PRIMARIES_BT470_6G = 5,
    AVIF_NCLX_COLOUR_PRIMARIES_BT601_7_525 = 6,
    AVIF_NCLX_COLOUR_PRIMARIES_BT1358 = 6,
    AVIF_NCLX_COLOUR_PRIMARIES_ST240 = 7,
    AVIF_NCLX_COLOUR_PRIMARIES_GENERIC_FILM = 8,
    AVIF_NCLX_COLOUR_PRIMARIES_BT2020 = 9,
    AVIF_NCLX_COLOUR_PRIMARIES_BT2100 = 9,
    AVIF_NCLX_COLOUR_PRIMARIES_XYZ = 10,
    AVIF_NCLX_COLOUR_PRIMARIES_ST428 = 10,
    AVIF_NCLX_COLOUR_PRIMARIES_RP431_2 = 11,
    AVIF_NCLX_COLOUR_PRIMARIES_EG432_1 = 12,
    AVIF_NCLX_COLOUR_PRIMARIES_P3 = 12,
    AVIF_NCLX_COLOUR_PRIMARIES_EBU3213E = 22
} avifNclxColourPrimaries;

// outPrimaries: rX, rY, gX, gY, bX, bY, wX, wY
void avifNclxColourPrimariesGetValues(avifNclxColourPrimaries ancp, float outPrimaries[8]);
avifNclxColourPrimaries avifNclxColourPrimariesFind(float inPrimaries[8], const char ** outName);

typedef enum avifNclxTransferCharacteristics
{
    // This is actually reserved, but libavif uses it as a sentinel value.
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_UNKNOWN = 0,

    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT709 = 1,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT1361 = 1,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_UNSPECIFIED = 2,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_GAMMA22 = 4,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_GAMMA28 = 5,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT601 = 6,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_ST240 = 7,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_LINEAR = 8,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_LOG_100_1 = 9,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_LOG_100_SQRT = 10,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_IEC61966 = 11,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT1361_EXTENDED = 12,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_61966_2_1 = 13,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_SRGB = 13,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_SYCC = 13,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT2020_10BIT = 14,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT2020_12BIT = 15,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_ST2084 = 16,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT2100_PQ = 16,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_ST428 = 17,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_STD_B67 = 18,
    AVIF_NCLX_TRANSFER_CHARACTERISTICS_BT2100_HLG = 18
} avifNclxTransferCharacteristics;

typedef enum avifNclxMatrixCoefficients
{
    AVIF_NCLX_MATRIX_COEFFICIENTS_IDENTITY = 0,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT709 = 1,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT1361_0 = 1,
    AVIF_NCLX_MATRIX_COEFFICIENTS_SRGB = 1,
    AVIF_NCLX_MATRIX_COEFFICIENTS_SYCC = 1,
    AVIF_NCLX_MATRIX_COEFFICIENTS_UNSPECIFIED = 2,
    AVIF_NCLX_MATRIX_COEFFICIENTS_USFC_73682 = 4,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT470_6B = 5,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT601_7_625 = 5,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT601_7_525 = 6,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT1700_NTSC = 6,
    AVIF_NCLX_MATRIX_COEFFICIENTS_ST170 = 6,
    AVIF_NCLX_MATRIX_COEFFICIENTS_ST240 = 7,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT2020_NCL = 9,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT2100 = 9,
    AVIF_NCLX_MATRIX_COEFFICIENTS_BT2020_CL = 10,
    AVIF_NCLX_MATRIX_COEFFICIENTS_ST2085 = 11,
    AVIF_NCLX_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL = 12,
    AVIF_NCLX_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL = 13,
    AVIF_NCLX_MATRIX_COEFFICIENTS_ICTCP = 14
} avifNclxMatrixCoefficients;

// for fullRangeFlag
typedef enum avifNclxRangeFlag
{
    AVIF_NCLX_LIMITED_RANGE = 0,
    AVIF_NCLX_FULL_RANGE = 0x80
} avifNclxRangeFlag;

typedef struct avifNclxColorProfile
{
    uint16_t colourPrimaries;
    uint16_t transferCharacteristics;
    uint16_t matrixCoefficients;
    uint8_t fullRangeFlag;
} avifNclxColorProfile;

// ---------------------------------------------------------------------------
// avifRange

typedef enum avifRange
{
    AVIF_RANGE_LIMITED = 0,
    AVIF_RANGE_FULL,
} avifRange;

// ---------------------------------------------------------------------------
// avifProfileFormat

typedef enum avifProfileFormat
{
    // No color profile present
    AVIF_PROFILE_FORMAT_NONE = 0,

    // icc represents an ICC profile chunk (inside a colr box)
    AVIF_PROFILE_FORMAT_ICC,

    // nclx represents a valid nclx colr box
    AVIF_PROFILE_FORMAT_NCLX
} avifProfileFormat;

// ---------------------------------------------------------------------------
// avifImage

typedef struct avifImage
{
    // Image information
    uint32_t width;
    uint32_t height;
    uint32_t depth; // all planes (RGB/YUV/A) must share this depth; if depth>8, all planes are uint16_t internally

    uint8_t * rgbPlanes[AVIF_PLANE_COUNT_RGB];
    uint32_t rgbRowBytes[AVIF_PLANE_COUNT_RGB];

    avifPixelFormat yuvFormat;
    avifRange yuvRange;
    uint8_t * yuvPlanes[AVIF_PLANE_COUNT_YUV];
    uint32_t yuvRowBytes[AVIF_PLANE_COUNT_YUV];
    avifBool decoderOwnsYUVPlanes;

    uint8_t * alphaPlane;
    uint32_t alphaRowBytes;
    avifBool decoderOwnsAlphaPlane;

    // Profile information
    avifProfileFormat profileFormat;
    avifRWData icc;
    avifNclxColorProfile nclx;

    // Metadata - set with avifImageSetMetadata*() before write, check .size>0 for existence after read
    avifRWData exif;
    avifRWData xmp;
} avifImage;

avifImage * avifImageCreate(int width, int height, int depth, avifPixelFormat yuvFormat);
avifImage * avifImageCreateEmpty(void);                         // helper for making an image to decode into
void avifImageCopy(avifImage * dstImage, avifImage * srcImage); // deep copy
void avifImageDestroy(avifImage * image);

void avifImageSetProfileNone(avifImage * image);
void avifImageSetProfileICC(avifImage * image, const uint8_t * icc, size_t iccSize);
void avifImageSetProfileNCLX(avifImage * image, avifNclxColorProfile * nclx);

// Warning: If the Exif payload is set and invalid, avifEncoderWrite() may return AVIF_RESULT_INVALID_EXIF_PAYLOAD
void avifImageSetMetadataExif(avifImage * image, const uint8_t * exif, size_t exifSize);
void avifImageSetMetadataXMP(avifImage * image, const uint8_t * xmp, size_t xmpSize);

void avifImageAllocatePlanes(avifImage * image, uint32_t planes); // Ignores any pre-existing planes
void avifImageFreePlanes(avifImage * image, uint32_t planes);     // Ignores already-freed planes

// Optional YUV<->RGB support
avifResult avifImageRGBToYUV(avifImage * image);
avifResult avifImageYUVToRGB(avifImage * image);

// ---------------------------------------------------------------------------
// YUV Utils

int avifFullToLimitedY(int depth, int v);
int avifFullToLimitedUV(int depth, int v);
int avifLimitedToFullY(int depth, int v);
int avifLimitedToFullUV(int depth, int v);

typedef struct avifReformatState
{
    // YUV coefficients
    float kr;
    float kg;
    float kb;

    avifPixelFormatInfo formatInfo;
    avifBool usesU16;
} avifReformatState;
avifBool avifPrepareReformatState(avifImage * image, avifReformatState * state);

// ---------------------------------------------------------------------------
// Codec selection

typedef enum avifCodecChoice
{
    AVIF_CODEC_CHOICE_AUTO = 0,
    AVIF_CODEC_CHOICE_AOM,
    AVIF_CODEC_CHOICE_DAV1D,   // Decode only
    AVIF_CODEC_CHOICE_LIBGAV1, // Decode only
    AVIF_CODEC_CHOICE_RAV1E    // Encode only
} avifCodecChoice;

typedef enum avifCodecFlags
{
    AVIF_CODEC_FLAG_CAN_DECODE = (1 << 0),
    AVIF_CODEC_FLAG_CAN_ENCODE = (1 << 1)
} avifCodecFlags;

// If this returns NULL, the codec choice/flag combination is unavailable
const char * avifCodecName(avifCodecChoice choice, uint32_t requiredFlags);
avifCodecChoice avifCodecChoiceFromName(const char * name);

// ---------------------------------------------------------------------------
// avifDecoder

// Useful stats related to a read/write
typedef struct avifIOStats
{
    size_t colorOBUSize;
    size_t alphaOBUSize;
} avifIOStats;

struct avifData;

typedef enum avifDecoderSource
{
    // If a moov box is present in the .avif(s), use the tracks in it, otherwise decode the primary item.
    AVIF_DECODER_SOURCE_AUTO = 0,

    // Use the primary item and the aux (alpha) item in the avif(s).
    // This is where single-image avifs store their image.
    AVIF_DECODER_SOURCE_PRIMARY_ITEM,

    // Use the chunks inside primary/aux tracks in the moov block.
    // This is where avifs image sequences store their images.
    AVIF_DECODER_SOURCE_TRACKS,

    // Decode the thumbnail item. Currently unimplemented.
    // AVIF_DECODER_SOURCE_THUMBNAIL_ITEM
} avifDecoderSource;

// Information about the timing of a single image in an image sequence
typedef struct avifImageTiming
{
    uint64_t timescale;            // timescale of the media (Hz)
    double pts;                    // presentation timestamp in seconds (ptsInTimescales / timescale)
    uint64_t ptsInTimescales;      // presentation timestamp in "timescales"
    double duration;               // in seconds (durationInTimescales / timescale)
    uint64_t durationInTimescales; // duration in "timescales"
} avifImageTiming;

typedef struct avifDecoder
{
    // Defaults to AVIF_CODEC_CHOICE_AUTO: Preference determined by order in availableCodecs table (avif.c)
    avifCodecChoice codecChoice;

    // avifs can have multiple sets of images in them. This specifies which to decode.
    // Set this via avifDecoderSetSource().
    avifDecoderSource requestedSource;

    // The current decoded image, owned by the decoder. Can be NULL if the decoder hasn't run or has run
    // out of images. The YUV and A contents of this image are likely owned by the decoder, so be
    // sure to copy any data inside of this image before advancing to the next image or reusing the
    // decoder. It is legal to call avifImageYUVToRGB() on this in between calls to avifDecoderNextImage(),
    // but use avifImageCopy() if you want to make a permanent copy of this image's contents.
    avifImage * image;

    // Counts and timing for the current image in an image sequence. Uninteresting for single image files.
    int imageIndex;                // 0-based
    int imageCount;                // Always 1 for non-sequences
    avifImageTiming imageTiming;   //
    uint64_t timescale;            // timescale of the media (Hz)
    double duration;               // in seconds (durationInTimescales / timescale)
    uint64_t durationInTimescales; // duration in "timescales"

    // The width and height as reported by the AVIF container, if any. There is no guarantee
    // these match the decoded images; they are merely reporting what is independently offered
    // from the container's boxes.
    // * If decoding an "item" and the item is associated with an ImageSpatialExtentsBox,
    //   it will use the box's width/height
    // * Else if decoding tracks, these will be the integer portions of the TrackHeaderBox width/height
    // * Else both will be set to 0.
    uint32_t containerWidth;
    uint32_t containerHeight;

    // The bit depth as reported by the AVIF container, if any. There is no guarantee
    // this matches the decoded images; it is merely reporting what is independently offered
    // from the container's boxes.
    // * If decoding an "item" and the item is associated with an av1C property,
    //   it will use the box's depth flags.
    // * Else if decoding tracks and there is a SampleDescriptionBox of type av01 containing an av1C box,
    //   it will use the box's depth flags.
    // * Else it will be set to 0.
    uint32_t containerDepth;

    // stats from the most recent read, possibly 0s if reading an image sequence
    avifIOStats ioStats;

    // Internals used by the decoder
    struct avifData * data;
} avifDecoder;

avifDecoder * avifDecoderCreate(void);
void avifDecoderDestroy(avifDecoder * decoder);

// Simple interface to decode a single image, independent of the decoder afterwards (decoder may be deestroyed).
avifResult avifDecoderRead(avifDecoder * decoder, avifImage * image, avifROData * input);

// Multi-function alternative to avifDecoderRead() for image sequences and gaining direct access
// to the decoder's YUV buffers (for performance's sake). Data passed into avifDecoderParse() is NOT
// copied, so it must continue to exist until the decoder is destroyed.
//
// Usage / function call order is:
// * avifDecoderCreate()
// * avifDecoderSetSource() - optional
// * avifDecoderParse()
// * avifDecoderNextImage() - in a loop, using decoder->image after each successful call
// * avifDecoderDestroy()
//
// You can use avifDecoderReset() any time after a successful call to avifDecoderParse()
// to reset the internal decoder back to before the first frame.
avifResult avifDecoderSetSource(avifDecoder * decoder, avifDecoderSource source);
avifResult avifDecoderParse(avifDecoder * decoder, avifROData * input);
avifResult avifDecoderNextImage(avifDecoder * decoder);
avifResult avifDecoderNthImage(avifDecoder * decoder, uint32_t frameIndex);
avifResult avifDecoderReset(avifDecoder * decoder);

// Keyframe information
// frameIndex - 0-based, matching avifDecoder->imageIndex, bound by avifDecoder->imageCount
// "nearest" keyframe means the keyframe prior to this frame index (returns frameIndex if it is a keyframe)
avifBool avifDecoderIsKeyframe(avifDecoder * decoder, uint32_t frameIndex);
uint32_t avifDecoderNearestKeyframe(avifDecoder * decoder, uint32_t frameIndex);

// ---------------------------------------------------------------------------
// avifEncoder

// Notes:
// * If avifEncoderWrite() returns AVIF_RESULT_OK, output must be freed with avifRWDataFree()
// * If (maxThreads < 2), multithreading is disabled
// * Quality range: [AVIF_BEST_QUALITY - AVIF_WORST_QUALITY]
// * To enable tiling, set tileRowsLog2 > 0 and/or tileColsLog2 > 0.
//   Tiling values range [0-6], where the value indicates a request for 2^n tiles in that dimension.
// * Speed range: [AVIF_SPEED_SLOWEST - AVIF_SPEED_FASTEST]. Slower should make for a better quality
//   image in less bytes. AVIF_SPEED_DEFAULT means "Leave the AV1 codec to its default speed settings"./
//   If avifEncoder uses rav1e, the speed value is directly passed through (0-10). If libaom is used,
//   a combination of settings are tweaked to simulate this speed range.
typedef struct avifEncoder
{
    // Defaults to AVIF_CODEC_CHOICE_AUTO: Preference determined by order in availableCodecs table (avif.c)
    avifCodecChoice codecChoice;

    // settings (see Notes above)
    int maxThreads;
    int minQuantizer;
    int maxQuantizer;
    int tileRowsLog2;
    int tileColsLog2;
    int speed;

    // stats from the most recent write
    avifIOStats ioStats;
} avifEncoder;

avifEncoder * avifEncoderCreate(void);
avifResult avifEncoderWrite(avifEncoder * encoder, avifImage * image, avifRWData * output);
void avifEncoderDestroy(avifEncoder * encoder);

// Helpers
avifBool avifImageUsesU16(avifImage * image);

// Returns AVIF_TRUE if input begins with a valid FileTypeBox (ftyp) that supports
// either the brand 'avif' or 'avis' (or both), without performing any allocations.
avifBool avifPeekCompatibleFileType(avifROData * input);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef AVIF_AVIF_H
