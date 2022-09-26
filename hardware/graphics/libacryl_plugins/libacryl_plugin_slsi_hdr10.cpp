/*
 *  libacryl_plugins/libacryl_plugin_slsi_hdr10.cpp
 *
 *   Copyright 2018 Samsung Electronics Co., Ltd.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <cassert>

#include <system/graphics.h>

#define LOG_TAG "libacryl_plugin_slsi_hdr10"
#include <log/log.h>

#include <hardware/exynos/g2d9810_hdr_plugin.h>

enum {
    G2D_CSC_STD_UNDEFINED = -1,
    G2D_CSC_STD_601       = 0,
    G2D_CSC_STD_709       = 1,
    G2D_CSC_STD_2020      = 2,
    G2D_CSC_STD_P3        = 3,

    G2D_CSC_STD_COUNT     = 4,
};

static char csc_std_to_matrix_index[] = {
    G2D_CSC_STD_709,                          // HAL_DATASPACE_STANDARD_UNSPECIFIED
    G2D_CSC_STD_709,                          // HAL_DATASPACE_STANDARD_BT709
    G2D_CSC_STD_601,                          // HAL_DATASPACE_STANDARD_BT601_625
    G2D_CSC_STD_601,                          // HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED
    G2D_CSC_STD_601,                          // HAL_DATASPACE_STANDARD_BT601_525
    G2D_CSC_STD_601,                          // HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED
    G2D_CSC_STD_2020,                         // HAL_DATASPACE_STANDARD_BT2020
    G2D_CSC_STD_2020,                         // HAL_DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE
    static_cast<char>(G2D_CSC_STD_UNDEFINED), // HAL_DATASPACE_STANDARD_BT470M
    static_cast<char>(G2D_CSC_STD_UNDEFINED), // HAL_DATASPACE_STANDARD_FILM
    G2D_CSC_STD_P3,                           // HAL_DATASPACE_STANDARD_DCI_P3
    static_cast<char>(G2D_CSC_STD_UNDEFINED), // HAL_DATASPACE_STANDARD_ADOBE_RGB
};

#define NUM_EOTF_COEFFICIENTS 65
#define NUM_GM_COEFFICIENTS   9
#define NUM_TM_COEFFICIENTS   33

#define EOTF_COEF(x, y) (((x) & 0x3FF) | (((y) & 0x3FFF) << 16))
#define GM_COEF(v)      (v & 0x1FFFF)
#define TM_COEF(x, y)   (((x) & 0x3FFF) | (((y) & 0x3FF) << 16))

/*****************************************************************************************************
 ****** /H/D/R/ /L/U/T/  ver. 20170818 ****************************************************************
 *****************************************************************************************************/

static uint32_t EOTF_HLG[NUM_EOTF_COEFFICIENTS] = {
    EOTF_COEF(0,     0    ), EOTF_COEF(32,    5    ), EOTF_COEF(64,    21   ), EOTF_COEF(96,    48   ),
    EOTF_COEF(128,   85   ), EOTF_COEF(160,   133  ), EOTF_COEF(192,   192  ), EOTF_COEF(224,   261  ),
    EOTF_COEF(256,   341  ), EOTF_COEF(288,   432  ), EOTF_COEF(320,   533  ), EOTF_COEF(352,   645  ),
    EOTF_COEF(384,   768  ), EOTF_COEF(416,   901  ), EOTF_COEF(448,   1045 ), EOTF_COEF(480,   1200 ),
    EOTF_COEF(512,   1365 ), EOTF_COEF(528,   1454 ), EOTF_COEF(544,   1552 ), EOTF_COEF(560,   1658 ),
    EOTF_COEF(576,   1774 ), EOTF_COEF(592,   1900 ), EOTF_COEF(608,   2038 ), EOTF_COEF(624,   2189 ),
    EOTF_COEF(640,   2353 ), EOTF_COEF(656,   2533 ), EOTF_COEF(672,   2728 ), EOTF_COEF(688,   2942 ),
    EOTF_COEF(704,   3175 ), EOTF_COEF(720,   3430 ), EOTF_COEF(736,   3707 ), EOTF_COEF(752,   4010 ),
    EOTF_COEF(768,   4341 ), EOTF_COEF(776,   4517 ), EOTF_COEF(784,   4702 ), EOTF_COEF(792,   4894 ),
    EOTF_COEF(800,   5096 ), EOTF_COEF(808,   5306 ), EOTF_COEF(816,   5525 ), EOTF_COEF(824,   5755 ),
    EOTF_COEF(832,   5994 ), EOTF_COEF(840,   6245 ), EOTF_COEF(848,   6506 ), EOTF_COEF(856,   6779 ),
    EOTF_COEF(864,   7065 ), EOTF_COEF(872,   7363 ), EOTF_COEF(880,   7674 ), EOTF_COEF(888,   7999 ),
    EOTF_COEF(896,   8339 ), EOTF_COEF(904,   8694 ), EOTF_COEF(912,   9065 ), EOTF_COEF(920,   9453 ),
    EOTF_COEF(928,   9857 ), EOTF_COEF(936,   10280), EOTF_COEF(944,   10722), EOTF_COEF(952,   11183),
    EOTF_COEF(960,   11665), EOTF_COEF(968,   12169), EOTF_COEF(976,   12695), EOTF_COEF(984,   13245),
    EOTF_COEF(992,   13819), EOTF_COEF(1000,  14418), EOTF_COEF(1008,  15045), EOTF_COEF(1016,  15699),
    EOTF_COEF(8,     684  ),
};

