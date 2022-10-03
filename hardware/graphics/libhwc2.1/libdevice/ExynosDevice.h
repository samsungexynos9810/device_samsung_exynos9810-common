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

#ifndef _EXYNOSDEVICE_H
#define _EXYNOSDEVICE_H

#include <unistd.h>
#include <hardware_legacy/uevent.h>

#include <utils/Vector.h>
#include <utils/Trace.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <cutils/atomic.h>
#include <unordered_map>

#include <hardware/hwcomposer2.h>

#include "ExynosHWC.h"
#include "ExynosHWCModule.h"
#include "ExynosHWCHelper.h"

#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#else
#include "gralloc_priv.h"
#endif
#include "GrallocWrapper.h"

#define MAX_DEV_NAME 128
#define ERROR_LOG_PATH0 "/data/vendor/log/hwc"
#define ERROR_LOG_PATH1 "/data/log"
#define ERR_LOG_SIZE    (1024*1024)     // 1MB
#define FENCE_ERR_LOG_SIZE    (1024*1024)     // 1MB

#ifndef DOZE_VSYNC_PERIOD
#define DOZE_VSYNC_PERIOD 33333333 // 30fps
#endif

namespace android {
namespace GrallocWrapper {
class Mapper;
class Allocator;
}
}

using namespace android;

struct exynos_callback_info_t {
    hwc2_callback_data_t callbackData;
    hwc2_function_pointer_t funcPointer;
};

typedef struct exynos_hwc_control {
    uint32_t forceGpu;
    uint32_t windowUpdate;
    uint32_t forcePanic;
    uint32_t skipStaticLayers;
    uint32_t skipM2mProcessing;
    uint32_t skipResourceAssign;
    uint32_t multiResolution;
    uint32_t dumpMidBuf;
    uint32_t displayMode;
    uint32_t setDDIScaler;
    uint32_t useDynamicRecomp;
    uint32_t skipWinConfig;
    uint32_t skipValidate;
    uint32_t doFenceFileDump;
    uint32_t fenceTracer;
    uint32_t sysFenceLogging;
} exynos_hwc_control_t;

typedef struct update_time_info {
    struct timeval lastUeventTime;
    struct timeval lastEnableVsyncTime;
    struct timeval lastDisableVsyncTime;
    struct timeval lastValidateTime;
    struct timeval lastPresentTime;
} update_time_info_t;

enum {
    GEOMETRY_LAYER_TYPE_CHANGED             = 1ULL << 0,
    GEOMETRY_LAYER_DATASPACE_CHANGED        = 1ULL << 1,
    GEOMETRY_LAYER_DISPLAYFRAME_CHANGED     = 1ULL << 2,
    GEOMETRY_LAYER_SOURCECROP_CHANGED       = 1ULL << 3,
    GEOMETRY_LAYER_TRANSFORM_CHANGED        = 1ULL << 4,
    GEOMETRY_LAYER_ZORDER_CHANGED           = 1ULL << 5,
    GEOMETRY_LAYER_FPS_CHANGED              = 1ULL << 6,
    GEOMETRY_LAYER_FLAG_CHANGED             = 1ULL << 7,
    GEOMETRY_LAYER_PRIORITY_CHANGED         = 1ULL << 8,
    GEOMETRY_LAYER_COMPRESSED_CHANGED       = 1ULL << 9,
    GEOMETRY_LAYER_BLEND_CHANGED            = 1ULL << 10,
    GEOMETRY_LAYER_FORMAT_CHANGED           = 1ULL << 11,
    GEOMETRY_LAYER_DRM_CHANGED              = 1ULL << 12,
    GEOMETRY_LAYER_UNKNOWN_CHANGED          = 1ULL << 13,
    /* 1ULL << 14 */
    /* 1ULL << 15 */
    /* 1ULL << 16 */
    /* 1ULL << 17 */
    /* 1ULL << 18 */
    /* 1ULL << 19 */
    GEOMETRY_DISPLAY_LAYER_ADDED            = 1ULL << 20,
    GEOMETRY_DISPLAY_LAYER_REMOVED          = 1ULL << 21,
    GEOMETRY_DISPLAY_CONFIG_CHANGED         = 1ULL << 22,
    GEOMETRY_DISPLAY_RESOLUTION_CHANGED     = 1ULL << 23,
    GEOMETRY_DISPLAY_SINGLEBUF_CHANGED      = 1ULL << 24,
    GEOMETRY_DISPLAY_FORCE_VALIDATE         = 1ULL << 25,
    GEOMETRY_DISPLAY_COLOR_MODE_CHANGED     = 1ULL << 26,
    GEOMETRY_DISPLAY_DYNAMIC_RECOMPOSITION  = 1ULL << 27,
    GEOMETRY_DISPLAY_POWER_ON               = 1ULL << 28,
    GEOMETRY_DISPLAY_POWER_OFF              = 1ULL << 29,
    GEOMETRY_DISPLAY_COLOR_TRANSFORM_CHANGED= 1ULL << 30,
    GEOMETRY_DISPLAY_DATASPACE_CHANGED      = 1ULL << 31,
    GEOMETRY_DISPLAY_FRAME_SKIPPED          = 1ULL << 32,
    GEOMETRY_DISPLAY_ADJUST_SIZE_CHANGED    = 1ULL << 33,
    /* 1ULL << 34 */
    /* 1ULL << 35 */
    GEOMETRY_DEVICE_DISPLAY_ADDED           = 1ULL << 36,
    GEOMETRY_DEVICE_DISPLAY_REMOVED         = 1ULL << 37,
    GEOMETRY_DEVICE_CONFIG_CHANGED          = 1ULL << 38,
    GEOMETRY_DEVICE_DISP_MODE_CHAGED        = 1ULL << 39,
    GEOMETRY_DEVICE_SCENARIO_CHANGED        = 1ULL << 40,

