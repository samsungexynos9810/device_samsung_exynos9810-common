/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _EXYNOSHWCHELPER_H
#define _EXYNOSHWCHELPER_H

#include <utils/String8.h>
#include <hardware/hwcomposer2.h>
#include <map>
#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#else
#include "gralloc_priv.h"
#endif
#include "DeconCommonHeader.h"
#include "VendorVideoAPI.h"
#include "exynos_sync.h"
#include "exynos_format.h"

#define MAX_FENCE_NAME 64
#define MAX_FENCE_THRESHOLD 500
#define MAX_FENCE_SEQUENCE 3

#define MAX_USE_FORMAT 27
#ifndef P010M_Y_SIZE
#define P010M_Y_SIZE(w,h) (__ALIGN_UP((w), 16) * 2 * __ALIGN_UP((h), 16) + 256)
#endif
#ifndef P010M_CBCR_SIZE
#define P010M_CBCR_SIZE(w,h) ((__ALIGN_UP((w), 16) * 2 * __ALIGN_UP((h), 16) / 2) + 256)
#endif
#ifndef P010_Y_SIZE
#define P010_Y_SIZE(w, h) ((w) * (h) * 2)
#endif
#ifndef P010_CBCR_SIZE
#define P010_CBCR_SIZE(w, h) ((w) * (h))
#endif

#define DISPLAYID_MASK_LEN 8
#define GET_STRING(map,n) (\
        (map.find(n) != map.end()) ? map.at(n) : "out of range")

template<typename T> inline T max(T a, T b) { return (a > b) ? a : b; }
template<typename T> inline T min(T a, T b) { return (a < b) ? a : b; }

class ExynosLayer;
class ExynosDisplay;

using namespace android;

enum {
    /* AFBC_NO_RESTRICTION means the case that resource set includes two DPPs
       sharing AFBC resource. If AFBC_NO_RESTRICTION of resource set
       is true, resource set supports AFBC without size restriction. */
    ATTRIBUTE_AFBC_NO_RESTRICTION = 0,
    ATTRIBUTE_HDR10PLUS,
    ATTRIBUTE_HDR10,
    ATTRIBUTE_DRM,
    ATTRIBUTE_AFBC,
    MAX_ATTRIBUTE_NUM,
};

#define HAL_COLOR_TRANSFORM_ERROR   100

enum {
    EXYNOS_HWC_DIM_LAYER    = 0x00000001,
    EXYNOS_HWC_FORCE_CLIENT = 0x00000002,
};

typedef enum format_type {

    /* format */
    RGB             = 0x00000001,
    YUV420          = 0x00000002,
    YUV422          = 0x00000004,
    UNDEF_FORMAT    = 0x00008000,

    /* bit */
    BIT8            = 0x00010000,
    BIT10           = 0x00020000,
    UNDEF_BIT       = 0x08000000,

    /* undefined */
    UNDEF           = 0x80000000,

} format_type_t;

typedef struct format_description {
    int halFormat;
    decon_pixel_format s3cFormat;
    uint32_t bufferNum;
    uint8_t bpp;
    uint32_t type;
    bool hasAlpha;
    String8 name;
    uint32_t reserved;
} format_description_t;