static uint32_t EOTF_ST2084_1000nit[NUM_EOTF_COEFFICIENTS] = {
    EOTF_COEF(0,     0    ), EOTF_COEF(64,    2    ), EOTF_COEF(128,   10   ), EOTF_COEF(160,   19   ),
    EOTF_COEF(192,   33   ), EOTF_COEF(224,   54   ), EOTF_COEF(256,   85   ), EOTF_COEF(288,   130  ),
    EOTF_COEF(304,   159  ), EOTF_COEF(320,   194  ), EOTF_COEF(336,   235  ), EOTF_COEF(352,   283  ),
    EOTF_COEF(368,   339  ), EOTF_COEF(384,   406  ), EOTF_COEF(400,   483  ), EOTF_COEF(416,   574  ),
    EOTF_COEF(432,   679  ), EOTF_COEF(448,   802  ), EOTF_COEF(464,   944  ), EOTF_COEF(480,   1110 ),
    EOTF_COEF(496,   1301 ), EOTF_COEF(512,   1523 ), EOTF_COEF(528,   1780 ), EOTF_COEF(536,   1923 ),
    EOTF_COEF(544,   2076 ), EOTF_COEF(552,   2241 ), EOTF_COEF(560,   2419 ), EOTF_COEF(568,   2610 ),
    EOTF_COEF(576,   2814 ), EOTF_COEF(584,   3034 ), EOTF_COEF(592,   3271 ), EOTF_COEF(600,   3524 ),
    EOTF_COEF(608,   3797 ), EOTF_COEF(616,   4089 ), EOTF_COEF(624,   4403 ), EOTF_COEF(632,   4740 ),
    EOTF_COEF(640,   5102 ), EOTF_COEF(648,   5490 ), EOTF_COEF(656,   5907 ), EOTF_COEF(664,   6354 ),
    EOTF_COEF(672,   6834 ), EOTF_COEF(680,   7350 ), EOTF_COEF(688,   7903 ), EOTF_COEF(696,   8496 ),
    EOTF_COEF(704,   9134 ), EOTF_COEF(712,   9817 ), EOTF_COEF(720,   10551), EOTF_COEF(728,   11339),
    EOTF_COEF(736,   12185), EOTF_COEF(744,   13093), EOTF_COEF(748,   13571), EOTF_COEF(752,   14067),
    EOTF_COEF(756,   14581), EOTF_COEF(760,   15114), EOTF_COEF(764,   15665), EOTF_COEF(768,   16237),
    EOTF_COEF(769,   16383), EOTF_COEF(770,   16383), EOTF_COEF(772,   16383), EOTF_COEF(776,   16383),
    EOTF_COEF(784,   16383), EOTF_COEF(800,   16383), EOTF_COEF(832,   16383), EOTF_COEF(896,   16383),
    EOTF_COEF(128,   0    ),
};

static uint32_t EOTF_ST2084_4000nit[NUM_EOTF_COEFFICIENTS] = {
    EOTF_COEF(0,     0    ), EOTF_COEF(32,    0    ), EOTF_COEF(48,    1    ), EOTF_COEF(56,    1    ),
    EOTF_COEF(64,    2    ), EOTF_COEF(72,    2    ), EOTF_COEF(80,    3    ), EOTF_COEF(84,    3    ),
    EOTF_COEF(88,    4    ), EOTF_COEF(92,    4    ), EOTF_COEF(94,    4    ), EOTF_COEF(96,    5    ),
    EOTF_COEF(100,   5    ), EOTF_COEF(102,   5    ), EOTF_COEF(104,   6    ), EOTF_COEF(108,   6    ),
    EOTF_COEF(112,   7    ), EOTF_COEF(120,   8    ), EOTF_COEF(128,   10   ), EOTF_COEF(130,   10   ),
    EOTF_COEF(132,   11   ), EOTF_COEF(134,   11   ), EOTF_COEF(136,   12   ), EOTF_COEF(138,   12   ),
    EOTF_COEF(140,   13   ), EOTF_COEF(142,   13   ), EOTF_COEF(144,   14   ), EOTF_COEF(146,   14   ),
    EOTF_COEF(148,   15   ), EOTF_COEF(152,   16   ), EOTF_COEF(156,   17   ), EOTF_COEF(158,   18   ),
    EOTF_COEF(160,   19   ), EOTF_COEF(168,   21   ), EOTF_COEF(176,   25   ), EOTF_COEF(192,   32   ),
    EOTF_COEF(224,   54   ), EOTF_COEF(240,   68   ), EOTF_COEF(256,   85   ), EOTF_COEF(272,   105  ),
    EOTF_COEF(288,   130  ), EOTF_COEF(304,   159  ), EOTF_COEF(320,   193  ), EOTF_COEF(352,   282  ),
    EOTF_COEF(384,   404  ), EOTF_COEF(416,   572  ), EOTF_COEF(448,   799  ), EOTF_COEF(480,   1106 ),
    EOTF_COEF(512,   1519 ), EOTF_COEF(544,   2071 ), EOTF_COEF(576,   2807 ), EOTF_COEF(608,   3773 ),
    EOTF_COEF(640,   4912 ), EOTF_COEF(768,   10549), EOTF_COEF(832,   13333), EOTF_COEF(896,   15605),
    EOTF_COEF(912,   16075), EOTF_COEF(920,   16294), EOTF_COEF(922,   16348), EOTF_COEF(923,   16374),
    EOTF_COEF(924,   16383), EOTF_COEF(928,   16383), EOTF_COEF(960,   16383), EOTF_COEF(992,   16383),
    EOTF_COEF(32,    0    ),
};

