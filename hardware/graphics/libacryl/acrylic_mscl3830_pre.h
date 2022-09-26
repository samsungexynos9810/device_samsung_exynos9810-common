/*
 * Copyright Samsung Electronics Co.,LTD.
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __HARDWARE_EXYNOS_HW2DCOMPOSITOR_MSCL3830_PRE_LEGACY_H__
#define __HARDWARE_EXYNOS_HW2DCOMPOSITOR_MSCL3830_PRE_LEGACY_H__

#include <linux/videodev2.h>
#include <hardware/exynos/acryl.h>

#include "acrylic_device.h"

class AcrylicCompositorMSCL3830Pre {
public:
    enum BUFDIRECTION { SOURCE = 0, TARGET = 1, NUM_IMAGES = 2 };

    AcrylicCompositorMSCL3830Pre();
    virtual ~AcrylicCompositorMSCL3830Pre();

    bool execute(AcrylicLayer *layerSrc, AcrylicCanvas &canvasDst);
    void clearCurrByFail();
private:
    enum { STATE_REQBUFS = 1, STATE_QBUF = 2, STATE_PROCESSING = STATE_REQBUFS | STATE_QBUF };

    bool resetMode(BUFDIRECTION dir);

    bool configureCSC(AcrylicCanvas &canvasSrc, AcrylicCanvas &canvasDst);
    bool setFormat(BUFDIRECTION dir, AcrylicCanvas &canvas);
    bool setCrop(BUFDIRECTION dir, hw2d_rect_t rect);
    bool prepareExecute(AcrylicCanvas &canvasSrc, AcrylicCanvas &canvasDst);
    bool prepareExecute(BUFDIRECTION dir, AcrylicCanvas &canvas);

    bool dequeueBuffer(BUFDIRECTION dir);

    bool prepareQBuf(AcrylicLayer *layerSrc, AcrylicCanvas &canvasDst);

    bool queueBuffer(AcrylicCanvas &canvas, BUFDIRECTION dir);

    void setCurrentInfo(BUFDIRECTION dir, AcrylicCanvas &canvas, hw2d_rect_t rect);

    bool checkReset(BUFDIRECTION dir, AcrylicCanvas &canvas, hw2d_rect_t crop);

    AcrylicDevice   mDev;
    bool            mProtectedContent;
    uint32_t        mCurrentPixFmt[NUM_IMAGES];
    hw2d_coord_t    mCurrentSize[NUM_IMAGES];
    hw2d_rect_t     mCurrentCrop[NUM_IMAGES];
    int             mCurrentTypeMem[NUM_IMAGES]; // AcrylicCanvas::memory_type
    uint32_t        mUseFenceFlag;
};
#endif //__HARDWARE_EXYNOS_HW2DCOMPOSITOR_MSCL3830_PRE_LEGACY_H__
