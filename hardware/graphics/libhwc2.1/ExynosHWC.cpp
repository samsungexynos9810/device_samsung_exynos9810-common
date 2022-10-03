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
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <hardware/hardware.h>
#include <utils/Errors.h>
#include <utils/Trace.h>
#include <log/log.h>
#include <sys/stat.h>

#include "ExynosHWC.h"
#include "ExynosHWCModule.h"
#include "ExynosHWCService.h"
#include "ExynosDisplay.h"
#include "ExynosLayer.h"
#include "ExynosExternalDisplayModule.h"
#include "ExynosDeviceModule.h"

class ExynosHWCService;

using namespace android;

uint32_t hwcApiVersion(const hwc_composer_device_1_t* hwc) {
    uint32_t hwcVersion = hwc->common.version;
    return hwcVersion & HARDWARE_API_VERSION_2_MAJ_MIN_MASK;
}

uint32_t hwcHeaderVersion(const hwc_composer_device_1_t* hwc) {
    uint32_t hwcVersion = hwc->common.version;
    return hwcVersion & HARDWARE_API_VERSION_2_HEADER_MASK;
}

bool hwcHasApiVersion(const hwc_composer_device_1_t* hwc, uint32_t version)
{
    return (hwcApiVersion(hwc) >= (version & HARDWARE_API_VERSION_2_MAJ_MIN_MASK));
}

/**************************************************************************************
 * HWC 2.x APIs
 * ************************************************************************************/
ExynosDevice *g_exynosDevice = NULL;

