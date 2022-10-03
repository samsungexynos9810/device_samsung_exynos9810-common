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

#ifndef _EXYNOSDISPLAY_H
#define _EXYNOSDISPLAY_H

#include <utils/Vector.h>
#include <utils/KeyedVector.h>
#include <system/graphics.h>

#include "ExynosHWC.h"
#include <hardware/hwcomposer2.h>
#ifdef GRALLOC_VERSION1
#include "gralloc1_priv.h"
#else
#include "gralloc_priv.h"
#endif
#include "ExynosHWCHelper.h"
#include "ExynosMPP.h"
#include "ExynosResourceManager.h"
#include "ExynosDisplayInterface.h"
#include "ExynosHWCDebug.h"
#include "ExynosDeviceModule.h"

#define HWC_CLEARDISPLAY_WITH_COLORMAP
#define HWC_PRINT_FRAME_NUM     10

#define LOW_FPS_THRESHOLD     5

#if defined(HDR_CAPABILITIES_NUM)
#define SET_HDR_CAPABILITIES_NUM HDR_CAPABILITIES_NUM
#else
#define SET_HDR_CAPABILITIES_NUM 0
#endif

#ifndef CPU_CLUSTER_CNT
#define CPU_CLUSTER_CNT 3
#endif

typedef hwc2_composition_t exynos_composition;

class ExynosLayer;
class ExynosDevice;
class ExynosMPP;
class ExynosMPPSource;

enum dynamic_recomp_mode {
    NO_MODE_SWITCH,
    DEVICE_2_CLIENT,
    CLIENT_2_DEVICE
};

enum rendering_state {
    RENDERING_STATE_NONE = 0,
    RENDERING_STATE_VALIDATED,
    RENDERING_STATE_ACCEPTED_CHANGE,
    RENDERING_STATE_PRESENTED,
    RENDERING_STATE_MAX
};

enum composition_type {
    COMPOSITION_NONE = 0,
    COMPOSITION_CLIENT,
    COMPOSITION_EXYNOS,
    COMPOSITION_MAX
};

enum {
    PSR_NONE = 0,
    PSR_DP,
    PSR_MIPI,
    PSR_MAX,
};

enum {
    PANEL_LEGACY = 0,
    PANEL_DSC,
    PANEL_MIC,
};

enum {
    eDisplayNone     = 0x0,
    ePrimaryDisplay  = 0x00000001,
    eExternalDisplay = 0x00000002,
    eVirtualDisplay  = 0x00000004,
};

class AssignedResourceVector : public android::SortedVector< resource_set_t* > {
    public:
        AssignedResourceVector();
        AssignedResourceVector(const AssignedResourceVector& rhs);
        virtual int do_compare(const void* lhs, const void* rhs) const;
};

#define NUM_SKIP_STATIC_LAYER  5
struct ExynosFrameInfo
{
    uint32_t srcNum;
    exynos_image srcInfo[NUM_SKIP_STATIC_LAYER];
    exynos_image dstInfo[NUM_SKIP_STATIC_LAYER];
};

struct exynos_readback_info
{
    private_handle_t *handle = NULL;
    /* release sync fence file descriptor,
     * which will be signaled when it is safe to write to the output buffer.
     */
    int rel_fence = -1;
    /* acquire sync fence file descriptor which will signal when the
     * buffer provided to setReadbackBuffer has been filled by the device and is
     * safe for the client to read.
     */
    int acq_fence = -1;
};

struct exynos_win_config_data
{
    enum {
        WIN_STATE_DISABLED = 0,
        WIN_STATE_COLOR,
        WIN_STATE_BUFFER,
        WIN_STATE_UPDATE,
        WIN_STATE_CURSOR,
    } state = WIN_STATE_DISABLED;

