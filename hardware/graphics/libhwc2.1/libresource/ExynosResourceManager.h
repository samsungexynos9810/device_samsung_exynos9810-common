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

#ifndef _EXYNOSRESOURCEMANAGER_H
#define _EXYNOSRESOURCEMANAGER_H

#include "ExynosDevice.h"
#include "ExynosDisplay.h"
#include "ExynosHWCHelper.h"
#include "ExynosMPPModule.h"

using namespace android;

class ExynosDevice;
class ExynosDisplay;
class ExynosMPP;

#define ASSIGN_RESOURCE_TRY_COUNT   100
#define M2M_MPP_OUT_IMAGS_COUNT     3

#define MAX_OVERLAY_LAYER_NUM       20

#ifndef MAX_ENABLED_DISPLAY_COUNT
#define MAX_ENABLED_DISPLAY_COUNT 2 // Maximum number of display that can be supported at once.
#endif

#ifndef NUM_OF_DISPLAY_NEED_OTFMPP
#define NUM_OF_DISPLAY_NEED_OTFMPP 1 // Number of display that need OtfMPP
#endif

/* Basic structure of resource table that define the number of resource set per display*/
struct resource_table_t {
    uint32_t connection_state; // 0b'DISP2''DISP1''DISP0'
    uint32_t setcnt[NUM_OF_DISPLAY_NEED_OTFMPP];
};

/* List of logic that used to fill table */
enum {
    UNDEFINED = 0,
    DEFAULT_LOGIC = 1,
};

typedef uint32_t displayEnableMap_t;

#ifndef SELECTED_LOGIC
#define SELECTED_LOGIC UNDEFINED
#endif

/* Based on multi-resolution feature */
enum dst_realloc_state {
    DST_REALLOC_DONE = 0,
    DST_REALLOC_START,
    DST_REALLOC_GOING,
};

class ExynosMPPVector : public android::SortedVector< ExynosMPP* > {
    public:
        ExynosMPPVector();
        ExynosMPPVector(const ExynosMPPVector& rhs);
        virtual int do_compare(const void* lhs, const void* rhs) const;
};

class ExynosResourceManager {
    private:
    class DstBufMgrThread: public Thread {
        private:
            ExynosResourceManager *mExynosResourceManager;
            Condition mCondition;
        public:
            bool mRunning;
            Mutex mMutex;
            Mutex mStateMutex;
            Mutex mResInfoMutex;
            uint32_t mBufXres;
            uint32_t mBufYres;
            void reallocDstBufs(uint32_t Xres, uint32_t Yres);
            bool needDstRealloc(uint32_t Xres, uint32_t Yres, ExynosMPP *m2mMPP);
            DstBufMgrThread(ExynosResourceManager *exynosResourceManager);
            ~DstBufMgrThread();
            virtual bool threadLoop();
    };

    public:
        uint32_t mForceReallocState;
        ExynosDevice *mDevice;
        bool hasHdrLayer;
        bool hasDrmLayer;

        uint32_t mFormatRestrictionCnt;
        uint32_t mSizeRestrictionCnt[RESTRICTION_MAX];
        restriction_key_t mFormatRestrictions[RESTRICTION_CNT_MAX];
        restriction_size_element_t mSizeRestrictions[RESTRICTION_MAX][RESTRICTION_CNT_MAX];

        android::Vector<uint32_t> mLayerAttributePriority;
        std::unordered_map<uint32_t /* physical type */, uint64_t /* attribute */> mMPPAttrs;

        ExynosResourceManager(ExynosDevice *device);
        virtual ~ExynosResourceManager();
        void reloadResourceForHWFC();
        void setTargetDisplayLuminance(uint16_t min, uint16_t max);
        void setTargetDisplayDevice(int device);
        int32_t doPreProcessing();
        void doReallocDstBufs(uint32_t Xres, uint32_t Yres);
        int32_t doAllocDstBufs(uint32_t mXres, uint32_t mYres);
        int32_t assignResource(ExynosDisplay *display);
        int32_t assignResourceInternal(ExynosDisplay *display);
        static ExynosMPP* getExynosMPP(uint32_t type);
        static ExynosMPP* getExynosMPP(uint32_t physicalType, uint32_t physicalIndex);
        ExynosMPP* getExynosMPPForBlending(uint32_t displayId);
        static void enableMPP(uint32_t physicalType, uint32_t physicalIndex, uint32_t logicalIndex, uint32_t enable);
        int32_t updateSupportedMPPFlag(ExynosDisplay * display);
        int32_t resetResources();
        virtual int32_t preAssignResources();
        virtual int32_t distributeResourceSet();
        virtual int32_t moveResourceSet(uint32_t* prevSetCnt, uint32_t* currSetCnt);
        virtual void moveOneResourceSet(size_t index, int32_t* needSetCnt,
                uint32_t takingIndex, uint32_t givingIndex,
                ExynosDisplay* takingDisplay, ExynosDisplay* givingDisplay);
        virtual void preAssignWindows();
        int32_t preProcessLayer(ExynosDisplay *display);
        int32_t resetAssignedResources(ExynosDisplay *display, bool forceReset = false);
        virtual int32_t assignCompositionTarget(ExynosDisplay *display, uint32_t targetType);
        int32_t validateLayer(uint32_t index, ExynosDisplay *display, ExynosLayer *layer);
        int32_t assignLayers(ExynosDisplay *display, uint32_t priority);
        virtual int32_t assignLayer(ExynosDisplay *display, ExynosLayer *layer, uint32_t layer_index,
                exynos_image &m2m_out_img, ExynosMPP **m2mMPP, ExynosMPP **otfMPP, uint32_t &overlayInfo);