hwc2_function_pointer_t exynos_function_pointer[] =
{
    NULL,                               //HWC2_FUNCTION_INVAILD
    reinterpret_cast<hwc2_function_pointer_t>(exynos_acceptDisplayChanges),        //HWC2_FUNCTION_ACCEPT_DISPLAY_CHANGES
    reinterpret_cast<hwc2_function_pointer_t>(exynos_createLayer),                 //HWC2_FUNCTION_CREATE_LAYER
    reinterpret_cast<hwc2_function_pointer_t>(exynos_createVirtualDisplay),        //HWC2_FUNCTION_CREATE_VIRTUAL_DISPLAY
    reinterpret_cast<hwc2_function_pointer_t>(exynos_destroyLayer),                //HWC2_FUNCTION_DESTROY_LAYER
    reinterpret_cast<hwc2_function_pointer_t>(exynos_destroyVirtualDisplay),       //HWC2_FUNCTION_DESTROY_VIRTUAL_DISPLAY
    reinterpret_cast<hwc2_function_pointer_t>(exynos_dump),                        //HWC2_FUNCTION_DUMP
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getActiveConfig),             //HWC2_FUNCTION_GET_ACTIVE_CONFIG
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getChangedCompositionTypes),  //HWC2_FUNCTION_GET_CHANGED_COMPOSITION_TYPES
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getClientTargetSupport),      //HWC2_FUNCTION_GET_CLIENT_TARGET_SUPPORT
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getColorModes),               //HWC2_FUNCTION_GET_COLOR_MODES
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayAttribute),         //HWC2_FUNCTION_GET_DISPLAY_ATTRIBUTE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayConfigs),           //HWC2_FUNCTION_GET_DISPLAY_CONFIGS
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayName),              //HWC2_FUNCTION_GET_DISPLAY_NAME
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayRequests),          //HWC2_FUNCTION_GET_DISPLAY_REQUESTS
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayType),              //HWC2_FUNCTION_GET_DISPLAY_TYPE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDozeSupport),              //HWC2_FUNCTION_GET_DOZE_SUPPORT
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getHdrCapabilities),          //HWC2_FUNCTION_GET_HDR_CAPABILITIES
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getMaxVirtualDisplayCount),   //HWC2_FUNCTION_GET_MAX_VIRTUAL_DISPLAY_COUNT
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getReleaseFences),            //HWC2_FUNCTION_GET_RELEASE_FENCES
    reinterpret_cast<hwc2_function_pointer_t>(exynos_presentDisplay),              //HWC2_FUNCTION_PRESENT_DISPLAY
    reinterpret_cast<hwc2_function_pointer_t>(exynos_registerCallback),            //HWC2_FUNCTION_REGISTER_CALLBACK
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setActiveConfig),             //HWC2_FUNCTION_SET_ACTIVE_CONFIG
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setClientTarget),             //HWC2_FUNCTION_SET_CLIENT_TARGET
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setColorMode),                //HWC2_FUNCTION_SET_COLOR_MODE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setColorTransform),           //HWC2_FUNCTION_SET_COLOR_TRANSFORM
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setCursorPosition),           //HWC2_FUNCTION_SET_CURSOR_POSITION
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerBlendMode),           //HWC2_FUNCTION_SET_LAYER_BLEND_MODE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerBuffer),              //HWC2_FUNCTION_SET_LAYER_BUFFER
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerColor),               //HWC2_FUNCTION_SET_LAYER_COLOR
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerCompositionType),     //HWC2_FUNCTION_SET_LAYER_COMPOSITION_TYPE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerDataspace),           //HWC2_FUNCTION_SET_LAYER_DATASPACE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerDisplayFrame),        //HWC2_FUNCTION_SET_LAYER_DISPLAY_FRAME
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerPlaneAlpha),          //HWC2_FUNCTION_SET_LAYER_PLANE_ALPHA
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerSidebandStream),      //HWC2_FUNCTION_SET_LAYER_SIDEBAND_STREAM
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerSourceCrop),          //HWC2_FUNCTION_SET_LAYER_SOURCE_CROP
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerSurfaceDamage),       //HWC2_FUNCTION_SET_LAYER_SURFACE_DAMAGE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerTransform),           //HWC2_FUNCTION_SET_LAYER_TRANSFORM
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerVisibleRegion),       //HWC2_FUNCTION_SET_LAYER_VISIBLE_REGION
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerZOrder),              //HWC2_FUNCTION_SET_LAYER_Z_ORDER
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setOutputBuffer),             //HWC2_FUNCTION_SET_OUTPUT_BUFFER
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setPowerMode),                //HWC2_FUNCTION_SET_POWER_MODE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setVsyncEnabled),             //HWC2_FUNCTION_SET_VSYNC_ENABLED
    reinterpret_cast<hwc2_function_pointer_t>(exynos_validateDisplay),             //HWC2_FUNCTION_VALIDATE_DISPLAY
    reinterpret_cast<hwc2_function_pointer_t>(NULL),                               //HWC2_FUNCTION_SET_LAYER_FLOAT_COLOR
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerPerFrameMetadata),    //HWC2_FUNCTION_SET_LAYER_PER_FRAME_METADATA
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getPerFrameMetadataKeys),     //HWC2_FUNCTION_GET_PER_FRAME_METADATA_KEYS
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setReadbackBuffer),           //HWC2_FUNCTION_SET_READBACK_BUFFER
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getReadbackBufferAttributes), //HWC2_FUNCTION_GET_READBACK_BUFFER_ATTRIBUTES
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getReadbackBufferFence),      //HWC2_FUNCTION_GET_READBACK_BUFFER_FENCE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getRenderIntents),            //HWC2_FUNCTION_GET_RENDER_INTENTS
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setColorModeWithRenderIntent),//HWC2_FUNCTION_SET_COLOR_MODE_WITH_RENDER_INTENT
    reinterpret_cast<hwc2_function_pointer_t>(NULL),                               //HWC2_FUNCTION_GET_DATASPACE_SATURATION_MATRIX

    // composer 2.3
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayIdentificationData),//HWC2_FUNCTION_GET_DISPLAY_IDENTIFICATION_DATA
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayCapabilities),      //HWC2_FUNCTION_GET_DISPLAY_CAPABILITIES
    reinterpret_cast<hwc2_function_pointer_t>(NULL),                               //HWC2_FUNCTION_SET_LAYER_COLOR_TRANSFORM
    reinterpret_cast<hwc2_function_pointer_t>(NULL),                               //HWC2_FUNCTION_GET_DISPLAYED_CONTENT_SAMPLING_ATTRIBUTES
    reinterpret_cast<hwc2_function_pointer_t>(NULL),                               //HWC2_FUNCTION_SET_DISPLAYED_CONTENT_SAMPLING_ENABLED
    reinterpret_cast<hwc2_function_pointer_t>(NULL),                               //HWC2_FUNCTION_GET_DISPLAYED_CONTENT_SAMPLE
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setLayerPerFrameMetadataBlobs), //HWC2_FUNCTION_SET_LAYER_PER_FRAME_METADATA_BLOBS
    reinterpret_cast<hwc2_function_pointer_t>(exynos_getDisplayBrightnessSupport),   //HWC2_FUNCTION_GET_DISPLAY_BRIGHTNESS_SUPPORT
    reinterpret_cast<hwc2_function_pointer_t>(exynos_setDisplayBrightness),          //HWC2_FUNCTION_SET_DISPLAY_BRIGHTNESS
};

