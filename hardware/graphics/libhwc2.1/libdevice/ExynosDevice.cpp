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

#include <sched.h>
#include <dlfcn.h>

#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosLayer.h"
#include "ExynosPrimaryDisplayModule.h"
#include "ExynosResourceManagerModule.h"
#include "ExynosExternalDisplayModule.h"
#include "ExynosVirtualDisplayModule.h"
#include "ExynosHWCDebug.h"
#include "ExynosHWCHelper.h"
#include "ExynosDeviceFbInterface.h"

/**
 * ExynosDevice implementation
 */

class ExynosDevice;

extern void vsync_callback(hwc2_callback_data_t callbackData,
        hwc2_display_t displayId, int64_t timestamp);
extern uint32_t mFenceLogSize;

int hwcDebug;
int hwcFenceDebug[FENCE_IP_ALL];
struct exynos_hwc_control exynosHWCControl;
struct update_time_info updateTimeInfo;
char fence_names[FENCE_MAX][32];

GrallocWrapper::Mapper* ExynosDevice::mMapper = NULL;
GrallocWrapper::Allocator* ExynosDevice::mAllocator = NULL;

ExynosDevice::ExynosDevice()
    : mGeometryChanged(0),
    mDRThread(0),
    mVsyncDisplayId(getDisplayId(HWC_DISPLAY_PRIMARY, 0)),
    mTimestamp(0),
    mDisplayMode(0),
    mTotalDumpCount(0),
    mIsDumpRequest(false),
    mInterfaceType(INTERFACE_TYPE_FB)
{

    exynosHWCControl.forceGpu = false;
    exynosHWCControl.windowUpdate = true;
    exynosHWCControl.forcePanic = false;
    exynosHWCControl.skipStaticLayers = true;
    exynosHWCControl.skipM2mProcessing = true;
    exynosHWCControl.skipResourceAssign = true;
    exynosHWCControl.multiResolution = true;
    exynosHWCControl.dumpMidBuf = false;
    exynosHWCControl.displayMode = DISPLAY_MODE_NUM;
    exynosHWCControl.setDDIScaler = false;
    exynosHWCControl.skipWinConfig = false;
    exynosHWCControl.skipValidate = true;
    exynosHWCControl.doFenceFileDump = false;
    exynosHWCControl.fenceTracer = 0;
    exynosHWCControl.sysFenceLogging = false;
    exynosHWCControl.useDynamicRecomp = false;

    mFenceInfo.clear();

    ALOGD("HWC2 : %s : %d , %lu", __func__, __LINE__, (unsigned long)mFenceInfo.size());

    mResourceManager = new ExynosResourceManagerModule(this);

    for (size_t i = 0; i < DISPLAY_COUNT; i++) {
        exynos_display_t display_t = AVAILABLE_DISPLAY_UNITS[i];
        ExynosDisplay *exynos_display = NULL;
        switch(display_t.type) {
            case HWC_DISPLAY_PRIMARY:
                exynos_display = (ExynosDisplay *)(new ExynosPrimaryDisplayModule(display_t.index, this));
                /* Primary display always plugged-in */
                exynos_display->mPlugState = true;
                if(display_t.index == 0) {
                    ExynosMPP::mainDisplayWidth = exynos_display->mXres;
                    if (ExynosMPP::mainDisplayWidth <= 0) {
                        ExynosMPP::mainDisplayWidth = 1440;
                    }
                    ExynosMPP::mainDisplayHeight = exynos_display->mYres;
                    if (ExynosMPP::mainDisplayHeight <= 0) {
                        ExynosMPP::mainDisplayHeight = 2560;
                    }
                }
                break;
            case HWC_DISPLAY_EXTERNAL:
                exynos_display = (ExynosDisplay *)(new ExynosExternalDisplayModule(display_t.index, this));
                break;
            case HWC_DISPLAY_VIRTUAL:
                exynos_display = (ExynosDisplay *)(new ExynosVirtualDisplayModule(display_t.index, this));
                mNumVirtualDisplay = 0;
                break;
            default:
                ALOGE("Unsupported display type(%d)", display_t.type);
                break;
        }
        exynos_display->mDeconNodeName.appendFormat("%s", display_t.decon_node_name);
        exynos_display->mDisplayName.appendFormat("%s", display_t.display_name);
        mDisplays.add(exynos_display);

#ifndef FORCE_DISABLE_DR
        if (exynos_display->mDREnable)
            exynosHWCControl.useDynamicRecomp = true;
#endif
    }

    memset(mCallbackInfos, 0, sizeof(mCallbackInfos));

    dynamicRecompositionThreadCreate();

    hwcDebug = 0;
    for (uint32_t i = 0; i < FENCE_IP_ALL; i++)
        hwcFenceDebug[i] = 0;

    for (uint32_t i = 0; i < FENCE_MAX; i++) {
        memset(fence_names[i], 0, sizeof(fence_names[0]));
        sprintf(fence_names[i], "_%2dh", i);
    }

    String8 saveString;
    saveString.appendFormat("ExynosDevice is initialized");
    uint32_t errFileSize = saveErrorLog(saveString);
    ALOGI("Initial errlog size: %d bytes\n", errFileSize);
    isBootFinished = false;

    /*
     * This order should not be changed
     * new ExynosResourceManager ->
     * create displays and add them to the list ->
     * initDeviceInterface() ->
     * ExynosResourceManager::updateRestrictions()
     */
    initDeviceInterface(mInterfaceType);
    mResourceManager->updateRestrictions();
    mResourceManager->generateResourceTable();

    /* Assign all resource sets to main display */
    ExynosDisplay *display = mDisplays[0];
    display->assignInitialResourceSet();

    /* CPU clock settings */
#ifdef EPIC_LIBRARY_PATH
    mEPICHandle = dlopen(EPIC_LIBRARY_PATH, RTLD_NOW | RTLD_LOCAL);
    if (!mEPICHandle) {
        ALOGE("%s: DLOPEN failed\n", __func__);
        return ;
    } else {
        (mEPICRequestFcnPtr) =
            (long (*)(int))dlsym(mEPICHandle, "epic_alloc_request");
        (mEPICFreeFcnPtr) =
            (void (*)(long))dlsym(mEPICHandle, "epic_free_request");
        (mEPICAcquireFcnPtr) =
            (bool (*)(long))dlsym(mEPICHandle, "epic_acquire");
        (mEPICReleaseFcnPtr) =
            (bool (*)(long))dlsym(mEPICHandle, "epic_release");
        (mEPICAcquireOptionFcnPtr) =
            (bool (*)(long, unsigned, unsigned))dlsym(mEPICHandle, "epic_acquire_option");
    }

    if (!mEPICRequestFcnPtr || !mEPICFreeFcnPtr || !mEPICAcquireFcnPtr || !mEPICReleaseFcnPtr) {
        ALOGE("%s: DLSYM failed\n", __func__);
    }
#endif

#ifdef USES_HWC_CPU_PERF_MODE

    /* Write 'CPU Performance table file' from default performance table */
    /* Default performance table is defined at perfTable structure in module code */

    mPerfTableFd = fopen(CPU_PERF_TABLE_PATH, "w+");
    if (mPerfTableFd == NULL)
        ALOGE("%s open failed", CPU_PERF_TABLE_PATH);
    else {
        for (auto it = perfTable.begin(); it != perfTable.end(); ++it) {
            uint32_t fps = it->first;
            perfMap tempMap =  perfTable.at(fps);
            fprintf(mPerfTableFd, "%u %u ", fps, tempMap.cpuIDs);

            for (int cl_num = 0; cl_num < CPU_CLUSTER_CNT; cl_num++)
                fprintf(mPerfTableFd, "%u ", tempMap.minClock[cl_num]);

            fprintf(mPerfTableFd, "%u\n", tempMap.m2mCapa);
        }
    }

#endif

}

