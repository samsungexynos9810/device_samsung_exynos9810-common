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
#include <utils/Errors.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <utils/CallStack.h>
#include <android/sync.h>
#include "ExynosHWCHelper.h"
#include "ExynosHWCDebug.h"
#include "ExynosHWC.h"
#include "ExynosLayer.h"
#include "exynos_sync.h"
#include "videodev2_exynos_media.h"
#include "VendorVideoAPI.h"

#define AFBC_MAGIC  0xafbc

#define FT_LOGD(msg, ...) \
    {\
        if (exynosHWCControl.fenceTracer >= 2) \
            ALOGD("[FenceTracer]::" msg, ##__VA_ARGS__); \
    }
#define FT_LOGE(msg, ...) \
    {\
        if (exynosHWCControl.fenceTracer > 0) \
            ALOGE("[FenceTracer]::" msg, ##__VA_ARGS__); \
    }
#define FT_LOGI(msg, ...) \
    {\
        if (exynosHWCControl.fenceTracer >= 1) \
            ALOGD("[FenceTracer]::" msg, ##__VA_ARGS__); \
    }

extern struct exynos_hwc_control exynosHWCControl;
extern char fence_names[FENCE_MAX][32];

uint32_t getHWC1CompType(int32_t type) {

    uint32_t cType = HWC_FRAMEBUFFER;

    switch(type) {
    case HWC2_COMPOSITION_DEVICE:
    case HWC2_COMPOSITION_EXYNOS:
        cType = HWC_OVERLAY;
        break;
    case HWC2_COMPOSITION_SOLID_COLOR:
        cType = HWC_BACKGROUND;
        break;
    case HWC2_COMPOSITION_CURSOR:
        cType = HWC_CURSOR_OVERLAY;
        break;
    case HWC2_COMPOSITION_SIDEBAND:
        cType = HWC_SIDEBAND;
        break;
    case HWC2_COMPOSITION_CLIENT:
    case HWC2_COMPOSITION_INVALID:
    default:
        cType = HWC_FRAMEBUFFER;
        break;
    }

    return cType;
}

uint32_t getDrmMode(uint64_t flags)
{
#ifdef GRALLOC_VERSION1
    if (flags & GRALLOC1_PRODUCER_USAGE_PROTECTED) {
        if (flags & GRALLOC1_PRODUCER_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }
#else
    if (flags & GRALLOC_USAGE_PROTECTED) {
        if (flags & GRALLOC_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }
#endif
    return NO_DRM;
}

uint32_t getDrmMode(const private_handle_t *handle)
{
#ifdef GRALLOC_VERSION1
    if (handle->producer_usage & GRALLOC1_PRODUCER_USAGE_PROTECTED) {
        if (handle->producer_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }
#else
    if (handle->flags & GRALLOC_USAGE_PROTECTED) {
        if (handle->flags & GRALLOC_USAGE_PRIVATE_NONSECURE)
            return NORMAL_DRM;
        else
            return SECURE_DRM;
    }
#endif
    return NO_DRM;
}

unsigned int isNarrowRgb(int format, android_dataspace data_space)
{
    if (format == HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL)
        return 0;
    else {
        if (isFormatRgb(format))
            return 0;
        else {
            uint32_t data_space_range = (data_space & HAL_DATASPACE_RANGE_MASK);
            if (data_space_range == HAL_DATASPACE_RANGE_UNSPECIFIED) {
                return 1;
            } else if (data_space_range == HAL_DATASPACE_RANGE_FULL) {
                return 0;
            } else {
                return 1;
            }
        }
    }
}

uint8_t formatToBpp(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format)
            return exynos_format_desc[i].bpp;
    }

    ALOGW("unrecognized pixel format %u", format);
    return 0;
}

uint8_t DpuFormatToBpp(decon_pixel_format format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].s3cFormat == format)
            return exynos_format_desc[i].bpp;
    }
    ALOGW("unrecognized decon format %u", format);
    return 0;
}

bool isFormatRgb(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format) {
            if (exynos_format_desc[i].type & RGB)
                return true;
            else
                return false;
        }
    }
    return false;
}

