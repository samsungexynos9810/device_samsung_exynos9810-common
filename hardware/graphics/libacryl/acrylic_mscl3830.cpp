#include <cstring>
#include <new>

#include <log/log.h>

#include <linux/v4l2-controls.h>

#include <hardware/hwcomposer2.h>

#include "acrylic_internal.h"
#include "acrylic_mscl3830.h"

#ifndef V4L2_BUF_FLAG_USE_SYNC
#define V4L2_BUF_FLAG_USE_SYNC         0x00008000
#endif

#define V4L2_BUF_FLAG_IN_FENCE         0x00200000
#define V4L2_BUF_FLAG_OUT_FENCE        0x00400000

#define V4L2_CAP_FENCES                0x20000000

#define EXYNOS_CID_BASE             (V4L2_CTRL_CLASS_USER| 0x2000U)
#define V4L2_CID_CONTENT_PROTECTION (EXYNOS_CID_BASE + 201)
#define V4L2_CID_CSC_EQ             (EXYNOS_CID_BASE + 101)
#define V4L2_CID_CSC_RANGE          (EXYNOS_CID_BASE + 102)
#define SC_CID_FRAMERATE            (EXYNOS_CID_BASE + 110)

/* for scaler blend set format */
#define V4L2_CID_2D_BLEND_OP                    (EXYNOS_CID_BASE + 103)
#define V4L2_CID_2D_SRC_BLEND_SET_H_POS         (EXYNOS_CID_BASE + 116)
#define V4L2_CID_2D_SRC_BLEND_SET_V_POS         (EXYNOS_CID_BASE + 117)
#define V4L2_CID_2D_SRC_BLEND_FMT_PREMULTI      (EXYNOS_CID_BASE + 118)
#define V4L2_CID_2D_SRC_BLEND_SET_WIDTH         (EXYNOS_CID_BASE + 119)
#define V4L2_CID_2D_SRC_BLEND_SET_HEIGHT        (EXYNOS_CID_BASE + 120)

static const char *__dirname[AcrylicCompositorMSCL3830::NUM_IMAGES] = {"source", "target"};

AcrylicCompositorMSCL3830::AcrylicCompositorMSCL3830(const HW2DCapability &capability)
    : Acrylic(capability), mPreCompositor(), mDev("/dev/video50"),
      mCurrentTransform(0), mProtectedContent(false),
      mTransformChanged(false), mBlendingBefore(HWC2_BLEND_MODE_NONE), mCurrentPixFmt{0, 0},
      mCurrentTypeMem{0, 0}, mDeviceState{0, 0}, mUseFenceFlag(V4L2_BUF_FLAG_USE_SYNC)
{
    memset(&mCurrentCrop, 0, sizeof(mCurrentCrop));
    memset(&mCurrentCropBlend, 0, sizeof(mCurrentCropBlend));

    v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (mDev.ioctl(VIDIOC_QUERYCAP, &cap) == 0) {
        if (cap.device_caps & V4L2_CAP_FENCES)
            mUseFenceFlag = V4L2_BUF_FLAG_IN_FENCE | V4L2_BUF_FLAG_OUT_FENCE;
    }
}

AcrylicCompositorMSCL3830::~AcrylicCompositorMSCL3830()
{
}

bool AcrylicCompositorMSCL3830::resetMode(AcrylicCanvas &canvas, BUFDIRECTION dir)
{
    if (testDeviceState(dir, STATE_REQBUFS)) {
        v4l2_requestbuffers reqbufs;

        reqbufs.count = 0;
        reqbufs.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        reqbufs.memory = (mCurrentTypeMem[dir] == AcrylicCanvas::MT_DMABUF) ? V4L2_MEMORY_DMABUF
                                                                            : V4L2_MEMORY_USERPTR;
        if (mDev.ioctl(VIDIOC_STREAMOFF, &reqbufs.type) < 0) {
            hw2d_coord_t coord = canvas.getImageDimension();
            ALOGERR("Failed to streamoff to reset format of %s to %#x/%dx%d but forcing reqbufs(0)",
                    __dirname[dir], canvas.getFormat(), coord.hori, coord.vert);
        }

        ALOGD_TEST("VIDIOC_STREAMOFF: type=%d", reqbufs.type);

        if (mDev.ioctl(VIDIOC_REQBUFS, &reqbufs) < 0) {
            hw2d_coord_t coord = canvas.getImageDimension();
            ALOGERR("Failed to reqbufs(0) to reset format of %s to %#x/%dx%d",
                    __dirname[dir], canvas.getFormat(), coord.hori, coord.vert);
            return false;
        }

        ALOGD_TEST("VIDIOC_REQBUFS: count=%d, type=%d, memory=%d", reqbufs.count, reqbufs.type, reqbufs.memory);

        clearDeviceState(dir, STATE_REQBUFS);
    }

    return true;
}