static uint32_t EOTF_SMPTE170M[NUM_EOTF_COEFFICIENTS] = {
    EOTF_COEF(0,     0    ), EOTF_COEF(64,    227  ), EOTF_COEF(96,    342  ), EOTF_COEF(112,   407  ),
    EOTF_COEF(128,   477  ), EOTF_COEF(144,   555  ), EOTF_COEF(160,   638  ), EOTF_COEF(176,   728  ),
    EOTF_COEF(192,   825  ), EOTF_COEF(208,   928  ), EOTF_COEF(224,   1038 ), EOTF_COEF(240,   1155 ),
    EOTF_COEF(256,   1278 ), EOTF_COEF(272,   1409 ), EOTF_COEF(288,   1547 ), EOTF_COEF(304,   1691 ),
    EOTF_COEF(320,   1843 ), EOTF_COEF(336,   2003 ), EOTF_COEF(352,   2169 ), EOTF_COEF(368,   2343 ),
    EOTF_COEF(384,   2524 ), EOTF_COEF(400,   2712 ), EOTF_COEF(416,   2908 ), EOTF_COEF(432,   3112 ),
    EOTF_COEF(448,   3323 ), EOTF_COEF(464,   3542 ), EOTF_COEF(480,   3769 ), EOTF_COEF(496,   4003 ),
    EOTF_COEF(512,   4245 ), EOTF_COEF(528,   4495 ), EOTF_COEF(544,   4753 ), EOTF_COEF(560,   5019 ),
    EOTF_COEF(576,   5293 ), EOTF_COEF(592,   5574 ), EOTF_COEF(608,   5864 ), EOTF_COEF(624,   6162 ),
    EOTF_COEF(640,   6468 ), EOTF_COEF(656,   6782 ), EOTF_COEF(672,   7105 ), EOTF_COEF(688,   7436 ),
    EOTF_COEF(704,   7775 ), EOTF_COEF(720,   8122 ), EOTF_COEF(736,   8478 ), EOTF_COEF(752,   8842 ),
    EOTF_COEF(768,   9215 ), EOTF_COEF(784,   9596 ), EOTF_COEF(800,   9985 ), EOTF_COEF(816,   10383),
    EOTF_COEF(832,   10790), EOTF_COEF(848,   11205), EOTF_COEF(864,   11629), EOTF_COEF(880,   12062),
    EOTF_COEF(896,   12503), EOTF_COEF(912,   12953), EOTF_COEF(928,   13412), EOTF_COEF(944,   13880),
    EOTF_COEF(960,   14356), EOTF_COEF(976,   14841), EOTF_COEF(992,   15336), EOTF_COEF(1008,  15839),
    EOTF_COEF(1016,  16094), EOTF_COEF(1020,  16222), EOTF_COEF(1022,  16286), EOTF_COEF(1023,  16318),
    EOTF_COEF(1,     65   ),
};

static uint32_t EOTF_sRGB[NUM_EOTF_COEFFICIENTS] = {
    EOTF_COEF(0,     0    ), EOTF_COEF(32,    40   ), EOTF_COEF(64,    84   ), EOTF_COEF(80,    114  ),
    EOTF_COEF(96,    149  ), EOTF_COEF(112,   189  ), EOTF_COEF(128,   235  ), EOTF_COEF(144,   287  ),
    EOTF_COEF(160,   345  ), EOTF_COEF(176,   409  ), EOTF_COEF(192,   480  ), EOTF_COEF(208,   557  ),
    EOTF_COEF(224,   642  ), EOTF_COEF(240,   733  ), EOTF_COEF(256,   832  ), EOTF_COEF(272,   938  ),
    EOTF_COEF(288,   1051 ), EOTF_COEF(304,   1172 ), EOTF_COEF(320,   1301 ), EOTF_COEF(336,   1438 ),
    EOTF_COEF(352,   1583 ), EOTF_COEF(368,   1736 ), EOTF_COEF(384,   1897 ), EOTF_COEF(400,   2066 ),
    EOTF_COEF(416,   2245 ), EOTF_COEF(432,   2431 ), EOTF_COEF(448,   2627 ), EOTF_COEF(464,   2831 ),
    EOTF_COEF(480,   3045 ), EOTF_COEF(496,   3267 ), EOTF_COEF(512,   3499 ), EOTF_COEF(528,   3740 ),
    EOTF_COEF(544,   3991 ), EOTF_COEF(560,   4251 ), EOTF_COEF(576,   4521 ), EOTF_COEF(592,   4800 ),
    EOTF_COEF(608,   5089 ), EOTF_COEF(624,   5388 ), EOTF_COEF(640,   5697 ), EOTF_COEF(656,   6017 ),
    EOTF_COEF(672,   6346 ), EOTF_COEF(688,   6686 ), EOTF_COEF(704,   7036 ), EOTF_COEF(720,   7396 ),
    EOTF_COEF(736,   7768 ), EOTF_COEF(752,   8149 ), EOTF_COEF(768,   8542 ), EOTF_COEF(784,   8945 ),
    EOTF_COEF(800,   9359 ), EOTF_COEF(816,   9784 ), EOTF_COEF(832,   10221), EOTF_COEF(848,   10668),
    EOTF_COEF(864,   11127), EOTF_COEF(880,   11597), EOTF_COEF(896,   12078), EOTF_COEF(912,   12571),
    EOTF_COEF(928,   13075), EOTF_COEF(944,   13591), EOTF_COEF(960,   14118), EOTF_COEF(976,   14658),
    EOTF_COEF(992,   15209), EOTF_COEF(1008,  15772), EOTF_COEF(1016,  16058), EOTF_COEF(1020,  16202),
    EOTF_COEF(4,     181  ),
};

static uint32_t TM_sRGB[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(64,    51   ), TM_COEF(128,   87   ), TM_COEF(256,   135  ),
    TM_COEF(384,   170  ), TM_COEF(512,   198  ), TM_COEF(640,   223  ), TM_COEF(768,   245  ),
    TM_COEF(1024,  284  ), TM_COEF(1280,  317  ), TM_COEF(1536,  346  ), TM_COEF(1792,  373  ),
    TM_COEF(2048,  398  ), TM_COEF(2560,  442  ), TM_COEF(3072,  481  ), TM_COEF(3584,  517  ),
    TM_COEF(4096,  549  ), TM_COEF(4608,  580  ), TM_COEF(5120,  608  ), TM_COEF(5376,  622  ),
    TM_COEF(5632,  635  ), TM_COEF(6144,  661  ), TM_COEF(7168,  709  ), TM_COEF(8192,  752  ),
    TM_COEF(8704,  773  ), TM_COEF(9216,  793  ), TM_COEF(10240, 831  ), TM_COEF(11264, 867  ),
    TM_COEF(12288, 901  ), TM_COEF(13312, 934  ), TM_COEF(13824, 949  ), TM_COEF(14336, 965  ),
    TM_COEF(2048,  58   ),
};