#define HAL_PIXEL_FORMAT_EXYNOS_UNDEFINED 0
const format_description_t exynos_format_desc[] = {
    /* RGB */
    {HAL_PIXEL_FORMAT_RGBA_8888,
        DECON_PIXEL_FORMAT_RGBA_8888, 1, 32, RGB|BIT8, true, String8("RGBA_8888"), 0},
    {HAL_PIXEL_FORMAT_RGBX_8888,
        DECON_PIXEL_FORMAT_RGBX_8888, 1, 32, RGB|BIT8, false, String8("RGBx_8888"), 0},
    {HAL_PIXEL_FORMAT_RGB_888,
        DECON_PIXEL_FORMAT_MAX, 1, 32, RGB|BIT8, false, String8("RGB_888"), 0},
    {HAL_PIXEL_FORMAT_RGB_565,
        DECON_PIXEL_FORMAT_RGB_565, 1, 16, RGB|UNDEF_BIT, false, String8("RGB_565"), 0},
    {HAL_PIXEL_FORMAT_BGRA_8888,
        DECON_PIXEL_FORMAT_BGRA_8888, 1, 32, RGB|BIT8, true, String8("BGRA_8888"), 0},
    {HAL_PIXEL_FORMAT_RGBA_1010102,
        DECON_PIXEL_FORMAT_ABGR_2101010, 1, 32, RGB|BIT10, true, String8("RGBA_1010102"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_ARGB_8888,
        DECON_PIXEL_FORMAT_MAX, 1, 32, RGB|BIT8, true, String8("EXYNOS_ARGB_8888"), 0},

    /* YUV 420 */
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M,
        DECON_PIXEL_FORMAT_YUV420M, 3, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_P_M"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M,
        DECON_PIXEL_FORMAT_NV12M, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SP_M"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_TILED,
        DECON_PIXEL_FORMAT_MAX, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SP_M_TILED"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YV12_M,
        DECON_PIXEL_FORMAT_YVU420M, 3, 12, YUV420|BIT8, false, String8("EXYNOS_YV12_M"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M,
        DECON_PIXEL_FORMAT_NV21M, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCrCb_420_SP_M"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL,
        DECON_PIXEL_FORMAT_NV21M, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCrCb_420_SP_M_FULL"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P,
        DECON_PIXEL_FORMAT_MAX, 1, 0, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_P"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP,
        DECON_PIXEL_FORMAT_MAX, 1, 0, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SP"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV,
        DECON_PIXEL_FORMAT_NV12M, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SP_M_PRIV"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_PN,
        DECON_PIXEL_FORMAT_MAX, 1, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_PN"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN,
        DECON_PIXEL_FORMAT_NV12N, 1, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SPN"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_TILED,
        DECON_PIXEL_FORMAT_MAX, 1, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SPN_TILED"), 0},
    {HAL_PIXEL_FORMAT_YCrCb_420_SP,
        DECON_PIXEL_FORMAT_NV21, 1, 12, YUV420|BIT8, false, String8("YCrCb_420_SP"), 0},
    {HAL_PIXEL_FORMAT_YV12,
        DECON_PIXEL_FORMAT_MAX, 1, 12, YUV420|BIT8, false, String8("YV12"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B,
        DECON_PIXEL_FORMAT_NV12M_S10B, 2, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SP_M_S10B"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B,
        DECON_PIXEL_FORMAT_NV12N_10B, 1, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SPN_S10B"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_P010_M,
        DECON_PIXEL_FORMAT_NV12M_P010, 2, 24, YUV420|BIT10, false, String8("EXYNOS_YCbCr_P010_M"), 0},
    {HAL_PIXEL_FORMAT_YCBCR_P010,
        DECON_PIXEL_FORMAT_NV12_P010, 1, 24, YUV420|BIT10, false, String8("EXYNOS_YCbCr_P010"), 0},

    /* YUV 422 */
    {HAL_PIXEL_FORMAT_EXYNOS_CbYCrY_422_I,
        DECON_PIXEL_FORMAT_MAX, 0, 0, YUV422|BIT8, false, String8("EXYNOS_CbYCrY_422_I"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_422_SP,
        DECON_PIXEL_FORMAT_MAX, 0, 0, YUV422|BIT8, false, String8("EXYNOS_YCrCb_422_SP"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_422_I,
        DECON_PIXEL_FORMAT_MAX, 0, 0, YUV422|BIT8, false, String8("EXYNOS_YCrCb_422_I"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_CrYCbY_422_I,
        DECON_PIXEL_FORMAT_MAX, 0, 0, YUV422|BIT8, false, String8("EXYNOS_CrYCbY_422_I"), 0},

    /* SBWC formats */
    /* NV12, YCbCr, Multi */
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC,
        DECON_PIXEL_FORMAT_NV12M_SBWC_8B, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SP_M_SBWC"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L50,
        DECON_PIXEL_FORMAT_NV12M_SBWC_8B_L50, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SP_M_SBWC_L50"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L75,
        DECON_PIXEL_FORMAT_NV12M_SBWC_8B_L75, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SP_M_SBWC_L75"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC,
        DECON_PIXEL_FORMAT_NV12M_SBWC_10B, 2, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SP_M_10B_SBWC"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L40,
        DECON_PIXEL_FORMAT_NV12M_SBWC_10B_L40, 2, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SP_M_10B_SBWC_L40"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L60,
        DECON_PIXEL_FORMAT_NV12M_SBWC_10B_L60, 2, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SP_M_10B_SBWC_L60"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L80,
        DECON_PIXEL_FORMAT_NV12M_SBWC_10B_L80, 2, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SP_M_10B_SBWC_L80"), 0},

    /* NV12, YCbCr, Single */
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC,
        DECON_PIXEL_FORMAT_NV12N_SBWC_8B, 1, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SPN_SBWC"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L50,
        DECON_PIXEL_FORMAT_NV12N_SBWC_8B_L50, 1, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SPN_SBWC_L50"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L75,
        DECON_PIXEL_FORMAT_NV12N_SBWC_8B_L75, 1, 12, YUV420|BIT8, false, String8("EXYNOS_YCbCr_420_SPN_SBWC_75"), 0},

    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC,
        DECON_PIXEL_FORMAT_NV12N_SBWC_10B, 1, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SPN_10B_SBWC"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L40,
        DECON_PIXEL_FORMAT_NV12N_SBWC_10B_L40, 1, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SPN_10B_SBWC_L40"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L60,
        DECON_PIXEL_FORMAT_NV12N_SBWC_10B_L60, 1, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SPN_10B_SBWC_L60"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L80,
        DECON_PIXEL_FORMAT_NV12N_SBWC_10B_L80, 1, 12, YUV420|BIT10, false, String8("EXYNOS_YCbCr_420_SPN_10B_SBWC_L80"), 0},

    /* NV12, YCrCb */
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_SBWC,
        DECON_PIXEL_FORMAT_NV21M_SBWC_8B, 2, 12, YUV420|BIT8, false, String8("EXYNOS_YCrCb_420_SP_M_SBWC"), 0},
    {HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_10B_SBWC,
        DECON_PIXEL_FORMAT_NV21M_SBWC_10B, 2, 12, YUV420|BIT10, false, String8("EXYNOS_YCrbCb_420_SP_M_10B_SBWC"), 0},

    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
        DECON_PIXEL_FORMAT_MAX, 0, 0, UNDEF, false, String8("ImplDef"), 0}
};

#define FORMAT_MAX_CNT sizeof(exynos_format_desc)/sizeof(format_description)

typedef enum hwc_fdebug_fence_type {
    FENCE_TYPE_SRC_RELEASE = 1,
    FENCE_TYPE_SRC_ACQUIRE = 2,
    FENCE_TYPE_DST_RELEASE = 3,
    FENCE_TYPE_DST_ACQUIRE = 4,
    FENCE_TYPE_HW_STATE = 7,
    FENCE_TYPE_RETIRE = 8,
    FENCE_TYPE_READBACK_ACQUIRE = 9,
    FENCE_TYPE_READBACK_RELEASE = 10,
    FENCE_TYPE_ALL = 11,
    FENCE_TYPE_UNDEFINED = 12,
    FENCE_TYPE_MAX
} hwc_fdebug_fence_type_t;

const std::map<int32_t, String8> fence_type_map = {
    {FENCE_TYPE_SRC_RELEASE, String8("SRC_REL")},
    {FENCE_TYPE_SRC_ACQUIRE, String8("SRC_ACQ")},
    {FENCE_TYPE_DST_RELEASE, String8("DST_REL")},
    {FENCE_TYPE_DST_ACQUIRE, String8("DST_ACQ")},
    {FENCE_TYPE_HW_STATE, String8("HWC_STATE")},
    {FENCE_TYPE_RETIRE, String8("RETIRE")},
    {FENCE_TYPE_ALL, String8("ALL")},
    {FENCE_TYPE_UNDEFINED, String8("UNDEF")},
};

typedef enum hwc_fdebug_ip_type {
    FENCE_IP_DPP = 0,
    FENCE_IP_MSC = 1,
    FENCE_IP_G2D = 2,
    FENCE_IP_FB = 3,
    FENCE_IP_LAYER = 4,
    FENCE_IP_OUTBUF = 5,
    FENCE_IP_ALL = 6,
    FENCE_IP_UNDEFINED = 7,
    FENCE_IP_MAX
} hwc_fdebug_ip_type_t;

const std::map<int32_t, String8> fence_ip_map = {
    {FENCE_IP_DPP, String8("DPP")},
    {FENCE_IP_MSC, String8("MSC")},
    {FENCE_IP_G2D, String8("G2D")},
    {FENCE_IP_FB, String8("FB")},
    {FENCE_IP_LAYER, String8("LAYER")},
    {FENCE_IP_OUTBUF, String8("OUTBUF")},
    {FENCE_IP_ALL, String8("ALL")},
    {FENCE_IP_UNDEFINED, String8("UNDEF")}
};

typedef enum hwc_fence_type {
    FENCE_LAYER_RELEASE_DPP     = 0,
    FENCE_LAYER_RELEASE_MSC     = 1,
    FENCE_LAYER_RELEASE_G2D     = 2,
    FENCE_DPP_HW_STATE          = 3,
    FENCE_MSC_HW_STATE          = 4,
    FENCE_G2D_HW_STATE          = 5,
    FENCE_MSC_SRC_LAYER         = 6,
    FENCE_G2D_SRC_LAYER         = 7,
    FENCE_MSC_DST_DPP           = 8,
    FENCE_G2D_DST_DPP           = 9,
    FENCE_DPP_SRC_MSC           = 10,
    FENCE_DPP_SRC_G2D           = 11,
    FENCE_DPP_SRC_LAYER         = 12,
    FENCE_RETIRE                = 15,
    FENCE_UNDEFINED             = 16,
    FENCE_MAX
} hwc_fence_type_t;

const std::map<uint32_t, String8> ip_fence_map = {
    {FENCE_LAYER_RELEASE_DPP, String8("LAYER_REL_DPP")},
    {FENCE_LAYER_RELEASE_MSC, String8("LAYER_REL_MSC")},
    {FENCE_LAYER_RELEASE_G2D, String8("LAYER_REL_G2D")},
    {FENCE_DPP_HW_STATE, String8("DPP_HW_STATE")},
    {FENCE_MSC_HW_STATE, String8("MSC_HW_STATE")},
    {FENCE_G2D_HW_STATE, String8("G2D_HW_STATE")},
    {FENCE_MSC_SRC_LAYER, String8("MSC_SRC")},
    {FENCE_G2D_SRC_LAYER, String8("G2D_SRC")},
    {FENCE_MSC_DST_DPP, String8("MSC_DST_DPP")},
    {FENCE_G2D_DST_DPP, String8("G2D_DST_DPP")},
    {FENCE_DPP_SRC_MSC, String8("DPP_SRC_MSC")},
    {FENCE_DPP_SRC_G2D, String8("DPP_SRC_G2D")},
    {FENCE_DPP_SRC_LAYER, String8("DPP_SRC_LAYER")},
    {FENCE_RETIRE, String8("RETIRE")},
    {FENCE_UNDEFINED, String8("UNDEF")},
    {FENCE_MAX, String8("FENCE_MAX")}
};

typedef enum fence_dir {
    FENCE_FROM = 0,
    FENCE_TO,
    FENCE_DUP,
    FENCE_CLOSE,
    FENCE_DIR_MAX
} fence_dir_t;

const std::map<int32_t, String8> fence_dir_map = {
    {FENCE_FROM, String8("From")},
    {FENCE_TO, String8("To")},
    {FENCE_DUP, String8("Dup")},
    {FENCE_CLOSE, String8("Close")},
};

enum {
    EXYNOS_ERROR_NONE       = 0,
    EXYNOS_ERROR_CHANGED    = 1
};

enum {
    eSkipLayer                    =     0x00000001,
    eInvalidHandle                =     0x00000002,
    eHasFloatSrcCrop              =     0x00000004,
    eUpdateExynosComposition      =     0x00000008,
    eDynamicRecomposition         =     0x00000010,
    eForceFbEnabled               =     0x00000020,
    eSandwitchedBetweenGLES       =     0x00000040,
    eSandwitchedBetweenEXYNOS     =     0x00000080,
    eInsufficientWindow           =     0x00000100,
    eInsufficientMPP              =     0x00000200,
    eSkipStaticLayer              =     0x00000400,
    eUnSupportedUseCase           =     0x00000800,
    eDimLayer                     =     0x00001000,
    eResourcePendingWork          =     0x00002000,
    eSourceOverBelow              =     0x00004000,
    eSkipRotateAnim               =     0x00008000,
    eUnSupportedColorTransform    =     0x00010000,
    eLowFpsLayer                  =     0x00020000,
    eReallocOnGoingForDDI         =     0x00040000,
    eInvalidDispFrame             =     0x00080000,
    eExceedMaxLayerNum            =     0x00100000,
    eFroceClientLayer             =     0x00200000,
    eResourceAssignFail           =     0x20000000,
    eMPPUnsupported               =     0x40000000,
    eUnknown                      =     0x80000000,
};

enum regionType {
    eTransparentRegion          =       0,
    eCoveredOpaqueRegion        =       1,
    eDamageRegionByDamage       =       2,
    eDamageRegionByLayer        =       3,
};

enum {
    eDamageRegionFull = 0,
    eDamageRegionPartial,
    eDamageRegionSkip,
    eDamageRegionError,
};

/*
 * bufferHandle can be NULL if it is not allocated yet
 * or size or format information can be different between other field values and
 * member of bufferHandle. This means bufferHandle should be reallocated.
 * */
typedef struct exynos_image {
    uint32_t fullWidth;
    uint32_t fullHeight;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    hwc_color_t color;
    uint32_t format;
    uint64_t usageFlags;
    uint32_t layerFlags;
    int acquireFenceFd = -1;
    int releaseFenceFd = -1;
    private_handle_t *bufferHandle;
    android_dataspace dataSpace;
    uint32_t blending;
    uint32_t transform;
    uint32_t compressed;
    float planeAlpha;
    uint32_t zOrder = 0;
    /* refer
     * frameworks/native/include/media/hardware/VideoAPI.h
     * frameworks/native/include/media/hardware/HardwareAPI.h */
    bool hasMetaParcel = false;
    ExynosVideoMeta metaParcel;
    ExynosVideoInfoType metaType = VIDEO_INFO_TYPE_INVALID;
} exynos_image_t;

uint32_t getHWC1CompType(int32_t /*hwc2_composition_t*/ type);

uint32_t getDrmMode(uint64_t flags);
uint32_t getDrmMode(const private_handle_t *handle);

inline int WIDTH(const hwc_rect &rect) { return rect.right - rect.left; }
inline int HEIGHT(const hwc_rect &rect) { return rect.bottom - rect.top; }
inline int WIDTH(const hwc_frect_t &rect) { return (int)(rect.right - rect.left); }
inline int HEIGHT(const hwc_frect_t &rect) { return (int)(rect.bottom - rect.top); }

uint32_t halDataSpaceToV4L2ColorSpace(android_dataspace data_space);
enum decon_pixel_format halFormatToDpuFormat(int format);
uint32_t DpuFormatToHalFormat(int format);
uint8_t formatToBpp(int format);
uint8_t DpuFormatToBpp(decon_pixel_format format);
enum decon_blending halBlendingToDpuBlending(int32_t blending);
enum dpp_rotate halTransformToDpuRot(uint32_t halTransform);

bool isFormatRgb(int format);
bool isFormatYUV(int format);
bool isFormatYUV420(int format);
bool isFormatYUV422(int format);
bool isFormatYCrCb(int format);
bool isFormat10BitYUV420(int format);
bool isFormatLossy(int format);
bool formatHasAlphaChannel(int format);
unsigned int isNarrowRgb(int format, android_dataspace data_space);
bool isCompressed(const private_handle_t *handle);
bool isSrcCropFloat(hwc_frect &frect);
bool isScaled(exynos_image &src, exynos_image &dst);
bool isScaledDown(exynos_image &src, exynos_image &dst);
bool hasHdrInfo(exynos_image &img);
bool hasHdrInfo(android_dataspace dataSpace);
bool hasHdr10Plus(exynos_image &img);

void dumpExynosImage(uint32_t type, exynos_image &img);
void dumpExynosImage(String8& result, exynos_image &img);
void dumpHandle(uint32_t type, private_handle_t *h);
String8 getFormatStr(int format);
String8 getMPPStr(int typeId);
void adjustRect(hwc_rect_t &rect, int32_t width, int32_t height);
uint32_t getBufferNumOfFormat(int format);
uint32_t getTypeOfFormat(int format);

int fence_close(int fence, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip);
bool fence_valid(int fence);

int hwcFdClose(int fd);
int hwc_dup(int fd, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip);
int hwc_print_stack();

inline hwc_rect expand(const hwc_rect &r1, const hwc_rect &r2)
{
    hwc_rect i;
    i.top = min(r1.top, r2.top);
    i.bottom = max(r1.bottom, r2.bottom);
    i.left = min(r1.left, r2.left);
    i.right = max(r1.right, r2.right);
    return i;
}

int pixel_align_down(int x, int a);

inline int pixel_align(int x, int a) {
    if ((a != 0) && ((x % a) != 0))
        return ((x) - (x % a)) + a;
    return x;
}

int getBufLength(private_handle_t *handle, uint32_t planer_num, size_t *length, int format, uint32_t width, uint32_t height);

//class hwc_fence_info(sync_fence_info_data* data, sync_pt_info* info) {
struct tm* getHwcFenceTime();

typedef struct fenceTrace {
    uint32_t dir;
    hwc_fdebug_fence_type type;
    hwc_fdebug_ip_type ip;
    struct timeval time;
    int32_t curFlag;
    int32_t usage;
    bool isLast;
} fenceTrace_t;

typedef struct hwc_fence_info {
    uint32_t displayId;
    fenceTrace_t seq[MAX_FENCE_SEQUENCE];
    uint32_t seq_no = 0;
    uint32_t last_dir;
    int32_t usage;
    bool pendingAllowed = false;
    bool leaking = false;
} hwc_fence_info_t;


void setFenceName(int fenceFd, hwc_fence_type fenceType);
void changeFenceInfoState(uint32_t fd, ExynosDisplay *display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool pendingAllowed = false);
void setFenceInfo(uint32_t fd, ExynosDisplay *display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool pendingAllowed = false);
void printFenceInfo(uint32_t fd, hwc_fence_info_t* info);
void dumpFenceInfo(ExynosDisplay *display, int32_t __unused depth);
bool fenceWarn(hwc_fence_info_t **info, uint32_t threshold);
void resetFenceCurFlag(ExynosDisplay *display);
bool fenceWarn(ExynosDisplay *display, uint32_t threshold);
void printLeakFds(ExynosDisplay *display);
bool validateFencePerFrame(ExynosDisplay *display);
android_dataspace colorModeToDataspace(android_color_mode_t mode);

inline uint32_t getDisplayId(int32_t displayType, int32_t displayIndex = 0 ) {
    return (displayType << DISPLAYID_MASK_LEN) | displayIndex;
}
#endif