bool AcrylicCompositorMSCL3830::isBlendingChanged()
{
    int mod_flags = AcrylicCanvas::SETTING_TYPE_MODIFIED | AcrylicCanvas::SETTING_DIMENSION_MODIFIED;

    if (getLayer(1)) {
        if ((getLayer(1)->getSettingFlags() & mod_flags) != 0)
            return true;

        if (getLayer(1)->getTargetRect() != mCurrentCropBlend)
            return true;
    }

    if (!!getLayer(1) != mBlendingBefore)
        return true;

    return false;
}

bool AcrylicCompositorMSCL3830::resetMode()
{
    bool reset_required = false;
    hw2d_rect_t crop = getLayer(0)->getTargetRect();
    int mod_flags = AcrylicCanvas::SETTING_TYPE_MODIFIED | AcrylicCanvas::SETTING_DIMENSION_MODIFIED;

    // If crop size, dimension, format is changed in any direction,
    // MSCL driver needs reqbufs(0) to the both directions.
    if (area_is_zero(crop))
        crop.size = getCanvas().getImageDimension();

    if ((crop != mCurrentCrop[TARGET]) || (getLayer(0)->getImageRect() != mCurrentCrop[SOURCE]))
        reset_required = true;

    if (((getLayer(0)->getSettingFlags() | getCanvas().getSettingFlags()) & mod_flags) != 0)
        reset_required = true;

    if (isBlendingChanged())
        reset_required = true;

    if (mTransformChanged) {
        reset_required = true;
        mTransformChanged = false;
    }

    if (reset_required) {
        // Ignore the return value because we have no choice when it is false.
        resetMode(*getLayer(0), SOURCE);
        resetMode(getCanvas(), TARGET);

        // It is alright to configure CSC before S_FMT.
        if (!configureCSC())
            return false;
    }

    return true;
}

bool AcrylicCompositorMSCL3830::changeMode(AcrylicCanvas &canvas, BUFDIRECTION dir)
{
    if (testDeviceState(dir, STATE_REQBUFS))
        return true;

    v4l2_buf_type buftype = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
                                            : V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if (!setFormat(canvas, buftype))
        return false;

    mCurrentPixFmt[dir] = canvas.getFormat();
    mCurrentCrop[dir].size = canvas.getImageDimension();
    mCurrentCrop[dir].pos = {0, 0};

    hw2d_rect_t rect;
    if (dir == SOURCE) {
        rect = getLayer(0)->getImageRect();
    } else {
        rect = getLayer(0)->getTargetRect();
        if (area_is_zero(rect))
            rect.size = getCanvas().getImageDimension();
    }

    if (!setCrop(rect, buftype))
        return false;

    mCurrentCrop[dir] = rect;

    return true;
}

/*
 * blending operation
 * The order is from Android PorterDuff.java
 */
enum sc_blend_op {
    /* [0, 0] */
    BL_OP_CLR = 1,
    /* [Sa, Sc] */
    BL_OP_SRC,
    /* [Da, Dc] */
    BL_OP_DST,
    /* [Sa + (1 - Sa)*Da, Rc = Sc + (1 - Sa)*Dc] */
    BL_OP_SRC_OVER,
    /* [Sa + (1 - Sa)*Da, Rc = Dc + (1 - Da)*Sc] */
    BL_OP_DST_OVER,
    /* [Sa * Da, Sc * Da] */
    BL_OP_SRC_IN,
    /* [Sa * Da, Sa * Dc] */
    BL_OP_DST_IN,
    /* [Sa * (1 - Da), Sc * (1 - Da)] */
    BL_OP_SRC_OUT,
    /* [Da * (1 - Sa), Dc * (1 - Sa)] */
    BL_OP_DST_OUT,
    /* [Da, Sc * Da + (1 - Sa) * Dc] */
    BL_OP_SRC_ATOP,
    /* [Sa, Sc * (1 - Da) + Sa * Dc ] */
    BL_OP_DST_ATOP,
    /* [-(Sa * Da), Sc * (1 - Da) + (1 - Sa) * Dc] */
    BL_OP_XOR,
    /* [Sa + Da - Sa*Da, Sc*(1 - Da) + Dc*(1 - Sa) + min(Sc, Dc)] */
    BL_OP_DARKEN,
    /* [Sa + Da - Sa*Da, Sc*(1 - Da) + Dc*(1 - Sa) + max(Sc, Dc)] */
    BL_OP_LIGHTEN,
    /** [Sa * Da, Sc * Dc] */
    BL_OP_MULTIPLY,
    /* [Sa + Da - Sa * Da, Sc + Dc - Sc * Dc] */
    BL_OP_SCREEN,
    /* Saturate(S + D) */
    BL_OP_ADD,
};

