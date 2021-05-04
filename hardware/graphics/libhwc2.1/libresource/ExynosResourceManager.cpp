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

/**
 * Project HWC 2.0 Design
 */

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)
#include <cutils/properties.h>
#include "ExynosResourceManager.h"
#include "ExynosMPPModule.h"
#include "ExynosResourceRestriction.h"
#include "ExynosLayer.h"
#include "ExynosHWCDebug.h"
#include "ExynosHWCHelper.h"
#include "hardware/exynos/acryl.h"
#include "ExynosPrimaryDisplayModule.h"
#include "ExynosVirtualDisplay.h"
#include "ExynosExternalDisplay.h"
#include "ExynosDeviceInterface.h"

#ifndef USE_MODULE_ATTR
/* Basic supported features */
feature_support_t feature_table[] =
{
    {MPP_DPP_G,
        MPP_ATTR_BLOCK_MODE | MPP_ATTR_WINDOW_UPDATE | MPP_ATTR_DIM | MPP_ATTR_CUSTOM_ROT
    },

    {MPP_DPP_GF,
        MPP_ATTR_AFBC | MPP_ATTR_BLOCK_MODE | MPP_ATTR_WINDOW_UPDATE | MPP_ATTR_DIM | MPP_ATTR_CUSTOM_ROT
    },

    {MPP_DPP_VG,
        MPP_ATTR_BLOCK_MODE | MPP_ATTR_WINDOW_UPDATE | MPP_ATTR_DIM | MPP_ATTR_CUSTOM_ROT
    },

    {MPP_DPP_VGS,
        MPP_ATTR_BLOCK_MODE | MPP_ATTR_WINDOW_UPDATE | MPP_ATTR_SCALE | MPP_ATTR_DIM | MPP_ATTR_CUSTOM_ROT
    },

    {MPP_DPP_VGF,
        MPP_ATTR_AFBC | MPP_ATTR_BLOCK_MODE | MPP_ATTR_WINDOW_UPDATE | MPP_ATTR_DIM | MPP_ATTR_CUSTOM_ROT
    },

    {MPP_DPP_VGFS,
        MPP_ATTR_AFBC | MPP_ATTR_BLOCK_MODE | MPP_ATTR_WINDOW_UPDATE | MPP_ATTR_SCALE | MPP_ATTR_DIM | MPP_ATTR_CUSTOM_ROT
    },

    {MPP_DPP_VGRFS,
        MPP_ATTR_AFBC | MPP_ATTR_BLOCK_MODE | MPP_ATTR_WINDOW_UPDATE | MPP_ATTR_SCALE |
        MPP_ATTR_FLIP_H | MPP_ATTR_FLIP_V | MPP_ATTR_ROT_90 | MPP_ATTR_ROT_180 | MPP_ATTR_ROT_270 | MPP_ATTR_CUSTOM_ROT |
        MPP_ATTR_DIM | MPP_ATTR_HDR10
    },

    {MPP_MSC,
        MPP_ATTR_FLIP_H | MPP_ATTR_FLIP_V | MPP_ATTR_ROT_90 | MPP_ATTR_ROT_180 | MPP_ATTR_ROT_270
    },

    {MPP_G2D,
        MPP_ATTR_AFBC | MPP_ATTR_FLIP_H | MPP_ATTR_FLIP_V | MPP_ATTR_ROT_90 | MPP_ATTR_ROT_180 | MPP_ATTR_ROT_270 |
        MPP_ATTR_HDR10 | MPP_ATTR_USE_CAPA
    }
};
#endif

using namespace android;

ExynosMPPVector ExynosResourceManager::mOtfMPPs;
ExynosMPPVector ExynosResourceManager::mM2mMPPs;
extern struct exynos_hwc_control exynosHWCControl;

ExynosMPPVector::ExynosMPPVector() {
}

ExynosMPPVector::ExynosMPPVector(const ExynosMPPVector& rhs)
    : android::SortedVector<ExynosMPP* >(rhs) {
}

int ExynosMPPVector::do_compare(const void* lhs, const void* rhs) const
{
    if (lhs == NULL || rhs == NULL)
        return 0;

    const ExynosMPP* l = *((ExynosMPP**)(lhs));
    const ExynosMPP* r = *((ExynosMPP**)(rhs));

    if (l == NULL || r == NULL)
        return 0;

    if (l->mPhysicalType != r->mPhysicalType) {
        return l->mPhysicalType - r->mPhysicalType;
    }

    if (l->mLogicalType != r->mLogicalType) {
        return l->mLogicalType - r->mLogicalType;
    }

    if (l->mPhysicalIndex != r->mPhysicalIndex) {
        return l->mPhysicalIndex - r->mPhysicalIndex;
    }

    return l->mLogicalIndex - r->mLogicalIndex;
}

/**
 * ExynosResourceManager implementation
 *
 */

ExynosResourceManager::DstBufMgrThread::DstBufMgrThread(ExynosResourceManager *exynosResourceManager)
: mExynosResourceManager(exynosResourceManager),
    mRunning(false),
    mBufXres(0),
    mBufYres(0)
{
}

ExynosResourceManager::DstBufMgrThread::~DstBufMgrThread()
{
}


ExynosResourceManager::ExynosResourceManager(ExynosDevice *device)
: mForceReallocState(DST_REALLOC_DONE),
    mDevice(device),
    hasHdrLayer(false),
    hasDrmLayer(false),
    mFormatRestrictionCnt(0),
    mDstBufMgrThread(sp<DstBufMgrThread>::make(this))
{

    memset(mSizeRestrictionCnt, 0, sizeof(mSizeRestrictionCnt));
    memset(mFormatRestrictions, 0, sizeof(mFormatRestrictions));
    memset(mSizeRestrictions, 0, sizeof(mSizeRestrictions));

    size_t num_mpp_units = sizeof(AVAILABLE_OTF_MPP_UNITS)/sizeof(exynos_mpp_t);
    for (size_t i = 0; i < num_mpp_units; i++) {
        exynos_mpp_t exynos_mpp = AVAILABLE_OTF_MPP_UNITS[i];
        ALOGI("otfMPP type(%d, %d), physical_index(%d), logical_index(%d)",
                exynos_mpp.physicalType, exynos_mpp.logicalType,
                exynos_mpp.physical_index, exynos_mpp.logical_index);
        ExynosMPP* exynosMPP = new ExynosMPPModule(this, exynos_mpp.physicalType,
                exynos_mpp.logicalType, exynos_mpp.name, exynos_mpp.physical_index,
                exynos_mpp.logical_index, exynos_mpp.pre_assign_info);
        exynosMPP->mMPPType = MPP_TYPE_OTF;
        mOtfMPPs.add(exynosMPP);
    }

    num_mpp_units = sizeof(AVAILABLE_M2M_MPP_UNITS)/sizeof(exynos_mpp_t);
    for (size_t i = 0; i < num_mpp_units; i++) {
        exynos_mpp_t exynos_mpp = AVAILABLE_M2M_MPP_UNITS[i];
        ALOGI("m2mMPP type(%d, %d), physical_index(%d), logical_index(%d)",
                exynos_mpp.physicalType, exynos_mpp.logicalType,
                exynos_mpp.physical_index, exynos_mpp.logical_index);
        ExynosMPP* exynosMPP = new ExynosMPPModule(this, exynos_mpp.physicalType,
                exynos_mpp.logicalType, exynos_mpp.name, exynos_mpp.physical_index,
                exynos_mpp.logical_index, exynos_mpp.pre_assign_info);
        exynosMPP->mMPPType = MPP_TYPE_M2M;
        mM2mMPPs.add(exynosMPP);
    }

    ALOGI("mOtfMPPs(%zu), mM2mMPPs(%zu)", mOtfMPPs.size(), mM2mMPPs.size());
    if (hwcCheckDebugMessages(eDebugResourceManager)) {
        for (uint32_t i = 0; i < mOtfMPPs.size(); i++)
        {
            HDEBUGLOGD(eDebugResourceManager, "otfMPP[%d]", i);
            String8 dumpMPP;
            mOtfMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++)
        {
            HDEBUGLOGD(eDebugResourceManager, "m2mMPP[%d]", i);
            String8 dumpMPP;
            mM2mMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }

    mDstBufMgrThread->mRunning = true;
    mDstBufMgrThread->run("DstBufMgrThread");
}

ExynosResourceManager::~ExynosResourceManager()
{
    for (int32_t i = mOtfMPPs.size(); i-- > 0;) {
        ExynosMPP *exynosMPP = mOtfMPPs[i];
        delete exynosMPP;
    }
    mOtfMPPs.clear();
    for (int32_t i = mM2mMPPs.size(); i-- > 0;) {
        ExynosMPP *exynosMPP = mM2mMPPs[i];
        delete exynosMPP;
    }
    mM2mMPPs.clear();

    mDstBufMgrThread->mRunning = false;
    mDstBufMgrThread->requestExitAndWait();
}

void ExynosResourceManager::reloadResourceForHWFC()
{
    for (int32_t i = mM2mMPPs.size(); i-- > 0;) {
        ExynosMPP *exynosMPP = mM2mMPPs[i];
        if (exynosMPP->mLogicalType == MPP_LOGICAL_G2D_COMBO &&
                (exynosMPP->mPreAssignDisplayInfo & HWC_DISPLAY_VIRTUAL_BIT)) {
            exynosMPP->reloadResourceForHWFC();
            break;
        }
    }
}

void ExynosResourceManager::setTargetDisplayLuminance(uint16_t min, uint16_t max)
{
    for (int32_t i = mM2mMPPs.size(); i-- > 0;) {
        ExynosMPP *exynosMPP = mM2mMPPs[i];
        if (exynosMPP->mLogicalType == MPP_LOGICAL_G2D_COMBO &&
                (exynosMPP->mPreAssignDisplayInfo & HWC_DISPLAY_VIRTUAL_BIT)) {
            exynosMPP->setTargetDisplayLuminance(min, max);
            break;
        }
    }
}

void ExynosResourceManager::setTargetDisplayDevice(int device)
{
    for (int32_t i = mM2mMPPs.size(); i-- > 0;) {
        ExynosMPP *exynosMPP = mM2mMPPs[i];
        if (exynosMPP->mLogicalType == MPP_LOGICAL_G2D_COMBO &&
                (exynosMPP->mPreAssignDisplayInfo & HWC_DISPLAY_VIRTUAL_BIT)) {
            exynosMPP->setTargetDisplayDevice(device);
            break;
        }
    }
}

int32_t ExynosResourceManager::doPreProcessing()
{
    int32_t ret = NO_ERROR;
    /* Assign m2mMPP's out buffers */
    ExynosDisplay *display = mDevice->getDisplay(getDisplayId(HWC_DISPLAY_PRIMARY, 0));
    if (display == NULL)
        return -EINVAL;
    ret = doAllocDstBufs(display->mXres, display->mYres);
    return ret;
}

void ExynosResourceManager::doReallocDstBufs(uint32_t Xres, uint32_t Yres)
{
    HDEBUGLOGD(eDebugBuf, "M2M dst alloc call ");
    mDstBufMgrThread->reallocDstBufs(Xres, Yres);
}

bool ExynosResourceManager::DstBufMgrThread::needDstRealloc(uint32_t Xres, uint32_t Yres, ExynosMPP *m2mMPP)
{
    bool ret = false;
    if ((Xres*Yres) != m2mMPP->getDstAllocSize())
        ret = true;

    return ret;
}

void ExynosResourceManager::DstBufMgrThread::reallocDstBufs(uint32_t Xres, uint32_t Yres)
{
    bool needRealloc = false;
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->needPreAllocation())
        {
            if (needDstRealloc(Xres, Yres, mM2mMPPs[i])) {
                HDEBUGLOGD(eDebugBuf, "M2M dst alloc : %d Realloc Start ++++++", mM2mMPPs[i]->mLogicalType);
                needRealloc = true;
            }
            else HDEBUGLOGD(eDebugBuf, "M2M dst alloc : %d MPP's DST Realloc is not needed : Size is same", mM2mMPPs[i]->mLogicalType);
        }
    }

    if (needRealloc) {
        Mutex::Autolock lock(mStateMutex);
        if (mExynosResourceManager->mForceReallocState == DST_REALLOC_DONE) {
            mExynosResourceManager->mForceReallocState = DST_REALLOC_START;
            android::Mutex::Autolock lock(mMutex);
            mCondition.signal();
        } else {
            HDEBUGLOGD(eDebugBuf, "M2M dst alloc thread : queue aready.");
        }
    }
}

bool ExynosResourceManager::DstBufMgrThread::threadLoop()
{
    while(mRunning) {
        Mutex::Autolock lock(mMutex);
        mCondition.wait(mMutex);

        ExynosDevice *device = mExynosResourceManager->mDevice;
        if (device == NULL)
            return false;
        ExynosDisplay *display = device->getDisplay(getDisplayId(HWC_DISPLAY_PRIMARY, 0));
        if (display == NULL)
            return false;

        do {
            {
                HDEBUGLOGD(eDebugBuf, "M2M dst alloc %d, %d, %d, %d : Realloc On going ----------",
                        mBufXres, display->mXres, mBufYres, display->mYres);
                Mutex::Autolock lock(mResInfoMutex);
                mBufXres = display->mXres;mBufYres = display->mYres;
            }
            mExynosResourceManager->doAllocDstBufs(mBufXres, mBufYres);
        } while (mBufXres != display->mXres || mBufYres != display->mYres);

        {
            Mutex::Autolock lock(mStateMutex);
            mExynosResourceManager->mForceReallocState = DST_REALLOC_DONE;
            HDEBUGLOGD(eDebugBuf, "M2M dst alloc %d, %d, %d, %d : Realloc On Done ----------",
                    mBufXres, display->mXres, mBufYres, display->mYres);
        }
    }
    return true;
}

int32_t ExynosResourceManager::doAllocDstBufs(uint32_t Xres, uint32_t Yres)
{
    ATRACE_CALL();
    int32_t ret = NO_ERROR;
    /* Assign m2mMPP's out buffers */

    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->needPreAllocation())
        {
            mM2mMPPs[i]->mFreeOutBufFlag = false;
            for (uint32_t index = 0; index < NUM_MPP_DST_BUFS(mM2mMPPs[i]->mLogicalType); index++) {
                HDEBUGLOGD(eDebugBuf, "%s allocate dst buffer[%d]%p, x: %d, y: %d",
                        __func__, index, mM2mMPPs[i]->mDstImgs[index].bufferHandle, Xres, Yres);
                uint32_t bufAlign = mM2mMPPs[i]->getOutBufAlign();
                ret = mM2mMPPs[i]->allocOutBuf(ALIGN_UP(Xres, bufAlign),
                        ALIGN_UP(Yres, bufAlign),
                        DEFAULT_MPP_DST_FORMAT, 0x0, index);
                if (ret < 0) {
                    HWC_LOGE(NULL, "%s:: fail to allocate dst buffer[%d]",
                            __func__, index);
                    return ret;
                }
                mM2mMPPs[i]->mPrevAssignedDisplayType = HWC_DISPLAY_PRIMARY;
            }
            mM2mMPPs[i]->setDstAllocSize(Xres, Yres);
        }
    }
    return ret;
}

/**
 * @param * display
 * @return int
 */