void ExynosDevice::initDeviceInterface(uint32_t interfaceType)
{
    mDeviceInterface = new ExynosDeviceFbInterface(this);
    /*
     * This order should not be changed
     * initDisplayInterface() of each display ->
     * ExynosDeviceInterface::init()
     */
    for (uint32_t i = 0; i < mDisplays.size(); i++)
        mDisplays[i]->initDisplayInterface(interfaceType);
    mDeviceInterface->init(this);
}

ExynosDevice::~ExynosDevice() {

    ExynosDisplay *primary_display = getDisplay(getDisplayId(HWC_DISPLAY_PRIMARY,0));

    /* TODO kill threads here */
    pthread_kill(mDRThread, SIGTERM);
    pthread_join(mDRThread, NULL);

    if (mMapper != NULL)
        delete mMapper;
    if (mAllocator != NULL)
        delete mAllocator;

    if (mDeviceInterface != NULL)
        delete mDeviceInterface;

    delete primary_display;

    if (mEPICHandle != NULL) {
        dlclose(mEPICHandle);
    }
    if (mPerfTableFd != NULL) {
        fclose(mPerfTableFd);
    }
}

bool ExynosDevice::isFirstValidate(ExynosDisplay *display)
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        /*
         * Do not skip when mDisplays[i] is same with display
         * If this condition is skipped
         * display can be checked with the first display
         * even though it was already validated.
         * This can be happend when the first validated display
         * is powered off or disconnected.
         */

        if ((mDisplays[i]->mType != HWC_DISPLAY_VIRTUAL) &&
            (mDisplays[i]->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_OFF))
            continue;

        if (mDisplays[i]->mPlugState == false)
            continue;

        /* exynos9810 specific source code */
        if (mDisplays[i]->mType == HWC_DISPLAY_EXTERNAL)
        {
            ExynosExternalDisplay *extDisp = (ExynosExternalDisplay*)mDisplays[i];
            if (extDisp->mBlanked == true)
                continue;
        }

        if (mDisplays[i]->mRenderingStateFlags.validateFlag) {
            HDEBUGLOGD(eDebugResourceManager, "\t%s is not first validate, %s is validated",
                    display->mDisplayName.string(), mDisplays[i]->mDisplayName.string());
            return false;
        }
    }

    HDEBUGLOGD(eDebugResourceManager, "\t%s is the first validate", display->mDisplayName.string());
    return true;
}

bool ExynosDevice::isLastValidate(ExynosDisplay *display)
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        if (mDisplays[i] == display)
            continue;

        if ((mDisplays[i]->mType != HWC_DISPLAY_VIRTUAL) &&
            (mDisplays[i]->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_OFF))
            continue;

        if (mDisplays[i]->mPlugState == false)
            continue;

        /* exynos9810 specific source code */
        if (mDisplays[i]->mType == HWC_DISPLAY_EXTERNAL)
        {
            ExynosExternalDisplay *extDisp = (ExynosExternalDisplay*)mDisplays[i];
            if (extDisp->mBlanked == true)
                continue;
        }

        if (mDisplays[i]->mRenderingStateFlags.validateFlag == false) {
            HDEBUGLOGD(eDebugResourceManager, "\t%s is not last validate, %s is not validated",
                    display->mDisplayName.string(), mDisplays[i]->mDisplayName.string());
            return false;
        }
    }
    HDEBUGLOGD(eDebugResourceManager, "\t%s is the last validate", display->mDisplayName.string());
    return true;
}

