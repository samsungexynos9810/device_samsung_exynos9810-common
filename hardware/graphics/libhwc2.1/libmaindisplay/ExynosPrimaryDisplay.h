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
#ifndef EXYNOS_PRIMARY_DISPLAY_H
#define EXYNOS_PRIMARY_DISPLAY_H

#include "../libdevice/ExynosDisplay.h"
#include "ExynosDisplayFbInterface.h"

class ExynosMPPModule;

class ExynosPrimaryDisplay : public ExynosDisplay {
    public:
        /* Methods */
        ExynosPrimaryDisplay(uint32_t index, ExynosDevice *device);
        ~ExynosPrimaryDisplay();
        virtual void setDDIScalerEnable(int width, int height);
        virtual int getDDIScalerMode(int width, int height);

        virtual void initDisplayInterface(uint32_t interfaceType);
    protected:
        virtual int32_t setPowerMode(
                int32_t /*hwc2_power_mode_t*/ mode);
        virtual bool getHDRException(ExynosLayer* __unused layer);
    public:
        // Prepare multi resolution
        ResolutionInfo mResolutionInfo;
};

#endif