        /* If product needs specific assign policy, describe at their module codes */
        virtual int32_t checkExceptionScenario(ExynosDisplay __unused *display) { return NO_ERROR; };

        virtual int32_t assignWindow(ExynosDisplay *display);
        int32_t updateResourceState();
        static float getResourceUsedCapa(ExynosMPP &mpp);
        void printDebugInfo(ExynosDisplay *display);
        int32_t updateExynosComposition(ExynosDisplay *display);
        int32_t updateClientComposition(ExynosDisplay *display);
        int32_t getCandidateM2mMPPOutImages(ExynosDisplay *display,
                ExynosLayer *layer, uint32_t *imageNum, exynos_image *image_lists);
        int32_t setResourcePriority(ExynosDisplay *display);
        int32_t deliverPerformanceInfo(ExynosDisplay *display);
        int32_t prepareResources();
        int32_t finishAssignResourceWork();
        int32_t initResourcesState(ExynosDisplay *display);

        uint32_t getOtfMPPSize() {return (uint32_t)mOtfMPPs.size();};
        ExynosMPP* getOtfMPP(uint32_t index) {return mOtfMPPs[index];};

        uint32_t getM2mMPPSize() {return (uint32_t)mM2mMPPs.size();};
        ExynosMPP* getM2mMPP(uint32_t index) {return mM2mMPPs[index];};

        virtual void makeAcrylRestrictions(mpp_phycal_type_t type);
        void makeSizeRestrictions(uint32_t mppId, restriction_size_t size, restriction_classification_t format);
        void makeFormatRestrictions(restriction_key_t table, int deviceFormat);

        void updateRestrictions();

        mpp_phycal_type_t getPhysicalType(int ch);
        uint32_t getFeatureTableSize();
        const ExynosMPPVector& getOtfMPPs() { return mOtfMPPs; };

        virtual void generateResourceTable();
        void updateTableEvenly(uint32_t tableSize);
        android::Vector< resource_table_t > mResourceAssignTable;
        resource_set_t mResourceSetTable[RESOURCE_SET_COUNT];

        virtual bool assignDisplayList(android::Vector<ExynosDisplay*> displayList, uint32_t attribute);
        virtual bool checkLayerAttribute(ExynosDisplay *display, ExynosLayer *layer, size_t type);
        virtual bool isProcessingWithInternals(ExynosDisplay *display, exynos_image *src_img, exynos_image *dst_img);
        virtual int setPreAssignedCapacity(ExynosDisplay *display, ExynosLayer *layer, exynos_image *src_img, exynos_image *dst_img, mpp_phycal_type_t type);
        virtual void preAssignM2MResources();
        float getAssignedCapacity(uint32_t physicalType);

        void setM2MCapa(uint32_t physicalType, uint32_t capa);

        virtual bool checkAlreadyReservedDisplay(uint32_t logicalType, uint32_t displayId);
        virtual bool hasHDR10PlusMPP();
        virtual void setM2mTargetCompression();

    private:
        uint32_t mUseDpuDisplayNum = 0;
        displayEnableMap_t mDisplayEnableState = 1;
        int32_t changeLayerFromClientToDevice(ExynosDisplay *display, ExynosLayer *layer,
                uint32_t layer_index, exynos_image m2m_out_img, ExynosMPP *m2mMPP, ExynosMPP *otfMPP);

        int32_t setDstAllocSize(uint32_t width);
        sp<DstBufMgrThread> mDstBufMgrThread;

    protected:
        virtual void setFrameRateForPerformance(ExynosMPP &mpp, AcrylicPerformanceRequestFrame *frame);
        static ExynosMPPVector mOtfMPPs;
        static ExynosMPPVector mM2mMPPs;
};

#endif //_EXYNOSRESOURCEMANAGER_H