bool ExynosDevice::isLastPresent(ExynosDisplay *display)
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        if (mDisplays[i] == display)
            continue;

        if ((mDisplays[i]->mType != HWC_DISPLAY_VIRTUAL) &&
            (mDisplays[i]->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_OFF))
            continue;

        if (mDisplays[i]->mPlugState == false)
            continue;

        /* exynos9810 specific source code */
        if (mDisplays[i]->mType == HWC_DISPLAY_EXTERNAL)
        {
            ExynosExternalDisplay *extDisp = (ExynosExternalDisplay*)mDisplays[i];
            if (extDisp->mBlanked == true)
                continue;
        }

        if (mDisplays[i]->mRenderingStateFlags.presentFlag == false) {
            HDEBUGLOGD(eDebugResourceManager, "\t%s is not last present, %s is not presented",
                    display->mDisplayName.string(), mDisplays[i]->mDisplayName.string());
            return false;
        }
    }
    HDEBUGLOGD(eDebugResourceManager, "\t%s is the last present", display->mDisplayName.string());
    return true;
}

bool ExynosDevice::isDynamicRecompositionThreadAlive()
{
    android_atomic_acquire_load(&mDRThreadStatus);
    return (mDRThreadStatus > 0);
}

void ExynosDevice::checkDynamicRecompositionThread()
{
    // If thread was destroyed, create thread and run. (resume status)
    if (isDynamicRecompositionThreadAlive() == false) {
        for (uint32_t i = 0; i < mDisplays.size(); i++) {
            if (mDisplays[i]->mDREnable) {
                dynamicRecompositionThreadCreate();
                return;
            }
        }
    } else {
    // If thread is running and all displays turnned off DR, destroy the thread.
        for (uint32_t i = 0; i < mDisplays.size(); i++) {
            if (mDisplays[i]->mDREnable)
                return;
        }
        mDRLoopStatus = false;
        pthread_join(mDRThread, 0);
    }
}

void ExynosDevice::dynamicRecompositionThreadCreate()
{
    if (exynosHWCControl.useDynamicRecomp == true) {
        /* pthread_create shouldn't have been failed. But, ignore if some error was occurred */
        if (pthread_create(&mDRThread, NULL, dynamicRecompositionThreadLoop, this) != 0) {
            ALOGE("%s: failed to start hwc_dynamicrecomp_thread thread:", __func__);
            mDRLoopStatus = false;
        } else {
            mDRLoopStatus = true;
        }
    }
}

void *ExynosDevice::dynamicRecompositionThreadLoop(void *data)
{
    ExynosDevice *dev = (ExynosDevice *)data;
    ExynosDisplay *display[dev->mDisplays.size()];
    uint64_t event_cnt[dev->mDisplays.size()];

    for (uint32_t i = 0; i < dev->mDisplays.size(); i++) {
        display[i] = dev->mDisplays[i];
        event_cnt[i] = 0;
    }
    android_atomic_inc(&(dev->mDRThreadStatus));

    while (dev->mDRLoopStatus) {
        uint32_t result = 0;
        for (uint32_t i = 0; i < dev->mDisplays.size(); i++)
            event_cnt[i] = display[i]->mUpdateEventCnt;

        /*
         * If there is no update for more than 100ms, favor the 3D composition mode.
         * If all other conditions are met, mode will be switched to 3D composition.
         */
        usleep(100000);
        for (uint32_t i = 0; i < dev->mDisplays.size(); i++) {
            if (display[i]->mDREnable &&
                display[i]->mPlugState == true &&
                event_cnt[i] == display[i]->mUpdateEventCnt) {
                if (display[i]->checkDynamicReCompMode() == DEVICE_2_CLIENT) {
                    display[i]->mUpdateEventCnt = 0;
                    display[i]->setGeometryChanged(GEOMETRY_DISPLAY_DYNAMIC_RECOMPOSITION);
                    result = 1;
                }
            }
        }
        if (result)
            dev->invalidate();
    }

    android_atomic_dec(&(dev->mDRThreadStatus));

    return NULL;
}
/**
 * @param display
 * @return ExynosDisplay
 */
ExynosDisplay* ExynosDevice::getDisplay(uint32_t display) {
    if (mDisplays.isEmpty()) {
        goto err;
    }

    for (size_t i = 0;i < mDisplays.size(); i++) {
        if (mDisplays[i]->mDisplayId == display)
            return (ExynosDisplay*)mDisplays[i];
    }

    return NULL;

err:
    ALOGE("mDisplays.size(%zu), requested display(%d)",
            mDisplays.size(), display);

    return NULL;
}

/**
 * Device Functions for HWC 2.0
 */

int32_t ExynosDevice::createVirtualDisplay(
        uint32_t __unused width, uint32_t __unused height, int32_t* /*android_pixel_format_t*/ __unused format, ExynosDisplay* __unused display) {
    ((ExynosVirtualDisplay*)display)->createVirtualDisplay(width, height, format);
    return 0;
}

/**
 * @param *display
 * @return int32_t
 */
int32_t ExynosDevice::destroyVirtualDisplay(ExynosDisplay* __unused display) {
    ((ExynosVirtualDisplay *)display)->destroyVirtualDisplay();
    return 0;
}

