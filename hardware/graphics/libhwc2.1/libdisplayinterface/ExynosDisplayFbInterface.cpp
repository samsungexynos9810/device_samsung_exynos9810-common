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

#include "ExynosDisplayFbInterface.h"
#include "ExynosPrimaryDisplay.h"
#include "ExynosExternalDisplay.h"
#include "ExynosHWCDebug.h"
#include "displayport_for_hwc.h"
#include <math.h>

using namespace android;
extern struct exynos_hwc_control exynosHWCControl;

const size_t NUM_HW_WINDOWS = MAX_DECON_WIN;

#ifndef DECON_READBACK_IDX
#define DECON_READBACK_IDX (-1)
#endif

#ifndef DECON_WIN_UPDATE_IDX
#define DECON_WIN_UPDATE_IDX (-1)
#endif
//////////////////////////////////////////////////// ExynosDisplayFbInterface //////////////////////////////////////////////////////////////////
ExynosDisplayFbInterface::ExynosDisplayFbInterface(ExynosDisplay *exynosDisplay)
: mDisplayFd(-1)
{
    mExynosDisplay = exynosDisplay;
    clearFbWinConfigData(mFbConfigData);
    memset(&mEdidData, 0, sizeof(decon_edid_data));
}

ExynosDisplayFbInterface::~ExynosDisplayFbInterface()
{
    if (mDisplayFd >= 0)
        hwcFdClose(mDisplayFd);
    mDisplayFd = -1;
}

void ExynosDisplayFbInterface::init(ExynosDisplay *exynosDisplay)
{
    mDisplayFd = -1;
    mExynosDisplay = exynosDisplay;

    if (exynosDisplay->mMaxWindowNum != getMaxWindowNum()) {
        ALOGE("%s:: Invalid max window number (mMaxWindowNum: %d, NUM_HW_WINDOWS: %zu",
                __func__, exynosDisplay->mMaxWindowNum, NUM_HW_WINDOWS);
        return;
    }

}

int32_t ExynosDisplayFbInterface::setPowerMode(int32_t mode)
{
    int32_t ret = NO_ERROR;
    int fb_blank = 0;
    if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
    } else {
        fb_blank = FB_BLANK_UNBLANK;
    }

    if ((ret = ioctl(mDisplayFd, FBIOBLANK, fb_blank)) != NO_ERROR) {
        HWC_LOGE(mExynosDisplay, "set powermode ioctl failed errno : %d", errno);
    }

    ALOGD("%s:: mode(%d), blank(%d)", __func__, mode, fb_blank);
    return ret;
}

int32_t ExynosDisplayFbInterface::setVsyncEnabled(uint32_t enabled)
{
    return ioctl(mDisplayFd, S3CFB_SET_VSYNC_INT, &enabled);
}

int32_t ExynosDisplayFbInterface::getDisplayAttribute(
        hwc2_config_t config,
        int32_t attribute, int32_t* outValue)
{
    switch (attribute) {
    case HWC2_ATTRIBUTE_VSYNC_PERIOD:
        *outValue = mExynosDisplay->mDisplayConfigs[config].vsyncPeriod;
        break;

    case HWC2_ATTRIBUTE_WIDTH:
        *outValue = mExynosDisplay->mDisplayConfigs[config].width;
        break;

    case HWC2_ATTRIBUTE_HEIGHT:
        *outValue = mExynosDisplay->mDisplayConfigs[config].height;
        break;

    case HWC2_ATTRIBUTE_DPI_X:
        *outValue = mExynosDisplay->mDisplayConfigs[config].Xdpi;
        break;

    case HWC2_ATTRIBUTE_DPI_Y:
        *outValue = mExynosDisplay->mDisplayConfigs[config].Ydpi;
        break;
    default:
        ALOGE("unknown display attribute %u", attribute);
        return HWC2_ERROR_BAD_CONFIG;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::getDisplayConfigs(
        uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs)
{
    if (outConfigs == NULL)
        *outNumConfigs = mExynosDisplay->mDisplayConfigs.size();
    else if (*outNumConfigs >= 1)
        for (int i = 0; i<*outNumConfigs; i++)
            outConfigs[i] = i;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::setActiveConfig(hwc2_config_t config)
{
    int32_t ret = NO_ERROR;
    if ((mExynosDisplay->mDisplayConfigs.size() <= 1) && (config != 0))
        ret = HWC2_ERROR_BAD_CONFIG;
    else if ((mExynosDisplay->mDisplayConfigs.size() == 1) && (config == 0))
        ret = HWC2_ERROR_NONE;
    else {

#ifdef USES_HWC_CPU_PERF_MODE
        displayConfigs_t _config = mExynosDisplay->mDisplayConfigs[config];
        struct decon_win_config_data win_data;
        struct decon_win_config *config = win_data.config;
        memset(&win_data, 0, sizeof(win_data));

        config[DECON_WIN_UPDATE_IDX].state = decon_win_config::DECON_WIN_STATE_MRESOL;
        config[DECON_WIN_UPDATE_IDX].dst.f_w = _config.width;
        config[DECON_WIN_UPDATE_IDX].dst.f_h = _config.height;
        config[DECON_WIN_UPDATE_IDX].plane_alpha = (int)(1000000000 / _config.vsyncPeriod);
        win_data.fps = (int)(1000000000 / _config.vsyncPeriod);

        if ((ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, &win_data)) < 0) {
            ALOGE("S3CFB_WIN_CONFIG failed errno : %d", errno);
            return HWC2_ERROR_BAD_CONFIG;
        }

        mExynosDisplay->mXres = config[DECON_WIN_UPDATE_IDX].dst.f_w;
        mExynosDisplay->mYres = config[DECON_WIN_UPDATE_IDX].dst.f_h;
        mExynosDisplay->mVsyncPeriod = (int)(1000000000 / win_data.fps);
        mExynosDisplay->mXdpi = 1000 * (mExynosDisplay->mXres * 25.4f) / config[DECON_WIN_UPDATE_IDX].dst.f_w;
        mExynosDisplay->mYdpi = 1000 * (mExynosDisplay->mYres * 25.4f) / config[DECON_WIN_UPDATE_IDX].dst.f_h;
        ALOGI("Display config changed to : %dx%d, %dms, %d Xdpi, %d Ydpi",
                mExynosDisplay->mXres, mExynosDisplay->mYres,
                mExynosDisplay->mVsyncPeriod,
                mExynosDisplay->mXdpi, mExynosDisplay->mYdpi);
#endif
    }

    mActiveConfig = config;

    return ret;
}

void ExynosDisplayFbInterface::dumpDisplayConfigs()
{
}

int32_t ExynosDisplayFbInterface::getColorModes(
        uint32_t* outNumModes,
        int32_t* outModes)
{
    uint32_t colorModeNum = 0;
    int32_t ret = 0;
    if ((ret = ioctl(mDisplayFd, EXYNOS_GET_COLOR_MODE_NUM, &colorModeNum )) < 0) {
        *outNumModes = 1;

        ALOGI("%s:: is not supported", __func__);
        if (outModes != NULL) {
            outModes[0] = HAL_COLOR_MODE_NATIVE;
        }
        return HWC2_ERROR_NONE;
    }

    if (outModes == NULL) {
        ALOGI("%s:: Supported color modes (%d)", __func__, colorModeNum);
        *outNumModes = colorModeNum;
        return HWC2_ERROR_NONE;
    }

    if (*outNumModes != colorModeNum) {
        ALOGE("%s:: invalid outNumModes(%d), should be(%d)", __func__, *outNumModes, colorModeNum);
        return -EINVAL;
    }

    for (uint32_t i= 0 ; i < colorModeNum; i++) {
        struct decon_color_mode_info colorMode;
        colorMode.index = i;
        if ((ret = ioctl(mDisplayFd, EXYNOS_GET_COLOR_MODE, &colorMode )) < 0) {
            return HWC2_ERROR_UNSUPPORTED;
        }
        ALOGI("\t[%d] mode %d", i, colorMode.color_mode);
        outModes[i] = colorMode.color_mode;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::setColorMode(int32_t mode)
{
    return ioctl(mDisplayFd, EXYNOS_SET_COLOR_MODE, &mode);
}

int32_t ExynosDisplayFbInterface::setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos) {
    struct decon_user_window win_pos;
    win_pos.x = x_pos;
    win_pos.y = y_pos;
    return ioctl(this->mDisplayFd, S3CFB_WIN_POSITION, &win_pos);
}

int32_t ExynosDisplayFbInterface::getHdrCapabilities(uint32_t* outNumTypes,
        int32_t* outTypes, float* outMaxLuminance,
        float* outMaxAverageLuminance, float* outMinLuminance)
{

    if (outTypes == NULL) {
        struct decon_hdr_capabilities_info outInfo;
        memset(&outInfo, 0, sizeof(outInfo));
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES_NUM, &outInfo) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES_NUM ioctl failed");
            return -1;
        }

        *outMaxLuminance = (float)outInfo.max_luminance / (float)10000;
        *outMaxAverageLuminance = (float)outInfo.max_average_luminance / (float)10000;
        *outMinLuminance = (float)outInfo.min_luminance / (float)10000;
        *outNumTypes = outInfo.out_num;
        // Save to member variables
        mExynosDisplay->mHdrTypeNum = *outNumTypes;
        mExynosDisplay->mMaxLuminance = *outMaxLuminance;
        mExynosDisplay->mMaxAverageLuminance = *outMaxAverageLuminance;
        mExynosDisplay->mMinLuminance = *outMinLuminance;
        ALOGI("%s: hdrTypeNum(%d), maxLuminance(%f), maxAverageLuminance(%f), minLuminance(%f)",
                mExynosDisplay->mDisplayName.string(), mExynosDisplay->mHdrTypeNum, mExynosDisplay->mMaxLuminance,
                mExynosDisplay->mMaxAverageLuminance, mExynosDisplay->mMinLuminance);
        return 0;
    }

    struct decon_hdr_capabilities outData;
    memset(&outData, 0, sizeof(outData));

    for (uint32_t i = 0; i < *outNumTypes; i += SET_HDR_CAPABILITIES_NUM) {
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES, &outData) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES ioctl Failed");
            return -1;
        }
        for (uint32_t j = 0; j < *outNumTypes - i; j++)
            outTypes[i+j] = outData.out_types[j];
        // Save to member variables
        mExynosDisplay->mHdrTypes[i] = (android_hdr_t)outData.out_types[i];
        HDEBUGLOGD(eDebugHWC, "%s HWC2: Types : %d",
                mExynosDisplay->mDisplayName.string(), mExynosDisplay->mHdrTypes[i]);
    }
    return 0;
}

