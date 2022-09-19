/****************************************************************************
 ****************************************************************************
 ***
 ***   This header was automatically generated from a Linux kernel header
 ***   of the same name, to make information necessary for userspace to
 ***   call into the kernel available to libc.  It contains only constants,
 ***   structures, and macros generated from the original header, and thus,
 ***   contains no copyrightable information.
 ***
 ***   To edit the content of this header, modify the corresponding
 ***   source file (e.g. under external/kernel-headers/original/) then
 ***   run bionic/libc/kernel/tools/update_all.py
 ***
 ***   Any manual change here will be lost the next time this script will
 ***   be run. You've been warned!
 ***
 ****************************************************************************
 ****************************************************************************/
#ifndef __LINUX_VIDEODEV2_EXYNOS_MEDIA_H
#define __LINUX_VIDEODEV2_EXYNOS_MEDIA_H
#include <linux/videodev2.h>
#define V4L2_CID_EXYNOS_BASE (V4L2_CTRL_CLASS_USER | 0x2000)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define V4L2_CID_CACHEABLE (V4L2_CID_EXYNOS_BASE + 10)
#define V4L2_CID_CSC_EQ_MODE (V4L2_CID_EXYNOS_BASE + 100)
#define V4L2_CID_CSC_EQ (V4L2_CID_EXYNOS_BASE + 101)
#define V4L2_CID_CSC_RANGE (V4L2_CID_EXYNOS_BASE + 102)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define V4L2_CID_CONTENT_PROTECTION (V4L2_CID_EXYNOS_BASE + 201)
#define V4L2_PIX_FMT_NV12N v4l2_fourcc('N', 'N', '1', '2')
#define V4L2_PIX_FMT_NV12NT v4l2_fourcc('T', 'N', '1', '2')
#define V4L2_PIX_FMT_YUV420N v4l2_fourcc('Y', 'N', '1', '2')
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define V4L2_PIX_FMT_NV12N_10B v4l2_fourcc('B', 'N', '1', '2')
#define V4L2_PIX_FMT_NV12M_S10B v4l2_fourcc('B', 'M', '1', '2')
#define V4L2_PIX_FMT_NV21M_S10B v4l2_fourcc('B', 'M', '2', '1')
#define V4L2_PIX_FMT_NV16M_S10B v4l2_fourcc('B', 'M', '1', '6')
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define V4L2_PIX_FMT_NV61M_S10B v4l2_fourcc('B', 'M', '6', '1')
#define V4L2_PIX_FMT_NV12M_P010 v4l2_fourcc('P', 'M', '1', '2')
#define V4L2_PIX_FMT_NV21M_P010 v4l2_fourcc('P', 'M', '2', '1')
#define V4L2_PIX_FMT_NV16M_P210 v4l2_fourcc('P', 'M', '1', '6')
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define V4L2_PIX_FMT_NV12M_SBWC_8B  v4l2_fourcc('M', '1', 'S', '8')
#define V4L2_PIX_FMT_NV12M_SBWC_10B v4l2_fourcc('M', '1', 'S', '1')
#define V4L2_PIX_FMT_NV21M_SBWC_8B  v4l2_fourcc('M', '2', 'S', '8')
#define V4L2_PIX_FMT_NV21M_SBWC_10B v4l2_fourcc('M', '2', 'S', '1')
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define V4L2_PIX_FMT_NV12N_SBWC_8B  v4l2_fourcc('N', '1', 'S', '8')
#define V4L2_PIX_FMT_NV12N_SBWC_10B v4l2_fourcc('N', '1', 'S', '1')
#define V4L2_PIX_FMT_NV12M_SBWCL_8B v4l2_fourcc('M', '1', 'L', '8')
#define V4L2_PIX_FMT_NV12M_SBWCL_10B    v4l2_fourcc('M', '1', 'L', '1')
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define V4L2_PIX_FMT_NV12N_SBWCL_8B v4l2_fourcc('N', '1', 'L', '8')
#define V4L2_PIX_FMT_NV12N_SBWCL_10B    v4l2_fourcc('N', '1', 'L', '1')
#define V4L2_PIX_FMT_NV61M_P210 v4l2_fourcc('P', 'M', '6', '1')
#ifndef __ALIGN_UP
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define __ALIGN_UP(x,a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif
#define NV12N_Y_SIZE(w,h) (__ALIGN_UP((w), 16) * __ALIGN_UP((h), 16) + 256)
#define NV12N_CBCR_SIZE(w,h) (__ALIGN_UP((__ALIGN_UP((w), 16) * (__ALIGN_UP((h), 16) / 2) + 256), 16))
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define NV12N_CBCR_BASE(base,w,h) ((base) + NV12N_Y_SIZE((w), (h)))
#define NV12N_10B_Y_8B_SIZE(w,h) (__ALIGN_UP((w), 64) * __ALIGN_UP((h), 16) + 256)
#define NV12N_10B_Y_2B_SIZE(w,h) ((__ALIGN_UP((w) / 4, 16) * __ALIGN_UP((h), 16) + 64))
#define NV12N_10B_CBCR_8B_SIZE(w,h) (__ALIGN_UP((__ALIGN_UP((w), 64) * (__ALIGN_UP((h), 16) / 2) + 256), 16))
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define NV12N_10B_CBCR_2B_SIZE(w,h) ((__ALIGN_UP((w) / 4, 16) * (__ALIGN_UP((h), 16) / 2) + 64))
#define NV12N_10B_CBCR_BASE(base,w,h) ((base) + NV12N_10B_Y_8B_SIZE((w), (h)) + NV12N_10B_Y_2B_SIZE((w), (h)))
#define YUV420N_Y_SIZE(w,h) (__ALIGN_UP((w), 16) * __ALIGN_UP((h), 16) + 256)
#define YUV420N_CB_SIZE(w,h) (__ALIGN_UP((__ALIGN_UP((w) / 2, 16) * (__ALIGN_UP((h), 16) / 2) + 256), 16))
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define YUV420N_CR_SIZE(w,h) (__ALIGN_UP((__ALIGN_UP((w) / 2, 16) * (__ALIGN_UP((h), 16) / 2) + 256), 16))
#define YUV420N_CB_BASE(base,w,h) ((base) + YUV420N_Y_SIZE((w), (h)))
#define YUV420N_CR_BASE(base,w,h) (YUV420N_CB_BASE((base), (w), (h)) + YUV420N_CB_SIZE((w), (h)))
#define NV12M_Y_SIZE(w,h) (__ALIGN_UP((w), 64) * __ALIGN_UP((h), 16) + 256)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define NV12M_CBCR_SIZE(w,h) ((__ALIGN_UP((w), 64) * __ALIGN_UP((h), 16) / 2) + 256)
#define NV12M_Y_2B_SIZE(w,h) (__ALIGN_UP((w / 4), 16) * __ALIGN_UP((h), 16) + 256)
#define NV12M_CBCR_2B_SIZE(w,h) ((__ALIGN_UP((w / 4), 16) * __ALIGN_UP((h), 16) / 2) + 256)
#define NV16M_Y_SIZE(w,h) (__ALIGN_UP((w), 64) * __ALIGN_UP((h), 16) + 256)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define NV16M_CBCR_SIZE(w,h) (__ALIGN_UP((w), 64) * __ALIGN_UP((h), 16) + 256)
#define NV16M_Y_2B_SIZE(w,h) (__ALIGN_UP((w / 4), 16) * __ALIGN_UP((h), 16) + 256)
#define NV16M_CBCR_2B_SIZE(w,h) (__ALIGN_UP((w / 4), 16) * __ALIGN_UP((h), 16) + 256)
#define P010M_Y_SIZE(w,h) (__ALIGN_UP((w), 16) * 2 * __ALIGN_UP((h), 16) + 256)
/* WARNING: DO NOT EDIT, AUTO-GENERATED CODE - SEE TOP FOR INSTRUCTIONS */
#define P010M_CBCR_SIZE(w,h) ((__ALIGN_UP((w), 16) * 2 * __ALIGN_UP((h), 16) / 2) + 256)
#define P010_Y_SIZE(w, h) ((w) * (h) * 2)
#define P010_CBCR_SIZE(w, h) ((w) * (h))
#endif