void ExynosDevice::dump(uint32_t *outSize, char *outBuffer) {
    if (outSize == NULL) {
        ALOGE("%s:: outSize is null", __func__);
        return;
    }

    android::String8 result;
    result.append("\n\n");

    struct tm* localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastUeventTime.tv_sec);
    result.appendFormat("lastUeventTime(%02d:%02d:%02d.%03lu) lastTimestamp(%" PRIu64 ")\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastUeventTime.tv_usec/1000, mTimestamp);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastEnableVsyncTime.tv_sec);
    result.appendFormat("lastEnableVsyncTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastEnableVsyncTime.tv_usec/1000);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastDisableVsyncTime.tv_sec);
    result.appendFormat("lastDisableVsyncTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastDisableVsyncTime.tv_usec/1000);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastValidateTime.tv_sec);
    result.appendFormat("lastValidateTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastValidateTime.tv_usec/1000);

    localTime = (struct tm*)localtime((time_t*)&updateTimeInfo.lastPresentTime.tv_sec);
    result.appendFormat("lastPresentTime(%02d:%02d:%02d.%03lu)\n",
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, updateTimeInfo.lastPresentTime.tv_usec/1000);

    for (size_t i = 0;i < mDisplays.size(); i++) {
        ExynosDisplay *display = mDisplays[i];
        if (display->mPlugState == true)
            display->dump(result);
    }

    if (outBuffer == NULL) {
        *outSize = (uint32_t)result.length();
    } else {
        if (*outSize == 0) {
            ALOGE("%s:: outSize is 0", __func__);
            return;
        }
        uint32_t copySize = *outSize;
        if (*outSize > result.size())
            copySize = (uint32_t)result.size();
        ALOGI("HWC dump:: resultSize(%zu), outSize(%d), copySize(%d)", result.size(), *outSize, copySize);
        strlcpy(outBuffer, result.string(), copySize);
    }

    return;
}

uint32_t ExynosDevice::getMaxVirtualDisplayCount() {
#ifdef USES_VIRTUAL_DISPLAY
    return 1;
#else
    return 0;
#endif
}

int32_t ExynosDevice::registerCallback (
        int32_t descriptor, hwc2_callback_data_t callbackData,
        hwc2_function_pointer_t point) {
    if (descriptor < 0 || descriptor > HWC2_CALLBACK_VSYNC)
        return HWC2_ERROR_BAD_PARAMETER;

    mCallbackInfos[descriptor].callbackData = callbackData;
    mCallbackInfos[descriptor].funcPointer = point;

    /* Call hotplug callback for primary display*/
    if (descriptor == HWC2_CALLBACK_HOTPLUG) {
        HWC2_PFN_HOTPLUG callbackFunc =
            (HWC2_PFN_HOTPLUG)mCallbackInfos[descriptor].funcPointer;
        if (callbackFunc != NULL) {
            callbackFunc(callbackData, getDisplayId(HWC_DISPLAY_PRIMARY, 0), HWC2_CONNECTION_CONNECTED);
#if defined(USES_DUAL_DISPLAY)
            callbackFunc(callbackData, getDisplayId(HWC_DISPLAY_PRIMARY, 1), HWC2_CONNECTION_CONNECTED);
#endif
        }
    }

    if (descriptor == HWC2_CALLBACK_VSYNC)
        mResourceManager->doPreProcessing();

    return HWC2_ERROR_NONE;
}

void ExynosDevice::invalidate()
{
    HWC2_PFN_REFRESH callbackFunc =
        (HWC2_PFN_REFRESH)mCallbackInfos[HWC2_CALLBACK_REFRESH].funcPointer;
    if (callbackFunc != NULL)
        callbackFunc(mCallbackInfos[HWC2_CALLBACK_REFRESH].callbackData,
                getDisplayId(HWC_DISPLAY_PRIMARY, 0));
    else
        ALOGE("%s:: refresh callback is not registered", __func__);

}

void ExynosDevice::setHWCDebug(unsigned int debug)
{
    hwcDebug = debug;
}

uint32_t ExynosDevice::getHWCDebug()
{
    return hwcDebug;
}

void ExynosDevice::setHWCFenceDebug(uint32_t typeNum, uint32_t ipNum, uint32_t mode)
{
    if (typeNum > FENCE_TYPE_ALL || ipNum > FENCE_IP_ALL || mode > 1) {
        ALOGE("%s:: input is not valid type(%u), IP(%u), mode(%d)", __func__, typeNum, ipNum, mode);
        return;
    }

    uint32_t value = 0;

    if (typeNum == FENCE_TYPE_ALL)
        value = (1 << FENCE_TYPE_ALL) - 1;
    else
        value = 1 << typeNum;

    if (ipNum == FENCE_IP_ALL) {
        for (uint32_t i = 0; i < FENCE_IP_ALL; i++) {
            if (mode)
                hwcFenceDebug[i] |= value;
            else
                hwcFenceDebug[i] &= (~value);
        }
    } else {
        if (mode)
            hwcFenceDebug[ipNum] |= value;
        else
            hwcFenceDebug[ipNum] &= (~value);
    }

}

void ExynosDevice::getHWCFenceDebug()
{
    for (uint32_t i = 0; i < FENCE_IP_ALL; i++)
        ALOGE("[HWCFenceDebug] IP_Number(%d) : Debug(%x)", i, hwcFenceDebug[i]);
}

void ExynosDevice::setHWCControl(uint32_t display, uint32_t ctrl, int32_t val)
{
    ExynosDisplay *exynosDisplay = NULL;
    switch (ctrl) {
        case HWC_CTL_FORCE_GPU:
            ALOGI("%s::HWC_CTL_FORCE_GPU on/off=%d", __func__, val);
            exynosHWCControl.forceGpu = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_WINDOW_UPDATE:
            ALOGI("%s::HWC_CTL_WINDOW_UPDATE on/off=%d", __func__, val);
            exynosHWCControl.windowUpdate = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_FORCE_PANIC:
            ALOGI("%s::HWC_CTL_FORCE_PANIC on/off=%d", __func__, val);
            exynosHWCControl.forcePanic = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            break;
        case HWC_CTL_SKIP_STATIC:
            ALOGI("%s::HWC_CTL_SKIP_STATIC on/off=%d", __func__, val);
            exynosHWCControl.skipStaticLayers = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            break;
        case HWC_CTL_SKIP_M2M_PROCESSING:
            ALOGI("%s::HWC_CTL_SKIP_M2M_PROCESSING on/off=%d", __func__, val);
            exynosHWCControl.skipM2mProcessing = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            break;
        case HWC_CTL_SKIP_RESOURCE_ASSIGN:
            ALOGI("%s::HWC_CTL_SKIP_RESOURCE_ASSIGN on/off=%d", __func__, val);
            exynosHWCControl.skipResourceAssign = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_SKIP_VALIDATE:
            ALOGI("%s::HWC_CTL_SKIP_VALIDATE on/off=%d", __func__, val);
            exynosHWCControl.skipValidate = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_DUMP_MID_BUF:
            ALOGI("%s::HWC_CTL_DUMP_MID_BUF on/off=%d", __func__, val);
            exynosHWCControl.dumpMidBuf = (unsigned int)val;
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_DISPLAY_MODE:
            ALOGI("%s::HWC_CTL_DISPLAY_MODE mode=%d", __func__, val);
            setDisplayMode((uint32_t)val);
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        // Support DDI scalser {
        case HWC_CTL_DDI_RESOLUTION_CHANGE:
            ALOGI("%s::HWC_CTL_DDI_RESOLUTION_CHANGE mode=%d", __func__, val);
            exynosDisplay = (ExynosDisplay*)getDisplay(display);
            uint32_t width, height;

            /* TODO: Add branch here for each resolution/index */
            switch(val) {
            case 1:
            case 2:
            case 3:
            default:
                width = 1440; height = 2960;
                break;
            }

            if (exynosDisplay == NULL) {
                for (uint32_t i = 0; i < mDisplays.size(); i++) {
                    mDisplays[i]->setDDIScalerEnable(width, height);
                }
            } else {
                exynosDisplay->setDDIScalerEnable(width, height);
            }
            setGeometryChanged(GEOMETRY_DISPLAY_RESOLUTION_CHANGED);
            invalidate();
            break;
        // } Support DDI scaler
        case HWC_CTL_ENABLE_COMPOSITION_CROP:
        case HWC_CTL_ENABLE_EXYNOSCOMPOSITION_OPT:
        case HWC_CTL_ENABLE_CLIENTCOMPOSITION_OPT:
        case HWC_CTL_USE_MAX_G2D_SRC:
        case HWC_CTL_ENABLE_HANDLE_LOW_FPS:
        case HWC_CTL_ENABLE_EARLY_START_MPP:
            exynosDisplay = (ExynosDisplay*)getDisplay(display);
            if (exynosDisplay == NULL) {
                for (uint32_t i = 0; i < mDisplays.size(); i++) {
                    mDisplays[i]->setHWCControl(ctrl, val);
                }
            } else {
                exynosDisplay->setHWCControl(ctrl, val);
            }
            setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
            invalidate();
            break;
        case HWC_CTL_DYNAMIC_RECOMP:
            ALOGI("%s::HWC_CTL_DYNAMIC_RECOMP on/off = %d", __func__, val);
            this->setDynamicRecomposition((unsigned int)val);
            break;
        case HWC_CTL_ENABLE_FENCE_TRACER:
            ALOGI("%s::HWC_CTL_ENABLE_FENCE_TRACER on/off=%d", __func__, val);
            exynosHWCControl.fenceTracer = (unsigned int)val;
            break;
        case HWC_CTL_SYS_FENCE_LOGGING:
            ALOGI("%s::HWC_CTL_SYS_FENCE_LOGGING on/off=%d", __func__, val);
            exynosHWCControl.sysFenceLogging = (unsigned int)val;
            break;
        case HWC_CTL_DO_FENCE_FILE_DUMP:
            ALOGI("%s::HWC_CTL_DO_FENCE_FILE_DUMP on/off=%d", __func__, val);
            exynosHWCControl.doFenceFileDump = (unsigned int)val;
            break;
        default:
            ALOGE("%s: unsupported HWC_CTL (%d)", __func__, ctrl);
            break;
    }
}

void ExynosDevice::setDisplayMode(uint32_t displayMode)
{
    exynosHWCControl.displayMode = displayMode;
}

void ExynosDevice::setDynamicRecomposition(unsigned int on)
{
    exynosHWCControl.useDynamicRecomp = on;
}

bool ExynosDevice::checkDisplayEnabled(uint32_t displayId)
{
	ExynosDisplay *display = getDisplay(displayId);

    if (!display)
        return false;
    else
        return display->isEnabled();
}

bool ExynosDevice::checkAdditionalConnection()
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        switch(mDisplays[i]->mType) {
            case HWC_DISPLAY_PRIMARY:
                break;
            case HWC_DISPLAY_EXTERNAL:
            case HWC_DISPLAY_VIRTUAL:
                if (mDisplays[i]->mPlugState)
                    return true;
                break;
            default:
                break;
        }
    }
    return false;
}
void ExynosDevice::getCapabilities(uint32_t *outCount, int32_t* outCapabilities)
{
    uint32_t capabilityNum = 0;
#ifdef HWC_SUPPORT_COLOR_TRANSFORM
    capabilityNum++;
#endif
#ifdef HWC_SKIP_VALIDATE
    capabilityNum++;
#endif
    if (outCapabilities == NULL) {
        *outCount = capabilityNum;
        return;
    }
    if (capabilityNum != *outCount) {
        ALOGE("%s:: invalid outCount(%d), should be(%d)", __func__, *outCount, capabilityNum);
        return;
    }
#if defined(HWC_SUPPORT_COLOR_TRANSFORM) || defined(HWC_SKIP_VALIDATE)
    uint32_t index = 0;
#endif
#ifdef HWC_SUPPORT_COLOR_TRANSFORM
    outCapabilities[index++] = HWC2_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM;
#endif
#ifdef HWC_SKIP_VALIDATE
    outCapabilities[index++] = HWC2_CAPABILITY_SKIP_VALIDATE;
#endif
    return;
}

void ExynosDevice::getAllocator(GrallocWrapper::Mapper** mapper, GrallocWrapper::Allocator** allocator)
{
    if ((mMapper == NULL) && (mAllocator == NULL)) {
        ALOGI("%s:: Allocator is created", __func__);
        mMapper = new GrallocWrapper::Mapper();
        mAllocator = new GrallocWrapper::Allocator(*mMapper);
    }
    *mapper = mMapper;
    *allocator = mAllocator;
}

void ExynosDevice::clearGeometryChanged()
{
    mGeometryChanged = 0;
}

void ExynosDevice::setGeometryFlagForSkipFrame()
{
    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        if ((mDisplays[i]->mType != HWC_DISPLAY_VIRTUAL) &&
            (mDisplays[i]->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_OFF))
            continue;

        if (mDisplays[i]->mPlugState == false)
            continue;

        /* exynos9810 specific source code */
        if (mDisplays[i]->mType == HWC_DISPLAY_EXTERNAL)
        {
            ExynosExternalDisplay *extDisp = (ExynosExternalDisplay*)mDisplays[i];
            if (extDisp->mBlanked == true)
                continue;
        }

        /*
         * mGeometryChanged might been cleared if this was last present.
         * Set geometry flag not to skip validate this display in next frame.
         */
        if ((mDisplays[i]->mIsSkipFrame) ||
            (mDisplays[i]->mNeedSkipPresent) ||
            (mDisplays[i]->mNeedSkipValidatePresent)) {
            setGeometryChanged(GEOMETRY_DISPLAY_FRAME_SKIPPED);
        }
    }
}