    uint32_t color = 0;
    int fd_idma[3] = {-1, -1, -1};
    int acq_fence = -1;
    int rel_fence = -1;
    int plane_alpha = 0;
    int32_t blending = HWC2_BLEND_MODE_NONE;
    ExynosMPP* assignedMPP = NULL;
    int format = 0;
    uint32_t transform = 0;
    android_dataspace dataspace = HAL_DATASPACE_UNKNOWN;
    bool hdr_enable = false;
    enum dpp_comp_src comp_src = DPP_COMP_SRC_NONE;
    uint32_t min_luminance = 0;
    uint32_t max_luminance = 0;
    struct decon_win_rect block_area = { 0, 0, 0, 0};
    struct decon_win_rect transparent_area = {0, 0, 0, 0};
    struct decon_win_rect opaque_area = {0, 0, 0, 0};
    struct decon_frame src = {0, 0, 0, 0, 0, 0};
    struct decon_frame dst = {0, 0, 0, 0, 0, 0};
    bool protection = false;
    bool compression = false;

    void initData(){
        state = WIN_STATE_DISABLED;
        color = 0;
        fd_idma[0] = -1; fd_idma[1] = -1; fd_idma[2] = -1;
        acq_fence = -1;
        rel_fence = -1;
        plane_alpha = 0;
        blending = HWC2_BLEND_MODE_NONE;
        assignedMPP = NULL;
        format = 0;
        transform = 0;
        dataspace = HAL_DATASPACE_UNKNOWN;
        hdr_enable = false;
        comp_src = DPP_COMP_SRC_NONE;
        min_luminance = 0;
        max_luminance = 0;
        block_area.x = 0; block_area.y = 0; block_area.w = 0; block_area.h = 0;
        transparent_area.x = 0; transparent_area.y = 0; transparent_area.w = 0; transparent_area.h = 0;
        opaque_area.x = 0; opaque_area.y = 0; opaque_area.w = 0; opaque_area.h = 0;
        src.x = 0; src.y = 0; src.w = 0; src.h = 0; src.f_w = 0; src.f_h = 0;
        dst.x = 0; dst.y = 0; dst.w = 0; dst.h = 0; dst.f_w = 0; dst.f_h = 0;
        protection = false;
        compression = false;
    };
};
struct exynos_dpu_data
{
    int retire_fence = -1;
    std::vector<exynos_win_config_data> configs;
    bool enable_win_update = false;
    bool enable_readback = false;
    struct decon_frame win_update_region = {0, 0, 0, 0, 0, 0};
    struct exynos_readback_info readback_info;

    void initData() {
        retire_fence = -1;
        for (uint32_t i = 0; i < configs.size(); i++)
            configs[i].initData();

        /*
         * Should not initialize readback_info
         * readback_info should be initialized after present
         */
    };
    exynos_dpu_data& operator =(const exynos_dpu_data configs_data){
        retire_fence = configs_data.retire_fence;
        if (configs.size() != configs_data.configs.size()) {
            HWC_LOGE(NULL, "invalid config, it has different configs size");
            return *this;
        }
        for (uint32_t i = 0; i < configs.size(); i++) {
            configs[i] = configs_data.configs[i];
        }
        return *this;
    };
};

struct redering_state_flags_info {
    bool validateFlag = false;
    bool presentFlag = false;
};

class ExynosLowFpsLayerInfo
{
    public:
        ExynosLowFpsLayerInfo();
        bool mHasLowFpsLayer;
        int32_t mFirstIndex;
        int32_t mLastIndex;

        void initializeInfos();
        int32_t addLowFpsLayer(uint32_t layerIndex);
};

class ExynosSortedLayer : public Vector <ExynosLayer*>
{
    public:
        ssize_t remove(const ExynosLayer *item);
        status_t vector_sort();
        static int compare(ExynosLayer * const *lhs, ExynosLayer *const *rhs);
};

class ExynosCompositionInfo : public ExynosMPPSource {
    public:
        ExynosCompositionInfo():ExynosCompositionInfo(COMPOSITION_NONE){};
        ExynosCompositionInfo(uint32_t type);
        uint32_t mType;
        bool mHasCompositionLayer;
        int32_t mFirstIndex;
        int32_t mLastIndex;
        private_handle_t *mTargetBuffer;
        android_dataspace mDataSpace;
        int32_t mAcquireFence;
        int32_t mReleaseFence;
        bool mEnableSkipStatic;
        bool mSkipStaticInitFlag;
        bool mSkipFlag;
        ExynosFrameInfo mSkipSrcInfo;
        exynos_win_config_data mLastWinConfigData;

