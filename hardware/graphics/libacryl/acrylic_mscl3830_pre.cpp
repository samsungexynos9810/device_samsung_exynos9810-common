#include <cstring>
#include <new>

#include <log/log.h>

#include <linux/v4l2-controls.h>

#include <hardware/hwcomposer2.h>

#include "acrylic_internal.h"
#include "acrylic_mscl3830_pre.h"

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

static const char *__dirname[AcrylicCompositorMSCL3830Pre::NUM_IMAGES] = {"source", "target"};

AcrylicCompositorMSCL3830Pre::AcrylicCompositorMSCL3830Pre()
    : mDev("/dev/video50"), mProtectedContent(false), mCurrentPixFmt{0, 0},
      mCurrentTypeMem{AcrylicCanvas::MT_DMABUF, AcrylicCanvas::MT_DMABUF}, mUseFenceFlag(V4L2_BUF_FLAG_USE_SYNC)
{
    memset(&mCurrentSize, 0, sizeof(mCurrentSize));
    memset(&mCurrentCrop, 0, sizeof(mCurrentCrop));

    v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (mDev.ioctl(VIDIOC_QUERYCAP, &cap) == 0) {
        if (cap.device_caps & V4L2_CAP_FENCES)
            mUseFenceFlag = V4L2_BUF_FLAG_IN_FENCE | V4L2_BUF_FLAG_OUT_FENCE;
    }
}

AcrylicCompositorMSCL3830Pre::~AcrylicCompositorMSCL3830Pre()
{
}

bool AcrylicCompositorMSCL3830Pre::resetMode(BUFDIRECTION dir)
{
    v4l2_requestbuffers reqbufs;

    reqbufs.count = 0;
    reqbufs.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    reqbufs.memory = (mCurrentTypeMem[dir] == AcrylicCanvas::MT_DMABUF) ? V4L2_MEMORY_DMABUF
                                                                        : V4L2_MEMORY_USERPTR;

    if (mDev.ioctl(VIDIOC_STREAMOFF, &reqbufs.type) < 0) {
        hw2d_coord_t coord = mCurrentSize[dir];
        ALOGERR("[Pre]Failed to streamoff to reset format of %s to %#x/%dx%d but forcing reqbufs(0)",
                __dirname[dir], mCurrentPixFmt[dir], coord.hori, coord.vert);
    }

    ALOGD_TEST("[Pre]VIDIOC_STREAMOFF: type=%d", reqbufs.type);

    if (mDev.ioctl(VIDIOC_REQBUFS, &reqbufs) < 0) {
        hw2d_coord_t coord = mCurrentSize[dir];
        ALOGERR("[Pre]Failed to reqbufs(0) to reset format of %s to %#x/%dx%d",
                __dirname[dir], mCurrentPixFmt[dir], coord.hori, coord.vert);
        return false;
    }

    ALOGD_TEST("[Pre]VIDIOC_REQBUFS: count=%d, type=%d, memory=%d", reqbufs.count, reqbufs.type, reqbufs.memory);


    return true;
}

bool AcrylicCompositorMSCL3830Pre::setFormat(BUFDIRECTION dir, AcrylicCanvas &canvas)
{
    hw2d_coord_t coord = canvas.getImageDimension();
    uint32_t pixfmt = halfmt_to_v4l2_deprecated(canvas.getFormat());
    v4l2_format fmt;

    memset(&fmt, 0, sizeof(fmt));

    // S_FMT always successes unless type is invalid.
    fmt.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = coord.hori;
    fmt.fmt.pix_mp.height = coord.vert;
    fmt.fmt.pix_mp.pixelformat = pixfmt;
    fmt.fmt.pix_mp.colorspace = haldataspace_to_v4l2(canvas.getDataspace(), coord.hori, coord.vert);
    fmt.fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
    fmt.fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
    fmt.fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
    ALOGD_TEST("[Pre]VIDIOC_S_FMT: v4l2_fmt/mp .type=%d, .width=%d, .height=%d, .pixelformat=%#x, .colorspace=%d\n"
                "                          .ycbcr_enc=%d, quantization=%d, .xfer_func=%d",
                fmt.type, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.pixelformat,
                fmt.fmt.pix_mp.colorspace, fmt.fmt.pix_mp.ycbcr_enc, fmt.fmt.pix_mp.quantization,
                fmt.fmt.pix_mp.xfer_func);

    if (mDev.ioctl(VIDIOC_S_FMT, &fmt) < 0) {
        ALOGERR("[Pre]Failed VIDIOC_S_FMT .type=%d, .width=%d, .height=%d, .pixelformat=%#x",
                fmt.type, fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);
        return false;
    }

    return true;
}