bool ExynosDevice::canSkipValidate()
{
    /*
     * This should be called by presentDisplay()
     * when presentDisplay() is called without validateDisplay() call
     */

    int ret = 0;
    if (exynosHWCControl.skipValidate == false)
        return false;

    for (uint32_t i = 0; i < mDisplays.size(); i++) {
        /*
         * Check all displays.
         * Resource assignment can have problem if validateDisplay is skipped
         * on only some displays.
         * All display's validateDisplay should be skipped or all display's validateDisplay
         * should not be skipped.
         */
        if (mDisplays[i]->mPlugState) {
            /*
             * presentDisplay is called without validateDisplay.
             * Call functions that should be called in validateDiplay
             */
            mDisplays[i]->doPreProcessing();
            mDisplays[i]->checkLayerFps();

            if ((ret = mDisplays[i]->canSkipValidate()) != NO_ERROR) {
                HDEBUGLOGD(eDebugSkipValidate, "Display[%d] can't skip validate (%d), renderingState(%d), geometryChanged(0x%" PRIx64 ")",
                        mDisplays[i]->mDisplayId, ret,
                        mDisplays[i]->mRenderingState, mGeometryChanged);
                return false;
            } else {
                HDEBUGLOGD(eDebugSkipValidate, "Display[%d] can skip validate (%d), renderingState(%d), geometryChanged(0x%" PRIx64 ")",
                        mDisplays[i]->mDisplayId, ret,
                        mDisplays[i]->mRenderingState, mGeometryChanged);
            }
        }
    }
    return true;
}