static uint32_t TM_SMPTE170M[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(256,   72   ), TM_COEF(384,   106  ), TM_COEF(512,   135  ),
    TM_COEF(640,   160  ), TM_COEF(768,   182  ), TM_COEF(1024,  222  ), TM_COEF(1280,  256  ),
    TM_COEF(1536,  286  ), TM_COEF(1792,  314  ), TM_COEF(2048,  340  ), TM_COEF(2304,  364  ),
    TM_COEF(2560,  386  ), TM_COEF(2816,  408  ), TM_COEF(3072,  428  ), TM_COEF(3584,  466  ),
    TM_COEF(3840,  484  ), TM_COEF(4096,  501  ), TM_COEF(4352,  518  ), TM_COEF(4608,  534  ),
    TM_COEF(5120,  565  ), TM_COEF(5632,  594  ), TM_COEF(6144,  622  ), TM_COEF(7168,  674  ),
    TM_COEF(8192,  722  ), TM_COEF(9216,  767  ), TM_COEF(10240, 809  ), TM_COEF(11264, 849  ),
    TM_COEF(12288, 886  ), TM_COEF(13312, 923  ), TM_COEF(14336, 957  ), TM_COEF(15360, 991  ),
    TM_COEF(1024,  32   ),
};

static uint32_t TM_SMPTE170M_1000nit[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(1,     12   ), TM_COEF(2,     17   ), TM_COEF(4,     23   ),
    TM_COEF(8,     32   ), TM_COEF(16,    45   ), TM_COEF(32,    62   ), TM_COEF(64,    86   ),
    TM_COEF(96,    104  ), TM_COEF(128,   120  ), TM_COEF(192,   146  ), TM_COEF(256,   167  ),
    TM_COEF(384,   205  ), TM_COEF(512,   237  ), TM_COEF(768,   290  ), TM_COEF(1024,  334  ),
    TM_COEF(1536,  410  ), TM_COEF(2048,  474  ), TM_COEF(2560,  529  ), TM_COEF(3072,  578  ),
    TM_COEF(3584,  622  ), TM_COEF(4096,  662  ), TM_COEF(4608,  698  ), TM_COEF(5120,  731  ),
    TM_COEF(6144,  790  ), TM_COEF(7168,  840  ), TM_COEF(8192,  882  ), TM_COEF(9216,  917  ),
    TM_COEF(10240, 947  ), TM_COEF(12288, 990  ), TM_COEF(14336, 1015 ), TM_COEF(15360, 1020 ),
    TM_COEF(16384, 1023 ),
};

static uint32_t TM_SMPTE170M_4000nit[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(1,     12   ), TM_COEF(2,     17   ), TM_COEF(4,     23   ),
    TM_COEF(8,     32   ), TM_COEF(16,    44   ), TM_COEF(32,    61   ), TM_COEF(64,    83   ),
    TM_COEF(96,    101  ), TM_COEF(128,   116  ), TM_COEF(192,   140  ), TM_COEF(256,   160  ),
    TM_COEF(384,   194  ), TM_COEF(512,   223  ), TM_COEF(768,   272  ), TM_COEF(1024,  312  ),
    TM_COEF(1536,  381  ), TM_COEF(2048,  439  ), TM_COEF(2560,  489  ), TM_COEF(3072,  534  ),
    TM_COEF(3584,  574  ), TM_COEF(4096,  612  ), TM_COEF(4608,  646  ), TM_COEF(5120,  678  ),
    TM_COEF(6144,  735  ), TM_COEF(7168,  785  ), TM_COEF(8192,  829  ), TM_COEF(9216,  867  ),
    TM_COEF(10240, 901  ), TM_COEF(12288, 956  ), TM_COEF(14336, 995  ), TM_COEF(15360, 1011 ),
    TM_COEF(16384, 1023 ),
};

static uint32_t TM_GAMMA22[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(8,     32   ), TM_COEF(16,    44   ), TM_COEF(32,    60   ),
    TM_COEF(64,    82   ), TM_COEF(128,   113  ), TM_COEF(192,   136  ), TM_COEF(256,   154  ),
    TM_COEF(384,   186  ), TM_COEF(512,   212  ), TM_COEF(768,   255  ), TM_COEF(1024,  290  ),
    TM_COEF(1280,  321  ), TM_COEF(1536,  349  ), TM_COEF(1792,  374  ), TM_COEF(2048,  398  ),
    TM_COEF(2560,  440  ), TM_COEF(3072,  478  ), TM_COEF(3584,  513  ), TM_COEF(4096,  545  ),
    TM_COEF(4608,  575  ), TM_COEF(5120,  603  ), TM_COEF(5632,  630  ), TM_COEF(6144,  655  ),
    TM_COEF(7168,  703  ), TM_COEF(8192,  747  ), TM_COEF(9216,  788  ), TM_COEF(10240, 826  ),
    TM_COEF(11264, 863  ), TM_COEF(12288, 898  ), TM_COEF(13312, 931  ), TM_COEF(14336, 963  ),
    TM_COEF(2048,  60   ),
};