    GEOMETRY_ERROR_CASE                     = 1ULL << 63,
};


class ExynosDevice;
class ExynosDisplay;
class ExynosResourceManager;
class ExynosDeviceInterface;

class ExynosDevice {
    public:
        /**
         * TODO : Should be defined as ExynosDisplay type
         * Display list that managed by Device.
         */
        android::Vector< ExynosDisplay* > mDisplays;

        int mNumVirtualDisplay;

        /**
         * Resource manager object that is used to manage HW resources and assign resources to each layers
         */
        ExynosResourceManager *mResourceManager;

        static GrallocWrapper::Mapper* mMapper;
        static GrallocWrapper::Allocator*  mAllocator;

        /**
         * Geometry change will be saved by bit map.
         * ex) Display create/destory.
         */
        uint64_t mGeometryChanged;

        /**
         * If Panel has not self-refresh feature, dynamic recomposition will be enabled.
         */
        pthread_t mDRThread;
        volatile int32_t mDRThreadStatus;
        bool mDRLoopStatus;
        bool mPrimaryBlank;

        bool isBootFinished;

        /**
         * Callback informations those are used by SurfaceFlinger.
         * - VsyncCallback: Vsync detect callback.
         * - RefreshCallback: Callback by refresh request from HWC.
         * - HotplugCallback: Hot plug event by new display hardware.
         */

        /** TODO : Array size shuld be checked
         * TODO : Is HWC2_CALLBACK_VSYNC max?
         */
        exynos_callback_info_t mCallbackInfos[HWC2_CALLBACK_VSYNC + 1];

        /**
         * mDisplayId of display that has the slowest fps.
         * HWC uses vsync of display that has the slowest fps to main vsync.
         */
        uint32_t mVsyncDisplayId;

        uint64_t mTimestamp;
        uint32_t mDisplayMode;

        uint32_t mTotalDumpCount;
        bool mIsDumpRequest;

        // Variable for fence tracer
        std::unordered_map<int32_t, hwc_fence_info> mFenceInfo;

        /**
         * This will be initialized with differnt class
         * that inherits ExynosDeviceInterface according to
         * interface type.
         */
        ExynosDeviceInterface *mDeviceInterface;

        // Con/Destructors
        ExynosDevice();
        virtual ~ExynosDevice();

        bool isFirstValidate(ExynosDisplay *display);
        bool isLastValidate(ExynosDisplay *display);
        bool isLastPresent(ExynosDisplay *display);

        /**
         * @param outSize
         * @param * outBuffer
         */