inline ExynosDevice* checkDevice(hwc2_device_t *dev)
{
    struct exynos_hwc2_device_t *pdev = (struct exynos_hwc2_device_t *)dev;
    ExynosDevice *exynosDevice = pdev->device;
    if ((g_exynosDevice == NULL) || (exynosDevice != g_exynosDevice)) {
        ALOGE("device pointer is not valid (%p, %p)", exynosDevice, g_exynosDevice);
        return NULL;
    }

    return exynosDevice;
}

inline ExynosDisplay* checkDisplay(ExynosDevice *exynosDevice, hwc2_display_t display)
{
    ExynosDisplay *exynosDisplay = exynosDevice->getDisplay((uint32_t)display);
    if (exynosDisplay == NULL) {
        ALOGE("exynosDisplay is NULL");
        return NULL;
    }

    return exynosDisplay;
}

inline ExynosLayer* checkLayer(ExynosDisplay *exynosDisplay, hwc2_layer_t layer)
{
    ExynosLayer *exynosLayer = exynosDisplay->checkLayer(layer);
    if (exynosLayer == NULL) {
        ALOGE("exynosLayer is NULL");
        return NULL;
    }

    return exynosLayer;
}

hwc2_function_pointer_t exynos_getFunction(struct hwc2_device *dev,
        int32_t descriptor)
{
    ExynosDevice *exynosDevice = checkDevice(dev);
    if (!exynosDevice)
        return NULL;

    if (descriptor <= HWC2_FUNCTION_INVALID || descriptor > HWC2_FUNCTION_SET_DISPLAY_BRIGHTNESS)
        return NULL;
    return exynos_function_pointer[descriptor];
}

void exynos_getCapabilities(struct hwc2_device *dev, uint32_t *outCount, int32_t *outCapabilities)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (!exynosDevice)
        *outCapabilities = 0;
    else
        return exynosDevice->getCapabilities(outCount, outCapabilities);
}

void exynos_dump(hwc2_device_t *dev, uint32_t *outSize, char *outBuffer)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice)
        return exynosDevice->dump(outSize, outBuffer);
}

int32_t exynos_acceptDisplayChanges(hwc2_device_t *dev, hwc2_display_t display)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, (uint32_t)display);
        if (exynosDisplay) {
            int32_t ret = exynosDisplay->acceptDisplayChanges();
            exynosDisplay->mHWCRenderingState = RENDERING_STATE_ACCEPTED_CHANGE;
            return ret;
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_createLayer(hwc2_device_t *dev,
        hwc2_display_t display, hwc2_layer_t *outLayer)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->createLayer(outLayer);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_createVirtualDisplay(hwc2_device_t *dev, uint32_t width, uint32_t height,
        int32_t *format, hwc2_display_t *outDisplay)
{
    ExynosDevice *exynosDevice = checkDevice(dev);
    *outDisplay = getDisplayId(HWC_DISPLAY_VIRTUAL, 0);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, *outDisplay);
        if (exynosDisplay)
            return exynosDevice->createVirtualDisplay(width, height, format, exynosDisplay);
    }

    return HWC2_ERROR_BAD_PARAMETER;
}