bool isFormatYUV(int format)
{
    if (isFormatRgb(format))
        return false;
    return true;
}

bool isFormatYUV420(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format) {
            if (exynos_format_desc[i].type & YUV420)
                return true;
            else
                return false;
        }
    }
    return false;
}

bool isFormat10BitYUV420(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format) {
            if ((exynos_format_desc[i].type & YUV420) &&
                (exynos_format_desc[i].type & BIT10))
                return true;
            else
                return false;
        }
    }
    return false;
}

bool isFormatYUV422(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format) {
            if (exynos_format_desc[i].type & YUV422)
                return true;
            else
                return false;
        }
    }
    return false;
}

bool isFormatYCrCb(int format)
{
    return format == HAL_PIXEL_FORMAT_EXYNOS_YV12_M;
}

bool isFormatLossy(int format)
{
    switch (format) {
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L50:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L75:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L50:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L75:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L40:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L60:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L80:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L40:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L60:
        case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L80:
            return true;
        default:
            return false;
    }
    return false;
}



bool formatHasAlphaChannel(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format) {
            return exynos_format_desc[i].hasAlpha;
        }
    }
    return false;
}

bool checkCompressionFd(const private_handle_t *handle)
{
    if ((getBufferNumOfFormat(handle->format) == 1) && (handle->fd1 > 0)) {
        void *_map = NULL;
        uint32_t *isAFBC = NULL;
        _map = mmap(0, sizeof(uint32_t), PROT_READ|PROT_WRITE, MAP_SHARED, handle->fd1, 0);
        if (_map == NULL) {
            ALOGE("%s, map is NULL", __func__);
        } else if (_map == MAP_FAILED) {
            ALOGE("%s, map failed", __func__);
        } else {
            isAFBC = static_cast<uint32_t *>(_map);
            if ((isAFBC != NULL) && (*isAFBC == AFBC_MAGIC)) {
                munmap(isAFBC, sizeof(uint32_t));
                return true;
            } else {
                if (isAFBC != NULL) munmap(isAFBC, sizeof(uint32_t));
                return false;
            }
        }
    }
    return false;
}

bool isCompressed(const private_handle_t *handle)
{
    if (handle != NULL) {
        if (checkCompressionFd(handle))
            return true;
    }

    return false;
}

uint32_t halDataSpaceToV4L2ColorSpace(android_dataspace data_space)
{
    uint32_t standard_data_space = (data_space & HAL_DATASPACE_STANDARD_MASK);
    switch (standard_data_space) {
    case HAL_DATASPACE_STANDARD_BT2020:
    case HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE:
        return V4L2_COLORSPACE_BT2020;
    case HAL_DATASPACE_STANDARD_DCI_P3:
        return V4L2_COLORSPACE_DCI_P3;
    case HAL_DATASPACE_STANDARD_BT709:
        return V4L2_COLORSPACE_REC709;
    default:
        return V4L2_COLORSPACE_DEFAULT;
    }
    return V4L2_COLORSPACE_DEFAULT;
}

enum decon_pixel_format halFormatToDpuFormat(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format)
            return exynos_format_desc[i].s3cFormat;
    }
    return DECON_PIXEL_FORMAT_MAX;
}

uint32_t DpuFormatToHalFormat(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].s3cFormat == static_cast<decon_pixel_format>(format))
            return exynos_format_desc[i].halFormat;
    }
    return HAL_PIXEL_FORMAT_EXYNOS_UNDEFINED;
}