bool AcrylicCompositorMSCL3830::setBlend(hw2d_coord_t RdPos, hw2d_coord_t BufSize, bool is_premulti)
{
    v4l2_ext_controls ext_ctrls;
    v4l2_ext_control ext_ctrl[5];

    memset(&ext_ctrls, 0, sizeof(ext_ctrls));
    memset(ext_ctrl, 0, sizeof(ext_ctrl));

    ext_ctrl[0].id = V4L2_CID_2D_SRC_BLEND_SET_H_POS;
    ext_ctrl[0].value = RdPos.hori;

    ext_ctrl[1].id = V4L2_CID_2D_SRC_BLEND_SET_V_POS;
    ext_ctrl[1].value = RdPos.vert;

    ext_ctrl[2].id = V4L2_CID_2D_SRC_BLEND_FMT_PREMULTI;
    ext_ctrl[2].value = is_premulti;

    ext_ctrl[3].id = V4L2_CID_2D_SRC_BLEND_SET_WIDTH;
    ext_ctrl[3].value = BufSize.hori;

    ext_ctrl[4].id = V4L2_CID_2D_SRC_BLEND_SET_HEIGHT;
    ext_ctrl[4].value = BufSize.vert;

    ext_ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ext_ctrls.count = 5;
    ext_ctrls.controls = ext_ctrl;
    ALOGD_TEST("VIDIOC_S_EXT_CTRLS (BLENDING): H_POS(%d), V_POS(%d), PREMULTI(%d), WIDTH(%d), HEIGHT(%d)",
            ext_ctrl[0].value, ext_ctrl[1].value, ext_ctrl[2].value, ext_ctrl[3].value, ext_ctrl[4].value);
    if (mDev.ioctl(VIDIOC_S_EXT_CTRLS, &ext_ctrls) < 0) {
        ALOGERR(" Failed to VIDIOC_S_EXT_CTRLS (BLENDING): H_POS(%d), V_POS(%d), PREMULTI(%d), WIDTH(%d), HEIGHT(%d)",
                ext_ctrl[0].value, ext_ctrl[1].value, ext_ctrl[2].value, ext_ctrl[3].value, ext_ctrl[4].value);
        return false;
    }

    return true;
}

bool AcrylicCompositorMSCL3830::setBlend()
{
    if (getLayer(1)) {
        // blending is used for blending on video area.
        hw2d_coord_t RdPos;
        hw2d_coord_t BufSize = getLayer(1)->getImageDimension();
        bool is_premulti;

        switch (getLayer(1)->getCompositingMode()) {
            // blending with premulti
            case HWC_BLENDING_PREMULT:
            case HWC2_BLEND_MODE_PREMULTIPLIED:
                is_premulti = true;
                break;
            default:
                is_premulti = false;
                break;
        }

        RdPos.hori = getLayer(0)->getTargetRect().pos.hori - getLayer(1)->getTargetRect().pos.hori;
        RdPos.vert = getLayer(0)->getTargetRect().pos.vert - getLayer(1)->getTargetRect().pos.vert;

        if (!setBlend(RdPos, BufSize, is_premulti))
            return false;
    }

    return true;
}