int32_t exynos_destroyLayer(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t layer)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosDisplay->destroyLayer((hwc2_layer_t)exynosLayer);
            else
                return HWC2_ERROR_BAD_LAYER;
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_destroyVirtualDisplay(hwc2_device_t* dev, hwc2_display_t display)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            exynosDisplay->destroyLayers();
            return exynosDevice->destroyVirtualDisplay(exynosDisplay);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getActiveConfig(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_config_t* outConfig)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getActiveConfig(outConfig);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getChangedCompositionTypes(hwc2_device_t *dev, hwc2_display_t display,
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* /*hwc2_composition_t*/ outTypes)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getChangedCompositionTypes(outNumElements, outLayers, outTypes);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getClientTargetSupport(hwc2_device_t *dev, hwc2_display_t display, uint32_t width,
        uint32_t height, int32_t format, int32_t dataSpace)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getClientTargetSupport(width, height, format, dataSpace);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getColorModes(hwc2_device_t *dev, hwc2_display_t display, uint32_t* outNumModes,
        int32_t* outModes)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getColorModes(outNumModes, outModes);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getRenderIntents(hwc2_device_t* dev, hwc2_display_t display, int32_t mode,
                uint32_t* outNumIntents, int32_t* /*android_render_intent_v1_1_t*/ outIntents)
{
    ExynosDevice *exynosDevice = checkDevice(dev);
    ALOGD("%s:: mode(%d)", __func__, mode);

    if (mode < 0)
        return HWC2_ERROR_BAD_PARAMETER;

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getRenderIntents(mode, outNumIntents, outIntents);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setColorModeWithRenderIntent(hwc2_device_t* dev, hwc2_display_t display,
        int32_t /*android_color_mode_t*/ mode,
        int32_t /*android_render_intent_v1_1_t */ intent)
{
    if ((mode < 0) || (intent < 0))
        return HWC2_ERROR_BAD_PARAMETER;

    ExynosDevice *exynosDevice = checkDevice(dev);
    ALOGD("%s:: mode(%d), intent(%d)", __func__, mode, intent);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->setColorModeWithRenderIntent(mode, intent);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayAttribute(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_config_t config, int32_t /*hwc2_attribute_t*/ attribute, int32_t* outValue)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getDisplayAttribute(config, attribute, outValue);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayConfigs(hwc2_device_t *dev, hwc2_display_t display,
        uint32_t* outNumConfigs, hwc2_config_t* outConfigs)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getDisplayConfigs(outNumConfigs, outConfigs);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayName(hwc2_device_t *dev, hwc2_display_t display,
        uint32_t* outSize, char* outName)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getDisplayName(outSize, outName);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayRequests(hwc2_device_t *dev, hwc2_display_t display,
        int32_t* /*hwc2_display_request_t*/ outDisplayRequests,
        uint32_t* outNumElements, hwc2_layer_t* outLayers,
        int32_t* /*hwc2_layer_request_t*/ outLayerRequests)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getDisplayRequests(outDisplayRequests, outNumElements, outLayers, outLayerRequests);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayType(hwc2_device_t *dev, hwc2_display_t display,
        int32_t* /*hwc2_display_type_t*/ outType)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getDisplayType(outType);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDozeSupport(hwc2_device_t *dev, hwc2_display_t display,
        int32_t* outSupport)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getDozeSupport(outSupport);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getHdrCapabilities(hwc2_device_t *dev, hwc2_display_t display,
        uint32_t* __unused outNumTypes,
        int32_t* __unused outTypes, float* __unused outMaxLuminance,
        float* __unused outMaxAverageLuminance, float* __unused outMinLuminance)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            return exynosDisplay->getHdrCapabilities(outNumTypes, outTypes, outMaxLuminance,
                    outMaxAverageLuminance, outMinLuminance);
            return 0;
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getMaxVirtualDisplayCount(hwc2_device_t* dev)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice)
        return exynosDevice->getMaxVirtualDisplayCount();
    else
        return HWC2_ERROR_BAD_PARAMETER;
}