android_dataspace colorModeToDataspace(android_color_mode_t mode)
{
    android_dataspace dataSpace = HAL_DATASPACE_UNKNOWN;
    switch (mode) {
        case HAL_COLOR_MODE_STANDARD_BT601_625:
            dataSpace = HAL_DATASPACE_STANDARD_BT601_625;
            break;
        case HAL_COLOR_MODE_STANDARD_BT601_625_UNADJUSTED:
            dataSpace = HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED;
            break;
        case HAL_COLOR_MODE_STANDARD_BT601_525:
            dataSpace = HAL_DATASPACE_STANDARD_BT601_525;
            break;
        case HAL_COLOR_MODE_STANDARD_BT601_525_UNADJUSTED:
            dataSpace = HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED;
            break;
        case HAL_COLOR_MODE_STANDARD_BT709:
            dataSpace = HAL_DATASPACE_STANDARD_BT709;
            break;
        case HAL_COLOR_MODE_DCI_P3:
            dataSpace = HAL_DATASPACE_DCI_P3;
            break;
        case HAL_COLOR_MODE_ADOBE_RGB:
            dataSpace = HAL_DATASPACE_ADOBE_RGB;
            break;
        case HAL_COLOR_MODE_DISPLAY_P3:
            dataSpace = HAL_DATASPACE_DISPLAY_P3;
            break;
        case HAL_COLOR_MODE_SRGB:
            dataSpace = HAL_DATASPACE_V0_SRGB;
            break;
        case HAL_COLOR_MODE_NATIVE:
            dataSpace = HAL_DATASPACE_UNKNOWN;
            break;
        default:
            break;
    }
    return dataSpace;
}

enum decon_blending halBlendingToDpuBlending(int32_t blending)
{
    switch (blending) {
    case HWC2_BLEND_MODE_NONE:
        return DECON_BLENDING_NONE;
    case HWC2_BLEND_MODE_PREMULTIPLIED:
        return DECON_BLENDING_PREMULT;
    case HWC2_BLEND_MODE_COVERAGE:
        return DECON_BLENDING_COVERAGE;

    default:
        return DECON_BLENDING_MAX;
    }
}

enum dpp_rotate halTransformToDpuRot(uint32_t halTransform)
{
    switch (halTransform) {
    case HAL_TRANSFORM_FLIP_H:
        return DPP_ROT_YFLIP;
    case HAL_TRANSFORM_FLIP_V:
        return DPP_ROT_XFLIP;
    case HAL_TRANSFORM_ROT_180:
        return DPP_ROT_180;
    case HAL_TRANSFORM_ROT_90:
        return DPP_ROT_90;
    case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H):
        /*
         * HAL: HAL_TRANSFORM_FLIP_H -> HAL_TRANSFORM_ROT_90
         * VPP: ROT_90 -> XFLIP
         */
        return DPP_ROT_90_XFLIP;
    case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V):
        /*
         * HAL: HAL_TRANSFORM_FLIP_V -> HAL_TRANSFORM_ROT_90
         * VPP: ROT_90 -> YFLIP
         */
        return DPP_ROT_90_YFLIP;
    case HAL_TRANSFORM_ROT_270:
        return DPP_ROT_270;
    default:
        return DPP_ROT_NORMAL;
    }
}

void dumpHandle(uint32_t type, private_handle_t *h)
{
    if (h == NULL)
        return;
    HDEBUGLOGD(type, "\t\tformat = %d, width = %u, height = %u, stride = %u, vstride = %u",
            h->format, h->width, h->height, h->stride, h->vstride);
}

void dumpExynosImage(uint32_t type, exynos_image &img)
{
    if (!hwcCheckDebugMessages(type))
        return;
    ALOGD("\tbufferHandle: %p, fullWidth: %d, fullHeight: %d, x: %d, y: %d, w: %d, h: %d, format: %s",
            img.bufferHandle, img.fullWidth, img.fullHeight, img.x, img.y, img.w, img.h, getFormatStr(img.format).string());
    ALOGD("\tusageFlags: 0x%" PRIx64 ", layerFlags: 0x%8x, acquireFenceFd: %d, releaseFenceFd: %d",
            img.usageFlags, img.layerFlags, img.acquireFenceFd, img.releaseFenceFd);
    ALOGD("\tdataSpace(%d), blending(%d), transform(0x%2x), compressed(%d)",
            img.dataSpace, img.blending, img.transform, img.compressed);
}