int32_t ExynosResourceManager::assignResource(ExynosDisplay *display)
{
    ATRACE_CALL();
    int ret = 0;
    if ((mDevice == NULL) || (display == NULL))
        return -EINVAL;

    HDEBUGLOGD(eDebugResourceManager|eDebugSkipResourceAssign, "mGeometryChanged(0x%" PRIx64 "), display(%d)",
            mDevice->mGeometryChanged, display->mType);

    if (mDevice->mGeometryChanged == 0) {
        return NO_ERROR;
    }

    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        display->mLayers[i]->resetValidateData();
    }

    display->initializeValidateInfos();

    if ((ret = preProcessLayer(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: preProcessLayer() error (%d)",
                __func__, ret);
        return ret;
    }

    if (mDevice->isFirstValidate(display)) {
        HDEBUGLOGD(eDebugResourceManager, "This is first validate");
        if (exynosHWCControl.displayMode < DISPLAY_MODE_NUM)
            mDevice->mDisplayMode = exynosHWCControl.displayMode;

        if ((ret = prepareResources()) != NO_ERROR) {
            HWC_LOGE(display, "%s:: prepareResources() error (%d)",
                    __func__, ret);
            return ret;
        }
        preAssignM2MResources();
        /* It should be called after prepareResources
           because it needs that MPPs->mReservedDisplay is not null */
        setM2mTargetCompression();
    }

    for (size_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mReservedDisplay == display->mDisplayId)
            mM2mMPPs[i]->mPreAssignedCapacity = 0.0f;
    }

    if ((ret = updateSupportedMPPFlag(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: updateSupportedMPPFlag() error (%d)",
                __func__, ret);
        return ret;
    }

    if ((ret = assignResourceInternal(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: assignResourceInternal() error (%d)",
                __func__, ret);
        return ret;
    }

    if ((ret = assignWindow(display)) != NO_ERROR) {
        HWC_LOGE(display, "%s:: assignWindow() error (%d)",
                __func__, ret);
        return ret;
    }

    if (hwcCheckDebugMessages(eDebugResourceManager)) {
        HDEBUGLOGD(eDebugResourceManager, "AssignResource result");
        String8 result;
        display->mClientCompositionInfo.dump(result);
        HDEBUGLOGD(eDebugResourceManager, "%s", result.string());
        result.clear();
        display->mExynosCompositionInfo.dump(result);
        HDEBUGLOGD(eDebugResourceManager, "%s", result.string());
        for (uint32_t i = 0; i < display->mLayers.size(); i++) {
            result.clear();
            HDEBUGLOGD(eDebugResourceManager, "%d layer(%p) dump", i, display->mLayers[i]);
            display->mLayers[i]->printLayer();
            HDEBUGLOGD(eDebugResourceManager, "%s", result.string());
        }
    }

    if (!display->mUseDpu) {
        if (display->mClientCompositionInfo.mHasCompositionLayer) {
            if ((ret = display->mExynosCompositionInfo.mM2mMPP->assignMPP(display, &display->mClientCompositionInfo)) != NO_ERROR)
            {
                ALOGE("%s:: %s MPP assignMPP() error (%d)",
                        __func__, display->mExynosCompositionInfo.mM2mMPP->mName.string(), ret);
                return ret;
            }
            int prevHasCompositionLayer = display->mExynosCompositionInfo.mHasCompositionLayer;
            display->mExynosCompositionInfo.mHasCompositionLayer = true;
            // if prevHasCompositionLayer is false, setResourcePriority is not called
            if (prevHasCompositionLayer == false)
                setResourcePriority(display);
        }
    }

    return NO_ERROR;
}

int32_t ExynosResourceManager::setResourcePriority(ExynosDisplay *display)
{
    int ret = NO_ERROR;
    int check_ret = NO_ERROR;
    ExynosMPP *m2mMPP = NULL;

    m2mMPP = display->mExynosCompositionInfo.mM2mMPP;

    if (m2mMPP == NULL || m2mMPP->mPhysicalType == MPP_MSC) return NO_ERROR;

    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        if ((layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) &&
            (layer->mM2mMPP != NULL) &&
            (layer->mM2mMPP->mPhysicalType == MPP_G2D) &&
            ((check_ret = layer->mM2mMPP->prioritize(2)) != NO_ERROR)) {
            if (check_ret < 0) {
                HWC_LOGE(display, "Fail to set exynoscomposition priority(%d)", ret);
            } else {
                m2mMPP = layer->mM2mMPP;
                layer->resetAssignedResource();
                layer->mOverlayInfo |= eResourcePendingWork;
                layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                ret = EXYNOS_ERROR_CHANGED;
                HDEBUGLOGD(eDebugResourceManager, "\t%s is reserved without display because of panding work",
                        m2mMPP->mName.string());
                m2mMPP->reserveMPP();
                layer->mCheckMPPFlag[m2mMPP->mLogicalType] = eMPPHWBusy;
            }
        }
    }

    m2mMPP = display->mExynosCompositionInfo.mM2mMPP;
    ExynosCompositionInfo &compositionInfo = display->mExynosCompositionInfo;
    if (compositionInfo.mHasCompositionLayer == true)
    {
        if ((m2mMPP == NULL) || (m2mMPP->mAcrylicHandle == NULL)) {
            HWC_LOGE(display, "There is exynos composition layers but resource is null (%p)",
                    m2mMPP);
        } else if ((check_ret = m2mMPP->prioritize(2)) != NO_ERROR) {
            HDEBUGLOGD(eDebugResourceManager, "%s setting priority error(%d)", m2mMPP->mName.string(), check_ret);
            if (check_ret < 0) {
                HWC_LOGE(display, "Fail to set exynoscomposition priority(%d)", ret);
            } else {
                uint32_t firstIndex = (uint32_t)display->mExynosCompositionInfo.mFirstIndex;
                uint32_t lastIndex = (uint32_t)display->mExynosCompositionInfo.mLastIndex;
                for (uint32_t i = firstIndex; i <= lastIndex; i++) {
                    if (display->mExynosCompositionInfo.mFirstIndex == -1)
                        break;
                    ExynosLayer *layer = display->mLayers[i];
                    layer->resetAssignedResource();
                    layer->mOverlayInfo |= eResourcePendingWork;
                    layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                    layer->mCheckMPPFlag[m2mMPP->mLogicalType] = eMPPHWBusy;
                }
                compositionInfo.initializeInfos(display);
                ret = EXYNOS_ERROR_CHANGED;
                m2mMPP->resetUsedCapacity();
                HDEBUGLOGD(eDebugResourceManager, "\t%s is reserved without display because of pending work",
                        m2mMPP->mName.string());
                m2mMPP->reserveMPP();
            }
        } else {
            HDEBUGLOGD(eDebugResourceManager, "%s setting priority is ok", m2mMPP->mName.string());
        }
    }

    return ret;
}

int32_t ExynosResourceManager::assignResourceInternal(ExynosDisplay *display)
{
    int ret = NO_ERROR;
    int retry_count = 0;

    Mutex::Autolock lock(mDstBufMgrThread->mStateMutex);

    /*
     * First add layers that SF requested HWC2_COMPOSITION_CLIENT type
     * to client composition
     */
    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        if (layer->mCompositionType == HWC2_COMPOSITION_CLIENT) {
            layer->mOverlayInfo |= eSkipLayer;
            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            if (((ret = display->addClientCompositionLayer(i)) != NO_ERROR) &&
                 (ret != EXYNOS_ERROR_CHANGED)) {
                HWC_LOGE(display, "Handle HWC2_COMPOSITION_CLIENT type layers, but addClientCompositionLayer failed (%d)", ret);
                return ret;
            }
        }
    }

    do {
        HDEBUGLOGD(eDebugResourceAssigning, "%s:: retry_count(%d)", __func__, retry_count);
        if ((ret = resetAssignedResources(display)) != NO_ERROR)
            return ret;
        if ((ret = assignCompositionTarget(display, COMPOSITION_CLIENT)) != NO_ERROR) {
            HWC_LOGE(display, "%s:: Fail to assign resource for compositionTarget",
                    __func__);
            return ret;
        }

        if ((ret = assignLayers(display, ePriorityMax)) != NO_ERROR) {
            if (ret == EXYNOS_ERROR_CHANGED) {
                retry_count++;
                continue;
            } else {
                HWC_LOGE(display, "%s:: Fail to assign resource for ePriorityMax layer",
                        __func__);
                goto err;
            }
        }

        if ((ret = assignLayers(display, ePriorityHigh)) != NO_ERROR) {
            if (ret == EXYNOS_ERROR_CHANGED) {
                retry_count++;
                continue;
            } else {
                HWC_LOGE(display, "%s:: Fail to assign resource for ePriorityHigh layer",
                        __func__);
                goto err;
            }
        }

        if ((ret = assignCompositionTarget(display, COMPOSITION_EXYNOS)) != NO_ERROR) {
            if (ret == eInsufficientMPP) {
                /*
                 * Change compositionTypes to HWC2_COMPOSITION_CLIENT
                 */
                uint32_t firstIndex = (uint32_t)display->mExynosCompositionInfo.mFirstIndex;
                uint32_t lastIndex = (uint32_t)display->mExynosCompositionInfo.mLastIndex;
                for (uint32_t i = firstIndex; i <= lastIndex; i++) {
                    ExynosLayer *layer = display->mLayers[i];
                    layer->resetAssignedResource();
                    layer->mOverlayInfo |= eInsufficientMPP;
                    layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                    if (((ret = display->addClientCompositionLayer(i)) != NO_ERROR) &&
                        (ret != EXYNOS_ERROR_CHANGED)) {
                        HWC_LOGE(display, "Change compositionTypes to HWC2_COMPOSITION_CLIENT, but addClientCompositionLayer failed (%d)", ret);
                        goto err;
                    }
                }
                display->mExynosCompositionInfo.initializeInfos(display);
                ret = EXYNOS_ERROR_CHANGED;
            } else {
                goto err;
            }
        }

        if (ret == NO_ERROR) {
            for (int32_t i = ePriorityHigh - 1; i > ePriorityNone; i--) {
                if ((ret = assignLayers(display, i)) == EXYNOS_ERROR_CHANGED)
                    break;
                if (ret != NO_ERROR)
                    goto err;
            }
        }

        /* Assignment is done */
        if (ret == NO_ERROR) {
            ret = setResourcePriority(display);
        }
        retry_count++;
    } while((ret == EXYNOS_ERROR_CHANGED) && (retry_count < ASSIGN_RESOURCE_TRY_COUNT));

    if (retry_count == ASSIGN_RESOURCE_TRY_COUNT) {
        HWC_LOGE(display, "%s:: assign resources fail", __func__);
        ret = eUnknown;
        goto err;
    } else {
        if ((ret = updateExynosComposition(display)) != NO_ERROR)
            return ret;
        if ((ret = updateClientComposition(display)) != NO_ERROR)
            return ret;
    }

    if (hwcCheckDebugMessages(eDebugCapacity)) {
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            if (mM2mMPPs[i]->mPhysicalType == MPP_G2D)
            {
                String8 dumpMPP;
                mM2mMPPs[i]->dump(dumpMPP);
                HDEBUGLOGD(eDebugCapacity, "%s", dumpMPP.string());
            }
        }
    }
    return ret;
err:
    return ret;
}
int32_t ExynosResourceManager::updateExynosComposition(ExynosDisplay *display)
{
    int ret = NO_ERROR;
    /* Use Exynos composition as many as possible */
    if ((display->mExynosCompositionInfo.mHasCompositionLayer == true) &&
        (display->mExynosCompositionInfo.mM2mMPP != NULL)) {
        if (display->mDisplayControl.useMaxG2DSrc == 1) {
            ExynosMPP *m2mMPP = display->mExynosCompositionInfo.mM2mMPP;
            uint32_t lastIndex = display->mExynosCompositionInfo.mLastIndex;
            uint32_t firstIndex = display->mExynosCompositionInfo.mFirstIndex;
            uint32_t remainNum = m2mMPP->mMaxSrcLayerNum - (lastIndex - firstIndex + 1);

            HDEBUGLOGD(eDebugResourceAssigning, "Update ExynosComposition firstIndex: %d, lastIndex: %d, remainNum: %d++++",
                    firstIndex, lastIndex, remainNum);

            ExynosLayer *layer = NULL;
            exynos_image src_img;
            exynos_image dst_img;
            if (remainNum > 0) {
                for (uint32_t i = (lastIndex + 1); i < display->mLayers.size(); i++)
                {
                    layer = display->mLayers[i];
                    layer->setSrcExynosImage(&src_img);
                    layer->setDstExynosImage(&dst_img);
                    layer->setExynosImage(src_img, dst_img);
                    bool isAssignable = false;
                    if ((layer->mSupportedMPPFlag & m2mMPP->mLogicalType) != 0)
                        isAssignable = m2mMPP->isAssignable(display, src_img, dst_img);

                    bool canChange = (layer->mValidateCompositionType != HWC2_COMPOSITION_CLIENT) &&
                                     ((display->mDisplayControl.cursorSupport == false) ||
                                      (layer->mCompositionType != HWC2_COMPOSITION_CURSOR)) &&
                                     (layer->mSupportedMPPFlag & m2mMPP->mLogicalType) && isAssignable;

                    HDEBUGLOGD(eDebugResourceAssigning, "\tlayer[%d] type: %d, 0x%8x, isAssignable: %d, canChange: %d, remainNum(%d)",
                            i, layer->mValidateCompositionType,
                            layer->mSupportedMPPFlag, isAssignable, canChange, remainNum);
                    if (canChange) {
                        layer->resetAssignedResource();
                        layer->mOverlayInfo |= eUpdateExynosComposition;
                        if ((ret = m2mMPP->assignMPP(display, layer)) != NO_ERROR)
                        {
                            ALOGE("%s:: %s MPP assignMPP() error (%d)",
                                    __func__, m2mMPP->mName.string(), ret);
                            return ret;
                        }
                        layer->setExynosMidImage(dst_img);
                        display->addExynosCompositionLayer(i);
                        layer->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
                        remainNum--;
                    }
                    if ((canChange == false) || (remainNum == 0))
                        break;
                }
            }
            if (remainNum > 0) {
                for (int32_t i = (firstIndex - 1); i >= 0; i--)
                {
                    layer = display->mLayers[i];
                    layer->setSrcExynosImage(&src_img);
                    layer->setDstExynosImage(&dst_img);
                    layer->setExynosImage(src_img, dst_img);
                    bool isAssignable = false;
                    if ((layer->mSupportedMPPFlag & m2mMPP->mLogicalType) != 0)
                        isAssignable = m2mMPP->isAssignable(display, src_img, dst_img);

                    bool canChange = (layer->mValidateCompositionType != HWC2_COMPOSITION_CLIENT) &&
                                     ((display->mDisplayControl.cursorSupport == false) ||
                                      (layer->mCompositionType != HWC2_COMPOSITION_CURSOR)) &&
                                     (layer->mSupportedMPPFlag & m2mMPP->mLogicalType) && isAssignable;

                    HDEBUGLOGD(eDebugResourceAssigning, "\tlayer[%d] type: %d, 0x%8x, isAssignable: %d, canChange: %d, remainNum(%d)",
                            i, layer->mValidateCompositionType,
                            layer->mSupportedMPPFlag, isAssignable, canChange, remainNum);
                    if (canChange) {
                        layer->resetAssignedResource();
                        layer->mOverlayInfo |= eUpdateExynosComposition;
                        if ((ret = m2mMPP->assignMPP(display, layer)) != NO_ERROR)
                        {
                            ALOGE("%s:: %s MPP assignMPP() error (%d)",
                                    __func__, m2mMPP->mName.string(), ret);
                            return ret;
                        }
                        layer->setExynosMidImage(dst_img);
                        display->addExynosCompositionLayer(i);
                        layer->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
                        remainNum--;
                    }
                    if ((canChange == false) || (remainNum == 0))
                        break;
                }
            }
            HDEBUGLOGD(eDebugResourceAssigning, "Update ExynosComposition firstIndex: %d, lastIndex: %d, remainNum: %d-----",
                    display->mExynosCompositionInfo.mFirstIndex, display->mExynosCompositionInfo.mLastIndex, remainNum);
        }

        /*
         * Check if there is only one exynos composition layer
         * Then it is not composition and m2mMPP is not required
         * if internalMPP can process the layer alone.
         */
        ExynosMPP *otfMPP = display->mExynosCompositionInfo.mOtfMPP;
        if ((display->mDisplayControl.enableExynosCompositionOptimization == true) &&
            (otfMPP != NULL) &&
            (display->mExynosCompositionInfo.mFirstIndex >= 0) &&
            (display->mExynosCompositionInfo.mFirstIndex == display->mExynosCompositionInfo.mLastIndex))
        {
            ExynosLayer* layer = display->mLayers[display->mExynosCompositionInfo.mFirstIndex];
            if (layer->mSupportedMPPFlag & otfMPP->mLogicalType) {
                exynos_image src_img;
                layer->setSrcExynosImage(&src_img);
                /* mSupportedMPPFlag shows whether MPP HW can support the layer or not.
                   But we should consider additional AFBC condition affected by shared otfMPP. */
                if (otfMPP->isSupportedCompression(src_img)) {
                    layer->resetAssignedResource();
                    layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
                    display->mExynosCompositionInfo.initializeInfos(display);
                    // reset otfMPP
                    if ((ret = otfMPP->resetAssignedState()) != NO_ERROR)
                    {
                        ALOGE("%s:: %s MPP resetAssignedState() error (%d)",
                                __func__, otfMPP->mName.string(), ret);
                    }
                    // assign otfMPP again
                    if ((ret = otfMPP->assignMPP(display, layer)) != NO_ERROR)
                    {
                        ALOGE("%s:: %s MPP assignMPP() error (%d)",
                                __func__, otfMPP->mName.string(), ret);
                    }
                }
            }
        }
    }
    return ret;
}

