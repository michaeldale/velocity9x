/*
 * ATI3DCIF constants the wrapper's pure logic needs
 * (docs\plans\ati-3dcif-wrapper.md): values from ATI3DCIF.H (SDK 4.1,
 * $Revision 2.41) and the SDK-C02700 Rev 1.30 guide, read for facts under
 * the plan's licence rule. Only what the modules use; nothing is copied.
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_CIF_API_H
#define VELOCITY9X_CIF_API_H

#include "velocity9x/types.h"

/* C3D_EC */
#define V9X_C3D_EC_OK              0ul
#define V9X_C3D_EC_BADPARAM        3ul
#define V9X_C3D_EC_NOTIMPYET       6ul

/* C3D_EVERTEX. VTCF is ten floats: x, y, z, s, t, w, r, g, b, a. */
#define V9X_C3D_EV_VTCF            3ul
#define V9X_C3D_EV_TLVERTEX        4ul
#define V9X_C3D_VTCF_X             0u
#define V9X_C3D_VTCF_Y             1u
#define V9X_C3D_VTCF_Z             2u
#define V9X_C3D_VTCF_S             3u
#define V9X_C3D_VTCF_T             4u
#define V9X_C3D_VTCF_W             5u
#define V9X_C3D_VTCF_R             6u
#define V9X_C3D_VTCF_G             7u
#define V9X_C3D_VTCF_B             8u
#define V9X_C3D_VTCF_A             9u
#define V9X_C3D_VTCF_FLOATS        10u

/* C3D_EPRIM */
#define V9X_C3D_EPRIM_LINE         0ul
#define V9X_C3D_EPRIM_TRI          1ul
#define V9X_C3D_EPRIM_QUAD         2ul
#define V9X_C3D_EPRIM_RECT         3ul
#define V9X_C3D_EPRIM_POINT        4ul

/* C3D_ESHADE */
#define V9X_C3D_ESH_NONE           0ul
#define V9X_C3D_ESH_SOLID          1ul
#define V9X_C3D_ESH_FLAT           2ul
#define V9X_C3D_ESH_SMOOTH         3ul

/* C3D_EASRC and C3D_EADST: the same eight numbers, ZERO..INVDSTALPHA;
 * 2 and 3 are the destination colour on the source side and the source
 * colour on the destination side. */
#define V9X_C3D_EBLEND_ZERO        0ul
#define V9X_C3D_EBLEND_ONE         1ul
#define V9X_C3D_EBLEND_COLOR       2ul
#define V9X_C3D_EBLEND_INVCOLOR    3ul
#define V9X_C3D_EBLEND_SRCALPHA    4ul
#define V9X_C3D_EBLEND_INVSRCALPHA 5ul
#define V9X_C3D_EBLEND_DSTALPHA    6ul
#define V9X_C3D_EBLEND_INVDSTALPHA 7ul
#define V9X_C3D_EBLEND_NUM         8ul

/* C3D_ETLIGHT */
#define V9X_C3D_ETL_NONE           0ul
#define V9X_C3D_ETL_MODULATE       1ul
#define V9X_C3D_ETL_ALPHA_DECAL    2ul

/* C3D_ETEXFILTER */
#define V9X_C3D_ETFILT_MINPNT_MAGPNT   0ul
#define V9X_C3D_ETFILT_MINPNT_MAG2BY2  1ul
#define V9X_C3D_ETFILT_MIN2BY2_MAG2BY2 2ul
#define V9X_C3D_ETFILT_MIPLIN_MAGPNT   3ul
#define V9X_C3D_ETFILT_MIPLIN_MAG2BY2  4ul
#define V9X_C3D_ETFILT_MIPTRI_MAG2BY2  5ul
#define V9X_C3D_ETFILT_MIN2BY2_MAGPNT  6ul

/* C3D_ETEXOP */
#define V9X_C3D_ETEXOP_NONE        0ul
#define V9X_C3D_ETEXOP_CHROMAKEY   1ul
#define V9X_C3D_ETEXOP_ALPHA       2ul
#define V9X_C3D_ETEXOP_ALPHA_MASK  3ul

/* C3D_EZMODE and C3D_EZCMP (NEVER 0 .. ALWAYS 7). */
#define V9X_C3D_EZMODE_OFF          0ul
#define V9X_C3D_EZMODE_TESTON       1ul
#define V9X_C3D_EZMODE_TESTON_WRITEZ 2ul
#define V9X_C3D_EZCMP_ALWAYS        7ul