int32_t exynos_getReleaseFences(hwc2_device_t *dev, hwc2_display_t display,
        uint32_t* outNumElements, hwc2_layer_t* outLayers, int32_t* outFences)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getReleaseFences(outNumElements, outLayers, outFences);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_presentDisplay(hwc2_device_t *dev, hwc2_display_t display,
        int32_t* outRetireFence)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay == NULL)
            return HWC2_ERROR_BAD_DISPLAY;

        int32_t ret = 0;
        if (exynosDisplay->mHWCRenderingState == RENDERING_STATE_VALIDATED) {
            ALOGI("%s:: acceptDisplayChanges was not called",
                    exynosDisplay->mDisplayName.string());
            if (exynosDisplay->acceptDisplayChanges() != HWC2_ERROR_NONE) {
                ALOGE("%s:: acceptDisplayChanges is failed",
                        exynosDisplay->mDisplayName.string());
            }
        }

        if (exynosDisplay->mPlugState == false) {
            exynosDisplay->mNeedSkipValidatePresent = true;
            if (exynosDevice->wasRenderingStateFlagsCleared())
                ret = exynosDisplay->forceSkipPresentDisplay(outRetireFence);
            else
                ret = exynosDisplay->presentDisplay(outRetireFence);
        } else {
            ret = exynosDisplay->presentDisplay(outRetireFence);
        }

        if (ret != HWC2_ERROR_NOT_VALIDATED)
            exynosDisplay->presentPostProcessing();

        exynosDisplay->mHWCRenderingState = RENDERING_STATE_PRESENTED;
        return ret;
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_registerCallback(hwc2_device_t* dev,
        int32_t /*hwc2_callback_descriptor_t*/ descriptor,
        hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer)
{
    int32_t ret = 0;
    struct exynos_hwc2_device_t *pdev = (struct exynos_hwc2_device_t *)dev;
    ExynosDevice *exynosDevice = pdev->device;
    if (exynosDevice == NULL)
    {
        ALOGE("%s:: descriptor(%d), exynosDevice(%p), pointer(%p)",
                __func__, descriptor, exynosDevice, pointer);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    switch (descriptor) {
        case HWC2_CALLBACK_HOTPLUG:
        case HWC2_CALLBACK_REFRESH:
        case HWC2_CALLBACK_VSYNC:
            ret = exynosDevice->registerCallback(descriptor, callbackData, pointer);
            return ret;
        default:
            return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}

int32_t exynos_setActiveConfig(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_config_t config)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            exynosDevice->performanceAssurance(exynosDisplay, config);
            return exynosDisplay->setActiveConfig(config);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setClientTarget(hwc2_device_t *dev, hwc2_display_t display,
        buffer_handle_t target, int32_t acquireFence,
        int32_t /*android_dataspace_t*/ dataspace, hwc_region_t __unused damage)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->setClientTarget(target, acquireFence, dataspace);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setColorMode(hwc2_device_t *dev, hwc2_display_t display, int32_t mode)
{
    if (mode < 0)
        return HWC2_ERROR_BAD_PARAMETER;

    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->setColorMode(mode);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setColorTransform(hwc2_device_t *dev, hwc2_display_t display,
        const float* matrix, int32_t hint)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->setColorTransform(matrix, hint);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setCursorPosition(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t layer, int32_t x, int32_t y)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setCursorPosition(x, y);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerBlendMode(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t __unused layer, int32_t /*hwc2_blend_mode_t*/ __unused mode)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerBlendMode(mode);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerBuffer(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t layer, buffer_handle_t buffer, int32_t acquireFence)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            Mutex::Autolock lock(exynosDisplay->mDisplayMutex);
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerBuffer(buffer, acquireFence);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerColor(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t layer, hwc_color_t color)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerColor(color);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerCompositionType(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t layer, int32_t /*hwc2_composition_t*/ type)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerCompositionType(type);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerDataspace(hwc2_device_t *dev, hwc2_display_t display, hwc2_layer_t layer, int32_t dataspace)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            Mutex::Autolock lock(exynosDisplay->mDisplayMutex);
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerDataspace(dataspace);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerDisplayFrame(hwc2_device_t *dev, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_rect_t __unused frame)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerDisplayFrame(frame);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerPlaneAlpha(hwc2_device_t __unused *dev, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, float __unused alpha)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerPlaneAlpha(alpha);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerSidebandStream(hwc2_device_t __unused *dev, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, const native_handle_t* __unused stream)
{
    ALOGE("%s:: unsuported api", __func__);
    return HWC2_ERROR_NONE;
}

int32_t exynos_setLayerSourceCrop(hwc2_device_t __unused *dev, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_frect_t __unused crop)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerSourceCrop(crop);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerSurfaceDamage(hwc2_device_t __unused *dev, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_region_t __unused damage)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerSurfaceDamage(damage);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerTransform(hwc2_device_t *dev, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, int32_t /*hwc_transform_t*/ __unused transform)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerTransform(transform);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerVisibleRegion(hwc2_device_t *dev, hwc2_display_t __unused display,
        hwc2_layer_t __unused layer, hwc_region_t __unused visible)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerVisibleRegion(visible);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setLayerZOrder(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t layer, uint32_t z)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerZOrder(z);
        }
    }

    return HWC2_ERROR_BAD_LAYER;
}

