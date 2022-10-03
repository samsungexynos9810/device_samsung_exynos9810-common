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

#ifndef EXYNOS_DISPLAY_FB_INTERFACE_MODULE_H
#define EXYNOS_DISPLAY_FB_INTERFACE_MODULE_H

#include "ExynosDisplayFbInterface.h"

mpp_phycal_type_t getMPPTypeFromDPPChannel(uint32_t channel);
class ExynosPrimaryDisplayFbInterfaceModule : public ExynosPrimaryDisplayFbInterface {
    public:
        ExynosPrimaryDisplayFbInterfaceModule(ExynosDisplay *exynosDisplay);
        virtual ~ExynosPrimaryDisplayFbInterfaceModule();
        virtual decon_idma_type getDeconDMAType(ExynosMPP *otfMPP);
};

class ExynosExternalDisplayFbInterfaceModule : public ExynosExternalDisplayFbInterface {
    public:
        ExynosExternalDisplayFbInterfaceModule(ExynosDisplay *exynosDisplay);
        virtual ~ExynosExternalDisplayFbInterfaceModule();
        virtual decon_idma_type getDeconDMAType(ExynosMPP *otfMPP);
};
#endif