bool ExynosDevice::validateFences(ExynosDisplay *display) {

    if (!validateFencePerFrame(display)) {
        String8 errString;
        errString.appendFormat("Per frame fence leak!\n");
        ALOGE("%s", errString.string());
        saveFenceTrace(display);
        return false;
    }

    if (fenceWarn(display, MAX_FENCE_THRESHOLD)) {
        String8 errString;
        errString.appendFormat("Fence leak!\n");
        printLeakFds(display);
        saveFenceTrace(display);
        return false;
    }

    if (exynosHWCControl.doFenceFileDump) {
        ALOGE("Fence file dump !");
        if (mFenceLogSize != 0)
            ALOGE("Fence file not empty!");
        saveFenceTrace(display);
        exynosHWCControl.doFenceFileDump = false;
    }

    return true;
}

void ExynosDevice::compareVsyncPeriod() {
    std::map<uint32_t, uint32_t> displayFps;
    displayFps.clear();

    for (size_t i = mDisplays.size(); i > 0; i--) {
        if (mDisplays[i-1]->mType == HWC_DISPLAY_VIRTUAL)
            continue;
        if (mDisplays[i-1]->mPlugState == false)
            continue;
        if (mDisplays[i-1]->mPowerModeState == HWC2_POWER_MODE_OFF)
            continue;
        else if (mDisplays[i-1]->mPowerModeState == HWC2_POWER_MODE_DOZE_SUSPEND)
            displayFps.insert(std::make_pair(DOZE_VSYNC_PERIOD,mDisplays[i-1]->mDisplayId));
        else
            displayFps.insert(std::make_pair(mDisplays[i-1]->mVsyncPeriod, mDisplays[i-1]->mDisplayId));
    }

    if (displayFps.size() == 0) {
        mVsyncDisplayId = getDisplayId(HWC_DISPLAY_PRIMARY, 0);
        return;
    }
    std::map<uint32_t, uint32_t>::iterator k = --displayFps.end();
    mVsyncDisplayId = k->second;

    return;
}

