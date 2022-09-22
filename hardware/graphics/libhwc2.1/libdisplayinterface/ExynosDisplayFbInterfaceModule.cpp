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

#include "ExynosDisplayFbInterfaceModule.h"

decon_idma_type getDPPChannel(ExynosMPP *otfMPP) {
    if (otfMPP == NULL)
        return MAX_DECON_DMA_TYPE;

    for (int i=0; i < MAX_DECON_DMA_TYPE; i++){
        if((IDMA_CHANNEL_MAP[i].type == otfMPP->mPhysicalType) &&
           (IDMA_CHANNEL_MAP[i].index == otfMPP->mPhysicalIndex))
            return IDMA_CHANNEL_MAP[i].channel;
    }
    return MAX_DECON_DMA_TYPE;
}

//////////////////////////////////////////////////// ExynosPrimaryDisplayFbInterfaceModule //////////////////////////////////////////////////////////////////
ExynosPrimaryDisplayFbInterfaceModule::ExynosPrimaryDisplayFbInterfaceModule(ExynosDisplay *exynosDisplay)
: ExynosPrimaryDisplayFbInterface(exynosDisplay)
{
}

ExynosPrimaryDisplayFbInterfaceModule::~ExynosPrimaryDisplayFbInterfaceModule()
{
}

decon_idma_type ExynosPrimaryDisplayFbInterfaceModule::getDeconDMAType(ExynosMPP *otfMPP)
{
    return getDPPChannel(otfMPP);
}

//////////////////////////////////////////////////// ExynosExternalDisplayFbInterfaceModule //////////////////////////////////////////////////////////////////
ExynosExternalDisplayFbInterfaceModule::ExynosExternalDisplayFbInterfaceModule(ExynosDisplay *exynosDisplay)
: ExynosExternalDisplayFbInterface(exynosDisplay)
{
}

ExynosExternalDisplayFbInterfaceModule::~ExynosExternalDisplayFbInterfaceModule()
{
}

decon_idma_type ExynosExternalDisplayFbInterfaceModule::getDeconDMAType(ExynosMPP *otfMPP)
{
    return getDPPChannel(otfMPP);
}
