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

#ifndef _EXYNOSDISPLAYINTERFACE_H
#define _EXYNOSDISPLAYINTERFACE_H

#include <sys/types.h>
#include <hardware/hwcomposer2.h>
#include <utils/Errors.h>

class ExynosDisplay;

using namespace android;
class ExynosDisplayInterface {
    protected:
        ExynosDisplay *mExynosDisplay = NULL;
        uint32_t mActiveConfig = 0;
    public:
        virtual ~ExynosDisplayInterface();
        virtual void init(ExynosDisplay* exynosDisplay) {mExynosDisplay = exynosDisplay; };
        virtual int32_t setPowerMode(int32_t __unused mode) {return NO_ERROR;};
        virtual int32_t setVsyncEnabled(uint32_t __unused enabled) {return NO_ERROR;};
        virtual int32_t getDisplayAttribute(
                hwc2_config_t __unused config,
                int32_t __unused attribute, int32_t* __unused outValue) {return NO_ERROR;};
        virtual int32_t getDisplayConfigs(
                uint32_t* outNumConfigs,
                hwc2_config_t* outConfigs);
        virtual void dumpDisplayConfigs() {};
        virtual int32_t getColorModes(
                uint32_t* outNumModes,
                int32_t* outModes);
        virtual int32_t setColorMode(int32_t __unused mode) {return NO_ERROR;};
        virtual int32_t setActiveConfig(hwc2_config_t __unused config) {return NO_ERROR;};
        virtual int32_t getActiveConfig(hwc2_config_t* outConfig);
        virtual int32_t setCursorPositionAsync(uint32_t __unused x_pos,
                uint32_t __unused y_pos) {return NO_ERROR;};
        virtual int32_t getHdrCapabilities(uint32_t* outNumTypes,
                int32_t* outTypes, float* outMaxLuminance,
                float* outMaxAverageLuminance, float* outMinLuminance);
        virtual int32_t deliverWinConfigData() {return NO_ERROR;};
        virtual int32_t clearDisplay(bool __unused readback = false) {return NO_ERROR;};
        virtual int32_t disableSelfRefresh(uint32_t __unused disable) {return NO_ERROR;};
        virtual int32_t setForcePanic();
        virtual int getDisplayFd() {return -1;};
        virtual uint32_t getMaxWindowNum() {return 0;};
        virtual int32_t setColorTransform(const float* __unused matrix,
                int32_t __unused hint) {return HWC2_ERROR_UNSUPPORTED;}
        virtual int32_t getRenderIntents(int32_t __unused mode, uint32_t* __unused outNumIntents,
                int32_t* __unused outIntents) {return 0;}
        virtual int32_t setColorModeWithRenderIntent(int32_t __unused mode, int32_t __unused intent) {return 0;}
        virtual int32_t getReadbackBufferAttributes(int32_t* /*android_pixel_format_t*/ outFormat,
                int32_t* /*android_dataspace_t*/ outDataspace);

        /* HWC 2.3 APIs */
        virtual int32_t getDisplayIdentificationData(uint8_t* __unused outPort,
                uint32_t* __unused outDataSize, uint8_t* __unused outData) {return 0;}
};

#endif
