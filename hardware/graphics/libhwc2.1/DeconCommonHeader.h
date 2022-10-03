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
#ifndef _DECON_COMMON_HELPER_H
#define _DECON_COMMON_HELPER_H
#define HDR_CAPABILITIES_NUM 4
#define MAX_FMT_CNT 64
#define MAX_DPP_CNT 7
typedef unsigned int u32;
enum decon_psr_mode {
  DECON_VIDEO_MODE = 0,
  DECON_DP_PSR_MODE = 1,
  DECON_MIPI_COMMAND_MODE = 2,
};
enum decon_pixel_format {
  DECON_PIXEL_FORMAT_ARGB_8888 = 0,
  DECON_PIXEL_FORMAT_ABGR_8888,
  DECON_PIXEL_FORMAT_RGBA_8888,
  DECON_PIXEL_FORMAT_BGRA_8888,
  DECON_PIXEL_FORMAT_XRGB_8888,
  DECON_PIXEL_FORMAT_XBGR_8888,
  DECON_PIXEL_FORMAT_RGBX_8888,
  DECON_PIXEL_FORMAT_BGRX_8888,
  DECON_PIXEL_FORMAT_RGBA_5551,
  DECON_PIXEL_FORMAT_BGRA_5551,
  DECON_PIXEL_FORMAT_ABGR_4444,
  DECON_PIXEL_FORMAT_RGBA_4444,
  DECON_PIXEL_FORMAT_BGRA_4444,
  DECON_PIXEL_FORMAT_RGB_565,
  DECON_PIXEL_FORMAT_BGR_565,
  DECON_PIXEL_FORMAT_ARGB_2101010,
  DECON_PIXEL_FORMAT_ABGR_2101010,
  DECON_PIXEL_FORMAT_RGBA_1010102,
  DECON_PIXEL_FORMAT_BGRA_1010102,
  DECON_PIXEL_FORMAT_NV16,
  DECON_PIXEL_FORMAT_NV61,
  DECON_PIXEL_FORMAT_YVU422_3P,
  DECON_PIXEL_FORMAT_NV12,
  DECON_PIXEL_FORMAT_NV21,
  DECON_PIXEL_FORMAT_NV12M,
  DECON_PIXEL_FORMAT_NV21M,
  DECON_PIXEL_FORMAT_YUV420,
  DECON_PIXEL_FORMAT_YVU420,
  DECON_PIXEL_FORMAT_YUV420M,
  DECON_PIXEL_FORMAT_YVU420M,
  DECON_PIXEL_FORMAT_NV12N,
  DECON_PIXEL_FORMAT_NV12N_10B,
  DECON_PIXEL_FORMAT_NV12M_P010,
  DECON_PIXEL_FORMAT_NV21M_P010,
  DECON_PIXEL_FORMAT_NV12M_S10B,
  DECON_PIXEL_FORMAT_NV21M_S10B,
  DECON_PIXEL_FORMAT_NV16M_P210,
  DECON_PIXEL_FORMAT_NV61M_P210,
  DECON_PIXEL_FORMAT_NV16M_S10B,
  DECON_PIXEL_FORMAT_NV61M_S10B,
  DECON_PIXEL_FORMAT_NV12_P010,
  DECON_PIXEL_FORMAT_NV12M_SBWC_8B,
  DECON_PIXEL_FORMAT_NV12M_SBWC_10B,
  DECON_PIXEL_FORMAT_NV21M_SBWC_8B,
  DECON_PIXEL_FORMAT_NV21M_SBWC_10B,
  DECON_PIXEL_FORMAT_NV12N_SBWC_8B,
  DECON_PIXEL_FORMAT_NV12N_SBWC_10B,
  /* formats for lossy SBWC case  */
  DECON_PIXEL_FORMAT_NV12M_SBWC_8B_L50,
  DECON_PIXEL_FORMAT_NV12M_SBWC_8B_L75,
  DECON_PIXEL_FORMAT_NV12N_SBWC_8B_L50,
  DECON_PIXEL_FORMAT_NV12N_SBWC_8B_L75,
  DECON_PIXEL_FORMAT_NV12M_SBWC_10B_L40,
  DECON_PIXEL_FORMAT_NV12M_SBWC_10B_L60,
  DECON_PIXEL_FORMAT_NV12M_SBWC_10B_L80,
  DECON_PIXEL_FORMAT_NV12N_SBWC_10B_L40,
  DECON_PIXEL_FORMAT_NV12N_SBWC_10B_L60,
  DECON_PIXEL_FORMAT_NV12N_SBWC_10B_L80,
  DECON_PIXEL_FORMAT_MAX,
};
enum decon_blending {
  DECON_BLENDING_NONE = 0,
  DECON_BLENDING_PREMULT = 1,
  DECON_BLENDING_COVERAGE = 2,
  DECON_BLENDING_MAX = 3,
};
enum dpp_rotate {
  DPP_ROT_NORMAL = 0x0,
  DPP_ROT_XFLIP,
  DPP_ROT_YFLIP,
  DPP_ROT_180,
  DPP_ROT_90,
  DPP_ROT_90_XFLIP,
  DPP_ROT_90_YFLIP,
  DPP_ROT_270,
};
enum dpp_comp_src {
  DPP_COMP_SRC_NONE = 0,
  DPP_COMP_SRC_G2D,
  DPP_COMP_SRC_GPU
};
enum dpp_csc_eq {
  CSC_STANDARD_SHIFT = 0,
  CSC_BT_601 = 0,
  CSC_BT_709 = 1,
  CSC_BT_2020 = 2,
  CSC_DCI_P3 = 3,
  CSC_BT_601_625,
  CSC_BT_601_625_UNADJUSTED,
  CSC_BT_601_525,
  CSC_BT_601_525_UNADJUSTED,
  CSC_BT_2020_CONSTANT_LUMINANCE,
  CSC_BT_470M,
  CSC_FILM,
  CSC_ADOBE_RGB,
  CSC_UNSPECIFIED = 63,
  CSC_RANGE_SHIFT = 6,
  CSC_RANGE_LIMITED = 0x0,
  CSC_RANGE_FULL = 0x1,
  CSC_RANGE_EXTENDED,
  CSC_RANGE_UNSPECIFIED = 7
};
enum dpp_hdr_standard {
  DPP_HDR_OFF = 0,
  DPP_HDR_ST2084,
  DPP_HDR_HLG,
  DPP_TRANSFER_LINEAR,
  DPP_TRANSFER_SRGB,
  DPP_TRANSFER_SMPTE_170M,
  DPP_TRANSFER_GAMMA2_2,
  DPP_TRANSFER_GAMMA2_6,
  DPP_TRANSFER_GAMMA2_8
};
enum hwc_ver {
  HWC_INIT = 0,
  HWC_1_0 = 1,
  HWC_2_0 = 2,
};
enum disp_pwr_mode {
  DECON_POWER_MODE_OFF = 0,
  DECON_POWER_MODE_DOZE,
  DECON_POWER_MODE_NORMAL,
  DECON_POWER_MODE_DOZE_SUSPEND,
};
struct decon_color_mode_info {
  int index;
  uint32_t color_mode;
};

