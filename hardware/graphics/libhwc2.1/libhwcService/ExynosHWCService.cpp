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
#include "ExynosHWCService.h"
#include "ExynosVirtualDisplayModule.h"
#include "ExynosVirtualDisplay.h"
#include "ExynosExternalDisplay.h"
#define HWC_SERVICE_DEBUG 0

namespace android {

ANDROID_SINGLETON_STATIC_INSTANCE(ExynosHWCService);

ExynosHWCService::ExynosHWCService() :
    mHWCService(NULL),
    mHWCCtx(NULL),
    bootFinishedCallback(NULL),
    doPSRExit(NULL)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "ExynosHWCService Constructor is called");
}

ExynosHWCService::~ExynosHWCService()
{
   ALOGD_IF(HWC_SERVICE_DEBUG, "ExynosHWCService Destructor is called");
}

int ExynosHWCService::addVirtualDisplayDevice()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    mHWCCtx->device->mNumVirtualDisplay++;

    return NO_ERROR;
}

int ExynosHWCService::destroyVirtualDisplayDevice()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    mHWCCtx->device->mNumVirtualDisplay--;

    return NO_ERROR;
}

int ExynosHWCService::setWFDMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setWFDMode(mode);
        }
    }
    return INVALID_OPERATION;
}

int ExynosHWCService::getWFDMode()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->getWFDMode();
        }
    }
    return INVALID_OPERATION;
}

int ExynosHWCService::getWFDInfo(int32_t* state, int32_t* compositionType, int32_t* format,
    int64_t *usage, int32_t* width, int32_t* height)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->getWFDInfo(state, compositionType, format,
                       usage, width, height);
        }
    }
    return INVALID_OPERATION;
}

int ExynosHWCService::sendWFDCommand(int32_t cmd, int32_t ext1, int32_t ext2)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::cmd=%d, ext1=%d, ext2=%d", __func__, cmd, ext1, ext2);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->sendWFDCommand(cmd, ext1, ext2);
        }
    }
    return INVALID_OPERATION;
}

int ExynosHWCService::setSecureVDSMode(unsigned int mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::mode=%d", __func__, mode);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setSecureVDSMode(mode);
        }
    }
    return INVALID_OPERATION;
}

int ExynosHWCService::setWFDOutputResolution(unsigned int width, unsigned int height)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::width=%d, height=%d", __func__, width, height);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setWFDOutputResolution(width, height);
        }
    }
    return INVALID_OPERATION;
}

void ExynosHWCService::getWFDOutputResolution(unsigned int *width, unsigned int *height)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            virtualdisplay->getWFDOutputResolution(width, height);
            return;
        }
    }
}

void ExynosHWCService::setPresentationMode(bool use)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::PresentationMode=%s", __func__, use == false ? "false" : "true");
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            virtualdisplay->setPresentationMode(!!use);
            return;
        }
    }
}

int ExynosHWCService::getPresentationMode()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->getPresentationMode();
        }
    }
    return INVALID_OPERATION;
}

int ExynosHWCService::setVDSGlesFormat(int format)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::format=%d", __func__, format);

    for (uint32_t i = 0; i < mHWCCtx->device->mDisplays.size(); i++) {
        if (mHWCCtx->device->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) {
            ExynosVirtualDisplay *virtualdisplay =
                (ExynosVirtualDisplay *)mHWCCtx->device->mDisplays[i];
            return virtualdisplay->setVDSGlesFormat(format);
        }
    }

    return INVALID_OPERATION;
}

int ExynosHWCService::setExternalVsyncEnabled(unsigned int index)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::config=%d", __func__, index);

    mHWCCtx->device->mVsyncDisplayId = index;
    ExynosExternalDisplay *external_display =
        (ExynosExternalDisplay *)mHWCCtx->device->getDisplay(getDisplayId(HWC_DISPLAY_EXTERNAL, 0));
    external_display->setVsyncEnabled(index);

    return NO_ERROR;
}

int ExynosHWCService::getExternalHdrCapabilities()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);

    ExynosExternalDisplay *external_display =
        (ExynosExternalDisplay *)mHWCCtx->device->getDisplay(getDisplayId(HWC_DISPLAY_EXTERNAL, 0));

    return external_display->mExternalHdrSupported;
}

void ExynosHWCService::setBootFinishedCallback(void (*callback)(ExynosHWCCtx *))
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, callback %p", __func__, callback);
    bootFinishedCallback = callback;
}

void ExynosHWCService::setBootFinished() {
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    if (bootFinishedCallback != NULL)
        bootFinishedCallback(mHWCCtx);
}

void ExynosHWCService::enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable)
{
    ALOGD("%s:: type(%d), index(%d, %d), enable(%d)",
            __func__, physicalType, physicalIndex, logicalIndex, enable);
    ExynosResourceManager::enableMPP(physicalType, physicalIndex, logicalIndex, enable);
    mHWCCtx->device->setGeometryChanged(GEOMETRY_DEVICE_CONFIG_CHANGED);
    mHWCCtx->device->invalidate();
}

void ExynosHWCService::setDumpCount(uint32_t dumpCount)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, dmpCount(%d)", __func__, dumpCount);
    mHWCCtx->device->mTotalDumpCount = dumpCount;
    mHWCCtx->device->mIsDumpRequest = true;
    mHWCCtx->device->invalidate();
    return;
}

void ExynosHWCService::setHWCDebug(int debug)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, debug %d", __func__, debug);
    mHWCCtx->device->setHWCDebug(debug);
}

