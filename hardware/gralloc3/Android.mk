#
# Copyright (C) 2016-2018 ARM Limited. All rights reserved.
#
# Copyright (C) 2008 The Android Open Source Project
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

TOP_LOCAL_PATH := $(call my-dir)

ifdef GRALLOC_USE_GRALLOC1_API
    ifdef GRALLOC_API_VERSION
        $(warning GRALLOC_API_VERSION flag is not compatible with GRALLOC_USE_GRALLOC1_API)
    endif
endif

include $(TOP_LOCAL_PATH)/gralloc.version.mk

# Place and access VPU library from /vendor directory in unit testing as default
# /system is not in the linker permitted paths
ifeq ($(MALI_GRALLOC_API_TESTS), 1)
MALI_GRALLOC_VPU_LIBRARY_PATH := \"/vendor/lib/\"
endif

#Build allocator for 2.x and gralloc libhardware HAL for all previous versions
GRALLOC_MAPPER := 0
ifeq ($(shell expr $(GRALLOC_VERSION_MAJOR) \>= 2), 1)
    $(info Build Gralloc allocator for 2.x)
else ifeq ($(shell expr $(GRALLOC_VERSION_MAJOR) \>= 1), 1)
    $(info Build Gralloc 1.x libhardware HAL)
else
    $(info Build Gralloc 0.x libhardware HAL)
endif
include $(TOP_LOCAL_PATH)/src/Android.mk

# Build gralloc mapper for 2.x
ifeq ($(shell expr $(GRALLOC_VERSION_MAJOR) \>= 2), 1)
   GRALLOC_MAPPER := 1
   $(info Build Gralloc mapper for 2.x)
   include $(TOP_LOCAL_PATH)/src/Android.mk
endif

####################################################################################################

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := liblog libcutils libutils android.hardware.graphics.allocator@2.0 android.hardware.graphics.mapper@2.0 \
	libsync libhardware libhidlbase

LOCAL_C_INCLUDES := \
    $(TOP)/hardware/samsung_slsi/exynos/include

LOCAL_SRC_FILES := 	\
	GrallocWrapper.cpp

ifeq ($(BOARD_USES_VENDORIMAGE), true)
LOCAL_PROPRIETARY_MODULE := true
endif
LOCAL_MODULE := libGrallocWrapper

include $(BUILD_SHARED_LIBRARY)