void dumpExynosImage(String8& result, exynos_image &img)
{
    result.appendFormat("\tbufferHandle: %p, fullWidth: %d, fullHeight: %d, x: %d, y: %d, w: %d, h: %d, format: %s\n",
            img.bufferHandle, img.fullWidth, img.fullHeight, img.x, img.y, img.w, img.h, getFormatStr(img.format).string());
    result.appendFormat("\tusageFlags: 0x%" PRIx64 ", layerFlags: 0x%8x, acquireFenceFd: %d, releaseFenceFd: %d\n",
            img.usageFlags, img.layerFlags, img.acquireFenceFd, img.releaseFenceFd);
    result.appendFormat("\tdataSpace(%d), blending(%d), transform(0x%2x), compressed(%d)\n",
            img.dataSpace, img.blending, img.transform, img.compressed);
    if (img.bufferHandle != NULL) {
        result.appendFormat("\tbuffer's stride: %d, %d\n", img.bufferHandle->stride, img.bufferHandle->vstride);
    }
}

bool isSrcCropFloat(hwc_frect &frect)
{
    return (frect.left != (int)frect.left) ||
        (frect.top != (int)frect.top) ||
        (frect.right != (int)frect.right) ||
        (frect.bottom != (int)frect.bottom);
}

bool isScaled(exynos_image &src, exynos_image &dst)
{
    uint32_t srcW = src.w;
    uint32_t srcH = src.h;
    uint32_t dstW = dst.w;
    uint32_t dstH = dst.h;

    if (!!(src.transform & HAL_TRANSFORM_ROT_90)) {
        dstW = dst.h;
        dstH = dst.w;
    }

    return ((srcW != dstW) || (srcH != dstH));
}

bool isScaledDown(exynos_image &src, exynos_image &dst)
{
    uint32_t srcW = src.w;
    uint32_t srcH = src.h;
    uint32_t dstW = dst.w;
    uint32_t dstH = dst.h;

    if (!!(src.transform & HAL_TRANSFORM_ROT_90)) {
        dstW = dst.h;
        dstH = dst.w;
    }

    return ((srcW > dstW) || (srcH > dstH));
}

bool hasHdrInfo(exynos_image &img)
{
    uint32_t dataSpace = img.dataSpace;

    /* By reference Layer's dataspace */
    uint32_t standard = (dataSpace & HAL_DATASPACE_STANDARD_MASK);
    uint32_t transfer = (dataSpace & HAL_DATASPACE_TRANSFER_MASK);

    if ((standard == HAL_DATASPACE_STANDARD_BT2020) ||
            (standard == HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE) ||
            (standard == HAL_DATASPACE_STANDARD_DCI_P3)) {
        if ((transfer == HAL_DATASPACE_TRANSFER_ST2084) ||
                (transfer == HAL_DATASPACE_TRANSFER_HLG))
            return true;
        else
            return false;
    }

    return false;
}

bool hasHdrInfo(android_dataspace dataSpace) {
    exynos_image img;
    img.dataSpace = dataSpace;
    return hasHdrInfo(img);
}

bool hasHdr10Plus(exynos_image &img) {
    /* TODO Check layer has hdr10 and dynamic metadata here */
    return (img.metaType & VIDEO_INFO_TYPE_HDR_DYNAMIC) ? true : false;
}

String8 getFormatStr(int format) {
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format)
            return exynos_format_desc[i].name;
    }
    String8 result;
    result.appendFormat("? %08x", format);
    return result;
}

void adjustRect(hwc_rect_t &rect, int32_t width, int32_t height)
{
    if (rect.left < 0)
        rect.left = 0;
    if (rect.left > width)
        rect.left = width;
    if (rect.top < 0)
        rect.top = 0;
    if (rect.top > height)
        rect.top = height;
    if (rect.right < rect.left)
        rect.right = rect.left;
    if (rect.right > width)
        rect.right = width;
    if (rect.bottom < rect.top)
        rect.bottom = rect.top;
    if (rect.bottom > height)
        rect.bottom = height;
}

uint32_t getBufferNumOfFormat(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format)
            return exynos_format_desc[i].bufferNum;
    }
    return 0;
}