        int32_t mWindowIndex;
        bool mCompressed;

        void initializeInfos(ExynosDisplay *display);
        void setTargetBuffer(ExynosDisplay *display, private_handle_t *handle,
                int32_t acquireFence, android_dataspace dataspace);
        void setCompressed(bool compressed);
        bool getCompressed();
        void dump(String8& result);
        String8 getTypeStr();
};

// Prepare multi-resolution
struct ResolutionSize {
    uint32_t w;
    uint32_t h;
};

struct ResolutionInfo {
    uint32_t nNum;
    ResolutionSize nResolution[3];
    uint32_t nDSCYSliceSize[3];
    uint32_t nDSCXSliceSize[3];
    int      nPanelType[3];
};

typedef struct displayConfigs {
    // HWC2_ATTRIBUTE_VSYNC_PERIOD
    uint32_t vsyncPeriod;
    // HWC2_ATTRIBUTE_WIDTH
    uint32_t width;
    // case HWC2_ATTRIBUTE_HEIGHT
    uint32_t height;
    // HWC2_ATTRIBUTE_DPI_X
    uint32_t Xdpi;
    // HWC2_ATTRIBUTE_DPI_Y
    uint32_t Ydpi;
    // Affinity map
    uint32_t cpuIDs;
    // min_clock
    uint32_t minClock[CPU_CLUSTER_CNT];
    // M2M Capa
    uint32_t m2mCapa;
} displayConfigs_t;

struct DisplayControl {
    /** Composition crop en/disable **/
    bool enableCompositionCrop;
    /** Resource assignment optimization for exynos composition **/
    bool enableExynosCompositionOptimization;
    /** Resource assignment optimization for client composition **/
    bool enableClientCompositionOptimization;
    /** Use G2D as much as possible **/
    bool useMaxG2DSrc;
    /** Low fps layer optimization **/
    bool handleLowFpsLayers;
    /** start m2mMPP before persentDisplay **/
    bool earlyStartMPP;
    /** Adjust display size of the layer having high priority */
    bool adjustDisplayFrame;
    /** setCursorPosition support **/
    bool cursorSupport;
    /** readback support **/
    bool readbackSupport = false;
};

typedef struct hiberState {
    FILE *hiberExitFd = NULL;
    bool exitRequested = false;
} hiberState_t;

class ExynosDisplay {
    public:
        uint32_t mDisplayId;
        uint32_t mType;
        uint32_t mIndex;
        String8 mDeconNodeName;
        uint32_t mXres;
        uint32_t mYres;
        uint32_t mXdpi;
        uint32_t mYdpi;
        uint32_t mVsyncPeriod;
        int32_t mVsyncFd;

        int                     mPanelType;
        int                     mPsrMode;
        int32_t                 mDSCHSliceNum;
        int32_t                 mDSCYSliceSize;

        /* Constructor */
        ExynosDisplay(uint32_t index, ExynosDevice *device);
        /* Destructor */
        virtual ~ExynosDisplay();

        ExynosDevice *mDevice;

        String8 mDisplayName;

        Mutex mDisplayMutex;

        /** State variables */
        bool mPlugState;
        hwc2_power_mode_t mPowerModeState;
        hwc2_vsync_t mVsyncState;
        bool mHasSingleBuffer;

        DisplayControl mDisplayControl;

        /**
         * TODO : Should be defined as ExynosLayer type
         * Layer list those sorted by z-order
         */
        ExynosSortedLayer mLayers;

        ExynosResourceManager *mResourceManager;

        /**
         * Layer index, target buffer information for GLES.
         */
        ExynosCompositionInfo mClientCompositionInfo;

        /**
         * Layer index, target buffer information for G2D.
         */
        ExynosCompositionInfo mExynosCompositionInfo;

        /**
         * Geometry change info is described by bit map.
         * This flag is cleared when resource assignment for all displays
         * is done.
         */
        uint64_t  mGeometryChanged;

        /**
         * Rendering step information that is seperated by
         * VALIDATED, ACCEPTED_CHANGE, PRESENTED.
         */
        rendering_state  mRenderingState;