static uint32_t TM_GAMMA22_1000nit[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(1,     16   ), TM_COEF(2,     22   ), TM_COEF(4,     31   ),
    TM_COEF(8,     42   ), TM_COEF(16,    57   ), TM_COEF(32,    79   ), TM_COEF(64,    108  ),
    TM_COEF(96,    130  ), TM_COEF(128,   148  ), TM_COEF(192,   178  ), TM_COEF(256,   203  ),
    TM_COEF(384,   244  ), TM_COEF(512,   278  ), TM_COEF(768,   334  ), TM_COEF(1024,  381  ),
    TM_COEF(1536,  458  ), TM_COEF(2048,  522  ), TM_COEF(2560,  577  ), TM_COEF(3072,  627  ),
    TM_COEF(4096,  715  ), TM_COEF(5120,  791  ), TM_COEF(6144,  860  ), TM_COEF(6656,  891  ),
    TM_COEF(7168,  917  ), TM_COEF(7680,  936  ), TM_COEF(8192,  950  ), TM_COEF(9216,  971  ),
    TM_COEF(10240, 985  ), TM_COEF(12288, 1004 ), TM_COEF(14336, 1015 ), TM_COEF(15360, 1019 ),
    TM_COEF(1024,  4    ),
};

static uint32_t TM_GAMMA22_4000nit[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(1,     16   ), TM_COEF(2,     22   ), TM_COEF(4,     31   ),
    TM_COEF(8,     42   ), TM_COEF(16,    57   ), TM_COEF(32,    79   ), TM_COEF(64,    108  ),
    TM_COEF(96,    130  ), TM_COEF(128,   148  ), TM_COEF(192,   178  ), TM_COEF(256,   203  ),
    TM_COEF(384,   244  ), TM_COEF(512,   278  ), TM_COEF(768,   334  ), TM_COEF(1024,  381  ),
    TM_COEF(1536,  458  ), TM_COEF(2048,  522  ), TM_COEF(2560,  577  ), TM_COEF(3072,  627  ),
    TM_COEF(3584,  672  ), TM_COEF(4096,  710  ), TM_COEF(4608,  743  ), TM_COEF(5120,  771  ),
    TM_COEF(6144,  818  ), TM_COEF(7168,  856  ), TM_COEF(8192,  888  ), TM_COEF(9216,  914  ),
    TM_COEF(10240, 936  ), TM_COEF(12288, 972  ), TM_COEF(14336, 1001 ), TM_COEF(15360, 1012 ),
    TM_COEF(1024,  11   ),
};

/*
static uint32_t TM_GAMMA24[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(16,    57   ), TM_COEF(32,    76   ), TM_COEF(64,    101  ),
    TM_COEF(128,   135  ), TM_COEF(256,   181  ), TM_COEF(384,   214  ), TM_COEF(512,   241  ),
    TM_COEF(768,   286  ), TM_COEF(1024,  322  ), TM_COEF(1280,  354  ), TM_COEF(1536,  382  ),
    TM_COEF(2048,  430  ), TM_COEF(2560,  472  ), TM_COEF(3072,  509  ), TM_COEF(3584,  543  ),
    TM_COEF(4096,  574  ), TM_COEF(4608,  603  ), TM_COEF(5120,  630  ), TM_COEF(6144,  680  ),
    TM_COEF(7168,  725  ), TM_COEF(7680,  746  ), TM_COEF(8192,  766  ), TM_COEF(8704,  786  ),
    TM_COEF(9216,  805  ), TM_COEF(10240, 841  ), TM_COEF(11264, 875  ), TM_COEF(12288, 907  ),
    TM_COEF(12800, 923  ), TM_COEF(13312, 938  ), TM_COEF(14336, 968  ), TM_COEF(15360, 996  ),
    TM_COEF(1024,  27   ),
}
*/

static uint32_t TM_GAMMA26[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(16,    71   ), TM_COEF(32,    93   ), TM_COEF(64,    121  ),
    TM_COEF(128,   158  ), TM_COEF(192,   185  ), TM_COEF(256,   207  ), TM_COEF(384,   242  ),
    TM_COEF(512,   270  ), TM_COEF(640,   294  ), TM_COEF(768,   315  ), TM_COEF(1024,  352  ),
    TM_COEF(1280,  384  ), TM_COEF(1536,  412  ), TM_COEF(1792,  437  ), TM_COEF(2048,  460  ),
    TM_COEF(2560,  501  ), TM_COEF(2816,  520  ), TM_COEF(3072,  537  ), TM_COEF(3584,  570  ),
    TM_COEF(4096,  600  ), TM_COEF(4608,  628  ), TM_COEF(5120,  654  ), TM_COEF(6144,  702  ),
    TM_COEF(7168,  744  ), TM_COEF(8192,  784  ), TM_COEF(9216,  820  ), TM_COEF(10240, 854  ),
    TM_COEF(11264, 886  ), TM_COEF(12288, 916  ), TM_COEF(14336, 972  ), TM_COEF(15360, 998  ),
    TM_COEF(1024,  25   ),
};

static uint32_t TM_ST2084_1000nit[NUM_TM_COEFFICIENTS] = {
    TM_COEF(0,     0    ), TM_COEF(4,     91   ), TM_COEF(8,     119  ), TM_COEF(16,    152  ),
    TM_COEF(32,    191  ), TM_COEF(64,    236  ), TM_COEF(96,    265  ), TM_COEF(128,   286  ),
    TM_COEF(192,   319  ), TM_COEF(256,   343  ), TM_COEF(384,   379  ), TM_COEF(512,   405  ),
    TM_COEF(640,   426  ), TM_COEF(768,   444  ), TM_COEF(1024,  472  ), TM_COEF(1280,  494  ),
    TM_COEF(1536,  513  ), TM_COEF(2048,  542  ), TM_COEF(2560,  566  ), TM_COEF(3072,  585  ),
    TM_COEF(3584,  601  ), TM_COEF(4096,  616  ), TM_COEF(4608,  629  ), TM_COEF(5120,  640  ),
    TM_COEF(6144,  660  ), TM_COEF(7168,  677  ), TM_COEF(8192,  692  ), TM_COEF(9216,  705  ),
    TM_COEF(10240, 716  ), TM_COEF(11264, 727  ), TM_COEF(12288, 737  ), TM_COEF(14336, 754  ),
    TM_COEF(2048,  15   ),
};