bool AcrylicCompositorMSCL3830::setBlendOp()
{
    v4l2_control ctrl;

    ctrl.id = V4L2_CID_2D_BLEND_OP;
    if (getLayer(1)) {
        switch (getLayer(1)->getCompositingMode()) {
            // no blending
            case HWC_BLENDING_NONE:
            case HWC2_BLEND_MODE_NONE:
                ctrl.value = BL_OP_DST;
                break;
            // blending with premulti
            case HWC_BLENDING_PREMULT:
            case HWC2_BLEND_MODE_PREMULTIPLIED:
                ctrl.value = BL_OP_DST_OVER;
                break;
            // blending with non-premulti
            case HWC_BLENDING_COVERAGE:
            case HWC2_BLEND_MODE_COVERAGE:
                ctrl.value = BL_OP_DST_OVER;
                break;
            default:
                ALOGERR("CompositingMode is invalid : %d", getLayer(1)->getCompositingMode());
                return false;
        }
    } else
        ctrl.value = 0;

    ALOGD_TEST("VIDIOC_S_CTRL: BLEND OP=%d", ctrl.value);
    if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
        ALOGERR("Failed to configure BLEND OP to %d", ctrl.value);
        return false;
    }

    return true;
}
bool AcrylicCompositorMSCL3830::setFormat(AcrylicCanvas &canvas, v4l2_buf_type buftype)
{
    hw2d_coord_t coord = canvas.getImageDimension();
    uint32_t pixfmt = halfmt_to_v4l2_deprecated(canvas.getFormat());
    v4l2_format fmt;

    memset(&fmt, 0, sizeof(fmt));

    if (buftype == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        if (!setBlend())
            return false;

        if (getLayer(1)) {
            pixfmt = v4l2_fmt_with_blend(pixfmt, getLayer(1)->getFormat());
            if (!pixfmt)
                return false;

            mCurrentCropBlend = getLayer(1)->getTargetRect();
        } else
            memset(&mCurrentCropBlend, 0, sizeof(mCurrentCropBlend));

        mBlendingBefore = !!getLayer(1);
    }

    // S_FMT always successes unless type is invalid.
    fmt.type = buftype;

    if (V4L2_TYPE_IS_MULTIPLANAR(buftype)) {
        fmt.fmt.pix_mp.width = coord.hori;
        fmt.fmt.pix_mp.height = coord.vert;
        fmt.fmt.pix_mp.pixelformat = pixfmt;
        fmt.fmt.pix_mp.colorspace = haldataspace_to_v4l2(canvas.getDataspace(), coord.hori, coord.vert);
        fmt.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
        fmt.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
        fmt.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
        ALOGD_TEST("VIDIOC_S_FMT: v4l2_fmt/mp .type=%d, .width=%d, .height=%d, .pixelformat=%#x, .colorspace=%d\n"
                   "                          .ycbcr_enc=%d, quantization=%d, .xfer_func=%d",
                   fmt.type, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.pixelformat,
                   fmt.fmt.pix_mp.colorspace, fmt.fmt.pix_mp.ycbcr_enc, fmt.fmt.pix_mp.quantization,
                   fmt.fmt.pix_mp.xfer_func);
    } else {
        fmt.fmt.pix.width = coord.hori;
        fmt.fmt.pix.height = coord.vert;
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.colorspace = haldataspace_to_v4l2(canvas.getDataspace(), coord.hori, coord.vert);
        fmt.fmt.pix.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
        fmt.fmt.pix.quantization = V4L2_QUANTIZATION_DEFAULT;
        fmt.fmt.pix.xfer_func = V4L2_XFER_FUNC_DEFAULT;
        ALOGD_TEST("VIDIOC_S_FMT: v4l2_fmt .type=%d, .width=%d, .height=%d, .pixelformat=%#x, .colorspace=%d\n"
                   "                       .ycbcr_enc=%d, quantization=%d, .xfer_func=%d",
                   fmt.type, fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat,
                   fmt.fmt.pix.colorspace, fmt.fmt.pix.ycbcr_enc, fmt.fmt.pix.quantization,
                   fmt.fmt.pix.xfer_func);
    }

    if (mDev.ioctl(VIDIOC_S_FMT, &fmt) < 0) {
        ALOGERR("Failed VIDIOC_S_FMT .type=%d, .width=%d, .height=%d, .pixelformat=%#x",
                fmt.type, fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);
        return false;
    }

    return true;
}

bool AcrylicCompositorMSCL3830::setTransform()
{
    uint32_t trdiff = mCurrentTransform ^ getLayer(0)->getTransform();
    v4l2_control ctrl;

    mTransformChanged = !!trdiff;

    // TODO: consider to use rot 180 and 270
    if (trdiff & HAL_TRANSFORM_FLIP_H) {
        ctrl.id = V4L2_CID_HFLIP;
        ctrl.value = !!(getLayer(0)->getTransform() & HAL_TRANSFORM_FLIP_H);
        ALOGD_TEST("VIDIOC_S_CTRL: HFLIP=%d, (transform=%d)", ctrl.value, getLayer(0)->getTransform());
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("Failed to configure HFLIP to %d", ctrl.value);
            return false;
        }
        trdiff &= ~HAL_TRANSFORM_FLIP_H;
    }

    if (trdiff & HAL_TRANSFORM_FLIP_V) {
        ctrl.id = V4L2_CID_VFLIP;
        ctrl.value = !!(getLayer(0)->getTransform() & HAL_TRANSFORM_FLIP_V);
        ALOGD_TEST("VIDIOC_S_CTRL: VFLIP=%d, (transform=%d)", ctrl.value, getLayer(0)->getTransform());
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("Failed to configure VFLIP to %d", ctrl.value);
            return false;
        }
        trdiff &= ~HAL_TRANSFORM_FLIP_V;
    }

    if (trdiff & HAL_TRANSFORM_ROT_90) {
        ctrl.id = V4L2_CID_ROTATE;
        ctrl.value = !(getLayer(0)->getTransform() & HAL_TRANSFORM_ROT_90) ? 0 : 90;
        ALOGD_TEST("VIDIOC_S_CTRL: ROTATE=%d, (transform=%d)", ctrl.value, getLayer(0)->getTransform());
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("Failed to configure Rotation of 90");
            return false;
        }
        trdiff &= ~HAL_TRANSFORM_ROT_90;
    }

    LOGASSERT(trdiff == 0, "Unexpected transform option is changed: %#x", trdiff);

    mCurrentTransform = getLayer(0)->getTransform();

    return true;
}