        /**
         * Rendering step information that is called by client
         */
        rendering_state  mHWCRenderingState;

        /*
         * Flag that each rendering step is performed
         * This flags are set in each rendering step.
         * this flags are cleared by setLayer...() APIs.
         */
        redering_state_flags_info mRenderingStateFlags;

        /**
         * Window total bandwidth by enabled window, It's used as dynamic re-composition enable/disable.
         */
        uint32_t  mDisplayBW;

        /**
         * Mode information Dynamic re-composition feature.
         * DEVICE_2_CLIENT: All layers are composited by GLES composition.
         * CLIENT_2_DEVICE: Device composition.
         */
        dynamic_recomp_mode mDynamicReCompMode;
        bool mDREnable;
        bool mDRDefault;
        Mutex mDRMutex;

        uint64_t mErrorFrameCount;
        uint64_t mLastModeSwitchTimeStamp;
        uint64_t mLastUpdateTimeStamp;
        uint64_t mUpdateEventCnt;
        uint32_t mDumpCount;

        /* default DMA for the display */
        decon_idma_type mDefaultDMA;

        /**
         * DECON WIN_CONFIG information.
         */
        exynos_dpu_data mDpuData;

        /**
         * Last win_config data is used as WIN_CONFIG skip decision or debugging.
         */
        exynos_dpu_data mLastDpuData;

        /**
         * Restore release fenc from DECON.
         */
        int mLastRetireFence;

        bool mUseDpu;

        /**
         * Max Window number, It should be set by display module(chip)
         */
        uint32_t mMaxWindowNum;
        uint32_t mWindowNumUsed;
        uint32_t mBaseWindowIndex;
        int32_t mBlendingNoneIndex;

        // Priority
        uint32_t mNumMaxPriorityAllowed;
        int32_t mCursorIndex;

        int32_t mColorTransformHint;

        ExynosLowFpsLayerInfo mLowFpsLayerInfo;

        // HDR capabilities
        int mHdrTypeNum;
        android_hdr_t mHdrTypes[HDR_CAPABILITIES_NUM];
        float mMaxLuminance;
        float mMaxAverageLuminance;
        float mMinLuminance;

        /* For debugging */
        hwc_display_contents_1_t *mHWC1LayerList;

        /* Support Multi-resolution scheme */
        int mOldScalerMode;
        int mNewScaledWidth;
        int mNewScaledHeight;
        int32_t mDeviceXres;
        int32_t mDeviceYres;
        ResolutionInfo mResolutionInfo;
        std::map<uint32_t, displayConfigs_t> mDisplayConfigs;
        android::SortedVector< uint32_t > mAssignedWindows;

        // WCG
        android_color_mode_t mColorMode;

        // Skip present frame if there was no validate after power on
        bool mNeedSkipPresent;
        // Skip validate, present frame
        bool mNeedSkipValidatePresent;

        AssignedResourceVector mAssignedResourceSet;

        /*
         * flag whether the frame is skipped
         * by specific display (ExynosVirtualDisplay, ExynosExternalDisplay...)
         */
        bool mIsSkipFrame;

        hiberState_t mHiberState;
        FILE *mBrightnessFd;
        unsigned int mMaxBrightness;

        void initDisplay();

        int getId();

        size_t yuvWriteByLines(void * temp, int align_Width, int original_Width, int original_Height, FILE *fp);
        int32_t setCompositionTargetExynosImage(uint32_t targetType, exynos_image *src_img, exynos_image *dst_img);
        int32_t initializeValidateInfos();
        int32_t addClientCompositionLayer(uint32_t layerIndex,
                uint32_t *isExynosCompositionChanged = NULL);
        int32_t removeClientCompositionLayer(uint32_t layerIndex);
        int32_t addExynosCompositionLayer(uint32_t layerIndex);

        /**
         * Dynamic AFBC Control solution : To get the prepared information is applied to current or not.
         */
        bool comparePreferedLayers();

        /**
         * @param *outLayer
         */
        int32_t destroyLayer(hwc2_layer_t outLayer);