int32_t ExynosResourceManager::changeLayerFromClientToDevice(ExynosDisplay *display, ExynosLayer *layer,
        uint32_t layer_index, exynos_image m2m_out_img, ExynosMPP *m2mMPP, ExynosMPP *otfMPP)
{
    int ret = NO_ERROR;
    if ((ret = display->removeClientCompositionLayer(layer_index)) != NO_ERROR) {
        ALOGD("removeClientCompositionLayer return error(%d)", ret);
        return ret;
    }
    if (otfMPP != NULL) {
        if ((ret = otfMPP->assignMPP(display, layer)) != NO_ERROR)
        {
            ALOGE("%s:: %s MPP assignMPP() error (%d)",
                    __func__, otfMPP->mName.string(), ret);
            return ret;
        }
        HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: %s MPP is assigned",
                layer_index, otfMPP->mName.string());
    }
    if (m2mMPP != NULL) {
        if ((ret = m2mMPP->assignMPP(display, layer)) != NO_ERROR)
        {
            ALOGE("%s:: %s MPP assignMPP() error (%d)",
                    __func__, m2mMPP->mName.string(), ret);
            return ret;
        }
        layer->setExynosMidImage(m2m_out_img);
        HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: %s MPP is assigned",
                layer_index, m2mMPP->mName.string());
    }
    layer->mValidateCompositionType = HWC2_COMPOSITION_DEVICE;
    display->mWindowNumUsed++;
    HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: mWindowNumUsed(%d)",
            layer_index, display->mWindowNumUsed);

    return ret;
}
int32_t ExynosResourceManager::updateClientComposition(ExynosDisplay *display)
{
    int ret = NO_ERROR;

    if (display->mDisplayControl.enableClientCompositionOptimization == false)
        return ret;

    if ((exynosHWCControl.forceGpu == 1) ||
        (display->mClientCompositionInfo.mHasCompositionLayer == false))
        return ret;

    /* Check if there is layer that can be handled by overlay */
    int32_t firstIndex = display->mClientCompositionInfo.mFirstIndex;
    int32_t lastIndex = display->mClientCompositionInfo.mLastIndex;

    /* Don't optimize if only low fps layers are composited by GLES */
    if ((display->mLowFpsLayerInfo.mHasLowFpsLayer == true) &&
        (display->mLowFpsLayerInfo.mFirstIndex == firstIndex) &&
        (display->mLowFpsLayerInfo.mLastIndex == lastIndex))
        return ret;

#ifdef USE_DEDICATED_TOP_WINDOW
    /* Don't optimize If client composition is assigned with dedicated channel */
    if((display->mClientCompositionInfo.mOtfMPP->mPhysicalType == DEDICATED_CHANNEL_TYPE) &&
       (display->mClientCompositionInfo.mOtfMPP->mPhysicalIndex == DEDICATED_CHANNEL_INDEX) &&
       (firstIndex == lastIndex))
        return ret;
#endif

    for (int32_t i = firstIndex; i <= lastIndex; i++) {
        ExynosMPP *m2mMPP = NULL;
        ExynosMPP *otfMPP = NULL;
        exynos_image m2m_out_img;
        uint32_t overlayInfo = 0;
        int32_t compositionType = 0;
        ExynosLayer *layer = display->mLayers[i];
        if ((layer->mOverlayPriority >= ePriorityHigh) &&
            (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE)) {
            display->mClientCompositionInfo.mFirstIndex++;
            continue;
        }
        compositionType = assignLayer(display, layer, i, m2m_out_img, &m2mMPP, &otfMPP, overlayInfo);
        if (compositionType == HWC2_COMPOSITION_DEVICE) {
            /*
             * Don't allocate G2D
             * Execute can be fail because of other job
             * Prioritizing is required to allocate G2D
             */
            if ((m2mMPP != NULL) && (m2mMPP->mPhysicalType == MPP_G2D))
                break;

            if ((ret = changeLayerFromClientToDevice(display, layer, i, m2m_out_img, m2mMPP, otfMPP)) != NO_ERROR)
                return ret;
        } else {
            break;
        }
    }

#ifdef USE_DEDICATED_TOP_WINDOW
    /* Don't optimize If client composition is assigned with dedicated channel */
    if((display->mClientCompositionInfo.mOtfMPP->mPhysicalType == DEDICATED_CHANNEL_TYPE) &&
       (display->mClientCompositionInfo.mOtfMPP->mPhysicalIndex == DEDICATED_CHANNEL_INDEX))
        return ret;
#endif

    firstIndex = display->mClientCompositionInfo.mFirstIndex;
    lastIndex = display->mClientCompositionInfo.mLastIndex;
    for (int32_t i = lastIndex; i >= 0; i--) {
        ExynosMPP *m2mMPP = NULL;
        ExynosMPP *otfMPP = NULL;
        exynos_image m2m_out_img;
        uint32_t overlayInfo = 0;
        int32_t compositionType = 0;
        ExynosLayer *layer = display->mLayers[i];
        if ((layer->mOverlayPriority >= ePriorityHigh) &&
            (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE)) {
            display->mClientCompositionInfo.mLastIndex--;
            continue;
        }
        compositionType = assignLayer(display, layer, i, m2m_out_img, &m2mMPP, &otfMPP, overlayInfo);
        if (compositionType == HWC2_COMPOSITION_DEVICE) {
            /*
             * Don't allocate G2D
             * Execute can be fail because of other job
             * Prioritizing is required to allocate G2D
             */
            if ((m2mMPP != NULL) && (m2mMPP->mPhysicalType == MPP_G2D))
                break;
            if ((ret = changeLayerFromClientToDevice(display, layer, i, m2m_out_img, m2mMPP, otfMPP)) != NO_ERROR)
                return ret;
        } else {
            break;
        }
    }

    return ret;
}

int32_t ExynosResourceManager::resetAssignedResources(ExynosDisplay * display, bool forceReset)
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i]->mAssignedDisplay != display)
            continue;

        mOtfMPPs[i]->resetAssignedState();
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mAssignedDisplay != display)
            continue;
        if ((forceReset == false) &&
            ((mM2mMPPs[i]->mMaxSrcLayerNum > 1)))
        {
            /*
             * Don't reset assigned state
             */
            continue;
        }
        mM2mMPPs[i]->resetAssignedState();
    }
    display->mWindowNumUsed = 0;

    return NO_ERROR;
}

int32_t ExynosResourceManager::assignCompositionTarget(ExynosDisplay * display, uint32_t targetType)
{
    int32_t ret = NO_ERROR;
    ExynosCompositionInfo *compositionInfo;

    HDEBUGLOGD(eDebugResourceManager, "%s:: display(%d), targetType(%d) +++++",
            __func__, display->mType, targetType);

    if (targetType == COMPOSITION_CLIENT)
        compositionInfo = &(display->mClientCompositionInfo);
    else if (targetType == COMPOSITION_EXYNOS)
        compositionInfo = &(display->mExynosCompositionInfo);
    else
        return -EINVAL;

    if (compositionInfo->mHasCompositionLayer == false)
    {
        HDEBUGLOGD(eDebugResourceManager, "\tthere is no composition layers");
        return NO_ERROR;
    }

    exynos_image src_img;
    exynos_image dst_img;
    display->setCompositionTargetExynosImage(targetType, &src_img, &dst_img);

    if (targetType == COMPOSITION_EXYNOS) {
        if (compositionInfo->mM2mMPP == NULL) {
            HWC_LOGE(display, "%s:: fail to assign M2mMPP (%d)",__func__, ret);
            return eInsufficientMPP;
        }
    }

    if ((compositionInfo->mFirstIndex < 0) ||
        (compositionInfo->mLastIndex < 0)) {
        HWC_LOGE(display, "%s:: layer index is not valid mFirstIndex(%d), mLastIndex(%d)",
                __func__, compositionInfo->mFirstIndex, compositionInfo->mLastIndex);
        return -EINVAL;
    }

    if (display->mUseDpu == false) {
        return NO_ERROR;
    }

    int64_t isSupported = 0;
    bool isAssignable = false;
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
#ifdef USE_DEDICATED_TOP_WINDOW
        if((mOtfMPPs[i]->mPhysicalType == DEDICATED_CHANNEL_TYPE) &&
           (mOtfMPPs[i]->mPhysicalIndex == DEDICATED_CHANNEL_INDEX) &&
           (uint32_t)compositionInfo->mLastIndex != (display->mLayers.size()-1))
            continue;
#endif
        isSupported = mOtfMPPs[i]->isSupported(*display, src_img, dst_img);
        if (isSupported == NO_ERROR)
            isAssignable = mOtfMPPs[i]->isAssignable(display, src_img, dst_img);

        HDEBUGLOGD(eDebugResourceAssigning, "\t\t check %s: supportedBit(0x%" PRIx64 "), isAssignable(%d)",
                mOtfMPPs[i]->mName.string(), -isSupported, isAssignable);
        if ((isSupported == NO_ERROR) && (isAssignable)) {
            if ((ret = mOtfMPPs[i]->assignMPP(display, compositionInfo)) != NO_ERROR)
            {
                HWC_LOGE(display, "%s:: %s MPP assignMPP() error (%d)",
                        __func__, mOtfMPPs[i]->mName.string(), ret);
                return ret;
            }
            compositionInfo->setExynosImage(src_img, dst_img);
            compositionInfo->setExynosMidImage(dst_img);
            compositionInfo->mOtfMPP = mOtfMPPs[i];
            display->mWindowNumUsed++;

            HDEBUGLOGD(eDebugResourceManager, "%s:: %s is assigned", __func__, mOtfMPPs[i]->mName.string());
            return NO_ERROR;
        }
    }

    HDEBUGLOGD(eDebugResourceManager, "%s:: insufficient MPP", __func__);
    return eInsufficientMPP;
}

int32_t ExynosResourceManager::validateLayer(uint32_t index, ExynosDisplay *display, ExynosLayer *layer)
{
    if ((layer == NULL) || (display == NULL))
        return eUnknown;

    if (exynosHWCControl.forceGpu == 1) {
        if ((layer->mLayerBuffer == NULL) ||
            (getDrmMode(layer->mLayerBuffer) == NO_DRM))
            return eForceFbEnabled;
    }

    if (layer->mLayerFlag & EXYNOS_HWC_FORCE_CLIENT)
        return eFroceClientLayer;

    if ((display->mLayers.size() >= MAX_OVERLAY_LAYER_NUM) &&
        (layer->mOverlayPriority < ePriorityHigh))
        return eExceedMaxLayerNum;

    if ((layer->mLayerBuffer != NULL) &&
        (getDrmMode(layer->mLayerBuffer->flags) == NO_DRM) &&
        (display->mDREnable == true) &&
        (display->mDynamicReCompMode == DEVICE_2_CLIENT))
        return eDynamicRecomposition;

    if ((layer->mLayerBuffer != NULL) &&
            (display->mDisplayId == getDisplayId(HWC_DISPLAY_PRIMARY, 0)) &&
            (mForceReallocState != DST_REALLOC_DONE)) {
        ALOGI("Device type assign skipping by dst reallocation...... ");
        return eReallocOnGoingForDDI;
    }

    if (layer->mCompositionType == HWC2_COMPOSITION_CLIENT)
        return eSkipLayer;

#ifndef HWC_SUPPORT_COLOR_TRANSFORM
    if (display->mColorTransformHint != HAL_COLOR_TRANSFORM_IDENTITY)
        return eUnSupportedColorTransform;
#else
    if ((display->mColorTransformHint == HAL_COLOR_TRANSFORM_ERROR) &&
        (layer->mOverlayPriority < ePriorityHigh))
        return eUnSupportedColorTransform;
#endif

    if ((display->mLowFpsLayerInfo.mHasLowFpsLayer == true) &&
        (display->mLowFpsLayerInfo.mFirstIndex <= (int32_t)index) &&
        ((int32_t)index <= display->mLowFpsLayerInfo.mLastIndex))
        return eLowFpsLayer;

    if(layer->mIsDimLayer && layer->mLayerBuffer == NULL) {
        return eDimLayer;
    }

    /* Process to Source copy layer blending exception */
    if ((display->mUseDpu) &&
        (display->mBlendingNoneIndex != -1) && (display->mLayers.size() > 0)) {
        if ((layer->mOverlayPriority < ePriorityHigh) &&
            ((int)index <= display->mBlendingNoneIndex))
            return eSourceOverBelow;
    }

    if (layer->mLayerBuffer == NULL)
        return eInvalidHandle;
    if (isSrcCropFloat(layer->mPreprocessedInfo.sourceCrop))
        return eHasFloatSrcCrop;

    if ((layer->mPreprocessedInfo.displayFrame.left < 0) ||
        (layer->mPreprocessedInfo.displayFrame.top < 0) ||
        (layer->mPreprocessedInfo.displayFrame.right > (int32_t)display->mXres) ||
        (layer->mPreprocessedInfo.displayFrame.bottom > (int32_t)display->mYres))
        return eInvalidDispFrame;

    return NO_ERROR;
}