bool AcrylicCompositorMSCL3830::setCrop(hw2d_rect_t rect, v4l2_buf_type buftype)
{
    v4l2_crop crop;

    crop.type = buftype;
    crop.c.left = rect.pos.hori;
    crop.c.top = rect.pos.vert;
    crop.c.width = rect.size.hori;
    crop.c.height = rect.size.vert;
    ALOGD_TEST("VIDIOC_S_CROP: type=%d, left=%d, top=%d, width=%d, height=%d",
            buftype, crop.c.left, crop.c.top, crop.c.width, crop.c.height);
    if (mDev.ioctl(VIDIOC_S_CROP, &crop) < 0) {
        ALOGERR("Failed to set crop of type %d to %dx%d@%dx%d", buftype,
                rect.size.hori, rect.size.vert, rect.pos.hori, rect.pos.vert);

        // MSC driver is check crop of Blending_SOURCE in s_crop of TARGET.
        if (buftype == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && getLayer(1)) {
            hw2d_coord_t coord = getLayer(1)->getImageDimension();

            ALOGERR("Failed to set crop of type Blend_SOURCE to %dx%d@%dx%d in %dx%d",
                    rect.size.hori, rect.size.vert, rect.pos.hori, rect.pos.vert, coord.hori, coord.vert);

            memset(&mCurrentCropBlend, 0, sizeof(mCurrentCropBlend));
        }
        return false;
    }

    return true;
}

bool AcrylicCompositorMSCL3830::prepareExecute(AcrylicCanvas &canvas, BUFDIRECTION dir)
{
    if (testDeviceState(dir, STATE_REQBUFS))
        return true;

    v4l2_requestbuffers reqbufs;

    reqbufs.count = 1;
    reqbufs.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
                                    : V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = (canvas.getBufferType() == AcrylicCanvas::MT_DMABUF)
                     ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_USERPTR;

    ALOGD_TEST("VIDIOC_REQBUFS: v4l2_requestbuffers .count=%d, .type=%d, .memory=%d, .reserved={%d,%d}",
               reqbufs.count, reqbufs.type, reqbufs.memory, reqbufs.reserved[0], reqbufs.reserved[1]);

    if (mDev.ioctl(VIDIOC_REQBUFS, &reqbufs)) {
        ALOGERR("Failed VIDIOC_REQBUFS: count=%d, type=%d, memory=%d", reqbufs.count, reqbufs.type, reqbufs.memory);
        return false;
    }

    if (mDev.ioctl(VIDIOC_STREAMON, &reqbufs.type) < 0) {
        ALOGERR("Failed VIDIOC_STREAMON with type %d", reqbufs.type);
        // we don't need to cancel the previous s_fmt and reqbufs
        // because we will try it again for the next frame.
        reqbufs.count = 0;
        mDev.ioctl(VIDIOC_REQBUFS, &reqbufs); // cancel reqbufs. ignore result
        return false;
    }

    ALOGD_TEST("VIDIOC_STREAMON: type=%d", reqbufs.type);

    mCurrentTypeMem[dir] = canvas.getBufferType();

    setDeviceState(dir, STATE_REQBUFS);

    return true;
}

bool AcrylicCompositorMSCL3830::prepareExecute()
{
    if (getCanvas().isProtected() != mProtectedContent) {
        v4l2_control ctrl;

        ctrl.id = V4L2_CID_CONTENT_PROTECTION;
        ctrl.value = getCanvas().isProtected();
        ALOGD_TEST("VIDIOC_S_CTRL: V4L2_CID_CONTENT_PROTECTION=%d", ctrl.value);
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("Failed to configure content protection to %d", ctrl.value);
            return false;
        }

        mProtectedContent = getCanvas().isProtected();
    }

    return prepareExecute(*getLayer(0), SOURCE) && prepareExecute(getCanvas(), TARGET);
}