static uint32_t GM_709_to_P3[NUM_GM_COEFFICIENTS] = {
    GM_COEF(13475), GM_COEF(  544), GM_COEF(  280),
    GM_COEF( 2909), GM_COEF(15840), GM_COEF( 1186),
    GM_COEF(    0), GM_COEF(    0), GM_COEF(14918),
};

static uint32_t GM_709_to_2020[NUM_GM_COEFFICIENTS] = {
    GM_COEF(10279), GM_COEF( 1132), GM_COEF(  269),
    GM_COEF( 5395), GM_COEF(15066), GM_COEF( 1442),
    GM_COEF(  710), GM_COEF(  186), GM_COEF(14673),
};

static uint32_t GM_P3_to_709[NUM_GM_COEFFICIENTS] = {
    GM_COEF(20069), GM_COEF( -689), GM_COEF( -322),
    GM_COEF(-3685), GM_COEF(17073), GM_COEF(-1288),
    GM_COEF(    0), GM_COEF(    0), GM_COEF(17994),
};

static uint32_t GM_P3_to_2020[NUM_GM_COEFFICIENTS] = {
    GM_COEF(12351), GM_COEF(  749), GM_COEF(  -20),
    GM_COEF( 3254), GM_COEF(15430), GM_COEF(  288),
    GM_COEF(  779), GM_COEF(  204), GM_COEF(16115),
};

static uint32_t GM_2020_to_709[NUM_GM_COEFFICIENTS] = {
    GM_COEF(27205), GM_COEF(-2041), GM_COEF( -297),
    GM_COEF(-9628), GM_COEF(18561), GM_COEF(-1648),
    GM_COEF(-1194), GM_COEF( -137), GM_COEF(18329),
};

static uint32_t GM_2020_to_P3[NUM_GM_COEFFICIENTS] = {
    GM_COEF(22013), GM_COEF(-1070), GM_COEF(   46),
    GM_COEF(-4623), GM_COEF(17626), GM_COEF( -321),
    GM_COEF(-1006), GM_COEF( -172), GM_COEF(16659),
};

// table index: (dataspace_t & HAL_DATASPACE_TRANSFER_MASK) >> HAL_DATASPACE_TRANSFER_SHIFT
// SMPTE 170M is used by BT.601, 709 and 2020 therefore it is used the default EOTF
// if transfer function is not specified.
// All Looup loop table entries have three different versions of tables. The tables have different
// coefficients calculated from the same transfer function with different domains.
// The maximum luminance of the first LUT is 400 nit while the second is 1000 nit and the third is
// 4000 nit. If a user of libacryl incorrectly configures the luminance range of the mastering display,
// an incorrect LUT will be selected.
enum {TRANSFER_IDX_SDR = 0, TRANSFER_IDX_HDR1000, TRANSFER_IDX_HDR4000};
static uint32_t *EOTF_LookUpTable[9][3] = {
//   TRANSFER_IDX_SDR     TRANSFER_IDX_HDR1000 TRANSFER_IDX_HDR4000
    {EOTF_SMPTE170M,      EOTF_SMPTE170M,      EOTF_SMPTE170M     }, // HAL_DATASPACE_TRANSFER_UNSPECIFIED
    {NULL,                NULL,                NULL               }, // HAL_DATASPACE_TRANSFER_LINEAR
    {EOTF_sRGB,           EOTF_sRGB,           EOTF_sRGB          }, // HAL_DATASPACE_TRANSFER_SRGB
    {EOTF_SMPTE170M,      EOTF_SMPTE170M,      EOTF_SMPTE170M     }, // HAL_DATASPACE_TRANSFER_SMPTE_170M
    {NULL,                NULL,                NULL               }, // HAL_DATASPACE_TRANSFER_GAMMA2_2
    {NULL,                NULL,                NULL               }, // HAL_DATASPACE_TRANSFER_GAMMA2_6
    {NULL,                NULL,                NULL               }, // HAL_DATASPACE_TRANSFER_GAMMA2_8
    {EOTF_ST2084_1000nit, EOTF_ST2084_1000nit, EOTF_ST2084_4000nit}, // HAL_DATASPACE_TRANSFER_ST2084
    {EOTF_HLG,            EOTF_HLG,            EOTF_HLG           }, // HAL_DATASPACE_TRANSFER_HLG
};

static uint32_t *TM_LookUpTable[9][3] = {
//   TRANSFER_IDX_SDR     TRANSFER_IDX_HDR1000 TRANSFER_IDX_HDR4000
    {TM_sRGB,             TM_sRGB,             TM_sRGB            }, // HAL_DATASPACE_TRANSFER_UNSPECIFIED
    {NULL,                NULL,                NULL               }, // HAL_DATASPACE_TRANSFER_LINEAR
    {TM_sRGB,             TM_sRGB,             TM_sRGB            }, // HAL_DATASPACE_TRANSFER_SRGB
    {TM_SMPTE170M,        TM_SMPTE170M_1000nit,TM_SMPTE170M_4000nit},// HAL_DATASPACE_TRANSFER_SMPTE_170M
    {TM_GAMMA22,          TM_GAMMA22_1000nit,  TM_GAMMA22_4000nit }, // HAL_DATASPACE_TRANSFER_GAMMA2_2
    {TM_GAMMA26,          TM_GAMMA26,          TM_GAMMA26         }, // HAL_DATASPACE_TRANSFER_GAMMA2_6
    {NULL,                NULL,                NULL               }, // HAL_DATASPACE_TRANSFER_GAMMA2_8
    {TM_ST2084_1000nit,   TM_ST2084_1000nit,   TM_ST2084_1000nit  }, // HAL_DATASPACE_TRANSFER_ST2084
    {NULL,                NULL,                NULL               }, // HAL_DATASPACE_TRANSFER_HLG
};

                               // source gamut      target gamut