int32_t ExynosResourceManager::getCandidateM2mMPPOutImages(ExynosDisplay *display,
        ExynosLayer *layer, uint32_t *imageNum, exynos_image *image_lists)
{
    uint32_t listSize = *imageNum;
    if (listSize != M2M_MPP_OUT_IMAGS_COUNT)
        return -EINVAL;

    uint32_t index = 0;
    exynos_image src_img;
    exynos_image dst_img;
    layer->setSrcExynosImage(&src_img);
    layer->setDstExynosImage(&dst_img);
    dst_img.transform = 0;
    /* Position is (0, 0) */
    dst_img.x = 0;
    dst_img.y = 0;

    /* Check original source format first */
    dst_img.format = src_img.format;
    dst_img.dataSpace = src_img.dataSpace;

    /* Copy origin source HDR metadata */
    dst_img.metaParcel = src_img.metaParcel;

    uint32_t dstW = dst_img.w;
    uint32_t dstH = dst_img.h;
    bool isPerpendicular = !!(src_img.transform & HAL_TRANSFORM_ROT_90);
    if (isPerpendicular) {
        dstW = dst_img.h;
        dstH = dst_img.w;
    }

    /* Scale up case */
    if ((dstW > src_img.w) && (dstH > src_img.h))
    {
        /* VGFS doesn't rotate image, m2mMPP rotates image */
        src_img.transform = 0;
        ExynosMPP *mppVGFS = getExynosMPP(MPP_LOGICAL_DPP_VGFS);
        exynos_image dst_scale_img = dst_img;

        /* Some chipset have VGS instead of VGFS */
        if (mppVGFS == NULL) {
            mppVGFS = getExynosMPP(MPP_LOGICAL_DPP_VGS);
            if (mppVGFS == NULL)
                mppVGFS = getExynosMPP(MPP_LOGICAL_DPP_VG);
        }

        if (hasHdrInfo(src_img)) {
            if (isFormatYUV(src_img.format))
                dst_scale_img.format = HAL_PIXEL_FORMAT_YCBCR_P010;
            else
                dst_scale_img.format = HAL_PIXEL_FORMAT_RGBA_1010102;
        } else {
            if (isFormatYUV(src_img.format)) {
                dst_scale_img.format = DEFAULT_MPP_DST_YUV_FORMAT;
            }
        }

        uint32_t upScaleRatio = mppVGFS->getMaxUpscale(src_img, dst_scale_img);
        uint32_t downScaleRatio = mppVGFS->getMaxDownscale(*display, src_img, dst_scale_img);
        uint32_t srcCropWidthAlign = mppVGFS->getSrcCropWidthAlign(src_img);
        uint32_t srcCropHeightAlign = mppVGFS->getSrcCropHeightAlign(src_img);

        dst_scale_img.x = 0;
        dst_scale_img.y = 0;
        if (isPerpendicular) {
            dst_scale_img.w = pixel_align(src_img.h, srcCropWidthAlign);
            dst_scale_img.h = pixel_align(src_img.w, srcCropHeightAlign);
        } else {
            dst_scale_img.w = pixel_align(src_img.w, srcCropWidthAlign);
            dst_scale_img.h = pixel_align(src_img.h, srcCropHeightAlign);
        }

        HDEBUGLOGD(eDebugResourceAssigning, "index[%d], w: %d, h: %d, ratio(type: %d, %d, %d)", index, dst_scale_img.w, dst_scale_img.h,
                mppVGFS->mLogicalType, upScaleRatio, downScaleRatio);
        if (dst_scale_img.w * upScaleRatio < dst_img.w) {
            dst_scale_img.w = pixel_align((uint32_t)ceilf((float)dst_img.w/(float)upScaleRatio), srcCropWidthAlign);
        }
        if (dst_scale_img.h * upScaleRatio < dst_img.h) {
            dst_scale_img.h = pixel_align((uint32_t)ceilf((float)dst_img.h/(float)upScaleRatio), srcCropHeightAlign);
        }
        HDEBUGLOGD(eDebugResourceAssigning, "\tsrc[%d, %d, %d,%d], dst[%d, %d, %d,%d], mid[%d, %d, %d, %d]",
                src_img.x, src_img.y, src_img.w, src_img.h,
                dst_img.x, dst_img.y, dst_img.w, dst_img.h,
                dst_scale_img.x, dst_scale_img.y, dst_scale_img.w, dst_scale_img.h);
        image_lists[index++] = dst_scale_img;
    }

    if (isFormatYUV(src_img.format) && !hasHdrInfo(src_img)) {
        dst_img.format = DEFAULT_MPP_DST_YUV_FORMAT;
    }

    ExynosExternalDisplay *external_display =
        (ExynosExternalDisplay*)mDevice->getDisplay(getDisplayId(HWC_DISPLAY_EXTERNAL, 0));
    ExynosExternalDisplay *external_display2 =
        (ExynosExternalDisplay*)mDevice->getDisplay(getDisplayId(HWC_DISPLAY_EXTERNAL, 1));

    /* For HDR through MSC or G2D case but dataspace is not changed */
    if (hasHdrInfo(src_img)) {
        if (isFormatYUV(src_img.format))
            dst_img.format = HAL_PIXEL_FORMAT_YCBCR_P010;
        else
            dst_img.format = HAL_PIXEL_FORMAT_RGBA_1010102;
        dst_img.dataSpace = src_img.dataSpace;

        /*
         * Align dst size
         * HDR10Plus should able to be processed by VGRFS
         * HDR on primary display should be processed by VGRFS
         * when external display is connected
         * because G2D is used by external display
         */
        if (hasHdr10Plus(dst_img) ||
            ((external_display != NULL) && (external_display->mPlugState) &&
             (display->mType == HWC_DISPLAY_PRIMARY)) ||
            ((external_display != NULL) && (external_display->mPlugState) &&
             (external_display2 != NULL) && (external_display2->mPlugState) &&
             (display->getId() == external_display2->getId()))) {
            ExynosMPP *mppVGRFS = getExynosMPP(MPP_LOGICAL_DPP_VGRFS);
            uint32_t srcCropWidthAlign = 1;
            uint32_t srcCropHeightAlign = 1;
            if (mppVGRFS != NULL) {
                srcCropWidthAlign = mppVGRFS->getSrcCropWidthAlign(dst_img);
                srcCropHeightAlign = mppVGRFS->getSrcCropHeightAlign(dst_img);
            }
            dst_img.w = pixel_align(dst_img.w, srcCropWidthAlign);
            dst_img.h = pixel_align(dst_img.h, srcCropHeightAlign);
        }
    }

    image_lists[index++] = dst_img;

    /* For G2D HDR case */
    if (hasHdrInfo(src_img)) {
        if ((display->mType == HWC_DISPLAY_EXTERNAL) &&
                (display->mPlugState) && (((ExynosExternalDisplay*)display)->mExternalHdrSupported)) {
            dst_img.format = HAL_PIXEL_FORMAT_RGBA_1010102;
            dst_img.dataSpace = src_img.dataSpace;
        } else {
            uint32_t dataspace = HAL_DATASPACE_UNKNOWN;
            if (display->mColorMode == HAL_COLOR_MODE_NATIVE) {
                dataspace = HAL_DATASPACE_DCI_P3;
                dataspace &= ~HAL_DATASPACE_TRANSFER_MASK;
                dataspace |= HAL_DATASPACE_TRANSFER_GAMMA2_2;
                dataspace &= ~HAL_DATASPACE_RANGE_MASK;
                dataspace |= HAL_DATASPACE_RANGE_LIMITED;
            } else {
                dataspace = colorModeToDataspace(display->mColorMode);
            }
            dst_img.format = HAL_PIXEL_FORMAT_RGBX_8888;
            dst_img.dataSpace = (android_dataspace)dataspace;
        }

        /*
         * This image is not pushed for primary display
         * if external display is connected
         * because G2D is used only for HDR on exernal display
         */
        if (!(((external_display != NULL && external_display->mPlugState) ||
               (external_display2 != NULL && external_display2->mPlugState)) && (display->mType == HWC_DISPLAY_PRIMARY))) {
            image_lists[index++] = dst_img;
        }
    }

    if (isFormatYUV(src_img.format) && !hasHdrInfo(src_img)) {
        /* Check RGB format */
        dst_img.format = DEFAULT_MPP_DST_FORMAT;
        if (display->mColorMode == HAL_COLOR_MODE_NATIVE) {
            /* Bypass dataSpace */
            dst_img.dataSpace = src_img.dataSpace;
        } else {
            /* Covert data space */
            dst_img.dataSpace = colorModeToDataspace(display->mColorMode);
        }
        image_lists[index++] = dst_img;
    }

    if (*imageNum < index)
        return -EINVAL;
    else {
        *imageNum = index;
        return (uint32_t)listSize;
    }
}

int32_t ExynosResourceManager::assignLayer(ExynosDisplay *display, ExynosLayer *layer, uint32_t layer_index,
        exynos_image &m2m_out_img, ExynosMPP **m2mMPP, ExynosMPP **otfMPP, uint32_t &overlayInfo)
{
    int32_t ret = NO_ERROR;
    uint32_t validateFlag = 0;

    exynos_image src_img;
    exynos_image dst_img;
    layer->setSrcExynosImage(&src_img);
    layer->setDstExynosImage(&dst_img);
    layer->setExynosImage(src_img, dst_img);
    layer->setExynosMidImage(dst_img);

    validateFlag = validateLayer(layer_index, display, layer);
    if ((display->mUseDpu) &&
        (display->mWindowNumUsed >= display->mMaxWindowNum))
        validateFlag |= eInsufficientWindow;

    HDEBUGLOGD(eDebugResourceManager, "\t[%d] layer: validateFlag(0x%8x), supportedMPPFlag(0x%8x), mLayerFlag(0x%8x)",
            layer_index, validateFlag, layer->mSupportedMPPFlag, layer->mLayerFlag);

    if (hwcCheckDebugMessages(eDebugResourceAssigning)) {
        layer->printLayer();
    }

    if ((validateFlag == NO_ERROR) || (validateFlag == eInsufficientWindow) ||
        (validateFlag == eDimLayer) || (validateFlag == eSourceOverBelow)) {
        bool isAssignable = false;
        uint64_t isSupported = 0;
        /* 1. Find available otfMPP */
        if ((display->mUseDpu) &&
            (validateFlag != eInsufficientWindow) &&
            (validateFlag != eSourceOverBelow)) {
            for (uint32_t j = 0; j < mOtfMPPs.size(); j++) {
#ifdef USE_DEDICATED_TOP_WINDOW
                if((mOtfMPPs[j]->mPhysicalType == DEDICATED_CHANNEL_TYPE) &&
                   (mOtfMPPs[j]->mPhysicalIndex == DEDICATED_CHANNEL_INDEX) &&
                   (uint32_t)layer_index != (display->mLayers.size()-1))
                    continue;
#endif
                if ((layer->mSupportedMPPFlag & mOtfMPPs[j]->mLogicalType) != 0)
                    isAssignable = mOtfMPPs[j]->isAssignable(display, src_img, dst_img);

                HDEBUGLOGD(eDebugResourceAssigning, "\t\t check %s: flag (%d) supportedBit(%d), isAssignable(%d)",
                        mOtfMPPs[j]->mName.string(),layer->mSupportedMPPFlag,
                        (layer->mSupportedMPPFlag & mOtfMPPs[j]->mLogicalType), isAssignable);
                if ((layer->mSupportedMPPFlag & mOtfMPPs[j]->mLogicalType) && (isAssignable)) {
                    isSupported = mOtfMPPs[j]->isSupported(*display, src_img, dst_img);
                    HDEBUGLOGD(eDebugResourceAssigning, "\t\t\t isSuported(%" PRIx64 ")", -isSupported);
                    if (isSupported == NO_ERROR) {
                        *otfMPP = mOtfMPPs[j];
                        return HWC2_COMPOSITION_DEVICE;
                    }
                }
            }
        }

        /* 2. Find available m2mMPP */
        for (uint32_t j = 0; j < mM2mMPPs.size(); j++) {

            if ((display->mUseDpu == true) &&
                ((mM2mMPPs[j]->mLogicalType == MPP_LOGICAL_G2D_COMBO) ||
                 (mM2mMPPs[j]->mLogicalType == MPP_LOGICAL_MSC_COMBO)))
                continue;
            if ((display->mUseDpu == false) &&
                (mM2mMPPs[j]->mLogicalType == MPP_LOGICAL_G2D_RGB))
                continue;

            /* Only G2D can be assigned if layer is supported by G2D
             * when window is not sufficient
             */
            if (((validateFlag == eInsufficientWindow) ||
                 (validateFlag == eSourceOverBelow)) &&
                    (!(mM2mMPPs[j]->mMaxSrcLayerNum > 1))) {
                HDEBUGLOGD(eDebugResourceAssigning, "\t\tInsufficient window but exynosComposition is not assigned");
                continue;
            }

            bool isAssignableState = mM2mMPPs[j]->isAssignableState(display, src_img, dst_img);

            HDEBUGLOGD(eDebugResourceAssigning, "\t\t check %s: supportedBit(%d), isAssignableState(%d)",
                    mM2mMPPs[j]->mName.string(),
                    (layer->mSupportedMPPFlag & mM2mMPPs[j]->mLogicalType), isAssignableState);

            if (isAssignableState) {
                if (!(mM2mMPPs[j]->mMaxSrcLayerNum > 1)) {
                    exynos_image otf_src_img = dst_img;
                    exynos_image otf_dst_img = dst_img;

                    otf_dst_img.format = DEFAULT_MPP_DST_FORMAT;

                    exynos_image image_lists[M2M_MPP_OUT_IMAGS_COUNT];
                    uint32_t imageNum = M2M_MPP_OUT_IMAGS_COUNT;
                    if ((ret = getCandidateM2mMPPOutImages(display, layer, &imageNum, image_lists)) < 0)
                    {
                        HWC_LOGE(display, "Fail getCandidateM2mMPPOutImages (%d)", ret);
                        return ret;
                    }
                    HDEBUGLOGD(eDebugResourceAssigning, "candidate M2mMPPOutImage num: %d", imageNum);
                    for (uint32_t outImg = 0; outImg < imageNum; outImg++)
                    {
                        dumpExynosImage(eDebugResourceAssigning, image_lists[outImg]);
                        otf_src_img = image_lists[outImg];
                        /* transform is already handled by m2mMPP */
                        otf_src_img.transform = 0;
                        otf_dst_img.transform = 0;

                        if (((isSupported = mM2mMPPs[j]->isSupported(*display, src_img, otf_src_img)) != NO_ERROR) ||
                            ((isAssignable = mM2mMPPs[j]->hasEnoughCapa(display, src_img, otf_src_img)) == false))
                        {
                            HDEBUGLOGD(eDebugResourceAssigning, "\t\t\t check %s: supportedBit(0x%" PRIx64 "), hasEnoughCapa(%d)",
                                    mM2mMPPs[j]->mName.string(), -isSupported, isAssignable);
                            continue;
                        }

                        /* 3. Find available OtfMPP for output of m2mMPP */
                        for (uint32_t k = 0; k < mOtfMPPs.size(); k++) {
#ifdef USE_DEDICATED_TOP_WINDOW
                            if((mOtfMPPs[k]->mPhysicalType == DEDICATED_CHANNEL_TYPE) &&
                               (mOtfMPPs[k]->mPhysicalIndex == DEDICATED_CHANNEL_INDEX) &&
                               (uint32_t)layer_index != (display->mLayers.size()-1))
                                continue;
#endif
                            isSupported = mOtfMPPs[k]->isSupported(*display, otf_src_img, otf_dst_img);
                            isAssignable = false;
                            if (isSupported == NO_ERROR)
                                isAssignable = mOtfMPPs[k]->isAssignable(display, otf_src_img, otf_dst_img);

                            HDEBUGLOGD(eDebugResourceAssigning, "\t\t\t check %s: supportedBit(0x%" PRIx64 "), isAssignable(%d)",
                                    mOtfMPPs[k]->mName.string(), -isSupported, isAssignable);
                            if ((isSupported == NO_ERROR) && isAssignable) {
                                *m2mMPP = mM2mMPPs[j];
                                *otfMPP = mOtfMPPs[k];
                                m2m_out_img = otf_src_img;
                                return HWC2_COMPOSITION_DEVICE;
                            }
                        }
                    }
                } else {
                    if ((layer->mSupportedMPPFlag & mM2mMPPs[j]->mLogicalType) &&
                        ((isAssignable = mM2mMPPs[j]->hasEnoughCapa(display, src_img, dst_img) == true))) {
                        *m2mMPP = mM2mMPPs[j];
                        return HWC2_COMPOSITION_EXYNOS;
                    } else {
                        HDEBUGLOGD(eDebugResourceManager, "\t\t\t check %s: layer's mSupportedMPPFlag(0x%8x), hasEnoughCapa(%d)",
                                mM2mMPPs[j]->mName.string(), layer->mSupportedMPPFlag, isAssignable);
                    }
                }
            }
        }
    }
    /* Fail to assign resource */
    if (validateFlag != NO_ERROR)
        overlayInfo = validateFlag;
    else
        overlayInfo = eMPPUnsupported;
    return HWC2_COMPOSITION_CLIENT;
}

int32_t ExynosResourceManager::assignLayers(ExynosDisplay * display, uint32_t priority)
{
    HDEBUGLOGD(eDebugResourceAssigning, "%s:: display(%d), priority(%d) +++++",
            __func__, display->mType, priority);

    int32_t ret = NO_ERROR;
    bool needReAssign = false;
    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        ExynosMPP *m2mMPP = NULL;
        ExynosMPP *otfMPP = NULL;
        exynos_image m2m_out_img;
        uint32_t validateFlag = 0;
        int32_t compositionType = 0;

        if ((layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT) ||
            (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS))
            continue;
        if (layer->mOverlayPriority != priority)
            continue;

        exynos_image src_img;
        exynos_image dst_img;
        layer->setSrcExynosImage(&src_img);
        layer->setDstExynosImage(&dst_img);
        layer->setExynosImage(src_img, dst_img);
        layer->setExynosMidImage(dst_img);

        compositionType = assignLayer(display, layer, i, m2m_out_img, &m2mMPP, &otfMPP, validateFlag);
        if (compositionType == HWC2_COMPOSITION_DEVICE) {
            if (otfMPP != NULL) {
                if ((ret = otfMPP->assignMPP(display, layer)) != NO_ERROR)
                {
                    ALOGE("%s:: %s MPP assignMPP() error (%d)",
                            __func__, otfMPP->mName.string(), ret);
                    return ret;
                }
                HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: %s MPP is assigned",
                        i, otfMPP->mName.string());
            }
            if (m2mMPP != NULL) {
                if ((ret = m2mMPP->assignMPP(display, layer)) != NO_ERROR)
                {
                    ALOGE("%s:: %s MPP assignMPP() error (%d)",
                            __func__, m2mMPP->mName.string(), ret);
                    return ret;
                }
                layer->setExynosMidImage(m2m_out_img);
                HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: %s MPP is assigned",
                        i, m2mMPP->mName.string());
            }
            layer->mValidateCompositionType = compositionType;
            display->mWindowNumUsed++;
            HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: mWindowNumUsed(%d)",
                    i, display->mWindowNumUsed);
        } else if (compositionType == HWC2_COMPOSITION_EXYNOS) {
            if (m2mMPP != NULL) {
                if ((ret = m2mMPP->assignMPP(display, layer)) != NO_ERROR)
                {
                    ALOGE("%s:: %s MPP assignMPP() error (%d)",
                            __func__, m2mMPP->mName.string(), ret);
                    return ret;
                }
                HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: %s MPP is assigned",
                        i, m2mMPP->mName.string());
            }
            layer->mValidateCompositionType = compositionType;

            HDEBUGLOGD(eDebugResourceAssigning, "\t\t[%d] layer: exynosComposition", i);
            /* G2D composition */
            if (((ret = display->addExynosCompositionLayer(i)) == EXYNOS_ERROR_CHANGED) ||
                 (ret < 0))
                return ret;
            else {
                /*
                 * If high fps layer should be composited by GLES then
                 * disable handling low fps feature and reassign resources
                 */
                if ((display->mLowFpsLayerInfo.mHasLowFpsLayer == true) &&
                    (display->mClientCompositionInfo.mHasCompositionLayer == true) &&
                    ((display->mClientCompositionInfo.mFirstIndex < display->mLowFpsLayerInfo.mFirstIndex) ||
                     (display->mClientCompositionInfo.mLastIndex > display->mLowFpsLayerInfo.mLastIndex))) {
                    needReAssign = true;
                    break;
                }
            }
        } else {
            /*
             * If high fps layer should be composited by GLES then
             * disable handling low fps feature and reassign resources
            */
            if ((display->mLowFpsLayerInfo.mHasLowFpsLayer == true) &&
                (((int32_t)i < display->mLowFpsLayerInfo.mFirstIndex) ||
                 (display->mLowFpsLayerInfo.mLastIndex < (int32_t)i))) {
                needReAssign = true;
                break;
            }

            /* Fail to assign resource, set HWC2_COMPOSITION_CLIENT */
            if (validateFlag != NO_ERROR)
                layer->mOverlayInfo |= validateFlag;
            else
                layer->mOverlayInfo |= eMPPUnsupported;

            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            if (((ret = display->addClientCompositionLayer(i)) == EXYNOS_ERROR_CHANGED) ||
                (ret < 0))
                return ret;
        }
    }
    if (needReAssign) {
        if ((display->mClientCompositionInfo.mHasCompositionLayer) &&
                (display->mClientCompositionInfo.mOtfMPP != NULL))
            display->mClientCompositionInfo.mOtfMPP->resetAssignedState();

        if (display->mExynosCompositionInfo.mHasCompositionLayer) {
            if (display->mExynosCompositionInfo.mOtfMPP != NULL)
                display->mExynosCompositionInfo.mOtfMPP->resetAssignedState();
            if (display->mExynosCompositionInfo.mM2mMPP != NULL)
                display->mExynosCompositionInfo.mM2mMPP->resetAssignedState();
        }

        display->initializeValidateInfos();
        display->mLowFpsLayerInfo.initializeInfos();
        return EXYNOS_ERROR_CHANGED;
    }
    return ret;
}

