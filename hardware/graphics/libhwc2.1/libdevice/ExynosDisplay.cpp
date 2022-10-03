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

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)
//#include <linux/fb.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <cutils/properties.h>
#include <utils/CallStack.h>
#include <hardware/hwcomposer_defs.h>
#include <android/sync.h>
#include <cmath>

#include <map>
#include "ExynosDisplay.h"
#include "ExynosExternalDisplay.h"
#include "ExynosLayer.h"
#include "ExynosHWCHelper.h"
#include "exynos_format.h"

#include <sys/mman.h>
#include <unistd.h>

/**
 * ExynosDisplay implementation
 */

using namespace android;
extern struct exynos_hwc_control exynosHWCControl;
extern struct update_time_info updateTimeInfo;

AssignedResourceVector::AssignedResourceVector() {
}

AssignedResourceVector::AssignedResourceVector(const AssignedResourceVector& rhs)
    : android::SortedVector<resource_set_t*>(rhs) {
}

int AssignedResourceVector::do_compare(const void* lhs, const void* rhs) const
{
    if (lhs == NULL || rhs == NULL)
        return 0;

    const resource_set_t* l = *(resource_set_t**)lhs;
    const resource_set_t* r = *(resource_set_t**)rhs;
    uint32_t l_key = 0;
    uint32_t r_key = 0;
    uint32_t size_of_attribute_priority_list = sizeof(ATTRIBUTE_PRIORITY_LIST)/sizeof(uint32_t);

    /* AssignedResourceVector is sorted by key value maded by bit operation of attribute.
       The more important attribute becomes the higher bit. */
    for (uint32_t i = 0; i < size_of_attribute_priority_list; i++) {
        uint32_t attr_index = ATTRIBUTE_PRIORITY_LIST[i];
        l_key |= l->attr_support[attr_index] << (size_of_attribute_priority_list -1 - i);
        r_key |= r->attr_support[attr_index] << (size_of_attribute_priority_list -1 - i);
    }
    if(l_key != r_key) {
        return l_key - r_key;
    }

    return l->windows[0] - r->windows[0];
}

int ExynosSortedLayer::compare(ExynosLayer * const *lhs, ExynosLayer *const *rhs)
{
    ExynosLayer *left = *((ExynosLayer**)(lhs));
    ExynosLayer *right = *((ExynosLayer**)(rhs));
    return left->mZOrder > right->mZOrder;
}

ssize_t ExynosSortedLayer::remove(const ExynosLayer *item)
{
    for (size_t i = 0; i < this->size(); i++)
    {
        if (this->array()[i] == item)
        {
            this->removeAt(i);
            return i;
        }
    }
    return -1;
}

status_t ExynosSortedLayer::vector_sort()
{
    return this->sort(compare);
}

ExynosLowFpsLayerInfo::ExynosLowFpsLayerInfo()
    : mHasLowFpsLayer(false),
    mFirstIndex(-1),
    mLastIndex(-1)
{
}

void ExynosLowFpsLayerInfo::initializeInfos()
{
    mHasLowFpsLayer = false;
    mFirstIndex = -1;
    mLastIndex = -1;
}

int32_t ExynosLowFpsLayerInfo::addLowFpsLayer(uint32_t layerIndex)
{
    if (mHasLowFpsLayer == false) {
        mFirstIndex = layerIndex;
        mLastIndex = layerIndex;
        mHasLowFpsLayer = true;
    } else {
        mFirstIndex = min(mFirstIndex, (int32_t)layerIndex);
        mLastIndex = max(mLastIndex, (int32_t)layerIndex);
    }
    return NO_ERROR;
}

ExynosCompositionInfo::ExynosCompositionInfo(uint32_t type)
    : ExynosMPPSource(MPP_SOURCE_COMPOSITION_TARGET, this),
    mType(type),
    mHasCompositionLayer(false),
    mFirstIndex(-1),
    mLastIndex(-1),
    mTargetBuffer(NULL),
    mDataSpace(HAL_DATASPACE_UNKNOWN),
    mAcquireFence(-1),
    mReleaseFence(-1),
    mEnableSkipStatic(false),
    mSkipStaticInitFlag(false),
    mSkipFlag(false),
    mWindowIndex(-1)
{
    /* If AFBC compression of mTargetBuffer is changed, */
    /* mCompressed should be set properly before resource assigning */

    char value[256];
    int afbc_prop;
    property_get("ro.vendor.ddk.set.afbc", value, "0");
    afbc_prop = atoi(value);

    if (afbc_prop == 0)
        mCompressed = false;
    else
        mCompressed = true;

    memset(&mSkipSrcInfo, 0, sizeof(mSkipSrcInfo));
    for (int i = 0; i < NUM_SKIP_STATIC_LAYER; i++) {
        mSkipSrcInfo.srcInfo[i].acquireFenceFd = -1;
        mSkipSrcInfo.srcInfo[i].releaseFenceFd = -1;
        mSkipSrcInfo.dstInfo[i].acquireFenceFd = -1;
        mSkipSrcInfo.dstInfo[i].releaseFenceFd = -1;
    }

    if(type == COMPOSITION_CLIENT)
        mEnableSkipStatic = true;

    memset(&mLastWinConfigData, 0x0, sizeof(mLastWinConfigData));
    mLastWinConfigData.acq_fence = -1;
    mLastWinConfigData.rel_fence = -1;
}

void ExynosCompositionInfo::initializeInfos(ExynosDisplay *display)
{
    mHasCompositionLayer = false;
    mFirstIndex = -1;
    mLastIndex = -1;
    mTargetBuffer = NULL;
    mDataSpace = HAL_DATASPACE_UNKNOWN;
    if (mAcquireFence >= 0) {
        ALOGD("ExynosCompositionInfo(%d):: mAcquire is not initialized(%d)", mType, mAcquireFence);
        if (display != NULL)
            fence_close(mAcquireFence, display, FENCE_TYPE_UNDEFINED, FENCE_IP_UNDEFINED);
    }
    mAcquireFence = -1;
    if (mReleaseFence >= 0) {
        ALOGD("ExynosCompositionInfo(%d):: mReleaseFence is not initialized(%d)", mType, mReleaseFence);
        if (display!= NULL)
            fence_close(mReleaseFence, display, FENCE_TYPE_UNDEFINED, FENCE_IP_UNDEFINED);
    }
    mReleaseFence = -1;
    mWindowIndex = -1;
    mOtfMPP = NULL;
    if ((display != NULL) &&
        (mType == COMPOSITION_EXYNOS) &&
        (mM2mMPP == NULL)) {
        mM2mMPP = display->mResourceManager->getExynosMPPForBlending(display->mDisplayId);
        if (mM2mMPP == NULL)
            ALOGV("display[%d]  m2mMPP for Blending is NULL", display->mType);
    }
}

void ExynosCompositionInfo::setTargetBuffer(ExynosDisplay *display, private_handle_t *handle,
        int32_t acquireFence, android_dataspace dataspace)
{
    mTargetBuffer = handle;
    if (mType == COMPOSITION_CLIENT) {
        if (display != NULL)
            mAcquireFence = hwcCheckFenceDebug(display, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_FB, acquireFence);
    } else {
        if (display != NULL)
            mAcquireFence = hwcCheckFenceDebug(display, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D, acquireFence);
    }
    if ((display != NULL) && (mDataSpace != dataspace))
        display->setGeometryChanged(GEOMETRY_DISPLAY_DATASPACE_CHANGED);
    mDataSpace = dataspace;
}

void ExynosCompositionInfo::setCompressed(bool compressed)
{
    mCompressed = compressed;
}

bool ExynosCompositionInfo::getCompressed()
{
    return mCompressed;
}

void ExynosCompositionInfo::dump(String8& result)
{
    result.appendFormat("CompositionInfo (%d)\n", mType);
    result.appendFormat("mHasCompositionLayer(%d)\n", mHasCompositionLayer);
    if (mHasCompositionLayer) {
        result.appendFormat("\tfirstIndex: %d, lastIndex: %d, dataSpace: 0x%8x, compressed: %d, windowIndex: %d\n",
                mFirstIndex, mLastIndex, mDataSpace, mCompressed, mWindowIndex);
        result.appendFormat("\thandle: %p, acquireFence: %d, releaseFence: %d, skipFlag: %d",
                mTargetBuffer, mAcquireFence, mReleaseFence, mSkipFlag);
        if ((mOtfMPP == NULL) && (mM2mMPP == NULL))
            result.appendFormat("\tresource is not assigned\n");
        if (mOtfMPP != NULL)
            result.appendFormat("\tassignedMPP: %s\n", mOtfMPP->mName.string());
        if (mM2mMPP != NULL)
            result.appendFormat("\t%s\n", mM2mMPP->mName.string());
    }
    if (mTargetBuffer != NULL) {
        uint64_t internal_format = 0;
        internal_format = mTargetBuffer->internal_format;
        result.appendFormat("\tinternal_format: 0x%" PRIx64 ", compressed: %d\n",
                internal_format, isCompressed(mTargetBuffer));
    }
    uint32_t assignedSrcNum = 0;
    if ((mM2mMPP != NULL) &&
        ((assignedSrcNum = mM2mMPP->mAssignedSources.size()) > 0)) {
        result.appendFormat("\tAssigned source num: %d\n", assignedSrcNum);
        result.append("\t");
        for (uint32_t i = 0; i < assignedSrcNum; i++) {
            if (mM2mMPP->mAssignedSources[i]->mSourceType == MPP_SOURCE_LAYER) {
                ExynosLayer* layer = (ExynosLayer*)(mM2mMPP->mAssignedSources[i]);
                result.appendFormat("[%d]layer_%p ", i, layer->mLayerBuffer);
            } else {
                result.appendFormat("[%d]sourceType_%d ", i, mM2mMPP->mAssignedSources[i]->mSourceType);
            }
        }
        result.append("\n");
    }
    result.append("\n");
}

String8 ExynosCompositionInfo::getTypeStr()
{
    switch(mType) {
    case COMPOSITION_NONE:
        return String8("COMPOSITION_NONE");
    case COMPOSITION_CLIENT:
        return String8("COMPOSITION_CLIENT");
    case COMPOSITION_EXYNOS:
        return String8("COMPOSITION_EXYNOS");
    default:
        return String8("InvalidType");
    }
}

ExynosDisplay::ExynosDisplay(uint32_t index, ExynosDevice *device)
:   mDisplayId(0),
    mType(HWC_DISPLAY_PRIMARY),
    mIndex(index),
    mDeconNodeName(""),
    mXres(1440),
    mYres(2960),
    mXdpi(25400),
    mYdpi(25400),
    mVsyncPeriod(16666666),
    mVsyncFd(-1),
    mDSCHSliceNum(1),
    mDSCYSliceSize(mYres),
    mDevice(device),
    mDisplayName(""),
    mPlugState(false),
    mHasSingleBuffer(false),
    mResourceManager(NULL),
    mClientCompositionInfo(COMPOSITION_CLIENT),
    mExynosCompositionInfo(COMPOSITION_EXYNOS),
    mGeometryChanged(0x0),
    mRenderingState(RENDERING_STATE_NONE),
    mHWCRenderingState(RENDERING_STATE_NONE),
    mDisplayBW(0),
    mDynamicReCompMode(NO_MODE_SWITCH),
    mDREnable(false),
    mDRDefault(false),
    mErrorFrameCount(0),
    mUpdateEventCnt(0),
    mDumpCount(0),
    mDefaultDMA(MAX_DECON_DMA_TYPE),
    mLastRetireFence(-1),
    mWindowNumUsed(0),
    mBaseWindowIndex(0),
    mBlendingNoneIndex(-1),
    mNumMaxPriorityAllowed(1),
    mCursorIndex(-1),
    mColorTransformHint(HAL_COLOR_TRANSFORM_IDENTITY),
    mHWC1LayerList(NULL),
    /* Support DDI scalser */
    mOldScalerMode(0),
    mNewScaledWidth(0),
    mNewScaledHeight(0),
    mDeviceXres(0),
    mDeviceYres(0),
    mColorMode(HAL_COLOR_MODE_NATIVE),
    mNeedSkipPresent(false),
    mBrightnessFd(NULL),
    mMaxBrightness(0),
    mDisplayInterface(NULL)
{
    mDisplayControl.enableCompositionCrop = true;
    mDisplayControl.enableExynosCompositionOptimization = true;
    mDisplayControl.enableClientCompositionOptimization = true;
    mDisplayControl.useMaxG2DSrc = false;
    mDisplayControl.handleLowFpsLayers = false;
    mDisplayControl.earlyStartMPP = true;
    mDisplayControl.adjustDisplayFrame = false;
    mDisplayControl.cursorSupport = false;

    mDisplayConfigs.clear();

    this->mPowerModeState = HWC2_POWER_MODE_OFF;
    this->mVsyncState = HWC2_VSYNC_DISABLE;

    /* TODO : Exception handling here */

    if (device == NULL) {
        ALOGE("Display creation failed!");
        return;
    }

    mResourceManager = device->mResourceManager;

    /* The number of window is same with the number of otfMPP */
    mMaxWindowNum = mResourceManager->getOtfMPPs().size();

    for(uint32_t i = 0; i < mMaxWindowNum; i++)
    {
        exynos_win_config_data config_data;
        mDpuData.configs.push_back(config_data);
        mLastDpuData.configs.push_back(config_data);
    }
    ALOGI("window configs size(%zu)", mDpuData.configs.size());

    mLowFpsLayerInfo.initializeInfos();

    mUseDpu = true;
    return;
}

ExynosDisplay::~ExynosDisplay()
{
    if (mDisplayInterface != NULL)
        delete mDisplayInterface;
}

/**
 * Member function for Dynamic AFBC Control solution.
 */
bool ExynosDisplay::comparePreferedLayers() {
    return false;
}

int ExynosDisplay::getId() {
    return mDisplayId;
}

void ExynosDisplay::initDisplay() {
    mClientCompositionInfo.initializeInfos(this);
    mClientCompositionInfo.mEnableSkipStatic = true;
    mClientCompositionInfo.mSkipStaticInitFlag = false;
    mClientCompositionInfo.mSkipFlag = false;
    memset(&mClientCompositionInfo.mSkipSrcInfo, 0x0, sizeof(mClientCompositionInfo.mSkipSrcInfo));
    for (int i = 0; i < NUM_SKIP_STATIC_LAYER; i++) {
        mClientCompositionInfo.mSkipSrcInfo.srcInfo[i].acquireFenceFd = -1;
        mClientCompositionInfo.mSkipSrcInfo.srcInfo[i].releaseFenceFd = -1;
        mClientCompositionInfo.mSkipSrcInfo.dstInfo[i].acquireFenceFd = -1;
        mClientCompositionInfo.mSkipSrcInfo.dstInfo[i].releaseFenceFd = -1;
    }
    memset(&mClientCompositionInfo.mLastWinConfigData, 0x0, sizeof(mClientCompositionInfo.mLastWinConfigData));
    mClientCompositionInfo.mLastWinConfigData.acq_fence = -1;
    mClientCompositionInfo.mLastWinConfigData.rel_fence = -1;

    mExynosCompositionInfo.initializeInfos(this);
    mExynosCompositionInfo.mEnableSkipStatic = false;
    mExynosCompositionInfo.mSkipStaticInitFlag = false;
    mExynosCompositionInfo.mSkipFlag = false;
    memset(&mExynosCompositionInfo.mSkipSrcInfo, 0x0, sizeof(mExynosCompositionInfo.mSkipSrcInfo));
    for (int i = 0; i < NUM_SKIP_STATIC_LAYER; i++) {
        mExynosCompositionInfo.mSkipSrcInfo.srcInfo[i].acquireFenceFd = -1;
        mExynosCompositionInfo.mSkipSrcInfo.srcInfo[i].releaseFenceFd = -1;
        mExynosCompositionInfo.mSkipSrcInfo.dstInfo[i].acquireFenceFd = -1;
        mExynosCompositionInfo.mSkipSrcInfo.dstInfo[i].releaseFenceFd = -1;
    }

    memset(&mExynosCompositionInfo.mLastWinConfigData, 0x0, sizeof(mExynosCompositionInfo.mLastWinConfigData));
    mExynosCompositionInfo.mLastWinConfigData.acq_fence = -1;
    mExynosCompositionInfo.mLastWinConfigData.rel_fence = -1;

    mGeometryChanged = 0x0;
    mRenderingState = RENDERING_STATE_NONE;
    mDisplayBW = 0;
    mDynamicReCompMode = NO_MODE_SWITCH;
    mCursorIndex = -1;

    mDpuData.initData();
    mLastDpuData.initData();

    if (mDisplayControl.earlyStartMPP == true) {
        for (size_t i = 0; i < mLayers.size(); i++) {
            exynos_image outImage;
            ExynosMPP* m2mMPP = mLayers[i]->mM2mMPP;

            /* Close acquire fence of dst buffer of last frame */
            if ((mLayers[i]->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) &&
                (m2mMPP != NULL) &&
                (m2mMPP->mAssignedDisplay == this) &&
                (m2mMPP->getDstImageInfo(&outImage) == NO_ERROR)) {
                if (m2mMPP->mPhysicalType == MPP_MSC) {
                    fence_close(outImage.acquireFenceFd, this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_MSC);
                } else if (m2mMPP->mPhysicalType == MPP_G2D) {
                    fence_close(outImage.acquireFenceFd, this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D);
                } else {
                    DISPLAY_LOGE("[%zu] layer has invalid mppType(%d)", i, m2mMPP->mPhysicalType);
                    fence_close(outImage.acquireFenceFd, this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_ALL);
                }
                m2mMPP->resetDstAcquireFence();
            }
        }
    }
}

/**
 * @param outLayer
 * @return int32_t
 */
int32_t ExynosDisplay::destroyLayer(hwc2_layer_t outLayer) {

    Mutex::Autolock lock(mDRMutex);

    if ((ExynosLayer*)outLayer == NULL)
        return HWC2_ERROR_BAD_LAYER;

    mLayers.remove((ExynosLayer*)outLayer);

    delete (ExynosLayer*)outLayer;
    setGeometryChanged(GEOMETRY_DISPLAY_LAYER_REMOVED);

    if (mPlugState == false) {
        DISPLAY_LOGI("%s : destroyLayer is done. But display is already disconnected",
                __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return HWC2_ERROR_NONE;
}

/**
 * @return void
 */
void ExynosDisplay::destroyLayers() {
    while (!mLayers.isEmpty()) {
        ExynosLayer *layer = mLayers[0];
        if (layer != NULL) {
            mLayers.remove(layer);
            delete layer;
        }
    }
}

/**
 * @param index
 * @return ExynosLayer
 */
ExynosLayer *ExynosDisplay::getLayer(uint32_t index) {

    if (mLayers.size() <= index) {
        HWC_LOGE(this, "HWC2 : %s : size(%zu), index(%d), wrong layer request!",
                __func__, mLayers.size(), index);
        return NULL;
    }
    if(mLayers[index] != NULL) {
        return mLayers[index];
    }
    else {
        HWC_LOGE(this, "HWC2 : %s : %d, wrong layer request!", __func__, __LINE__);
        return NULL;
    }
}

ExynosLayer *ExynosDisplay::checkLayer(hwc2_layer_t addr) {
    if (!mLayers.isEmpty()) {
        ExynosLayer *temp = (ExynosLayer *)addr;
        for (size_t i = 0; i < mLayers.size(); i++) {
            if (mLayers[i] == temp)
                return mLayers[i];
        }
    } else {
        ALOGE("HWC2: mLayers is empty");
        return NULL;
    }

    ALOGE("HWC2 : %s : %d, wrong layer request!", __func__, __LINE__);
    return NULL;
}

/**
 * @return void
 */
void ExynosDisplay::doPreProcessing() {
    /* Low persistence setting */
    int ret = 0;
    uint32_t selfRefresh = 0;
    unsigned int skipProcessing = 1;
    bool hasSingleBuffer = false;
    mBlendingNoneIndex = -1;
    bool skipStaticLayers = true;

    for (size_t i=0; i < mLayers.size(); i++) {
        private_handle_t *handle = mLayers[i]->mLayerBuffer;
        /* TODO: This should be checked **/
        if ((handle != NULL) &&
#ifdef GRALLOC_VERSION1
            (handle->consumer_usage & GRALLOC1_CONSUMER_USAGE_DAYDREAM_SINGLE_BUFFER_MODE))
#else
            (handle->flags & GRALLOC_USAGE_DAYDREAM_SINGLE_BUFFER_MODE))
#endif
        {
            hasSingleBuffer = true;
        }
        /* Prepare to Source copy layer blending exception */
        if ((i != 0) && (mLayers[i]->mBlending == HWC2_BLEND_MODE_NONE)) {
            if (handle == NULL)
                mBlendingNoneIndex = i;
            else if ((mLayers[i]->mOverlayPriority < ePriorityHigh) &&
                     formatHasAlphaChannel(handle->format))
                mBlendingNoneIndex = i;
        }
        if (mLayers[i]->mCompositionType == HWC2_COMPOSITION_CLIENT)
            skipStaticLayers = false;

        if (mLayers[i]->doPreProcess() < 0) {
            DISPLAY_LOGE("%s:: layer.doPreProcess() error, layer %zu", __func__, i);
        }

        exynos_image srcImg;
        exynos_image dstImg;
        mLayers[i]->setSrcExynosImage(&srcImg);
        mLayers[i]->setDstExynosImage(&dstImg);
        mLayers[i]->setExynosImage(srcImg, dstImg);
    }

    // Re-align layer priority for max overlay resources
    uint32_t numMaxPriorityLayers = 0;
    for (int i = (mLayers.size()-1); i >= 0; i--) {
        ExynosLayer *layer = mLayers[i];
        HDEBUGLOGD(eDebugResourceManager, "Priority align: i:%d, layer priority:%d, Max:%d, mNumMaxPriorityAllowed:%d", i,
                layer->mOverlayPriority, numMaxPriorityLayers, mNumMaxPriorityAllowed);
        if (layer->mOverlayPriority == ePriorityMax) {
            if (numMaxPriorityLayers >= mNumMaxPriorityAllowed) {
                layer->mOverlayPriority = ePriorityHigh;
                setGeometryChanged(GEOMETRY_LAYER_PRIORITY_CHANGED);
            }
            numMaxPriorityLayers++;
        }
    }

    /*
     * Disable skip static layer feature if there is the layer that's
     * mCompositionType  is HWC2_COMPOSITION_CLIENT
     * HWC should not change compositionType if it is HWC2_COMPOSITION_CLIENT
     */
    if (mType != HWC_DISPLAY_VIRTUAL)
        mClientCompositionInfo.mEnableSkipStatic = skipStaticLayers;

    if (mHasSingleBuffer != hasSingleBuffer) {
        if (hasSingleBuffer) {
            selfRefresh = 1;
            skipProcessing = 0;
        } else {
            selfRefresh = 0;
            skipProcessing = 1;
        }
        if ((ret = mDisplayInterface->disableSelfRefresh(selfRefresh)) < 0)
            DISPLAY_LOGE("ioctl S3CFB_LOW_PERSISTENCE failed: %s ret(%d)", strerror(errno), ret);
        mHasSingleBuffer = hasSingleBuffer;
        mDevice->setHWCControl(mDisplayId, HWC_CTL_SKIP_M2M_PROCESSING, skipProcessing);
        mDevice->setHWCControl(mDisplayId, HWC_CTL_SKIP_STATIC, skipProcessing);
        setGeometryChanged(GEOMETRY_DISPLAY_SINGLEBUF_CHANGED);
    }

    if ((exynosHWCControl.displayMode < DISPLAY_MODE_NUM) &&
        (mDevice->mDisplayMode != exynosHWCControl.displayMode))
        setGeometryChanged(GEOMETRY_DEVICE_DISP_MODE_CHAGED);

    if ((ret = mResourceManager->checkExceptionScenario(this)) != NO_ERROR)
        DISPLAY_LOGE("checkExceptionScenario error ret(%d)", ret);

    if (exynosHWCControl.skipResourceAssign == 0) {
        /* Set any flag to mGeometryChanged */
        setGeometryChanged(GEOMETRY_DEVICE_SCENARIO_CHANGED);
    }
#ifndef HWC_SKIP_VALIDATE
    if (mDevice->checkAdditionalConnection()) {
        /* Set any flag to mGeometryChanged */
        mDevice->mGeometryChanged = 0x10;
    }
#endif

    return;
}

/**
 * @return int
 */
int ExynosDisplay::checkLayerFps() {
    mLowFpsLayerInfo.initializeInfos();

    if (mDisplayControl.handleLowFpsLayers == false)
        return NO_ERROR;

    for (size_t i=0; i < mLayers.size(); i++) {
         if ((mLayers[i]->mOverlayPriority < ePriorityHigh) &&
             (mLayers[i]->getFps() < LOW_FPS_THRESHOLD)) {
             mLowFpsLayerInfo.addLowFpsLayer(i);
         } else {
             if (mLowFpsLayerInfo.mHasLowFpsLayer == true)
                 break;
             else
                 continue;
         }
    }
    /* There is only one low fps layer, Overlay is better in this case */
    if ((mLowFpsLayerInfo.mHasLowFpsLayer == true) &&
        (mLowFpsLayerInfo.mFirstIndex == mLowFpsLayerInfo.mLastIndex))
        mLowFpsLayerInfo.initializeInfos();

    return NO_ERROR;
}

/**
 * @return int
 */
int ExynosDisplay::checkDynamicReCompMode() {
    unsigned int updateFps = 0;
    unsigned int lcd_size = this->mXres * this->mYres;
    uint64_t TimeStampDiff;
    uint64_t w = 0, h = 0, incomingPixels = 0;
    uint64_t maxFps = 0, layerFps = 0;

    Mutex::Autolock lock(mDRMutex);

    if (!exynosHWCControl.useDynamicRecomp) {
        mLastModeSwitchTimeStamp = 0;
        mDynamicReCompMode = NO_MODE_SWITCH;
        return 0;
    }

    /* initialize the Timestamps */
    if (!mLastModeSwitchTimeStamp) {
        mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
        mDynamicReCompMode = NO_MODE_SWITCH;
        return 0;
    }

    /* If video layer is there, skip the mode switch */
    for (size_t i = 0; i < mLayers.size(); i++) {
        if ((mLayers[i]->mLayerBuffer) && (isFormatYUV(mLayers[i]->mLayerBuffer->format))) {
            if (mDynamicReCompMode != DEVICE_2_CLIENT) {
                return 0;
            } else {
                mDynamicReCompMode = CLIENT_2_DEVICE;
                mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
                DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] GLES_2_HWC by video layer");
                this->setGeometryChanged(GEOMETRY_DISPLAY_DYNAMIC_RECOMPOSITION);
                return CLIENT_2_DEVICE;
            }
        }
    }

    for (size_t i = 0; i < mLayers.size(); i++) {
        w = WIDTH(mLayers[i]->mPreprocessedInfo.displayFrame);
        h = HEIGHT(mLayers[i]->mPreprocessedInfo.displayFrame);
        incomingPixels += w * h;
    }

    /* Mode Switch is not required if total pixels are not more than the threshold */
    if (incomingPixels <= lcd_size) {
        if (mDynamicReCompMode != DEVICE_2_CLIENT) {
            return 0;
        } else {
            mDynamicReCompMode = CLIENT_2_DEVICE;
            mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
            DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] GLES_2_HWC by BW check");
            this->setGeometryChanged(GEOMETRY_DISPLAY_DYNAMIC_RECOMPOSITION);
            return CLIENT_2_DEVICE;
        }
    }

    /*
     * There will be at least one composition call per one minute (because of time update)
     * To minimize the analysis overhead, just analyze it once in a second
     */
    TimeStampDiff = systemTime(SYSTEM_TIME_MONOTONIC) - mLastModeSwitchTimeStamp;

    /*
     * previous CompModeSwitch was CLIENT_2_DEVICE: check fps every 250ms from mLastModeSwitchTimeStamp
     * previous CompModeSwitch was DEVICE_2_CLIENT: check immediately
     */
    if ((mDynamicReCompMode != DEVICE_2_CLIENT) && (TimeStampDiff < (VSYNC_INTERVAL * 15)))
        return 0;

    mLastModeSwitchTimeStamp = mLastUpdateTimeStamp;
    if ((mUpdateEventCnt != 1) &&
        (mDynamicReCompMode == DEVICE_2_CLIENT)) {
        DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] first frame after DEVICE_2_CLIENT");
        updateFps = HWC_FPS_TH;
    } else {
        for (uint32_t i = 0; i < mLayers.size(); i++) {
            layerFps = mLayers[i]->getFps();
            if (maxFps < layerFps)
                maxFps = layerFps;
        }
        updateFps = maxFps;
    }

    /*
     * FPS estimation.
     * If FPS is lower than HWC_FPS_TH, try to switch the mode to GLES
     */
    if (updateFps < HWC_FPS_TH) {
        if (mDynamicReCompMode != DEVICE_2_CLIENT) {
            mDynamicReCompMode = DEVICE_2_CLIENT;
            DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] DEVICE_2_CLIENT by low FPS(%d)", updateFps);
            this->setGeometryChanged(GEOMETRY_DISPLAY_DYNAMIC_RECOMPOSITION);
            return DEVICE_2_CLIENT;
        } else {
            return 0;
        }
    } else {
        if (mDynamicReCompMode == DEVICE_2_CLIENT) {
            mDynamicReCompMode = CLIENT_2_DEVICE;
            DISPLAY_LOGD(eDebugDynamicRecomp, "[DYNAMIC_RECOMP] CLIENT_2_HWC by high FPS(%d)", updateFps);
            this->setGeometryChanged(GEOMETRY_DISPLAY_DYNAMIC_RECOMPOSITION);
            return CLIENT_2_DEVICE;
        } else {
            return 0;
        }
    }

    return 0;
}

