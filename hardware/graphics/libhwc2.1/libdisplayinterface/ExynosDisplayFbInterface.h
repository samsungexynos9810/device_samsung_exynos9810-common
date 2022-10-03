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

#ifndef _EXYNOSDISPLAYFBINTERFACE_H
#define _EXYNOSDISPLAYFBINTERFACE_H

#include "ExynosDisplayInterface.h"
#include <sys/types.h>
#include <hardware/hwcomposer2.h>
#include <linux/videodev2.h>
#include "videodev2_exynos_displayport.h"
#include "ExynosDisplay.h"
#include "DeconHeader.h"

class ExynosDisplay;

/* dpu dataspace = trasnfer[13:9] | range[8:6] | standard[5:0] */
#define DPU_DATASPACE_STANDARD_SHIFT    0
#define DPU_DATASPACE_STANDARD_MASK     0x3f

#define DPU_DATASPACE_RANGE_SHIFT       6
#define DPU_DATASPACE_RANGE_MASK        0x1C0

#define DPU_DATASPACE_TRANSFER_SHIFT    9
#define DPU_DATASPACE_TRANSFER_MASK     0x3E0

class ExynosDisplayFbInterface : public ExynosDisplayInterface {
    public:
        ExynosDisplayFbInterface(ExynosDisplay *exynosDisplay);
        ~ExynosDisplayFbInterface();
        virtual void init(ExynosDisplay *exynosDisplay);
        virtual int32_t setPowerMode(int32_t mode);
        virtual int32_t setVsyncEnabled(uint32_t enabled);
        virtual int32_t getDisplayAttribute(
                hwc2_config_t config,
                int32_t attribute, int32_t* outValue);
        virtual int32_t getDisplayConfigs(
                uint32_t* outNumConfigs,
                hwc2_config_t* outConfigs);
        virtual void dumpDisplayConfigs();
        virtual int32_t getColorModes(
                uint32_t* outNumModes,
                int32_t* outModes);
        virtual int32_t setColorMode(int32_t mode);
        virtual int32_t setActiveConfig(hwc2_config_t config);
        virtual int32_t setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos);
        virtual int32_t getHdrCapabilities(uint32_t* outNumTypes,
                int32_t* outTypes, float* outMaxLuminance,
                float* outMaxAverageLuminance, float* outMinLuminance);
        virtual int32_t deliverWinConfigData();
        virtual int32_t clearDisplay(bool readback = false);
        virtual int32_t disableSelfRefresh(uint32_t disable);
        virtual int32_t setForcePanic();
        virtual int getDisplayFd() { return mDisplayFd; };
        virtual uint32_t getMaxWindowNum();
        virtual decon_idma_type getDeconDMAType(ExynosMPP *otfMPP);
        virtual int32_t setColorTransform(const float* matrix, int32_t hint);
        virtual int32_t getRenderIntents(int32_t mode,
                uint32_t* outNumIntents, int32_t* outIntents);
        virtual int32_t setColorModeWithRenderIntent(int32_t mode, int32_t intent);
        virtual int32_t getReadbackBufferAttributes(int32_t* /*android_pixel_format_t*/ outFormat,
                int32_t* /*android_dataspace_t*/ outDataspace);

        /* HWC 2.3 APIs */
        virtual int32_t getDisplayIdentificationData(uint8_t* outPort,
                uint32_t* outDataSize, uint8_t* outData);

    protected:
        void clearFbWinConfigData(decon_win_config_data &winConfigData);
        dpp_csc_eq halDataSpaceToDisplayParam(exynos_win_config_data& config);
        dpp_hdr_standard halTransferToDisplayParam(exynos_win_config_data& config);
        String8& dumpFbWinConfigInfo(String8 &result,
                decon_win_config_data &fbConfig, bool debugPrint = false);
        void setReadbackConfig(decon_win_config *config);
        android_dataspace dpuDataspaceToHalDataspace(uint32_t dpu_dataspace);
    protected:
        /**
         * LCD device member variables
         */
        int mDisplayFd;
        decon_win_config_data mFbConfigData;
        decon_edid_data mEdidData;
};

class ExynosPrimaryDisplay;
class ExynosPrimaryDisplayFbInterface: public ExynosDisplayFbInterface {
    public:
        ExynosPrimaryDisplayFbInterface(ExynosDisplay *exynosDisplay);
        virtual void init(ExynosDisplay *exynosDisplay);
        virtual int32_t setPowerMode(int32_t mode);
        void getDisplayHWInfo();
        void getDisplayConfigsFromDPU();
    protected:
        ExynosPrimaryDisplay *mPrimaryDisplay;
};

#include <utils/Vector.h>
#define SUPPORTED_DV_TIMINGS_NUM        100
#define DP_RESOLUTION_DEFAULT V4L2_DV_1080P60

struct preset_index_mapping {
    int preset;
    int dv_timings_index;
};

const struct preset_index_mapping preset_index_mappings[SUPPORTED_DV_TIMINGS_NUM] = {
    {V4L2_DV_480P59_94, 0}, // 720X480P59_94
    {V4L2_DV_576P50, 1},
    {V4L2_DV_720P50, 2},
    {V4L2_DV_720P60, 3},
    {V4L2_DV_1080P24, 4},
    {V4L2_DV_1080P25, 5},
    {V4L2_DV_1080P30, 6},
    {V4L2_DV_1080P50, 7},
    {V4L2_DV_1080P60, 8},
    {V4L2_DV_2160P24, 9},
    {V4L2_DV_2160P25, 10},
    {V4L2_DV_2160P30, 11},
    {V4L2_DV_2160P50, 12},
    {V4L2_DV_2160P60, 13},
    {V4L2_DV_2160P24_1, 14},
    {V4L2_DV_2160P25_1, 15},
    {V4L2_DV_2160P30_1, 16},
    {V4L2_DV_2160P50_1, 17},
    {V4L2_DV_2160P60_1, 18},
    {V4L2_DV_2160P59, 19},
    {V4L2_DV_480P60, 20}, // 640X480P60
    {V4L2_DV_1440P59, 21},
    {V4L2_DV_1440P60, 22},
    {V4L2_DV_800P60_RB, 23}, // 1280x800P60_RB
    {V4L2_DV_1024P60, 24}, // 1280x1024P60
    {V4L2_DV_1440P60_1, 25}, // 1920x1440P60
};

class ExynosExternalDisplay;
class ExynosExternalDisplayFbInterface: public ExynosDisplayFbInterface {
    public:
        ExynosExternalDisplayFbInterface(ExynosDisplay *exynosDisplay);
        virtual void init(ExynosDisplay *exynosDisplay);
        virtual int32_t getDisplayConfigs(
                uint32_t* outNumConfigs,
                hwc2_config_t* outConfigs);
        virtual void dumpDisplayConfigs();
        virtual int32_t getDisplayAttribute(
                hwc2_config_t config,
                int32_t attribute, int32_t* outValue);
        virtual int32_t getHdrCapabilities(uint32_t* outNumTypes,
                int32_t* outTypes, float* outMaxLuminance,
                float* outMaxAverageLuminance, float* outMinLuminance);
    protected:
        int32_t calVsyncPeriod(v4l2_dv_timings dv_timing);
        void cleanConfigurations();
    protected:
        ExynosExternalDisplay *mExternalDisplay;
        struct v4l2_dv_timings dv_timings[SUPPORTED_DV_TIMINGS_NUM];
        android::Vector< unsigned int > mConfigurations;
};
#endif