int32_t ExynosResourceManager::assignWindow(ExynosDisplay *display)
{
    HDEBUGLOGD(eDebugResourceManager, "%s +++++", __func__);
    int ret = NO_ERROR;
    uint32_t index = 0;
    uint32_t windowIndex = 0;

    if (!display->mUseDpu)
        return ret;

    if (SELECTED_LOGIC == UNDEFINED) {
        windowIndex = display->mBaseWindowIndex;
        for (uint32_t i = 0; i < display->mLayers.size(); i++) {
            ExynosLayer *layer = display->mLayers[i];
            HDEBUGLOGD(eDebugResourceAssigning, "\t[%d] layer type: %d", i, layer->mValidateCompositionType);

            if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) {
#ifdef USE_DEDICATED_TOP_WINDOW
                if ((layer->mOtfMPP) &&
                    (layer->mOtfMPP->mPhysicalType == DEDICATED_CHANNEL_TYPE) &&
                    (layer->mOtfMPP->mPhysicalIndex == DEDICATED_CHANNEL_INDEX)){
                    layer->mWindowIndex = MAX_DECON_WIN - 1;
                    HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer windowIndex: %d", i, MAX_DECON_WIN-1);
                    continue;
                }
#endif
                layer->mWindowIndex = windowIndex;
                HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer windowIndex: %d", i, windowIndex);
            } else if ((layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT) ||
                    (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)) {
                ExynosCompositionInfo *compositionInfo;
                if (layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT)
                    compositionInfo = &display->mClientCompositionInfo;
                else
                    compositionInfo = &display->mExynosCompositionInfo;

                if ((compositionInfo->mHasCompositionLayer == false) ||
                    (compositionInfo->mFirstIndex < 0) ||
                    (compositionInfo->mLastIndex < 0)) {
                    HWC_LOGE(display, "%s:: Invalid %s CompositionInfo mHasCompositionLayer(%d), "
                            "mFirstIndex(%d), mLastIndex(%d) ",
                            __func__, compositionInfo->getTypeStr().string(),
                            compositionInfo->mHasCompositionLayer,
                            compositionInfo->mFirstIndex,
                            compositionInfo->mLastIndex);
                    continue;
                }
                if (i != (uint32_t)compositionInfo->mLastIndex)
                    continue;
#ifdef USE_DEDICATED_TOP_WINDOW
                if ((compositionInfo->mOtfMPP) &&
                    (compositionInfo->mOtfMPP->mPhysicalType == DEDICATED_CHANNEL_TYPE) &&
                    (compositionInfo->mOtfMPP->mPhysicalIndex == DEDICATED_CHANNEL_INDEX)){
                    compositionInfo->mWindowIndex = MAX_DECON_WIN - 1;
                    HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] %s Composition windowIndex: %d",
                            i, compositionInfo->getTypeStr().string(), MAX_DECON_WIN-1);
                    continue;
                }
#endif
                compositionInfo->mWindowIndex = windowIndex;
                HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] %s Composition windowIndex: %d",
                        i, compositionInfo->getTypeStr().string(), windowIndex);
            } else {
                HWC_LOGE(display, "%s:: Invalid layer compositionType layer(%d), compositionType(%d)",
                        __func__, i, layer->mValidateCompositionType);
                continue;
            }
            windowIndex++;
        }
        HDEBUGLOGD(eDebugResourceManager, "%s ------", __func__);
        return ret;
    }

    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        HDEBUGLOGD(eDebugResourceAssigning, "\t[%d] layer type: %d", i, layer->mValidateCompositionType);

        if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) {
            layer->mWindowIndex = display->mAssignedWindows[index];
            HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] layer windowIndex: %d", i, layer->mWindowIndex);
        } else if ((layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT) ||
                   (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)) {
            ExynosCompositionInfo *compositionInfo;
            if (layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT)
                compositionInfo = &display->mClientCompositionInfo;
            else
                compositionInfo = &display->mExynosCompositionInfo;

            if ((compositionInfo->mHasCompositionLayer == false) ||
                (compositionInfo->mFirstIndex < 0) ||
                (compositionInfo->mLastIndex < 0)) {
                HWC_LOGE(display, "%s:: Invalid %s CompositionInfo mHasCompositionLayer(%d), "
                        "mFirstIndex(%d), mLastIndex(%d) ",
                        __func__, compositionInfo->getTypeStr().string(),
                        compositionInfo->mHasCompositionLayer,
                        compositionInfo->mFirstIndex,
                        compositionInfo->mLastIndex);
                continue;
            }
            if (i != (uint32_t)compositionInfo->mLastIndex)
                continue;
            compositionInfo->mWindowIndex = display->mAssignedWindows[index];
            HDEBUGLOGD(eDebugResourceManager, "\t\t[%d] %s Composition windowIndex: %d",
                    i, compositionInfo->getTypeStr().string(), compositionInfo->mWindowIndex);
        } else {
            HWC_LOGE(display, "%s:: Invalid layer compositionType layer(%d), compositionType(%d)",
                    __func__, i, layer->mValidateCompositionType);
            continue;
        }
        index++;
    }
    HDEBUGLOGD(eDebugResourceManager, "%s ------", __func__);
    return ret;
}

/**
 * @param * display
 * @return int
 */
int32_t ExynosResourceManager::updateSupportedMPPFlag(ExynosDisplay * display)
{
    int64_t ret = 0;
    HDEBUGLOGD(eDebugResourceAssigning, "%s++++++++++", __func__);
    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        HDEBUGLOGD(eDebugResourceAssigning, "[%d] layer ", i);

        if (layer->mGeometryChanged == 0)
            continue;

        exynos_image src_img;
        exynos_image dst_img;
        exynos_image dst_img_yuv;
        layer->setSrcExynosImage(&src_img);
        layer->setDstExynosImage(&dst_img);
        layer->setDstExynosImage(&dst_img_yuv);
        dst_img.format = DEFAULT_MPP_DST_FORMAT;
        dst_img_yuv.format = DEFAULT_MPP_DST_YUV_FORMAT;
        HDEBUGLOGD(eDebugResourceAssigning, "\tsrc_img");
        dumpExynosImage(eDebugResourceAssigning, src_img);
        HDEBUGLOGD(eDebugResourceAssigning, "\tdst_img");
        dumpExynosImage(eDebugResourceAssigning, dst_img);

        /* Initialize flags */
        layer->mSupportedMPPFlag = 0;
        layer->mCheckMPPFlag.clear();

        /* Check OtfMPPs */
        for (uint32_t j = 0; j < mOtfMPPs.size(); j++) {
            if ((ret = mOtfMPPs[j]->isSupported(*display, src_img, dst_img)) == NO_ERROR) {
                layer->mSupportedMPPFlag |= mOtfMPPs[j]->mLogicalType;
                HDEBUGLOGD(eDebugResourceAssigning, "\t%s: supported", mOtfMPPs[j]->mName.string());
            } else {
                if (((-ret) == eMPPUnsupportedFormat) &&
                    ((ret = mOtfMPPs[j]->isSupported(*display, src_img, dst_img_yuv)) == NO_ERROR)) {
                    layer->mSupportedMPPFlag |= mOtfMPPs[j]->mLogicalType;
                    HDEBUGLOGD(eDebugResourceAssigning, "\t%s: supported with yuv dst", mOtfMPPs[j]->mName.string());
                }
            }
            if (ret < 0) {
                HDEBUGLOGD(eDebugResourceAssigning, "\t%s: unsupported flag(0x%" PRIx64 ")", mOtfMPPs[j]->mName.string(), -ret);
                uint64_t checkFlag = 0x0;
                if (layer->mCheckMPPFlag.count(mOtfMPPs[j]->mLogicalType) != 0) {
                    checkFlag = layer->mCheckMPPFlag.at(mOtfMPPs[j]->mLogicalType);
                }
                checkFlag |= (-ret);
                layer->mCheckMPPFlag[mOtfMPPs[j]->mLogicalType] = checkFlag;
            }
        }

        /* Check M2mMPPs */
        for (uint32_t j = 0; j < mM2mMPPs.size(); j++) {
            if ((ret = mM2mMPPs[j]->isSupported(*display, src_img, dst_img)) == NO_ERROR) {
                layer->mSupportedMPPFlag |= mM2mMPPs[j]->mLogicalType;
                HDEBUGLOGD(eDebugResourceAssigning, "\t%s: supported", mM2mMPPs[j]->mName.string());
            } else {
                if (((-ret) == eMPPUnsupportedFormat) &&
                    ((ret = mM2mMPPs[j]->isSupported(*display, src_img, dst_img_yuv)) == NO_ERROR)) {
                    layer->mSupportedMPPFlag |= mM2mMPPs[j]->mLogicalType;
                    HDEBUGLOGD(eDebugResourceAssigning, "\t%s: supported with yuv dst", mM2mMPPs[j]->mName.string());
                }
            }
            if (ret < 0) {
                HDEBUGLOGD(eDebugResourceAssigning, "\t%s: unsupported flag(0x%" PRIx64 ")", mM2mMPPs[j]->mName.string(), -ret);
                uint64_t checkFlag = 0x0;
                if (layer->mCheckMPPFlag.count(mM2mMPPs[j]->mLogicalType) != 0) {
                    checkFlag = layer->mCheckMPPFlag.at(mM2mMPPs[j]->mLogicalType);
                }
                checkFlag |= (-ret);
                layer->mCheckMPPFlag[mM2mMPPs[j]->mLogicalType] = checkFlag;
            }
        }
        HDEBUGLOGD(eDebugResourceAssigning, "[%d] layer mSupportedMPPFlag(0x%8x)", i, layer->mSupportedMPPFlag);
    }
    HDEBUGLOGD(eDebugResourceAssigning, "%s-------------", __func__);

    return NO_ERROR;
}

int32_t ExynosResourceManager::resetResources()
{
    HDEBUGLOGD(eDebugResourceManager, "%s+++++++++", __func__);

    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        mOtfMPPs[i]->resetMPP();
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mOtfMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        mM2mMPPs[i]->resetMPP();
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mM2mMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }

    HDEBUGLOGD(eDebugResourceManager, "%s-----------",  __func__);
    return NO_ERROR;
}

int32_t ExynosResourceManager::preAssignResources()
{
    HDEBUGLOGD(eDebugResourceManager|eDebugResourceSetReserve, "%s+++++++++", __func__);
    uint32_t displayMode = mDevice->mDisplayMode;
    if (SELECTED_LOGIC == UNDEFINED) {
        for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
            if (mOtfMPPs[i]->mEnableByDebug == false || mOtfMPPs[i]->mDisableByUserScenario == true) {
                mOtfMPPs[i]->reserveMPP();
                continue;
            }

            if (mOtfMPPs[i]->mPreAssignDisplayList[displayMode] != 0) {
                HDEBUGLOGD(eDebugResourceAssigning, "\t%s check, dispMode(%d), 0x%8x", mOtfMPPs[i]->mName.string(), displayMode, mOtfMPPs[i]->mPreAssignDisplayList[displayMode]);

                ExynosDisplay *display = NULL;
                for (size_t j = 0; j < mDevice->mDisplays.size(); j++) {
                    display = mDevice->mDisplays[j];
                    if (display == NULL) continue;
                    int checkBit = mOtfMPPs[i]->mPreAssignDisplayList[displayMode] & (1<<(display->mType));
                    HDEBUGLOGD(eDebugResourceAssigning, "\t\tdisplay index(%zu), checkBit(%d)", j, checkBit);
                    if (checkBit) {
                        HDEBUGLOGD(eDebugResourceAssigning, "\t\tdisplay index(%zu), displayId(%d), display(%p)", j, display->mDisplayId, display);
                        if (display->mPlugState == true) {
                            HDEBUGLOGD(eDebugResourceAssigning, "\t\treserve to display %d", display->mDisplayId);
                            mOtfMPPs[i]->reserveMPP(display->mDisplayId);
                            break;
                        }
                    }
                }
            }
        }
    } else {
        for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
            if (mOtfMPPs[i]->mEnableByDebug == false || mOtfMPPs[i]->mDisableByUserScenario == true) {
                mOtfMPPs[i]->reserveMPP();
                continue;
            }

            HDEBUGLOGD(eDebugResourceAssigning|eDebugResourceSetReserve,
                "\t%s check, dispMode(%d)", mOtfMPPs[i]->mName.string(), displayMode);

            ExynosDisplay *display = NULL;
            for (size_t j = 0; j < mUseDpuDisplayNum; j++) {
                display = mDevice->mDisplays[j];
                if (display == NULL) continue;
                for (size_t k = 0; k < display->mAssignedResourceSet.size(); k++) {
                    for (uint32_t m = 0; m < MAX_DPPCNT_PER_SET; m++) {
                        if (!strcmp(display->mAssignedResourceSet[k]->dpps[m], "")) continue;
                        if (!strcmp(display->mAssignedResourceSet[k]->dpps[m],mOtfMPPs[i]->mName)) {
                            mOtfMPPs[i]->reserveMPP(display->mDisplayId);
                            goto found;
                        }
                    }
                }
            }
found:
            HDEBUGLOGD(eDebugResourceAssigning|eDebugResourceSetReserve,
                    "\t%s : disp[type:%d, index:%d], ", mOtfMPPs[i]->mName.string(), display->mType, display->mIndex);
        }
    }

    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mEnableByDebug == false || mM2mMPPs[i]->mDisableByUserScenario == true) {
            mM2mMPPs[i]->reserveMPP();
            continue;
        }

        int32_t tempDisplayId = -1;
        HDEBUGLOGD(eDebugResourceAssigning, "\t%s check, 0x%8x", mM2mMPPs[i]->mName.string(), mM2mMPPs[i]->mPreAssignDisplayList[displayMode]);
        if (mM2mMPPs[i]->mPreAssignDisplayList[displayMode] != 0) {
            ExynosDisplay *display = NULL;
            for (size_t j = 0; j < mDevice->mDisplays.size(); j++) {
                display = mDevice->mDisplays[j];
                if (display == NULL) continue;
                if (checkAlreadyReservedDisplay(mM2mMPPs[i]->mLogicalType, display->mDisplayId)) {
                    tempDisplayId = display->mDisplayId;
                    continue;
                }
                int checkBit = mM2mMPPs[i]->mPreAssignDisplayList[displayMode] & (1<<(display->mType));
                HDEBUGLOGD(eDebugResourceAssigning, "\t\tdisplay index(%zu), checkBit(%d)", j, checkBit);
                if (checkBit) {
                    HDEBUGLOGD(eDebugResourceAssigning, "\t\tdisplay index(%zu), displayId(%d), display(%p)", j, display->mDisplayId, display);
                    if (display->mPlugState == true) {
                        HDEBUGLOGD(eDebugResourceAssigning, "\t\treserve to display %d", display->mDisplayId);
                        mM2mMPPs[i]->reserveMPP(display->mDisplayId);
                    } else {
                        HDEBUGLOGD(eDebugResourceAssigning, "\t\treserve without display");
                        mM2mMPPs[i]->reserveMPP();
                    }
                    break;
                }
            }
        }
        if (mM2mMPPs[i]->mAssignedState != MPP_ASSIGN_STATE_RESERVED) {
            HDEBUGLOGD(eDebugResourceAssigning, "\t\treserve MPP that has the same logical type with previously reserved MPP to display %d", tempDisplayId);
            mM2mMPPs[i]->reserveMPP(tempDisplayId);
        }
    }
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mOtfMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (hwcCheckDebugMessages(eDebugResourceManager)) {
            String8 dumpMPP;
            mM2mMPPs[i]->dump(dumpMPP);
            HDEBUGLOGD(eDebugResourceManager, "%s", dumpMPP.string());
        }
    }
    HDEBUGLOGD(eDebugResourceManager|eDebugResourceSetReserve, "%s-----------",  __func__);
    return NO_ERROR;
}