uint32_t ExynosHWCService::getHWCDebug()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    return mHWCCtx->device->getHWCDebug();
}

void ExynosHWCService::setHWCFenceDebug(uint32_t fenceNum, uint32_t ipNum, uint32_t mode)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    mHWCCtx->device->setHWCFenceDebug(fenceNum, ipNum, mode);
}

void ExynosHWCService::getHWCFenceDebug()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s", __func__);
    mHWCCtx->device->getHWCFenceDebug();
}

int ExynosHWCService::setHWCCtl(uint32_t display, uint32_t ctrl, int32_t val)
{
    int err = 0;
    switch (ctrl) {
    case HWC_CTL_FORCE_GPU:
    case HWC_CTL_WINDOW_UPDATE:
    case HWC_CTL_FORCE_PANIC:
    case HWC_CTL_SKIP_STATIC:
    case HWC_CTL_SKIP_M2M_PROCESSING:
    case HWC_CTL_SKIP_RESOURCE_ASSIGN:
    case HWC_CTL_SKIP_VALIDATE:
    case HWC_CTL_DUMP_MID_BUF:
    case HWC_CTL_ENABLE_COMPOSITION_CROP:
    case HWC_CTL_ENABLE_EXYNOSCOMPOSITION_OPT:
    case HWC_CTL_ENABLE_CLIENTCOMPOSITION_OPT:
    case HWC_CTL_USE_MAX_G2D_SRC:
    case HWC_CTL_ENABLE_HANDLE_LOW_FPS:
    case HWC_CTL_ENABLE_EARLY_START_MPP:
    case HWC_CTL_DISPLAY_MODE:
    case HWC_CTL_DDI_RESOLUTION_CHANGE:
    case HWC_CTL_ENABLE_FENCE_TRACER:
    case HWC_CTL_SYS_FENCE_LOGGING:
    case HWC_CTL_DO_FENCE_FILE_DUMP:
        ALOGI("%s::%d on/off=%d", __func__, ctrl, val);
        mHWCCtx->device->setHWCControl(display, ctrl, val);
        break;
    default:
        ALOGE("%s: unsupported HWC_CTL, (%d)", __func__, ctrl);
        err = -1;
        break;
    }
    return err;
}

int ExynosHWCService::setDDIScaler(uint32_t width, uint32_t height)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, width=%d, height=%d", __func__, width, height);
    if (mHWCCtx) {
        ExynosDisplay *display = (ExynosDisplay*)mHWCCtx->device->getDisplay(getDisplayId(HWC_DISPLAY_PRIMARY, 0));

        if (display == NULL)
            return -EINVAL;

        display->setDDIScalerEnable(width, height);
        return NO_ERROR;
    } else {
        ALOGE_IF(HWC_SERVICE_DEBUG, "Service is not exist");
        return -EINVAL;
    }
}

#if 0
void ExynosHWCService::setPSRExitCallback(void (*callback)(exynos_hwc_composer_device_1_t *))
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, callback %p", __func__, callback);
    doPSRExit = callback;
}

void ExynosHWCService::notifyPSRExit()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, doPSRExit %p", __func__, doPSRExit);
    if (doPSRExit != NULL) {
        ALOGD_IF(HWC_SERVICE_DEBUG, "%s, line %d", __func__, __LINE__);
        doPSRExit(mHWCCtx);
    }
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, line %d", __func__, __LINE__);
}

#endif

int ExynosHWCService::createServiceLocked()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::", __func__);
    sp<IServiceManager> sm = defaultServiceManager();
    sm->addService(String16("Exynos.HWCService"), mHWCService, false);
    if (sm->checkService(String16("Exynos.HWCService")) != NULL) {
        ALOGD_IF(HWC_SERVICE_DEBUG, "adding Exynos.HWCService succeeded");
        return 0;
    } else {
        ALOGE_IF(HWC_SERVICE_DEBUG, "adding Exynos.HWCService failed");
        return -1;
    }
}

int ExynosHWCService::getCPUPerfInfo(int display, int config, int32_t *cpuIDs, int32_t *min_clock)
{
    return mHWCCtx->device->getCPUPerfInfo(display, config, cpuIDs, min_clock);
}

bool ExynosHWCService::isNeedCompressedTargetBuffer(uint64_t id)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, display(%" PRIu64 ")", __func__, id);
    int ret = mHWCCtx->device->isNeedCompressedTargetBuffer(id);
    // When AFBC is disabled in system, HWC will not concern Client target buffer flag setting.
    // So HWC will return true in this function.
    // When the return value is true, use default flag(GRALLOC_USAGE_HW_RENDER).
    // When the return value is false, use custom flag.
    ALOGI("DisplayID(%" PRIu64 ") : NeedCompressedTargetBuffer : %d", id, ret);
    return (ret != 0);
}

ExynosHWCService *ExynosHWCService::getExynosHWCService()
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s::", __func__);
    ExynosHWCService& instance = ExynosHWCService::getInstance();
    Mutex::Autolock _l(instance.mLock);
    if (instance.mHWCService == NULL) {
        instance.mHWCService = &instance;
        int status = ExynosHWCService::getInstance().createServiceLocked();
        if (status != 0) {
            ALOGE_IF(HWC_SERVICE_DEBUG, "getExynosHWCService failed");
        }
    }
    return instance.mHWCService;
}

void ExynosHWCService::setExynosHWCCtx(ExynosHWCCtx *HWCCtx)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "%s, HWCCtx=%p", __func__, HWCCtx);
    if(HWCCtx) {
        mHWCCtx = HWCCtx;
    }
}
} //namespace android