uint32_t getTypeOfFormat(int format)
{
    for (unsigned int i = 0; i < FORMAT_MAX_CNT; i++){
        if (exynos_format_desc[i].halFormat == format)
            return exynos_format_desc[i].type;
    }
    return 0;
}

void setFenceName(int fenceFd, hwc_fence_type fenceType)
{
    if (fenceFd >= 3)
        ioctl(fenceFd, SYNC_IOC_FENCE_NAME, fence_names[fenceType]);
    else if (fenceFd == -1) {
        HDEBUGLOGD(eDebugFence, "%s : fence (type %d) is -1", __func__, (int)fenceType);
    }
    else {
        ALOGW("%s : fence (type %d) is less than 3", __func__, (int)fenceType);
        hwc_print_stack();
    }
}

int pixel_align_down(int x, int a) {
    if ((a != 0) && ((x % a) != 0)) {
        int ret = ((x) - (x % a));
        if (ret < 0)
            ret = 0;
        return ret;
    }
    return x;
}

uint32_t getExynosBufferYLength(uint32_t width, uint32_t height, int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:
        return NV12M_Y_SIZE(width, height);
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:
        HDEBUGLOGD(eDebugMPP, "8bit size(Y) : %d, extra size : %d", NV12M_Y_SIZE(width, height), NV12M_Y_2B_SIZE(width, height));
        return NV12M_Y_SIZE(width, height) + NV12M_Y_2B_SIZE(width, height);
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_P010_M:
        HDEBUGLOGD(eDebugMPP, "size(Y) : %d", P010M_Y_SIZE(width, height));
        return P010M_Y_SIZE(width, height);
    case HAL_PIXEL_FORMAT_YCBCR_P010:
        HDEBUGLOGD(eDebugMPP, "size(Y) : %d", P010_Y_SIZE(width, height));
        return P010_Y_SIZE(width, height);
    /* TODO : Add SBWC format and macros here */
    }

    return NV12M_Y_SIZE(width, height) + ((width % 128) == 0 ? 0 : 256);
}

uint32_t getExynosBufferCbCrLength(uint32_t width, uint32_t height, int format)
{
    switch (format) {
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_PRIV:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:
        return NV12M_CBCR_SIZE(width, height);
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:
        HDEBUGLOGD(eDebugMPP, "8bit size(CbCr) : %d, extra size : %d",NV12M_CBCR_SIZE(width, height), NV12M_CBCR_2B_SIZE(width, height));
        return NV12M_CBCR_SIZE(width, height) + NV12M_CBCR_2B_SIZE(width, height);
    case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_P010_M:
        HDEBUGLOGD(eDebugMPP, "size(CbCr) : %d", P010M_CBCR_SIZE(width, height));
        return P010M_CBCR_SIZE(width, height);
    case HAL_PIXEL_FORMAT_YCBCR_P010:
        HDEBUGLOGD(eDebugMPP, "size(CbCr) : %d", P010_CBCR_SIZE(width, height));
        return P010_CBCR_SIZE(width, height);
    /* TODO : Add SBWC format and macros here */
    }

    return NV12M_CBCR_SIZE(width, height);
}

int getBufLength(private_handle_t *handle, uint32_t planerNum, size_t *length, int format, uint32_t width, uint32_t height)
{
    uint32_t bufferNumber = getBufferNumOfFormat(format);
    if ((bufferNumber == 0) || (bufferNumber > planerNum))
        return -EINVAL;

    switch (bufferNumber) {
        case 1:
            length[0] = handle->size;
            break;
        case 2:
            HDEBUGLOGD(eDebugMPP, "-- %s x : %d y : %d format : %d",__func__, width, height, format);
            length[0] = handle->size;
            length[1] = handle->size1;
            HDEBUGLOGD(eDebugMPP, "Y size : %zu CbCr size : %zu", length[0], length[1]);
            break;
        case 3:
            length[0] = width * height;
            length[1]= (length[0]/4);
            length[2]= (length[0]/4);
            break;
    }
    return NO_ERROR;
}