int32_t ExynosResourceManager::distributeResourceSet() {
    if (SELECTED_LOGIC == UNDEFINED)
        return NO_ERROR;

    HDEBUGLOGD(eDebugResourceSetReserve, "[%s] +++++++++", __func__);
    displayEnableMap_t prevDisplayEnableState = mDisplayEnableState;
    uint32_t numOfEnabledDisplay = 0;

    /* Check the display (connection) state */
    ExynosDisplay *display = NULL;
    mDisplayEnableState = 0;
    for (uint32_t i = 0; i < mUseDpuDisplayNum; i++) {
        display = mDevice->mDisplays[i];
        if (display->isEnabled()) {
            mDisplayEnableState |= 1 << i;
            numOfEnabledDisplay++;
        }
    }

    if (numOfEnabledDisplay > MAX_ENABLED_DISPLAY_COUNT) {
        HWC_LOGE(NULL, "%s:: Too many connected display(connected display:%d, max display:%d)",
                __func__, numOfEnabledDisplay, MAX_ENABLED_DISPLAY_COUNT);
        return -1;
    }

    HDEBUGLOGD(eDebugResourceSetReserve, "%s:: previous display state(%d), current display state(%d)", __func__, prevDisplayEnableState, mDisplayEnableState);

    uint32_t prevSetCnt[mUseDpuDisplayNum];
    uint32_t currSetCnt[mUseDpuDisplayNum];

    /* Assign resource set.
     * If the display state is changed, the resource set is redistributed. */
    if (mDisplayEnableState == 0) {
       HWC_LOGE(NULL, "%s:: All displays were disconnected.It is impossible state, so keep the previous state", __func__);
       mDisplayEnableState = prevDisplayEnableState;
    } else if (prevDisplayEnableState != mDisplayEnableState) {
        /* Read the number of resource set from pre-defined table */
        for (size_t i = 0; i < mResourceAssignTable.size(); i++) {
            if (mResourceAssignTable[i].connection_state == prevDisplayEnableState) {
                for (size_t j = 0; j < mUseDpuDisplayNum; j++)
                    prevSetCnt[j] = mResourceAssignTable[i].setcnt[j];
            }
            if (mResourceAssignTable[i].connection_state == mDisplayEnableState) {
                for (size_t j = 0; j < mUseDpuDisplayNum; j++)
                    currSetCnt[j] = mResourceAssignTable[i].setcnt[j];
            }
        }
        /* redistribute resource set */
        moveResourceSet(prevSetCnt, currSetCnt);
    }

    for (size_t i = 0; i < mUseDpuDisplayNum; i++) {
        ExynosDisplay *display = mDevice->mDisplays[i];
        HDEBUGLOGD(eDebugResourceSetReserve, "%s:: display(%d) setcnt(%zu)",__func__, display->mDisplayId, display->mAssignedResourceSet.size());
    }

    HDEBUGLOGD(eDebugResourceSetReserve, "[%s] -----------",  __func__);
    return NO_ERROR;
}

int32_t ExynosResourceManager::moveResourceSet(uint32_t* prevSetCnt, uint32_t* currSetCnt)
{
    if (SELECTED_LOGIC == UNDEFINED)
        return NO_ERROR;
    int32_t totalCnt = 0;
    int32_t needSetCnt[mUseDpuDisplayNum];

    /* Calculate the number of resource sets that need to be moved
     * If needSetCnt has positive value, it means that this display need more resource sets.
     * If needSetCnt has negative value, it means that this display need less resource sets. */
    for (uint32_t i = 0; i < mUseDpuDisplayNum; i++) {
        needSetCnt[i] = (int32_t)currSetCnt[i] - (int32_t)prevSetCnt[i];
    }

    /* Map of display that need more DPPs, it is sorted in ascending order of priority. */
    std::map<uint32_t /* attr_priority */, uint32_t /* display index */> takingDppDisplays;
    /* Map of display that need less DPPs, it is sorted in descending order of priority.  */
    std::map<uint32_t /* attr_priority */, uint32_t /* display index */, std::greater<uint32_t>> givingDppDisplays;

    int32_t attrIndex = -1;
    int32_t nextAttrIndex = -1;
    const uint32_t sizeOfAttributePriorityList = sizeof(ATTRIBUTE_PRIORITY_LIST)/sizeof(uint32_t);
    std::map<uint32_t, uint32_t>::iterator iterTaking;
    std::map<uint32_t, uint32_t, std::greater<uint32_t>>::iterator iterGiving;

    for (uint32_t k = 0; k < sizeOfAttributePriorityList; k++) {
        takingDppDisplays.clear();
        givingDppDisplays.clear();
        attrIndex = ATTRIBUTE_PRIORITY_LIST[k];
        if (k < sizeOfAttributePriorityList - 1)
            nextAttrIndex = ATTRIBUTE_PRIORITY_LIST[k+1];
        else
            nextAttrIndex = -1;

        /* Fill data to map */
        for (uint32_t i = 0; i < mUseDpuDisplayNum; i++) {
            totalCnt += needSetCnt[i];
            if (needSetCnt[i] < 0) {
                givingDppDisplays.insert(std::make_pair(RESOURCE_INFO_TABLE[i].attr_priority[attrIndex], i));
            } else if (needSetCnt[i] > 0) {
                takingDppDisplays.insert(std::make_pair(RESOURCE_INFO_TABLE[i].attr_priority[attrIndex], i));
            }
        }

        if (totalCnt != 0) {
            HWC_LOGE(NULL, "%s:: Sum of updated set count must be 0, but now sum is %d, (line:%d)", __func__, totalCnt, __LINE__);
            return -1;
        }

        /* Assign resource sets that has the special attribute */
        for (iterTaking = takingDppDisplays.begin(); iterTaking != takingDppDisplays.end(); ++iterTaking) {
            if (needSetCnt[iterTaking->second] == 0) continue;

            bool assignDone = false;
            ExynosDisplay* takingDisplay = mDevice->mDisplays[iterTaking->second];

            /* If takingDisplay already supports a target attribute, skip the process for target attribute */
            for (size_t i = 0; i < takingDisplay->mAssignedResourceSet.size(); i++) {
                if (takingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex]) {
                    assignDone = true;
                    break;
                }
            }
            if (assignDone) continue;

            for (iterGiving = givingDppDisplays.begin(); iterGiving != givingDppDisplays.end(); ++iterGiving) {
                if (needSetCnt[iterTaking->second] == 0) break;
                if (needSetCnt[iterGiving->second] == 0) continue;

                uint32_t specialSetCnt = 0;
                ExynosDisplay* givingDisplay = mDevice->mDisplays[iterGiving->second];
                /* If giving display has more than two special set, giving display give a special set to taking display */
                for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                    if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex]) {
                        specialSetCnt++;
                    }
                }

                if (specialSetCnt > 1) {
                    /* This is the case that nextAttrIndex is not available.
                       It means this attrIndex is the last order. */
                    if (nextAttrIndex < 0) {
                        for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                            if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex]) {
                                moveOneResourceSet(i, needSetCnt, iterTaking->second, iterGiving->second, takingDisplay, givingDisplay);
                                assignDone = true;
                            }
                        }
                    } else {
                        /* If givingDisplay is disconnected or takingDisplay's priority about
                           next attribute is higher than givingDisplay's priority,
                           givingDisplay gives the resource set that supports next attribute to a takingDisplay. */
                        if (RESOURCE_INFO_TABLE[iterTaking->second].attr_priority[nextAttrIndex]
                            > RESOURCE_INFO_TABLE[iterGiving->second].attr_priority[nextAttrIndex] ||
                            (currSetCnt[iterGiving->second] == 0)) {
                            for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                                if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex] &&
                                        givingDisplay->mAssignedResourceSet[i]->attr_support[nextAttrIndex]) {
                                    moveOneResourceSet(i, needSetCnt, iterTaking->second, iterGiving->second, takingDisplay, givingDisplay);
                                    assignDone = true;
                                }
                            }
                        } else {
                            /* If takingDisplay's priority about next attribute is less than givingDisplay's priority,
                               givingDisplay gives the resource set found first in the mAssignedResourceSet.
                               Because mAssignedResourceSet is sorted by attribute supported by resource set. */
                            for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                                if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex]) {
                                    moveOneResourceSet(i, needSetCnt, iterTaking->second, iterGiving->second, takingDisplay, givingDisplay);
                                    assignDone = true;
                                }
                            }
                        }
                    }
                    break;
                } else {
                    /* If givingDisplay is disconnected or takingDisplay has high priority than giving display,
                       givingDisplay give a special set to takingDisplay.
                       In this case, givingDisplay gives the resource set found first in the mAssignedResourceSet. */
                    if ((iterTaking->first < iterGiving->first) || (currSetCnt[iterGiving->second] == 0)) {
                        for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                            if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex]) {
                                moveOneResourceSet(i, needSetCnt, iterTaking->second, iterGiving->second, takingDisplay, givingDisplay);
                                assignDone = true;
                                break;
                            }
                        }
                    }
                }
                /* If takingDisplay recieves a resource set, terminate the process for this takingDisplay. */
                if (assignDone) break;
            }

            /* "assignDone == false" means that takingDisplay cannot be assigned a special set.
               If takingDisplay is not assigned a special set, next takingDisplay cannot be assigned a special set, too */
            if (!assignDone) break;
        }
    }

    /* Assign resource sets regardless of special attribute */
    takingDppDisplays.clear();
    givingDppDisplays.clear();

    /* Fill data to map */
    for (uint32_t i = 0; i < mUseDpuDisplayNum; i++) {
        totalCnt += needSetCnt[i];
        if (needSetCnt[i] < 0) {
            givingDppDisplays.insert(std::make_pair(RESOURCE_INFO_TABLE[i].attr_priority[attrIndex], i));
        } else if (needSetCnt[i] > 0) {
            takingDppDisplays.insert(std::make_pair(RESOURCE_INFO_TABLE[i].attr_priority[attrIndex], i));
        }
    }

    if (totalCnt != 0) {
        HWC_LOGE(NULL, "%s:: Sum of updated set count must be 0, but now sum is %d, (line:%d)", __func__, totalCnt, __LINE__);
        return -1;
    }

    for(iterTaking = takingDppDisplays.begin(); iterTaking != takingDppDisplays.end(); ++iterTaking) {
        ExynosDisplay* takingDisplay = mDevice->mDisplays[iterTaking->second];
        if (needSetCnt[iterTaking->second] == 0) continue;
        for(iterGiving = givingDppDisplays.begin(); iterGiving != givingDppDisplays.end(); ++iterGiving) {
            if (needSetCnt[iterTaking->second] == 0) break;
            if (needSetCnt[iterGiving->second] == 0) continue;
            ExynosDisplay* givingDisplay = mDevice->mDisplays[iterGiving->second];

            /* If display is disconnected, all resource set is moved to remained display */
            if (currSetCnt[iterGiving->second] == 0) {
                for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                    moveOneResourceSet(i, needSetCnt, iterTaking->second, iterGiving->second, takingDisplay, givingDisplay);
                    if (needSetCnt[iterTaking->second] == 0) break;
                    if (needSetCnt[iterGiving->second] == 0) break;
                }
            } else {
                uint32_t attrCount[sizeOfAttributePriorityList] = {};
                /* Update total attribute of givingDisplay's resource */
                for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                    for (uint32_t j = 0; j < sizeOfAttributePriorityList; j++) {
                        attrIndex = ATTRIBUTE_PRIORITY_LIST[j];
                        if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex])
                            attrCount[j]++;
                    }
                }

                /* Move resource set from givingDisplay to takingDisplay.
                   It tries to move resource set that is not needed for givingDisplay. */
                for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                    bool setFound = false;
                    for (uint32_t j = 0; j < sizeOfAttributePriorityList; j++) {
                        attrIndex = ATTRIBUTE_PRIORITY_LIST[j];
                        if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex] && (attrCount[j] == 1)) {
                            setFound = false;
                            break;
                        } else {
                            setFound = true;
                        }
                    }
                    if (setFound) {
                        moveOneResourceSet(i, needSetCnt, iterTaking->second, iterGiving->second, takingDisplay, givingDisplay);
                        if (needSetCnt[iterTaking->second] == 0) break;
                        if (needSetCnt[iterGiving->second] == 0) break;
                        for (uint32_t j = 0; j < sizeOfAttributePriorityList; j++) {
                            attrIndex = ATTRIBUTE_PRIORITY_LIST[j];
                            if (givingDisplay->mAssignedResourceSet[i]->attr_support[attrIndex])
                                attrCount[j]--;
                        }
                    }
                }
            }
        }

        if (needSetCnt[iterTaking->second] != 0) {
            for(iterGiving = givingDppDisplays.begin(); iterGiving != givingDppDisplays.end(); ++iterGiving) {
                if (needSetCnt[iterTaking->second] == 0) break;
                if (needSetCnt[iterGiving->second] == 0) continue;
                ExynosDisplay* givingDisplay = mDevice->mDisplays[iterGiving->second];
                for (size_t i = 0; i < givingDisplay->mAssignedResourceSet.size(); i++) {
                    moveOneResourceSet(i, needSetCnt, iterTaking->second, iterGiving->second, takingDisplay, givingDisplay);
                    if (needSetCnt[iterTaking->second] == 0) break;
                    if (needSetCnt[iterGiving->second] == 0) break;
                }
            }
        }
    }

    /* Print debugging log */
    String8 debugStr;
    for (uint32_t i = 0; i < mUseDpuDisplayNum; i++) {
        ExynosDisplay *display = mDevice->mDisplays[i];
        debugStr.appendFormat("[%d] ", display->mDisplayId);
        if (display->mAssignedResourceSet.size() == 0)
            debugStr.appendFormat("not assigned, ");
        for (size_t j = 0; j < display->mAssignedResourceSet.size(); j++)
            for (uint32_t m = 0; m < MAX_DPPCNT_PER_SET; m++) {
                debugStr.appendFormat("%s, ", display->mAssignedResourceSet[j]->dpps[m]);
            }
    }
    ALOGI("%s:: connection state(%d), assign state: %s", __func__, mDisplayEnableState, debugStr.string());
    return NO_ERROR;
}

void ExynosResourceManager::moveOneResourceSet(size_t index, int32_t* needSetCnt, uint32_t takingIndex, uint32_t givingIndex, ExynosDisplay* takingDisplay, ExynosDisplay* givingDisplay)
{
    /* Move resource set from givingDisplay to takingDisplay */
    takingDisplay->mAssignedResourceSet.add(givingDisplay->mAssignedResourceSet[index]);
    givingDisplay->mAssignedResourceSet.removeAt(index);
    needSetCnt[takingIndex]--;
    needSetCnt[givingIndex]++;
}

void ExynosResourceManager::preAssignWindows()
{
    ExynosDisplay *display = NULL;

    if (SELECTED_LOGIC == UNDEFINED) {
        ExynosPrimaryDisplayModule *primaryDisplay =
            (ExynosPrimaryDisplayModule *)mDevice->getDisplay(getDisplayId(HWC_DISPLAY_PRIMARY, 0));
        primaryDisplay->usePreDefinedWindow(false);

        for (size_t i = 1; i < mDevice->mDisplays.size(); i++) {
            display = mDevice->mDisplays[i];
            if ((display == NULL) || (display->mType != HWC_DISPLAY_EXTERNAL))
                continue;
            if (display->mPlugState == true) {
                primaryDisplay->usePreDefinedWindow(true);
            }
        }
        return;
    }

    for (size_t i = 0; i < mUseDpuDisplayNum; i++) {
        display = mDevice->mDisplays[i];
        if (display == NULL) continue;
        display->mAssignedWindows.clear();
        for (size_t j = 0; j < display->mAssignedResourceSet.size(); j++) {
            for (uint32_t m = 0; m < MAX_DPPCNT_PER_SET; m++) {
               display->mAssignedWindows.add(display->mAssignedResourceSet[j]->windows[m]);
            }
        }
        display->mMaxWindowNum = display->mAssignedWindows.size();
    }

    if (hwcCheckDebugMessages(eDebugResourceSetReserve)) {
        String8 debugStr;
        debugStr.appendFormat("window status : ");
        for (size_t i = 0; i < mUseDpuDisplayNum; i++) {
            display = mDevice->mDisplays[i];
            debugStr.appendFormat("[type(%d),index(%d),num(%zu),windowIndex(", display->mType, display->mIndex, display->mAssignedWindows.size());
            for (size_t j = 0; j < display->mAssignedWindows.size(); j++) {
                debugStr.appendFormat("%d,", display->mAssignedWindows[j]);
            }
            debugStr.appendFormat(")] ");
        }
        HDEBUGLOGD(eDebugResourceSetReserve,"%s", debugStr.string());
    }
}