struct decon_display_mode {
  uint32_t index;
  uint32_t width;
  uint32_t height;
  uint32_t mm_width;
  uint32_t mm_height;
  uint32_t fps;
};

struct decon_brightness {
  uint32_t current_brightness;
  uint32_t max_brightness;
};

enum dpp_attr {
  DPP_ATTR_AFBC = 0,
  DPP_ATTR_BLOCK = 1,
  DPP_ATTR_FLIP = 2,
  DPP_ATTR_ROT = 3,
  DPP_ATTR_CSC = 4,
  DPP_ATTR_SCALE = 5,
  DPP_ATTR_HDR = 6,
  DPP_ATTR_C_HDR = 7,
  DPP_ATTR_C_HDR10_PLUS = 8,
  DPP_ATTR_WCG = 9,
  DPP_ATTR_IDMA = 16,
  DPP_ATTR_ODMA = 17,
  DPP_ATTR_DPP = 18,
};
struct decon_hdr_capabilities {
  unsigned int out_types[HDR_CAPABILITIES_NUM];
};
struct decon_hdr_capabilities_info {
  int out_num;
  int max_luminance;
  int max_average_luminance;
  int min_luminance;
};
struct dpp_size_range {
  u32 min;
  u32 max;
  u32 align;
};
struct dpp_restriction {
  struct dpp_size_range src_f_w;
  struct dpp_size_range src_f_h;
  struct dpp_size_range src_w;
  struct dpp_size_range src_h;
  u32 src_x_align;
  u32 src_y_align;
  struct dpp_size_range dst_f_w;
  struct dpp_size_range dst_f_h;
  struct dpp_size_range dst_w;
  struct dpp_size_range dst_h;
  u32 dst_x_align;
  u32 dst_y_align;
  struct dpp_size_range blk_w;
  struct dpp_size_range blk_h;
  u32 blk_x_align;
  u32 blk_y_align;
  u32 src_h_rot_max;
  u32 format[MAX_FMT_CNT];
  int format_cnt;
  u32 scale_down;
  u32 scale_up;
  u32 reserved[6];
};
struct dpp_ch_restriction {
  int id;
  unsigned long attr;
  struct dpp_restriction restriction;
  u32 reserved[4];
};
struct dpp_restrictions_info {
  u32 ver;
  struct dpp_ch_restriction dpp_ch[MAX_DPP_CNT];
  int dpp_cnt;
  u32 reserved[4];
};
#define DECON_MATRIX_ELEMENT_NUM 16
struct decon_color_transform_info {
  u32 hint;
  int matrix[DECON_MATRIX_ELEMENT_NUM];
};
struct decon_render_intents_num_info {
  u32 color_mode;
  u32 render_intent_num;
};
struct decon_render_intent_info {
  u32 color_mode;
  u32 index;
  u32 render_intent;
};
struct decon_color_mode_with_render_intent_info {
  u32 color_mode;
  u32 render_intent;
};
struct decon_readback_attribute {
  u32 format;
  u32 dataspace;
};

