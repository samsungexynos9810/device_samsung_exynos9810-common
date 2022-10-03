# Copyright (C) 2012 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)
# HAL module implemenation, not prelinked and stored in
# hw/<COPYPIX_HARDWARE_MODULE_ID>.<ro.product.board>.so

include $(CLEAR_VARS)

ifndef TARGET_SOC_BASE
	TARGET_SOC_BASE := $(TARGET_SOC)
endif

LOCAL_SHARED_LIBRARIES := liblog libcutils libhardware \
	android.hardware.graphics.allocator@2.0 \
	android.hardware.graphics.mapper@2.0 \
	libGrallocWrapper libhardware_legacy libutils libsync libacryl libui libion_exynos libion

LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers libbinder_headers libexynos_headers
LOCAL_STATIC_LIBRARIES += libVendorVideoApi
LOCAL_CFLAGS := -DHLOG_CODE=0
LOCAL_CFLAGS += -DLOG_TAG=\"hwcomposer\"
LOCAL_PROPRIETARY_MODULE := true

LOCAL_C_INCLUDES += \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libdevice \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libmaindisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libhwchelper \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1 \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libmaindisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libdevice \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libdisplayinterface \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libhwcService \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libdisplayinterface

LOCAL_SRC_FILES := \
	libhwchelper/ExynosHWCHelper.cpp \
	ExynosHWCDebug.cpp \
	libdevice/ExynosDisplay.cpp \
	libdevice/ExynosDevice.cpp \
	libdevice/ExynosLayer.cpp \
	libmaindisplay/ExynosPrimaryDisplay.cpp \
	libresource/ExynosMPP.cpp \
	libresource/ExynosResourceManager.cpp \
	libexternaldisplay/ExynosExternalDisplay.cpp \
	libvirtualdisplay/ExynosVirtualDisplay.cpp \
	libdisplayinterface/ExynosDeviceFbInterface.cpp \
	libdisplayinterface/ExynosDisplayInterface.cpp \
	libdisplayinterface/ExynosDisplayFbInterface.cpp

LOCAL_EXPORT_SHARED_LIBRARY_HEADERS += libacryl

include $(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/Android.mk

LOCAL_CFLAGS := -DHLOG_CODE=0
LOCAL_CFLAGS += -DLOG_TAG=\"display\"
LOCAL_CFLAGS += -Wno-unused-parameter

LOCAL_MODULE := libexynosdisplay
LOCAL_MODULE_TAGS := optional

include $(TOP)/hardware/samsung_slsi/graphics/base/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)

################################################################################

ifeq ($(BOARD_USES_HWC_SERVICES),true)

include $(CLEAR_VARS)

LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers libbinder_headers libexynos_headers
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils libbinder libexynosdisplay libacryl \
	android.hardware.graphics.allocator@2.0 \
	android.hardware.graphics.mapper@2.0 \
	libGrallocWrapper libion
LOCAL_STATIC_LIBRARIES += libVendorVideoApi
LOCAL_PROPRIETARY_MODULE := true

LOCAL_C_INCLUDES += \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libdevice \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libmaindisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libhwchelper \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1 \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libmaindisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libdevice \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libhwcService \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libdisplayinterface

LOCAL_CFLAGS := -DHLOG_CODE=0
LOCAL_CFLAGS += -DLOG_TAG=\"hwcservice\"

LOCAL_SRC_FILES := \
	libhwcService/IExynosHWC.cpp \
	libhwcService/ExynosHWCService.cpp

LOCAL_MODULE := libExynosHWCService
LOCAL_MODULE_TAGS := optional

include $(TOP)/hardware/samsung_slsi/graphics/base/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)

endif

################################################################################

include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils libexynosdisplay libacryl \
	android.hardware.graphics.allocator@2.0 \
	android.hardware.graphics.mapper@2.0 \
	libGrallocWrapper libui libion
LOCAL_PROPRIETARY_MODULE := true
LOCAL_HEADER_LIBRARIES := libhardware_legacy_headers libbinder_headers libexynos_headers

LOCAL_CFLAGS := -DHLOG_CODE=0
LOCAL_CFLAGS += -DLOG_TAG=\"hwcomposer\"

ifeq ($(BOARD_USES_HWC_SERVICES),true)
LOCAL_CFLAGS += -DUSES_HWC_SERVICES
LOCAL_SHARED_LIBRARIES += libExynosHWCService
endif
LOCAL_STATIC_LIBRARIES += libVendorVideoApi

LOCAL_C_INCLUDES += \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libdevice \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libmaindisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libhwchelper \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1 \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libmaindisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libexternaldisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libvirtualdisplay \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libdevice \
	$(TOP)/hardware/samsung_slsi/graphics/$(TARGET_SOC_BASE)/libhwc2.1/libresource \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libhwcService \
	$(TOP)/hardware/samsung_slsi/graphics/base/libhwc2.1/libdisplayinterface

LOCAL_SRC_FILES := \
	ExynosHWC.cpp

ifeq ($(TARGET_SOC),$(TARGET_BOOTLOADER_BOARD_NAME))
LOCAL_MODULE := hwcomposer.$(TARGET_BOOTLOADER_BOARD_NAME)
else
LOCAL_MODULE := hwcomposer.$(TARGET_SOC)
endif
LOCAL_MODULE_TAGS := optional

include $(TOP)/hardware/samsung_slsi/graphics/base/BoardConfigCFlags.mk
include $(BUILD_SHARED_LIBRARY)