int32_t ExynosResourceManager::preProcessLayer(ExynosDisplay * display)
{
    hasHdrLayer = false;
    hasDrmLayer = false;

    for (uint32_t i = 0; i < display->mLayers.size(); i++) {
        ExynosLayer *layer = display->mLayers[i];
        /* mIsHdrLayer is known after preprocess */
        if (layer->mIsHdrLayer) hasHdrLayer = true;
        if ((layer->mLayerBuffer != NULL) && (getDrmMode(layer->mLayerBuffer) != NO_DRM))
            hasDrmLayer = true;
    }

    return NO_ERROR;
}

ExynosMPP* ExynosResourceManager::getExynosMPP(uint32_t type)
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i]->mLogicalType == type)
            return mOtfMPPs[i];
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mLogicalType == type)
            return mM2mMPPs[i];
    }

    return NULL;
}

ExynosMPP* ExynosResourceManager::getExynosMPP(uint32_t physicalType, uint32_t physicalIndex)
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if ((mOtfMPPs[i]->mPhysicalType == physicalType) &&
            (mOtfMPPs[i]->mPhysicalIndex == physicalIndex))
            return mOtfMPPs[i];
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if ((mM2mMPPs[i]->mPhysicalType == physicalType) &&
            (mM2mMPPs[i]->mPhysicalIndex == physicalIndex))
            return mM2mMPPs[i];
    }

    return NULL;
}

ExynosMPP* ExynosResourceManager::getExynosMPPForBlending(uint32_t displayId)
{
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        ExynosMPP *mpp = mM2mMPPs[i];

        if ((mpp->mMaxSrcLayerNum > 1) && (mpp->mReservedDisplay == displayId))
            return mpp;
    }

    return NULL;
}

int32_t ExynosResourceManager::updateResourceState()
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i]->mAssignedSources.size() == 0)
            mOtfMPPs[i]->requestHWStateChange(MPP_HW_STATE_IDLE);
        mOtfMPPs[i]->mPrevAssignedState = mOtfMPPs[i]->mAssignedState;
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mAssignedSources.size() == 0)
            mM2mMPPs[i]->requestHWStateChange(MPP_HW_STATE_IDLE);
        mM2mMPPs[i]->mPrevAssignedState = mM2mMPPs[i]->mAssignedState;
        mM2mMPPs[i]->mWasUsedPrevFrame = false;
    }
    return NO_ERROR;
}

void ExynosResourceManager::setFrameRateForPerformance(ExynosMPP __unused &mpp,
        AcrylicPerformanceRequestFrame *frame)
{
    int g2dFps = (int)(1000 / mpp.mCapacity);
    HDEBUGLOGD(eDebugResourceAssigning, "G2D setFrameRate %d", g2dFps);
    frame->setFrameRate(g2dFps);
}

int32_t ExynosResourceManager::deliverPerformanceInfo(ExynosDisplay *display)
{
    int ret = NO_ERROR;
    for (uint32_t mpp_physical_type = 0; mpp_physical_type < MPP_P_TYPE_MAX; mpp_physical_type++) {
        AcrylicPerformanceRequest request;
        uint32_t assignedInstanceNum = 0;
        uint32_t assignedInstanceIndex = 0;
        ExynosMPP *mpp = NULL;
        bool canSkipSetting = true;

        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            mpp = mM2mMPPs[i];
            if (mpp->mPhysicalType != mpp_physical_type)
                continue;
            if (mpp->mAssignedDisplay == NULL) {
                continue;
            }
            /* Performance setting can be skipped
             * if all of instance's mPrevAssignedState, mAssignedState
             * are MPP_ASSIGN_STATE_FREE
             */
            if ((mpp->mPrevAssignedState & MPP_ASSIGN_STATE_ASSIGNED) ||
                (mpp->mAssignedState & MPP_ASSIGN_STATE_ASSIGNED))
            {
                canSkipSetting = false;
            }

            /* post processing is not performed */
            if ((mpp->mAssignedDisplay == display) &&
                mpp->canSkipProcessing())
                continue;
            /*
             * canSkipProcessing() can not be used if post processing
             * was already performed
             * becasue variables that are used in canSkipProcessing()
             * to check whether there was update or not are set when
             * post processing is perfomed.
             */
            if ((display->mRenderingStateFlags.validateFlag ||
                 display->mRenderingStateFlags.presentFlag) &&
                (mpp->mWasUsedPrevFrame == true)) {
                continue;
            }

            if (mpp->mAssignedSources.size() > 0)
            {
                assignedInstanceNum++;
            }
        }
        if ((canSkipSetting == true) && (assignedInstanceNum != 0)) {
            HWC_LOGE(NULL, "%s:: canSKip true but assignedInstanceNum(%d)",
                    __func__, assignedInstanceNum);
        }

        if (canSkipSetting == true) {
            continue;
        }

        request.reset(assignedInstanceNum);

        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            mpp = mM2mMPPs[i];
            if ((mpp->mPhysicalType == mpp_physical_type) &&
                (mpp->mAssignedDisplay != NULL) &&
                (mpp->mAssignedSources.size() > 0))
            {
                /* post processing is not performed */
                if ((mpp->mAssignedDisplay == display) &&
                    mpp->canSkipProcessing())
                    continue;

                /*
                 * canSkipProcessing() can not be used if post processing
                 * was already performed
                 * becasue variables that are used in canSkipProcessing()
                 * to check whether there was update or not are set when
                 * post processing is perfomed.
                 */
                if ((display->mRenderingStateFlags.validateFlag ||
                     display->mRenderingStateFlags.presentFlag) &&
                    (mpp->mWasUsedPrevFrame == true))
                    continue;

                if (assignedInstanceIndex >= assignedInstanceNum) {
                    HWC_LOGE(NULL,"assignedInstanceIndex error (%d, %d)", assignedInstanceIndex, assignedInstanceNum);
                    break;
                }
                AcrylicPerformanceRequestFrame *frame = request.getFrame(assignedInstanceIndex);
                if(frame->reset(mpp->mAssignedSources.size()) == false) {
                    HWC_LOGE(NULL,"%d frame reset fail (%zu)", assignedInstanceIndex, mpp->mAssignedSources.size());
                    break;
                }
                setFrameRateForPerformance(*mpp, frame);

                for (uint32_t j = 0; j < mpp->mAssignedSources.size(); j++) {
                    ExynosMPPSource* mppSource = mpp->mAssignedSources[j];
                    frame->setSourceDimension(j,
                            mppSource->mSrcImg.w, mppSource->mSrcImg.h,
                            mppSource->mSrcImg.format);

                    hwc_rect_t src_area;
                    src_area.left = mppSource->mSrcImg.x;
                    src_area.top = mppSource->mSrcImg.y;
                    src_area.right = mppSource->mSrcImg.x + mppSource->mSrcImg.w;
                    src_area.bottom = mppSource->mSrcImg.y + mppSource->mSrcImg.h;

                    hwc_rect_t out_area;
                    out_area.left = mppSource->mMidImg.x;
                    out_area.top = mppSource->mMidImg.y;
                    out_area.right = mppSource->mMidImg.x + mppSource->mMidImg.w;
                    out_area.bottom = mppSource->mMidImg.y + mppSource->mMidImg.h;

                    frame->setTransfer(j, src_area, out_area, mppSource->mSrcImg.transform);
                }
                uint32_t format = mpp->mAssignedSources[0]->mMidImg.format;
                bool hasSolidColorLayer = false;
                if ((mpp->mMaxSrcLayerNum > 1) &&
                        (mpp->mAssignedSources.size() > 1) &&
                        (mpp->mNeedSolidColorLayer)) {
                    format = DEFAULT_MPP_DST_FORMAT;
                    hasSolidColorLayer = true;
                }

                frame->setTargetDimension(mpp->mAssignedDisplay->mXres,
                        mpp->mAssignedDisplay->mYres, format, hasSolidColorLayer);

                assignedInstanceIndex++;
            }
            if (mpp->mPhysicalType == MPP_G2D || mpp->mPhysicalType == MPP_MSC) {
                mpp->mAcrylicHandle->requestPerformanceQoS(&request);
            }
        }
    }
    return ret;
}

/*
 * Get used capacity of the resource that abstracts same HW resource
 * but it is different instance with mpp
 */
float ExynosResourceManager::getResourceUsedCapa(ExynosMPP &mpp)
{
    float usedCapa = 0;
    if (mpp.mCapacity < 0)
        return usedCapa;

    HDEBUGLOGD(eDebugResourceAssigning, "%s:: [%s][%d] mpp[%d, %d]",
            __func__, mpp.mName.string(), mpp.mLogicalIndex,
            mpp.mPhysicalType, mpp.mPhysicalIndex);

    if (mpp.mMPPType == MPP_TYPE_OTF) {
        for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
            if ((mpp.mPhysicalType == mOtfMPPs[i]->mPhysicalType) &&
                (mpp.mPhysicalIndex == mOtfMPPs[i]->mPhysicalIndex)) {
                usedCapa += mOtfMPPs[i]->mUsedCapacity;
            }
        }
    } else {
        for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
            if ((mpp.mPhysicalType == mM2mMPPs[i]->mPhysicalType) &&
                (mpp.mPhysicalIndex == mM2mMPPs[i]->mPhysicalIndex)) {
                usedCapa += mM2mMPPs[i]->mUsedCapacity;
                usedCapa += mM2mMPPs[i]->mPreAssignedCapacity;
            }
        }
    }

    HDEBUGLOGD(eDebugResourceAssigning, "\t[%s][%d] mpp usedCapa: %f",
            mpp.mName.string(), mpp.mLogicalIndex, usedCapa);
    return usedCapa;
}

void ExynosResourceManager::enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable)
{
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if ((mOtfMPPs[i]->mPhysicalType == physicalType) &&
            (mOtfMPPs[i]->mPhysicalIndex == physicalIndex) &&
            (mOtfMPPs[i]->mLogicalIndex == logicalIndex)) {
            mOtfMPPs[i]->mEnableByDebug = !!(enable);
            return;
        }
    }

    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if ((mM2mMPPs[i]->mPhysicalType == physicalType) &&
            (mM2mMPPs[i]->mPhysicalIndex == physicalIndex) &&
            (mM2mMPPs[i]->mLogicalIndex == logicalIndex)) {
            mM2mMPPs[i]->mEnableByDebug = !!(enable);
            return;
        }
    }
}

int32_t  ExynosResourceManager::prepareResources()
{
    int ret = NO_ERROR;
    HDEBUGLOGD(eDebugResourceManager, "This is first validate");
    if ((ret = resetResources()) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: resetResources() error (%d)",
                __func__, ret);
        return ret;
    }

    if ((ret = preAssignResources()) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: preAssignResources() error (%d)",
                __func__, ret);
        return ret;
    }

    preAssignWindows();

	return ret;
}

int32_t ExynosResourceManager::finishAssignResourceWork()
{
	int ret = NO_ERROR;
    if ((ret = updateResourceState()) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: stopUnAssignedResource() error (%d)",
                __func__, ret);
        return ret;
    }

    return ret;
}

int32_t ExynosResourceManager::initResourcesState(ExynosDisplay *display)
{
    int ret = 0;

    if (mDevice->isFirstValidate(display)) {
        HDEBUGLOGD(eDebugResourceManager, "This is first validate");
        if (exynosHWCControl.displayMode < DISPLAY_MODE_NUM)
            mDevice->mDisplayMode = exynosHWCControl.displayMode;

        if ((ret = prepareResources()) != NO_ERROR) {
            HWC_LOGE(display, "%s:: prepareResources() error (%d)",
                    __func__, ret);
            return ret;
        }
    }

    return NO_ERROR;
}

void ExynosResourceManager::makeSizeRestrictions(uint32_t mppId, restriction_size_t size, restriction_classification_t format) {

    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.hwType = static_cast<mpp_phycal_type_t>(mppId);
    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.nodeType = NODE_SRC;
    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.format = HAL_PIXEL_FORMAT_NONE;
    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.reserved = 0;
    mSizeRestrictions[format][mSizeRestrictionCnt[format]++].sizeRestriction = size;

    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.hwType = static_cast<mpp_phycal_type_t>(mppId);
    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.nodeType = NODE_DST;
    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.format = HAL_PIXEL_FORMAT_NONE;
    mSizeRestrictions[format][mSizeRestrictionCnt[format]].key.reserved = 0;
    mSizeRestrictions[format][mSizeRestrictionCnt[format]++].sizeRestriction = size;

    HDEBUGLOGD(eDebugDefault, "MPP : %s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",
            getMPPStr(mppId).string(),
            size.maxDownScale,
            size.maxUpScale,
            size.maxFullWidth,
            size.maxFullHeight,
            size.minFullWidth,
            size.minFullHeight,
            size.fullWidthAlign,
            size.fullHeightAlign,
            size.maxCropWidth,
            size.maxCropHeight,
            size.minCropWidth,
            size.minCropHeight,
            size.cropXAlign,
            size.cropYAlign,
            size.cropWidthAlign,
            size.cropHeightAlign);
}

void ExynosResourceManager::makeFormatRestrictions(restriction_key_t table, int deviceFormat) {

    mFormatRestrictions[mFormatRestrictionCnt] = table;

    HDEBUGLOGD(eDebugDefault, "MPP : %s, %d, %s(device : %d), %d"
            ,getMPPStr(mFormatRestrictions[mFormatRestrictionCnt].hwType).string()
            ,mFormatRestrictions[mFormatRestrictionCnt].nodeType
            ,getFormatStr(mFormatRestrictions[mFormatRestrictionCnt].format).string(), deviceFormat
            ,mFormatRestrictions[mFormatRestrictionCnt].reserved);
    mFormatRestrictionCnt++;
}

void ExynosResourceManager::makeAcrylRestrictions(mpp_phycal_type_t type){

    Acrylic *arc = NULL;
    const HW2DCapability *cap;
//    restriction_key queried_format_table[128];
    int cnt=0;

    if (type == MPP_MSC)
        arc = Acrylic::createScaler();
    else if (type == MPP_G2D)
        arc = Acrylic::createCompositor();
    else {
        ALOGE("Unknown MPP");
        return;
    }

    cap = &arc->getCapabilities();

    restriction_key_t queried_format_table[1024];

    /* format restriction */
    for (uint32_t i = 0; i < FORMAT_MAX_CNT; i++) {
        if (cap->isFormatSupported(exynos_format_desc[i].halFormat)) {
            queried_format_table[cnt].hwType = type;
            queried_format_table[cnt].nodeType = NODE_NONE;
            queried_format_table[cnt].format = exynos_format_desc[i].halFormat;
            queried_format_table[cnt].reserved = 0;
            makeFormatRestrictions(queried_format_table[cnt],
                    queried_format_table[cnt].format);
            cnt++;
        }
    }

    /* RGB size restrictions */
    restriction_size rSize;
    rSize.maxDownScale = cap->supportedMinMinification().hori;
    rSize.maxUpScale = cap->supportedMaxMagnification().hori;
    rSize.maxFullWidth = cap->supportedMaxSrcDimension().hori;
    rSize.maxFullHeight = cap->supportedMaxSrcDimension().vert;
    rSize.minFullWidth = cap->supportedMinSrcDimension().hori;
    rSize.minFullHeight = cap->supportedMinSrcDimension().vert;
    rSize.fullWidthAlign = cap->supportedDimensionAlign().hori;
    rSize.fullHeightAlign = cap->supportedDimensionAlign().vert;
    rSize.maxCropWidth = cap->supportedMaxSrcDimension().hori;
    rSize.maxCropHeight = cap->supportedMaxSrcDimension().vert;
    rSize.minCropWidth = cap->supportedMinSrcDimension().hori;
    rSize.minCropHeight = cap->supportedMinSrcDimension().vert;
    rSize.cropXAlign = cap->supportedDimensionAlign().hori;
    rSize.cropYAlign = cap->supportedDimensionAlign().vert;
    rSize.cropWidthAlign = cap->supportedDimensionAlign().hori;
    rSize.cropHeightAlign = cap->supportedDimensionAlign().vert;

    makeSizeRestrictions(type, rSize, RESTRICTION_RGB);

    /* YUV size restrictions */
    rSize.fullWidthAlign = max(static_cast<uint32_t>(cap->supportedDimensionAlign().hori),
            YUV_CHROMA_H_SUBSAMPLE);
    rSize.fullHeightAlign = max(static_cast<uint32_t>(cap->supportedDimensionAlign().vert),
            YUV_CHROMA_V_SUBSAMPLE);
    rSize.cropXAlign = max(static_cast<uint32_t>(cap->supportedDimensionAlign().hori),
            YUV_CHROMA_H_SUBSAMPLE);
    rSize.cropYAlign = max(static_cast<uint32_t>(cap->supportedDimensionAlign().vert),
            YUV_CHROMA_V_SUBSAMPLE);
    rSize.cropWidthAlign = max(static_cast<uint32_t>(cap->supportedDimensionAlign().hori),
            YUV_CHROMA_H_SUBSAMPLE);
    rSize.cropHeightAlign = max(static_cast<uint32_t>(cap->supportedDimensionAlign().vert),
            YUV_CHROMA_V_SUBSAMPLE);

    makeSizeRestrictions(type, rSize, RESTRICTION_YUV);

    delete arc;
}