#define MAX_EDID_BLOCK 4
#define EDID_BLOCK_SIZE 128
struct decon_edid_data {
  int size;
  uint8_t edid_data[EDID_BLOCK_SIZE * MAX_EDID_BLOCK];
};

#define S3CFB_SET_VSYNC_INT _IOW('F', 206, __u32)
#define S3CFB_DECON_SELF_REFRESH _IOW('F', 207, __u32)
#define S3CFB_WIN_CONFIG _IOW('F', 209, struct decon_win_config_data)
#define EXYNOS_DISP_INFO _IOW('F', 260, struct decon_disp_info)
#define S3CFB_FORCE_PANIC _IOW('F', 211, __u32)
#define S3CFB_WIN_POSITION _IOW('F', 222, struct decon_user_window)
#define S3CFB_POWER_MODE _IOW('F', 223, __u32)
#define EXYNOS_DISP_RESTRICTIONS _IOW('F', 261, struct dpp_restrictions_info)
#define S3CFB_START_CRC _IOW('F', 270, u32)
#define S3CFB_SEL_CRC_BITS _IOW('F', 271, u32)
#define S3CFB_GET_CRC_DATA _IOR('F', 272, u32)
#define EXYNOS_GET_DISPLAYPORT_CONFIG _IOW('F', 300, struct exynos_displayport_data)
#define EXYNOS_SET_DISPLAYPORT_CONFIG _IOW('F', 301, struct exynos_displayport_data)
#define EXYNOS_DPU_DUMP _IOW('F', 302, struct decon_win_config_data)
#define S3CFB_GET_HDR_CAPABILITIES _IOW('F', 400, struct decon_hdr_capabilities)
#define S3CFB_GET_HDR_CAPABILITIES_NUM _IOW('F', 401, struct decon_hdr_capabilities_info)
#define EXYNOS_GET_COLOR_MODE_NUM _IOW('F', 600, __u32)
#define EXYNOS_GET_COLOR_MODE _IOW('F', 601, struct decon_color_mode_info)
#define EXYNOS_SET_COLOR_MODE _IOW('F', 602, __u32)
#define EXYNOS_GET_RENDER_INTENTS_NUM _IOW('F', 610, struct decon_render_intents_num_info)
#define EXYNOS_GET_RENDER_INTENT _IOW('F', 611, struct decon_render_intent_info)
#define EXYNOS_SET_COLOR_TRANSFORM _IOW('F', 612, struct decon_color_transform_info)
#define EXYNOS_SET_COLOR_MODE_WITH_RENDER_INTENT _IOW('F', 613, struct decon_color_mode_with_render_intent_info)
#define EXYNOS_GET_READBACK_ATTRIBUTE _IOW('F', 614, struct decon_readback_attribute)

/* HWC 2.3 */
#define EXYNOS_GET_DISPLAY_MODE_NUM _IOW('F', 700, u32)
#define EXYNOS_GET_DISPLAY_MODE _IOW('F', 701, struct decon_display_mode)
#define EXYNOS_SET_DISPLAY_MODE _IOW('F', 702, struct decon_display_mode)
#define EXYNOS_SET_DISPLAY_RESOLUTION _IOW('F', 703, struct decon_display_mode)
#define EXYNOS_SET_DISPLAY_REFRESH_RATE _IOW('F', 704, struct decon_display_mode)
#define EXYNOS_GET_EDID _IOW('F', 800, struct decon_edid_data)
#endif