bool AcrylicCompositorMSCL3830::queueBuffer(AcrylicCanvas &canvas, BUFDIRECTION dir, int *fence, bool needReleaseFence)
{
    bool output = (dir == SOURCE);
    bool dmabuf = canvas.getBufferType() == AcrylicCanvas::MT_DMABUF;
    v4l2_buffer buffer;
    v4l2_plane plane[4];

    memset(&buffer, 0, sizeof(buffer));

    buffer.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buffer.memory = dmabuf ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_USERPTR;

    if (output && canvas.getFence() >= 0 && getLayer(1) && getLayer(1)->getFence() >= 0) {
        ALOGERR("MSCL3830 driver is not supporting two fences of Layer(0) and Layer(1)");
        return false;
    }

    if (canvas.getFence() >= 0) {
        buffer.flags = mUseFenceFlag;
        buffer.reserved = canvas.getFence();
    } else if (output && getLayer(1) && getLayer(1)->getFence() >= 0) {
        buffer.flags = mUseFenceFlag;
        buffer.reserved = getLayer(1)->getFence();
    } else if (needReleaseFence) {
        /*
         * In this case, release fence is requested without acquire fence.
         * For compatibility, flags should be set as below.
         * If (cap.device_caps & V4L2_CAPFENCES) is true, flags should be V4L2_BUF_FLAG_OUT_FENCE only.
         * else, flags should be V4L2_BUF_FLAG_USE_SYNC and reserved should be -1.
         */
        buffer.flags = mUseFenceFlag & ~V4L2_BUF_FLAG_IN_FENCE;
        buffer.reserved = -1;
    }

    for (unsigned int i = 0; i < canvas.getBufferCount(); i++) {
        plane[i].length = canvas.getBufferLength(i);
        if (output)
            plane[i].bytesused = halfmt_plane_length(canvas.getFormat(), i,
                                canvas.getImageDimension().hori, mCurrentCrop[SOURCE].size.vert);
        if (dmabuf)
            plane[i].m.fd = canvas.getDmabuf(i);
        else
            plane[i].m.userptr = reinterpret_cast<unsigned long>(canvas.getUserptr(i));

        plane[i].data_offset = canvas.getOffset(i);

        ALOGD_TEST("VIDIOC_QBUF: plane[%d] .length=%d, .bytesused=%d, .m.fd=%d/userptr=%#lx, .offset=%#x",
                    i, plane[i].length, plane[i].bytesused,
                    plane[i].m.fd, plane[i].m.userptr, plane[i].data_offset);
    }
    buffer.length = canvas.getBufferCount();
    if (output && getLayer(1)) {
        unsigned int BlendPlane = canvas.getBufferCount();

        plane[BlendPlane].length = getLayer(1)->getBufferLength(0);
        // crop of blend source is decided by that of TARGET.
        plane[BlendPlane].bytesused = halfmt_plane_length(getLayer(1)->getFormat(), 0,
                                getLayer(1)->getImageDimension().hori, mCurrentCrop[TARGET].size.vert);
        if (dmabuf)
            plane[BlendPlane].m.fd = getLayer(1)->getDmabuf(0);
        else
            plane[BlendPlane].m.userptr = reinterpret_cast<unsigned long>(getLayer(1)->getUserptr(0));
        plane[BlendPlane].data_offset = getLayer(1)->getOffset(0);

        ALOGD_TEST("VIDIOC_QBUF: plane[%d] .length=%d, .bytesused=%d, .m.fd=%d/userptr=%#lx, .offset=%#x",
                    BlendPlane, plane[BlendPlane].length, plane[BlendPlane].bytesused,
                    plane[BlendPlane].m.fd, plane[BlendPlane].m.userptr, plane[BlendPlane].data_offset);
        buffer.length++;
    }
    buffer.m.planes = plane;

    ALOGD_TEST("             .type=%d, .memory=%d, .flags=%d, .length=%d, .reserved=%d, .reserved2=%d",
               buffer.type, buffer.memory, buffer.flags, buffer.length, buffer.reserved, buffer.reserved2);

    if (mDev.ioctl(VIDIOC_QBUF, &buffer) < 0) {
        canvas.setFence(-1);
        ALOGERR("Failed VIDOC_QBUF: type=%d, memory=%d", buffer.type, buffer.memory);
        return false;
    }

    // NOTE: V4L2 clears V4L2_BUF_FLAG_USE_SYNC on return.
    if (canvas.getFence() >= 0 || (output && getLayer(1) && getLayer(1)->getFence()) || needReleaseFence) {
        if (fence)
            *fence = buffer.reserved;
        else
            close(buffer.reserved); // no one waits for the release fence
    }

    canvas.setFence(-1);

    return true;
}