int fence_close(int fence, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip) {
    if (display != NULL)
        setFenceInfo(fence, display, type, ip, FENCE_CLOSE);
    return hwcFdClose(fence);
}

bool fence_valid(int fence) {
    if (fence == -1){
        HDEBUGLOGD(eDebugFence, "%s : fence is -1", __func__);
        return false;
    } else if (fence < 3) {
        ALOGW("%s : fence (fd:%d) is less than 3", __func__, fence);
        hwc_print_stack();
        return true;
    }
    return true;
}

int hwcFdClose(int fd) {
    if (fd>= 3)
        close(fd);
    else if (fd == -1){
        HDEBUGLOGD(eDebugFence, "%s : Fd is -1", __func__);
    } else {
        ALOGW("%s : Fd:%d is less than 3", __func__, fd);
        hwc_print_stack();
    }
    return -1;
}

int hwc_dup(int fd, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip) {

    int dup_fd = -1;

    if (fd>= 3)
        dup_fd = dup(fd);
    else if (fd == -1){
        HDEBUGLOGD(eDebugFence, "%s : Fd is -1", __func__);
    } else {
        ALOGW("%s : Fd:%d is less than 3", __func__, fd);
        hwc_print_stack();
    }

    if ((dup_fd < 3) && (dup_fd != -1)) {
        ALOGW("%s : Dupulicated Fd:%d is less than 3 : %d", __func__, fd, dup_fd);
        hwc_print_stack();
    }

    setFenceInfo(dup_fd, display, type, ip, FENCE_FROM);
    FT_LOGD("duplicated %d from %d", dup_fd, fd);

    return dup_fd;
}

int hwc_print_stack() {
#if 0
    CallStack stack(LOG_TAG);
    stack.update();
    stack.log("HWCException", ANDROID_LOG_ERROR, "HWCException");
#endif
    return 0;
}

struct tm* getLocalTime(struct timeval tv) {
    return (struct tm*)localtime((time_t*)&tv.tv_sec);
}

void writeFenceInfo(uint32_t fd, hwc_fence_info_t *info,
    hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
    uint32_t direction, bool pendingAllowed) {

    /* Sequnce is ring buffer */
    /* update last flag, sequence */
    info->seq[info->seq_no].isLast = false;
    info->seq_no++;
    if (info->seq_no >= MAX_FENCE_SEQUENCE) info->seq_no = 0;
    fenceTrace_t *seq = &info->seq[info->seq_no];
    seq->isLast = true;

    /* direction, type, ip */
    seq->dir =  direction;
    seq->type = type;
    seq->ip = ip;
    seq->curFlag = 1;
    info->pendingAllowed = pendingAllowed;

    /* time */
    struct timeval tv;
    gettimeofday(&seq->time, NULL);
    tv = seq->time;
}

void setFenceInfo(uint32_t fd, ExynosDisplay* display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool pendingAllowed) {

    if (!fence_valid(fd) || display == NULL) return;
    ExynosDevice* device = display->mDevice;
    if (device == NULL) return;

    /* init or recevice(from previous) trace info */
    hwc_fence_info_t info;
    if (device->mFenceInfo.count(fd) == 0) {
        memset(&info, 0, sizeof(hwc_fence_info_t));
    } else
        info = device->mFenceInfo.at(fd);

    info.displayId = display->mDisplayId;

    writeFenceInfo(fd, &info, type, ip, direction, pendingAllowed);

    fenceTrace_t *seq = &info.seq[info.seq_no];
    /* update usage count */
    if ((seq->dir == FENCE_FROM) || (seq->dir == FENCE_DUP)) {
        info.usage++;
    } else if ((seq->dir == FENCE_TO) || (seq->dir == FENCE_CLOSE)) {
        info.usage--;
        if ((seq->dir == FENCE_CLOSE) && (info.usage < 0)) info.usage = 0;
    } else
        ALOGE("Fence trace : Undefined direction!");

    seq->usage = info.usage;

    // Fence's usage count shuld be zero at end of frame(present done).
    // This flag means usage count of the fence can be pended over frame.
    if (info.usage == 0)
        info.pendingAllowed = false;

    /* last direction */
    info.last_dir = direction;

    FT_LOGI("FD : %d, direction : %d, type : %d, ip : %d, usage : %d (%s)",
            fd, direction, seq->type, seq->ip, info.usage, __func__);

    device->mFenceInfo[fd] = info;

}

