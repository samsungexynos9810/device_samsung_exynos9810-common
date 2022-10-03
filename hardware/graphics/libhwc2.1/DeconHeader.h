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
#ifndef ___SAMSUNG_DECON_H__
#define ___SAMSUNG_DECON_H__
#include "DeconCommonHeader.h"
#define S3C_FB_MAX_WIN (4)
#define MAX_DECON_WIN (4)
#define DECON_WIN_UPDATE_IDX MAX_DECON_WIN
#define MAX_PLANE_CNT (3)
#define SUCCESS_EXYNOS_SMC 0
#define HDR_CAPABILITIES_NUM 4
typedef unsigned int u32;
#ifdef USES_ARCH_ARM64
typedef uint64_t dma_addr_t;
#else
typedef uint32_t dma_addr_t;
#endif
#define CHIP_VER (9610)
#define MAX_RES_NUMBER 5
#define MAX_FMT_CNT 64
#define MAX_DPP_CNT 7
struct lcd_res_info {
  unsigned int width;
  unsigned int height;
  unsigned int dsc_en;
  unsigned int dsc_width;
  unsigned int dsc_height;
};
struct lcd_mres_info {
  unsigned int mres_en;
  unsigned int mres_number;
  struct lcd_res_info res_info[MAX_RES_NUMBER];
};
struct decon_win_rect {
  int x;
  int y;
  u32 w;
  u32 h;
};
struct decon_rect {
  u32 left;
  u32 top;
  u32 right;
  u32 bottom;
};
enum decon_idma_type {
  IDMA_G0 = 0,
  IDMA_G1,
  IDMA_GF,
  IDMA_VGS0,
  MAX_DECON_DMA_TYPE,
};
struct decon_user_window {
  int x;
  int y;
};
struct dpp_params {
  dma_addr_t addr[MAX_PLANE_CNT];
  enum dpp_rotate rot;
  enum dpp_csc_eq eq_mode;
  enum dpp_comp_src comp_src;
  enum dpp_hdr_standard hdr_std;
  u32 min_luminance;
  u32 max_luminance;
};
struct decon_frame {
  int x;
  int y;
  u32 w;
  u32 h;
  u32 f_w;
  u32 f_h;
};
struct decon_win_config {
  enum {
    DECON_WIN_STATE_DISABLED = 0,
    DECON_WIN_STATE_COLOR,
    DECON_WIN_STATE_BUFFER,
    DECON_WIN_STATE_UPDATE,
    DECON_WIN_STATE_CURSOR,
  } state;
  union {
    __u32 color;
    struct {
      int fd_idma[3];
      int acq_fence;
      int rel_fence;
      int plane_alpha;
      enum decon_blending blending;
      enum decon_idma_type idma_type;
      enum decon_pixel_format format;
      struct dpp_params dpp_parm;
      struct decon_win_rect block_area;
      struct decon_win_rect transparent_area;
      struct decon_win_rect opaque_area;
      struct decon_frame src;
    };
  };
  struct decon_frame dst;
  bool protection;
  bool compression;
};
struct decon_win_config_data {
  int retire_fence;
  int fd_odma;
  struct decon_win_config config[MAX_DECON_WIN + 1];
};
struct decon_disp_info {
  enum hwc_ver ver;
  enum decon_psr_mode psr_mode;
  struct lcd_mres_info mres_info;
  u32 chip_ver;
  uint8_t reverved[128];
};
struct decon_dual_display_blank_data {
  enum {
    DECON_PRIMARY_DISPLAY = 0,
    DECON_SECONDARY_DISPLAY,
  } display_type;
  int blank;
};
#endif