int32_t exynos_setOutputBuffer(hwc2_device_t *dev, hwc2_display_t display,
        buffer_handle_t buffer, int32_t releaseFence)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->setOutputBuffer(buffer, releaseFence);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setPowerMode(hwc2_device_t *dev, hwc2_display_t __unused display,
        int32_t /*hwc2_power_mode_t*/ __unused mode)
{
    if (mode < 0)
        return HWC2_ERROR_BAD_PARAMETER;

    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            return exynosDisplay->setPowerMode(mode);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setVsyncEnabled(hwc2_device_t *dev, hwc2_display_t __unused display,
        int32_t /*hwc2_vsync_t*/ __unused enabled)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->setVsyncEnabled(enabled);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_validateDisplay(hwc2_device_t *dev, hwc2_display_t display,
        uint32_t* outNumTypes, uint32_t* outNumRequests)
{
    int32_t ret = NO_ERROR;
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay == NULL)
            return HWC2_ERROR_BAD_DISPLAY;

        if (exynosDisplay->mPlugState == false) {
            exynosDisplay->mNeedSkipValidatePresent = true;
            ret = exynosDisplay->forceSkipValidateDisplay(outNumTypes, outNumRequests);
            exynosDisplay->mHWCRenderingState = RENDERING_STATE_VALIDATED;
            return ret;
        }

        if (exynosDevice->isFirstValidate(exynosDisplay)) {
            /*
             * Validate all of displays
             */
            ret = exynosDevice->validateDisplays(exynosDisplay, outNumTypes, outNumRequests);
        } else {
            if (exynosDisplay->mRenderingStateFlags.validateFlag) {
                HDEBUGLOGD(eDebugResourceManager, "%s is already validated",
                        exynosDisplay->mDisplayName.string());

                /*
                 * HWC doesn't skip this frame in validate, present time.
                 * However this display was already validated in first validate time
                 * so it doesn't call validateDisplay() here.
                 */
                exynosDisplay->mNeedSkipValidatePresent = false;
                /* This display was already validated */
                int32_t displayRequests = 0;
                int32_t retDisplayRequests = NO_ERROR;
                if ((ret = exynosDisplay->getChangedCompositionTypes(outNumTypes, NULL, NULL)) != NO_ERROR) {
                    HWC_LOGE(exynosDisplay, "%s:: getChangedCompositionTypes() fail, display(%d), ret(%d)",
                            __func__, exynosDisplay->mDisplayId, ret);
                    exynosDisplay->setGeometryChanged(GEOMETRY_ERROR_CASE);
                }
                if ((retDisplayRequests = exynosDisplay->getDisplayRequests(&displayRequests, outNumRequests, NULL, NULL)) != NO_ERROR) {
                    HWC_LOGE(exynosDisplay, "%s:: getDisplayRequests() fail, display(%d), ret(%d)",
                            __func__, exynosDisplay->mDisplayId, ret);
                    exynosDisplay->setGeometryChanged(GEOMETRY_ERROR_CASE);
                    ret = retDisplayRequests;
                }
                if (ret == NO_ERROR) {
                    if (*outNumTypes == 0)
                        ret = HWC2_ERROR_NONE;
                    else
                        ret = HWC2_ERROR_HAS_CHANGES;
                }
                /*
                 * mRenderingState could be changed to RENDERING_STATE_NONE
                 * if presentDisplay() was called by presentOrValidate()
                 * before this validate call
                 * Restore mRenderingState to RENDERING_STATE_VALIDATED
                 * because display was already validated
                 */
                exynosDisplay->mRenderingState = RENDERING_STATE_VALIDATED;
            } else {
                HDEBUGLOGD(eDebugResourceManager, "%s is power on after first validate",
                        exynosDisplay->mDisplayName.string());
                /*
                 * This display was not validated in first validate time.
                 * It means that power is on after first validate time.
                 * Skip validate and present in this frame.
                 */
                exynosDisplay->mNeedSkipValidatePresent = true;
                ret = exynosDisplay->validateDisplay(outNumTypes, outNumRequests);
                exynosDevice->invalidate();
            }
        }
        exynosDisplay->mHWCRenderingState = RENDERING_STATE_VALIDATED;
        return ret;
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setLayerPerFrameMetadata(hwc2_device_t *dev, hwc2_display_t display,
        hwc2_layer_t layer, uint32_t numElements,
        const int32_t* /*hw2_per_frame_metadata_key_t*/ keys,
        const float* metadata) {
    ExynosDevice *exynosDevice = checkDevice(dev);
    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer == NULL) {
                ALOGE("%s:: invalid layer", __func__);
                return HWC2_ERROR_BAD_PARAMETER;
            }
            return exynosLayer->setLayerPerFrameMetadata(numElements, keys, metadata);
        }
    }
    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getPerFrameMetadataKeys(hwc2_device_t* __unused dev, hwc2_display_t __unused display,
        uint32_t* outNumKeys, int32_t* /*hwc2_per_frame_metadata_key_t*/ outKeys) {

    ExynosDevice *exynosDevice = checkDevice(dev);
    ExynosResourceManager *resourceManager = exynosDevice->mResourceManager;

    uint32_t numKeys = 0;

    if (resourceManager->hasHDR10PlusMPP())
        numKeys = HWC2_HDR10_PLUS_SEI;
    else
        numKeys = HWC2_MAX_FRAME_AVERAGE_LIGHT_LEVEL;

    if (outKeys == NULL) {
        *outNumKeys = numKeys + 1;
        return NO_ERROR;
    } else {
        if (*outNumKeys != (numKeys + 1)) {
            ALOGE("%s:: invalid outNumKeys(%d)", __func__, *outNumKeys);
            return -1;
        }
        for (uint32_t i = 0; i < (*outNumKeys) ; i++) {
            outKeys[i] = i;
        }
    }
    return NO_ERROR;
}