static uint32_t *GM_LookUpTable[G2D_CSC_STD_COUNT][G2D_CSC_STD_COUNT] = {
    {   // G2D_CSC_STD_601
        NULL,
        NULL,
        GM_709_to_2020,
        GM_709_to_P3,
    }, {// G2D_CSC_STD_709
        NULL,
        NULL,
        GM_709_to_2020,
        GM_709_to_P3,
    }, {//G2D_CSC_STD_2020
        GM_2020_to_709,
        GM_2020_to_709,
        NULL,
        GM_2020_to_P3,
    }, {//G2D_CSC_STD_P3
        GM_P3_to_709,
        GM_P3_to_709,
        GM_P3_to_2020,
        NULL,
    },
};

#define DATASPACE_TO_STANDARD(dataspace) \
        (((dataspace) & HAL_DATASPACE_STANDARD_MASK) >> HAL_DATASPACE_STANDARD_SHIFT)
#define DATASPACE_TO_TRANSFER(dataspace) \
        (((dataspace) & HAL_DATASPACE_TRANSFER_MASK) >> HAL_DATASPACE_TRANSFER_SHIFT)

#define IS_TRANSFER_BASED_ON_GAMMA2_2(dataspace) \
        ((((dataspace) & HAL_DATASPACE_TRANSFER_MASK) >= HAL_DATASPACE_TRANSFER_SRGB) && \
         (((dataspace) & HAL_DATASPACE_TRANSFER_MASK) <= HAL_DATASPACE_TRANSFER_GAMMA2_2))

#define CMD_HDR_EOTF_SHIFT  0
#define CMD_HDR_GM_SHIFT    4
#define CMD_HDR_TM_SHIFT    7

uint32_t eotfOffset[NUM_EOTF_COEFFICIENTS] = {0x3200, 0x3000};
uint32_t gmOffset[NUM_GM_COEFFICIENTS]     = {0x3500, 0x3400};
uint32_t tmOffset[NUM_TM_COEFFICIENTS]     = {0x3700, 0x3600};

class HDRMatrixWriter {
    enum { HDR_MATRIX_MAX_INDEX = 2 };
public:
    HDRMatrixWriter(unsigned int dataspace)
                    : eotfMatrix{0}, gmMatrix{0}, tmMatrix{0},
                      eotfCount(0), gmCount(0), tmCount(0)
    {
        targetDataspace = dataspace;
        targetGamutRange = csc_std_to_matrix_index[DATASPACE_TO_STANDARD(dataspace)];
        tmCoeff = TM_LookUpTable[DATASPACE_TO_TRANSFER(dataspace)];
        isTmBasedOnGamma22 = IS_TRANSFER_BASED_ON_GAMMA2_2(dataspace);
    }

    bool configure(unsigned int dataspace, unsigned int max_luminance, uint32_t *command) {
        unsigned int gamut = csc_std_to_matrix_index[DATASPACE_TO_STANDARD(dataspace)];
        unsigned int tf = DATASPACE_TO_TRANSFER(dataspace);
        unsigned int luminance_index = TRANSFER_IDX_SDR;
        uint32_t *eotf, *gm;

        // We do not convert between the following similar dataspaces:
        // - BT.601, BT.709, sRGB
        // The conversion between them is done during color-space conversion.
        // BT.2020 uses the traditional transfer function with larger gamut range.
        // But we still have proper cnversion to sRGB with the traditional color-space conversion.
        if (isTmBasedOnGamma22 && IS_TRANSFER_BASED_ON_GAMMA2_2(dataspace))
            return true;

        gm = GM_LookUpTable[gamut][targetGamutRange];
        if (!gm && (tf == DATASPACE_TO_TRANSFER(targetDataspace)))
            return true;

        if (gm && !configure(gm, gmMatrix, gmCount, CMD_HDR_GM_SHIFT, command)) {
            ALOGE("Too many Gamut mapping request: dataspace %u -> %u", dataspace, targetDataspace);
            return false;
        }

        if ((tf == (HAL_DATASPACE_TRANSFER_ST2084 >> HAL_DATASPACE_TRANSFER_SHIFT)) || (max_luminance > 100))
            luminance_index = ((max_luminance < 10000) && (max_luminance > 1000))
                              ? TRANSFER_IDX_HDR4000 : TRANSFER_IDX_HDR1000;

        eotf = EOTF_LookUpTable[tf][luminance_index];
        if (eotf && !configure(eotf, eotfMatrix, eotfCount, CMD_HDR_EOTF_SHIFT, command)) {
            ALOGE("Too many EOTF request: dataspace %u -> %u / %u nit",
                  dataspace, targetDataspace, max_luminance);
            return false;
        }

        if (!configure(tmCoeff[luminance_index], tmMatrix, tmCount, CMD_HDR_TM_SHIFT, command)) {
            ALOGE("Too many Tone mapping request: dataspace %u -> %u / %u nit",
                  dataspace, targetDataspace, max_luminance);
            return false;
        }

        return true;
    }

    unsigned int getRegisterCount() {
        unsigned int count = 0;

        count += eotfCount * NUM_EOTF_COEFFICIENTS;
        count += gmCount * NUM_GM_COEFFICIENTS;
        count += tmCount * NUM_TM_COEFFICIENTS;

        return count;
    }

    unsigned int write(g2d_reg regs[]) {
        unsigned int count = 0;

        for (unsigned int i = 0; i < eotfCount; i++)
            count += writeMatrix(&regs[count], eotfMatrix[i], eotfOffset[i], NUM_EOTF_COEFFICIENTS);

        for (unsigned int i = 0; i < gmCount; i++)
            count += writeMatrix(&regs[count], gmMatrix[i], gmOffset[i], NUM_GM_COEFFICIENTS);

        for (unsigned int i = 0; i < tmCount; i++)
            count += writeMatrix(&regs[count], tmMatrix[i], tmOffset[i], NUM_TM_COEFFICIENTS);

        return count;
    }
private:
    unsigned int writeMatrix(g2d_reg regs[], uint32_t matrix[], uint32_t offset, unsigned int count) {
        for (unsigned int idx = 0; idx < count; idx++) {
            regs[idx].offset = offset;
            regs[idx].value = matrix[idx];

            offset += sizeof(uint32_t);
        }

        return count;
    }