decon_idma_type ExynosDisplayFbInterface::getDeconDMAType(ExynosMPP* __unused otfMPP)
{
    return MAX_DECON_DMA_TYPE;
}

int32_t ExynosDisplayFbInterface::deliverWinConfigData()
{
    int32_t ret = 0;
    android::String8 result;
    clearFbWinConfigData(mFbConfigData);
    struct decon_win_config *config = mFbConfigData.config;
    for (uint32_t i = 0; i < NUM_HW_WINDOWS; i++) {
        exynos_win_config_data &display_config = mExynosDisplay->mDpuData.configs[i];

        if (display_config.state == display_config.WIN_STATE_DISABLED)
            continue;

        config[i].dst = display_config.dst;
        config[i].plane_alpha = 255;
        if ((display_config.plane_alpha >= 0) && (display_config.plane_alpha < 255)) {
            config[i].plane_alpha = display_config.plane_alpha;
        }
        if ((config[i].blending = halBlendingToDpuBlending(display_config.blending))
                >= DECON_BLENDING_MAX) {
            HWC_LOGE(mExynosDisplay, "%s:: config [%d] has invalid blending(0x%8x)",
                    __func__, i, display_config.blending);
            return -EINVAL;
        }

        if (display_config.assignedMPP == NULL) {
            HWC_LOGE(mExynosDisplay, "%s:: config [%d] has invalid idma_type, assignedMPP is NULL",
                    __func__, i);
            return -EINVAL;
        } else if ((config[i].idma_type = getDeconDMAType(display_config.assignedMPP))
                == MAX_DECON_DMA_TYPE) {
            HWC_LOGE(mExynosDisplay, "%s:: config [%d] has invalid idma_type, assignedMPP(%s)",
                    __func__, i, display_config.assignedMPP->mName.string());
            return -EINVAL;
        }

        if (display_config.state == display_config.WIN_STATE_COLOR) {
            config[i].state = config[i].DECON_WIN_STATE_COLOR;
            config[i].color = display_config.color;
            if (!((display_config.plane_alpha >= 0) && (display_config.plane_alpha <= 255)))
                config[i].plane_alpha = 0;
        } else if ((display_config.state == display_config.WIN_STATE_BUFFER) ||
                   (display_config.state == display_config.WIN_STATE_CURSOR)) {
            if (display_config.state == display_config.WIN_STATE_BUFFER)
                config[i].state = config[i].DECON_WIN_STATE_BUFFER;
            else
                config[i].state = config[i].DECON_WIN_STATE_CURSOR;

            config[i].fd_idma[0] = display_config.fd_idma[0];
            config[i].fd_idma[1] = display_config.fd_idma[1];
            config[i].fd_idma[2] = display_config.fd_idma[2];
            config[i].acq_fence = display_config.acq_fence;
            config[i].rel_fence = display_config.rel_fence;
            if ((config[i].format = halFormatToDpuFormat(display_config.format))
                    == DECON_PIXEL_FORMAT_MAX) {
                HWC_LOGE(mExynosDisplay, "%s:: config [%d] has invalid format(0x%8x)",
                        __func__, i, display_config.format);
                return -EINVAL;
            }
            config[i].dpp_parm.comp_src = display_config.comp_src;
            config[i].dpp_parm.rot = (dpp_rotate)halTransformToDpuRot(display_config.transform);
            config[i].dpp_parm.eq_mode = halDataSpaceToDisplayParam(display_config);
            if (display_config.hdr_enable)
                config[i].dpp_parm.hdr_std = halTransferToDisplayParam(display_config);
            config[i].dpp_parm.min_luminance = display_config.min_luminance;
            config[i].dpp_parm.max_luminance = display_config.max_luminance;
            config[i].block_area = display_config.block_area;
            config[i].transparent_area = display_config.transparent_area;
            config[i].opaque_area = display_config.opaque_area;
            config[i].src = display_config.src;
            config[i].protection = display_config.protection;
            config[i].compression = display_config.compression;
        }
    }
    if (mExynosDisplay->mDpuData.enable_win_update) {
        size_t winUpdateInfoIdx = DECON_WIN_UPDATE_IDX;
        config[winUpdateInfoIdx].state = config[winUpdateInfoIdx].DECON_WIN_STATE_UPDATE;
        config[winUpdateInfoIdx].dst.x = mExynosDisplay->mDpuData.win_update_region.x;
        config[winUpdateInfoIdx].dst.w = mExynosDisplay->mDpuData.win_update_region.w;
        config[winUpdateInfoIdx].dst.y = mExynosDisplay->mDpuData.win_update_region.y;
        config[winUpdateInfoIdx].dst.h = mExynosDisplay->mDpuData.win_update_region.h;
    }

    if (mExynosDisplay->mDpuData.enable_readback)
        setReadbackConfig(config);

    dumpFbWinConfigInfo(result, mFbConfigData, true);

    {
        ATRACE_CALL();
        ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, &mFbConfigData);
    }

    if (ret) {
        result.clear();
        result.appendFormat("WIN_CONFIG ioctl error\n");
        HWC_LOGE(mExynosDisplay, "%s", dumpFbWinConfigInfo(result, mFbConfigData).string());
    } else {
        mExynosDisplay->mDpuData.retire_fence = mFbConfigData.retire_fence;
        struct decon_win_config *config = mFbConfigData.config;
        for (uint32_t i = 0; i < NUM_HW_WINDOWS; i++) {
            mExynosDisplay->mDpuData.configs[i].rel_fence = config[i].rel_fence;
        }
        if (mExynosDisplay->mDpuData.enable_readback && (DECON_READBACK_IDX >= 0)) {
            if (config[DECON_READBACK_IDX].acq_fence >= 0) {
                mExynosDisplay->setReadbackBufferAcqFence(config[DECON_READBACK_IDX].acq_fence);
            }
        }
    }

    return ret;
}