int32_t exynos_getReadbackBufferAttributes(hwc2_device_t *dev, hwc2_display_t display,
        int32_t* /*android_pixel_format_t*/ outFormat,
        int32_t* /*android_dataspace_t*/ outDataspace) {
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getReadbackBufferAttributes(outFormat, outDataspace);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayIdentificationData(hwc2_device_t* dev, hwc2_display_t display, uint8_t* outPort,
        uint32_t* outDataSize, uint8_t* outData)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);

        if (exynosDisplay) {
            return exynosDisplay->getDisplayIdentificationData(outPort, outDataSize, outData);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setReadbackBuffer(hwc2_device_t *dev, hwc2_display_t display,
        buffer_handle_t buffer, int32_t releaseFence) {
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->setReadbackBuffer(buffer, releaseFence);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayCapabilities(hwc2_device_t* dev, hwc2_display_t display, uint32_t* outNumCapabilities,
        uint32_t* outCapabilities)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            return exynosDisplay->getDisplayCapabilities(outNumCapabilities, outCapabilities);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getReadbackBufferFence(hwc2_device_t *dev, hwc2_display_t display,
        int32_t* outFence) {
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay)
            return exynosDisplay->getReadbackBufferFence(outFence);
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setLayerPerFrameMetadataBlobs(hwc2_device_t* dev, hwc2_display_t display,
        hwc2_layer_t layer, uint32_t numElements, const int32_t* keys, const uint32_t* sizes,
        const uint8_t* metadata)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            ExynosLayer *exynosLayer = checkLayer(exynosDisplay, layer);
            if (exynosLayer)
                return exynosLayer->setLayerPerFrameMetadataBlobs(numElements, keys, sizes, metadata);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_getDisplayBrightnessSupport(hwc2_device_t* dev, hwc2_display_t display, bool* outSupport)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            return exynosDisplay->getDisplayBrightnessSupport(outSupport);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

int32_t exynos_setDisplayBrightness(hwc2_device_t* dev, hwc2_display_t display, float brightness)
{
    ExynosDevice *exynosDevice = checkDevice(dev);

    if (exynosDevice) {
        ExynosDisplay *exynosDisplay = checkDisplay(exynosDevice, display);
        if (exynosDisplay) {
            return exynosDisplay->setDisplayBrightness(brightness);
        }
    }

    return HWC2_ERROR_BAD_DISPLAY;
}

/* ************************************************************************************/

void exynos_boot_finished(ExynosHWCCtx *dev)
{
    ALOGI("%s +", __func__);
    int sw_fd;

    if (dev == NULL) {
        ALOGE("%s:: dev is NULL", __func__);
        return;
    }

    for (size_t i = 0; i < dev->device->mDisplays.size(); i++) {
        if (dev->device->mDisplays[i]->mType != HWC_DISPLAY_EXTERNAL)
            continue;
        ExynosExternalDisplayModule *display = (ExynosExternalDisplayModule*)dev->device->mDisplays[i];
        sw_fd = open(display->mEventNodeName, O_RDONLY);
        if (sw_fd >= 0) {
            char val;
            if (read(sw_fd, &val, 1) == 1 && val == '1') {
                ALOGI("%s : try to reconnect displayport(%d)", __func__, display->mDisplayId);
                display->handleHotplugEvent();
            }
            hwcFdClose(sw_fd);
         }
    }
    dev->device->isBootFinished = true;
    ALOGI("%s -", __func__);
}

int exynos_close(hw_device_t* device)
{
    if (device == NULL)
    {
        ALOGE("%s:: device is null", __func__);
        return -EINVAL;
    }

    /* For HWC2.x version */
    struct exynos_hwc2_device_t *dev = (struct exynos_hwc2_device_t *)device;
    if (dev != NULL) {
        if (dev->device != NULL)
            delete dev->device;
        delete dev;
    }

    return NO_ERROR;
}

int exynos_open(const struct hw_module_t *module, const char *name,
        struct hw_device_t **device)
{
    if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
        return -EINVAL;
    }

    void *ptrDev = NULL;

    ALOGD("HWC module_api_version(%d), hal_api_version(%d)",
            module->module_api_version, module->hal_api_version);
    /* For HWC2.x version */
    struct exynos_hwc2_device_t *dev;
    dev = (struct exynos_hwc2_device_t *)malloc(sizeof(*dev));
    memset(dev, 0, sizeof(*dev));

    dev->device = new ExynosDeviceModule;
    g_exynosDevice = dev->device;

    dev->base.common.tag = HARDWARE_DEVICE_TAG;
    dev->base.common.version = HWC_DEVICE_API_VERSION_2_0;
    dev->base.common.module = const_cast<hw_module_t *>(module);
    dev->base.common.close = exynos_close;

    dev->base.getCapabilities = exynos_getCapabilities;
    dev->base.getFunction = exynos_getFunction;
    *device = &dev->base.common;
    ptrDev = dev;

    ALOGD("Start HWCService");
#ifdef USES_HWC_SERVICES
    ExynosHWCCtx *hwcCtx = (ExynosHWCCtx*)ptrDev;
    android::ExynosHWCService   *HWCService;
    HWCService = android::ExynosHWCService::getExynosHWCService();
    HWCService->setExynosHWCCtx(hwcCtx);
    HWCService->setBootFinishedCallback(exynos_boot_finished);
#endif

    return NO_ERROR;
}

static struct hw_module_methods_t exynos_hwc_module_methods = {
    .open = exynos_open,
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = 2,
        .hal_api_version = 0,
        .id = HWC_HARDWARE_MODULE_ID,
        .name = "Samsung exynos hwcomposer module",
        .author = "Samsung LSI",
        .methods = &exynos_hwc_module_methods,
        .dso = 0,
        .reserved = {0},
    }
};