/**
 * @return int
 */
int ExynosDisplay::handleDynamicReCompMode() {
    return 0;
}

/**
 * @param changedBit
 * @return int
 */
void ExynosDisplay::setGeometryChanged(uint64_t changedBit) {
    mGeometryChanged |= changedBit;
    mDevice->setGeometryChanged(changedBit);
}

void ExynosDisplay::clearGeometryChanged()
{
    mGeometryChanged = 0;
    for (size_t i=0; i < mLayers.size(); i++) {
        mLayers[i]->clearGeometryChanged();
    }
}

int ExynosDisplay::handleStaticLayers(ExynosCompositionInfo& compositionInfo)
{
    if (compositionInfo.mType != COMPOSITION_CLIENT)
        return -EINVAL;

    if (mType == HWC_DISPLAY_VIRTUAL)
        return NO_ERROR;

    if (compositionInfo.mHasCompositionLayer == false) {
        DISPLAY_LOGD(eDebugSkipStaicLayer, "there is no client composition");
        return NO_ERROR;
    }
    if ((compositionInfo.mWindowIndex < 0) ||
        (compositionInfo.mWindowIndex >= (int32_t)mDpuData.configs.size()))
    {
        DISPLAY_LOGE("invalid mWindowIndex(%d)", compositionInfo.mWindowIndex);
        return -EINVAL;
    }

    exynos_win_config_data &config = mDpuData.configs[compositionInfo.mWindowIndex];

    /* Store configuration of client target configuration */
    if (compositionInfo.mSkipFlag == false) {
        memcpy(&compositionInfo.mLastWinConfigData, &config,
            sizeof(compositionInfo.mLastWinConfigData));
        DISPLAY_LOGD(eDebugSkipStaicLayer, "config[%d] is stored",
                compositionInfo.mWindowIndex);
    } else {
        for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
            if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT) &&
                (mLayers[i]->mAcquireFence >= 0))
                fence_close(mLayers[i]->mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);
            mLayers[i]->mAcquireFence = -1;
            mLayers[i]->mReleaseFence = -1;
        }

        if (compositionInfo.mTargetBuffer == NULL) {
            fence_close(config.acq_fence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);

            memcpy(&config, &compositionInfo.mLastWinConfigData, sizeof(compositionInfo.mLastWinConfigData));
            /* Assigned otfMPP for client target can be changed */
            config.assignedMPP = compositionInfo.mOtfMPP;
            /* acq_fence was closed by DPU driver in the previous frame */
            config.acq_fence = -1;
        } else {
            /* Check target buffer is same with previous frame */
            if ((config.fd_idma[0] != compositionInfo.mLastWinConfigData.fd_idma[0]) ||
                (config.fd_idma[1] != compositionInfo.mLastWinConfigData.fd_idma[1]) ||
                (config.fd_idma[2] != compositionInfo.mLastWinConfigData.fd_idma[2])) {
                DISPLAY_LOGE("Current config [%d][%d, %d, %d]",
                        compositionInfo.mWindowIndex,
                        config.fd_idma[0], config.fd_idma[1], config.fd_idma[2]);
                DISPLAY_LOGE("=============================  dump last win configs  ===================================");
                for (size_t i = 0; i <= mLastDpuData.configs.size(); i++) {
                    android::String8 result;
                    result.appendFormat("config[%zu]\n", i);
                    dumpConfig(result, mLastDpuData.configs[i]);
                    DISPLAY_LOGE("%s", result.string());
                }
                DISPLAY_LOGE("compositionInfo.mLastWinConfigData config [%d, %d, %d]",
                        compositionInfo.mLastWinConfigData.fd_idma[0],
                        compositionInfo.mLastWinConfigData.fd_idma[1],
                        compositionInfo.mLastWinConfigData.fd_idma[2]);
                return -EINVAL;
            }
        }

        DISPLAY_LOGD(eDebugSkipStaicLayer, "skipStaticLayer config[%d]", compositionInfo.mWindowIndex);
        dumpConfig(config);
    }

    return NO_ERROR;
}

bool ExynosDisplay::skipStaticLayerChanged(ExynosCompositionInfo& compositionInfo)
{
    if ((int)compositionInfo.mSkipSrcInfo.srcNum !=
            (compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1)) {
        DISPLAY_LOGD(eDebugSkipStaicLayer, "Client composition number is changed (%d -> %d)",
                compositionInfo.mSkipSrcInfo.srcNum,
                compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1);
        return true;
    }

    bool isChanged = false;
    for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
        ExynosLayer *layer = mLayers[i];
        size_t index = i - compositionInfo.mFirstIndex;
        if ((layer->mLayerBuffer == NULL) ||
            (compositionInfo.mSkipSrcInfo.srcInfo[index].bufferHandle != layer->mLayerBuffer))
        {
            isChanged = true;
            DISPLAY_LOGD(eDebugSkipStaicLayer, "layer[%zu] handle is changed"\
                    " handle(%p -> %p), layerFlag(0x%8x)",
                    i, compositionInfo.mSkipSrcInfo.srcInfo[index].bufferHandle,
                    layer->mLayerBuffer, layer->mLayerFlag);
            break;
        } else if ((compositionInfo.mSkipSrcInfo.srcInfo[index].x != layer->mSrcImg.x) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].y != layer->mSrcImg.y) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].w != layer->mSrcImg.w) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].h != layer->mSrcImg.h) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].dataSpace != layer->mSrcImg.dataSpace) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].blending != layer->mSrcImg.blending) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].transform != layer->mSrcImg.transform) ||
                (compositionInfo.mSkipSrcInfo.srcInfo[index].planeAlpha != layer->mSrcImg.planeAlpha))
        {
            isChanged = true;
            DISPLAY_LOGD(eDebugSkipStaicLayer, "layer[%zu] source info is changed, "\
                    "x(%d->%d), y(%d->%d), w(%d->%d), h(%d->%d), dataSpace(%d->%d), "\
                    "blending(%d->%d), transform(%d->%d), planeAlpha(%3.1f->%3.1f)", i,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].x, layer->mSrcImg.x,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].y, layer->mSrcImg.y,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].w,  layer->mSrcImg.w,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].h,  layer->mSrcImg.h,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].dataSpace, layer->mSrcImg.dataSpace,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].blending, layer->mSrcImg.blending,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].transform, layer->mSrcImg.transform,
                    compositionInfo.mSkipSrcInfo.srcInfo[index].planeAlpha, layer->mSrcImg.planeAlpha);
            break;
        } else if ((compositionInfo.mSkipSrcInfo.dstInfo[index].x != layer->mDstImg.x) ||
                (compositionInfo.mSkipSrcInfo.dstInfo[index].y != layer->mDstImg.y) ||
                (compositionInfo.mSkipSrcInfo.dstInfo[index].w != layer->mDstImg.w) ||
                (compositionInfo.mSkipSrcInfo.dstInfo[index].h != layer->mDstImg.h))
        {
            isChanged = true;
            DISPLAY_LOGD(eDebugSkipStaicLayer, "layer[%zu] dst info is changed, "\
                    "x(%d->%d), y(%d->%d), w(%d->%d), h(%d->%d)", i,
                    compositionInfo.mSkipSrcInfo.dstInfo[index].x, layer->mDstImg.x,
                    compositionInfo.mSkipSrcInfo.dstInfo[index].y, layer->mDstImg.y,
                    compositionInfo.mSkipSrcInfo.dstInfo[index].w, layer->mDstImg.w,
                    compositionInfo.mSkipSrcInfo.dstInfo[index].h, layer->mDstImg.h);
            break;
        }
    }
    return isChanged;
}

/**
 * @param compositionType
 * @return int
 */
int ExynosDisplay::skipStaticLayers(ExynosCompositionInfo& compositionInfo)
{
    compositionInfo.mSkipFlag = false;

    if (compositionInfo.mType != COMPOSITION_CLIENT)
        return -EINVAL;

    if ((exynosHWCControl.skipStaticLayers == 0) ||
        (compositionInfo.mEnableSkipStatic == false)) {
        DISPLAY_LOGD(eDebugSkipStaicLayer, "skipStaticLayers(%d), mEnableSkipStatic(%d)",
                exynosHWCControl.skipStaticLayers, compositionInfo.mEnableSkipStatic);
        compositionInfo.mSkipStaticInitFlag = false;
        return NO_ERROR;
    }

    if ((compositionInfo.mHasCompositionLayer == false) ||
        (compositionInfo.mFirstIndex < 0) ||
        (compositionInfo.mLastIndex < 0) ||
        ((compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1) > NUM_SKIP_STATIC_LAYER)) {
        DISPLAY_LOGD(eDebugSkipStaicLayer, "mHasCompositionLayer(%d), mFirstIndex(%d), mLastIndex(%d)",
                compositionInfo.mHasCompositionLayer,
                compositionInfo.mFirstIndex, compositionInfo.mLastIndex);
        compositionInfo.mSkipStaticInitFlag = false;
        return NO_ERROR;
    }

    if (compositionInfo.mSkipStaticInitFlag) {
        bool isChanged = skipStaticLayerChanged(compositionInfo);
        if (isChanged == true) {
            compositionInfo.mSkipStaticInitFlag = false;
            return NO_ERROR;
        }

        for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
            ExynosLayer *layer = mLayers[i];
            if (layer->mValidateCompositionType == COMPOSITION_CLIENT) {
                layer->mOverlayInfo |= eSkipStaticLayer;
            } else {
                compositionInfo.mSkipStaticInitFlag = false;
                if (layer->mOverlayPriority < ePriorityHigh) {
                    DISPLAY_LOGE("[%zu] Invalid layer type: %d",
                            i, layer->mValidateCompositionType);
                    return -EINVAL;
                } else {
                    return NO_ERROR;
                }
            }
        }

        compositionInfo.mSkipFlag = true;
        DISPLAY_LOGD(eDebugSkipStaicLayer, "SkipStaicLayer is enabled");
        return NO_ERROR;
    }

    compositionInfo.mSkipStaticInitFlag = true;
    memset(&compositionInfo.mSkipSrcInfo, 0, sizeof(compositionInfo.mSkipSrcInfo));
    for (int i = 0; i < NUM_SKIP_STATIC_LAYER; i++) {
        compositionInfo.mSkipSrcInfo.srcInfo[i].acquireFenceFd = -1;
        compositionInfo.mSkipSrcInfo.srcInfo[i].releaseFenceFd = -1;
        compositionInfo.mSkipSrcInfo.dstInfo[i].acquireFenceFd = -1;
        compositionInfo.mSkipSrcInfo.dstInfo[i].releaseFenceFd = -1;
    }

    for (size_t i = (size_t)compositionInfo.mFirstIndex; i <= (size_t)compositionInfo.mLastIndex; i++) {
        ExynosLayer *layer = mLayers[i];
        size_t index = i - compositionInfo.mFirstIndex;
        compositionInfo.mSkipSrcInfo.srcInfo[index] = layer->mSrcImg;
        compositionInfo.mSkipSrcInfo.dstInfo[index] = layer->mDstImg;
        DISPLAY_LOGD(eDebugSkipStaicLayer, "mSkipSrcInfo.srcInfo[%zu] is initialized, %p",
                index, layer->mSrcImg.bufferHandle);
    }
    compositionInfo.mSkipSrcInfo.srcNum = (compositionInfo.mLastIndex - compositionInfo.mFirstIndex + 1);
    return NO_ERROR;
}

/**
 * @return int
 */
int ExynosDisplay::doPostProcessing() {

    for (size_t i=0; i < mLayers.size(); i++) {
        /* Layer handle back-up */
        mLayers[i]->mLastLayerBuffer = mLayers[i]->mLayerBuffer;
    }
    clearGeometryChanged();

    return 0;
}

bool ExynosDisplay::validateExynosCompositionLayer()
{
    bool isValid = true;
    ExynosMPP *m2mMpp = mExynosCompositionInfo.mM2mMPP;

    int sourceSize = (int)m2mMpp->mAssignedSources.size();
    if ((mExynosCompositionInfo.mFirstIndex >= 0) &&
        (mExynosCompositionInfo.mLastIndex >= 0)) {
        sourceSize = mExynosCompositionInfo.mLastIndex - mExynosCompositionInfo.mFirstIndex + 1;

        if (!mUseDpu && mClientCompositionInfo.mHasCompositionLayer)
            sourceSize++;
    }

    if (m2mMpp->mAssignedSources.size() == 0) {
        DISPLAY_LOGE("No source images");
        isValid = false;
    } else if (mUseDpu && (((mExynosCompositionInfo.mFirstIndex < 0) ||
               (mExynosCompositionInfo.mLastIndex < 0)) ||
               (sourceSize != (int)m2mMpp->mAssignedSources.size()))) {
        DISPLAY_LOGE("Invalid index (%d, %d), size(%zu), sourceSize(%d)",
                mExynosCompositionInfo.mFirstIndex,
                mExynosCompositionInfo.mLastIndex,
                m2mMpp->mAssignedSources.size(),
                sourceSize);
        isValid = false;
    }
    if (isValid == false) {
        for (int32_t i = mExynosCompositionInfo.mFirstIndex; i <= mExynosCompositionInfo.mLastIndex; i++) {
            /* break when only framebuffer target is assigned on ExynosCompositor */
            if (i == -1)
                break;

            if (mLayers[i]->mAcquireFence >= 0)
                fence_close(mLayers[i]->mAcquireFence, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_ALL);
            mLayers[i]->mAcquireFence = -1;
        }
        mExynosCompositionInfo.mM2mMPP->requestHWStateChange(MPP_HW_STATE_IDLE);
    }
    return isValid;
}

/**
 * @return int
 */
int ExynosDisplay::doExynosComposition() {
    int ret = NO_ERROR;
    exynos_image src_img;
    exynos_image dst_img;

    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if (mExynosCompositionInfo.mM2mMPP == NULL) {
            DISPLAY_LOGE("mExynosCompositionInfo.mM2mMPP is NULL");
            return -EINVAL;
        }
        mExynosCompositionInfo.mM2mMPP->requestHWStateChange(MPP_HW_STATE_RUNNING);
        /* mAcquireFence is updated, Update image info */
        for (int32_t i = mExynosCompositionInfo.mFirstIndex; i <= mExynosCompositionInfo.mLastIndex; i++) {
            /* break when only framebuffer target is assigned on ExynosCompositor */
            if (i == -1)
                break;

            struct exynos_image srcImg, dstImg;
            mLayers[i]->setSrcExynosImage(&srcImg);
            dumpExynosImage(eDebugFence, srcImg);
            mLayers[i]->setDstExynosImage(&dstImg);
            mLayers[i]->setExynosImage(srcImg, dstImg);
        }

        /* For debugging */
        if (validateExynosCompositionLayer() == false) {
            DISPLAY_LOGE("mExynosCompositionInfo is not valid");
            return -EINVAL;
        }

        if ((ret = mExynosCompositionInfo.mM2mMPP->doPostProcessing(mExynosCompositionInfo.mSrcImg,
                mExynosCompositionInfo.mDstImg)) != NO_ERROR) {
            DISPLAY_LOGE("exynosComposition doPostProcessing fail ret(%d)", ret);
            return ret;
        }

        for (int32_t i = mExynosCompositionInfo.mFirstIndex; i <= mExynosCompositionInfo.mLastIndex; i++) {
            /* break when only framebuffer target is assigned on ExynosCompositor */
            if (i == -1)
                break;
            /* This should be closed by resource lib (libmpp or libacryl) */
            mLayers[i]->mAcquireFence = -1;
        }

        exynos_image outImage;
        if ((ret = mExynosCompositionInfo.mM2mMPP->getDstImageInfo(&outImage)) != NO_ERROR) {
            DISPLAY_LOGE("exynosComposition getDstImageInfo fail ret(%d)", ret);
            return ret;
        }

        android_dataspace dataspace = HAL_DATASPACE_UNKNOWN;
        if (mColorMode != HAL_COLOR_MODE_NATIVE)
            dataspace = colorModeToDataspace(mColorMode);
        mExynosCompositionInfo.setTargetBuffer(this, outImage.bufferHandle,
                outImage.acquireFenceFd, dataspace);
        /*
         * buffer handle, dataspace can be changed by setTargetBuffer()
         * ExynosImage should be set again according to changed handle and dataspace
         */
        setCompositionTargetExynosImage(COMPOSITION_EXYNOS, &src_img, &dst_img);
        mExynosCompositionInfo.setExynosImage(src_img, dst_img);

        DISPLAY_LOGD(eDebugFence, "mExynosCompositionInfo acquireFencefd(%d)",
                mExynosCompositionInfo.mAcquireFence);
        // Test..
        // setFenceInfo(mExynosCompositionInfo.mAcquireFence, this, "G2D_DST_ACQ", FENCE_FROM);

        if ((ret =  mExynosCompositionInfo.mM2mMPP->resetDstAcquireFence()) != NO_ERROR)
        {
            DISPLAY_LOGE("exynosComposition resetDstAcquireFence fail ret(%d)", ret);
            return ret;
        }
    }

    return ret;
}

bool ExynosDisplay::getHDRException(ExynosLayer* __unused layer)
{
    return false;
}

bool ExynosDisplay::is2StepBlendingRequired(exynos_image &src, private_handle_t *outbuf)
{
    return false;
}