void printLastFenceInfo(uint32_t fd, ExynosDisplay* display) {

    struct timeval tv;

    if (!fence_valid(fd)) return;

    ExynosDevice* device = display->mDevice;

    if (device->mFenceInfo.count(fd) == 0)
        return;

    hwc_fence_info_t info = device->mFenceInfo.at(fd);
    FT_LOGD("---- Fence FD : %d, Display(%d), usage(%d) ----", fd, info.displayId, info.usage);

    for (int i = 0; i < MAX_FENCE_SEQUENCE; i++) {
        fenceTrace_t *seq = &info.seq[i];
        FT_LOGD("fd(%d) %s(%s)(%s)(cur:%d)(usage:%d)(last:%d)",
                fd, GET_STRING(fence_dir_map, seq->dir),
                GET_STRING(fence_ip_map, seq->ip), GET_STRING(fence_type_map, seq->type),
                seq->curFlag, seq->usage, (int)seq->isLast);

        tv = seq->time;
        struct tm* localTime = getLocalTime(tv);

        FT_LOGD("time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)",
                localTime->tm_mon+1, localTime->tm_mday,
                localTime->tm_hour, localTime->tm_min,
                localTime->tm_sec, tv.tv_usec/1000,
                ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
    }

}

void dumpFenceInfo(ExynosDisplay *display, int32_t __unused depth) {

    ExynosDevice* device = display->mDevice;

    FT_LOGD("Dump fence ++");
    for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
        uint32_t i = it->first;
        hwc_fence_info_t info = device->mFenceInfo.at(i);
        if (device->mFenceInfo.count(i) == 0) continue;
        if ((info.usage >= 1 || info.usage <= -1) && (!info.pendingAllowed))
            printLastFenceInfo(i, display);
    }
    FT_LOGD("Dump fence --");
}

void printLeakFds(ExynosDisplay *display){

    ExynosDevice* device = display->mDevice;

    int cnt = 1;

    String8 errStringPlus;
    String8 errStringMinus;

    errStringPlus.appendFormat("Leak Fds (1) :\n");

    for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
        uint32_t i = it->first;
        hwc_fence_info_t info = device->mFenceInfo.at(i);
        if(info.usage >= 1) {
            errStringPlus.appendFormat("%d,", i);
            if(cnt++%10 == 0)
                errStringPlus.appendFormat("\n");
        }
    }
    FT_LOGI("%s", errStringPlus.string());

    errStringMinus.appendFormat("Leak Fds (-1) :\n");

    cnt = 1;
    for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
        uint32_t i = it->first;
        hwc_fence_info_t info = device->mFenceInfo.at(i);
        if(info.usage < 0) {
            errStringMinus.appendFormat("%d,", i);
            if(cnt++%10 == 0)
                errStringMinus.appendFormat("\n");
        }
    }
    FT_LOGI("%s", errStringMinus.string());
}

void dumpNCheckLeak(ExynosDisplay *display, int32_t __unused depth) {

    ExynosDevice* device = display->mDevice;

    FT_LOGD("Dump leaking fence ++");
    for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
        uint32_t i = it->first;
        hwc_fence_info_t info = device->mFenceInfo.at(i);
        if ((info.usage >= 1 || info.usage <= -1) && (!info.pendingAllowed))
            // leak is occured in this frame first
            if (!info.leaking) {
                info.leaking = true;
                printLastFenceInfo(i, display);
            }
    }

    int priv = exynosHWCControl.fenceTracer;
    exynosHWCControl.fenceTracer = 3;
    printLeakFds(display);
    exynosHWCControl.fenceTracer = priv;

    FT_LOGD("Dump leaking fence --");
}

