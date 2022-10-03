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

#include "ExynosMPPModule.h"
#include "ExynosHWCDebug.h"
#include "ExynosResourceManager.h"
#include "ExynosResourceRestriction.h"

ExynosMPPModule::ExynosMPPModule(ExynosResourceManager* resourceManager,
        uint32_t physicalType, uint32_t logicalType, const char *name,
        uint32_t physicalIndex, uint32_t logicalIndex, uint32_t preAssignInfo)
    : ExynosMPP(resourceManager, physicalType, logicalType, name, physicalIndex, logicalIndex, preAssignInfo),
    mChipId(0x00)
{
}

ExynosMPPModule::~ExynosMPPModule()
{
}

uint32_t ExynosMPPModule::getMPPClock()
{
    if (mPhysicalType == MPP_G2D)
        return 667000;
    else
        return 0;
}