bool AcrylicCompositorMSCL3830::queueBuffer(int fence[], unsigned int num_fences)
{
    int release_fence[NUM_IMAGES] = {-1, -1};
    int fence_count = num_fences;
    bool success;
    unsigned int max_fences = layerCount() + 1;

    if (num_fences > max_fences) {
        ALOGE("num_fences (%d) should be smaller than max_fences (%d)", num_fences, max_fences);
        return false;
    }

    success = queueBuffer(*getLayer(0), SOURCE, &release_fence[SOURCE], (fence_count > 0));
    if (success) {
        if (!queueBuffer(getCanvas(), TARGET, &release_fence[TARGET], (--fence_count > 0))) {
            // reset the state of the output path.
            // ignore even though resetMode() is failed. Nothing to do any more.
            resetMode();
            if (release_fence[SOURCE] >= 0)
                close(release_fence[SOURCE]);
            success = false;
        }
    }

    if (success) {
        setDeviceState(SOURCE, STATE_QBUF);
        setDeviceState(TARGET, STATE_QBUF);

        unsigned int j;
        for (j = 0; j < num_fences; j++) {
            if (j < NUM_IMAGES)
                fence[j] = release_fence[j];
            else
                fence[j] = dup(release_fence[SOURCE]);
        }

        for (unsigned int i = j; i < NUM_IMAGES; i++) {
            if (release_fence[i] >= 0)
                close(release_fence[i]);
        }
    }

    getCanvas().clearSettingModified();
    getLayer(0)->clearSettingModified();
    if (getLayer(1))
        getLayer(1)->clearSettingModified();

    getCanvas().setFence(-1);
    getLayer(0)->setFence(-1);
    if (getLayer(1))
        getLayer(1)->setFence(-1);

    return success;
}

bool AcrylicCompositorMSCL3830::configureCSC()
{
    bool csc_req = false;
    uint32_t csc_sel = 0;
    uint32_t csc_range = 0;

    if ((halfmt_chroma_subsampling(getLayer(0)->getFormat()) == 0x11) &&
        (halfmt_chroma_subsampling(getCanvas().getFormat()) != 0x11)) {
        hw2d_coord_t coord = getCanvas().getImageDimension();

        // RGB of sRGB -> Y'CbCr
        csc_req = true;
        csc_sel = haldataspace_to_v4l2(getCanvas().getDataspace(), coord.hori, coord.vert);
    } else if ((halfmt_chroma_subsampling(getLayer(0)->getFormat()) != 0x11) &&
        (halfmt_chroma_subsampling(getCanvas().getFormat()) == 0x11)) {
        hw2d_coord_t coord = getLayer(0)->getImageDimension();

        // Y'CbCr -> RGB of sRGB
        csc_req = true;
        csc_sel = haldataspace_to_v4l2(getLayer(0)->getDataspace(), coord.hori, coord.vert);
    }

    if (csc_req) {
        switch (csc_sel) {
            case V4L2_COLORSPACE_SRGB:
                csc_sel = V4L2_COLORSPACE_REC709;
                csc_range = 1; // full swing
                break;
            case V4L2_COLORSPACE_REC709:
                csc_sel = V4L2_COLORSPACE_REC709;
                csc_range = 0; // studio swing
                break;
            case V4L2_COLORSPACE_JPEG:
                csc_sel = V4L2_COLORSPACE_SMPTE170M;
                csc_range = 1; // full swing
                break;
            case V4L2_COLORSPACE_SMPTE170M:
                csc_sel = V4L2_COLORSPACE_SMPTE170M;
                csc_range = 0; // studio swing
                break;
            case V4L2_COLORSPACE_BT2020:
                csc_sel = V4L2_COLORSPACE_BT2020;
                csc_range = 0; // studio swing
                break;
        }

        v4l2_control ctrl;

        ctrl.id = V4L2_CID_CSC_EQ;
        ctrl.value = csc_sel;
        ALOGD_TEST("VIDIOC_S_CTRL: csc_matrix_sel=%d", ctrl.value);
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("Failed to configure csc matrix to %d", ctrl.value);
            return false;
        }

        ctrl.id = V4L2_CID_CSC_RANGE;
        ctrl.value = csc_range;
        ALOGD_TEST("VIDIOC_S_CTRL: csc_range=%d", ctrl.value);
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("Failed to configure csc range to %d", ctrl.value);
            return false;
        }
    }

    return true;
}