    bool configure(uint32_t required[], uint32_t *configured[], unsigned int &count,
                   unsigned int shift, uint32_t *command) {
        unsigned int i;

        // if the given matrix is nullptr, skips that mapping.
        // null lookup table means one of the following two
        // - no conversion is required
        // - no lookup table is prepared for this case
        // If this is the latter case, we may need to develop lookup table for the case.
        if (required == nullptr)
            return true;

        for (i = 0; i < count; i++) {
            if (configured[i] == required) {
                *command |= (i << shift) | (1 << (shift + 1));
                return true;
            }
        }

        if (count == HDR_MATRIX_MAX_INDEX)
            return false;

        *command |= (count << shift) | (1 << (shift + 1));
        configured[count++] = required;

        return true;
    }

    uint32_t *eotfMatrix[HDR_MATRIX_MAX_INDEX];
    uint32_t *gmMatrix[HDR_MATRIX_MAX_INDEX];
    uint32_t *tmMatrix[HDR_MATRIX_MAX_INDEX];
    unsigned int eotfCount;
    unsigned int gmCount;
    unsigned int tmCount;

    unsigned int targetDataspace;
    unsigned int targetGamutRange;
    bool isTmBasedOnGamma22;
    uint32_t **tmCoeff;
};

#define MAX_LAYER_COUNT 16
#define NUM_HDR_COEFFICIENTS  (2 * (NUM_EOTF_COEFFICIENTS + NUM_GM_COEFFICIENTS + NUM_TM_COEFFICIENTS))
#define NUM_HDR_REGS (NUM_HDR_COEFFICIENTS + MAX_LAYER_COUNT)

class G2DHdr10CommandWriter: public IG2DHdr10CommandWriter {
    int mLayerMap;
    int mLayerAlphaMap;
    int mTargetDataspace;
    int mLayerDataspace[MAX_LAYER_COUNT];
    unsigned int mLayerMaxLuminance[MAX_LAYER_COUNT];
    struct g2d_commandlist mCommandList;
public:
    G2DHdr10CommandWriter() : mLayerMap(0), mLayerAlphaMap(0), mTargetDataspace(HAL_DATASPACE_TRANSFER_SRGB),
                              mCommandList{nullptr, nullptr, 0, 0} {
    }
    ~G2DHdr10CommandWriter() { delete [] mCommandList.commands; }

    bool setLayerStaticMetadata(int index, int dataspace, unsigned int __unused min_luminance, unsigned int max_luminance) {
        mLayerMap |= 1 << index;
        mLayerDataspace[index] = dataspace;
        mLayerMaxLuminance[index] = max_luminance;

        return true;
    }

    bool setLayerImageInfo(int index, unsigned int __unused pixfmt, bool alpha_premult) {
        if (alpha_premult)
            mLayerAlphaMap |= 1 << index;
	return true;
    }

    bool setTargetInfo(int dataspace, void * __unused data) {
        mTargetDataspace = dataspace;

        return true;
    }

    struct g2d_commandlist *getCommands() {
        int LayerMap = mLayerMap;
        int LayerAlphaMap = mLayerAlphaMap;

        // initialize for the next layer metadata configuration
        mLayerMap = 0;
        mLayerAlphaMap = 0;

        if (LayerMap == 0)
            return NULL;

        if (mCommandList.commands == NULL) {
            g2d_reg *cmds = new g2d_reg[NUM_HDR_REGS]; // 1840 bytes
            if (!cmds) {
                ALOGE("Failed to allocate command list for HDR");
                return NULL;
            }

            mCommandList.commands = cmds;
            mCommandList.layer_hdr_mode = cmds + NUM_HDR_COEFFICIENTS;
        }

        HDRMatrixWriter hdrMatrixWriter(mTargetDataspace);

        mCommandList.layer_count = 0;
        for (unsigned int i = 0; i < MAX_LAYER_COUNT; i++) {
            if (!(LayerMap & (1 << i)))
                continue;

            mCommandList.layer_hdr_mode[mCommandList.layer_count].value = 0;

            if (!hdrMatrixWriter.configure(mLayerDataspace[i], mLayerMaxLuminance[i],
                                           &mCommandList.layer_hdr_mode[mCommandList.layer_count].value)) {
                ALOGE("Failed to configure HDR coefficient of layer %d for dataspace %u",
                      i, mLayerDataspace[i]);
                return NULL;
            }

            if ((LayerAlphaMap & (1 << i)) &&
                (mCommandList.layer_hdr_mode[mCommandList.layer_count].value & ((CMD_HDR_EOTF_SHIFT | CMD_HDR_GM_SHIFT | CMD_HDR_TM_SHIFT) << 1)))
                mCommandList.layer_hdr_mode[mCommandList.layer_count].value |= G2D_LAYER_HDRMODE_DEMULT_ALPHA;

            mCommandList.layer_hdr_mode[mCommandList.layer_count].offset = 0x290 + i * 0x100; // LAYERx_HDR_MODE_REG
            mCommandList.layer_count++;
        }

        mCommandList.command_count = hdrMatrixWriter.write(mCommandList.commands);

        return &mCommandList;
    }

    void putCommands(struct g2d_commandlist __unused *commands) {
        assert(commands == &mCommandList);
    }
};

IG2DHdr10CommandWriter *IG2DHdr10CommandWriter::createInstance() {
    return new G2DHdr10CommandWriter();
}