        void dynamicRecompositionThreadCreate();
        static void* dynamicRecompositionThreadLoop(void *data);


        /**
         * @param display
         */
        ExynosDisplay* getDisplay(uint32_t display);

        /**
         * Device Functions for HWC 2.0
         */

        /**
         * Descriptor: HWC2_FUNCTION_CREATE_VIRTUAL_DISPLAY
         * HWC2_PFN_CREATE_VIRTUAL_DISPLAY
         */
        int32_t createVirtualDisplay(
                uint32_t width, uint32_t height, int32_t *format, ExynosDisplay *display);

        /**
         * Descriptor: HWC2_FUNCTION_DESTROY_VIRTUAL_DISPLAY
         * HWC2_PFN_DESTROY_VIRTUAL_DISPLAY
         */
        int32_t destroyVirtualDisplay(
                ExynosDisplay *display);

        /**
         * Descriptor: HWC2_FUNCTION_DUMP
         * HWC2_PFN_DUMP
         */
        void dump(uint32_t *outSize, char *outBuffer);

        /**
         * Descriptor: HWC2_FUNCTION_GET_MAX_VIRTUAL_DISPLAY_COUNT
         * HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT
         */
        /* TODO overide check!! */
        uint32_t getMaxVirtualDisplayCount();

        /**
         * Descriptor: HWC2_FUNCTION_REGISTER_CALLBACK
         * HWC2_PFN_REGISTER_CALLBACK
         */
        int32_t registerCallback (
                int32_t descriptor, hwc2_callback_data_t callbackData, hwc2_function_pointer_t point);

        void invalidate();

        void setHWCDebug(unsigned int debug);
        uint32_t getHWCDebug();
        void setHWCFenceDebug(uint32_t ipNum, uint32_t typeNum, uint32_t mode);
        void getHWCFenceDebug();
        void setHWCControl(uint32_t display, uint32_t ctrl, int32_t val);
        void setDisplayMode(uint32_t displayMode);
        bool checkDisplayEnabled(uint32_t displayId);
        bool checkAdditionalConnection();
        void getCapabilities(uint32_t *outCount, int32_t* outCapabilities);
        static void getAllocator(GrallocWrapper::Mapper** mapper, GrallocWrapper::Allocator**  allocator);
        void setGeometryChanged(uint64_t changedBit) { mGeometryChanged|= changedBit;};
        void clearGeometryChanged();
        void setGeometryFlagForSkipFrame();
        void setDynamicRecomposition(unsigned int on);
        bool canSkipValidate();
        bool validateFences(ExynosDisplay *display);
        void compareVsyncPeriod();
        void setDumpCount();
        bool isDynamicRecompositionThreadAlive();
        void checkDynamicRecompositionThread();
        void clearRenderingStateFlags();
        bool wasRenderingStateFlagsCleared();

        int32_t validateDisplays(ExynosDisplay* firstDisplay,
                uint32_t* outNumTypes, uint32_t* outNumRequests);

        virtual bool getCPUPerfInfo(int display, int config, int32_t *cpuIds, int32_t *min_clock);

        /* Add EPIC APIs */
        void* mEPICHandle = NULL;
        FILE* mPerfTableFd = NULL;
        long (*mEPICRequestFcnPtr)(int id);
        void (*mEPICFreeFcnPtr)(long handle);
        bool (*mEPICAcquireFcnPtr)(long handle);
        bool (*mEPICReleaseFcnPtr)(long handle);
        bool (*mEPICAcquireOptionFcnPtr)(long handle, unsigned int value, unsigned int usec);

        virtual bool supportPerformaceAssurance() { return false; };
        virtual void performanceAssurance(ExynosDisplay *display, hwc2_config_t config);
        virtual void setCPUClocksPerCluster(ExynosDisplay __unused *display, hwc2_config_t __unused config)
        { return; };
        virtual bool makeCPUPerfTable(ExynosDisplay *display, hwc2_config_t config);
        virtual int isNeedCompressedTargetBuffer(uint64_t displayId);

    protected:
        void initDeviceInterface(uint32_t interfaceType);
    protected:
        enum {
            INTERFACE_TYPE_FB = 0,
        };
        uint32_t mInterfaceType;
};

#endif //_EXYNOSDEVICE_H