int32_t ExynosDisplayFbInterface::clearDisplay(bool readback)
{
    int ret = 0;

    struct decon_win_config_data win_data;
    memset(&win_data, 0, sizeof(win_data));
    win_data.retire_fence = -1;
    struct decon_win_config *config = win_data.config;
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        config[i].acq_fence = -1;
        config[i].rel_fence = -1;
    }

    uint32_t default_window = 0;
    if (SELECTED_LOGIC == UNDEFINED)
        default_window = mExynosDisplay->mBaseWindowIndex;
    else
        default_window = mExynosDisplay->mAssignedWindows[0];

#if defined(HWC_CLEARDISPLAY_WITH_COLORMAP)
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        if (i == default_window) {
            config[i].state = config[i].DECON_WIN_STATE_COLOR;
            config[i].idma_type = mExynosDisplay->mDefaultDMA;
            config[i].color = 0x0;
            config[i].dst.x = 0;
            config[i].dst.y = 0;
            config[i].dst.w = mExynosDisplay->mXres;
            config[i].dst.h = mExynosDisplay->mYres;
            config[i].dst.f_w = mExynosDisplay->mXres;
            config[i].dst.f_h = mExynosDisplay->mYres;
        }
        else
            config[i].state = config[i].DECON_WIN_STATE_DISABLED;
    }
#endif

    if (readback && mExynosDisplay->mDpuData.enable_readback)
        setReadbackConfig(config);

    win_data.retire_fence = -1;

    ret = ioctl(mDisplayFd, S3CFB_WIN_CONFIG, &win_data);
    if (ret < 0)
        HWC_LOGE(mExynosDisplay, "ioctl S3CFB_WIN_CONFIG failed to clear screen: %s",
                strerror(errno));

    if (readback && mExynosDisplay->mDpuData.enable_readback && (DECON_READBACK_IDX >= 0)) {
        if (config[DECON_READBACK_IDX].acq_fence >= 0) {
            mExynosDisplay->setReadbackBufferAcqFence(config[DECON_READBACK_IDX].acq_fence);
        }
        // DPU doesn't close rel_fence of readback buffer, HWC should close it
        if (mExynosDisplay->mDpuData.readback_info.rel_fence >= 0) {
            mExynosDisplay->mDpuData.readback_info.rel_fence =
                fence_close(mExynosDisplay->mDpuData.readback_info.rel_fence, mExynosDisplay,
                        FENCE_TYPE_READBACK_RELEASE, FENCE_IP_FB);
        }
    }

    if (win_data.retire_fence > 0)
        fence_close(win_data.retire_fence, mExynosDisplay, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
    return ret;
}

int32_t ExynosDisplayFbInterface::disableSelfRefresh(uint32_t disable)
{
    return ioctl(mDisplayFd, S3CFB_DECON_SELF_REFRESH, &disable);
}

int32_t ExynosDisplayFbInterface::setForcePanic()
{
    if (exynosHWCControl.forcePanic == 0)
        return NO_ERROR;

    usleep(20000);
    return ioctl(mDisplayFd, S3CFB_FORCE_PANIC, 0);
}

void ExynosDisplayFbInterface::clearFbWinConfigData(decon_win_config_data &winConfigData)
{
    memset(&winConfigData, 0, sizeof(winConfigData));
    winConfigData.fd_odma = -1;
    winConfigData.retire_fence = -1;
    struct decon_win_config *config = winConfigData.config;
    /* init */
    for (size_t i = 0; i < NUM_HW_WINDOWS; i++) {
        config[i].fd_idma[0] = -1;
        config[i].fd_idma[1] = -1;
        config[i].fd_idma[2] = -1;
        config[i].acq_fence = -1;
        config[i].rel_fence = -1;
    }
    if (DECON_WIN_UPDATE_IDX >= 0) {
        config[DECON_WIN_UPDATE_IDX].fd_idma[0] = -1;
        config[DECON_WIN_UPDATE_IDX].fd_idma[1] = -1;
        config[DECON_WIN_UPDATE_IDX].fd_idma[2] = -1;
        config[DECON_WIN_UPDATE_IDX].acq_fence = -1;
        config[DECON_WIN_UPDATE_IDX].rel_fence = -1;
    }
    if (DECON_READBACK_IDX >= 0) {
        config[DECON_READBACK_IDX].fd_idma[0] = -1;
        config[DECON_READBACK_IDX].fd_idma[1] = -1;
        config[DECON_READBACK_IDX].fd_idma[2] = -1;
        config[DECON_READBACK_IDX].acq_fence = -1;
        config[DECON_READBACK_IDX].rel_fence = -1;
    }
}

dpp_csc_eq ExynosDisplayFbInterface::halDataSpaceToDisplayParam(exynos_win_config_data& config)
{
    uint32_t cscEQ = 0;
    android_dataspace dataspace = config.dataspace;
    ExynosMPP* otfMPP = config.assignedMPP;
    if (dataspace == HAL_DATASPACE_UNKNOWN) {
        if (isFormatRgb(config.format))
            dataspace = HAL_DATASPACE_V0_SRGB;
    }
    uint32_t standard = (dataspace & HAL_DATASPACE_STANDARD_MASK);
    uint32_t range = (dataspace & HAL_DATASPACE_RANGE_MASK);

    if (otfMPP == NULL) {
        HWC_LOGE(mExynosDisplay, "%s:: assignedMPP is NULL", __func__);
        return (dpp_csc_eq)cscEQ;
    }

    if (dataspace_standard_map.find(standard) != dataspace_standard_map.end())
        cscEQ = dataspace_standard_map.at(standard).eq_mode;
    else
        cscEQ = CSC_UNSPECIFIED;

    if ((otfMPP->mAttr & MPP_ATTR_WCG) == 0) {
        switch(cscEQ) {
            case CSC_BT_709:
            case CSC_BT_601:
            case CSC_BT_2020:
            case CSC_DCI_P3:
                break;
            default:
                cscEQ = CSC_UNSPECIFIED;
                break;
        }
        switch(range) {
            case HAL_DATASPACE_RANGE_FULL:
            case HAL_DATASPACE_RANGE_LIMITED:
                break;
            default:
                range = HAL_DATASPACE_RANGE_UNSPECIFIED;
                break;
        }
    }

    if (dataspace_range_map.find(range) != dataspace_range_map.end())
        cscEQ |= (cscEQ | (dataspace_range_map.at(range)));
    else
        cscEQ |= (cscEQ | (CSC_RANGE_UNSPECIFIED << CSC_RANGE_SHIFT));

    return (dpp_csc_eq)cscEQ;
}

dpp_hdr_standard ExynosDisplayFbInterface::halTransferToDisplayParam(exynos_win_config_data& config)
{
    android_dataspace dataspace = config.dataspace;
    ExynosMPP* otfMPP = config.assignedMPP;

    if (dataspace == HAL_DATASPACE_UNKNOWN) {
        if (isFormatRgb(config.format))
            dataspace = HAL_DATASPACE_V0_SRGB;
    }

    uint32_t transfer = (dataspace & HAL_DATASPACE_TRANSFER_MASK);
    dpp_hdr_standard ret = DPP_HDR_OFF;

    if (otfMPP == NULL) return ret;

    if ((otfMPP->mAttr & MPP_ATTR_WCG) == 0) {
        if (hasHdrInfo(dataspace) == false)
            return DPP_HDR_OFF;
    }

    if (((otfMPP->mAttr & MPP_ATTR_HDR10) == 0) &&
        ((otfMPP->mAttr & MPP_ATTR_WCG) == 0) &&
        ((otfMPP->mAttr & MPP_ATTR_HDR10PLUS) == 0)) return DPP_HDR_OFF;

    if (dataspace_transfer_map.find(transfer) != dataspace_transfer_map.end())
        ret = dataspace_transfer_map.at(transfer).hdr_std;

    return ret;
}

