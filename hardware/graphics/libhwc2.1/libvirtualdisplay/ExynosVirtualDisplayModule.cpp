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
#undef LOG_TAG
#define LOG_TAG "virtualdisplaymodule"

#include "ExynosVirtualDisplayModule.h"

ExynosVirtualDisplayModule::ExynosVirtualDisplayModule(uint32_t index, ExynosDevice *device)
    :   ExynosVirtualDisplay(index, device)
{
    mGLESFormat = HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M;

    if (device == NULL) {
        ALOGE("Display creation failed!");
    }

    mBaseWindowIndex = 0;
    mMaxWindowNum = 0;
}

ExynosVirtualDisplayModule::~ExynosVirtualDisplayModule ()
{

}

int32_t ExynosVirtualDisplayModule::getDisplayAttribute(
    hwc2_config_t __unused config,
    int32_t /*hwc2_attribute_t*/ __unused attribute, int32_t* __unused outValue)
{
    switch(attribute) {
        case HWC_DISPLAY_COMPOSITION_TYPE:
            *outValue = (int32_t)mCompositionType;
            break;
        case HWC_DISPLAY_GLES_FORMAT:
            *outValue = (int32_t)mGLESFormat;
            break;
        case HWC_DISPLAY_SINK_BQ_FORMAT:
            if (mIsSkipFrame)
                *outValue = (int32_t)0xFFFFFFFF;
            else if (mIsSecureDRM && !mIsSecureVDSState)
                *outValue = (int32_t)HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN;
            else
                *outValue = (int32_t)mGLESFormat;
            break;
        case HWC_DISPLAY_SINK_BQ_USAGE:
            *outValue = (int32_t)mSinkUsage;
            break;
        case HWC_DISPLAY_SINK_BQ_WIDTH:
            *outValue = (int32_t)mDisplayWidth;
            break;
        case HWC_DISPLAY_SINK_BQ_HEIGHT:
            *outValue = (int32_t)mDisplayHeight;
            break;
        default:
            ALOGE("unknown display attribute %u", attribute);
            return -EINVAL;
    }
    return HWC2_ERROR_NONE;
}