/* C3D_ETEXFMT */
#define V9X_C3D_ETF_CI4            1ul
#define V9X_C3D_ETF_CI8            2ul
#define V9X_C3D_ETF_RGB1555        3ul
#define V9X_C3D_ETF_RGB565         4ul
#define V9X_C3D_ETF_RGB8888        6ul
#define V9X_C3D_ETF_RGB332         7ul
#define V9X_C3D_ETF_Y8             8ul
#define V9X_C3D_ETF_YUV422         11ul
#define V9X_C3D_ETF_RGB4444        15ul
#define V9X_C3D_ETF_VQ             20ul

/* C3D_ECI_TMAP_TYPE: which nibble a CI4 texel is in, or a VQ codebook. */
#define V9X_C3D_ECI_TMAP_TRUE_COLOR 0ul
#define V9X_C3D_ECI_TMAP_4BIT_HI   1ul
#define V9X_C3D_ECI_TMAP_4BIT_LOW  2ul
#define V9X_C3D_ECI_TMAP_8BIT      3ul
#define V9X_C3D_ECI_TMAP_VQ        4ul

/* C3D_ETEXTILE: ON is the Rage PRO's interleaved texel layout. */
#define V9X_C3D_ETEXTILE_ON        2ul

/* The largest texture edge, log2 (the guide: "cannot exceed 1024x1024"). */
#define V9X_C3D_TEXTURE_LG2_MAX    10ul

/* C3D_ERSID: 37 states, ids 0..36. */
#define V9X_C3D_ERS_FG_CLR           0u
#define V9X_C3D_ERS_VERTEX_TYPE      1u
#define V9X_C3D_ERS_PRIM_TYPE        2u
#define V9X_C3D_ERS_SOLID_CLR        3u
#define V9X_C3D_ERS_SHADE_MODE       4u
#define V9X_C3D_ERS_TMAP_EN          5u
#define V9X_C3D_ERS_TMAP_SELECT      6u
#define V9X_C3D_ERS_TMAP_LIGHT       7u
#define V9X_C3D_ERS_TMAP_PERSP_COR   8u
#define V9X_C3D_ERS_TMAP_FILTER      9u
#define V9X_C3D_ERS_TMAP_TEXOP       10u
#define V9X_C3D_ERS_ALPHA_SRC        11u
#define V9X_C3D_ERS_ALPHA_DST        12u
#define V9X_C3D_ERS_SURF_DRAW_PTR    13u
#define V9X_C3D_ERS_SURF_DRAW_PITCH  14u
#define V9X_C3D_ERS_SURF_DRAW_PF     15u
#define V9X_C3D_ERS_SURF_VPORT       16u
#define V9X_C3D_ERS_FOG_EN           17u
#define V9X_C3D_ERS_DITHER_EN        18u
#define V9X_C3D_ERS_Z_CMP_FNC        19u
#define V9X_C3D_ERS_Z_MODE           20u
#define V9X_C3D_ERS_SURF_Z_PTR       21u
#define V9X_C3D_ERS_SURF_Z_PITCH     22u
#define V9X_C3D_ERS_SURF_SCISSOR     23u
#define V9X_C3D_ERS_COMPOSITE_EN     24u
#define V9X_C3D_ERS_COMPOSITE_SELECT 25u
#define V9X_C3D_ERS_COMPOSITE_FNC    26u
#define V9X_C3D_ERS_COMPOSITE_FACTOR 27u
#define V9X_C3D_ERS_COMPOSITE_FILTER 28u
#define V9X_C3D_ERS_COMPOSITE_FACTOR_ALPHA 29u
#define V9X_C3D_ERS_LOD_BIAS_LEVEL   30u
#define V9X_C3D_ERS_ALPHA_DST_TEST_ENABLE 31u
#define V9X_C3D_ERS_ALPHA_DST_TEST_FNC 32u
#define V9X_C3D_ERS_ALPHA_DST_WRITE_SELECT 33u
#define V9X_C3D_ERS_ALPHA_DST_REFERENCE 34u
#define V9X_C3D_ERS_SPECULAR_EN      35u
#define V9X_C3D_ERS_ENHANCED_COLOR_RANGE_EN 36u
#define V9X_C3D_ERS_NUM              37u

/* C3D_COLOR and C3D_PALETTENTRY are a u32 with red in the low byte, then
 * green, blue, and alpha (a palette entry's flags) in the high byte. */
#define V9X_C3D_COLOR_RED(c)   ((c) & 0xfful)
#define V9X_C3D_COLOR_GREEN(c) (((c) >> 8) & 0xfful)
#define V9X_C3D_COLOR_BLUE(c)  (((c) >> 16) & 0xfful)
#define V9X_C3D_COLOR_ALPHA(c) (((c) >> 24) & 0xfful)

#endif /* VELOCITY9X_CIF_API_H */