mpp_phycal_type_t ExynosResourceManager::getPhysicalType(int ch) {

    for (int i=0; i < MAX_DECON_DMA_TYPE; i++){
        if(IDMA_CHANNEL_MAP[i].channel == ch)
            return IDMA_CHANNEL_MAP[i].type;
    }

    return MPP_P_TYPE_MAX;
}

void ExynosResourceManager::updateRestrictions() {

    if (mDevice->mDeviceInterface->getUseQuery() == true) {
        uint32_t num_mpp_units = sizeof(AVAILABLE_M2M_MPP_UNITS)/sizeof(exynos_mpp_t);
        for(uint32_t i = MPP_DPP_NUM; i < MPP_P_TYPE_MAX; i++){
            for(uint32_t j = 0; j < num_mpp_units; j++){
                if(AVAILABLE_M2M_MPP_UNITS[j].physicalType == i){
                    makeAcrylRestrictions((mpp_phycal_type_t)i);
                    break;
                }
            }
        }
    } else {
        mFormatRestrictionCnt = sizeof(restriction_format_table)/sizeof(restriction_key);
        for (uint32_t i = 0 ; i < mFormatRestrictionCnt; i++) {
            mFormatRestrictions[i].hwType = restriction_format_table[i].hwType;
            mFormatRestrictions[i].nodeType = restriction_format_table[i].nodeType;
            mFormatRestrictions[i].format = restriction_format_table[i].format;
            mFormatRestrictions[i].reserved = restriction_format_table[i].reserved;
        }

        // i = RGB, YUV
        // j = Size restriction count for each format (YUV, RGB)
        for (uint32_t i = 0; i < sizeof(restriction_tables)/sizeof(restriction_table_element); i++) {
            mSizeRestrictionCnt[i] = restriction_tables[i].table_element_size;
            for (uint32_t j = 0; j < mSizeRestrictionCnt[i]; j++) {
                memcpy(&mSizeRestrictions[i][j], &restriction_tables[i].table[j],
                        sizeof(restriction_size_element_t));
            }
        }
    }

    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        // mAttr should be updated with updated feature_table
        mOtfMPPs[i]->updateAttr();
        mOtfMPPs[i]->setupRestriction();
    }

    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        // mAttr should be updated with updated feature_table
        mM2mMPPs[i]->updateAttr();
        mM2mMPPs[i]->setupRestriction();
    }
}

uint32_t ExynosResourceManager::getFeatureTableSize()
{
    return sizeof(feature_table)/sizeof(feature_support_t);
}

void ExynosResourceManager::generateResourceTable()
{
    if (SELECTED_LOGIC == UNDEFINED)
        return;

    /* Initialize resource set table */
    memcpy(mResourceSetTable, REFERENCE_RESOURCE_SET_TABLE, sizeof(mResourceSetTable));
    /* Update attribute of resource set table */
    for (uint32_t i = 0; i < RESOURCE_SET_COUNT; i++) {
        for (uint32_t j = 0; j < MAX_DPPCNT_PER_SET; j++) {
            for (uint32_t k = 0; k < mOtfMPPs.size(); k++) {
                if (!strcmp(mResourceSetTable[i].dpps[j], mOtfMPPs[k]->mName)) {
                    mResourceSetTable[i].attr_support[ATTRIBUTE_AFBC_NO_RESTRICTION]
                        = mOtfMPPs[k]->isSupportedFullAfbcChannel(mResourceSetTable[i]);
                    mResourceSetTable[i].attr_support[ATTRIBUTE_AFBC] |=
                        mOtfMPPs[k]->mAttr & MPP_ATTR_AFBC;
                    mResourceSetTable[i].attr_support[ATTRIBUTE_HDR10PLUS] |=
                        mOtfMPPs[k]->mAttr & MPP_ATTR_HDR10PLUS;
                }
            }
        }
    }

    /* Count the number of display using decon */
    for (size_t i = 0; i < mDevice->mDisplays.size(); i++) {
        ExynosDisplay *display = mDevice->mDisplays[i];
        if(display->mUseDpu)
            mUseDpuDisplayNum++;
    }

    /* Cacluate the table size (table size = 2^(the number of display using decon) */
    uint32_t tableSize = (uint32_t)pow(2, mUseDpuDisplayNum);
    ALOGI("%s:: mUseDpuDisplayNum(%d) tableSize(%d)", __func__, mUseDpuDisplayNum, tableSize);

    mResourceAssignTable.clear();

    switch(SELECTED_LOGIC) {
        case DEFAULT_LOGIC:
        default:
            updateTableEvenly(tableSize);
            break;
    }

    String8 debugStr;
    for (size_t i = 0; i < mResourceAssignTable.size(); i++) {
        debugStr.appendFormat("{%d, {", mResourceAssignTable[i].connection_state);
        for (uint32_t j = 0; j < mUseDpuDisplayNum; j++) {
            debugStr.appendFormat(" %d,", mResourceAssignTable[i].setcnt[j]);
        }
        debugStr.appendFormat("}}, ");
    }
    ALOGI("%s:: defined resource table info: %s", __func__, debugStr.string());
}

void ExynosResourceManager::updateTableEvenly(uint32_t tableSize) {

    /* Fill the table that defines the number of resource set assgined to display */
    for (uint32_t i = 0; i < tableSize; i++) {
        uint32_t remainSetCnt = RESOURCE_SET_COUNT;

        /* Map of display that can be connected, it is sorted in ascending order of priority. */
        std::map<uint32_t /* dpp_priority */, uint32_t /* display index */> enabledDisplays;
        resource_table_t tableColumn = {i, {0,}};

        /* Ignore the case that all displays are disconnected */
        if (i == 0) {
            mResourceAssignTable.push_back(tableColumn);
            continue;
        }

        uint32_t enabled = 0;
        for (uint32_t j = 0; j < mUseDpuDisplayNum; j++) {
            enabled = i & (1 << j);
            if (enabled) {
                if (RESOURCE_INFO_TABLE[j].fix_setcnt) {
                    tableColumn.setcnt[j] = RESOURCE_INFO_TABLE[j].fix_setcnt;
                    remainSetCnt -= RESOURCE_INFO_TABLE[j].fix_setcnt;
                } else {
                    enabledDisplays.insert(std::make_pair(RESOURCE_INFO_TABLE[j].dpp_priority, j));
                }
            }
        }

        uint32_t remainder = remainSetCnt % enabledDisplays.size();
        uint32_t quotient = remainSetCnt / enabledDisplays.size();
        std::map<uint32_t, uint32_t>::iterator iter;
        for (iter = enabledDisplays.begin(); iter != enabledDisplays.end(); ++iter) {
            /* Distribute resource set evenly to each display */
            tableColumn.setcnt[iter->second] = quotient;
            remainSetCnt -= quotient;
            /* Distribute resource set by priority */
            if (remainder > 0) {
                tableColumn.setcnt[iter->second]++;
                remainder--;
                remainSetCnt--;
            }
        }
        /* If fix_setcnt is not zero, resource sets may be remained.
         * In this case, remaining resource sets are assigned to 0th display. */
        if (remainSetCnt) {
            for (uint32_t j = 0; j < mUseDpuDisplayNum; j++) {
                if (RESOURCE_INFO_TABLE[j].fix_setcnt != 0) continue;
                tableColumn.setcnt[j] += remainSetCnt;
            }
        }

        mResourceAssignTable.push_back(tableColumn);
    }
}

bool ExynosResourceManager::assignDisplayList(android::Vector<ExynosDisplay*> displayList, uint32_t attribute)
{
    uint32_t check = 0;
    for (uint32_t i = 0; i < mDevice->mDisplays.size(); i++) {
        if ((mDevice->mDisplays[i]->mType == HWC_DISPLAY_VIRTUAL) &&
            (mDevice->mDisplays[i]->mPlugState == true))
            return false;
    }
    for (uint32_t i = 0; i < sizeof(ATTRIBUTE_PRIORITY_LIST) / sizeof(uint32_t); i++) {
        if (ATTRIBUTE_PRIORITY_LIST[i] == attribute) {
            check = 1;
            break;
        }
    }
    if (check) {
        for (uint32_t i = 0; i < sizeof(RESOURCE_INFO_TABLE) / sizeof(display_resource_info_t); i++ ) {
            for (uint32_t j = 0; j < sizeof(RESOURCE_INFO_TABLE) / sizeof(display_resource_info_t); j++ ) {
                if (RESOURCE_INFO_TABLE[j].attr_priority[attribute] == i) {
                    ExynosDisplay *tmpDisplay = mDevice->getDisplay(RESOURCE_INFO_TABLE[j].displayId);
                    if (tmpDisplay && (tmpDisplay->mPlugState == true)) {
                        displayList.add(tmpDisplay);
                        break;
                    }
                }
            }
        }
    } else {
        for (int32_t j = mDevice->mDisplays.size() - 1; j >= 0; j--) {
            if (mDevice->mDisplays[j]->mPlugState == true)
                displayList.add(mDevice->mDisplays[j]);
        }
    }
    return true;
}

bool ExynosResourceManager::checkLayerAttribute(ExynosDisplay* display, ExynosLayer* layer,
        size_t type) {
    exynos_image src_img;
    layer->setSrcExynosImage(&src_img);

    if (type == ATTRIBUTE_DRM)
        return layer->isDrm();
    if (type == ATTRIBUTE_HDR10PLUS)
        return hasHdr10Plus(src_img);
    if (type == ATTRIBUTE_HDR10)
        return hasHdrInfo(layer->mDataSpace);

    return false;
}

bool ExynosResourceManager::isProcessingWithInternals(ExynosDisplay* display,
        exynos_image* src_img, exynos_image* dst_img) {
    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if ((mOtfMPPs[i]->mReservedDisplay == display->mDisplayId) &&
            (mOtfMPPs[i]->isSupported(*display, *src_img, *dst_img) == NO_ERROR))
            return true;
    }
    return false;
}

int ExynosResourceManager::setPreAssignedCapacity(ExynosDisplay *display, ExynosLayer *layer,
        exynos_image *src_img, exynos_image *dst_img, mpp_phycal_type_t type) {
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if ((mM2mMPPs[i]->mReservedDisplay == display->mDisplayId) &&  // ID check
            (mM2mMPPs[i]->mPhysicalType == type) && // type check
            (mM2mMPPs[i]->isAssignable(display, *src_img, *dst_img))) { // capacity check

            exynos_image image_lists[M2M_MPP_OUT_IMAGS_COUNT];
            uint32_t imageNum = M2M_MPP_OUT_IMAGS_COUNT;
            exynos_image otf_src_img = *dst_img;
            exynos_image otf_dst_img = *dst_img;

            if (getCandidateM2mMPPOutImages(display, layer, &imageNum, image_lists) < 0)
                return -1;

            for (uint32_t outImg = 0; outImg < imageNum; outImg++) {
                otf_src_img = image_lists[outImg];
                otf_src_img.transform = 0;
                otf_dst_img.transform = 0;
                int64_t ret = mM2mMPPs[i]->isSupported(*display, *src_img, otf_src_img);

                if (ret != NO_ERROR)
                    continue;

                for (uint32_t j = 0; j < mOtfMPPs.size(); j++) {
                    if ((mOtfMPPs[j]->mReservedDisplay == display->mDisplayId) &&
                        (mOtfMPPs[j]->isSupported(*display, otf_src_img, otf_dst_img))) {
                        mM2mMPPs[i]->mPreAssignedCapacity = mM2mMPPs[i]->getRequiredCapacity(display, *src_img, *dst_img);
                        HDEBUGLOGD(eDebugResourceManager, "%s::[display %u] [MPP %s] preAssigned capacity : %lf",
                                __func__, display->mDisplayId, mM2mMPPs[i]->mName.string(), mM2mMPPs[i]->mPreAssignedCapacity);
                        return 0;
                    }
                }
            }
        }
    }
    HDEBUGLOGD(eDebugResourceManager, "%s::Can not preassign capacity", __func__);
    return -1;
}

void ExynosResourceManager::preAssignM2MResources() {
    for (size_t i = 0; i < mM2mMPPs.size(); i++)
        mM2mMPPs[i]->mPreAssignedCapacity = (float)0.0f;

    for (uint32_t k = 0; k < mLayerAttributePriority.size(); k++) {
        android::Vector<ExynosDisplay*> displayList;
        if (assignDisplayList(displayList, mLayerAttributePriority[k]) == false)
            return;

        for (size_t i = 0; i < displayList.size(); i++) {
            for (size_t j = 0; j < displayList[i]->mLayers.size(); j++) {
                if (checkLayerAttribute(displayList[i], displayList[i]->mLayers[j], mLayerAttributePriority[k])) {
                    int ret = 0;
                    exynos_image src_img, dst_img;
                    displayList[i]->mLayers[j]->setSrcExynosImage(&src_img);
                    displayList[i]->mLayers[j]->setDstExynosImage(&dst_img);

                    if (!isProcessingWithInternals(displayList[i], &src_img, &dst_img)) {
                        ret = setPreAssignedCapacity(displayList[i], displayList[i]->mLayers[j], &src_img, &dst_img, MPP_MSC);
                        if (ret)
                            ret = setPreAssignedCapacity(displayList[i], displayList[i]->mLayers[j], &src_img, &dst_img, MPP_G2D);
                    }
                }
            }
        }
    }
}

float ExynosResourceManager::getAssignedCapacity(uint32_t physicalType)
{
    float totalCapacity = 0;

    for (size_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mPhysicalType == physicalType)
            totalCapacity += mM2mMPPs[i]->getAssignedCapacity();
    }
    return totalCapacity;
}

void ExynosResourceManager::setM2MCapa(uint32_t physicalType, uint32_t capa)
{
    for (size_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mPhysicalType == physicalType)
            mM2mMPPs[i]->mCapacity = capa;
    }
}

bool ExynosResourceManager::checkAlreadyReservedDisplay(uint32_t logicalType, uint32_t displayId)
{
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if ((mM2mMPPs[i]->mLogicalType == logicalType) &&
                (mM2mMPPs[i]->mReservedDisplay == displayId))
            return true;
    }

    return false;
}

bool ExynosResourceManager::hasHDR10PlusMPP() {

    for (uint32_t i = 0; i < mOtfMPPs.size(); i++) {
        if (mOtfMPPs[i] == NULL) continue;
        if (mOtfMPPs[i]->mAttr & MPP_ATTR_HDR10PLUS)
            return true;
    }
    for (uint32_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i] == NULL) continue;
        if (mM2mMPPs[i]->mAttr & MPP_ATTR_HDR10PLUS)
            return true;
    }

    return false;
}

void ExynosResourceManager::setM2mTargetCompression()
{
    for (size_t i = 0; i < mM2mMPPs.size(); i++) {
        if (mM2mMPPs[i]->mLogicalType == MPP_LOGICAL_G2D_RGB) {
            if (mM2mMPPs[i]->mReservedDisplay < 0) {
                continue;
            } else {
                ExynosDisplay *display = mDevice->getDisplay(mM2mMPPs[i]->mReservedDisplay);
                mM2mMPPs[i]->mNeedCompressedTarget = display->mExynosCompositionInfo.mCompressed;
            }
        }
    }
}