        void destroyLayers();

        /**
         * @param index
         */
        ExynosLayer *getLayer(uint32_t index);
        ExynosLayer *checkLayer(hwc2_layer_t addr);

        virtual void doPreProcessing();

        int checkLayerFps();

        int checkDynamicReCompMode();

        int handleDynamicReCompMode();

        /**
         * @param compositionType
         */
        int skipStaticLayers(ExynosCompositionInfo& compositionInfo);
        int handleStaticLayers(ExynosCompositionInfo& compositionInfo);

        int doPostProcessing();

        int doExynosComposition();

        int32_t configureOverlay(ExynosLayer *layer, exynos_win_config_data &cfg);
        int32_t configureOverlay(ExynosCompositionInfo &compositionInfo);

        int32_t configureHandle(ExynosLayer &layer,  int fence_fd, exynos_win_config_data &cfg);

        virtual int setWinConfigData();

        virtual int setDisplayWinConfigData();

        virtual int32_t validateWinConfigData();

        virtual int deliverWinConfigData();

        virtual int setReleaseFences();

        virtual bool checkFrameValidation();

        virtual bool is2StepBlendingRequired(exynos_image &src, private_handle_t *outbuf);

        /**
         * Display Functions for HWC 2.0
         */

        /**
         * Descriptor: HWC2_FUNCTION_ACCEPT_DISPLAY_CHANGES
         * HWC2_PFN_ACCEPT_DISPLAY_CHANGES
         **/
        virtual int32_t acceptDisplayChanges();

        /**
         * Descriptor: HWC2_FUNCTION_CREATE_LAYER
         * HWC2_PFN_CREATE_LAYER
         */
        virtual int32_t createLayer(hwc2_layer_t* outLayer);

        /**
         * Descriptor: HWC2_FUNCTION_GET_ACTIVE_CONFIG
         * HWC2_PFN_GET_ACTIVE_CONFIG
         */
        virtual int32_t getActiveConfig(hwc2_config_t* outConfig);

        /**
         * Descriptor: HWC2_FUNCTION_GET_CHANGED_COMPOSITION_TYPES
         * HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES
         */
        virtual int32_t getChangedCompositionTypes(
                uint32_t* outNumElements, hwc2_layer_t* outLayers,
                int32_t* /*hwc2_composition_t*/ outTypes);

        /**
         * Descriptor: HWC2_FUNCTION_GET_CLIENT_TARGET_SUPPORT
         * HWC2_PFN_GET_CLIENT_TARGET_SUPPORT
         */
        virtual int32_t getClientTargetSupport(
                uint32_t width, uint32_t height,
                int32_t /*android_pixel_format_t*/ format,
                int32_t /*android_dataspace_t*/ dataspace);

        /**
         * Descriptor: HWC2_FUNCTION_GET_COLOR_MODES
         * HWC2_PFN_GET_COLOR_MODES
         */
        virtual int32_t getColorModes(
                uint32_t* outNumModes,
                int32_t* /*android_color_mode_t*/ outModes);

        /* getDisplayAttribute(..., config, attribute, outValue)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_ATTRIBUTE
         * HWC2_PFN_GET_DISPLAY_ATTRIBUTE
         */
        virtual int32_t getDisplayAttribute(
                hwc2_config_t config,
                int32_t /*hwc2_attribute_t*/ attribute, int32_t* outValue);

        /* getDisplayConfigs(..., outNumConfigs, outConfigs)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_CONFIGS
         * HWC2_PFN_GET_DISPLAY_CONFIGS
         */
        virtual int32_t getDisplayConfigs(
                uint32_t* outNumConfigs,
                hwc2_config_t* outConfigs);

        /* getDisplayName(..., outSize, outName)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_NAME
         * HWC2_PFN_GET_DISPLAY_NAME
         */
        virtual int32_t getDisplayName(uint32_t* outSize, char* outName);

        /* getDisplayRequests(..., outDisplayRequests, outNumElements, outLayers,
         *     outLayerRequests)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_REQUESTS
         * HWC2_PFN_GET_DISPLAY_REQUESTS
         */
        virtual int32_t getDisplayRequests(
                int32_t* /*hwc2_display_request_t*/ outDisplayRequests,
                uint32_t* outNumElements, hwc2_layer_t* outLayers,
                int32_t* /*hwc2_layer_request_t*/ outLayerRequests);