android_dataspace ExynosDisplayFbInterface::dpuDataspaceToHalDataspace(uint32_t dpu_dataspace)
{
    uint32_t hal_dataspace = HAL_DATASPACE_UNKNOWN;

    uint32_t display_standard =
        (dpu_dataspace | DPU_DATASPACE_STANDARD_MASK);
    uint32_t display_range =
        (dpu_dataspace | DPU_DATASPACE_RANGE_MASK);
    uint32_t display_transfer =
        ((dpu_dataspace | DPU_DATASPACE_TRANSFER_MASK) >> DPU_DATASPACE_TRANSFER_SHIFT);

    for (auto it = dataspace_standard_map.begin(); it != dataspace_standard_map.end(); it++)
    {
        if (it->second.eq_mode == display_standard)
        {
            hal_dataspace |= it->first;
            break;
        }
    }

    for (auto it = dataspace_range_map.begin(); it != dataspace_range_map.end(); it++)
    {
        if (it->second == display_range)
        {
            hal_dataspace |= it->first;
            break;
        }
    }

    for (auto it = dataspace_transfer_map.begin(); it != dataspace_transfer_map.end(); it++)
    {
        if (it->second.hdr_std == display_transfer)
        {
            hal_dataspace |= it->first;
            break;
        }
    }

    return (android_dataspace)hal_dataspace;
}