bool AcrylicCompositorMSCL3830Pre::setCrop(BUFDIRECTION dir, hw2d_rect_t rect)
{
    v4l2_crop crop;

    crop.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    crop.c.left = rect.pos.hori;
    crop.c.top = rect.pos.vert;
    crop.c.width = rect.size.hori;
    crop.c.height = rect.size.vert;
    ALOGD_TEST("[Pre]VIDIOC_S_CROP: type=%d, left=%d, top=%d, width=%d, height=%d",
            crop.type, crop.c.left, crop.c.top, crop.c.width, crop.c.height);
    if (mDev.ioctl(VIDIOC_S_CROP, &crop) < 0) {
        ALOGERR("[Pre]Failed to set crop of type %d to %dx%d@%dx%d", crop.type,
                rect.size.hori, rect.size.vert, rect.pos.hori, rect.pos.vert);

        return false;
    }

    return true;
}

bool AcrylicCompositorMSCL3830Pre::prepareExecute(BUFDIRECTION dir, AcrylicCanvas &canvas)
{
    v4l2_requestbuffers reqbufs;

    reqbufs.count = 1;
    reqbufs.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = (canvas.getBufferType() == AcrylicCanvas::MT_DMABUF)
                     ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_USERPTR;

    ALOGD_TEST("[Pre]VIDIOC_REQBUFS: v4l2_requestbuffers .count=%d, .type=%d, .memory=%d, .reserved={%d,%d}",
               reqbufs.count, reqbufs.type, reqbufs.memory, reqbufs.reserved[0], reqbufs.reserved[1]);

    if (mDev.ioctl(VIDIOC_REQBUFS, &reqbufs)) {
        ALOGERR("[Pre]Failed VIDIOC_REQBUFS: count=%d, type=%d, memory=%d", reqbufs.count, reqbufs.type, reqbufs.memory);
        return false;
    }

    if (mDev.ioctl(VIDIOC_STREAMON, &reqbufs.type) < 0) {
        ALOGERR("[Pre]Failed VIDIOC_STREAMON with type %d", reqbufs.type);
        // we don't need to cancel the previous s_fmt and reqbufs
        // because we will try it again for the next frame.
        reqbufs.count = 0;
        mDev.ioctl(VIDIOC_REQBUFS, &reqbufs); // cancel reqbufs. ignore result
        return false;
    }

    ALOGD_TEST("[Pre]VIDIOC_STREAMON: type=%d", reqbufs.type);

    return true;
}

bool AcrylicCompositorMSCL3830Pre::prepareExecute(AcrylicCanvas &canvasSrc, AcrylicCanvas &canvasDst)
{
    if (canvasDst.isProtected() != mProtectedContent) {
        v4l2_control ctrl;

        ctrl.id = V4L2_CID_CONTENT_PROTECTION;
        ctrl.value = canvasDst.isProtected();
        ALOGD_TEST("[Pre]VIDIOC_S_CTRL: V4L2_CID_CONTENT_PROTECTION=%d", ctrl.value);
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("[Pre]Failed to configure content protection to %d", ctrl.value);
            return false;
        }

        mProtectedContent = canvasDst.isProtected();
    }

    return prepareExecute(SOURCE, canvasSrc) && prepareExecute(TARGET, canvasDst);
}