        /* getDisplayType(..., outType)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_TYPE
         * HWC2_PFN_GET_DISPLAY_TYPE
         */
        virtual int32_t getDisplayType(
                int32_t* /*hwc2_display_type_t*/ outType);
        /* getDozeSupport(..., outSupport)
         * Descriptor: HWC2_FUNCTION_GET_DOZE_SUPPORT
         * HWC2_PFN_GET_DOZE_SUPPORT
         */
        virtual int32_t getDozeSupport(int32_t* outSupport);

        /* getReleaseFences(..., outNumElements, outLayers, outFences)
         * Descriptor: HWC2_FUNCTION_GET_RELEASE_FENCES
         * HWC2_PFN_GET_RELEASE_FENCES
         */
        virtual int32_t getReleaseFences(
                uint32_t* outNumElements,
                hwc2_layer_t* outLayers, int32_t* outFences);

        enum {
            SKIP_ERR_NONE = 0,
            SKIP_ERR_CONFIG_DISABLED,
            SKIP_ERR_FIRST_FRAME,
            SKIP_ERR_GEOMETRY_CHAGNED,
            SKIP_ERR_HAS_CLIENT_COMP,
            SKIP_ERR_SKIP_STATIC_CHANGED,
            SKIP_ERR_HAS_REQUEST,
            SKIP_ERR_DISP_NOT_CONNECTED,
            SKIP_ERR_DISP_NOT_POWER_ON,
            SKIP_ERR_FORCE_VALIDATE
        };
        virtual int32_t canSkipValidate();

        int32_t forceSkipPresentDisplay(int32_t* outRetireFence);
        /* presentDisplay(..., outRetireFence)
         * Descriptor: HWC2_FUNCTION_PRESENT_DISPLAY
         * HWC2_PFN_PRESENT_DISPLAY
         */
        virtual int32_t presentDisplay(int32_t* outRetireFence);
        virtual int32_t presentPostProcessing();

        /* setActiveConfig(..., config)
         * Descriptor: HWC2_FUNCTION_SET_ACTIVE_CONFIG
         * HWC2_PFN_SET_ACTIVE_CONFIG
         */
        virtual int32_t setActiveConfig(hwc2_config_t config);

        /* setClientTarget(..., target, acquireFence, dataspace)
         * Descriptor: HWC2_FUNCTION_SET_CLIENT_TARGET
         * HWC2_PFN_SET_CLIENT_TARGET
         */
        virtual int32_t setClientTarget(
                buffer_handle_t target,
                int32_t acquireFence, int32_t /*android_dataspace_t*/ dataspace);

        /* setColorTransform(..., matrix, hint)
         * Descriptor: HWC2_FUNCTION_SET_COLOR_TRANSFORM
         * HWC2_PFN_SET_COLOR_TRANSFORM
         */
        virtual int32_t setColorTransform(
                const float* matrix,
                int32_t /*android_color_transform_t*/ hint);

        /* setColorMode(..., mode)
         * Descriptor: HWC2_FUNCTION_SET_COLOR_MODE
         * HWC2_PFN_SET_COLOR_MODE
         */
        virtual int32_t setColorMode(
                int32_t /*android_color_mode_t*/ mode);

        /* setOutputBuffer(..., buffer, releaseFence)
         * Descriptor: HWC2_FUNCTION_SET_OUTPUT_BUFFER
         * HWC2_PFN_SET_OUTPUT_BUFFER
         */
        virtual int32_t setOutputBuffer(
                buffer_handle_t buffer,
                int32_t releaseFence);

        virtual int clearDisplay(bool readback = false);

        /* setPowerMode(..., mode)
         * Descriptor: HWC2_FUNCTION_SET_POWER_MODE
         * HWC2_PFN_SET_POWER_MODE
         */
        virtual int32_t setPowerMode(
                int32_t /*hwc2_power_mode_t*/ mode);

