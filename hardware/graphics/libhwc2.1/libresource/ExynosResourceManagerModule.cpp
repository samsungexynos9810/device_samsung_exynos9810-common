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
#include <cutils/properties.h>

#include "ExynosResourceManagerModule.h"
#include "ExynosMPPModule.h"
#define CHIP_ID_PATH "/sys/devices/system/chip-id/revision"

ExynosResourceManagerModule::ExynosResourceManagerModule(ExynosDevice* device)
        : ExynosResourceManager(device)
{
    uint32_t chipid = 0x00;
    int chipid_fd = open(CHIP_ID_PATH, O_RDONLY);
    char buf[10];
    int err = 0;
    memset(buf, 0, sizeof(buf));

    if (chipid_fd > 0) {
        err = read(chipid_fd, buf, sizeof(buf));
        if (err < 0) {
            ALOGE("error reading chipid: %s", strerror(errno));
            close(chipid_fd);
            return;
        }
    } else {
        ALOGE("Fail to open chip id file");
    }

    if (!strncmp(buf, "00000000", 8)) {
        chipid = 0x00;
    } else if (!strncmp(buf, "00000001", 8)) {
        chipid = 0x01;
    } else if (!strncmp(buf, "00000011", 8)) {
        chipid = 0x11;
    } else {
        chipid = 0x00;
    }
    ALOGI("chip id: %s, 0x%2x", buf, chipid);

    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        ((ExynosMPPModule *)mOtfMPPs[i])->mChipId = chipid;
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        ((ExynosMPPModule *)mM2mMPPs[i])->mChipId = chipid;
    }

    if (chipid_fd > 0)
        close(chipid_fd);
}

ExynosResourceManagerModule::~ExynosResourceManagerModule()
{
}

int32_t ExynosResourceManagerModule::checkExceptionScenario(ExynosDisplay *display)
{
    /* Check whether camera preview is running */
    char value[PROPERTY_VALUE_MAX];
    bool preview;
    property_get("persist.vendor.sys.camera.preview", value, "0");
    preview = !!atoi(value);

    /* when camera is operating, HWC can't use G2D */
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mPhysicalType != MPP_G2D) continue;
        if (preview)
            mM2mMPPs[i]->mDisableByUserScenario = true;
        else
            mM2mMPPs[i]->mDisableByUserScenario = false;
    }

    return NO_ERROR;
}
