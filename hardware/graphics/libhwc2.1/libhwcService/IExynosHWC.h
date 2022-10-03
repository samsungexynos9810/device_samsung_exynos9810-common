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

#ifndef ANDROID_EXYNOS_IHWC_H_
#define ANDROID_EXYNOS_IHWC_H_

#include <stdint.h>
#include <sys/types.h>
#include <vector>

#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <binder/IInterface.h>

namespace android {

    struct CPUPerfInfo {
        int32_t cpuIDs = -1;
        int32_t min_clock = -1;
    };

class IExynosHWCService : public IInterface {
public:

    DECLARE_META_INTERFACE(ExynosHWCService);

    virtual int addVirtualDisplayDevice() = 0;
    virtual int destroyVirtualDisplayDevice() = 0;

    /*
     * setWFDMode() function sets the WFD operation Mode.
     * It enables / disables the WFD.
     */
    virtual int setWFDMode(unsigned int mode) = 0;
    virtual int getWFDMode() = 0;
    virtual int getWFDInfo(int32_t* state, int32_t* compositionType, int32_t* format,
        int64_t* usage, int32_t* width, int32_t* height) = 0;
    virtual int sendWFDCommand(int32_t cmd, int32_t ext1, int32_t ext2) = 0;
    virtual int setSecureVDSMode(unsigned int mode) = 0;
    virtual int setWFDOutputResolution(unsigned int width, unsigned int height) = 0;
    virtual void getWFDOutputResolution(unsigned int *width, unsigned int *height) = 0;
    virtual void setPresentationMode(bool use) = 0;
    virtual int getPresentationMode(void) = 0;
    virtual int setVDSGlesFormat(int format) = 0;
    virtual int setExternalVsyncEnabled(unsigned int index) = 0;
    virtual int getExternalHdrCapabilities() = 0;
    virtual void setBootFinished(void) = 0;
    virtual void enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable) = 0;
    virtual void setHWCDebug(int debug) = 0;
    virtual uint32_t getHWCDebug() = 0;
    virtual void setHWCFenceDebug(uint32_t ipNum, uint32_t fenceNum, uint32_t mode) = 0;
    virtual void getHWCFenceDebug() = 0;
    virtual int setHWCCtl(uint32_t display, uint32_t ctrl, int32_t val) = 0;

    virtual int setDDIScaler(uint32_t width, uint32_t height) = 0;
    virtual void setDumpCount(uint32_t dumpCount) = 0;
    virtual int getCPUPerfInfo(int display, int config, int32_t *cpuIDs, int32_t *min_clock) = 0;
    virtual bool isNeedCompressedTargetBuffer(uint64_t id) = 0;
    /*
    virtual void notifyPSRExit() = 0;
    */
};

/* Native Interface */
class BnExynosHWCService : public BnInterface<IExynosHWCService> {
public:
    virtual status_t    onTransact( uint32_t code,
                                    const Parcel& data,
                                    Parcel* reply,
                                    uint32_t flags = 0);

};
}
#endif