        /* setVsyncEnabled(..., enabled)
         * Descriptor: HWC2_FUNCTION_SET_VSYNC_ENABLED
         * HWC2_PFN_SET_VSYNC_ENABLED
         */
        virtual int32_t setVsyncEnabled(
                int32_t /*hwc2_vsync_t*/ enabled);

        int32_t forceSkipValidateDisplay(
                uint32_t* outNumTypes, uint32_t* outNumRequests);
        /* validateDisplay(..., outNumTypes, outNumRequests)
         * Descriptor: HWC2_FUNCTION_VALIDATE_DISPLAY
         * HWC2_PFN_VALIDATE_DISPLAY
         */
        virtual int32_t validateDisplay(
                uint32_t* outNumTypes, uint32_t* outNumRequests);

        /* getHdrCapabilities(..., outNumTypes, outTypes, outMaxLuminance,
         *     outMaxAverageLuminance, outMinLuminance)
         * Descriptor: HWC2_FUNCTION_GET_HDR_CAPABILITIES
         */
        virtual int32_t getHdrCapabilities(uint32_t* outNumTypes, int32_t* /*android_hdr_t*/ outTypes, float* outMaxLuminance,
                float* outMaxAverageLuminance, float* outMinLuminance);

        int32_t getRenderIntents(int32_t mode, uint32_t* outNumIntents,
                int32_t* /*android_render_intent_v1_1_t*/ outIntents);
        int32_t setColorModeWithRenderIntent(int32_t /*android_color_mode_t*/ mode,
                int32_t /*android_render_intent_v1_1_t */ intent);

        /* HWC 2.3 APIs */

        /* getDisplayIdentificationData(..., outPort, outDataSize, outData)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_IDENTIFICATION_DATA
         * Parameters:
         *   outPort - the connector to which the display is connected;
         *             pointer will be non-NULL
         *   outDataSize - if outData is NULL, the size in bytes of the data which would
         *       have been returned; if outData is not NULL, the size of outData, which
         *       must not exceed the value stored in outDataSize prior to the call;
         *       pointer will be non-NULL
         *   outData - the EDID 1.3 blob identifying the display
         *
         * Returns HWC2_ERROR_NONE or one of the following errors:
         *   HWC2_ERROR_BAD_DISPLAY - an invalid display handle was passed in
         */
        int32_t getDisplayIdentificationData(uint8_t* outPort,
                uint32_t* outDataSize, uint8_t* outData);

        /* getDisplayCapabilities(..., outCapabilities)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_CAPABILITIES
         * Parameters:
         *   outNumCapabilities - if outCapabilities was nullptr, returns the number of capabilities
         *       if outCapabilities was not nullptr, returns the number of capabilities stored in
         *       outCapabilities, which must not exceed the value stored in outNumCapabilities prior
         *       to the call; pointer will be non-NULL
         *   outCapabilities - a list of supported capabilities.
         *
         * Returns HWC2_ERROR_NONE or one of the following errors:
         *   HWC2_ERROR_BAD_DISPLAY - an invalid display handle was passed in
         */
        /* Capabilities
           Invalid = HWC2_CAPABILITY_INVALID,
           SidebandStream = HWC2_CAPABILITY_SIDEBAND_STREAM,
           SkipClientColorTransform = HWC2_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM,
           PresentFenceIsNotReliable = HWC2_CAPABILITY_PRESENT_FENCE_IS_NOT_RELIABLE,
           SkipValidate = HWC2_CAPABILITY_SKIP_VALIDATE,
        */
        int32_t getDisplayCapabilities(uint32_t* outNumCapabilities,
                uint32_t* outCapabilities);

        /* getDisplayBrightnessSupport(displayToken)
         * Descriptor: HWC2_FUNCTION_GET_DISPLAY_BRIGHTNESS_SUPPORT
         * Parameters:
         *   outSupport - whether the display supports operations.
         *
         * Returns HWC2_ERROR_NONE or one of the following errors:
         *   HWC2_ERROR_BAD_DISPLAY when the display is invalid.
         */
        int32_t getDisplayBrightnessSupport(bool* outSupport);