String8& ExynosDisplayFbInterface::dumpFbWinConfigInfo(String8 &result,
        decon_win_config_data &fbConfig, bool debugPrint)
{
    /* print log only if eDebugDisplayInterfaceConfig flag is set when debugPrint is true */
    if (debugPrint &&
        (hwcCheckDebugMessages(eDebugDisplayInterfaceConfig) == false))
        return result;

    result.appendFormat("retire_fence(%d)\n", mFbConfigData.retire_fence);
    struct decon_win_config *config = mFbConfigData.config;
    for (uint32_t i = 0; i <= NUM_HW_WINDOWS; i++) {
        decon_win_config &c = config[i];
        String8 configString;
        configString.appendFormat("win[%d] state = %u\n", i, c.state);
        if (c.state == c.DECON_WIN_STATE_COLOR) {
            configString.appendFormat("\t\tx = %d, y = %d, width = %d, height = %d, color = %u, alpha = %u\n",
                    c.dst.x, c.dst.y, c.dst.w, c.dst.h, c.color, c.plane_alpha);
        } else/* if (c.state != c.DECON_WIN_STATE_DISABLED) */{
            configString.appendFormat("\t\tidma = %d, fd = (%d, %d, %d), acq_fence = %d, rel_fence = %d "
                    "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                    "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                    "format = %u, pa = %d, rot = %d, eq_mode = 0x%8x, hdr_std = %d, blending = %u, "
                    "protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                    "block(x:%d, y:%d, w:%d, h:%d)\n",
                    c.idma_type, c.fd_idma[0], c.fd_idma[1], c.fd_idma[2],
                    c.acq_fence, c.rel_fence,
                    c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                    c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                    c.format, c.plane_alpha, c.dpp_parm.rot, c.dpp_parm.eq_mode,
                    c.dpp_parm.hdr_std, c.blending, c.protection,
                    c.compression, c.dpp_parm.comp_src,
                    c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                    c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
        }

        if (debugPrint)
            ALOGD("%s", configString.string());
        else
            result.append(configString);
    }
    return result;
}

uint32_t ExynosDisplayFbInterface::getMaxWindowNum()
{
    return NUM_HW_WINDOWS;
}

int32_t ExynosDisplayFbInterface::setColorTransform(const float* matrix, int32_t hint)
{
    struct decon_color_transform_info transform_info;
    transform_info.hint = hint;
    for (uint32_t i = 0; i < DECON_MATRIX_ELEMENT_NUM; i++) {
        transform_info.matrix[i] = (int)round(matrix[i] * 65536);
    }
    int ret = ioctl(mDisplayFd, EXYNOS_SET_COLOR_TRANSFORM, &transform_info);
    if (ret < 0) {
        ALOGE("%s:: is not supported", __func__);
        // Send initialization matrix
        uint32_t row = (uint32_t)sqrt(DECON_MATRIX_ELEMENT_NUM);
        for (uint32_t i = 0; i < DECON_MATRIX_ELEMENT_NUM; i++) {
            if (i % (row + 1) == 0)
                transform_info.matrix[i] = 65536;
            else
                transform_info.matrix[i] = 0;
        }
        int reset_ret = ioctl(mDisplayFd, EXYNOS_SET_COLOR_TRANSFORM, &transform_info);
        if (reset_ret < 0)
            ALOGE("%s:: init ioctl is not operated", __func__);
        return HWC2_ERROR_UNSUPPORTED;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::getRenderIntents(int32_t mode, uint32_t* outNumIntents,
        int32_t* outIntents) {
    struct decon_render_intents_num_info intents_num_info;
    intents_num_info.color_mode = mode;
    int32_t ret = 0;
    if ((ret = ioctl(mDisplayFd, EXYNOS_GET_RENDER_INTENTS_NUM, &intents_num_info )) < 0) {
        *outNumIntents = 1;

        ALOGI("%s:: is not supported", __func__);
        if (outIntents != NULL) {
            outIntents[0] = HAL_RENDER_INTENT_COLORIMETRIC;
        }
        return HWC2_ERROR_NONE;
    }

    if (outIntents == NULL) {
        ALOGI("%s:: Supported intent (mode: %d, num: %d)",
                __func__, mode, intents_num_info.render_intent_num);
        *outNumIntents = intents_num_info.render_intent_num;
        return HWC2_ERROR_NONE;
    }

    if (*outNumIntents != intents_num_info.render_intent_num) {
        ALOGE("%s:: invalid outIntents(mode: %d, num: %d), should be(%d)",
                __func__, mode, *outIntents, intents_num_info.render_intent_num);
        return -EINVAL;
    }

    for (uint32_t i= 0 ; i < intents_num_info.render_intent_num; i++) {
        struct decon_render_intent_info render_intent;
        render_intent.color_mode = mode;
        render_intent.index = i;
        if ((ret = ioctl(mDisplayFd, EXYNOS_GET_RENDER_INTENT, &render_intent )) < 0) {
            return HWC2_ERROR_UNSUPPORTED;
        }
        ALOGI("\t[%d] intent %d", i, render_intent.render_intent);
        outIntents[i] = render_intent.render_intent;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::setColorModeWithRenderIntent(int32_t mode, int32_t intent)
{
    struct decon_color_mode_with_render_intent_info color_info;
    color_info.color_mode = mode;
    color_info.render_intent = intent;

    if (ioctl(mDisplayFd, EXYNOS_SET_COLOR_MODE_WITH_RENDER_INTENT, &color_info) < 0) {
        ALOGI("%s:: is not supported", __func__);
    }
    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplayFbInterface::getReadbackBufferAttributes(
        int32_t* /*android_pixel_format_t*/ outFormat,
        int32_t* /*android_dataspace_t*/ outDataspace)
{
    int32_t ret = NO_ERROR;
    struct decon_readback_attribute readback_attribute;
    readback_attribute.format = 0;
    readback_attribute.dataspace = 0;
    if ((ret = ioctl(mDisplayFd, EXYNOS_GET_READBACK_ATTRIBUTE,
                    &readback_attribute)) == NO_ERROR)
    {
        *outFormat = DpuFormatToHalFormat(readback_attribute.format);
        *outDataspace = dpuDataspaceToHalDataspace(readback_attribute.dataspace);
    }
    return ret;
}

void ExynosDisplayFbInterface::setReadbackConfig(decon_win_config *config)
{
    if (mExynosDisplay->mDpuData.readback_info.handle != NULL) {
        HWC_LOGE(mExynosDisplay, "%s:: readback is enabled but readback buffer is NULL", __func__);
    } else if (DECON_READBACK_IDX < 0) {
        HWC_LOGE(mExynosDisplay, "%s:: readback is enabled but window index for readback is not defined", __func__);
        if (mExynosDisplay->mDpuData.readback_info.rel_fence >= 0) {
            mExynosDisplay->mDpuData.readback_info.rel_fence =
                fence_close(mExynosDisplay->mDpuData.readback_info.rel_fence, mExynosDisplay,
                        FENCE_TYPE_READBACK_RELEASE, FENCE_IP_FB);
        }
        mExynosDisplay->mDpuData.enable_readback = false;
    } else {
        config[DECON_READBACK_IDX].state = config[DECON_READBACK_IDX].DECON_WIN_STATE_BUFFER;
        config[DECON_READBACK_IDX].fd_idma[0] =
            mExynosDisplay->mDpuData.readback_info.handle->fd;
        config[DECON_READBACK_IDX].fd_idma[1] =
            mExynosDisplay->mDpuData.readback_info.handle->fd1;
        config[DECON_READBACK_IDX].fd_idma[2] =
            mExynosDisplay->mDpuData.readback_info.handle->fd2;
        /*
         * This will be closed by ExynosDisplay::setReleaseFences()
         * after HWC delivers fence because display driver doesn't close it
         */
        config[DECON_READBACK_IDX].rel_fence = mExynosDisplay->mDpuData.readback_info.rel_fence;
    }
}

uint8_t EDID_SAMPLE[] =
{
    0x00 ,0xFF ,0xFF ,0xFF ,0xFF ,0xFF ,0xFF ,0x00 ,0x4C ,0x2D ,0x20 ,0x20 ,0x24 ,0x42 ,0x34 ,0x01,
    0xFF ,0x12 ,0x01 ,0x04 ,0xA4 ,0x00 ,0x02 ,0x64 ,0x1C ,0x60 ,0x41 ,0xA6 ,0x56 ,0x4A ,0x9C ,0x25,
    0x12 ,0x50 ,0x54 ,0x00 ,0x00 ,0x00 ,0x95 ,0x00 ,0x01 ,0x00 ,0x01 ,0x00 ,0x01 ,0x00 ,0x01 ,0x01,
    0x01 ,0x01 ,0x01 ,0x01 ,0x01 ,0x01 ,0x7C ,0x2E ,0xA0 ,0xA0 ,0x50 ,0xE0 ,0x1E ,0xB0 ,0x30 ,0x20,
    0x36 ,0x00 ,0xB1 ,0x0F ,0x11 ,0x00 ,0x00 ,0x1A ,0x00 ,0x00 ,0x00 ,0xFD ,0x00 ,0x38 ,0x4B ,0x1E,
    0x51 ,0x0E ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0xFC ,0x00 ,0x53,
    0x79 ,0x6E ,0x63 ,0x4D ,0x61 ,0x73 ,0x74 ,0x65 ,0x72 ,0x0A ,0x20 ,0x20 ,0x00 ,0x00 ,0x00 ,0xFF,
    0x00 ,0x48 ,0x56 ,0x43 ,0x51 ,0x37 ,0x33 ,0x32 ,0x39 ,0x30 ,0x36 ,0x0A ,0x20 ,0x20 ,0x00 ,0x1D,
};

int32_t ExynosDisplayFbInterface::getDisplayIdentificationData(uint8_t* outPort,
        uint32_t* outDataSize, uint8_t* outData)
{
    if (outData == NULL) {
        *outPort = mExynosDisplay->mDisplayId;
        if (ioctl(mDisplayFd, EXYNOS_GET_EDID, &mEdidData) < 0){
            ALOGI("%s: display(%d) cannot support EDID info", __func__, mExynosDisplay->mDisplayId);
            mEdidData.size = sizeof(EDID_SAMPLE);
            memcpy(mEdidData.edid_data, EDID_SAMPLE, sizeof(EDID_SAMPLE));
        }
        *outDataSize = mEdidData.size;
        return HWC2_ERROR_NONE;
    }

    memcpy(outData, mEdidData.edid_data, mEdidData.size);

    return HWC2_ERROR_NONE;
}

//////////////////////////////////////////////////// ExynosPrimaryDisplayFbInterface //////////////////////////////////////////////////////////////////

ExynosPrimaryDisplayFbInterface::ExynosPrimaryDisplayFbInterface(ExynosDisplay *exynosDisplay)
    : ExynosDisplayFbInterface(exynosDisplay)
{
    mPrimaryDisplay = NULL;
}

void ExynosPrimaryDisplayFbInterface::init(ExynosDisplay *exynosDisplay)
{
    mDisplayFd = open(exynosDisplay->mDeconNodeName, O_RDWR);
    if (mDisplayFd < 0)
        ALOGE("%s:: %s failed to open framebuffer", __func__, exynosDisplay->mDisplayName.string());

    mExynosDisplay = exynosDisplay;
    mPrimaryDisplay = (ExynosPrimaryDisplay *)exynosDisplay;

    getDisplayHWInfo();
    getDisplayConfigsFromDPU();
}

int32_t ExynosPrimaryDisplayFbInterface::setPowerMode(int32_t mode)
{
    int32_t ret = NO_ERROR;
    int fb_blank = -1;
    if (mode == HWC_POWER_MODE_DOZE ||
        mode == HWC_POWER_MODE_DOZE_SUSPEND) {
        if (mPrimaryDisplay->mPowerModeState != HWC_POWER_MODE_DOZE &&
            mPrimaryDisplay->mPowerModeState != HWC_POWER_MODE_OFF &&
            mPrimaryDisplay->mPowerModeState != HWC_POWER_MODE_DOZE_SUSPEND) {
            fb_blank = FB_BLANK_POWERDOWN;
        }
    } else if (mode == HWC_POWER_MODE_OFF) {
        fb_blank = FB_BLANK_POWERDOWN;
    } else {
        fb_blank = FB_BLANK_UNBLANK;
    }

    if (fb_blank >= 0) {
        if ((ret = ioctl(mDisplayFd, FBIOBLANK, fb_blank)) < 0) {
            ALOGE("FB BLANK ioctl failed errno : %d", errno);
            return ret;
        }
    }

    if ((ret = ioctl(mDisplayFd, S3CFB_POWER_MODE, &mode)) < 0) {
        ALOGE("Need to check S3CFB power mode ioctl : %d", errno);
        return ret;
    }

    return ret;
}

void ExynosPrimaryDisplayFbInterface::getDisplayHWInfo() {

    int refreshRate = 0;

    /* get PSR info */
    FILE *psrInfoFd;
    int psrMode;
    int panelType;
    uint64_t refreshCalcFactor = 0;

    /* Get screen info from Display DD */
    struct fb_var_screeninfo info;

    if (ioctl(mDisplayFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("FBIOGET_VSCREENINFO ioctl failed: %s", strerror(errno));
        goto err_ioctl;
    }

    if (info.reserved[0] == 0 && info.reserved[1] == 0) {
        info.reserved[0] = info.xres;
        info.reserved[1] = info.yres;

        if (ioctl(mDisplayFd, FBIOPUT_VSCREENINFO, &info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO ioctl failed: %s", strerror(errno));
            goto err_ioctl;
        }
    }

    struct decon_disp_info disp_info;
    disp_info.ver = HWC_2_0;

    if (ioctl(mDisplayFd, EXYNOS_DISP_INFO, &disp_info) == -1) {
        ALOGI("EXYNOS_DISP_INFO ioctl failed: %s", strerror(errno));
        goto err_ioctl;
    } else {
        ALOGI("HWC2: %d, psr_mode : %d", disp_info.ver, disp_info.psr_mode);
    }

    mPrimaryDisplay->mXres = info.reserved[0];
    mPrimaryDisplay->mYres = info.reserved[1];

    /* Support Multi-resolution scheme */
    {
        mPrimaryDisplay->mDeviceXres = mPrimaryDisplay->mXres;
        mPrimaryDisplay->mDeviceYres = mPrimaryDisplay->mYres;
        mPrimaryDisplay->mNewScaledWidth = mPrimaryDisplay->mXres;
        mPrimaryDisplay->mNewScaledHeight = mPrimaryDisplay->mYres;

        mPrimaryDisplay->mResolutionInfo.nNum = 1;
        mPrimaryDisplay->mResolutionInfo.nResolution[0].w = 1440;
        mPrimaryDisplay->mResolutionInfo.nResolution[0].h = 2960;
    }
    refreshCalcFactor = uint64_t( info.upper_margin + info.lower_margin + mPrimaryDisplay->mYres + info.vsync_len )
                        * ( info.left_margin  + info.right_margin + mPrimaryDisplay->mXres + info.hsync_len )
                        * info.pixclock;

    if (refreshCalcFactor)
        refreshRate = 1000000000000LLU / refreshCalcFactor;

    if (refreshRate == 0) {
        ALOGW("invalid refresh rate, assuming 60 Hz");
        refreshRate = 60;
    }

    mPrimaryDisplay->mXdpi = 1000 * (mPrimaryDisplay->mXres * 25.4f) / info.width;
    mPrimaryDisplay->mYdpi = 1000 * (mPrimaryDisplay->mYres * 25.4f) / info.height;
    mPrimaryDisplay->mVsyncPeriod = 1000000000 / refreshRate;

    ALOGD("using\n"
            "xres         = %d px\n"
            "yres         = %d px\n"
            "width        = %d mm (%f dpi)\n"
            "height       = %d mm (%f dpi)\n"
            "refresh rate = %d Hz\n",
            mPrimaryDisplay->mXres, mPrimaryDisplay->mYres, info.width,
            mPrimaryDisplay->mXdpi / 1000.0,
            info.height, mPrimaryDisplay->mYdpi / 1000.0, refreshRate);

    /* get PSR info */
    psrInfoFd = NULL;
    mPrimaryDisplay->mPsrMode = psrMode = PSR_MAX;
    panelType = PANEL_LEGACY;

    char devname[MAX_DEV_NAME + 1];
    devname[MAX_DEV_NAME] = '\0';

    strncpy(devname, VSYNC_DEV_PREFIX, MAX_DEV_NAME);
#if !defined(USES_DUAL_DISPLAY)
    strlcat(devname, PSR_DEV_NAME, MAX_DEV_NAME);
#else
    if (mPrimaryDisplay->mIndex == 0)
        strlcat(devname, PSR_DEV_NAME, MAX_DEV_NAME);
    else
        strlcat(devname, PSR_DEV_NAME_S, MAX_DEV_NAME);
#endif

    char psrDevname[MAX_DEV_NAME + 1];
    memset(psrDevname, 0, MAX_DEV_NAME + 1);

    strncpy(psrDevname, devname, strlen(devname) - 8);
    strlcat(psrDevname, "psr_info", MAX_DEV_NAME);
    ALOGI("PSR info devname = %s\n", psrDevname);

    psrInfoFd = fopen(psrDevname, "r");

    if (psrInfoFd == NULL)
        ALOGW("HWC needs to know whether LCD driver is using PSR mode or not\n");

    if (psrInfoFd != NULL) {
        char val[4];
        if (fread(&val, 1, 1, psrInfoFd) == 1) {
            mPrimaryDisplay->mPsrMode = psrMode = (0x03 & atoi(val));
        }
    } else {
        ALOGW("HWC needs to know whether LCD driver is using PSR mode or not (2nd try)\n");
    }

    ALOGI("PSR mode   = %d (0: video mode, 1: DP PSR mode, 2: MIPI-DSI command mode)\n",
            psrMode);

    if (psrInfoFd != NULL) {
        /* get DSC info */
        if (exynosHWCControl.multiResolution == true) {
            uint32_t panelModeCnt = 1;
            uint32_t sliceXSize = mPrimaryDisplay->mXres;
            uint32_t sliceYSize = mPrimaryDisplay->mYres;
            uint32_t xSize = mPrimaryDisplay->mXres;
            uint32_t ySize = mPrimaryDisplay->mYres;
            uint32_t panelType = PANEL_LEGACY;
            const int mode_limit = 3;
            ResolutionInfo &resolutionInfo = mPrimaryDisplay->mResolutionInfo;

            if (fscanf(psrInfoFd, "%u\n", &panelModeCnt) != 1) {
                ALOGE("Fail to read panel mode count");
            } else {
                ALOGI("res count : %u", panelModeCnt);
                if (panelModeCnt <= mode_limit) {
                    resolutionInfo.nNum = panelModeCnt;
                    for(uint32_t i = 0; i < panelModeCnt; i++) {
                        if (fscanf(psrInfoFd, "%d\n%d\n%d\n%d\n%d\n", &xSize, &ySize, &sliceXSize, &sliceYSize, &panelType) < 0) {
                            ALOGE("Fail to read slice information");
                        } else {
                            resolutionInfo.nResolution[i].w = xSize;
                            resolutionInfo.nResolution[i].h = ySize;
                            resolutionInfo.nDSCXSliceSize[i] = sliceXSize;
                            resolutionInfo.nDSCYSliceSize[i] = sliceYSize;
                            resolutionInfo.nPanelType[i] = panelType;
                            ALOGI("mode no. : %d, Width : %d, Height : %d, X_Slice_Size : %d, Y_Slice_Size : %d, Panel type : %d\n", i,
                                    resolutionInfo.nResolution[i].w,
                                    resolutionInfo.nResolution[i].h,
                                    resolutionInfo.nDSCXSliceSize[i],
                                    resolutionInfo.nDSCYSliceSize[i],
                                    resolutionInfo.nPanelType[i]);
                        }
                    }
                }
                mPrimaryDisplay->mDSCHSliceNum = mPrimaryDisplay->mXres / resolutionInfo.nDSCXSliceSize[0];
                mPrimaryDisplay->mDSCYSliceSize = resolutionInfo.nDSCYSliceSize[0];
            }
        } else {
            uint32_t sliceNum = 1;
            uint32_t sliceSize = mPrimaryDisplay->mYres;
            if (fscanf(psrInfoFd, "\n%d\n%d\n", &sliceNum, &sliceSize) < 0) {
                ALOGE("Fail to read slice information");
            } else {
                mPrimaryDisplay->mDSCHSliceNum = sliceNum;
                mPrimaryDisplay->mDSCYSliceSize = sliceSize;
            }
        }
        fclose(psrInfoFd);
    }

    mPrimaryDisplay->mDRDefault = (mPrimaryDisplay->mPsrMode == PSR_NONE);
    mPrimaryDisplay->mDREnable = mPrimaryDisplay->mDRDefault;

    ALOGI("DSC H_Slice_Num: %d, Y_Slice_Size: %d (for window partial update)",
            mPrimaryDisplay->mDSCHSliceNum, mPrimaryDisplay->mDSCYSliceSize);

    struct decon_hdr_capabilities_info outInfo;
    memset(&outInfo, 0, sizeof(outInfo));

    if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES_NUM, &outInfo) < 0) {
        ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES_NUM ioctl failed");
        goto err_ioctl;
    }


    // Save to member variables
    mPrimaryDisplay->mHdrTypeNum = outInfo.out_num;
    mPrimaryDisplay->mMaxLuminance = (float)outInfo.max_luminance / (float)10000;
    mPrimaryDisplay->mMaxAverageLuminance = (float)outInfo.max_average_luminance / (float)10000;
    mPrimaryDisplay->mMinLuminance = (float)outInfo.min_luminance / (float)10000;

    ALOGI("%s: hdrTypeNum(%d), maxLuminance(%f), maxAverageLuminance(%f), minLuminance(%f)",
            mPrimaryDisplay->mDisplayName.string(), mPrimaryDisplay->mHdrTypeNum,
            mPrimaryDisplay->mMaxLuminance, mPrimaryDisplay->mMaxAverageLuminance,
            mPrimaryDisplay->mMinLuminance);

    struct decon_hdr_capabilities outData;

    for (int i = 0; i < mPrimaryDisplay->mHdrTypeNum; i += SET_HDR_CAPABILITIES_NUM) {
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES, &outData) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES ioctl Failed");
            goto err_ioctl;
        }
        mPrimaryDisplay->mHdrTypes[i] = (android_hdr_t)outData.out_types[i];
        ALOGI("HWC2: Primary display's HDR Type(%d)",  mPrimaryDisplay->mHdrTypes[i]);
    }

    //TODO : shuld be set by valid number
    //mHdrTypes[0] = HAL_HDR_HDR10;

    return;

err_ioctl:
    return;
}

void ExynosPrimaryDisplayFbInterface::getDisplayConfigsFromDPU()
{
    int32_t num = 0;
    decon_display_mode mode;

    if (ioctl(mDisplayFd, EXYNOS_GET_DISPLAY_MODE_NUM, &num) < 0) {
        ALOGI("Not support EXYNOS_GET_DISPLAY_MODE_NUM : %s", strerror(errno));
        goto use_legacy;
    }

    if (num == 0)
        goto use_legacy;

    for (uint32_t i = 0; i < num; i++) {
        mode.index = i;
        if (ioctl(mDisplayFd, EXYNOS_GET_DISPLAY_MODE, &mode) < 0) {
            ALOGI("Not support EXYNOS_GET_DISPLAY_MODE: index(%d) %s", i, strerror(errno));
            goto use_legacy;
        }
        displayConfigs_t configs;
        configs.vsyncPeriod = 1000000000 / mode.fps;
        configs.width = mode.width;
        configs.height = mode.height;

        // TODO : Xdpi should be recalaulated
        configs.Xdpi = 1000 * (mode.width * 25.4f) / mode.mm_width;
        configs.Ydpi = 1000 * (mode.height * 25.4f) / mode.mm_height;

        /* TODO : add cpu affinity/clock settings here */
        configs.cpuIDs = 0;
        for(int cl_num = 0; cl_num < CPU_CLUSTER_CNT; cl_num++)
            configs.minClock[cl_num] = 0;

        ALOGI("Display config : %d, vsync : %d, width : %d, height : %d, xdpi : %d, ydpi : %d",
                i, configs.vsyncPeriod, configs.width, configs.height, configs.Xdpi, configs.Ydpi);

        mExynosDisplay->mDisplayConfigs.insert(std::make_pair(i,configs));
    }

    return;

use_legacy:
    displayConfigs_t configs;
    configs.vsyncPeriod = mExynosDisplay->mVsyncPeriod;
    configs.width = mExynosDisplay->mXres;
    configs.height = mExynosDisplay->mYres;
    configs.Xdpi = mExynosDisplay->mXdpi;
    configs.Ydpi = mExynosDisplay->mYdpi;
    configs.cpuIDs = 0;
    for(int cl_num = 0; cl_num < CPU_CLUSTER_CNT; cl_num++)
        configs.minClock[cl_num] = 0;
    mExynosDisplay->mDisplayConfigs.insert(std::make_pair(0,configs));

    return;
}

//////////////////////////////////////////////////// ExynosExternalDisplayFbInterface //////////////////////////////////////////////////////////////////

bool is_same_dv_timings(const struct v4l2_dv_timings *t1,
        const struct v4l2_dv_timings *t2)
{
    if (t1->type == t2->type &&
            t1->bt.width == t2->bt.width &&
            t1->bt.height == t2->bt.height &&
            t1->bt.interlaced == t2->bt.interlaced &&
            t1->bt.polarities == t2->bt.polarities &&
            t1->bt.pixelclock == t2->bt.pixelclock &&
            t1->bt.hfrontporch == t2->bt.hfrontporch &&
            t1->bt.vfrontporch == t2->bt.vfrontporch &&
            t1->bt.vsync == t2->bt.vsync &&
            t1->bt.vbackporch == t2->bt.vbackporch &&
            (!t1->bt.interlaced ||
             (t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
              t1->bt.il_vsync == t2->bt.il_vsync &&
              t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
        return true;
    return false;
}

int ExynosExternalDisplay::getDVTimingsIndex(int preset)
{
    for (int i = 0; i < SUPPORTED_DV_TIMINGS_NUM; i++) {
        if (preset == preset_index_mappings[i].preset)
            return preset_index_mappings[i].dv_timings_index;
    }
    return -1;
}

ExynosExternalDisplayFbInterface::ExynosExternalDisplayFbInterface(ExynosDisplay *exynosDisplay)
    : ExynosDisplayFbInterface(exynosDisplay)
{
    mExternalDisplay = NULL;
    memset(&dv_timings, 0, sizeof(dv_timings));
    cleanConfigurations();
}

void ExynosExternalDisplayFbInterface::init(ExynosDisplay *exynosDisplay)
{
    mExynosDisplay = exynosDisplay;
    mExternalDisplay = (ExynosExternalDisplay *)exynosDisplay;

    mDisplayFd = open(mExternalDisplay->mDeconNodeName, O_RDWR);
    if (mDisplayFd < 0)
        ALOGE("%s:: %s failed to open framebuffer", __func__, mExternalDisplay->mDisplayName.string());

    memset(&dv_timings, 0, sizeof(dv_timings));
}

int32_t ExynosExternalDisplayFbInterface::getDisplayAttribute(
        hwc2_config_t config,
        int32_t attribute, int32_t* outValue)
{
    if (config >= SUPPORTED_DV_TIMINGS_NUM) {
        HWC_LOGE(mExternalDisplay, "%s:: Invalid config(%d), mConfigurations(%zu)", __func__, config, mConfigurations.size());
        return -EINVAL;
    }

    v4l2_dv_timings dv_timing = dv_timings[config];
    switch(attribute) {
    case HWC2_ATTRIBUTE_VSYNC_PERIOD:
        {
            *outValue = calVsyncPeriod(dv_timing);
            break;
        }
    case HWC2_ATTRIBUTE_WIDTH:
        *outValue = dv_timing.bt.width;
        break;

    case HWC2_ATTRIBUTE_HEIGHT:
        *outValue = dv_timing.bt.height;
        break;

    case HWC2_ATTRIBUTE_DPI_X:
        *outValue = mExternalDisplay->mXdpi;
        break;

    case HWC2_ATTRIBUTE_DPI_Y:
        *outValue = mExternalDisplay->mYdpi;
        break;

    default:
        HWC_LOGE(mExternalDisplay, "%s unknown display attribute %u",
                mExternalDisplay->mDisplayName.string(), attribute);
        return HWC2_ERROR_BAD_CONFIG;
    }

    return HWC2_ERROR_NONE;
}


int32_t ExynosExternalDisplayFbInterface::getDisplayConfigs(
        uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs)
{
    int ret = 0;
    exynos_displayport_data dp_data;
    memset(&dp_data, 0, sizeof(dp_data));
    size_t index = 0;

    if (outConfigs != NULL) {
        while (index < *outNumConfigs) {
            outConfigs[index] = mConfigurations[index];
            index++;
        }

        dp_data.timings = dv_timings[outConfigs[0]];
        dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_PRESET;
        if(ioctl(this->mDisplayFd, EXYNOS_SET_DISPLAYPORT_CONFIG, &dp_data) <0) {
            HWC_LOGE(mExternalDisplay, "%s fail to send selected config data, %d",
                    mExternalDisplay->mDisplayName.string(), errno);
            return -1;
        }

        mExternalDisplay->mXres = dv_timings[outConfigs[0]].bt.width;
        mExternalDisplay->mYres = dv_timings[outConfigs[0]].bt.height;
        mExternalDisplay->mVsyncPeriod = calVsyncPeriod(dv_timings[outConfigs[0]]);
        HDEBUGLOGD(eDebugExternalDisplay, "ExternalDisplay is connected to (%d x %d, %d fps) sink",
                mExternalDisplay->mXres, mExternalDisplay->mYres, mExternalDisplay->mVsyncPeriod);

        dumpDisplayConfigs();

        return HWC2_ERROR_NONE;
    }

    memset(&dv_timings, 0, sizeof(dv_timings));
    cleanConfigurations();

    /* configs store the index of mConfigurations */
    dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_ENUM_PRESET;
    while (index < SUPPORTED_DV_TIMINGS_NUM) {
        dp_data.etimings.index = index;
        ret = ioctl(this->mDisplayFd, EXYNOS_GET_DISPLAYPORT_CONFIG, &dp_data);
        if (ret < 0) {
            if (errno == EINVAL) {
                HDEBUGLOGD(eDebugExternalDisplay, "%s:: Unmatched config index %zu", __func__, index);
                index++;
                continue;
            }
            else if (errno == E2BIG) {
                HDEBUGLOGD(eDebugExternalDisplay, "%s:: Total configurations %zu", __func__, index);
                break;
            }
            HWC_LOGE(mExternalDisplay, "%s: enum_dv_timings error, %d", __func__, errno);
            return -1;
        }

        dv_timings[index] = dp_data.etimings.timings;
        mConfigurations.push_back(index);
        index++;
    }

    if (mConfigurations.size() == 0){
        HWC_LOGE(mExternalDisplay, "%s do not receivce any configuration info",
                mExternalDisplay->mDisplayName.string());
        mExternalDisplay->closeExternalDisplay();
        return -1;
    }

    int config = 0;
    v4l2_dv_timings temp_dv_timings = dv_timings[mConfigurations[mConfigurations.size()-1]];
    for (config = 0; config < (int)mConfigurations[mConfigurations.size()-1]; config++) {
        if (dv_timings[config].bt.width != 0) {
            dv_timings[mConfigurations[mConfigurations.size()-1]] = dv_timings[config];
            break;
        }
    }

    dv_timings[config] = temp_dv_timings;
    mExternalDisplay->mActiveConfigIndex = config;

    *outNumConfigs = mConfigurations.size();

    return 0;
}

void ExynosExternalDisplayFbInterface::cleanConfigurations()
{
    mConfigurations.clear();
}

void ExynosExternalDisplayFbInterface::dumpDisplayConfigs()
{
    HDEBUGLOGD(eDebugExternalDisplay, "External display configurations:: total(%zu), active configuration(%d)",
            mConfigurations.size(), mExternalDisplay->mActiveConfigIndex);

    for (size_t i = 0; i <  mConfigurations.size(); i++ ) {
        unsigned int dv_timings_index = mConfigurations[i];
        v4l2_dv_timings configuration = dv_timings[dv_timings_index];
        float refresh_rate = (float)((float)configuration.bt.pixelclock /
                ((configuration.bt.width + configuration.bt.hfrontporch + configuration.bt.hsync + configuration.bt.hbackporch) *
                 (configuration.bt.height + configuration.bt.vfrontporch + configuration.bt.vsync + configuration.bt.vbackporch)));
        uint32_t vsyncPeriod = 1000000000 / refresh_rate;
        HDEBUGLOGD(eDebugExternalDisplay, "%zu : index(%d) type(%d), %d x %d, fps(%f), vsyncPeriod(%d)", i, dv_timings_index, configuration.type, configuration.bt.width,
                configuration.bt.height,
                refresh_rate, vsyncPeriod);
    }
}

int32_t ExynosExternalDisplayFbInterface::calVsyncPeriod(v4l2_dv_timings dv_timing)
{
    int32_t result;
    float refreshRate = (float)((float)dv_timing.bt.pixelclock /
            ((dv_timing.bt.width + dv_timing.bt.hfrontporch + dv_timing.bt.hsync + dv_timing.bt.hbackporch) *
             (dv_timing.bt.height + dv_timing.bt.vfrontporch + dv_timing.bt.vsync + dv_timing.bt.vbackporch)));

    result = (1000000000/refreshRate);
    return result;
}

int32_t ExynosExternalDisplayFbInterface::getHdrCapabilities(
        uint32_t* outNumTypes, int32_t* outTypes, float* outMaxLuminance,
        float* outMaxAverageLuminance, float* outMinLuminance)
{
    HDEBUGLOGD(eDebugExternalDisplay, "HWC2: %s, %d", __func__, __LINE__);
    if (outTypes == NULL) {
        struct decon_hdr_capabilities_info outInfo;
        memset(&outInfo, 0, sizeof(outInfo));

        exynos_displayport_data dp_data;
        memset(&dp_data, 0, sizeof(dp_data));
        dp_data.state = dp_data.EXYNOS_DISPLAYPORT_STATE_HDR_INFO;
        int ret = ioctl(mDisplayFd, EXYNOS_GET_DISPLAYPORT_CONFIG, &dp_data);
        if (ret < 0) {
            ALOGE("%s: EXYNOS_DISPLAYPORT_STATE_HDR_INFO ioctl error, %d", __func__, errno);
        }

        mExternalDisplay->mExternalHdrSupported = dp_data.hdr_support;
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES_NUM, &outInfo) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES_NUM ioctl failed");
            return -1;
        }

        if (mExternalDisplay->mExternalHdrSupported) {
            *outMaxLuminance = 50 * pow(2.0 ,(double)outInfo.max_luminance / 32);
            *outMaxAverageLuminance = 50 * pow(2.0 ,(double)outInfo.max_average_luminance / 32);
            *outMinLuminance = *outMaxLuminance * (float)pow(outInfo.min_luminance, 2.0) / pow(255.0, 2.0) / (float)100;
        }
        else {
            *outMaxLuminance = (float)outInfo.max_luminance / (float)10000;
            *outMaxAverageLuminance = (float)outInfo.max_average_luminance / (float)10000;
            *outMinLuminance = (float)outInfo.min_luminance / (float)10000;
        }

#ifndef USES_HDR_GLES_CONVERSION
        mExternalDisplay->mExternalHdrSupported = 0;
#endif

        *outNumTypes = outInfo.out_num;
        // Save to member variables
        mExternalDisplay->mHdrTypeNum = *outNumTypes;
        mExternalDisplay->mMaxLuminance = *outMaxLuminance;
        mExternalDisplay->mMaxAverageLuminance = *outMaxAverageLuminance;
        mExternalDisplay->mMinLuminance = *outMinLuminance;
        ALOGI("%s: hdrTypeNum(%d), maxLuminance(%f), maxAverageLuminance(%f), minLuminance(%f), externalHdrSupported(%d)",
                mExternalDisplay->mDisplayName.string(), mExternalDisplay->mHdrTypeNum,
                mExternalDisplay->mMaxLuminance, mExternalDisplay->mMaxAverageLuminance,
                mExternalDisplay->mMinLuminance, mExternalDisplay->mExternalHdrSupported);
        return 0;
    }

    struct decon_hdr_capabilities outData;
    memset(&outData, 0, sizeof(outData));

    for (uint32_t i = 0; i < *outNumTypes; i += SET_HDR_CAPABILITIES_NUM) {
        if (ioctl(mDisplayFd, S3CFB_GET_HDR_CAPABILITIES, &outData) < 0) {
            ALOGE("getHdrCapabilities: S3CFB_GET_HDR_CAPABILITIES ioctl Failed");
            return -1;
        }
        for (uint32_t j = 0; j < *outNumTypes - i; j++)
            outTypes[i+j] = outData.out_types[j];
        // Save to member variables
        mExternalDisplay->mHdrTypes[i] = (android_hdr_t)outData.out_types[i];
        HDEBUGLOGD(eDebugExternalDisplay, "HWC2: Types : %d", mExternalDisplay->mHdrTypes[i]);
    }
    return 0;
}