bool AcrylicCompositorMSCL3830::execute(int fence[], unsigned int num_fences)
{
    if (getLayer(1) && (getLayer(0)->getTargetRect() != getLayer(1)->getTargetRect())) {
        if (!mPreCompositor.execute(getLayer(1), getCanvas())) {
            ALOGERR("fail to copy UI layer to TARGET before blending");
            return false;
        }
    }

    if (!validateAllLayers())
        return false;

    LOGASSERT(layerCount() == 1 || layerCount() == 2, "Number of layer is not 1 or 2 but %d", layerCount());

    if (!waitExecution(0)) {
        ALOGE("Error occurred in the previous image processing");
        return false;
    }

    if (!setTransform())
        return false;

    if (!resetMode())
        return false;

    if (!setBlendOp())
        return false;

    if (!changeMode(*getLayer(0), SOURCE) || !changeMode(getCanvas(), TARGET))
        return false;

    if (!prepareExecute())
        return false;

    return queueBuffer(fence, num_fences);
}

bool AcrylicCompositorMSCL3830::execute(int *handle)
{
    bool success = execute(NULL, 0);

    if (success) {
        if (handle != NULL)
            *handle = 1; /* dummy handle */
        else
            success = waitExecution(0);
    }

    return success;
}

bool AcrylicCompositorMSCL3830::dequeueBuffer()
{
    LOGASSERT(testDeviceState(SOURCE, STATE_QBUF) == testDeviceState(TARGET, STATE_QBUF),
              "State of the device is different: source %#x, target %#x",
              mDeviceState[SOURCE], mDeviceState[TARGET]);

    v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));

    if (!dequeueBuffer(SOURCE, &buffer))
        return false;

    if (!dequeueBuffer(TARGET, &buffer))
        return false;

    return true;
}

bool AcrylicCompositorMSCL3830::dequeueBuffer(BUFDIRECTION dir, v4l2_buffer *buffer)
{
    if (testDeviceState(dir, STATE_QBUF)) {
        buffer->type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buffer->memory = (mCurrentTypeMem[dir] == AcrylicCanvas::MT_DMABUF) ? V4L2_MEMORY_DMABUF
                                                                           : V4L2_MEMORY_USERPTR;

        if (V4L2_TYPE_IS_MULTIPLANAR(buffer->type)) {
            v4l2_plane planes[4];

            memset(planes, 0, sizeof(planes));

            buffer->length = 4;
            buffer->m.planes = planes;
        }

        ALOGD_TEST("VIDIOC_DQBUF: v4l2_buffer .type=%d, .memory=%d, .length=%d",
                   buffer->type, buffer->memory, buffer->length);

        if (mDev.ioctl(VIDIOC_DQBUF, buffer) < 0) {
            ALOGERR("Failed VIDIOC_DQBUF: type=%d, memory=%d", buffer->type, buffer->memory);
            return false;
        } else if (!!(buffer->flags & V4L2_BUF_FLAG_ERROR)) {
            ALOGE("Error during streaming: type=%d, memory=%d", buffer->type, buffer->memory);
            return false;
        }

        clearDeviceState(dir, STATE_QBUF);

        // The clients of V4L2 capture/m2m device should identify and verify the payload
        // written by the device and the expected payload but MSCL driver does not specify
        // the payload written by it.
        // The driver assumes that the payload of the result of MSCL is always the same as
        // the expected and it is always true in fact.
        // Therefore checking the payload written by the device is ignored here.
    }

    return true;

}

bool AcrylicCompositorMSCL3830::waitExecution(int __unused handle)
{
    return dequeueBuffer();
}

bool AcrylicCompositorMSCL3830::requestPerformanceQoS(AcrylicPerformanceRequest *request)
{
    uint32_t framerate;

    if (!request || (request->getFrameCount() == 0))
        framerate = 0;
    else
        framerate = request->getFrame(0)->mFrameRate;

    v4l2_control ctrl;

    ctrl.id = SC_CID_FRAMERATE;
    ctrl.value = framerate;
    ALOGD_TEST("VIDIOC_S_CTRL: framerate=%d", ctrl.value);
    /* For compatiblity, error is not returned. */
    if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0)
        ALOGD("Trying set frame rate is failed, but ignored");

    return true;
}