bool AcrylicCompositorMSCL3830Pre::queueBuffer(AcrylicCanvas &canvas, BUFDIRECTION dir)
{
    bool dmabuf = canvas.getBufferType() == AcrylicCanvas::MT_DMABUF;
    v4l2_buffer buffer;
    v4l2_plane plane[4];

    memset(&buffer, 0, sizeof(buffer));

    buffer.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buffer.memory = dmabuf ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_USERPTR;

    if (canvas.getFence() >= 0) {
        buffer.flags = mUseFenceFlag;
        buffer.reserved = canvas.getFence();
    } else {
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
        if (V4L2_TYPE_IS_OUTPUT(buffer.type))
            plane[i].bytesused = halfmt_plane_length(canvas.getFormat(), i,
                                canvas.getImageDimension().hori, mCurrentCrop[SOURCE].size.vert);
        if (dmabuf)
            plane[i].m.fd = canvas.getDmabuf(i);
        else
            plane[i].m.userptr = reinterpret_cast<unsigned long>(canvas.getUserptr(i));

        plane[i].data_offset = canvas.getOffset(i);

        ALOGD_TEST("[Pre]VIDIOC_QBUF: plane[%d] .length=%d, .bytesused=%d, .m.fd=%d/userptr=%#lx, .offset=%#x",
                    i, plane[i].length, plane[i].bytesused,
                    plane[i].m.fd, plane[i].m.userptr, plane[i].data_offset);
    }
    buffer.length = canvas.getBufferCount();
    buffer.m.planes = plane;


    ALOGD_TEST("[Pre]             .type=%d, .memory=%d, .flags=%d, .length=%d, .reserved=%d, .reserved2=%d",
               buffer.type, buffer.memory, buffer.flags, buffer.length, buffer.reserved, buffer.reserved2);

    if (mDev.ioctl(VIDIOC_QBUF, &buffer) < 0) {
        ALOGERR("[Pre]Failed VIDOC_QBUF: type=%d, memory=%d", buffer.type, buffer.memory);
        // If qbuf is failed, next execution() call reset by this.
        clearCurrByFail();
        return false;
    }

    // NOTE: V4L2 clears V4L2_BUF_FLAG_USE_SYNC on return.
    canvas.setFence(buffer.reserved);

    return true;
}

bool AcrylicCompositorMSCL3830Pre::configureCSC(AcrylicCanvas &canvasSrc, AcrylicCanvas &canvasDst)
{
    bool csc_req = false;
    uint32_t csc_sel = 0;
    uint32_t csc_range = 0;

    if ((halfmt_chroma_subsampling(mCurrentPixFmt[SOURCE]) == 0x11) &&
        (halfmt_chroma_subsampling(mCurrentPixFmt[TARGET]) != 0x11)) {
        hw2d_coord_t coord = canvasDst.getImageDimension();

        // RGB of sRGB -> Y'CbCr
        csc_req = true;
        csc_sel = haldataspace_to_v4l2(canvasDst.getDataspace(), coord.hori, coord.vert);
    } else if ((halfmt_chroma_subsampling(mCurrentPixFmt[SOURCE]) != 0x11) &&
        (halfmt_chroma_subsampling(mCurrentPixFmt[TARGET]) == 0x11)) {
        hw2d_coord_t coord = canvasSrc.getImageDimension();

        // Y'CbCr -> RGB of sRGB
        csc_req = true;
        csc_sel = haldataspace_to_v4l2(canvasSrc.getDataspace(), coord.hori, coord.vert);
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
        ALOGD_TEST("[Pre]VIDIOC_S_CTRL: csc_matrix_sel=%d", ctrl.value);
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("[Pre]Failed to configure csc matrix to %d", ctrl.value);
            return false;
        }

        ctrl.id = V4L2_CID_CSC_RANGE;
        ctrl.value = csc_range;
        ALOGD_TEST("[Pre]VIDIOC_S_CTRL: csc_range=%d", ctrl.value);
        if (mDev.ioctl(VIDIOC_S_CTRL, &ctrl) < 0) {
            ALOGERR("[Pre]Failed to configure csc range to %d", ctrl.value);
            return false;
        }
    }

    return true;
}

bool AcrylicCompositorMSCL3830Pre::dequeueBuffer(BUFDIRECTION dir)
{
    v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));

    buffer.type = (dir == SOURCE) ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
                                    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buffer.memory = (mCurrentTypeMem[dir] == AcrylicCanvas::MT_DMABUF) ? V4L2_MEMORY_DMABUF
                                                                        : V4L2_MEMORY_USERPTR;

    if (V4L2_TYPE_IS_MULTIPLANAR(buffer.type)) {
        v4l2_plane planes[4];

        memset(planes, 0, sizeof(planes));

        buffer.length = 4;
        buffer.m.planes = planes;
    }

    ALOGD_TEST("[Pre]VIDIOC_DQBUF: v4l2_buffer .type=%d, .memory=%d, .length=%d",
                buffer.type, buffer.memory, buffer.length);

    if (mDev.ioctl(VIDIOC_DQBUF, &buffer) < 0) {
        ALOGERR("[Pre]Failed VIDIOC_DQBUF: type=%d, memory=%d", buffer.type, buffer.memory);
        return false;
    } else if (!!(buffer.flags & V4L2_BUF_FLAG_ERROR)) {
        ALOGE("[Pre]Error during streaming: type=%d, memory=%d", buffer.type, buffer.memory);
        return false;
    }


    // The clients of V4L2 capture/m2m device should identify and verify the payload
    // written by the device and the expected payload but MSCL driver does not specify
    // the payload written by it.
    // The driver assumes that the payload of the result of MSCL is always the same as
    // the expected and it is always true in fact.
    // Therefore checking the payload written by the device is ignored here.

    return true;

}