void ExynosDevice::setDumpCount()
{
    for (size_t i = 0; i < mDisplays.size(); i++) {
        if(mDisplays[i]->mPlugState == true)
            mDisplays[i]->setDumpCount(mTotalDumpCount);
    }
}

void ExynosDevice::clearRenderingStateFlags()
{
    for (size_t i = 0; i < mDisplays.size(); i++) {
        mDisplays[i]->mRenderingStateFlags.validateFlag = false;
        mDisplays[i]->mRenderingStateFlags.presentFlag = false;
    }
}

bool ExynosDevice::wasRenderingStateFlagsCleared()
{
    for (size_t i = 0; i < mDisplays.size(); i++) {
        if (mDisplays[i]->mRenderingStateFlags.validateFlag ||
            mDisplays[i]->mRenderingStateFlags.presentFlag)
            return false;
    }
    return true;
}

int32_t ExynosDevice::validateDisplays(ExynosDisplay* firstDisplay,
        uint32_t* outNumTypes, uint32_t* outNumRequests)
{
    int32_t ret = NO_ERROR;
    uint32_t tmpOutNumTypes = 0;
    uint32_t tmpNumRequests = 0;

    HDEBUGLOGD(eDebugResourceManager, "Validate all of displays ++++++++++++++++++++++++++++++++");
    for (int32_t i = (mDisplays.size() - 1); i >= 0; i--) {
        /*
         * No skip validation if SurfaceFlinger calls validateDisplay
         * even though plug state is false.
         * HWC should return valid outNumTypes and outNumRequests if
         * SurfaceFlinger calls validateDisplay.
         */
        if (mDisplays[i] != firstDisplay) {
            if ((mDisplays[i]->mType != HWC_DISPLAY_VIRTUAL) &&
                (mDisplays[i]->mPowerModeState == (hwc2_power_mode_t)HWC_POWER_MODE_OFF))
                continue;
            if (mDisplays[i]->mPlugState == false)
                continue;

            /* exynos9810 specific source code */
            if (mDisplays[i]->mType == HWC_DISPLAY_EXTERNAL)
            {
                ExynosExternalDisplay *extDisp = (ExynosExternalDisplay*)mDisplays[i];
                if (extDisp->mBlanked == true)
                    continue;
            }
        }

        int32_t displayRet = NO_ERROR;

        /* No skips validate and present */
        mDisplays[i]->mNeedSkipValidatePresent = false;
        if (mDisplays[i] == firstDisplay)
            displayRet = mDisplays[i]->validateDisplay(outNumTypes, outNumRequests);
        else {
            if ((mDisplays[i]->mNeedSkipPresent) &&
                (mDisplays[i]->mRenderingStateFlags.presentFlag == true)) {
                /*
                 * Present was already skipped after power is turned on.
                 * Surfaceflinger will not call present() again in this frame.
                 * So validateDisplays() should not validate this display
                 * and should not start m2mMPP.
                 */
                HDEBUGLOGD(eDebugResourceManager,
                        "%s:: validate is skipped in validateDisplays (mNeedSkipPresent is set)",
                        mDisplays[i]->mDisplayName.string());
                continue;
            }
            displayRet = mDisplays[i]->validateDisplay(&tmpOutNumTypes, &tmpNumRequests);
        }

        if ((displayRet != NO_ERROR) &&
            (displayRet != HWC2_ERROR_HAS_CHANGES)) {
            mDisplays[i]->setGeometryChanged(GEOMETRY_ERROR_CASE);
            HWC_LOGE(mDisplays[i], "%s:: validate fail for display[%d] firstValidate(%d), ret(%d)",
                    __func__, i,
                    mDisplays[i] == firstDisplay ? 1 : 0,
                    displayRet);
            ret = displayRet;
        }
    }
    HDEBUGLOGD(eDebugResourceManager, "Validate all of displays ----------------------------------");
    return ret;
}

bool ExynosDevice::makeCPUPerfTable(ExynosDisplay *display, hwc2_config_t config) {

#ifdef USES_HWC_CPU_PERF_MODE
    /* Remake CPU performance table from file */

    if (mPerfTableFd != NULL)
        fclose(mPerfTableFd);

    mPerfTableFd = fopen(CPU_PERF_TABLE_PATH, "r");
    if (mPerfTableFd == NULL) {
        ALOGE("%s open failed", CPU_PERF_TABLE_PATH);
        return false;
    }

    rewind(mPerfTableFd);

    for (auto it = perfTable.begin(); it != perfTable.end(); ++it) {
        //uint32_t i = it->first;
        int fps;
        if (fscanf(mPerfTableFd, "%u ", &fps) < 0)
            return false;
        perfMap tempMap =  perfTable.at(fps);

        if (fscanf(mPerfTableFd, "%u ", &tempMap.cpuIDs) < 0)
            return false;
        for (int cl_num = 0; cl_num < CPU_CLUSTER_CNT; cl_num++) {
            if (fscanf(mPerfTableFd, "%u ", &tempMap.minClock[cl_num]) < 0)
                return false;
        }
        if (fscanf(mPerfTableFd, "%u\n", &tempMap.m2mCapa) < 0)
            return false;

        perfTable[fps] = tempMap;
    }

    /* Fill the display configs structure */
    for (int cfgId = 0; cfgId < display->mDisplayConfigs.size(); cfgId++) {

        int fps = (int)(1000000000 / display->mDisplayConfigs[cfgId].vsyncPeriod);

        const auto it = perfTable.find(fps);
        if (it == perfTable.end()) {
            ALOGI("Can not find performance table for fps (%d)", fps);
            display->mDisplayConfigs[cfgId].cpuIDs = 0;
            for (int cl_num = 0; cl_num < CPU_CLUSTER_CNT; cl_num++)
                display->mDisplayConfigs[cfgId].minClock[cl_num] = 0;
            display->mDisplayConfigs[cfgId].m2mCapa = MPP_G2D_CAPACITY;
        } else {
            display->mDisplayConfigs[cfgId].cpuIDs = perfTable[fps].cpuIDs;
            for (int cl_num = 0; cl_num < CPU_CLUSTER_CNT; cl_num++)
                display->mDisplayConfigs[cfgId].minClock[cl_num] = perfTable[fps].minClock[cl_num];
            display->mDisplayConfigs[cfgId].m2mCapa = perfTable[fps].m2mCapa;
        }
    }

    /* Dump display Configs */
    for (int cfgId = 0; cfgId < display->mDisplayConfigs.size(); cfgId++) {
        ALOGI("ID : %d, cpuIDs : %d", cfgId, display->mDisplayConfigs[cfgId].cpuIDs);
        for (int cl_num = 0; cl_num < CPU_CLUSTER_CNT; cl_num++)
            ALOGI("minClock : %d", display->mDisplayConfigs[cfgId].minClock[cl_num]);
        ALOGI("m2mCapa : %d", display->mDisplayConfigs[cfgId].m2mCapa);
    }
#endif
    return true;
}