        /* setDisplayBrightness(displayToken, brightnesss)
         * Descriptor: HWC2_FUNCTION_SET_DISPLAY_BRIGHTNESS
         * Parameters:
         *   brightness - a number between 0.0f (minimum brightness) and 1.0f (maximum brightness), or
         *          -1.0f to turn the backlight off.
         *
         * Returns HWC2_ERROR_NONE or one of the following errors:
         *   HWC2_ERROR_BAD_DISPLAY   when the display is invalid, or
         *   HWC2_ERROR_UNSUPPORTED   when brightness operations are not supported, or
         *   HWC2_ERROR_BAD_PARAMETER when the brightness is invalid, or
         *   HWC2_ERROR_NO_RESOURCES  when the brightness cannot be applied.
         */
        int32_t setDisplayBrightness(float brightness);

        /* TODO : TBD */
        int32_t setCursorPositionAsync(uint32_t x_pos, uint32_t y_pos);

        int32_t getReadbackBufferAttributes(int32_t* /*android_pixel_format_t*/ outFormat,
                int32_t* /*android_dataspace_t*/ outDataspace);
        int32_t setReadbackBuffer(buffer_handle_t buffer, int32_t releaseFence);
        void setReadbackBufferInternal(buffer_handle_t buffer, int32_t releaseFence);
        int32_t getReadbackBufferFence(int32_t* outFence);
        /* This function is called by ExynosDisplayInterface class to set acquire fence*/
        int32_t setReadbackBufferAcqFence(int32_t acqFence);

        void dump(String8& result);

        virtual int32_t startPostProcessing();

        void dumpConfig(const exynos_win_config_data &c);
        void dumpConfig(String8 &result, const exynos_win_config_data &c);
        void printConfig(exynos_win_config_data &c);

        unsigned int getLayerRegion(ExynosLayer *layer,
                hwc_rect *rect_area, uint32_t regionType);

        int handleWindowUpdate();
        bool windowUpdateExceptions();

        virtual void waitPreviousFrameDone() { return; }

        /* For debugging */
        void setHWC1LayerList(hwc_display_contents_1_t *contents) {mHWC1LayerList = contents;};
        bool validateExynosCompositionLayer();
        void printDebugInfos(String8 &reason);

        bool checkConfigChanged(const exynos_dpu_data &lastConfigsData,
                const exynos_dpu_data &newConfigsData);
        int checkConfigDstChanged(const exynos_dpu_data &lastConfigData,
                const exynos_dpu_data &newConfigData, uint32_t index);

        uint32_t getRestrictionIndex(int halFormat);
        void closeFences();
        void closeFencesForSkipFrame(rendering_state renderingState);

        int32_t getLayerCompositionTypeForValidationType(uint32_t layerIndex);
        void setHWCControl(uint32_t ctrl, int32_t val);
        void setGeometryChanged(uint64_t changedBit);
        void clearGeometryChanged();

        virtual void setDDIScalerEnable(int width, int height);
        virtual int getDDIScalerMode(int width, int height);
        void increaseMPPDstBufIndex();
        virtual void initDisplayInterface(uint32_t interfaceType);
        void getDumpLayer();
        void setDumpCount(uint32_t dumpCount);
        void writeDumpData( private_handle_t *hnd, size_t bufLength[], size_t i, void *temp);
        virtual void assignInitialResourceSet();
        /* Override for each display's meaning of 'enabled state'
         * Primary : Power on, this function overrided in primary display module
         * Exteranal : Plug-in, default */
        virtual bool isEnabled() { return mPlugState; };

        virtual void requestHiberExit();
        virtual void initHiberState();
    protected:
        virtual bool getHDRException(ExynosLayer *layer);
        void setPresentAndClearRenderingStatesFlags();
        int32_t handleSkipPresent(int32_t* outRetireFence);

    public:
        /**
         * This will be initialized with differnt class
         * that inherits ExynosDisplayInterface according to
         * interface type.
         */
        ExynosDisplayInterface *mDisplayInterface;

    private:
        bool skipStaticLayerChanged(ExynosCompositionInfo& compositionInfo);
};

#endif //_EXYNOSDISPLAY_H