bool AcrylicCompositorMSCL3830Pre::checkReset(BUFDIRECTION dir, AcrylicCanvas &canvas, hw2d_rect_t crop)
{
    if (mCurrentSize[dir] != canvas.getImageDimension() || mCurrentPixFmt[dir] != canvas.getFormat())
        return true;
    if (mCurrentCrop[dir] != crop)
        return true;
    if (mCurrentTypeMem[dir] != canvas.getBufferType())
        return true;

    return false;
}

void AcrylicCompositorMSCL3830Pre::setCurrentInfo(BUFDIRECTION dir, AcrylicCanvas &canvas, hw2d_rect_t rect)
{
    mCurrentPixFmt[dir] = canvas.getFormat();
    mCurrentSize[dir] = canvas.getImageDimension();

    mCurrentCrop[dir] = rect;

    mCurrentTypeMem[dir] = canvas.getBufferType();
}

void AcrylicCompositorMSCL3830Pre::clearCurrByFail()
{
    memset(&mCurrentPixFmt, 0, sizeof(mCurrentPixFmt));
    mCurrentTypeMem[SOURCE] = AcrylicCanvas::MT_DMABUF;
    mCurrentTypeMem[TARGET] = AcrylicCanvas::MT_DMABUF;
    memset(&mCurrentSize, 0, sizeof(mCurrentSize));
    memset(&mCurrentCrop, 0, sizeof(mCurrentCrop));
}

class for_clear {
public:
    for_clear(AcrylicCompositorMSCL3830Pre *ptr) { this->ptr = ptr; };
    ~for_clear() { if (ptr) ptr->clearCurrByFail(); };
    bool noNeedToClear(bool ret)
    {
        this->ptr = NULL;
        return ret;
    };
private:
    AcrylicCompositorMSCL3830Pre *ptr = NULL;
};

bool AcrylicCompositorMSCL3830Pre::prepareQBuf(AcrylicLayer *layerSrc, AcrylicCanvas &canvasDst)
{
    class for_clear clearInFailByDestructor(this);

    hw2d_rect_t crop = layerSrc->getTargetRect();
    if (area_is_zero(crop)) {
        crop.pos = {.hori = 0, .vert = 0};
        crop.size = canvasDst.getImageDimension();
    }

    if (checkReset(SOURCE, *layerSrc, layerSrc->getImageRect()) || checkReset(TARGET, canvasDst, crop)) {
        if (!resetMode(SOURCE) || !resetMode(TARGET))
            return false;

        if (!configureCSC(*layerSrc, canvasDst))
            return false;

        if (!setFormat(SOURCE, *layerSrc) || !setFormat(TARGET, canvasDst))
            return false;

        if (!setCrop(SOURCE, layerSrc->getImageRect()) || !setCrop(TARGET, crop))
            return false;

        if (!prepareExecute(*layerSrc, canvasDst))
            return false;

        setCurrentInfo(SOURCE, *layerSrc, layerSrc->getImageRect());
        setCurrentInfo(TARGET, canvasDst, crop);
    } else if (!dequeueBuffer(SOURCE) || !dequeueBuffer(TARGET)) {
        ALOGE("[Pre]Error occurred in the previous image processing");
        return false;
    }

    return clearInFailByDestructor.noNeedToClear(true);
}

bool AcrylicCompositorMSCL3830Pre::execute(AcrylicLayer *layerSrc, AcrylicCanvas &canvasDst)
{
    if (!prepareQBuf(layerSrc, canvasDst))
        return false;

    if (!queueBuffer(*layerSrc, SOURCE) || !queueBuffer(canvasDst, TARGET))
        return false;

    return true;
}