void ExynosDevice::performanceAssurance(ExynosDisplay *display, hwc2_config_t config)
{
    /* Find out in module which is support or not */
    if (!supportPerformaceAssurance() ||
        display->mDisplayConfigs.empty() ||
        display->mDisplayConfigs.size() <= 1) return;

    const auto it = display->mDisplayConfigs.find(config);
    if (it == display->mDisplayConfigs.end()) return;

    /* Getting CPU perf Table */
    if (!makeCPUPerfTable(display, config)) {
        ALOGE("Fail to read CPU perf map");
        return;
    }

    /* Affinity settings */
    cpu_set_t  mask;
    CPU_ZERO(&mask); // Clear mask
    ALOGI("Set Affinity config : %d, cpuIDs : %d", config, display->mDisplayConfigs[config].cpuIDs);
    for (int cpu_no = 0; cpu_no < 32; cpu_no++) {
        if (display->mDisplayConfigs[config].cpuIDs & (1 << cpu_no)) {
            CPU_SET(cpu_no, &mask);
            ALOGI("Set Affinity CPU ID : %d", cpu_no);
        }
    }
    sched_setaffinity(getpid(), sizeof(cpu_set_t), &mask);

    ALOGI("Set affinity HWC : %d", getpid());

    /* TODO cluster modification in module */
    setCPUClocksPerCluster(display, config);

    /* TODO MPP capacity settings */
    mResourceManager->setM2MCapa(MPP_G2D, display->mDisplayConfigs[config].m2mCapa);
    return;
}

bool ExynosDevice::getCPUPerfInfo(int display, int config, int32_t *cpuIds, int32_t *min_clock) {

    ExynosDisplay *primary_display = mDisplays[display];
    if (primary_display == NULL) return false;

    /* Find out in module which is support or not */
    if (!supportPerformaceAssurance() ||
        primary_display->mDisplayConfigs.empty() ||
        primary_display->mDisplayConfigs.size() <= 1) return false;

    *cpuIds = primary_display->mDisplayConfigs[config].cpuIDs;
    /* SurfaceFlinger not control CPU clock */
    *min_clock = 0;
    ALOGI("CPU perf : Display(%d), Config(%d), Affinity(%d)", display, config, *cpuIds);

    return true;
}

int ExynosDevice::isNeedCompressedTargetBuffer(uint64_t displayId)
{
    ExynosDisplay *target_display = getDisplay(displayId);
    char value[256];
    int afbc_prop;
    property_get("ro.vendor.ddk.set.afbc", value, "0");
    afbc_prop = atoi(value);

    // When AFBC is disabled in system, HWC will not concern Client target buffer flag setting.
    // So HWC will return true in this function.
    // When the return value is -1, use default flag(GRALLOC_USAGE_HW_RENDER).
    // When the return value is 0, use custom flag.
    // When the return value is 1, use default flag but check product module function.
    if ((afbc_prop == 0) || (target_display == NULL) || (target_display->mType == HWC_DISPLAY_VIRTUAL))
        return -1; //Bypass


    for (size_t i = 0; i < mResourceManager->getOtfMPPSize(); i++) {
        ExynosMPP *temp = mResourceManager->getOtfMPP(i);
        if (SELECTED_LOGIC == UNDEFINED) {
            if ((temp->mAttr & MPP_ATTR_AFBC) &&
                    (temp->mPreAssignDisplayList[mDisplayMode] & (1 << (target_display->mType)))) {
                target_display->mClientCompositionInfo.mCompressed = true;
                target_display->mExynosCompositionInfo.mCompressed = true;
                return 1;
            }
        } else {
            if (!(temp->mAttr & MPP_ATTR_AFBC))
                continue;

            for (size_t j = 0; j < target_display->mAssignedResourceSet.size(); j++) {
                for (uint32_t k = 0; k < MAX_DPPCNT_PER_SET; k++) {
                    if (!strcmp(target_display->mAssignedResourceSet[j]->dpps[k], temp->mName)) {
                        target_display->mClientCompositionInfo.mCompressed = true;
                        target_display->mExynosCompositionInfo.mCompressed = true;
                        return 1;
                    }
                }
            }
        }
    }

    target_display->mClientCompositionInfo.mCompressed = false;
    target_display->mExynosCompositionInfo.mCompressed = false;
    return 0;
}