int32_t ExynosDisplay::configureHandle(ExynosLayer &layer, int fence_fd, exynos_win_config_data &cfg)
{
    /* TODO : this is hardcoded */
    int32_t ret = NO_ERROR;
    private_handle_t *handle = NULL;
    int32_t blending = 0x0100;
    int32_t planeAlpha = 0;
    uint32_t x = 0, y = 0;
    uint32_t w = WIDTH(layer.mPreprocessedInfo.displayFrame);
    uint32_t h = HEIGHT(layer.mPreprocessedInfo.displayFrame);
    ExynosMPP* otfMPP = NULL;
    ExynosMPP* m2mMPP = NULL;
    unsigned int luminanceMin = 0;
    unsigned int luminanceMax = 0;

    blending = layer.mBlending;
    planeAlpha = (int)(255 * layer.mPlaneAlpha);
    otfMPP = layer.mOtfMPP;
    m2mMPP = layer.mM2mMPP;

    cfg.compression = layer.mCompressed;
    if (layer.mCompressed) {
        cfg.comp_src = DPP_COMP_SRC_GPU;
    }
    if (otfMPP == NULL) {
        HWC_LOGE(this, "%s:: otfMPP is NULL", __func__);
        return -EINVAL;
    }
    if (m2mMPP != NULL)
        handle = m2mMPP->mDstImgs[m2mMPP->mCurrentDstBuf].bufferHandle;
    else
        handle = layer.mLayerBuffer;

    if ((!layer.mIsDimLayer) && handle == NULL) {
        HWC_LOGE(this, "%s:: invalid handle", __func__);
        return -EINVAL;
    }

    if (layer.mPreprocessedInfo.displayFrame.left < 0) {
        unsigned int crop = -layer.mPreprocessedInfo.displayFrame.left;
        DISPLAY_LOGD(eDebugWinConfig, "layer off left side of screen; cropping %u pixels from left edge",
                crop);
        x = 0;
        w -= crop;
    } else {
        x = layer.mPreprocessedInfo.displayFrame.left;
    }

    if (layer.mPreprocessedInfo.displayFrame.right > (int)mXres) {
        unsigned int crop = layer.mPreprocessedInfo.displayFrame.right - this->mXres;
        DISPLAY_LOGD(eDebugWinConfig, "layer off right side of screen; cropping %u pixels from right edge",
                crop);
        w -= crop;
    }

    if (layer.mPreprocessedInfo.displayFrame.top < 0) {
        unsigned int crop = -layer.mPreprocessedInfo.displayFrame.top;
        DISPLAY_LOGD(eDebugWinConfig, "layer off top side of screen; cropping %u pixels from top edge",
                crop);
        y = 0;
        h -= crop;
    } else {
        y = layer.mPreprocessedInfo.displayFrame.top;
    }

    if (layer.mPreprocessedInfo.displayFrame.bottom > (int)mYres) {
        int crop = layer.mPreprocessedInfo.displayFrame.bottom - this->mYres;
        DISPLAY_LOGD(eDebugWinConfig, "layer off bottom side of screen; cropping %u pixels from bottom edge",
                crop);
        h -= crop;
    }

    if ((layer.mExynosCompositionType == HWC2_COMPOSITION_DEVICE) &&
        (layer.mCompositionType == HWC2_COMPOSITION_CURSOR))
        cfg.state = cfg.WIN_STATE_CURSOR;
    else
        cfg.state = cfg.WIN_STATE_BUFFER;
    cfg.dst.x = x;
    cfg.dst.y = y;
    cfg.dst.w = w;
    cfg.dst.h = h;
    cfg.dst.f_w = mXres;
    cfg.dst.f_h = mYres;

    cfg.plane_alpha = 255;
    if ((planeAlpha >= 0) && (planeAlpha < 255)) {
        cfg.plane_alpha = planeAlpha;
    }
    cfg.blending = blending;
    cfg.assignedMPP = otfMPP;

    if (layer.mIsDimLayer && handle == NULL) {
        cfg.state = cfg.WIN_STATE_COLOR;
        hwc_color_t color = layer.mColor;
        cfg.color = (color.r << 16) | (color.g << 8) | color.b;
        if (!((planeAlpha >= 0) && (planeAlpha <= 255)))
            cfg.plane_alpha = 0;
        DISPLAY_LOGD(eDebugWinConfig, "HWC2: DIM layer is enabled, alpha : %d", cfg.plane_alpha);
        return ret;
    }

    if (!layer.mPreprocessedInfo.mUsePrivateFormat)
        cfg.format = handle->format;
    else
        cfg.format = layer.mPreprocessedInfo.mPrivateFormat;

    cfg.fd_idma[0] = handle->fd;
    cfg.fd_idma[1] = handle->fd1;
    cfg.fd_idma[2] = handle->fd2;
    cfg.protection = (getDrmMode(handle) == SECURE_DRM) ? 1 : 0;

    exynos_image src_img = layer.mSrcImg;

    if (m2mMPP != NULL)
    {
        DISPLAY_LOGD(eDebugWinConfig, "\tUse m2mMPP, bufIndex: %d", m2mMPP->mCurrentDstBuf);
        dumpExynosImage(eDebugWinConfig, m2mMPP->mAssignedSources[0]->mMidImg);
        exynos_image mpp_dst_img;
        if (m2mMPP->getDstImageInfo(&mpp_dst_img) == NO_ERROR) {
            dumpExynosImage(eDebugWinConfig, mpp_dst_img);
            cfg.src.f_w = mpp_dst_img.fullWidth;
            cfg.src.f_h = mpp_dst_img.fullHeight;
            cfg.src.x = mpp_dst_img.x;
            cfg.src.y = mpp_dst_img.y;
            cfg.src.w = mpp_dst_img.w;
            cfg.src.h = mpp_dst_img.h;
            cfg.format = mpp_dst_img.format;
            cfg.acq_fence =
                hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, mpp_dst_img.acquireFenceFd);

            if (m2mMPP->mPhysicalType == MPP_MSC) {
                setFenceName(cfg.acq_fence, FENCE_DPP_SRC_MSC);
            } else if (m2mMPP->mPhysicalType == MPP_G2D) {
                setFenceName(cfg.acq_fence, FENCE_DPP_SRC_G2D);
            } else {
                setFenceName(cfg.acq_fence, FENCE_UNDEFINED);
            }
            m2mMPP->resetDstAcquireFence();
        } else {
            HWC_LOGE(this, "%s:: Failed to get dst info of m2mMPP", __func__);
        }
        cfg.dataspace = mpp_dst_img.dataSpace;

        cfg.transform = 0;

        if (hasHdrInfo(layer.mMidImg)) {
            bool hdr_exception = getHDRException(&layer);
            if (layer.mBufferHasMetaParcel) {
                uint32_t parcelFdIndex = getBufferNumOfFormat(layer.mMidImg.format);
                if (parcelFdIndex > 0) {
                    if (layer.mLayerBuffer->flags & private_handle_t::PRIV_FLAGS_USES_2PRIVATE_DATA)
                        cfg.fd_idma[parcelFdIndex] = layer.mLayerBuffer->fd1;
                    else if (layer.mLayerBuffer->flags & private_handle_t::PRIV_FLAGS_USES_3PRIVATE_DATA)
                        cfg.fd_idma[parcelFdIndex] = layer.mLayerBuffer->fd2;
                }
            } else {
                uint32_t parcelFdIndex = getBufferNumOfFormat(layer.mMidImg.format);
                if (parcelFdIndex > 0)
                    cfg.fd_idma[parcelFdIndex] = layer.mMetaParcelFd;
            }

            if (!hdr_exception)
                cfg.hdr_enable = true;
            else
                cfg.hdr_enable = false;

            /*
             * Static info uses 0.0001nit unit for luminace
             * Display uses 1nit unit for max luminance
             * and uses 0.0001nit unit for min luminance
             * Conversion is required
             */
            luminanceMin = src_img.metaParcel.sHdrStaticInfo.sType1.mMinDisplayLuminance;
            luminanceMax = src_img.metaParcel.sHdrStaticInfo.sType1.mMaxDisplayLuminance/10000;
            DISPLAY_LOGD(eDebugMPP, "HWC2: DPP luminance min %d, max %d", luminanceMin, luminanceMax);
        } else {
            cfg.hdr_enable = true;
        }

        src_img = layer.mMidImg;
    } else {
        cfg.src.f_w = src_img.fullWidth;
        cfg.src.f_h = src_img.fullHeight;
        cfg.src.x = layer.mPreprocessedInfo.sourceCrop.left;
        cfg.src.y = layer.mPreprocessedInfo.sourceCrop.top;
        cfg.src.w = WIDTH(layer.mPreprocessedInfo.sourceCrop) - (cfg.src.x - (uint32_t)layer.mPreprocessedInfo.sourceCrop.left);
        cfg.src.h = HEIGHT(layer.mPreprocessedInfo.sourceCrop) - (cfg.src.y - (uint32_t)layer.mPreprocessedInfo.sourceCrop.top);
        cfg.acq_fence = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, fence_fd);
        setFenceName(cfg.acq_fence, FENCE_DPP_SRC_LAYER);

        cfg.dataspace = src_img.dataSpace;
        cfg.transform = src_img.transform;

        if (hasHdrInfo(src_img)) {
            bool hdr_exception = getHDRException(&layer);
            if (!hdr_exception)
                cfg.hdr_enable = true;
            else
                cfg.hdr_enable = false;

            if (layer.mBufferHasMetaParcel == false) {
                uint32_t parcelFdIndex = getBufferNumOfFormat(handle->format);
                if (parcelFdIndex > 0)
                    cfg.fd_idma[parcelFdIndex] = layer.mMetaParcelFd;
            }
            luminanceMin = src_img.metaParcel.sHdrStaticInfo.sType1.mMinDisplayLuminance;
            luminanceMax = src_img.metaParcel.sHdrStaticInfo.sType1.mMaxDisplayLuminance/10000;
            DISPLAY_LOGD(eDebugMPP, "HWC2: DPP luminance min %d, max %d", luminanceMin, luminanceMax);
        } else {
            cfg.hdr_enable = true;
        }
    }

    cfg.min_luminance = luminanceMin;
    cfg.max_luminance = luminanceMax;

    /* Adjust configuration */
    uint32_t srcMaxWidth, srcMaxHeight, srcWidthAlign, srcHeightAlign = 0;
    uint32_t srcXAlign, srcYAlign, srcMaxCropWidth, srcMaxCropHeight, srcCropWidthAlign, srcCropHeightAlign = 0;
    srcMaxWidth = otfMPP->getSrcMaxWidth(src_img);
    srcMaxHeight = otfMPP->getSrcMaxHeight(src_img);
    srcWidthAlign = otfMPP->getSrcWidthAlign(src_img);
    srcHeightAlign = otfMPP->getSrcHeightAlign(src_img);
    srcXAlign = otfMPP->getSrcXOffsetAlign(src_img);
    srcYAlign = otfMPP->getSrcYOffsetAlign(src_img);
    srcMaxCropWidth = otfMPP->getSrcMaxCropWidth(src_img);
    srcMaxCropHeight = otfMPP->getSrcMaxCropHeight(src_img);
    srcCropWidthAlign = otfMPP->getSrcCropWidthAlign(src_img);
    srcCropHeightAlign = otfMPP->getSrcCropHeightAlign(src_img);

    if (cfg.src.x < 0)
        cfg.src.x = 0;
    if (cfg.src.y < 0)
        cfg.src.y = 0;

    if (otfMPP != NULL) {
        if (cfg.src.f_w > srcMaxWidth)
            cfg.src.f_w = srcMaxWidth;
        if (cfg.src.f_h > srcMaxHeight)
            cfg.src.f_h = srcMaxHeight;
        cfg.src.f_w = pixel_align_down((unsigned int)cfg.src.f_w, srcWidthAlign);
        cfg.src.f_h = pixel_align_down((unsigned int)cfg.src.f_h, srcHeightAlign);

        cfg.src.x = pixel_align(cfg.src.x, srcXAlign);
        cfg.src.y = pixel_align(cfg.src.y, srcYAlign);
    }

    if (cfg.src.x + cfg.src.w > cfg.src.f_w)
        cfg.src.w = cfg.src.f_w - cfg.src.x;
    if (cfg.src.y + cfg.src.h > cfg.src.f_h)
        cfg.src.h = cfg.src.f_h - cfg.src.y;

    if (otfMPP != NULL) {
        if (cfg.src.w > srcMaxCropWidth)
            cfg.src.w = srcMaxCropWidth;
        if (cfg.src.h > srcMaxCropHeight)
            cfg.src.h = srcMaxCropHeight;
        cfg.src.w = pixel_align_down(cfg.src.w, srcCropWidthAlign);
        cfg.src.h = pixel_align_down(cfg.src.h, srcCropHeightAlign);
    }

    uint64_t bufSize = (uint64_t)handle->size * formatToBpp(handle->format);
    uint64_t srcSize = (uint64_t)cfg.src.f_w * cfg.src.f_h * formatToBpp(cfg.format);

    if (!isFormatLossy(handle->format) && (bufSize < srcSize)) {
        DISPLAY_LOGE("%s:: buffer size is smaller than source size, buf(size: %d, format: %d), src(w: %d, h: %d, format: %d)",
                __func__, handle->size, handle->format, cfg.src.f_w, cfg.src.f_h, cfg.format);
        return -EINVAL;
    }

    return ret;
}


int32_t ExynosDisplay::configureOverlay(ExynosLayer *layer, exynos_win_config_data &cfg)
{
    int32_t ret = NO_ERROR;
    if(layer != NULL) {
        if ((ret = configureHandle(*layer, layer->mAcquireFence, cfg)) != NO_ERROR)
            return ret;

        /* This will be closed by setReleaseFences() using config.acq_fence */
        layer->mAcquireFence = -1;
    }
    return ret;
}
int32_t ExynosDisplay::configureOverlay(ExynosCompositionInfo &compositionInfo)
{
    int32_t windowIndex = compositionInfo.mWindowIndex;
    private_handle_t *handle = compositionInfo.mTargetBuffer;

    if ((windowIndex < 0) || (windowIndex >= (int32_t)mDpuData.configs.size()))
    {
        HWC_LOGE(this, "%s:: ExynosCompositionInfo(%d) has invalid data, windowIndex(%d)",
                __func__, compositionInfo.mType, windowIndex);
        return -EINVAL;
    }

    exynos_win_config_data &config = mDpuData.configs[windowIndex];

    if (handle == NULL) {
        /* config will be set by handleStaticLayers */
        if (compositionInfo.mSkipFlag)
            return NO_ERROR;

        if (compositionInfo.mType == COMPOSITION_CLIENT) {
            ALOGW("%s:: ExynosCompositionInfo(%d) has invalid data, handle(%p)",
                    __func__, compositionInfo.mType, handle);
            if (compositionInfo.mAcquireFence >= 0) {
                compositionInfo.mAcquireFence = fence_close(compositionInfo.mAcquireFence, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
            }
            config.state = config.WIN_STATE_DISABLED;
            return NO_ERROR;
        } else {
            HWC_LOGE(this, "%s:: ExynosCompositionInfo(%d) has invalid data, handle(%p)",
                    __func__, compositionInfo.mType, handle);
            return -EINVAL;
        }
    }

    config.fd_idma[0] = handle->fd;
    config.fd_idma[1] = handle->fd1;
    config.fd_idma[2] = handle->fd2;
    config.protection = (getDrmMode(handle) == SECURE_DRM) ? 1 : 0;
    config.state = config.WIN_STATE_BUFFER;

    config.assignedMPP = compositionInfo.mOtfMPP;

    config.dst.f_w = mXres;
    config.dst.f_h = mYres;
    config.format = handle->format;
    if (compositionInfo.mType == COMPOSITION_EXYNOS) {
        config.src.f_w = pixel_align(mXres, G2D_JUSTIFIED_DST_ALIGN);
        config.src.f_h = pixel_align(mYres, G2D_JUSTIFIED_DST_ALIGN);
    } else {
        config.src.f_w = handle->stride;
        config.src.f_h = handle->vstride;
    }
    config.compression = compositionInfo.mCompressed;
    if (compositionInfo.mCompressed) {
        if (compositionInfo.mType == COMPOSITION_EXYNOS)
            config.comp_src = DPP_COMP_SRC_G2D;
        else if (compositionInfo.mType == COMPOSITION_CLIENT)
            config.comp_src = DPP_COMP_SRC_GPU;
        else
            HWC_LOGE(this, "unknown composition type: %d", compositionInfo.mType);
    }

    bool useCompositionCrop = true;
    if ((mDisplayControl.enableCompositionCrop) &&
        (compositionInfo.mHasCompositionLayer) &&
        (compositionInfo.mFirstIndex >= 0) &&
        (compositionInfo.mLastIndex >= 0)) {
        hwc_rect merged_rect, src_rect;
        merged_rect.left = mXres;
        merged_rect.top = mYres;
        merged_rect.right = 0;
        merged_rect.bottom = 0;

        for (int i = compositionInfo.mFirstIndex; i <= compositionInfo.mLastIndex; i++) {
            ExynosLayer *layer = mLayers[i];
            src_rect.left = layer->mDisplayFrame.left;
            src_rect.top = layer->mDisplayFrame.top;
            src_rect.right = layer->mDisplayFrame.right;
            src_rect.bottom = layer->mDisplayFrame.bottom;
            merged_rect = expand(merged_rect, src_rect);
            DISPLAY_LOGD(eDebugWinConfig, "[%d] layer type: [%d, %d] dispFrame [l: %d, t: %d, r: %d, b: %d], mergedRect [l: %d, t: %d, r: %d, b: %d]",
                    i,
                    layer->mCompositionType,
                    layer->mExynosCompositionType,
                    layer->mDisplayFrame.left,
                    layer->mDisplayFrame.top,
                    layer->mDisplayFrame.right,
                    layer->mDisplayFrame.bottom,
                    merged_rect.left,
                    merged_rect.top,
                    merged_rect.right,
                    merged_rect.bottom);
        }

        config.src.x = merged_rect.left;
        config.src.y = merged_rect.top;
        config.src.w = merged_rect.right - merged_rect.left;
        config.src.h = merged_rect.bottom - merged_rect.top;

        ExynosMPP* exynosMPP = config.assignedMPP;
        if (exynosMPP == NULL) {
            DISPLAY_LOGE("%s:: assignedMPP is NULL", __func__);
            useCompositionCrop = false;
        } else {
            /* Check size constraints */
            uint32_t restrictionIdx = getRestrictionIndex(config.format);
            uint32_t srcXAlign = exynosMPP->getSrcXOffsetAlign(restrictionIdx);
            uint32_t srcYAlign = exynosMPP->getSrcYOffsetAlign(restrictionIdx);
            uint32_t srcWidthAlign = exynosMPP->getSrcCropWidthAlign(restrictionIdx);
            uint32_t srcHeightAlign = exynosMPP->getSrcCropHeightAlign(restrictionIdx);
            uint32_t srcMinWidth = exynosMPP->getSrcMinWidth(restrictionIdx);
            uint32_t srcMinHeight = exynosMPP->getSrcMinHeight(restrictionIdx);

            if (config.src.w < srcMinWidth) {
                config.src.x -= (srcMinWidth - config.src.w);
                if (config.src.x < 0)
                    config.src.x = 0;
                config.src.w = srcMinWidth;
            }
            if (config.src.h < srcMinHeight) {
                config.src.y -= (srcMinHeight - config.src.h);
                if (config.src.y < 0)
                    config.src.y = 0;
                config.src.h = srcMinHeight;
            }

            int32_t alignedSrcX = pixel_align_down(config.src.x, srcXAlign);
            int32_t alignedSrcY = pixel_align_down(config.src.y, srcYAlign);
            config.src.w += (config.src.x - alignedSrcX);
            config.src.h += (config.src.y - alignedSrcY);
            config.src.x = alignedSrcX;
            config.src.y = alignedSrcY;
            config.src.w = pixel_align(config.src.w, srcWidthAlign);
            config.src.h = pixel_align(config.src.h, srcHeightAlign);
        }

        config.dst.x = config.src.x;
        config.dst.y = config.src.y;
        config.dst.w = config.src.w;
        config.dst.h = config.src.h;

        if ((config.src.x < 0) ||
            (config.src.y < 0) ||
            ((config.src.x + config.src.w) > mXres) ||
            ((config.src.y + config.src.h) > mYres)) {
            useCompositionCrop = false;
            ALOGW("Invalid composition target crop size: (%d, %d, %d, %d)",
                    config.src.x, config.src.y,
                    config.src.w, config.src.h);
        }

        DISPLAY_LOGD(eDebugWinConfig, "composition(%d) config[%d] x : %d, y : %d, w : %d, h : %d",
                compositionInfo.mType, windowIndex,
                config.dst.x, config.dst.y,
                config.dst.w, config.dst.h);
    } else {
        useCompositionCrop = false;
    }

    if (useCompositionCrop == false) {
        config.src.x = 0;
        config.src.y = 0;
        config.src.w = mXres;
        config.src.h = mYres;
        config.dst.x = 0;
        config.dst.y = 0;
        config.dst.w = mXres;
        config.dst.h = mYres;
    }

    config.blending = HWC2_BLEND_MODE_PREMULTIPLIED;

    config.acq_fence =
        hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, compositionInfo.mAcquireFence);
    config.plane_alpha = 255;
    config.dataspace = compositionInfo.mSrcImg.dataSpace;
    config.hdr_enable = true;

    /* This will be closed by setReleaseFences() using config.acq_fence */
    compositionInfo.mAcquireFence = -1;
    DISPLAY_LOGD(eDebugSkipStaicLayer, "Configure composition target[%d], config[%d]!!!!",
            compositionInfo.mType, windowIndex);
    dumpConfig(config);

    uint64_t bufSize = (uint64_t)handle->size * formatToBpp(handle->format);
    uint64_t srcSize = (uint64_t)config.src.f_w * config.src.f_h * formatToBpp(config.format);
    if (!isFormatLossy(handle->format) && (bufSize < srcSize)) {
        DISPLAY_LOGE("%s:: buffer size is smaller than source size, buf(size: %d, format: %d), src(w: %d, h: %d, format: %d)",
                __func__, handle->size, handle->format, config.src.f_w, config.src.f_h, config.format);
        return -EINVAL;
    }

    return NO_ERROR;
}

/**
 * @return int
 */
int ExynosDisplay::setWinConfigData() {
    int ret = NO_ERROR;
    mDpuData.initData();

    if (mClientCompositionInfo.mHasCompositionLayer) {
        if ((ret = configureOverlay(mClientCompositionInfo)) != NO_ERROR)
        {
            DISPLAY_LOGE("configureOverlay(ClientCompositionInfo) is failed");
            return ret;
        }
    }
    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if ((ret = configureOverlay(mExynosCompositionInfo)) != NO_ERROR) {
            /* TEST */
            //return ret;
            DISPLAY_LOGE("configureOverlay(ExynosCompositionInfo) is failed");
        }
    }

    /* TODO loop for number of layers */
    for (size_t i = 0; i < mLayers.size(); i++) {
        if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS) ||
                (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT))
            continue;
        int32_t windowIndex =  mLayers[i]->mWindowIndex;
        if ((windowIndex < 0) || (windowIndex >= (int32_t)mDpuData.configs.size())) {
            DISPLAY_LOGE("%s:: %zu layer has invalid windowIndex(%d)",
                    __func__, i, windowIndex);
            return -EINVAL;
        }
        DISPLAY_LOGD(eDebugWinConfig, "%zu layer, config[%d]", i, windowIndex);
        if ((ret = configureOverlay(mLayers[i], mDpuData.configs[windowIndex])) != NO_ERROR)
        {
            DISPLAY_LOGE("Fail to configure layer[%zu]", i);
            return ret;
        }
    }

    return 0;
}