bool fenceWarn(ExynosDisplay *display, uint32_t threshold) {

    uint32_t cnt = 0, r_cnt = 0;
    ExynosDevice* device = display->mDevice;

    for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
        uint32_t i = it->first;
        hwc_fence_info_t info = device->mFenceInfo.at(i);
        if(info.usage >= 1 || info.usage <= -1)
            cnt++;
    }

    if ((cnt>threshold) || (exynosHWCControl.fenceTracer > 0))
        dumpFenceInfo(display, 0);

    if (r_cnt>threshold)
        ALOGE("Fence leak somewhare!!");

    FT_LOGD("fence hwc : %d, real : %d", cnt, r_cnt);

    return (cnt>threshold) ? true : false;
}

void resetFenceCurFlag(ExynosDisplay *display) {
    ExynosDevice* device = display->mDevice;
    FT_LOGD("%s ++", __func__);
    for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
        uint32_t i = it->first;
        hwc_fence_info_t info = device->mFenceInfo.at(i);

        if (device->mFenceInfo.count(i) == 0) continue;

        if (info.usage == 0) {
            hwc_fence_info_t *info_ = &device->mFenceInfo[i];
            for(int j=0; j<MAX_FENCE_SEQUENCE; j++) info_->seq[j].curFlag = 0;
        } else if (!info.pendingAllowed)
            FT_LOGE("usage mismatched fd %d, usage %d, pending %d", i,
                    info.usage, info.pendingAllowed);
    }
    FT_LOGD("%s --", __func__);
}

bool validateFencePerFrame(ExynosDisplay *display) {

    bool ret = true;
    ExynosDevice* device = display->mDevice;

    for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {
        uint32_t i = it->first;
        hwc_fence_info_t info = device->mFenceInfo.at(i);
        if (info.displayId != display->mDisplayId)
            continue;
        if ((info.usage >= 1 || info.usage <= -1) &&
                (!info.pendingAllowed) && (!info.leaking)) {
            ret = false;
        }
    }

    if (!ret) {
        int priv = exynosHWCControl.fenceTracer;
        exynosHWCControl.fenceTracer = 3;
        dumpNCheckLeak(display, 0);
        exynosHWCControl.fenceTracer = priv;
    }

    return ret;
}

void changeFenceInfoState(uint32_t fd, ExynosDisplay *display,
        hwc_fdebug_fence_type type, hwc_fdebug_ip_type ip,
        uint32_t direction, bool pendingAllowed) {

    if (!fence_valid(fd) || display == NULL) return;
    ExynosDevice* device = display->mDevice;
    if (device == NULL) return;

    /* init or recevice(from previous) trace info */
    hwc_fence_info_t info;
    if (device->mFenceInfo.count(fd) == 0) {
        memset(&info, 0, sizeof(hwc_fence_info_t));
        info.seq_no = 0;
    } else
        info = device->mFenceInfo.at(fd);

    info.displayId = display->mDisplayId;

    writeFenceInfo(fd, &info, type, ip, direction, pendingAllowed);

    FT_LOGD("FD : %d, direction : %d, type(%d), ip(%d) (%s)", fd, direction, type, ip, __func__);

    device->mFenceInfo[fd] = info;
}

String8 getMPPStr(int typeId) {
    if (typeId < MPP_DPP_NUM){
        int cnt = sizeof(AVAILABLE_OTF_MPP_UNITS)/sizeof(exynos_mpp_t);
        for (int i = 0; i < cnt; i++){
            if (AVAILABLE_OTF_MPP_UNITS[i].physicalType == typeId)
                return String8(AVAILABLE_OTF_MPP_UNITS[i].name);
        }
    } else {
        int cnt = sizeof(AVAILABLE_M2M_MPP_UNITS)/sizeof(exynos_mpp_t);
        for (int i = 0; i < cnt; i++){
            if (AVAILABLE_M2M_MPP_UNITS[i].physicalType == typeId)
                return String8(AVAILABLE_M2M_MPP_UNITS[i].name);
        }
    }
    String8 result;
    result.appendFormat("? %08x", typeId);
    return result;
}