void ExynosDisplay::printDebugInfos(String8 &reason)
{
    bool writeFile = true;
    FILE *pFile = NULL;
    struct timeval tv;
    struct tm* localTime;
    gettimeofday(&tv, NULL);
    localTime = (struct tm*)localtime((time_t*)&tv.tv_sec);
    reason.appendFormat("errFrameNumber: %" PRId64 " time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)\n",
            mErrorFrameCount,
            localTime->tm_mon+1, localTime->tm_mday,
            localTime->tm_hour, localTime->tm_min,
            localTime->tm_sec, tv.tv_usec/1000,
            ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
    ALOGD("%s", reason.string());

    if (mErrorFrameCount >= HWC_PRINT_FRAME_NUM)
        writeFile = false;
    else {
        char filePath[128];
        sprintf(filePath, "%s/%s_hwc_debug%d.dump", ERROR_LOG_PATH0, mDisplayName.string(), (int)mErrorFrameCount);
        pFile = fopen(filePath, "wb");
        if (pFile == NULL) {
            ALOGE("Fail to open file %s, error: %s", filePath, strerror(errno));
            sprintf(filePath, "%s/%s_hwc_debug%d.dump", ERROR_LOG_PATH1, mDisplayName.string(), (int)mErrorFrameCount);
            pFile = fopen(filePath, "wb");
        }
        if (pFile == NULL) {
            ALOGE("Fail to open file %s, error: %s", filePath, strerror(errno));
        } else {
            ALOGI("%s was created", filePath);
            fwrite(reason.string(), 1, reason.size(), pFile);
        }
    }
    mErrorFrameCount++;

    android::String8 result;
    result.appendFormat("Device mGeometryChanged(%" PRIx64 "), mGeometryChanged(%" PRIx64 "), mRenderingState(%d)\n",
            mDevice->mGeometryChanged, mGeometryChanged, mRenderingState);
    result.appendFormat("=======================  dump composition infos  ================================\n");
    ExynosCompositionInfo clientCompInfo = mClientCompositionInfo;
    ExynosCompositionInfo exynosCompInfo = mExynosCompositionInfo;
    clientCompInfo.dump(result);
    exynosCompInfo.dump(result);
    ALOGD("%s", result.string());
    if (pFile != NULL) {
        fwrite(result.string(), 1, result.size(), pFile);
    }
    result.clear();

    result.appendFormat("=======================  dump exynos layers (%zu)  ================================\n",
            mLayers.size());
    ALOGD("%s", result.string());
    if (pFile != NULL) {
        fwrite(result.string(), 1, result.size(), pFile);
    }
    result.clear();
    for (uint32_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        layer->printLayer();
        if (pFile != NULL) {
            layer->dump(result);
            fwrite(result.string(), 1, result.size(), pFile);
            result.clear();
        }
    }

    result.appendFormat("=============================  dump win configs  ===================================\n");
    ALOGD("%s", result.string());
    if (pFile != NULL) {
        fwrite(result.string(), 1, result.size(), pFile);
    }
    result.clear();
    for (size_t i = 0; i <= mDpuData.configs.size(); i++) {
        ALOGD("config[%zu]", i);
        printConfig(mDpuData.configs[i]);
        if (pFile != NULL) {
            result.appendFormat("config[%zu]\n", i);
            dumpConfig(result, mDpuData.configs[i]);
            fwrite(result.string(), 1, result.size(), pFile);
            result.clear();
        }
    }

    /* Fence Tracer Information */
    ExynosDevice *device = mDevice;
    if (device != NULL) {
        for (auto it = device->mFenceInfo.begin(); it != device->mFenceInfo.end(); ++it) {

            uint32_t i = it->first;

            if (device->mFenceInfo.count(i) == 0 ) continue;

            hwc_fence_info_t info = device->mFenceInfo.at(i);
            if (info.usage >= 1 && !info.pendingAllowed) {
                result.appendFormat("\nFD hwc : %d, usage %d, pending : %d\n", i, info.usage, (int)info.pendingAllowed);
                for (int j = 0; j < MAX_FENCE_SEQUENCE; j++) {
                    fenceTrace_t *seq = &info.seq[j];
                    result.appendFormat("    %s(%s)(%s)(cur:%d)(usage:%d)(last:%d)",
                            GET_STRING(fence_dir_map, seq->dir),
                            GET_STRING(fence_ip_map, seq->ip), GET_STRING(fence_type_map, seq->type),
                            seq->curFlag, seq->usage, (int)seq->isLast);

                    tv = seq->time;
                    localTime = (struct tm*)localtime((time_t*)&tv.tv_sec);

                    result.appendFormat(" - time:%02d-%02d %02d:%02d:%02d.%03lu(%lu)\n",
                            localTime->tm_mon+1, localTime->tm_mday,
                            localTime->tm_hour, localTime->tm_min,
                            localTime->tm_sec, tv.tv_usec/1000,
                            ((tv.tv_sec * 1000) + (tv.tv_usec / 1000)));
                    if (pFile != NULL) {
                        fwrite(result.string(), 1, result.size(), pFile);
                    }
                    result.clear();
                }
            }
        }
    }

    if (pFile != NULL) {
        fclose(pFile);
    }
}

int32_t ExynosDisplay::validateWinConfigData()
{
    bool flagValidConfig = true;
    int bufferStateCnt = 0;

    for (size_t i = 0; i < mDpuData.configs.size(); i++) {
        exynos_win_config_data &config = mDpuData.configs[i];
        if (config.state == config.WIN_STATE_BUFFER) {
            bool configInvalid = false;
            /* multiple dma mapping */
            for (size_t j = (i+1); j < mDpuData.configs.size(); j++) {
                exynos_win_config_data &compare_config = mDpuData.configs[j];
                if ((config.state == config.WIN_STATE_BUFFER) &&
                    (compare_config.state == compare_config.WIN_STATE_BUFFER)) {
                    if ((config.assignedMPP != NULL) &&
                        (config.assignedMPP == compare_config.assignedMPP)) {
                        DISPLAY_LOGE("WIN_CONFIG error: duplicated assignedMPP(%s) between win%zu, win%zu",
                                config.assignedMPP->mName.string(), i, j);
                        compare_config.state = compare_config.WIN_STATE_DISABLED;
                        flagValidConfig = false;
                        continue;
                    }
                }
            }
            if ((config.src.x < 0) || (config.src.y < 0)||
                (config.dst.x < 0) || (config.dst.y < 0)||
                (config.src.w <= 0) || (config.src.h <= 0)||
                (config.dst.w <= 0) || (config.dst.h <= 0)||
                (config.dst.x + config.dst.w > (uint32_t)mXres) ||
                (config.dst.y + config.dst.h > (uint32_t)mYres)) {
                DISPLAY_LOGE("WIN_CONFIG error: invalid pos or size win%zu", i);
                configInvalid = true;
            }

            if ((config.src.w > config.src.f_w) ||
                (config.src.h > config.src.f_h)) {
                DISPLAY_LOGE("WIN_CONFIG error: invalid size %zu, %d, %d, %d, %d", i,
                        config.src.w, config.src.f_w, config.src.h, config.src.f_h);
                configInvalid = true;
            }

            /* Source alignment check */
            ExynosMPP* exynosMPP = config.assignedMPP;
            if (exynosMPP == NULL) {
                DISPLAY_LOGE("WIN_CONFIG error: config %zu assigendMPP is NULL", i);
                configInvalid = true;
            } else {
                uint32_t restrictionIdx = getRestrictionIndex(config.format);
                uint32_t srcXAlign = exynosMPP->getSrcXOffsetAlign(restrictionIdx);
                uint32_t srcYAlign = exynosMPP->getSrcYOffsetAlign(restrictionIdx);
                uint32_t srcWidthAlign = exynosMPP->getSrcCropWidthAlign(restrictionIdx);
                uint32_t srcHeightAlign = exynosMPP->getSrcCropHeightAlign(restrictionIdx);
                if ((config.src.x % srcXAlign != 0) ||
                    (config.src.y % srcYAlign != 0) ||
                    (config.src.w % srcWidthAlign != 0) ||
                    (config.src.h % srcHeightAlign != 0))
                {
                    DISPLAY_LOGE("WIN_CONFIG error: invalid src alignment : %zu, "\
                            "assignedMPP: %s, mppType:%d, format(%d), s_x: %d(%d), s_y: %d(%d), s_w : %d(%d), s_h : %d(%d)", i,
                            config.assignedMPP->mName.string(), exynosMPP->mLogicalType, config.format, config.src.x, srcXAlign,
                            config.src.y, srcYAlign, config.src.w, srcWidthAlign, config.src.h, srcHeightAlign);
                    configInvalid = true;
                }
            }

            if (configInvalid) {
                config.state = config.WIN_STATE_DISABLED;
                flagValidConfig = false;
            }

            bufferStateCnt++;
        }

        if ((config.state == config.WIN_STATE_COLOR) ||
            (config.state == config.WIN_STATE_CURSOR))
            bufferStateCnt++;
    }

    if (bufferStateCnt == 0) {
        DISPLAY_LOGE("WIN_CONFIG error: has no buffer window");
        flagValidConfig = false;
    }

    if (flagValidConfig)
        return NO_ERROR;
    else
        return -EINVAL;
}

/**
 * @return int
 */
int ExynosDisplay::setDisplayWinConfigData() {
    return 0;
}

bool ExynosDisplay::checkConfigChanged(const exynos_dpu_data &lastConfigsData, const exynos_dpu_data &newConfigsData)
{
    if (exynosHWCControl.skipWinConfig == 0)
        return true;

    /* HWC doesn't skip WIN_CONFIG if other display is connected */
    if ((mDevice->checkAdditionalConnection()) && (mType == HWC_DISPLAY_PRIMARY))
        return true;

    for (size_t i = 0; i < lastConfigsData.configs.size(); i++) {
        if ((lastConfigsData.configs[i].state != newConfigsData.configs[i].state) ||
            (lastConfigsData.configs[i].fd_idma[0] != newConfigsData.configs[i].fd_idma[0]) ||
            (lastConfigsData.configs[i].fd_idma[1] != newConfigsData.configs[i].fd_idma[1]) ||
            (lastConfigsData.configs[i].fd_idma[2] != newConfigsData.configs[i].fd_idma[2]) ||
            (lastConfigsData.configs[i].dst.x != newConfigsData.configs[i].dst.x) ||
            (lastConfigsData.configs[i].dst.y != newConfigsData.configs[i].dst.y) ||
            (lastConfigsData.configs[i].dst.w != newConfigsData.configs[i].dst.w) ||
            (lastConfigsData.configs[i].dst.h != newConfigsData.configs[i].dst.h) ||
            (lastConfigsData.configs[i].src.x != newConfigsData.configs[i].src.x) ||
            (lastConfigsData.configs[i].src.y != newConfigsData.configs[i].src.y) ||
            (lastConfigsData.configs[i].src.w != newConfigsData.configs[i].src.w) ||
            (lastConfigsData.configs[i].src.h != newConfigsData.configs[i].src.h) ||
            (lastConfigsData.configs[i].format != newConfigsData.configs[i].format) ||
            (lastConfigsData.configs[i].blending != newConfigsData.configs[i].blending) ||
            (lastConfigsData.configs[i].plane_alpha != newConfigsData.configs[i].plane_alpha))
            return true;
    }

    /* To cover buffer payload changed case */
    for (size_t i = 0; i < mLayers.size(); i++) {
        if(mLayers[i]->mLastLayerBuffer != mLayers[i]->mLayerBuffer)
            return true;
    }

    return false;
}

int ExynosDisplay::checkConfigDstChanged(const exynos_dpu_data &lastConfigsData, const exynos_dpu_data &newConfigsData, uint32_t index)
{
    if ((lastConfigsData.configs[index].state != newConfigsData.configs[index].state) ||
        (lastConfigsData.configs[index].fd_idma[0] != newConfigsData.configs[index].fd_idma[0]) ||
        (lastConfigsData.configs[index].fd_idma[1] != newConfigsData.configs[index].fd_idma[1]) ||
        (lastConfigsData.configs[index].fd_idma[2] != newConfigsData.configs[index].fd_idma[2]) ||
        (lastConfigsData.configs[index].format != newConfigsData.configs[index].format) ||
        (lastConfigsData.configs[index].blending != newConfigsData.configs[index].blending) ||
        (lastConfigsData.configs[index].plane_alpha != newConfigsData.configs[index].plane_alpha)) {
        DISPLAY_LOGD(eDebugWindowUpdate, "damage region is skip, but other configuration except dst was changed");
        DISPLAY_LOGD(eDebugWindowUpdate, "\tstate[%d, %d], fd[%d, %d], format[0x%8x, 0x%8x], blending[%d, %d], plane_alpha[%d, %d]",
                lastConfigsData.configs[index].state, newConfigsData.configs[index].state,
                lastConfigsData.configs[index].fd_idma[0], newConfigsData.configs[index].fd_idma[0],
                lastConfigsData.configs[index].format, newConfigsData.configs[index].format,
                lastConfigsData.configs[index].blending, newConfigsData.configs[index].blending,
                lastConfigsData.configs[index].plane_alpha, newConfigsData.configs[index].plane_alpha);
        return -1;
    }
    if ((lastConfigsData.configs[index].dst.x != newConfigsData.configs[index].dst.x) ||
        (lastConfigsData.configs[index].dst.y != newConfigsData.configs[index].dst.y) ||
        (lastConfigsData.configs[index].dst.w != newConfigsData.configs[index].dst.w) ||
        (lastConfigsData.configs[index].dst.h != newConfigsData.configs[index].dst.h) ||
        (lastConfigsData.configs[index].src.x != newConfigsData.configs[index].src.x) ||
        (lastConfigsData.configs[index].src.y != newConfigsData.configs[index].src.y) ||
        (lastConfigsData.configs[index].src.w != newConfigsData.configs[index].src.w) ||
        (lastConfigsData.configs[index].src.h != newConfigsData.configs[index].src.h))
        return 1;

    else
        return 0;
}

/**
 * @return int
 */
int ExynosDisplay::deliverWinConfigData() {

    ATRACE_CALL();
    String8 errString;
    int ret = NO_ERROR;

    ret = validateWinConfigData();
    if (ret != NO_ERROR) {
        errString.appendFormat("Invalid WIN_CONFIG\n");
        goto err;
    }

    for (size_t i = 0; i < mDpuData.configs.size(); i++) {
        if (i == DECON_WIN_UPDATE_IDX) {
            DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "window update config[%zu]", i);
        } else {
            DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "deliver config[%zu]", i);
        }
        dumpConfig(mDpuData.configs[i]);
    }

    if (checkConfigChanged(mDpuData, mLastDpuData) == false) {
        DISPLAY_LOGD(eDebugWinConfig, "Winconfig : same");
#ifndef DISABLE_FENCE
        if (mLastRetireFence > 0) {
            mDpuData.retire_fence =
                hwcCheckFenceDebug(this, FENCE_TYPE_RETIRE, FENCE_IP_DPP,
                        hwc_dup(mLastRetireFence, this,  FENCE_TYPE_RETIRE, FENCE_IP_DPP));
        } else
            mDpuData.retire_fence = -1;
#endif
        ret = 0;
    } else {

        waitPreviousFrameDone();

        for (size_t i = 0; i < mDpuData.configs.size(); i++) {
            setFenceInfo(mDpuData.configs[i].acq_fence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP, FENCE_TO);
        }

        if ((ret = mDisplayInterface->deliverWinConfigData()) < 0) {
            errString.appendFormat("interface's deliverWinConfigData() failed: %s ret(%d)\n", strerror(errno), ret);
            goto err;
        } else {

            mLastDpuData = mDpuData;

            /* For inform */
            if ((mHiberState.hiberExitFd != NULL) && (!mHiberState.exitRequested))
                DISPLAY_LOGD(eDebugWinConfig, "Late hiber exit request!");
            initHiberState();
        }

        for (size_t i = 0; i < mDpuData.configs.size(); i++) {
            setFenceInfo(mDpuData.configs[i].rel_fence, this,
                    FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP, FENCE_FROM);
        }
        setFenceInfo(mDpuData.retire_fence, this,
                FENCE_TYPE_RETIRE, FENCE_IP_DPP, FENCE_FROM);
    }

    return ret;
err:
    printDebugInfos(errString);
    closeFences();
    clearDisplay();
    mDisplayInterface->setForcePanic();

    return ret;
}

/**
 * @return int
 */
int ExynosDisplay::setReleaseFences() {

    int release_fd = -1;
    String8 errString;

    /*
     * Close release fence for client target buffer
     * SurfaceFlinger doesn't get release fence for client target buffer
     */
    if ((mClientCompositionInfo.mHasCompositionLayer) &&
        (mClientCompositionInfo.mWindowIndex >= 0) &&
        (mClientCompositionInfo.mWindowIndex < (int32_t)mDpuData.configs.size())) {

        exynos_win_config_data &config = mDpuData.configs[mClientCompositionInfo.mWindowIndex];

        for (int i = mClientCompositionInfo.mFirstIndex; i <= mClientCompositionInfo.mLastIndex; i++) {
            if (mLayers[i]->mExynosCompositionType != HWC2_COMPOSITION_CLIENT) {
                if(mLayers[i]->mOverlayPriority < ePriorityHigh) {
                    errString.appendFormat("%d layer compositionType is not client(%d)\n", i, mLayers[i]->mExynosCompositionType);
                    goto err;
                } else {
                    continue;
                }
            }
            if (mType == HWC_DISPLAY_VIRTUAL)
                mLayers[i]->mReleaseFence = -1;
            else
                mLayers[i]->mReleaseFence =
                    hwcCheckFenceDebug(this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP,
                            hwc_dup(config.rel_fence, this,
                                FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP));
        }
        config.rel_fence = fence_close(config.rel_fence, this,
                   FENCE_TYPE_SRC_RELEASE, FENCE_IP_FB);
    }

    // DPU doesn't close acq_fence, HWC should close it.
    for (size_t i = 0; i < mDpuData.configs.size(); i++) {
        if (mDpuData.configs[i].acq_fence != -1)
            fence_close(mDpuData.configs[i].acq_fence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP);
        mDpuData.configs[i].acq_fence = -1;
    }
    // DPU doesn't close rel_fence of readback buffer, HWC should close it
    if (mDpuData.readback_info.rel_fence >= 0) {
        mDpuData.readback_info.rel_fence =
            fence_close(mDpuData.readback_info.rel_fence, this,
                FENCE_TYPE_READBACK_RELEASE, FENCE_IP_FB);
    }

    for (size_t i = 0; i < mLayers.size(); i++) {
        if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT) ||
            (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS))
            continue;
        if ((mLayers[i]->mWindowIndex < 0) ||
            (mLayers[i]->mWindowIndex >= mDpuData.configs.size())) {
            errString.appendFormat("%s:: layer[%zu] has invalid window index(%d)\n",
                    __func__, i, mLayers[i]->mWindowIndex);
            goto err;
        }
        exynos_win_config_data &config = mDpuData.configs[mLayers[i]->mWindowIndex];
        if (mLayers[i]->mOtfMPP != NULL) {
            mLayers[i]->mOtfMPP->setHWStateFence(-1);
        }
        if (mLayers[i]->mM2mMPP != NULL) {
            if (mLayers[i]->mM2mMPP->mUseM2MSrcFence)
                mLayers[i]->mReleaseFence = mLayers[i]->mM2mMPP->getSrcReleaseFence(0);
            else {
                mLayers[i]->mReleaseFence = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP,
                    hwc_dup(config.rel_fence, this,
                        FENCE_TYPE_SRC_RELEASE, FENCE_IP_LAYER));
            }

            mLayers[i]->mM2mMPP->resetSrcReleaseFence();
#ifdef DISABLE_FENCE
            mLayers[i]->mM2mMPP->setDstReleaseFence(-1);
#else
            DISPLAY_LOGD(eDebugFence, "m2m : win_index(%d), releaseFencefd(%d)",
                    mLayers[i]->mWindowIndex, config.rel_fence);
            if (config.rel_fence > 0) {
                release_fd = config.rel_fence;
                if (release_fd >= 0) {
                    changeFenceInfoState(release_fd,
                            this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_DPP, FENCE_FROM, true);
                    mLayers[i]->mM2mMPP->setDstReleaseFence(release_fd);
                } else {
                    DISPLAY_LOGE("fail to dup, ret(%d, %s)", errno, strerror(errno));
                    mLayers[i]->mM2mMPP->setDstReleaseFence(-1);
                }
            } else {
                mLayers[i]->mM2mMPP->setDstReleaseFence(-1);
            }
            DISPLAY_LOGD(eDebugFence, "mM2mMPP is used, layer[%zu].releaseFencefd(%d)",
                    i, mLayers[i]->mReleaseFence);
#endif
        } else {
#ifdef DISABLE_FENCE
            mLayers[i]->mReleaseFence = -1;
#else
            DISPLAY_LOGD(eDebugFence, "other : win_index(%d), releaseFencefd(%d)",
                    mLayers[i]->mWindowIndex, config.rel_fence);
            if (config.rel_fence > 0) {
                release_fd = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP, config.rel_fence);
                if (release_fd >= 0)
                    mLayers[i]->mReleaseFence = release_fd;
                else {
                    DISPLAY_LOGE("fail to dup, ret(%d, %s)", errno, strerror(errno));
                    mLayers[i]->mReleaseFence = -1;
                }
            } else {
                mLayers[i]->mReleaseFence = -1;
            }
            DISPLAY_LOGD(eDebugFence, "Direct overlay layer[%zu].releaseFencefd(%d)",
                i, mLayers[i]->mReleaseFence);
#endif
        }
    }

    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if (mExynosCompositionInfo.mM2mMPP == NULL)
        {
            errString.appendFormat("There is exynos composition, but m2mMPP is NULL\n");
            goto err;
        }
        if (mUseDpu &&
            ((mExynosCompositionInfo.mWindowIndex < 0) ||
             (mExynosCompositionInfo.mWindowIndex >= (int32_t)mDpuData.configs.size()))) {
            errString.appendFormat("%s:: exynosComposition has invalid window index(%d)\n",
                    __func__, mExynosCompositionInfo.mWindowIndex);
            goto err;
        }
        exynos_win_config_data &config = mDpuData.configs[mExynosCompositionInfo.mWindowIndex];
        for (int i = mExynosCompositionInfo.mFirstIndex; i <= mExynosCompositionInfo.mLastIndex; i++) {
            /* break when only framebuffer target is assigned on ExynosCompositor */
            if (i == -1)
                break;

            if (mLayers[i]->mExynosCompositionType != HWC2_COMPOSITION_EXYNOS) {
                errString.appendFormat("%d layer compositionType is not exynos(%d)\n", i, mLayers[i]->mExynosCompositionType);
                goto err;
            }

            if (mExynosCompositionInfo.mM2mMPP->mUseM2MSrcFence)
                mLayers[i]->mReleaseFence =
                    mExynosCompositionInfo.mM2mMPP->getSrcReleaseFence(i-mExynosCompositionInfo.mFirstIndex);
            else {
                mLayers[i]->mReleaseFence =
                    hwc_dup(config.rel_fence,
                            this, FENCE_TYPE_SRC_RELEASE, FENCE_IP_LAYER);
            }

            DISPLAY_LOGD(eDebugFence, "exynos composition layer[%d].releaseFencefd(%d)",
                    i, mLayers[i]->mReleaseFence);
        }
        mExynosCompositionInfo.mM2mMPP->resetSrcReleaseFence();
        if(mUseDpu) {
#ifdef DISABLE_FENCE
            mExynosCompositionInfo.mM2mMPP->setDstReleaseFence(-1);
#else
            if (config.rel_fence > 0) {
                changeFenceInfoState(config.rel_fence,
                        this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_DPP, FENCE_FROM, true);
                mExynosCompositionInfo.mM2mMPP->setDstReleaseFence(config.rel_fence);
            }
            else
                mExynosCompositionInfo.mM2mMPP->setDstReleaseFence(-1);
#endif
        }
    }
    return 0;

err:
    printDebugInfos(errString);
    closeFences();
    mDisplayInterface->setForcePanic();
    return -EINVAL;
}

/**
 * If display uses outbuf and outbuf is invalid, this function return false.
 * Otherwise, this function return true.
 * If outbuf is invalid, display should handle fence of layers.
 */
bool ExynosDisplay::checkFrameValidation() {
    return true;
}

int32_t ExynosDisplay::acceptDisplayChanges() {
    int32_t type = 0;
    if (mRenderingState != RENDERING_STATE_VALIDATED) {
        DISPLAY_LOGE("%s:: display is not validated : %d", __func__, mRenderingState);
        mDisplayInterface->setForcePanic();
        return HWC2_ERROR_NOT_VALIDATED;
    }

    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i] != NULL) {
            HDEBUGLOGD(eDebugDefault, "%s, Layer %zu : %d, %d", __func__, i,
                    mLayers[i]->mExynosCompositionType, mLayers[i]->mValidateCompositionType);
            type = getLayerCompositionTypeForValidationType(i);

            /* update compositionType
             * SF updates their state and doesn't call back into HWC HAL
             */
            mLayers[i]->mCompositionType = type;
            mLayers[i]->mExynosCompositionType = mLayers[i]->mValidateCompositionType;
        }
        else {
            HWC_LOGE(this, "Layer %zu is NULL", i);
        }
    }
    mRenderingState = RENDERING_STATE_ACCEPTED_CHANGE;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::createLayer(hwc2_layer_t* outLayer) {

    Mutex::Autolock lock(mDRMutex);
    if (mPlugState == false) {
        DISPLAY_LOGI("%s : skip createLayer. Display is already disconnected", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    /* TODO : Implementation here */
    ExynosLayer *layer = new ExynosLayer(this);

    /* TODO : Sort sequence should be added to somewhere */
    mLayers.add((ExynosLayer*)layer);

    *outLayer = (hwc2_layer_t)layer;
    setGeometryChanged(GEOMETRY_DISPLAY_LAYER_ADDED);

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getActiveConfig(
        hwc2_config_t* outConfig) {
    return mDisplayInterface->getActiveConfig(outConfig);
}

int32_t ExynosDisplay::getLayerCompositionTypeForValidationType(uint32_t layerIndex)
{
    int32_t type = -1;

    if (layerIndex >= mLayers.size())
    {
        DISPLAY_LOGE("invalid layer index (%d)", layerIndex);
        return type;
    }
    if ((mLayers[layerIndex]->mValidateCompositionType == HWC2_COMPOSITION_CLIENT) &&
        (mClientCompositionInfo.mSkipFlag) &&
        (mClientCompositionInfo.mFirstIndex <= (int32_t)layerIndex) &&
        ((int32_t)layerIndex <= mClientCompositionInfo.mLastIndex)) {
        type = HWC2_COMPOSITION_DEVICE;
    } else if (mLayers[layerIndex]->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) {
        if (mLayers[layerIndex]->mCompositionType == HWC2_COMPOSITION_SOLID_COLOR)
            type = HWC2_COMPOSITION_SOLID_COLOR;
        else
            type = HWC2_COMPOSITION_DEVICE;
    } else if ((mLayers[layerIndex]->mCompositionType == HWC2_COMPOSITION_CURSOR) &&
               (mLayers[layerIndex]->mValidateCompositionType == HWC2_COMPOSITION_DEVICE)) {
        if (mDisplayControl.cursorSupport == true)
            type = HWC2_COMPOSITION_CURSOR;
        else
            type = HWC2_COMPOSITION_DEVICE;
    } else if ((mLayers[layerIndex]->mCompositionType == HWC2_COMPOSITION_SOLID_COLOR) &&
               (mLayers[layerIndex]->mValidateCompositionType == HWC2_COMPOSITION_DEVICE)) {
        type = HWC2_COMPOSITION_SOLID_COLOR;
    } else {
        type = mLayers[layerIndex]->mValidateCompositionType;
    }

    return type;
}

int32_t ExynosDisplay::getChangedCompositionTypes(
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* /*hwc2_composition_t*/ outTypes) {

    if (mNeedSkipValidatePresent) {
        *outNumElements = 0;
        return HWC2_ERROR_NONE;
    }

    uint32_t count = 0;
    int32_t type = 0;

    for (size_t i = 0; i < mLayers.size(); i++) {
        DISPLAY_LOGD(eDebugHWC, "[%zu] layer: mCompositionType(%d), mValidateCompositionType(%d), mExynosCompositionType(%d), skipFlag(%d)",
                i, mLayers[i]->mCompositionType, mLayers[i]->mValidateCompositionType,
                mLayers[i]->mExynosCompositionType, mClientCompositionInfo.mSkipFlag);

        type = getLayerCompositionTypeForValidationType(i);
        if (type != mLayers[i]->mCompositionType) {
            if (outLayers == NULL || outTypes == NULL) {
                count++;
            }
            else {
                if (count < *outNumElements) {
                    outLayers[count] = (hwc2_layer_t)mLayers[i];
                    outTypes[count] = type;
                    count++;
                } else {
                    DISPLAY_LOGE("array size is not valid (%d, %d)", count, *outNumElements);
                    String8 errString;
                    errString.appendFormat("array size is not valid (%d, %d)", count, *outNumElements);
                    printDebugInfos(errString);
                    return -1;
                }
            }
        }
    }

    if ((outLayers == NULL) || (outTypes == NULL))
        *outNumElements = count;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getClientTargetSupport(
        uint32_t width, uint32_t height,
        int32_t /*android_pixel_format_t*/ format,
        int32_t /*android_dataspace_t*/ dataspace) {
    if (width != mXres)
        return HWC2_ERROR_UNSUPPORTED;
    if (height != mYres)
        return HWC2_ERROR_UNSUPPORTED;
    if (format != HAL_PIXEL_FORMAT_RGBA_8888)
        return HWC2_ERROR_UNSUPPORTED;
    if (dataspace != HAL_DATASPACE_UNKNOWN)
        return HWC2_ERROR_UNSUPPORTED;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getColorModes(
        uint32_t* outNumModes,
        int32_t* /*android_color_mode_t*/ outModes) {
    return mDisplayInterface->getColorModes(outNumModes, outModes);
}

int32_t ExynosDisplay::getDisplayAttribute(
        hwc2_config_t config,
        int32_t /*hwc2_attribute_t*/ attribute, int32_t* outValue) {
    return mDisplayInterface->getDisplayAttribute(config, attribute, outValue);
}

int32_t ExynosDisplay::getDisplayConfigs(
        uint32_t* outNumConfigs,
        hwc2_config_t* outConfigs) {
    return mDisplayInterface->getDisplayConfigs(outNumConfigs, outConfigs);
}

int32_t ExynosDisplay::getDisplayName(uint32_t* outSize, char* outName)
{
    if (outName == NULL) {
        *outSize = mDisplayName.size();
        return HWC2_ERROR_NONE;
    }

    uint32_t strSize = mDisplayName.size();
    if (*outSize < strSize) {
        DISPLAY_LOGE("Invalide outSize(%d), mDisplayName.size(%d)",
                *outSize, strSize);
        strSize = *outSize;
    }
    std::strncpy(outName, mDisplayName, strSize);
    *outSize = strSize;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getDisplayRequests(
        int32_t* /*hwc2_display_request_t*/ outDisplayRequests,
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* /*hwc2_layer_request_t*/ outLayerRequests) {
    /*
     * This function doesn't check mRenderingState
     * This can be called in the below rendering state
     * RENDERING_STATE_PRESENTED: when it is called by canSkipValidate()
     * RENDERING_STATE_ACCEPTED_CHANGE: when it is called by SF
     * RENDERING_STATE_VALIDATED:  when it is called by validateDisplay()
     */

    String8 errString;
    *outDisplayRequests = 0;

    /*
     * There was the case that this function was called after
     * HWC called hotplug callback function with disconnect parameter,
     * and all of layers are destroyed.
     * This function checks the number of layers to check this case and
     * just returns in this case.
     */
    if ((mNeedSkipValidatePresent) || (mLayers.size() == 0)) {
        *outNumElements = 0;
        return HWC2_ERROR_NONE;
    }

    uint32_t requestNum = 0;
    if (mClientCompositionInfo.mHasCompositionLayer == true) {
        if ((mClientCompositionInfo.mFirstIndex < 0) ||
            (mClientCompositionInfo.mFirstIndex >= (int)mLayers.size()) ||
            (mClientCompositionInfo.mLastIndex < 0) ||
            (mClientCompositionInfo.mLastIndex >= (int)mLayers.size())) {
            errString.appendFormat("%s:: mClientCompositionInfo.mHasCompositionLayer is true "
                    "but index is not valid (firstIndex: %d, lastIndex: %d)\n",
                    __func__, mClientCompositionInfo.mFirstIndex,
                    mClientCompositionInfo.mLastIndex);
            *outNumElements = 0;
            goto err;
        }

        for (int32_t i = mClientCompositionInfo.mFirstIndex; i < mClientCompositionInfo.mLastIndex; i++) {
            ExynosLayer *layer = mLayers[i];
            if (layer->mOverlayPriority >= ePriorityHigh) {
                if ((outLayers != NULL) && (outLayerRequests != NULL)) {
                    if (requestNum >= *outNumElements) {
                        errString.appendFormat("%s:: requestNum(%d), *outNumElements(%d)",
                                __func__, requestNum, *outNumElements);
                        goto err;
                    }
                    outLayers[requestNum] = (hwc2_layer_t)layer;
                    outLayerRequests[requestNum] = HWC2_LAYER_REQUEST_CLEAR_CLIENT_TARGET;
                }
                requestNum++;
            }
        }
    }
    if ((outLayers == NULL) || (outLayerRequests == NULL))
        *outNumElements = requestNum;

    return HWC2_ERROR_NONE;

err:
    printDebugInfos(errString);
    mDisplayInterface->setForcePanic();
    return -EINVAL;
}

int32_t ExynosDisplay::getDisplayType(
        int32_t* /*hwc2_display_type_t*/ outType) {
    switch (mType) {
    case HWC_DISPLAY_PRIMARY:
    case HWC_DISPLAY_EXTERNAL:
        *outType = HWC2_DISPLAY_TYPE_PHYSICAL;
        return HWC2_ERROR_NONE;
    case HWC_DISPLAY_VIRTUAL:
        *outType = HWC2_DISPLAY_TYPE_VIRTUAL;
        return HWC2_ERROR_NONE;
    default:
        DISPLAY_LOGE("Invalid display type(%d)", mType);
        *outType = HWC2_DISPLAY_TYPE_INVALID;
        return HWC2_ERROR_NONE;
    }
}

int32_t ExynosDisplay::getDozeSupport(
        int32_t* outSupport) {
#ifdef USES_DOZEMODE
    *outSupport = 1;
#else
    *outSupport = 0;
#endif
    return 0;
}

int32_t ExynosDisplay::getReleaseFences(
        uint32_t* outNumElements,
        hwc2_layer_t* outLayers, int32_t* outFences) {

    Mutex::Autolock lock(mDisplayMutex);
    if (outLayers == NULL || outFences == NULL)
    {
        uint32_t deviceLayerNum = 0;
        deviceLayerNum = mLayers.size();
        *outNumElements = deviceLayerNum;
    } else {
        uint32_t deviceLayerNum = 0;
        for (size_t i = 0; i < mLayers.size(); i++) {
            outLayers[deviceLayerNum] = (hwc2_layer_t)mLayers[i];
            outFences[deviceLayerNum] = mLayers[i]->mReleaseFence;
            /*
             * layer's release fence will be closed by caller of this function.
             * HWC should not close this fence after this function is returned.
             */
            mLayers[i]->mReleaseFence = -1;

            DISPLAY_LOGD(eDebugHWC, "[%zu] layer deviceLayerNum(%d), release fence: %d", i, deviceLayerNum, outFences[deviceLayerNum]);
            deviceLayerNum++;

            if (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS) {
                setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_DPP);
            } else if (mLayers[i]->mCompositionType == HWC2_COMPOSITION_CLIENT) {
                setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_DPP);
            } else {
                setFenceName(mLayers[i]->mReleaseFence, FENCE_LAYER_RELEASE_DPP);
            }
        }
    }
    return 0;
}

int32_t ExynosDisplay::canSkipValidate() {
    if (exynosHWCControl.skipResourceAssign == 0)
        return SKIP_ERR_CONFIG_DISABLED;

    /* This is first frame. validateDisplay can't be skipped */
    if (mRenderingState == RENDERING_STATE_NONE)
        return SKIP_ERR_FIRST_FRAME;

    if (mDevice->mGeometryChanged != 0) {
        /* validateDisplay() should be called */
        return SKIP_ERR_GEOMETRY_CHAGNED;
    } else if (mClientCompositionInfo.mHasCompositionLayer) {
        /*
         * SurfaceFlinger doesn't skip validateDisplay when there is
         * client composition layer.
         */
        return SKIP_ERR_HAS_CLIENT_COMP;
    } else {
        for (uint32_t i = 0; i < mLayers.size(); i++) {
            if (mLayers[i]->mCompositionType == HWC2_COMPOSITION_CLIENT)
                return SKIP_ERR_HAS_CLIENT_COMP;
        }

        if ((mClientCompositionInfo.mSkipStaticInitFlag == true) &&
            (mClientCompositionInfo.mSkipFlag == true)) {
            if (skipStaticLayerChanged(mClientCompositionInfo) == true)
                return SKIP_ERR_SKIP_STATIC_CHANGED;
        }

        /*
         * If there is hwc2_layer_request_t
         * validateDisplay() can't be skipped
         */
        int32_t displayRequests = 0;
        uint32_t outNumRequests = 0;
        if ((getDisplayRequests(&displayRequests, &outNumRequests, NULL, NULL) != NO_ERROR) ||
            (outNumRequests != 0))
            return SKIP_ERR_HAS_REQUEST;
    }
    return NO_ERROR;
}

int32_t ExynosDisplay::handleSkipPresent(int32_t* outRetireFence)
{
    int ret = 0;
    if ((mNeedSkipPresent == false) && (mNeedSkipValidatePresent == false))
        return -1;

    ALOGI("[%d] presentDisplay is skipped by mNeedSkipPresent(%d), mNeedSkipValidatePresent(%d)",
            mDisplayId, mNeedSkipPresent, mNeedSkipValidatePresent);
    closeFencesForSkipFrame(RENDERING_STATE_PRESENTED);
    *outRetireFence = -1;
    for (size_t i=0; i < mLayers.size(); i++) {
        mLayers[i]->mReleaseFence = -1;
    }
    if (mNeedSkipValidatePresent) {
        ALOGD("\t%s: This display might have been turned on after first validate time",
                mDisplayName.string());
        ret = HWC2_ERROR_NONE;
    } else if (mRenderingState == RENDERING_STATE_NONE) {
        ALOGD("\t%s: present has been called without validate after power on - 1",
                mDisplayName.string());
        ret = HWC2_ERROR_NONE;
    } else {
        /* mRenderingState == RENDERING_STATE_PRESENTED */
        ALOGD("\t%s: present has been called without validate after power on - 2",
                mDisplayName.string());
        ret = HWC2_ERROR_NOT_VALIDATED;
    }

    mRenderingState = RENDERING_STATE_PRESENTED;

    /*
     * Condition that composer service would not call
     * validate and present again in this frame
     */
    if (ret == HWC2_ERROR_NONE)
        setPresentAndClearRenderingStatesFlags();

    /*
     * else means that composer service would call
     * validate and present
     */

    mDevice->invalidate();
    return ret;
}

int32_t ExynosDisplay::forceSkipPresentDisplay(int32_t* outRetireFence)
{
    ATRACE_CALL();
    gettimeofday(&updateTimeInfo.lastPresentTime, NULL);

    int ret = 0;
    Mutex::Autolock lock(mDisplayMutex);

    HDEBUGLOGD(eDebugResourceManager,
            "%s present is forced to be skipped",
            mDisplayName.string());

    closeFencesForSkipFrame(RENDERING_STATE_PRESENTED);
    *outRetireFence = -1;

    for (size_t i=0; i < mLayers.size(); i++) {
        mLayers[i]->mReleaseFence = -1;
    }
    mRenderingState = RENDERING_STATE_PRESENTED;

    /* Clear presentFlag */
    mRenderingStateFlags.presentFlag = false;

    return ret;
}

int32_t ExynosDisplay::presentDisplay(int32_t* outRetireFence) {

    ATRACE_CALL();
    gettimeofday(&updateTimeInfo.lastPresentTime, NULL);

    int ret = 0;
    String8 errString;

    Mutex::Autolock lock(mDisplayMutex);

    /*
     * buffer handle, dataspace were set by setClientTarget() after validateDisplay
     * ExynosImage should be set again according to changed handle and dataspace
     */
    exynos_image src_img;
    exynos_image dst_img;
    setCompositionTargetExynosImage(COMPOSITION_CLIENT, &src_img, &dst_img);
    mClientCompositionInfo.setExynosImage(src_img, dst_img);
    mClientCompositionInfo.setExynosMidImage(dst_img);

    if ((ret = handleSkipPresent(outRetireFence)) >= 0)
        return ret;
    else
        ret = HWC2_ERROR_NONE;

    DISPLAY_LOGD(eDebugResourceManager, "%s: renderingState(%d)", mDisplayName.string(), mRenderingState);
    if (mRenderingState != RENDERING_STATE_ACCEPTED_CHANGE) {
        /*
         * presentDisplay() can be called before validateDisplay()
         * when HWC2_CAPABILITY_SKIP_VALIDATE is supported
         */
#ifndef HWC_SKIP_VALIDATE
        DISPLAY_LOGE("%s:: Skip validate is not supported. Invalid rendering state : %d", __func__, mRenderingState);
        goto err;
#endif
        if ((mRenderingState != RENDERING_STATE_NONE) &&
            (mRenderingState != RENDERING_STATE_PRESENTED) &&
            /* validated by first validate */
            (mRenderingState != RENDERING_STATE_VALIDATED)) {
            DISPLAY_LOGE("%s:: invalid rendering state : %d", __func__, mRenderingState);
            goto err;
        }

        if (mDevice->canSkipValidate() == false)
            goto not_validated;
        else {
            // Reset current frame flags for Fence Tracer
            resetFenceCurFlag(this);
            for (size_t i=0; i < mLayers.size(); i++) {
                // Layer's acquire fence from SF
                setFenceInfo(mLayers[i]->mAcquireFence, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER, FENCE_FROM);
            }
            DISPLAY_LOGD(eDebugSkipValidate, "validate is skipped");
        }

        /*
         * startPostProcessing() might be performed in first validate
         * if validateFlag is true
         */
        if ((mDisplayControl.earlyStartMPP == true) &&
            (mRenderingStateFlags.validateFlag == false)) {
            /*
             * HWC should update performanceInfo when validate is skipped
             * HWC excludes the layer from performance calculation
             * if there is no buffer update. (using ExynosMPP::canSkipProcessing())
             * Therefore performanceInfo should be calculated again if the buffer is updated.
             */
            if ((ret = mDevice->mResourceManager->deliverPerformanceInfo(this)) != NO_ERROR) {
                DISPLAY_LOGE("deliverPerformanceInfo() error (%d) in validateSkip case", ret);
            }
            startPostProcessing();
        }
    }

    if (mDumpCount > 0)
        getDumpLayer();

    mDpuData.initData();

    if ((mLayers.size() == 0) &&
        (mType != HWC_DISPLAY_VIRTUAL)) {
        if (mDevice->isBootFinished)
            clearDisplay(mDpuData.enable_readback);
        *outRetireFence = -1;
        mLastRetireFence = fence_close(mLastRetireFence, this,
                FENCE_TYPE_RETIRE, FENCE_IP_DPP);
        ret = 0;
        goto set_state;
    }

    if (!checkFrameValidation()) {
        clearDisplay();
        *outRetireFence = -1;
        mLastRetireFence = fence_close(mLastRetireFence, this,
                FENCE_TYPE_RETIRE, FENCE_IP_DPP);
        goto set_state;
    }

    if ((mDisplayControl.earlyStartMPP == false) &&
        ((ret = doExynosComposition()) != NO_ERROR)) {
        errString.appendFormat("exynosComposition fail (%d)\n", ret);
        goto err;
    }

    // loop for all layer
    for (size_t i=0; i < mLayers.size(); i++) {
        /* mAcquireFence is updated, Update image info */
        struct exynos_image srcImg, dstImg, midImg;
        mLayers[i]->setSrcExynosImage(&srcImg);
        mLayers[i]->setDstExynosImage(&dstImg);
        mLayers[i]->setExynosImage(srcImg, dstImg);

        if (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_CLIENT) {
            mLayers[i]->mReleaseFence = -1;
            mLayers[i]->mAcquireFence =
                fence_close(mLayers[i]->mAcquireFence, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
        } else if (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_EXYNOS) {
            continue;
        } else {
            if (mLayers[i]->mOtfMPP != NULL) {
                mLayers[i]->mOtfMPP->requestHWStateChange(MPP_HW_STATE_RUNNING);
            }

            if ((mDisplayControl.earlyStartMPP == false) &&
                (mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_DEVICE) &&
                (mLayers[i]->mM2mMPP != NULL)) {
                ExynosMPP *m2mMpp = mLayers[i]->mM2mMPP;
                srcImg = mLayers[i]->mSrcImg;
                midImg = mLayers[i]->mMidImg;
                m2mMpp->requestHWStateChange(MPP_HW_STATE_RUNNING);
                if ((ret = m2mMpp->doPostProcessing(srcImg, midImg)) != NO_ERROR) {
                    HWC_LOGE(this, "%s:: doPostProcessing() failed, layer(%zu), ret(%d)",
                            __func__, i, ret);
                    errString.appendFormat("%s:: doPostProcessing() failed, layer(%zu), ret(%d)\n",
                            __func__, i, ret);
                    goto err;
                } else {
                    /* This should be closed by lib for each resource */
                    mLayers[i]->mAcquireFence = -1;
                }
            }
        }
    }

    if ((ret = setWinConfigData()) != NO_ERROR) {
        errString.appendFormat("setWinConfigData fail (%d)\n", ret);
        goto err;
    }

    if ((ret = handleStaticLayers(mClientCompositionInfo)) != NO_ERROR) {
        mClientCompositionInfo.mSkipStaticInitFlag = false;
        errString.appendFormat("handleStaticLayers error\n");
        goto err;
    }

    handleWindowUpdate();

    setDisplayWinConfigData();

    if ((ret = deliverWinConfigData()) != NO_ERROR) {
        HWC_LOGE(this, "%s:: fail to deliver win_config (%d)", __func__, ret);
        if (mDpuData.retire_fence > 0)
            fence_close(mDpuData.retire_fence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
        mDpuData.retire_fence = -1;
    }

    setReleaseFences();

    if (mDpuData.retire_fence != -1) {
#ifdef DISABLE_FENCE
        if (mDpuData.retire_fence >= 0)
            fence_close(mDpuData.retire_fence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
        *outRetireFence = -1;
#else
        *outRetireFence =
            hwcCheckFenceDebug(this, FENCE_TYPE_RETIRE, FENCE_IP_DPP, mDpuData.retire_fence);
#endif
        setFenceInfo(mDpuData.retire_fence, this,
                FENCE_TYPE_RETIRE, FENCE_IP_LAYER, FENCE_TO);
    } else
        *outRetireFence = -1;

    /* Update last retire fence */
    mLastRetireFence = fence_close(mLastRetireFence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
    mLastRetireFence = hwc_dup((*outRetireFence), this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
    changeFenceInfoState(mLastRetireFence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP, FENCE_DUP, true);
    setFenceName(mLastRetireFence, FENCE_RETIRE);

    increaseMPPDstBufIndex();

    /* Check all of acquireFence are closed */
    for (size_t i=0; i < mLayers.size(); i++) {
        if (mLayers[i]->mAcquireFence != -1) {
            DISPLAY_LOGE("layer[%zu] fence(%d) type(%d, %d, %d) is not closed",
                    i, mLayers[i]->mAcquireFence,
                    mLayers[i]->mCompositionType,
                    mLayers[i]->mExynosCompositionType,
                    mLayers[i]->mValidateCompositionType);
            if (mLayers[i]->mM2mMPP != NULL)
                DISPLAY_LOGE("\t%s is assigned", mLayers[i]->mM2mMPP->mName.string());
            if (mLayers[i]->mAcquireFence > 0)
                fence_close(mLayers[i]->mAcquireFence, this,
                        FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
            mLayers[i]->mAcquireFence = -1;
            printDebugInfos(errString);
            mDisplayInterface->setForcePanic();
        }
    }
    if (mExynosCompositionInfo.mAcquireFence >= 0) {
        DISPLAY_LOGE("mExynosCompositionInfo mAcquireFence(%d) is not initialized", mExynosCompositionInfo.mAcquireFence);
        fence_close(mExynosCompositionInfo.mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
        mExynosCompositionInfo.mAcquireFence = -1;
    }
    if (mClientCompositionInfo.mAcquireFence >= 0) {
        DISPLAY_LOGE("mClientCompositionInfo mAcquireFence(%d) is not initialized", mClientCompositionInfo.mAcquireFence);
        fence_close(mClientCompositionInfo.mAcquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
        mClientCompositionInfo.mAcquireFence = -1;
    }

    /* All of release fences are tranferred */
    for (size_t i=0; i < mLayers.size(); i++) {
        setFenceInfo(mLayers[i]->mReleaseFence, this,
                FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER, FENCE_TO);
    }

    doPostProcessing();

    if (!mDevice->validateFences(this)){
        String8 errString;
        errString.appendFormat("%s:: validate fence failed. \n", __func__);
        printDebugInfos(errString);
        mDisplayInterface->setForcePanic();
    }

    mDpuData.initData();

set_state:
    setPresentAndClearRenderingStatesFlags();
    mRenderingState = RENDERING_STATE_PRESENTED;

    return ret;
err:
    printDebugInfos(errString);
    closeFences();
    *outRetireFence = -1;
    mLastRetireFence = -1;
    setPresentAndClearRenderingStatesFlags();
    mRenderingState = RENDERING_STATE_PRESENTED;
    setGeometryChanged(GEOMETRY_ERROR_CASE);

    mLastDpuData.initData();

    mClientCompositionInfo.mSkipStaticInitFlag = false;
    mExynosCompositionInfo.mSkipStaticInitFlag = false;

    mDpuData.initData();

    if (!mDevice->validateFences(this)){
        errString.appendFormat("%s:: validate fence failed. \n", __func__);
        printDebugInfos(errString);
    }
    mDisplayInterface->setForcePanic();

    return -EINVAL;

not_validated:
    DISPLAY_LOGD(eDebugSkipValidate, "display need validate");
    mRenderingState = RENDERING_STATE_NONE;
    return HWC2_ERROR_NOT_VALIDATED;
}

int32_t ExynosDisplay::presentPostProcessing()
{
    setReadbackBufferInternal(NULL, -1);
    mDpuData.enable_readback = false;
    return NO_ERROR;
}

int32_t ExynosDisplay::setActiveConfig(
        hwc2_config_t config) {
    return mDisplayInterface->setActiveConfig(config);
}

int32_t ExynosDisplay::setClientTarget(
        buffer_handle_t target,
        int32_t acquireFence, int32_t /*android_dataspace_t*/ dataspace) {
    private_handle_t *handle = NULL;
    if (target != NULL)
        handle = private_handle_t::dynamicCast(target);

    if (mClientCompositionInfo.mHasCompositionLayer == false) {
        if (acquireFence >= 0)
            fence_close(acquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
    } else {
#ifdef DISABLE_FENCE
        if (acquireFence >= 0)
            fence_close(acquireFence, this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
        acquireFence = -1;
#endif
        acquireFence = hwcCheckFenceDebug(this, FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB, acquireFence);
        if (handle == NULL) {
            DISPLAY_LOGD(eDebugOverlaySupported, "ClientTarget is NULL, skipStaic (%d)",
                    mClientCompositionInfo.mSkipFlag);
            if (mClientCompositionInfo.mSkipFlag == false) {
                DISPLAY_LOGE("ClientTarget is NULL");
                DISPLAY_LOGE("\t%s:: mRenderingState(%d)",__func__, mRenderingState);
            }
        } else {
            DISPLAY_LOGD(eDebugOverlaySupported, "ClientTarget handle: %p [fd: %d, %d, %d]",
                    handle, handle->fd, handle->fd1, handle->fd2);
            if ((mClientCompositionInfo.mSkipFlag == true) &&
                ((mClientCompositionInfo.mLastWinConfigData.fd_idma[0] != handle->fd) ||
                 (mClientCompositionInfo.mLastWinConfigData.fd_idma[1] != handle->fd1) ||
                 (mClientCompositionInfo.mLastWinConfigData.fd_idma[2] != handle->fd2))) {
                String8 errString;
                DISPLAY_LOGE("skip flag is enabled but buffer is updated lastConfig[%d, %d, %d], handle[%d, %d, %d]\n",
                        mClientCompositionInfo.mLastWinConfigData.fd_idma[0],
                        mClientCompositionInfo.mLastWinConfigData.fd_idma[1],
                        mClientCompositionInfo.mLastWinConfigData.fd_idma[2],
                        handle->fd, handle->fd1, handle->fd2);
                DISPLAY_LOGE("last win config");
                for (size_t i = 0; i < mLastDpuData.configs.size(); i++) {
                    errString.appendFormat("config[%zu]\n", i);
                    dumpConfig(errString, mLastDpuData.configs[i]);
                    DISPLAY_LOGE("\t%s", errString.string());
                    errString.clear();
                }
                errString.appendFormat("%s:: skip flag is enabled but buffer is updated\n",
                        __func__);
                printDebugInfos(errString);
            }
        }
        mClientCompositionInfo.setTargetBuffer(this, handle, acquireFence, (android_dataspace)dataspace);
        setFenceInfo(acquireFence, this,
                FENCE_TYPE_SRC_RELEASE, FENCE_IP_FB, FENCE_FROM);
    }
    if (handle)
        mClientCompositionInfo.mCompressed = isCompressed(handle);

    return 0;
}

int32_t ExynosDisplay::setColorTransform(
        const float* matrix,
        int32_t /*android_color_transform_t*/ hint) {
    if ((hint < HAL_COLOR_TRANSFORM_IDENTITY) ||
        (hint > HAL_COLOR_TRANSFORM_CORRECT_TRITANOPIA))
        return HWC2_ERROR_BAD_PARAMETER;
    ALOGI("%s:: %d, %d", __func__, mColorTransformHint, hint);
    if (mColorTransformHint != hint)
        setGeometryChanged(GEOMETRY_DISPLAY_COLOR_TRANSFORM_CHANGED);
    mColorTransformHint = hint;
#ifdef HWC_SUPPORT_COLOR_TRANSFORM
    int ret = mDisplayInterface->setColorTransform(matrix, hint);
    if (ret != HWC2_ERROR_NONE) {
        mColorTransformHint = HAL_COLOR_TRANSFORM_ERROR;
        setGeometryChanged(GEOMETRY_DISPLAY_COLOR_TRANSFORM_CHANGED);
    }
    return ret;
#else
    return HWC2_ERROR_NONE;
#endif
}

int32_t ExynosDisplay::setColorMode(
        int32_t /*android_color_mode_t*/ mode) {
    int ret = mDisplayInterface->setColorMode(mode);
    if (ret < 0) {
        if (mode == HAL_COLOR_MODE_NATIVE)
            return HWC2_ERROR_NONE;

        ALOGE("%s:: is not supported", __func__);
        return HWC2_ERROR_UNSUPPORTED;
    }

    ALOGI("%s:: %d, %d", __func__, mColorMode, mode);
    if (mColorMode != mode)
        setGeometryChanged(GEOMETRY_DISPLAY_COLOR_MODE_CHANGED);
    mColorMode = (android_color_mode_t)mode;
    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getRenderIntents(int32_t mode, uint32_t* outNumIntents,
        int32_t* /*android_render_intent_v1_1_t*/ outIntents)
{
    ALOGI("%s:: mode(%d), outNum(%d), outIntents(%p)",
            __func__, mode, *outNumIntents, outIntents);

    return mDisplayInterface->getRenderIntents(mode, outNumIntents, outIntents);
}

int32_t ExynosDisplay::setColorModeWithRenderIntent(int32_t /*android_color_mode_t*/ mode,
        int32_t /*android_render_intent_v1_1_t */ intent)
{
    ALOGI("%s:: mode(%d), intent(%d)", __func__, mode, intent);

    return mDisplayInterface->setColorModeWithRenderIntent(mode, intent);
}

int32_t ExynosDisplay::getDisplayIdentificationData(uint8_t* outPort,
        uint32_t* outDataSize, uint8_t* outData)
{
    return mDisplayInterface->getDisplayIdentificationData(outPort, outDataSize, outData);
}

int32_t ExynosDisplay::getDisplayCapabilities(uint32_t* outNumCapabilities,
        uint32_t* outCapabilities)
{
    /* If each display has their own capabilities,
     * this should be described in display module codes */

    uint32_t capabilityNum = 0;

    if (mBrightnessFd != NULL)
        capabilityNum++;

#ifdef USES_DOZEMODE
    capabilityNum++;
#endif

    if (outCapabilities == NULL) {
        *outNumCapabilities = capabilityNum;
        return HWC2_ERROR_NONE;
    }
    if (capabilityNum != *outNumCapabilities) {
        ALOGE("%s:: invalid outNumCapabilities(%d), should be(%d)", __func__, *outNumCapabilities, capabilityNum);
        return HWC2_ERROR_NONE;
    }

    uint32_t index = 0;

    if (mBrightnessFd != NULL)
        outCapabilities[index++] = HWC2_DISPLAY_CAPABILITY_BRIGHTNESS;

#ifdef USES_DOZEMODE
    outCapabilities[index++] = HWC2_DISPLAY_CAPABILITY_DOZE;
#endif

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::getDisplayBrightnessSupport(bool* outSupport)
{
    if (mBrightnessFd == NULL) {
        *outSupport = false;
    } else {
        *outSupport = true;
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::setDisplayBrightness(float brightness)
{
    if (mBrightnessFd == NULL)
        return HWC2_ERROR_UNSUPPORTED;

    char val[4];
    uint32_t scaledBrightness = brightness * mMaxBrightness;

    sprintf(val, "%d", scaledBrightness);
    uint32_t res = fwrite(val, 4, 1, mBrightnessFd);
    if(res == 0){
        ALOGE("brightness write failed.");
    }
    rewind(mBrightnessFd);

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::setOutputBuffer(
        buffer_handle_t __unused buffer,
        int32_t __unused releaseFence) {
    return 0;
}

int ExynosDisplay::clearDisplay(bool readback) {

    int ret = mDisplayInterface->clearDisplay(readback);

    mClientCompositionInfo.mSkipStaticInitFlag = false;
    mClientCompositionInfo.mSkipFlag = false;

    mLastDpuData.initData();

    /* Update last retire fence */
    mLastRetireFence = fence_close(mLastRetireFence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);

    return ret;
}

int32_t ExynosDisplay::setPowerMode(
        int32_t /*hwc2_power_mode_t*/ mode) {
    Mutex::Autolock lock(mDisplayMutex);

#ifndef USES_DOZEMODE
    if ((mode == HWC2_POWER_MODE_DOZE) || (mode == HWC2_POWER_MODE_DOZE_SUSPEND))
        return HWC2_ERROR_UNSUPPORTED;
#endif

    if (mode == HWC_POWER_MODE_OFF) {
        mDevice->mPrimaryBlank = true;
        clearDisplay();
        /*
         * present will be skipped when display is power off.
         * Set present flag and clear flags hear.
         */
        setPresentAndClearRenderingStatesFlags();
        ALOGV("HWC2: Clear display (power off)");
    } else {
        mDevice->mPrimaryBlank = false;
    }

    if (mode == HWC_POWER_MODE_OFF)
        mDREnable = false;
    else
        mDREnable = mDRDefault;

    // check the dynamic recomposition thread by following display power status;
    mDevice->checkDynamicRecompositionThread();


    /* TODO: Call display interface */
    mDisplayInterface->setPowerMode(mode);

    ALOGD("%s:: mode(%d))", __func__, mode);

    this->mPowerModeState = (hwc2_power_mode_t)mode;

    if (mode == HWC_POWER_MODE_OFF) {
        /* It should be called from validate() when the screen is on */
        mNeedSkipPresent = true;
        setGeometryChanged(GEOMETRY_DISPLAY_POWER_OFF);
        if ((mRenderingState >= RENDERING_STATE_VALIDATED) &&
            (mRenderingState < RENDERING_STATE_PRESENTED))
            closeFencesForSkipFrame(RENDERING_STATE_VALIDATED);
        mRenderingState = RENDERING_STATE_NONE;
    } else {
        setGeometryChanged(GEOMETRY_DISPLAY_POWER_ON);
    }

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::setVsyncEnabled(
        int32_t /*hwc2_vsync_t*/ enabled) {

    __u32 val = 0;

//    ALOGD("HWC2 : %s : %d %d", __func__, __LINE__, enabled);

    if (enabled < 0 || enabled > HWC2_VSYNC_DISABLE)
        return HWC2_ERROR_BAD_PARAMETER;


    if (enabled == HWC2_VSYNC_ENABLE) {
        gettimeofday(&updateTimeInfo.lastEnableVsyncTime, NULL);
        val = 1;
    } else {
        gettimeofday(&updateTimeInfo.lastDisableVsyncTime, NULL);
    }

    if (mDisplayInterface->setVsyncEnabled(val) < 0) {
        HWC_LOGE(this, "vsync ioctl failed errno : %d", errno);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    mVsyncState = (hwc2_vsync_t)enabled;

    return HWC2_ERROR_NONE;
}

int32_t ExynosDisplay::forceSkipValidateDisplay(
        uint32_t* outNumTypes, uint32_t* outNumRequests) {
    ATRACE_CALL();
    gettimeofday(&updateTimeInfo.lastValidateTime, NULL);
    Mutex::Autolock lock(mDisplayMutex);
    int ret = 0;
    mUpdateEventCnt++;
    mLastUpdateTimeStamp = systemTime(SYSTEM_TIME_MONOTONIC);

    HDEBUGLOGD(eDebugResourceManager,
            "%s validate is forced to be skipped",
            mDisplayName.string());
    *outNumTypes = 0;
    *outNumRequests = 0;

    mRenderingState = RENDERING_STATE_VALIDATED;

    /* Clear validateFlag */
    mRenderingStateFlags.validateFlag = false;

    return ret;
}
int32_t ExynosDisplay::validateDisplay(
        uint32_t* outNumTypes, uint32_t* outNumRequests) {

    ATRACE_CALL();
    gettimeofday(&updateTimeInfo.lastValidateTime, NULL);
    Mutex::Autolock lock(mDisplayMutex);
    int ret = NO_ERROR;
    bool validateError = false;
    mUpdateEventCnt++;
    mLastUpdateTimeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
    int32_t displayRequests = 0;

    if (mNeedSkipValidatePresent) {
        HDEBUGLOGD(eDebugResourceManager|eDebugSkipResourceAssign,
                "%s validate si skipped by mNeedSkipValidatePresent",
                mDisplayName.string());
        *outNumTypes = 0;
        *outNumRequests = 0;
        goto set_state;
    }

    if (mLayers.size() == 0)
        DISPLAY_LOGI("%s:: validateDisplay layer size is 0", __func__);

    if (mLayers.size() > 1)
        mLayers.vector_sort();

    // Reset current frame flags for Fence Tracer
    resetFenceCurFlag(this);

    doPreProcessing();
    checkLayerFps();
    if (exynosHWCControl.useDynamicRecomp == true && mDREnable)
        checkDynamicReCompMode();

    if (exynosHWCControl.useDynamicRecomp == true &&
        mDevice->isDynamicRecompositionThreadAlive() == false &&
        mDevice->mDRLoopStatus == false) {
        mDevice->dynamicRecompositionThreadCreate();
    }

    for (size_t i=0; i < mLayers.size(); i++) {
        // Layer's acquire fence from SF
        setFenceInfo(mLayers[i]->mAcquireFence, this,
                FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER, FENCE_FROM);
    }

    if ((ret = mResourceManager->assignResource(this)) != NO_ERROR) {
        validateError = true;
        HWC_LOGE(this, "%s:: assignResource() fail, display(%d), ret(%d)", __func__, mDisplayId, ret);
        String8 errString;
        errString.appendFormat("%s:: assignResource() fail, display(%d), ret(%d)\n",
                __func__, mDisplayId, ret);
        printDebugInfos(errString);
        mDisplayInterface->setForcePanic();
    }

    if ((ret = skipStaticLayers(mClientCompositionInfo)) != NO_ERROR) {
        validateError = true;
        HWC_LOGE(this, "%s:: skipStaticLayers() fail, display(%d), ret(%d)", __func__, mDisplayId, ret);
    } else {
        if ((mClientCompositionInfo.mHasCompositionLayer) &&
            (mClientCompositionInfo.mSkipFlag == false)) {
            /* Initialize compositionType */
            for (size_t i = (size_t)mClientCompositionInfo.mFirstIndex; i <= (size_t)mClientCompositionInfo.mLastIndex; i++) {
                if (mLayers[i]->mOverlayPriority >= ePriorityHigh)
                    continue;
                mLayers[i]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            }
        }
    }

    if (mDevice->mIsDumpRequest && mDevice->isLastValidate(this)) {
        mDevice->mIsDumpRequest = false;
        mDevice->setDumpCount();
    }

    /*
     * HWC should update performanceInfo even if assignResource is skipped
     * HWC excludes the layer from performance calculation
     * if there is no buffer update. (using ExynosMPP::canSkipProcessing())
     * Therefore performanceInfo should be calculated again if only the buffer is updated.
     */
    if ((ret = mDevice->mResourceManager->deliverPerformanceInfo(this)) != NO_ERROR) {
        HWC_LOGE(NULL,"%s:: deliverPerformanceInfo() error (%d)",
                __func__, ret);
    }

    if ((validateError == false) && (mDisplayControl.earlyStartMPP == true)) {
        if ((ret = startPostProcessing()) != NO_ERROR)
            validateError = true;
    }

    if (validateError) {
        setGeometryChanged(GEOMETRY_ERROR_CASE);
        mClientCompositionInfo.mSkipStaticInitFlag = false;
        mExynosCompositionInfo.mSkipStaticInitFlag = false;
        mResourceManager->resetAssignedResources(this, true);
        mClientCompositionInfo.initializeInfos(this);
        mExynosCompositionInfo.initializeInfos(this);
        for (uint32_t i = 0; i < mLayers.size(); i++) {
            ExynosLayer *layer = mLayers[i];
            layer->mOverlayInfo |= eResourceAssignFail;
            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            addClientCompositionLayer(i);
        }
        mResourceManager->assignCompositionTarget(this, COMPOSITION_CLIENT);
        mResourceManager->assignWindow(this);
    }

    if ((ret = getChangedCompositionTypes(outNumTypes, NULL, NULL)) != NO_ERROR) {
        HWC_LOGE(this, "%s:: getChangedCompositionTypes() fail, display(%d), ret(%d)", __func__, mDisplayId, ret);
        setGeometryChanged(GEOMETRY_ERROR_CASE);
    }
    if ((ret = getDisplayRequests(&displayRequests, outNumRequests, NULL, NULL)) != NO_ERROR) {
        HWC_LOGE(this, "%s:: getDisplayRequests() fail, display(%d), ret(%d)", __func__, mDisplayId, ret);
        setGeometryChanged(GEOMETRY_ERROR_CASE);
    }

    mNeedSkipPresent = false;

set_state:
    mRenderingState = RENDERING_STATE_VALIDATED;
    /*
     * isFirstValidate() should be checked only before setting validateFlag
     */
    mRenderingStateFlags.validateFlag = true;

    if ((*outNumTypes == 0) && (*outNumRequests == 0))
        return HWC2_ERROR_NONE;

    return HWC2_ERROR_HAS_CHANGES;
}

int32_t ExynosDisplay::startPostProcessing()
{
    int ret = NO_ERROR;
    String8 errString;

    float assignedCapacity = 0;
    if ((assignedCapacity = mDevice->mResourceManager->getAssignedCapacity(MPP_G2D)) > 8.5) {
        DISPLAY_LOGE("Assigned capacity for exynos composition is over restriction (%f)",
                assignedCapacity);
        printDebugInfos(errString);
        mDisplayInterface->setForcePanic();
        return -EINVAL;
    }

    if ((ret = doExynosComposition()) != NO_ERROR) {
        errString.appendFormat("exynosComposition fail (%d)\n", ret);
        goto err;
    }

    // loop for all layer
    for (size_t i=0; i < mLayers.size(); i++) {
        if((mLayers[i]->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) &&
           (mLayers[i]->mM2mMPP != NULL)) {
            /* mAcquireFence is updated, Update image info */
            struct exynos_image srcImg, dstImg, midImg;
            mLayers[i]->setSrcExynosImage(&srcImg);
            mLayers[i]->setDstExynosImage(&dstImg);
            mLayers[i]->setExynosImage(srcImg, dstImg);
            ExynosMPP *m2mMpp = mLayers[i]->mM2mMPP;
            srcImg = mLayers[i]->mSrcImg;
            midImg = mLayers[i]->mMidImg;
            m2mMpp->requestHWStateChange(MPP_HW_STATE_RUNNING);
            if ((ret = m2mMpp->doPostProcessing(srcImg, midImg)) != NO_ERROR) {
                DISPLAY_LOGE("%s:: doPostProcessing() failed, layer(%zu), ret(%d)",
                        __func__, i, ret);
                errString.appendFormat("%s:: doPostProcessing() failed, layer(%zu), ret(%d)\n",
                        __func__, i, ret);
                goto err;
            } else {
                /* This should be closed by lib for each resource */
                mLayers[i]->mAcquireFence = -1;
            }
        }
    }
    return ret;
err:
    printDebugInfos(errString);
    closeFences();
    mDisplayInterface->setForcePanic();
    return -EINVAL;
}

int32_t ExynosDisplay::setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos) {
    mDisplayInterface->setCursorPositionAsync(x_pos, y_pos);
    return HWC2_ERROR_NONE;
}

void ExynosDisplay::dumpConfig(const exynos_win_config_data &c)
{
    DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "\tstate = %u", c.state);
    if (c.state == c.WIN_STATE_COLOR) {
        DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer,
                "\t\tx = %d, y = %d, width = %d, height = %d, color = %u, alpha = %u\n",
                c.dst.x, c.dst.y, c.dst.w, c.dst.h, c.color, c.plane_alpha);
    } else/* if (c.state != c.WIN_STATE_DISABLED) */{
        DISPLAY_LOGD(eDebugWinConfig|eDebugSkipStaicLayer, "\t\tfd = (%d, %d, %d), acq_fence = %d, rel_fence = %d "
                "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                "format = %u, pa = %d, transform = %d, dataspace = 0x%8x, hdr_enable = %d, blending = %u, "
                "protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                "block(x:%d, y:%d, w:%d, h:%d)",
                c.fd_idma[0], c.fd_idma[1], c.fd_idma[2],
                c.acq_fence, c.rel_fence,
                c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                c.format, c.plane_alpha, c.transform, c.dataspace, c.hdr_enable,
                c.blending, c.protection, c.compression, c.comp_src,
                c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
    }
}

void ExynosDisplay::dump(String8& result)
{
    Mutex::Autolock lock(mDisplayMutex);
    result.appendFormat("[%s] display information size: %d x %d, vsyncState: %d, colorMode: %d, colorTransformHint: %d\n",
            mDisplayName.string(),
            mXres, mYres, mVsyncState, mColorMode, mColorTransformHint);
    mClientCompositionInfo.dump(result);
    mExynosCompositionInfo.dump(result);

    for (uint32_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        layer->dump(result);
    }
    result.appendFormat("\n");
}

void ExynosDisplay::dumpConfig(String8 &result, const exynos_win_config_data &c)
{
    result.appendFormat("\tstate = %u\n", c.state);
    if (c.state == c.WIN_STATE_COLOR) {
        result.appendFormat("\t\tx = %d, y = %d, width = %d, height = %d, color = %u, alpha = %u\n",
                c.dst.x, c.dst.y, c.dst.w, c.dst.h, c.color, c.plane_alpha);
    } else/* if (c.state != c.WIN_STATE_DISABLED) */{
        result.appendFormat("\t\tfd = (%d, %d, %d), acq_fence = %d, rel_fence = %d "
                "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                "format = %u, pa = %d, transform = %d, dataspace = 0x%8x, hdr_enable = %d, blending = %u, "
                "protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                "block(x:%d, y:%d, w:%d, h:%d)\n",
                c.fd_idma[0], c.fd_idma[1], c.fd_idma[2],
                c.acq_fence, c.rel_fence,
                c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                c.format, c.plane_alpha, c.transform, c.dataspace, c.hdr_enable, c.blending, c.protection,
                c.compression, c.comp_src,
                c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
    }
}

void ExynosDisplay::printConfig(exynos_win_config_data &c)
{
    ALOGD("\tstate = %u", c.state);
    if (c.state == c.WIN_STATE_COLOR) {
        ALOGD("\t\tx = %d, y = %d, width = %d, height = %d, color = %u, alpha = %u\n",
                c.dst.x, c.dst.y, c.dst.w, c.dst.h, c.color, c.plane_alpha);
    } else/* if (c.state != c.WIN_STATE_DISABLED) */{
        ALOGD("\t\tfd = (%d, %d, %d), acq_fence = %d, rel_fence = %d "
                "src_f_w = %u, src_f_h = %u, src_x = %d, src_y = %d, src_w = %u, src_h = %u, "
                "dst_f_w = %u, dst_f_h = %u, dst_x = %d, dst_y = %d, dst_w = %u, dst_h = %u, "
                "format = %u, pa = %d, transform = %d, dataspace = 0x%8x, hdr_enable = %d, blending = %u, "
                "protection = %u, compression = %d, compression_src = %d, transparent(x:%d, y:%d, w:%d, h:%d), "
                "block(x:%d, y:%d, w:%d, h:%d)",
                c.fd_idma[0], c.fd_idma[1], c.fd_idma[2],
                c.acq_fence, c.rel_fence,
                c.src.f_w, c.src.f_h, c.src.x, c.src.y, c.src.w, c.src.h,
                c.dst.f_w, c.dst.f_h, c.dst.x, c.dst.y, c.dst.w, c.dst.h,
                c.format, c.plane_alpha, c.transform, c.dataspace, c.hdr_enable, c.blending, c.protection,
                c.compression, c.comp_src,
                c.transparent_area.x, c.transparent_area.y, c.transparent_area.w, c.transparent_area.h,
                c.opaque_area.x, c.opaque_area.y, c.opaque_area.w, c.opaque_area.h);
    }
}

int32_t ExynosDisplay::setCompositionTargetExynosImage(uint32_t targetType, exynos_image *src_img, exynos_image *dst_img)
{
    ExynosCompositionInfo compositionInfo;

    if (targetType == COMPOSITION_CLIENT)
        compositionInfo = mClientCompositionInfo;
    else if (targetType == COMPOSITION_EXYNOS)
        compositionInfo = mExynosCompositionInfo;
    else
        return -EINVAL;

    src_img->fullWidth = mXres;
    src_img->fullHeight = mYres;
    /* To do */
    /* Fb crop should be set hear */
    src_img->x = 0;
    src_img->y = 0;
    src_img->w = mXres;
    src_img->h = mYres;

    if (compositionInfo.mTargetBuffer != NULL) {
        src_img->bufferHandle = compositionInfo.mTargetBuffer;
        src_img->format = compositionInfo.mTargetBuffer->format;
#ifdef GRALLOC_VERSION1
        src_img->usageFlags = compositionInfo.mTargetBuffer->producer_usage;
#else
        src_img->usageFlags = compositionInfo.mTargetBuffer->flags;
#endif
    } else {
        src_img->bufferHandle = NULL;
        src_img->format = HAL_PIXEL_FORMAT_RGBA_8888;
        src_img->usageFlags = 0;
    }
    src_img->layerFlags = 0x0;
    src_img->acquireFenceFd = compositionInfo.mAcquireFence;
    src_img->releaseFenceFd = -1;
    src_img->dataSpace = compositionInfo.mDataSpace;
    src_img->blending = HWC2_BLEND_MODE_PREMULTIPLIED;
    src_img->transform = 0;
    src_img->compressed = compositionInfo.mCompressed;
    src_img->planeAlpha = 1;
    src_img->zOrder = 0;
    if ((targetType == COMPOSITION_CLIENT) && (mType == HWC_DISPLAY_VIRTUAL)) {
        if (compositionInfo.mLastIndex < mExynosCompositionInfo.mLastIndex)
            src_img->zOrder = 0;
        else
            src_img->zOrder = 1000;
    }

    dst_img->fullWidth = mXres;
    dst_img->fullHeight = mYres;
    /* To do */
    /* Fb crop should be set hear */
    dst_img->x = 0;
    dst_img->y = 0;
    dst_img->w = mXres;
    dst_img->h = mYres;

    dst_img->bufferHandle = NULL;
    dst_img->format = HAL_PIXEL_FORMAT_RGBA_8888;
    dst_img->usageFlags = 0;

    dst_img->layerFlags = 0x0;
    dst_img->acquireFenceFd = -1;
    dst_img->releaseFenceFd = -1;
    dst_img->dataSpace = src_img->dataSpace;
    if (mColorMode != HAL_COLOR_MODE_NATIVE)
        dst_img->dataSpace = colorModeToDataspace(mColorMode);
    dst_img->blending = HWC2_BLEND_MODE_NONE;
    dst_img->transform = 0;
    dst_img->compressed = compositionInfo.mCompressed;
    dst_img->planeAlpha = 1;
    dst_img->zOrder = src_img->zOrder;

    return NO_ERROR;
}

int32_t ExynosDisplay::initializeValidateInfos()
{
    mCursorIndex = -1;
    for (uint32_t i = 0; i < mLayers.size(); i++) {
        ExynosLayer *layer = mLayers[i];
        layer->mValidateCompositionType = HWC2_COMPOSITION_INVALID;
        layer->mOverlayInfo = 0;
        if ((mDisplayControl.cursorSupport == true) &&
            (mLayers[i]->mCompositionType == HWC2_COMPOSITION_CURSOR))
            mCursorIndex = i;
    }

    exynos_image src_img;
    exynos_image dst_img;

    mClientCompositionInfo.initializeInfos(this);
    setCompositionTargetExynosImage(COMPOSITION_CLIENT, &src_img, &dst_img);
    mClientCompositionInfo.setExynosImage(src_img, dst_img);

    mExynosCompositionInfo.initializeInfos(this);
    setCompositionTargetExynosImage(COMPOSITION_EXYNOS, &src_img, &dst_img);
    mExynosCompositionInfo.setExynosImage(src_img, dst_img);

    return NO_ERROR;
}

int32_t ExynosDisplay::addClientCompositionLayer(uint32_t layerIndex,
        uint32_t *isExynosCompositionChanged)
{
    bool exynosCompositionChanged = false;
    int32_t ret = NO_ERROR;

    if (isExynosCompositionChanged != NULL)
        *isExynosCompositionChanged = 0;

    DISPLAY_LOGD(eDebugResourceManager, "[%d] layer is added to client composition", layerIndex);

    if (mClientCompositionInfo.mHasCompositionLayer == false) {
        mClientCompositionInfo.mFirstIndex = layerIndex;
        mClientCompositionInfo.mLastIndex = layerIndex;
        mClientCompositionInfo.mHasCompositionLayer = true;
        return EXYNOS_ERROR_CHANGED;
    } else {
        mClientCompositionInfo.mFirstIndex = min(mClientCompositionInfo.mFirstIndex, (int32_t)layerIndex);
        mClientCompositionInfo.mLastIndex = max(mClientCompositionInfo.mLastIndex, (int32_t)layerIndex);
    }
    DISPLAY_LOGD(eDebugResourceAssigning, "\tClient composition range [%d] - [%d]",
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);

    if ((mClientCompositionInfo.mFirstIndex < 0) || (mClientCompositionInfo.mLastIndex < 0))
    {
        HWC_LOGE(this, "%s:: mClientCompositionInfo.mHasCompositionLayer is true "
                "but index is not valid (firstIndex: %d, lastIndex: %d)",
                __func__, mClientCompositionInfo.mFirstIndex,
                mClientCompositionInfo.mLastIndex);
        return -EINVAL;
    }

    /* handle sandwiched layers */
    for (uint32_t i = (uint32_t)mClientCompositionInfo.mFirstIndex + 1; i < (uint32_t)mClientCompositionInfo.mLastIndex; i++) {
        ExynosLayer *layer = mLayers[i];
        if (layer->mOverlayPriority >= ePriorityHigh) {
            DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer has high or max priority (%d)", i, layer->mOverlayPriority);
            continue;
        }
        if (layer->mValidateCompositionType != HWC2_COMPOSITION_CLIENT)
        {
            DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer changed", i);
            if (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)
                exynosCompositionChanged = true;
            else {
                if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE)
                    mWindowNumUsed--;
            }
            layer->resetAssignedResource();
            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            layer->mOverlayInfo |= eSandwitchedBetweenGLES;
        }
    }

    /* handle source over blending */
    if ((mBlendingNoneIndex != -1) && (mLayers.size() > 0)) {
        if ((mLayers[layerIndex]->mOverlayPriority < ePriorityHigh) &&
            (layerIndex <= (uint32_t)mBlendingNoneIndex)) {
            DISPLAY_LOGD(eDebugResourceAssigning, "\tmBlendingNoneIndex(%d), layerIndex(%d)", mBlendingNoneIndex, layerIndex);
            for (int32_t i = mBlendingNoneIndex; i >= 0; i--) {
                ExynosLayer *layer = mLayers[i];
                if (layer->mOverlayPriority >= ePriorityHigh)
                    continue;

                DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer: %d", i, layer->mValidateCompositionType);
                if (layer->mValidateCompositionType != HWC2_COMPOSITION_CLIENT)
                {
                    DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer changed", i);
                    if (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)
                        exynosCompositionChanged = true;
                    layer->resetAssignedResource();
                    layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                    layer->mOverlayInfo |= eSourceOverBelow;
                    mClientCompositionInfo.mFirstIndex = min(mClientCompositionInfo.mFirstIndex, (int32_t)i);
                    mClientCompositionInfo.mLastIndex = max(mClientCompositionInfo.mLastIndex, (int32_t)i);
                }
            }
        }
    }

    /* Check Exynos Composition info is changed */
    if (exynosCompositionChanged) {
        DISPLAY_LOGD(eDebugResourceAssigning, "exynos composition [%d] - [%d] is changed",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);
        uint32_t newFirstIndex = ~0;
        int32_t newLastIndex = -1;

        if ((mExynosCompositionInfo.mFirstIndex < 0) || (mExynosCompositionInfo.mLastIndex < 0))
        {
            HWC_LOGE(this, "%s:: mExynosCompositionInfo.mHasCompositionLayer should be true(%d) "
                    "but index is not valid (firstIndex: %d, lastIndex: %d)",
                    __func__, mExynosCompositionInfo.mHasCompositionLayer,
                    mExynosCompositionInfo.mFirstIndex,
                    mExynosCompositionInfo.mLastIndex);
            return -EINVAL;
        }

        for (uint32_t i = 0; i < mLayers.size(); i++)
        {
            ExynosLayer *exynosLayer = mLayers[i];
            if (exynosLayer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS) {
                newFirstIndex = min(newFirstIndex, i);
                newLastIndex = max(newLastIndex, (int32_t)i);
            }
        }

        DISPLAY_LOGD(eDebugResourceAssigning, "changed exynos composition [%d] - [%d]",
                newFirstIndex, newLastIndex);

        /* There is no exynos composition layer */
        if (newFirstIndex == (uint32_t)~0)
        {
            mExynosCompositionInfo.initializeInfos(this);
            ret = EXYNOS_ERROR_CHANGED;
        } else {
            mExynosCompositionInfo.mFirstIndex = newFirstIndex;
            mExynosCompositionInfo.mLastIndex = newLastIndex;
        }
        if (isExynosCompositionChanged != NULL)
            *isExynosCompositionChanged = 1;
    }

    DISPLAY_LOGD(eDebugResourceManager, "\tresult changeFlag(0x%8x)", ret);
    DISPLAY_LOGD(eDebugResourceManager, "\tClient composition(%d) range [%d] - [%d]",
            mClientCompositionInfo.mHasCompositionLayer,
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);
    DISPLAY_LOGD(eDebugResourceManager, "\tExynos composition(%d) range [%d] - [%d]",
            mExynosCompositionInfo.mHasCompositionLayer,
            mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);

    return ret;
}
int32_t ExynosDisplay::removeClientCompositionLayer(uint32_t layerIndex)
{
    int32_t ret = NO_ERROR;

    DISPLAY_LOGD(eDebugResourceManager, "[%d] - [%d] [%d] layer is removed from client composition",
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex,
            layerIndex);

    /* Only first layer or last layer can be removed */
    if ((mClientCompositionInfo.mHasCompositionLayer == false) ||
        ((mClientCompositionInfo.mFirstIndex != (int32_t)layerIndex) &&
         (mClientCompositionInfo.mLastIndex != (int32_t)layerIndex))) {
        DISPLAY_LOGE("removeClientCompositionLayer() error, [%d] - [%d], layer[%d]",
                mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex,
                layerIndex);
        return -EINVAL;
    }

    if (mClientCompositionInfo.mFirstIndex == mClientCompositionInfo.mLastIndex) {
        ExynosMPP *otfMPP = mClientCompositionInfo.mOtfMPP;
        if (otfMPP != NULL)
            otfMPP->resetAssignedState();
        else {
            DISPLAY_LOGE("mClientCompositionInfo.mOtfMPP is NULL");
            return -EINVAL;
        }
        mClientCompositionInfo.initializeInfos(this);
        mWindowNumUsed--;
    } else if ((int32_t)layerIndex == mClientCompositionInfo.mFirstIndex)
        mClientCompositionInfo.mFirstIndex++;
    else
        mClientCompositionInfo.mLastIndex--;

    DISPLAY_LOGD(eDebugResourceManager, "\tClient composition(%d) range [%d] - [%d]",
            mClientCompositionInfo.mHasCompositionLayer,
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);

    return ret;
}


int32_t ExynosDisplay::addExynosCompositionLayer(uint32_t layerIndex)
{
    bool invalidFlag = false;
    int32_t changeFlag = NO_ERROR;
    int ret = 0;
    int32_t startIndex;
    int32_t endIndex;

    DISPLAY_LOGD(eDebugResourceManager, "[%d] layer is added to exynos composition", layerIndex);

    if (mExynosCompositionInfo.mHasCompositionLayer == false) {
        mExynosCompositionInfo.mFirstIndex = layerIndex;
        mExynosCompositionInfo.mLastIndex = layerIndex;
        mExynosCompositionInfo.mHasCompositionLayer = true;
        return EXYNOS_ERROR_CHANGED;
    } else {
        mExynosCompositionInfo.mFirstIndex = min(mExynosCompositionInfo.mFirstIndex, (int32_t)layerIndex);
        mExynosCompositionInfo.mLastIndex = max(mExynosCompositionInfo.mLastIndex, (int32_t)layerIndex);
    }

    DISPLAY_LOGD(eDebugResourceAssigning, "\tExynos composition range [%d] - [%d]",
            mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);

    ExynosMPP *m2mMPP = mExynosCompositionInfo.mM2mMPP;

    if (m2mMPP == NULL) {
        DISPLAY_LOGE("exynosComposition m2mMPP is NULL");
        return -EINVAL;
    }

    startIndex = mExynosCompositionInfo.mFirstIndex;
    endIndex = mExynosCompositionInfo.mLastIndex;

    if ((startIndex < 0) || (endIndex < 0) ||
        (startIndex >= (int32_t)mLayers.size()) || (endIndex >= (int32_t)mLayers.size())) {
        DISPLAY_LOGE("exynosComposition invalid index (%d), (%d)", startIndex, endIndex);
        return -EINVAL;
    }

    uint32_t highPriorityIndex = 0;
    uint32_t highPriorityNum = 0;
    int32_t highPriorityCheck = 0;
    std::vector<int32_t> highPriority;
    highPriority.assign(mLayers.size(), -1);
    /* handle sandwiched layers */
    for (int32_t i = startIndex; i <= endIndex; i++) {
        ExynosLayer *layer = mLayers[i];
        if (layer == NULL) {
            DISPLAY_LOGE("layer[%d] layer is null", i);
            continue;
        }

        if (layer->mOverlayPriority >= ePriorityHigh)
        {
            DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer has high priority", i);
            highPriority[highPriorityIndex++] = i;
            highPriorityNum++;
            continue;
        }

        if (layer->mValidateCompositionType == HWC2_COMPOSITION_EXYNOS)
            continue;

        exynos_image src_img;
        exynos_image dst_img;
        layer->setSrcExynosImage(&src_img);
        layer->setDstExynosImage(&dst_img);
        layer->setExynosImage(src_img, dst_img);
        layer->setExynosMidImage(dst_img);
        bool isAssignable = false;
        if ((layer->mSupportedMPPFlag & m2mMPP->mLogicalType) != 0)
            isAssignable = m2mMPP->isAssignable(this, src_img, dst_img);

        if (layer->mValidateCompositionType == HWC2_COMPOSITION_CLIENT)
        {
            DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer is client composition", i);
            invalidFlag = true;
        } else if (((layer->mSupportedMPPFlag & m2mMPP->mLogicalType) == 0) ||
                   (isAssignable == false))
        {
            DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer is not supported by G2D", i);
            invalidFlag = true;
            layer->resetAssignedResource();
            layer->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            if ((ret = addClientCompositionLayer(i)) < 0)
                return ret;
            changeFlag |= ret;
        } else if ((layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) ||
                   (layer->mValidateCompositionType == HWC2_COMPOSITION_INVALID)) {
            DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer changed", i);
            layer->mOverlayInfo |= eSandwitchedBetweenEXYNOS;
            layer->resetAssignedResource();
            if ((ret = m2mMPP->assignMPP(this, layer)) != NO_ERROR)
            {
                HWC_LOGE(this, "%s:: %s MPP assignMPP() error (%d)",
                        __func__, m2mMPP->mName.string(), ret);
                return ret;
            }
            if (layer->mValidateCompositionType == HWC2_COMPOSITION_DEVICE)
                mWindowNumUsed--;
            layer->mValidateCompositionType = HWC2_COMPOSITION_EXYNOS;
            mExynosCompositionInfo.mFirstIndex = min(mExynosCompositionInfo.mFirstIndex, (int32_t)i);
            mExynosCompositionInfo.mLastIndex = max(mExynosCompositionInfo.mLastIndex, (int32_t)i);
        } else {
            DISPLAY_LOGD(eDebugResourceAssigning, "\t[%d] layer has known type (%d)", i, layer->mValidateCompositionType);
        }
    }

    if (invalidFlag) {
        DISPLAY_LOGD(eDebugResourceAssigning, "\tClient composition range [%d] - [%d]",
                mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);
        DISPLAY_LOGD(eDebugResourceAssigning, "\tExynos composition range [%d] - [%d], highPriorityNum[%d]",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex, highPriorityNum);

        /* Check if exynos comosition nests GLES composition */
        if ((mClientCompositionInfo.mHasCompositionLayer) &&
            (mExynosCompositionInfo.mFirstIndex < mClientCompositionInfo.mFirstIndex) &&
            (mClientCompositionInfo.mFirstIndex < mExynosCompositionInfo.mLastIndex) &&
            (mExynosCompositionInfo.mFirstIndex < mClientCompositionInfo.mLastIndex) &&
            (mClientCompositionInfo.mLastIndex < mExynosCompositionInfo.mLastIndex)) {

            uint32_t isExynosCompositionChanged = 0;
            if ((mClientCompositionInfo.mFirstIndex - mExynosCompositionInfo.mFirstIndex) <
                (mExynosCompositionInfo.mLastIndex - mClientCompositionInfo.mLastIndex)) {
                mLayers[mExynosCompositionInfo.mFirstIndex]->resetAssignedResource();
                mLayers[mExynosCompositionInfo.mFirstIndex]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                if ((ret = addClientCompositionLayer(mExynosCompositionInfo.mFirstIndex,
                                &isExynosCompositionChanged)) < 0)
                    return ret;
                /* Update index only if index was not already changed by addClientCompositionLayer */
                if (isExynosCompositionChanged == 0)
                    mExynosCompositionInfo.mFirstIndex = mClientCompositionInfo.mLastIndex + 1;
                changeFlag |= ret;
            } else {
                mLayers[mExynosCompositionInfo.mLastIndex]->resetAssignedResource();
                mLayers[mExynosCompositionInfo.mLastIndex]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
                if ((ret = addClientCompositionLayer(mExynosCompositionInfo.mLastIndex,
                                &isExynosCompositionChanged)) < 0)
                    return ret;
                /* Update index only if index was not already changed by addClientCompositionLayer */
                if (isExynosCompositionChanged == 0)
                    mExynosCompositionInfo.mLastIndex = (mClientCompositionInfo.mFirstIndex - 1);
                changeFlag |= ret;
            }
        }
    }

    if (highPriorityNum > 0 && (m2mMPP->mLogicalType == MPP_LOGICAL_G2D_RGB)) {
        for (uint32_t i = 0; i < highPriorityNum; i++) {
            if ((int32_t)highPriority[i] == mExynosCompositionInfo.mFirstIndex)
                mExynosCompositionInfo.mFirstIndex++;
            else if ((int32_t)highPriority[i] == mExynosCompositionInfo.mLastIndex)
                mExynosCompositionInfo.mLastIndex--;
        }
    }

    if ((mExynosCompositionInfo.mFirstIndex < 0) ||
        (mExynosCompositionInfo.mFirstIndex >= (int)mLayers.size()) ||
        (mExynosCompositionInfo.mLastIndex < 0) ||
        (mExynosCompositionInfo.mLastIndex >= (int)mLayers.size()) ||
        (mExynosCompositionInfo.mFirstIndex > mExynosCompositionInfo.mLastIndex))
    {
        DISPLAY_LOGD(eDebugResourceAssigning, "\texynos composition is disabled, because of invalid index (%d, %d), size(%zu)",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex, mLayers.size());
        mExynosCompositionInfo.initializeInfos(this);
        changeFlag = EXYNOS_ERROR_CHANGED;
    }

    for (uint32_t i = 0; i < highPriorityNum; i++) {
        if ((mExynosCompositionInfo.mFirstIndex < (int32_t)highPriority[i]) &&
            ((int32_t)highPriority[i] < mExynosCompositionInfo.mLastIndex)) {
            highPriorityCheck = 1;
            break;
        }
    }


    if (highPriorityCheck && (m2mMPP->mLogicalType = MPP_LOGICAL_G2D_RGB)) {
        startIndex = mExynosCompositionInfo.mFirstIndex;
        endIndex = mExynosCompositionInfo.mLastIndex;
        DISPLAY_LOGD(eDebugResourceAssigning, "\texynos composition is disabled because of sandwitched max priority layer (%d, %d)",
                mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);
        for (int32_t i = startIndex; i <= endIndex; i++) {
            int32_t checkPri = 0;
            for (uint32_t j = 0; j < highPriorityNum; j++) {
                if (i == (int32_t)highPriority[j]) {
                    checkPri = 1;
                    break;
                }
            }

            if (checkPri)
                continue;

            mLayers[i]->resetAssignedResource();
            mLayers[i]->mValidateCompositionType = HWC2_COMPOSITION_CLIENT;
            if ((ret = addClientCompositionLayer(i)) < 0)
                HWC_LOGE(this, "%d layer: addClientCompositionLayer() fail", i);
        }
        mExynosCompositionInfo.initializeInfos(this);
        changeFlag = EXYNOS_ERROR_CHANGED;
    }

    DISPLAY_LOGD(eDebugResourceManager, "\tresult changeFlag(0x%8x)", changeFlag);
    DISPLAY_LOGD(eDebugResourceManager, "\tClient composition range [%d] - [%d]",
            mClientCompositionInfo.mFirstIndex, mClientCompositionInfo.mLastIndex);
    DISPLAY_LOGD(eDebugResourceManager, "\tExynos composition range [%d] - [%d]",
            mExynosCompositionInfo.mFirstIndex, mExynosCompositionInfo.mLastIndex);

    return changeFlag;
}

bool ExynosDisplay::windowUpdateExceptions()
{

    if (mExynosCompositionInfo.mHasCompositionLayer) {
        DISPLAY_LOGD(eDebugWindowUpdate, "has exynos composition");
        return true;
    }
    if (mClientCompositionInfo.mHasCompositionLayer) {
        DISPLAY_LOGD(eDebugWindowUpdate, "has client composition");
        return true;
    }

    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i]->mM2mMPP != NULL) return true;
        if (mLayers[i]->mLayerBuffer == NULL) return true;
        if (mLayers[i]->mTransform != 0) return true;
    }

    for (size_t i = 0; i < mDpuData.configs.size(); i++) {
        exynos_win_config_data &config = mDpuData.configs[i];
        if (config.state == config.WIN_STATE_BUFFER) {
            if (config.src.w/config.dst.w != 1 || config.src.h/config.dst.h != 1) {
                DISPLAY_LOGD(eDebugWindowUpdate, "Skip reason : scaled");
                return true;
            }
        }
    }

    return false;
}

int ExynosDisplay::handleWindowUpdate()
{
    int ret = NO_ERROR;
    // TODO will be implemented
    unsigned int excp;

    mDpuData.enable_win_update = false;

    if (exynosHWCControl.windowUpdate != 1) return 0;

    if (mGeometryChanged != 0) {
        DISPLAY_LOGD(eDebugWindowUpdate, "GEOMETRY chnaged 0x%"  PRIx64 "",
                mGeometryChanged);
        return 0;
    }

    if ((mCursorIndex >= 0) && (mCursorIndex < (int32_t)mLayers.size())) {
        ExynosLayer *layer = mLayers[mCursorIndex];
        /* Cursor layer is enabled */
        if (layer->mExynosCompositionType == HWC2_COMPOSITION_DEVICE) {
            return 0;
        }
    }

    /* exceptions */
    if (windowUpdateExceptions())
        return 0;

    hwc_rect mergedRect = {(int)mXres, (int)mYres, 0, 0};
    hwc_rect damageRect = {(int)mXres, (int)mYres, 0, 0};

    for (size_t i = 0; i < mLayers.size(); i++) {
        excp = getLayerRegion(mLayers[i], &damageRect, eDamageRegionByDamage);
        if (excp == eDamageRegionPartial) {
            DISPLAY_LOGD(eDebugWindowUpdate, "layer(%zu) partial : %d, %d, %d, %d", i,
                    damageRect.left, damageRect.top, damageRect.right, damageRect.bottom);
            mergedRect = expand(mergedRect, damageRect);
        }
        else if (excp == eDamageRegionSkip) {
            int32_t windowIndex = mLayers[i]->mWindowIndex;
            if ((windowIndex < 0) ||
                (windowIndex >= mDpuData.configs.size())) {
                DISPLAY_LOGE("%s:: layer[%zu] has invalid window index(%d)\n",
                        __func__, i, windowIndex);
                return 0;
            }
            if ((ret = checkConfigDstChanged(mDpuData, mLastDpuData, windowIndex)) < 0) {
                return 0;
            } else if (ret > 0) {
                damageRect.left = mLayers[i]->mDisplayFrame.left;
                damageRect.right = mLayers[i]->mDisplayFrame.right;
                damageRect.top = mLayers[i]->mDisplayFrame.top;
                damageRect.bottom = mLayers[i]->mDisplayFrame.bottom;
                DISPLAY_LOGD(eDebugWindowUpdate, "Skip layer (origin) : %d, %d, %d, %d",
                        damageRect.left, damageRect.top, damageRect.right, damageRect.bottom);
                mergedRect = expand(mergedRect, damageRect);
                hwc_rect prevDst = {mLastDpuData.configs[i].dst.x, mLastDpuData.configs[i].dst.y,
                    mLastDpuData.configs[i].dst.x + (int)mLastDpuData.configs[i].dst.w,
                    mLastDpuData.configs[i].dst.y + (int)mLastDpuData.configs[i].dst.h};
                mergedRect = expand(mergedRect, prevDst);
            } else {
                DISPLAY_LOGD(eDebugWindowUpdate, "layer(%zu) skip", i);
                continue;
            }
        }
        else if (excp == eDamageRegionFull) {
            damageRect.left = mLayers[i]->mDisplayFrame.left;
            damageRect.top = mLayers[i]->mDisplayFrame.top;
            damageRect.right = mLayers[i]->mDisplayFrame.left + mLayers[i]->mDisplayFrame.right;
            damageRect.bottom = mLayers[i]->mDisplayFrame.top + mLayers[i]->mDisplayFrame.bottom;
            DISPLAY_LOGD(eDebugWindowUpdate, "Full layer update : %d, %d, %d, %d", mLayers[i]->mDisplayFrame.left,
                    mLayers[i]->mDisplayFrame.top, mLayers[i]->mDisplayFrame.right, mLayers[i]->mDisplayFrame.bottom);
            mergedRect = expand(mergedRect, damageRect);
        }
        else {
            DISPLAY_LOGD(eDebugWindowUpdate, "Partial canceled, Skip reason (layer %zu) : %d", i, excp);
            return 0;
        }
    }

    if (mergedRect.left != (int32_t)mXres || mergedRect.right != 0 ||
        mergedRect.top != (int32_t)mYres || mergedRect.bottom != 0) {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial(origin) : %d, %d, %d, %d",
                mergedRect.left, mergedRect.top, mergedRect.right, mergedRect.bottom);
    } else {
        return 0;
    }

    unsigned int blockWidth, blockHeight;

    if (mDSCHSliceNum != 0 && mDSCYSliceSize != 0) {
        blockWidth = mXres/mDSCHSliceNum;
        blockHeight = mDSCYSliceSize;
    } else {
        blockWidth = 2;
        blockHeight = 2;
    }

    DISPLAY_LOGD(eDebugWindowUpdate, "DSC block size (for align) : %d, %d", blockWidth, blockHeight);

    if (mergedRect.left%blockWidth != 0)
        mergedRect.left = pixel_align_down(mergedRect.left, blockWidth);
    if (mergedRect.left < 0) mergedRect.left = 0;

    if (mergedRect.right%blockWidth != 0)
        mergedRect.right = pixel_align(mergedRect.right, blockWidth);
    if (mergedRect.right > (int32_t)mXres) mergedRect.right = mXres;

    if (mergedRect.top%blockHeight != 0)
        mergedRect.top = pixel_align_down(mergedRect.top, blockHeight);
    if (mergedRect.top < 0) mergedRect.top = 0;

    if (mergedRect.bottom%blockHeight != 0)
        mergedRect.bottom = pixel_align(mergedRect.bottom, blockHeight);
    if (mergedRect.bottom > (int32_t)mYres) mergedRect.bottom = mYres;

    if (mergedRect.left == 0 && mergedRect.right == (int32_t)mXres &&
            mergedRect.top == 0 && mergedRect.bottom == (int32_t)mYres) {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial(aligned) : Full size");
        mDpuData.enable_win_update = true;
        mDpuData.win_update_region.x = 0;
        mDpuData.win_update_region.w = mXres;
        mDpuData.win_update_region.y = 0;
        mDpuData.win_update_region.h = mYres;
        DISPLAY_LOGD(eDebugWindowUpdate, "window update end ------------------");
        return 0;
    }

    if (mergedRect.left != (int32_t)mXres && mergedRect.right != 0 &&
            mergedRect.top != (int32_t)mYres && mergedRect.bottom != 0) {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial(aligned) : %d, %d, %d, %d",
                mergedRect.left, mergedRect.top, mergedRect.right, mergedRect.bottom);

        mDpuData.enable_win_update = true;
        mDpuData.win_update_region.x = mergedRect.left;
        mDpuData.win_update_region.w = WIDTH(mergedRect);
        mDpuData.win_update_region.y = mergedRect.top;
        mDpuData.win_update_region.h = HEIGHT(mergedRect);
    }
    else {
        DISPLAY_LOGD(eDebugWindowUpdate, "Partial canceled, All layer skiped" );
    }

    DISPLAY_LOGD(eDebugWindowUpdate, "window update end ------------------");
    return 0;
}

unsigned int ExynosDisplay::getLayerRegion(ExynosLayer *layer, hwc_rect *rect_area, uint32_t regionType) {

    android::Vector <hwc_rect_t> hwcRects;
    size_t numRects = 0;

    rect_area->left = INT_MAX;
    rect_area->top = INT_MAX;
    rect_area->right = rect_area->bottom = 0;

    hwcRects = layer->mDamageRects;
    numRects = layer->mDamageNum;

    if ((numRects == 0) || (hwcRects.size() == 0))
        return eDamageRegionFull;

    if ((numRects == 1) && (hwcRects[0].left == 0) && (hwcRects[0].top == 0) &&
            (hwcRects[0].right == 0) && (hwcRects[0].bottom == 0))
        return eDamageRegionSkip;

    switch (regionType) {
    case eDamageRegionByDamage:
        for (size_t j = 0; j < hwcRects.size(); j++) {
            hwc_rect_t rect;

            if ((hwcRects[j].left < 0) || (hwcRects[j].top < 0) ||
                    (hwcRects[j].right < 0) || (hwcRects[j].bottom < 0) ||
                    (hwcRects[j].left >= hwcRects[j].right) || (hwcRects[j].top >= hwcRects[j].bottom) ||
                    (hwcRects[j].right - hwcRects[j].left > WIDTH(layer->mSourceCrop)) ||
                    (hwcRects[j].bottom - hwcRects[j].top > HEIGHT(layer->mSourceCrop))) {
                rect_area->left = INT_MAX;
                rect_area->top = INT_MAX;
                rect_area->right = rect_area->bottom = 0;
                return eDamageRegionFull;
            }

            rect.left = layer->mDisplayFrame.left + hwcRects[j].left - layer->mSourceCrop.left;
            rect.top = layer->mDisplayFrame.top + hwcRects[j].top - layer->mSourceCrop.top;
            rect.right = layer->mDisplayFrame.left + hwcRects[j].right - layer->mSourceCrop.left;
            rect.bottom = layer->mDisplayFrame.top + hwcRects[j].bottom - layer->mSourceCrop.top;
            DISPLAY_LOGD(eDebugWindowUpdate, "Display frame : %d, %d, %d, %d", layer->mDisplayFrame.left,
                    layer->mDisplayFrame.top, layer->mDisplayFrame.right, layer->mDisplayFrame.bottom);
            DISPLAY_LOGD(eDebugWindowUpdate, "hwcRects : %d, %d, %d, %d", hwcRects[j].left,
                    hwcRects[j].top, hwcRects[j].right, hwcRects[j].bottom);
            adjustRect(rect, INT_MAX, INT_MAX);
            /* Get sums of rects */
            *rect_area = expand(*rect_area, rect);
        }
        return eDamageRegionPartial;
        break;
    case eDamageRegionByLayer:
        if (layer->mLastLayerBuffer != layer->mLayerBuffer)
            return eDamageRegionFull;
        else
            return eDamageRegionSkip;
        break;
    default:
        HWC_LOGE(this, "%s:: Invalid regionType (%d)", __func__, regionType);
        return eDamageRegionError;
        break;
    }

    return eDamageRegionFull;
}

uint32_t ExynosDisplay::getRestrictionIndex(int halFormat)
{
    if (isFormatRgb(halFormat))
        return RESTRICTION_RGB;
    else
        return RESTRICTION_YUV;
}

void ExynosDisplay::closeFencesForSkipFrame(rendering_state renderingState)
{
    for (size_t i=0; i < mLayers.size(); i++) {
        if (mLayers[i]->mAcquireFence != -1) {
            mLayers[i]->mAcquireFence = fence_close(mLayers[i]->mAcquireFence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
        }
    }

    if (mDpuData.readback_info.rel_fence >= 0) {
        mDpuData.readback_info.rel_fence =
            fence_close(mDpuData.readback_info.rel_fence, this,
                    FENCE_TYPE_READBACK_RELEASE, FENCE_IP_FB);
    }
    if (mDpuData.readback_info.acq_fence >= 0) {
        mDpuData.readback_info.acq_fence =
            fence_close(mDpuData.readback_info.acq_fence, this,
                    FENCE_TYPE_READBACK_ACQUIRE, FENCE_IP_DPP);
    }

    if (renderingState >= RENDERING_STATE_VALIDATED) {
        if (mDisplayControl.earlyStartMPP == true) {
            if (mExynosCompositionInfo.mHasCompositionLayer) {
                /*
                 * m2mMPP's release fence for dst buffer was set to
                 * mExynosCompositionInfo.mAcquireFence by startPostProcessing()
                 * in validate time.
                 * This fence should be passed to display driver
                 * but it wont't because this frame will not be presented.
                 * So fence should be closed.
                 */
                mExynosCompositionInfo.mAcquireFence = fence_close(mExynosCompositionInfo.mAcquireFence,
                        this, FENCE_TYPE_DST_RELEASE, FENCE_IP_G2D);
            }

            for (size_t i = 0; i < mLayers.size(); i++) {
                exynos_image outImage;
                ExynosMPP* m2mMPP = mLayers[i]->mM2mMPP;
                if ((mLayers[i]->mValidateCompositionType == HWC2_COMPOSITION_DEVICE) &&
                    (m2mMPP != NULL) &&
                    (m2mMPP->mAssignedDisplay == this) &&
                    (m2mMPP->getDstImageInfo(&outImage) == NO_ERROR)) {
                    if (m2mMPP->mPhysicalType == MPP_MSC) {
                        fence_close(outImage.acquireFenceFd, this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_MSC);
                    } else if (m2mMPP->mPhysicalType == MPP_G2D) {
                        ALOGD("close(%d)", outImage.releaseFenceFd);
                        fence_close(outImage.acquireFenceFd, this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_G2D);
                    } else {
                        DISPLAY_LOGE("[%zu] layer has invalid mppType(%d)", i, m2mMPP->mPhysicalType);
                        fence_close(outImage.acquireFenceFd, this, FENCE_TYPE_DST_ACQUIRE, FENCE_IP_ALL);
                    }
                    m2mMPP->resetDstAcquireFence();
                    ALOGD("reset buf[%d], %d", m2mMPP->mCurrentDstBuf,
                            m2mMPP->mDstImgs[m2mMPP->mCurrentDstBuf].acrylicReleaseFenceFd);
                }
            }
        }
    }

    if (renderingState >= RENDERING_STATE_PRESENTED) {
        /* mAcquireFence is set after validate */
        mClientCompositionInfo.mAcquireFence = fence_close(mClientCompositionInfo.mAcquireFence, this,
                FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);
    }
}
void ExynosDisplay::closeFences()
{
    for (size_t i = 0; i < mDpuData.configs.size(); i++) {
        if (mDpuData.configs[i].acq_fence != -1)
            fence_close(mDpuData.configs[i].acq_fence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_DPP);
        mDpuData.configs[i].acq_fence = -1;
        if (mDpuData.configs[i].rel_fence >= 0)
            fence_close(mDpuData.configs[i].rel_fence, this,
                    FENCE_TYPE_SRC_RELEASE, FENCE_IP_DPP);
        mDpuData.configs[i].rel_fence = -1;
    }
    for (size_t i = 0; i < mLayers.size(); i++) {
        if (mLayers[i]->mReleaseFence > 0) {
            fence_close(mLayers[i]->mReleaseFence, this,
                    FENCE_TYPE_SRC_RELEASE, FENCE_IP_LAYER);
            mLayers[i]->mReleaseFence = -1;
        }
        if ((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_DEVICE) &&
            (mLayers[i]->mM2mMPP != NULL)) {
            mLayers[i]->mM2mMPP->closeFences();
        }
    }
    if (mExynosCompositionInfo.mHasCompositionLayer) {
        if (mExynosCompositionInfo.mM2mMPP == NULL)
        {
            DISPLAY_LOGE("There is exynos composition, but m2mMPP is NULL");
            return;
        }
        mExynosCompositionInfo.mM2mMPP->closeFences();
    }

    for (size_t i=0; i < mLayers.size(); i++) {
        if (mLayers[i]->mAcquireFence != -1) {
            mLayers[i]->mAcquireFence = fence_close(mLayers[i]->mAcquireFence, this,
                    FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_LAYER);
        }
    }

    mExynosCompositionInfo.mAcquireFence = fence_close(mExynosCompositionInfo.mAcquireFence, this,
            FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_G2D);
    mClientCompositionInfo.mAcquireFence = fence_close(mClientCompositionInfo.mAcquireFence, this,
            FENCE_TYPE_SRC_ACQUIRE, FENCE_IP_FB);

    if (mDpuData.retire_fence > 0)
        fence_close(mDpuData.retire_fence, this, FENCE_TYPE_RETIRE, FENCE_IP_DPP);
    mDpuData.retire_fence = -1;

    mLastRetireFence = fence_close(mLastRetireFence, this,  FENCE_TYPE_RETIRE, FENCE_IP_DPP);

    if (mDpuData.readback_info.rel_fence >= 0) {
        mDpuData.readback_info.rel_fence =
            fence_close(mDpuData.readback_info.rel_fence, this,
                    FENCE_TYPE_READBACK_RELEASE, FENCE_IP_FB);
    }
    if (mDpuData.readback_info.acq_fence >= 0) {
        mDpuData.readback_info.acq_fence =
            fence_close(mDpuData.readback_info.acq_fence, this,
                    FENCE_TYPE_READBACK_ACQUIRE, FENCE_IP_DPP);
    }
}

void ExynosDisplay::setHWCControl(uint32_t ctrl, int32_t val)
{
    switch (ctrl) {
        case HWC_CTL_ENABLE_COMPOSITION_CROP:
            mDisplayControl.enableCompositionCrop = (unsigned int)val;
            break;
        case HWC_CTL_ENABLE_EXYNOSCOMPOSITION_OPT:
            mDisplayControl.enableExynosCompositionOptimization = (unsigned int)val;
            break;
        case HWC_CTL_ENABLE_CLIENTCOMPOSITION_OPT:
            mDisplayControl.enableClientCompositionOptimization = (unsigned int)val;
            break;
        case HWC_CTL_USE_MAX_G2D_SRC:
            mDisplayControl.useMaxG2DSrc = (unsigned int)val;
            break;
        case HWC_CTL_ENABLE_HANDLE_LOW_FPS:
            mDisplayControl.handleLowFpsLayers = (unsigned int)val;
            break;
        case HWC_CTL_ENABLE_EARLY_START_MPP:
            mDisplayControl.earlyStartMPP = (unsigned int)val;
            break;
        default:
            ALOGE("%s: unsupported HWC_CTL (%d)", __func__, ctrl);
            break;
    }
}

int32_t ExynosDisplay::getHdrCapabilities(uint32_t* outNumTypes,
        int32_t* outTypes, float* outMaxLuminance,
        float* outMaxAverageLuminance, float* outMinLuminance)
{
    DISPLAY_LOGD(eDebugHWC, "HWC2: %s, %d", __func__, __LINE__);
    return mDisplayInterface->getHdrCapabilities(outNumTypes, outTypes,
                    outMaxLuminance, outMaxAverageLuminance, outMinLuminance);
}

// Support DDI scalser
void ExynosDisplay::setDDIScalerEnable(int __unused width, int __unused height) {
}

int ExynosDisplay::getDDIScalerMode(int __unused width, int __unused height) {
    return 1; // WQHD
}

void ExynosDisplay::increaseMPPDstBufIndex() {
    for (size_t i=0; i < mLayers.size(); i++) {
        if((mLayers[i]->mExynosCompositionType == HWC2_COMPOSITION_DEVICE) &&
           (mLayers[i]->mM2mMPP != NULL)) {
            mLayers[i]->mM2mMPP->increaseDstBuffIndex();
        }
    }

    if ((mExynosCompositionInfo.mHasCompositionLayer) &&
        (mExynosCompositionInfo.mM2mMPP != NULL)) {
        mExynosCompositionInfo.mM2mMPP->increaseDstBuffIndex();
    }
}

int32_t ExynosDisplay::getReadbackBufferAttributes(int32_t* /*android_pixel_format_t*/ outFormat,
        int32_t* /*android_dataspace_t*/ outDataspace)
{
    int32_t ret = mDisplayInterface->getReadbackBufferAttributes(outFormat, outDataspace);
    if (ret == NO_ERROR) {
        mDisplayControl.readbackSupport = true;
        ALOGI("readback info: format(0x%8x), dataspace(0x%8x)", *outFormat, *outDataspace);
    } else {
        mDisplayControl.readbackSupport = false;
        ALOGI("readback is not supported, ret(%d)", ret);
        ret = HWC2_ERROR_UNSUPPORTED;
    }
    return ret;
}

int32_t ExynosDisplay::setReadbackBuffer(buffer_handle_t buffer, int32_t releaseFence)
{
    int32_t ret = NO_ERROR;

    if (buffer == nullptr)
        return HWC2_ERROR_BAD_PARAMETER;

    if (mDisplayControl.readbackSupport) {
        mDpuData.enable_readback = true;
    } else {
        DISPLAY_LOGE("readback is not supported but setReadbackBuffer is called, buffer(%p), releaseFence(%d)",
                buffer, releaseFence);
        if (releaseFence >= 0)
            releaseFence = fence_close(releaseFence, this,
                    FENCE_TYPE_READBACK_RELEASE, FENCE_IP_FB);
        mDpuData.enable_readback = false;
        ret = HWC2_ERROR_UNSUPPORTED;
    }
    setReadbackBufferInternal(buffer, releaseFence);
    return ret;
}

void ExynosDisplay::setReadbackBufferInternal(buffer_handle_t buffer, int32_t releaseFence)
{
    if (mDpuData.readback_info.rel_fence >= 0) {
        mDpuData.readback_info.rel_fence =
            fence_close(mDpuData.readback_info.rel_fence, this,
                    FENCE_TYPE_READBACK_RELEASE, FENCE_IP_FB);
        DISPLAY_LOGE("previous readback release fence is not delivered to display device");
    }
    if (releaseFence >= 0) {
        setFenceInfo(releaseFence, this, FENCE_TYPE_READBACK_RELEASE,
                FENCE_IP_FB, FENCE_FROM);
    }
    mDpuData.readback_info.rel_fence = releaseFence;

    private_handle_t *handle = NULL;
    if (buffer != NULL)
        handle = private_handle_t::dynamicCast(buffer);
    mDpuData.readback_info.handle = handle;
}

int32_t ExynosDisplay::getReadbackBufferFence(int32_t* outFence)
{
    /*
     * acq_fence was not set or
     * it was already closed by error or frame skip
     */
    if (mDpuData.readback_info.acq_fence < 0) {
        *outFence = -1;
        return HWC2_ERROR_UNSUPPORTED;
    }

    *outFence = mDpuData.readback_info.acq_fence;
    /* Fence will be closed by caller of this function */
    mDpuData.readback_info.acq_fence = -1;
    return NO_ERROR;
}

int32_t ExynosDisplay::setReadbackBufferAcqFence(int32_t acqFence) {
    if (mDpuData.readback_info.acq_fence >= 0) {
        mDpuData.readback_info.acq_fence =
            fence_close(mDpuData.readback_info.acq_fence, this,
                    FENCE_TYPE_READBACK_ACQUIRE, FENCE_IP_DPP);
        DISPLAY_LOGE("previous readback out fence is not delivered to framework");
    }
    mDpuData.readback_info.acq_fence = acqFence;
    if (acqFence >= 0) {
        /*
         * Requtester of readback will get acqFence after presentDisplay
         * so validateFences should not check this fence
         * in presentDisplay so this function sets pendingAllowed parameter.
         */
        setFenceInfo(acqFence, this, FENCE_TYPE_READBACK_ACQUIRE,
                FENCE_IP_DPP, FENCE_FROM, true);
    }

    return NO_ERROR;
}

void ExynosDisplay::initDisplayInterface(uint32_t __unused interfaceType)
{
    mDisplayInterface = new ExynosDisplayInterface();
    mDisplayInterface->init(this);
}

void ExynosDisplay::setDumpCount(uint32_t dumpCount)
{
    mDumpCount = dumpCount;
}

void ExynosDisplay::getDumpLayer()
{
    void *temp = NULL;
    size_t bufLength[4];

    DISPLAY_LOGD(eDebugHWC,"debug_dump_source %s dumpCount=%d mDisplayId=%d", __func__, mDumpCount, mDisplayId);

    //virtual and external check whether connection

    for (size_t i = 0; i < mLayers.size(); i++) {
        private_handle_t *hnd = mLayers[i]->mLayerBuffer;

        if (!hnd) {
            ALOGE("%s: [%zu] handle is NULL", __func__, i);
            continue;
        }

        if (fence_valid(mLayers[i]->mAcquireFence)) {
            if (sync_wait(mLayers[i]->mAcquireFence, 1000) < 0) {
                ALOGE("debug_dump_source %s:: [%zu] sync wait failed", __func__, i);
                return;
            }
        }

        if (getBufLength(hnd, 4, bufLength, hnd->format, hnd->stride, hnd->vstride) != NO_ERROR) {
            ALOGE("debug_dump_source %s:: invalid bufferLength(%zu, %zu, %zu), format(0x%8x)", __func__,
                    bufLength[0], bufLength[1], bufLength[2], hnd->format);
            return;
        }

        writeDumpData(hnd, bufLength, i, temp);
    }

    mDumpCount--;
    if (mDumpCount == 0) {
        uint32_t dumpedIdx = mDevice->mTotalDumpCount - mDumpCount + 1;
        DISPLAY_LOGD(eDebugHWC, "debug_dump_source finished dump file index=%d", dumpedIdx);
    }
}

void ExynosDisplay::writeDumpData(private_handle_t *hnd, size_t bufLength[], size_t i, void *temp){
    FILE *fp = NULL;
    size_t result = 0;
    char filePath[MAX_DEV_NAME];
    uint32_t bufferNum = 0;
    uint32_t layer_type = 0;
    int bufFds[4];
    uint32_t currentDumpIdx;

    bufFds[0] = hnd->fd;
    bufFds[1] = hnd->fd1;
    bufFds[2] = hnd->fd2;
    bufferNum = getBufferNumOfFormat(hnd->format);
    layer_type = getTypeOfFormat(hnd->format);
    currentDumpIdx = mDevice->mTotalDumpCount - mDumpCount + 1;


    DISPLAY_LOGD(eDebugHWC, "%s::debug_dump_source fd=%d, fd1=%d, fd2=%d size=%zu size1=%zu size2=%zu"
            "layer_index=%zu srcHandle->size=%d srcHandle->size1=%d srcHandle->size2=%d srcHandle->fd=%d srcHandle->fd1=%d srcHandle->fd2=%d bufferNum=%d",
            __func__, bufFds[0], bufFds[1], bufFds[2], bufLength[0], bufLength[1], bufLength[2], i, hnd->size, hnd->size1,
            hnd->size2, hnd->fd, hnd->fd1, hnd->fd2, bufferNum);

    if ((1 == bufferNum) && (layer_type == (RGB|BIT8) || layer_type == (RGB|BIT10) || layer_type == (RGB|UNDEF_BIT))) {
        DISPLAY_LOGD(eDebugHWC, "debug_dump_source rgb data enter");
        sprintf(filePath,"%s/displayid_%u_layer_%zu_%dx%d_format_%d_compressed_%d_comtype_%d_frame_%d.raw", ERROR_LOG_PATH0, mDisplayId,
                i, hnd->width, hnd->height, hnd->format, mLayers[i]->mCompressed, mLayers[i]->mCompositionType, currentDumpIdx);
        fp = fopen(filePath, "w+");

        if (fp)
        {
            temp = mmap(0, bufLength[0], PROT_READ|PROT_WRITE, MAP_SHARED, bufFds[0], 0);
            if (temp != MAP_FAILED && temp != NULL)
            {
                result = fwrite(temp, hnd->size, 1, fp);
                munmap(temp, bufLength[0]);
            }
            fclose(fp);
        }
        DISPLAY_LOGD(eDebugHWC, "debug_dump_source Frame Dump %s: is %s result=%s mLayers.size=%zu", filePath, result ? "Successful" : "Failed", result ? "1" : strerror(errno), mLayers.size());
    }

    if ((bufferNum >= 1) && (layer_type == (YUV420|BIT8) || layer_type == (YUV420|BIT10))) {
        sprintf(filePath,"%s/displayid_%u_layer_%zu_%dx%d_format_%d_compressed_%d_comtype_%d_frame_%d.raw", ERROR_LOG_PATH0, mDisplayId,
                i, hnd->width, hnd->height, hnd->format, mLayers[i]->mCompressed, mLayers[i]->mCompositionType, currentDumpIdx);
        //forbit apend buffer when file has exited.so before each dumping,clear file
        fp = fopen(filePath, "w+");
        if (fp) {
            fclose(fp);
        }
        for (uint32_t start =0; start < bufferNum; start++)
        {
            DISPLAY_LOGD(eDebugHWC, "debug_dump_source yuv data enter start_times=%d", start);
            fp = fopen(filePath, "a+");

            if (fp)
            {
                temp = mmap(0, bufLength[start], PROT_READ|PROT_WRITE, MAP_SHARED, bufFds[start], 0);
                if (temp != MAP_FAILED && temp != NULL)
                {
                    int align_width =  hnd->stride;
                    int width = hnd->width;
                    int height =  start == 0? hnd->height: hnd->height/2;
                    if(bufferNum == 3 && start != 0)
                    {
                        height =  start == 0? hnd->height: hnd->height/4;
                    }
                    result = yuvWriteByLines(temp, align_width,width, height, fp);
                    munmap(temp, bufLength[start]);
                }
                fclose(fp);
            }
            DISPLAY_LOGD(eDebugHWC, "debug_dump_source Frame Dump %s: is %s result=%s mLayers.size=%zu", filePath, result ? "Successful" : "Failed", result ? "1" : strerror(errno), mLayers.size());
        }
    }
}

size_t ExynosDisplay::yuvWriteByLines(void * temp, int align_Width, int original_Width, int original_Height, FILE *fp) {
    size_t result = 0;

    DISPLAY_LOGD(eDebugHWC,"debug_dump_source yuvWriteByLines enter align_Width=%d original_Width=%d, original_Height=%d",
            align_Width, original_Width, original_Height);

    for (int h = 0; h < original_Height; h++) {
        result = fwrite((char*)temp+ h * align_Width, original_Width, 1, fp);
    }
    return result;
}

void ExynosDisplay::assignInitialResourceSet() {
    if (SELECTED_LOGIC == UNDEFINED)
        return;
    for (size_t i = 0; i < RESOURCE_SET_COUNT; i++) {
        mAssignedResourceSet.add(&(mResourceManager->mResourceSetTable[i]));
    }
}

void ExynosDisplay::setPresentAndClearRenderingStatesFlags()
{
    int32_t ret = 0;
    mRenderingStateFlags.presentFlag = true;
    if (mDevice->isLastPresent(this)) {
        if ((ret = mResourceManager->finishAssignResourceWork()) != NO_ERROR)
            HWC_LOGE(this, "%s:: finishAssignResourceWork() error (%d)",
                    __func__, ret);

        /*
         * geomegryChanged flag should be cleared in last present
         * not in last validate
         * so that present can return HWC2_ERROR_NOT_VALIDATED
         * if geometry is changed in current frame
         * even if present is called after
         * all of display are validated in first validate time
         */
        mDevice->clearGeometryChanged();
        /*
         * Resource assignment information was initialized during skipping frames
         * so resource assignment for the first displayed frame after skipping frames
         * should not be skipped
         */
        mDevice->setGeometryFlagForSkipFrame();

        mDevice->clearRenderingStateFlags();
    }
}

void ExynosDisplay::requestHiberExit() {

    if ((mHiberState.hiberExitFd == NULL) ||
            (mHiberState.exitRequested)) return;

    uint32_t val;
    size_t size;
    rewind(mHiberState.hiberExitFd);
    size = fread(&val, 4, 1, mHiberState.hiberExitFd);
    HDEBUGLOGD(eDebugWinConfig, "hiber exit read size = %lu", (unsigned long)size);
    mHiberState.exitRequested = true;

    return;
}

void ExynosDisplay::initHiberState() {
    if (mHiberState.hiberExitFd == NULL) return;
    mHiberState.exitRequested = false;
    return;
}
