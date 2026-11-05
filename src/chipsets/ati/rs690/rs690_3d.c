/*
 * ATI Radeon X1200 IGP (RS690) 3D engine register builders. See
 * include\velocity9x\rs690_3d.h for the sources and the contract.
 *
 * Every value below is either copied from xf86-video-ati 6.14.6's
 * RADEONInit3DEngine / R300PrepareComposite with the same name, or, for the
 * colour path the X driver never used, Mesa r300's software-TCL encoding.
 * Where the two drivers differ, the comment says which one is followed.
 */
#include "velocity9x/rs690_3d.h"

/* GB_TILE_CONFIG: tiling on, 16-pixel tiles, the pipe count in 3:1
 * (R300_ENABLE_TILING, R300_TILE_SIZE_16, R300_PIPE_COUNT_*). */
#define V9X_RS690_GB_TILE_BASE          0x00000011ul
#define V9X_RS690_PIPE_SELECT_SHIFT     12
#define V9X_RS690_PIPE_SELECT_MASK      0x3ul

/* GB_MSPOS0/1: every multisample position and boundary 6, as the X driver
 * programs them with antialiasing off. */
#define V9X_RS690_MSPOS0                0x66666666ul
#define V9X_RS690_MSPOS1                0x06666666ul

/* GA_ENHANCE: deadlock and fast-sync control on. GA_POLY_MODE: front and
 * back faces drawn as triangles. GA_ROUND_MODE: geometry and colour round
 * to nearest. GA_COLOR_CONTROL: all four colours Gouraud-shaded. */
#define V9X_RS690_GA_ENHANCE_VALUE      0x00000003ul
#define V9X_RS690_GA_POLY_TRIANGLES     0x00000120ul
#define V9X_RS690_GA_ROUND_NEAREST      0x00000005ul
#define V9X_RS690_GA_ALL_GOURAUD        0x0000aaaaul

/* SU_CULL_MODE: no culling, front face counter-clockwise (R300_FACE_NEG).
 * SU_DEPTH_SCALE: 16777215.0f, the X driver's value. */
#define V9X_RS690_SU_FACE_NEG           0x00000004ul
#define V9X_RS690_SU_DEPTH_SCALE_VALUE  0x4b7ffffful

/* VAP_CNTL_STATUS: R300_PVS_BYPASS, since the RS690 has no vertex shader
 * hardware. VAP_CNTL without TCL: 10 PVS slots, 5 controllers, 5 vertices
 * in flight, 2 FPUs (the family default branch of the X driver). */
#define V9X_RS690_VAP_PVS_BYPASS        0x00000100ul
#define V9X_RS690_VAP_CNTL_NO_TCL       0x0014025aul

/* VAP_VTE_CNTL: X/Y and Z already in window coordinates; no viewport
 * transform (R300_VTX_XY_FMT | R300_VTX_Z_FMT). */
#define V9X_RS690_VTE_WINDOW_COORDS     0x00000300ul

/* VAP_PROG_STREAM_CNTL_EXT_n: each stream's x, y, z, w taken straight and
 * all four written: select X/Y/Z/W (0, 1, 2, 3 at 3-bit steps) with write
 * enable 0xF at bit 12, the second stream of a register at bit 16. */
#define V9X_RS690_STREAM_SWIZZLE_XYZW   0x0000f688ul

/* RS_IP_0/1 and RS_INST_0/1 as RADEONInit3DEngine leaves them: two texture
 * coordinate interpolators feeding temps 0 and 1. The target state below
 * replaces interpolator 0 with the colour. */
#define V9X_RS690_RS_IP_TEX_ST01        0x01610000ul
#define V9X_RS690_RS_IP_TEX_PTR_2       0x00000002ul
#define V9X_RS690_RS_INST_TEX0          0x00000008ul
#define V9X_RS690_RS_INST_TEX1          0x00000049ul

/* US_OUT_FMT_n: C4_8 with blue, green, red, alpha into C0..C3, the BGRA
 * order of RGB565 and ARGB8888 buffers; unused outputs marked 15. */
#define V9X_RS690_US_OUT_BGRA           0x00001b00ul
#define V9X_RS690_US_OUT_UNUSED         0x0000000ful

/* RB3D_COLOR_CHANNEL_MASK: blue, green, red and alpha writable. */
#define V9X_RS690_CHANNELS_ALL          0x0000000ful

/* SC_EDGERULE, SC_CLIP_RULE, SC_SCREENDOOR: the X driver's values. */
#define V9X_RS690_SC_EDGERULE_VALUE     0x0a5294a5ul
#define V9X_RS690_SC_CLIP_RULE_VALUE    0x0000aaaaul
#define V9X_RS690_SC_SCREENDOOR_ALL     0x00fffffful

/*
 * The vertex stream (Mesa r300_swtcl_vertex_psc shape, the X driver's data
 * types): stream 0 FLOAT_2 into vector slot 0 (position), stream 1 FLOAT_4
 * into slot 2 (colour 0) and last. SIGNED on both, as the X driver sets it
 * on its float streams.
 */
#define V9X_RS690_STREAMS_POS2_COLOR4   0x62034001ul

/* Mesa writes VAP_VTX_STATE_CNTL 0x5555 (user colour 0 for colours 0-7,
 * "copied from classic") and VAP_VSM_VTX_ASSM position | colour for a
 * colour-carrying vertex; the X driver, which sends no colour, writes 0 and
 * nothing. Mesa's are followed. VAP_OUTPUT_VTX_FMT_0: position and colour
 * 0 present. */
#define V9X_RS690_VTX_STATE_USER_COLOR  0x00005555ul
#define V9X_RS690_VSM_POS_COLOR         0x00000005ul
#define V9X_RS690_OUT_VTX_POS_COLOR0    0x00000003ul

/* Rasterizer: interpolator 0 is colour 0, RGBA (RS_COL_PTR 0, RS_COL_FMT
 * 0); instruction 0 writes it to shader temp 0 (RS_INST_COL_ID 0,
 * COL_CN_WRITE bit 14, COL_ADDR 0). RS_COUNT: no texture components, one
 * colour (IC_COUNT 1 << 7), HIRES_EN (bit 18). */
#define V9X_RS690_RS_IP_COLOR0          0x00000000ul
#define V9X_RS690_RS_INST_COLOR0_TEMP0  0x00004000ul
#define V9X_RS690_RS_COUNT_ONE_COLOR    0x00040080ul

/*
 * The fragment shader: one node of one ALU instruction and no texture
 * instructions (US_CONFIG 0, every size field 0 = one or none), its last
 * node in CODE_ADDR_3 with RGBA_OUT (bit 22). US_PIXSIZE 1 as the X driver
 * sets it.
 *
 * RGB: out.rgb = temp0.rgb * 1 + 0 - source 0 = temp 0, output mask XYZ
 * (bits 28:26), target 0; MAD of SRC0C_XYZ (0), ONE (21 << 7), ZERO
 * (20 << 14), clamped (bit 30). Alpha: out.a = temp0.a * 1 + 0 - output
 * bit 24; SRC0A (9), ONE (17 << 7), ZERO (16 << 14), clamped.
 */
#define V9X_RS690_US_PIXSIZE_VALUE      0x00000001ul
#define V9X_RS690_US_LAST_NODE_RGBA_OUT 0x00400000ul
#define V9X_RS690_ALU_RGB_ADDR_OUT      0x1c000000ul
#define V9X_RS690_ALU_RGB_MOV           0x40050a80ul
#define V9X_RS690_ALU_ALPHA_ADDR_OUT    0x01000000ul
#define V9X_RS690_ALU_ALPHA_MOV         0x40040889ul

/*
 * Textured drawing. The vertex: stream 1 (colour) loses LAST_VEC_1 and
 * stream 2, FLOAT_2 into slot 6 (texture 0 in PVS bypass, the X driver's
 * placement), signed and last, goes in VAP_PROG_STREAM_CNTL_1. Mesa's
 * VSM_VTX_ASSM bit for texture 0 (R300_INPUT_CNTL_TC0) and two components
 * for texture 0 in VAP_OUTPUT_VTX_FMT_1, as the X driver sends them.
 */
#define V9X_RS690_STREAMS_POS2_COLOR4_MORE 0x42034001ul
#define V9X_RS690_STREAM_TEX0_FLOAT2_LAST  0x00006601ul
#define V9X_RS690_VSM_TC0               0x00000400ul
#define V9X_RS690_OUT_VTX_TEX0_ST       0x00000002ul

/* Rasterizer, textured: interpolator 0 carries s, t (RS_IP_TEX_ST01, as
 * the init sets it) and colour 0 (COL_PTR 0, RGBA); instruction 0 writes
 * the coordinates to temp 0 (RS_INST_TEX0) and the colour to temp 1
 * (COL_CN_WRITE bit 14, COL_ADDR 1 at bit 17, Mesa's field). RS_COUNT:
 * two texture components, one colour, HIRES_EN. */
#define V9X_RS690_RS_INST_COLOR0_TEMP1  0x00024000ul
#define V9X_RS690_RS_COUNT_ST_ONE_COLOR 0x00040082ul

/* Texture unit 0. TX_FILTER0: S and T clamp in 2:0 and 5:3, CLAMP_LAST
 * (2) being Direct3D's CLAMP and WRAP 0; MAG in 10:9 and MIN in 12:11,
 * nearest 1 and linear 2; no mipmap filter; unit 0 in 31:28. */
#define V9X_RS690_TX_CLAMP_ST_EDGE      0x00000012ul
#define V9X_RS690_TX_FILTER_NEAREST     0x00000a00ul
#define V9X_RS690_TX_FILTER_LINEAR      0x00001400ul

/* TX_FORMAT0: width - 1 in 10:0, height - 1 in 21:11, TXPITCH_EN (bit 31)
 * so TX_FORMAT2 gives the row pitch in texels - 1. */
#define V9X_RS690_TXHEIGHT_SHIFT        11
#define V9X_RS690_TXPITCH_EN            0x80000000ul

/* TX_FORMAT1 as R300TexFormats has them, R300_EASY_TX_FORMAT(B, G, R, A,
 * fmt) with B at 20:18, G 17:15, R 14:12, A 11:9: r5g6b5 is (X, Y, Z, ONE,
 * Z5Y6X5 6), a8r8g8b8 (X, Y, Z, W, W8Z8Y8X8 0Ch). */
#define V9X_RS690_TXFMT1_RGB565         0x0000aa06ul
#define V9X_RS690_TXFMT1_ARGB8888       0x0000a60cul
#define V9X_RS690_TEX_0_ENABLE          0x00000001ul

/* US_CONFIG FIRST_TEX: the first node has texture instructions. US_TEX_INST
 * 0: LD (1 << 15) into temp 0 (DST_ADDR 10:6) from temp 0's coordinates
 * (SRC_ADDR 5:0) with unit 0 (TEX_ID 14:11), as the X driver's. */
#define V9X_RS690_US_FIRST_TEX          0x00000008ul
#define V9X_RS690_TEX_LD_0_FROM_0       0x00008000ul

/* MODULATE: out = temp0 * temp1 + 0. Source 1 is temp 1 (ADDR1, 11:6, of
 * both address words); RGB takes SRC0_RGB (0) times SRC1_RGB (4 << 7),
 * alpha SRC0_A (9) times SRC1_A (10 << 7); C and the rest as the MOV. */
#define V9X_RS690_ALU_ADDR1_TEMP1       0x00000040ul
#define V9X_RS690_ALU_RGB_MUL           0x40050200ul
#define V9X_RS690_ALU_ALPHA_MUL         0x40040509ul

/*
 * The TL vertex (Mesa r300 data types and swizzles, r300_reg.h:238-382,
 * r300_state_inlines.h r300_translate_vertex_data_type). Stream 0: FLOAT_4
 * (3), slot 0, SIGNED - sx, sy, sz, rhw. Stream 1: BYTE (4) with NORMALIZE
 * (bit 15), slot 2 - the diffuse D3DCOLOR. Stream 2: the same into slot 3,
 * colour 1 - the specular. Stream 3: FLOAT_2 (1), slot 6, SIGNED, last -
 * tu, tv. A D3DCOLOR's bytes are B, G, R, A, so its swizzle selects
 * Z, Y, X, W (2 | 1 << 3 | 0 << 6 | 3 << 9) with all four written, as Mesa
 * gives B8G8R8A8_UNORM. VTE's W0 format bit stays clear, so the fourth
 * position component is 1/W, Mesa's software-TCL arrangement.
 */
#define V9X_RS690_STREAMS_TL_POS4_DIFFUSE  0x82044003ul
#define V9X_RS690_STREAMS_TL_SPECULAR_TEX0 0x66018304ul
#define V9X_RS690_STREAM_SWIZZLE_BGRA   0x0000f60aul
#define V9X_RS690_OUT_VTX_POS_COLOR01   0x00000007ul

/* Z: ZB_FORMAT 16-bit integer; ZB_CNTL Z_ENABLE (bit 1) and
 * Z_WRITE_ENABLE (bit 2); ZB_DEPTHPITCH in pixels in 13:2, linear
 * (micro- and macrotiling off). */
#define V9X_RS690_DEPTHFORMAT_16BIT     0x00000000ul
#define V9X_RS690_Z_ENABLE              0x00000002ul
#define V9X_RS690_Z_WRITE_ENABLE        0x00000004ul
#define V9X_RS690_DEPTHPITCH_MASK       0x00003ffcul

/* RB3D_CBLEND/ABLEND: ALPHA_BLEND_ENABLE (bit 0) and READ_ENABLE (bit 2),
 * COMB_FCN_ADD_CLAMP (0 in 14:12), source factor at 21:16, destination at
 * 29:24. */
#define V9X_RS690_BLEND_ON_READ         0x00000005ul
#define V9X_RS690_SRC_BLEND_SHIFT       16
#define V9X_RS690_DST_BLEND_SHIFT       24

/* FG_ALPHA_FUNC: reference in 7:0, compare in 10:8, enable bit 11. */
#define V9X_RS690_AFUNC_SHIFT           8
#define V9X_RS690_ALPHA_TEST_ENABLE     0x00000800ul

/* Alpha from the diffuse colour: SRC1_A (10) times ONE (17 << 7). */
#define V9X_RS690_ALU_ALPHA_DIFFUSE     0x4004088aul

/* IEEE single fields. */
#define V9X_RS690_FLOAT_EXPONENT_BIAS   127ul
#define V9X_RS690_FLOAT_MANTISSA_BITS   23
#define V9X_RS690_FLOAT_MANTISSA_MASK   0x007ffffful
#define V9X_RS690_FLOAT_INTEGER_LIMIT   0x01000000ul

#define V9X_RS690_CHANNEL_MAX           255ul

static void v9x_rs690_3d_begin(struct v9x_rs690_3d_list *list)
{
    list->count = 0u;
}

/* The list is sized for the longest sequence; a builder that overflows it
 * is a bug, and the extra writes are dropped rather than written past the
 * arrays. */
static void v9x_rs690_3d_add(struct v9x_rs690_3d_list *list, v9x_u32 offset,
                             v9x_u32 value)
{
    if (list->count >= V9X_RS690_3D_WRITES) {
        return;
    }
    list->offsets[list->count] = offset;
    list->values[list->count] = value;
    ++list->count;
}

static v9x_u32 v9x_rs690_sc_point(v9x_u32 x, v9x_u32 y)
{
    return (x + V9X_RS690_SC_OFFSET) |
           ((y + V9X_RS690_SC_OFFSET) << V9X_RS690_SC_Y_SHIFT);
}

v9x_u32 v9x_rs690_float_integer(v9x_u32 value)
{
    v9x_u32 top = 0ul;

    if (value == 0ul || value >= V9X_RS690_FLOAT_INTEGER_LIMIT) {
        return 0ul;
    }
    while ((value >> (top + 1ul)) != 0ul) {
        ++top;
    }
    return ((V9X_RS690_FLOAT_EXPONENT_BIAS + top) <<
            V9X_RS690_FLOAT_MANTISSA_BITS) |
           ((value << (V9X_RS690_FLOAT_MANTISSA_BITS - top)) &
            V9X_RS690_FLOAT_MANTISSA_MASK);
}

/*
 * Long division, one mantissa bit at a time. The dividend is first scaled
 * by 2^shift into [denominator, 2 * denominator), so the quotient lies in
 * [1, 2) and its 24 bits come out with the leading one first; what is left
 * decides the rounding. Every intermediate stays below 2^26.
 */
v9x_u32 v9x_rs690_float_ratio(v9x_u32 numerator, v9x_u32 denominator)
{
    v9x_u32 remainder = numerator;
    v9x_u32 shift = 0ul;
    v9x_u32 mantissa = 0ul;
    v9x_u32 bit;

    if (numerator == 0ul || denominator == 0ul ||
        numerator > denominator ||
        denominator >= V9X_RS690_FLOAT_INTEGER_LIMIT) {
        return 0ul;
    }
    while (remainder < denominator) {
        remainder <<= 1;
        ++shift;
    }
    for (bit = 0ul; bit <= (v9x_u32)V9X_RS690_FLOAT_MANTISSA_BITS; ++bit) {
        mantissa <<= 1;
        if (remainder >= denominator) {
            mantissa |= 1ul;
            remainder -= denominator;
        }
        remainder <<= 1;
    }

    /* remainder is now twice what is left over: above the denominator is
     * past half a unit, equal is a tie, broken to even. */
    if (remainder > denominator ||
        (remainder == denominator && (mantissa & 1ul) != 0ul)) {
        ++mantissa;
        if (mantissa == V9X_RS690_FLOAT_INTEGER_LIMIT) {
            mantissa >>= 1;
            --shift;
        }
    }
    return ((V9X_RS690_FLOAT_EXPONENT_BIAS - shift) <<
            V9X_RS690_FLOAT_MANTISSA_BITS) |
           (mantissa & V9X_RS690_FLOAT_MANTISSA_MASK);
}

v9x_status v9x_rs690_build_3d_init(v9x_u32 pipe_select,
                                   struct v9x_rs690_3d_list *list)
{
    static const v9x_u32 pipe_fields[4] = {
        0x0ul,  /* R300_PIPE_COUNT_RV350, one pipe */
        0x6ul,  /* R300_PIPE_COUNT_R300, two */
        0xcul,  /* R300_PIPE_COUNT_R420_3P, three */
        0xeul   /* R300_PIPE_COUNT_R420, four */
    };
    v9x_u32 idle_clean = V9X_RS690_WAIT_2D_IDLECLEAN |
                         V9X_RS690_WAIT_3D_IDLECLEAN;
    v9x_u32 pipes;

    if (list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    pipes = (pipe_select >> V9X_RS690_PIPE_SELECT_SHIFT) &
            V9X_RS690_PIPE_SELECT_MASK;
    v9x_rs690_3d_begin(list);

    /* Caches flushed and the engines idle before the global setup. */
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_DSTCACHE_CTLSTAT,
                     V9X_RS690_RB3D_DC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_ZCACHE_CTLSTAT,
                     V9X_RS690_ZC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_WAIT_UNTIL, idle_clean);

    /* Tiling and pipes. DST_PIPE_CONFIG is written for every chip from
     * CHIP_FAMILY_R420 on, which takes in the RS690. */
    v9x_rs690_3d_add(list, V9X_RS690_GB_TILE_CONFIG,
                     V9X_RS690_GB_TILE_BASE | pipe_fields[pipes]);
    v9x_rs690_3d_add(list, V9X_RS690_WAIT_UNTIL, idle_clean);
    v9x_rs690_3d_add(list, V9X_RS690_DST_PIPE_CONFIG,
                     V9X_RS690_PIPE_AUTO_CONFIG);
    v9x_rs690_3d_add(list, V9X_RS690_GB_SELECT, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_GB_ENABLE, 0ul);

    v9x_rs690_3d_add(list, V9X_RS690_RB3D_DSTCACHE_CTLSTAT,
                     V9X_RS690_RB3D_DC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_ZCACHE_CTLSTAT,
                     V9X_RS690_ZC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_WAIT_UNTIL, idle_clean);

    v9x_rs690_3d_add(list, V9X_RS690_GB_AA_CONFIG, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_DSTCACHE_CTLSTAT,
                     V9X_RS690_RB3D_DC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_ZCACHE_CTLSTAT,
                     V9X_RS690_ZC_FLUSH_ALL);

    /* Multisample positions, geometry assembly, setup unit. */
    v9x_rs690_3d_add(list, V9X_RS690_GB_MSPOS0, V9X_RS690_MSPOS0);
    v9x_rs690_3d_add(list, V9X_RS690_GB_MSPOS1, V9X_RS690_MSPOS1);
    v9x_rs690_3d_add(list, V9X_RS690_GA_ENHANCE, V9X_RS690_GA_ENHANCE_VALUE);
    v9x_rs690_3d_add(list, V9X_RS690_GA_POLY_MODE,
                     V9X_RS690_GA_POLY_TRIANGLES);
    v9x_rs690_3d_add(list, V9X_RS690_GA_ROUND_MODE,
                     V9X_RS690_GA_ROUND_NEAREST);
    v9x_rs690_3d_add(list, V9X_RS690_GA_COLOR_CONTROL,
                     V9X_RS690_GA_ALL_GOURAUD);
    v9x_rs690_3d_add(list, V9X_RS690_GA_OFFSET, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_SU_TEX_WRAP, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_SU_POLY_OFFSET_ENABLE, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_SU_CULL_MODE, V9X_RS690_SU_FACE_NEG);
    v9x_rs690_3d_add(list, V9X_RS690_SU_DEPTH_SCALE,
                     V9X_RS690_SU_DEPTH_SCALE_VALUE);
    v9x_rs690_3d_add(list, V9X_RS690_SU_DEPTH_OFFSET, 0ul);

    /* The vertex assembler, bypassing the absent vertex shader. */
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTX_STATE_CNTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PVS_STATE_FLUSH_REG, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_CNTL_STATUS,
                     V9X_RS690_VAP_PVS_BYPASS);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_CNTL, V9X_RS690_VAP_CNTL_NO_TCL);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PVS_STATE_FLUSH_REG, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTE_CNTL,
                     V9X_RS690_VTE_WINDOW_COORDS);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PSC_SGN_NORM_CNTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_0,
                     V9X_RS690_STREAM_SWIZZLE_XYZW |
                     (V9X_RS690_STREAM_SWIZZLE_XYZW << 16));
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_1,
                     V9X_RS690_STREAM_SWIZZLE_XYZW);

    /* The rasterizer's two texture interpolators. */
    v9x_rs690_3d_add(list, V9X_RS690_RS_IP_0, V9X_RS690_RS_IP_TEX_ST01);
    v9x_rs690_3d_add(list, V9X_RS690_RS_IP_1,
                     V9X_RS690_RS_IP_TEX_ST01 | V9X_RS690_RS_IP_TEX_PTR_2);
    v9x_rs690_3d_add(list, V9X_RS690_RS_INST_0, V9X_RS690_RS_INST_TEX0);
    v9x_rs690_3d_add(list, V9X_RS690_RS_INST_1, V9X_RS690_RS_INST_TEX1);

    /* Shader output formats. */
    v9x_rs690_3d_add(list, V9X_RS690_US_W_FMT, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_OUT_FMT_1,
                     V9X_RS690_US_OUT_BGRA | V9X_RS690_US_OUT_UNUSED);
    v9x_rs690_3d_add(list, V9X_RS690_US_OUT_FMT_2,
                     V9X_RS690_US_OUT_BGRA | V9X_RS690_US_OUT_UNUSED);
    v9x_rs690_3d_add(list, V9X_RS690_US_OUT_FMT_3,
                     V9X_RS690_US_OUT_BGRA | V9X_RS690_US_OUT_UNUSED);

    /* Fog, depth source and alpha test off. */
    v9x_rs690_3d_add(list, V9X_RS690_FG_DEPTH_SRC, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_FG_FOG_BLEND, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_FG_ALPHA_FUNC, 0ul);

    /* Render backend: no blending, Z, ROP or dithering; all channels. */
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_ABLEND, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_ZSTENCILCNTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_ZCACHE_CTLSTAT,
                     V9X_RS690_ZC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_BW_CNTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_CNTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_ZTOP, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_ROPCNTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_AARESOLVE_CTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_COLOR_CHANNEL_MASK,
                     V9X_RS690_CHANNELS_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_DSTCACHE_CTLSTAT,
                     V9X_RS690_RB3D_DC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_CCTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_DITHER_CTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_DSTCACHE_CTLSTAT,
                     V9X_RS690_RB3D_DC_FLUSH_ALL);

    /* Scan converter: edge rule, the guard-offset clip rectangle, every
     * pixel of the screen door. */
    v9x_rs690_3d_add(list, V9X_RS690_SC_EDGERULE,
                     V9X_RS690_SC_EDGERULE_VALUE);
    v9x_rs690_3d_add(list, V9X_RS690_SC_CLIP_0_A, v9x_rs690_sc_point(0ul, 0ul));
    v9x_rs690_3d_add(list, V9X_RS690_SC_CLIP_0_B,
                     V9X_RS690_SC_CLIP_MAX |
                     (V9X_RS690_SC_CLIP_MAX << V9X_RS690_SC_Y_SHIFT));
    v9x_rs690_3d_add(list, V9X_RS690_SC_CLIP_RULE,
                     V9X_RS690_SC_CLIP_RULE_VALUE);
    v9x_rs690_3d_add(list, V9X_RS690_SC_SCREENDOOR,
                     V9X_RS690_SC_SCREENDOOR_ALL);
    return V9X_STATUS_OK;
}

/* The colour-only target state appended to the list, after its checks;
 * nothing is appended when they refuse. */
static v9x_status v9x_rs690_add_target(const struct v9x_rs690_3d_target *target,
                                       struct v9x_rs690_3d_list *list)
{
    v9x_u32 format;
    v9x_u32 pitch_pixels;
    v9x_u32 surface_bytes;

    if (target->bytes_per_pixel == 2ul) {
        format = V9X_RS690_COLOR_FORMAT_RGB565;
    } else if (target->bytes_per_pixel == 4ul) {
        format = V9X_RS690_COLOR_FORMAT_ARGB8888;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (target->pitch_bytes % target->bytes_per_pixel != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    pitch_pixels = target->pitch_bytes / target->bytes_per_pixel;
    if (pitch_pixels % V9X_RS690_COLOR_PITCH_ALIGN != 0ul ||
        (pitch_pixels & ~V9X_RS690_COLORPITCH_MASK) != 0ul ||
        target->offset % V9X_RS690_COLOR_OFFSET_ALIGN != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* The surface: non-empty, inside its pitch, inside what the scan
     * converter can address past its guard offset, and inside the frame
     * buffer without the GPU address wrapping. */
    if (target->width == 0ul || target->height == 0ul ||
        target->width > pitch_pixels ||
        target->width > V9X_RS690_SURFACE_MAX ||
        target->height > V9X_RS690_SURFACE_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    surface_bytes = target->pitch_bytes * target->height;
    if (target->offset >= target->fb_bytes ||
        surface_bytes > target->fb_bytes - target->offset ||
        target->fb_bytes - 1ul > 0xfffffffful - target->fb_gpu_base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Vertex stream and what the assembler passes on. */
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_0,
                     V9X_RS690_STREAMS_POS2_COLOR4);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTX_STATE_CNTL,
                     V9X_RS690_VTX_STATE_USER_COLOR);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VSM_VTX_ASSM,
                     V9X_RS690_VSM_POS_COLOR);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_OUTPUT_VTX_FMT_0,
                     V9X_RS690_OUT_VTX_POS_COLOR0);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_OUTPUT_VTX_FMT_1, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_TX_ENABLE, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_GB_ENABLE, 0ul);

    /* Rasterizer: colour 0 into temp 0, nothing else. */
    v9x_rs690_3d_add(list, V9X_RS690_RS_IP_0, V9X_RS690_RS_IP_COLOR0);
    v9x_rs690_3d_add(list, V9X_RS690_RS_COUNT,
                     V9X_RS690_RS_COUNT_ONE_COLOR);
    v9x_rs690_3d_add(list, V9X_RS690_RS_INST_COUNT, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RS_INST_0,
                     V9X_RS690_RS_INST_COLOR0_TEMP0);

    /* The shader. */
    v9x_rs690_3d_add(list, V9X_RS690_US_CONFIG, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_PIXSIZE, V9X_RS690_US_PIXSIZE_VALUE);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_OFFSET, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_0, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_1, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_2, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_3,
                     V9X_RS690_US_LAST_NODE_RGBA_OUT);
    v9x_rs690_3d_add(list, V9X_RS690_US_OUT_FMT_0, V9X_RS690_US_OUT_BGRA);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_RGB_ADDR_0,
                     V9X_RS690_ALU_RGB_ADDR_OUT);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_RGB_INST_0,
                     V9X_RS690_ALU_RGB_MOV);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_ADDR_0,
                     V9X_RS690_ALU_ALPHA_ADDR_OUT);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_INST_0,
                     V9X_RS690_ALU_ALPHA_MOV);

    /* Scissor to the surface, the colour buffer, blending off, and the
     * vertex size last as the X driver writes it. */
    v9x_rs690_3d_add(list, V9X_RS690_SC_SCISSOR0,
                     v9x_rs690_sc_point(0ul, 0ul));
    v9x_rs690_3d_add(list, V9X_RS690_SC_SCISSOR1,
                     v9x_rs690_sc_point(target->width - 1ul,
                                        target->height - 1ul));
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_COLOROFFSET0,
                     target->fb_gpu_base + target->offset);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_COLORPITCH0, pitch_pixels | format);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_CBLEND, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_ABLEND, 0ul);

    /* Z, the alpha test and culling off, as the init left them, so this
     * state means the same after a TL-vertex state that set them. */
    v9x_rs690_3d_add(list, V9X_RS690_ZB_CNTL, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_FG_ALPHA_FUNC, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_SU_CULL_MODE, V9X_RS690_SU_FACE_NEG);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTX_SIZE, V9X_RS690_VERTEX_DWORDS);
    return V9X_STATUS_OK;
}

v9x_status v9x_rs690_build_3d_target(const struct v9x_rs690_3d_target *target,
                                     struct v9x_rs690_3d_list *list)
{
    if (target == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_rs690_3d_begin(list);
    return v9x_rs690_add_target(target, list);
}

/* The checks both draw builders share: a known primitive and a whole
 * number of them. The per-primitive count, or 0 when refused with the
 * status in *status. */
static v9x_u32 v9x_rs690_check_primitive(v9x_u32 primitive, v9x_u32 count,
                                         v9x_status *status)
{
    v9x_u32 per_primitive;

    if (primitive == V9X_RS690_PRIM_TRIANGLES) {
        per_primitive = 3ul;
    } else if (primitive == V9X_RS690_PRIM_QUADS) {
        per_primitive = 4ul;
    } else {
        *status = V9X_STATUS_UNSUPPORTED;
        return 0ul;
    }
    if (count == 0ul || count > V9X_RS690_3D_VERTICES_MAX ||
        count % per_primitive != 0ul) {
        *status = V9X_STATUS_INVALID_ARGUMENT;
        return 0ul;
    }
    return per_primitive;
}

v9x_status v9x_rs690_build_3d_draw(v9x_u32 primitive,
                                   const struct v9x_rs690_3d_vertex *vertices,
                                   v9x_u32 count, v9x_u32 width,
                                   v9x_u32 height,
                                   struct v9x_rs690_3d_list *list)
{
    v9x_status status = V9X_STATUS_OK;
    v9x_u32 index;

    if (vertices == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_rs690_check_primitive(primitive, count, &status) == 0ul) {
        return status;
    }
    for (index = 0ul; index < count; ++index) {
        const struct v9x_rs690_3d_vertex *vertex = &vertices[index];

        if (vertex->x > width || vertex->y > height ||
            vertex->red > V9X_RS690_CHANNEL_MAX ||
            vertex->green > V9X_RS690_CHANNEL_MAX ||
            vertex->blue > V9X_RS690_CHANNEL_MAX ||
            vertex->alpha > V9X_RS690_CHANNEL_MAX) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    v9x_rs690_3d_begin(list);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VF_CNTL,
                     primitive | V9X_RS690_VF_PRIM_WALK_DATA |
                     (count << V9X_RS690_VF_NUM_VERTICES_SHIFT));
    for (index = 0ul; index < count; ++index) {
        const struct v9x_rs690_3d_vertex *vertex = &vertices[index];

        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_integer(vertex->x));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_integer(vertex->y));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->red,
                                               V9X_RS690_CHANNEL_MAX));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->green,
                                               V9X_RS690_CHANNEL_MAX));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->blue,
                                               V9X_RS690_CHANNEL_MAX));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->alpha,
                                               V9X_RS690_CHANNEL_MAX));
    }
    return V9X_STATUS_OK;
}

/* The texture state appended to the list, after its checks; nothing is
 * appended when they refuse. */
static v9x_status v9x_rs690_add_texture(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                        const struct v9x_rs690_3d_texture *texture,
                                        struct v9x_rs690_3d_list *list)
{
    v9x_u32 bytes_per_texel;
    v9x_u32 card_format;
    v9x_u32 filter;
    v9x_u32 rgb_addr;
    v9x_u32 rgb_inst;
    v9x_u32 alpha_addr;
    v9x_u32 alpha_inst;
    v9x_u32 texture_bytes;

    if (texture->format == V9X_RS690_TEXFMT_RGB565) {
        bytes_per_texel = 2ul;
        card_format = V9X_RS690_TXFMT1_RGB565;
    } else if (texture->format == V9X_RS690_TEXFMT_ARGB8888) {
        bytes_per_texel = 4ul;
        card_format = V9X_RS690_TXFMT1_ARGB8888;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (texture->filter == V9X_RS690_TEXFILTER_NEAREST) {
        filter = V9X_RS690_TX_FILTER_NEAREST;
    } else if (texture->filter == V9X_RS690_TEXFILTER_LINEAR) {
        filter = V9X_RS690_TX_FILTER_LINEAR;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (texture->address == V9X_RS690_TEXADDR_CLAMP) {
        filter |= V9X_RS690_TX_CLAMP_ST_EDGE;
    } else if (texture->address != V9X_RS690_TEXADDR_WRAP) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (texture->combine == V9X_RS690_TEXCOMBINE_REPLACE) {
        rgb_addr = V9X_RS690_ALU_RGB_ADDR_OUT;
        rgb_inst = V9X_RS690_ALU_RGB_MOV;
        alpha_addr = V9X_RS690_ALU_ALPHA_ADDR_OUT;
        alpha_inst = V9X_RS690_ALU_ALPHA_MOV;
    } else if (texture->combine == V9X_RS690_TEXCOMBINE_MODULATE) {
        rgb_addr = V9X_RS690_ALU_RGB_ADDR_OUT | V9X_RS690_ALU_ADDR1_TEMP1;
        rgb_inst = V9X_RS690_ALU_RGB_MUL;
        alpha_addr = V9X_RS690_ALU_ALPHA_ADDR_OUT | V9X_RS690_ALU_ADDR1_TEMP1;
        alpha_inst = V9X_RS690_ALU_ALPHA_MUL;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (texture->offset % V9X_RS690_TEX_ALIGN != 0ul ||
        texture->pitch_bytes % V9X_RS690_TEX_ALIGN != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }

    /* Size, a pitch that holds a row, and every row inside the frame
     * buffer without the GPU address wrapping. The pitch field is 11 bits
     * like the size fields, so the largest row bounds it too. */
    if (texture->width == 0ul || texture->height == 0ul ||
        texture->width > V9X_RS690_TEX_SIZE_MAX ||
        texture->height > V9X_RS690_TEX_SIZE_MAX ||
        texture->pitch_bytes < texture->width * bytes_per_texel ||
        texture->pitch_bytes / bytes_per_texel > V9X_RS690_TEX_SIZE_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    texture_bytes = texture->pitch_bytes * texture->height;
    if (texture->offset >= fb_bytes ||
        texture_bytes > fb_bytes - texture->offset ||
        fb_bytes - 1ul > 0xfffffffful - fb_gpu_base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* The vertex gains texture 0 after the colour; the colour stream is no
     * longer the last. */
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_0,
                     V9X_RS690_STREAMS_POS2_COLOR4_MORE);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_1,
                     V9X_RS690_STREAM_TEX0_FLOAT2_LAST);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VSM_VTX_ASSM,
                     V9X_RS690_VSM_POS_COLOR | V9X_RS690_VSM_TC0);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_OUTPUT_VTX_FMT_0,
                     V9X_RS690_OUT_VTX_POS_COLOR0);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_OUTPUT_VTX_FMT_1,
                     V9X_RS690_OUT_VTX_TEX0_ST);

    /* Rasterizer: s, t into temp 0 and colour 0 into temp 1, one
     * interpolator. */
    v9x_rs690_3d_add(list, V9X_RS690_RS_IP_0, V9X_RS690_RS_IP_TEX_ST01);
    v9x_rs690_3d_add(list, V9X_RS690_RS_COUNT,
                     V9X_RS690_RS_COUNT_ST_ONE_COLOR);
    v9x_rs690_3d_add(list, V9X_RS690_RS_INST_COUNT, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_RS_INST_0,
                     V9X_RS690_RS_INST_TEX0 | V9X_RS690_RS_INST_COLOR0_TEMP1);

    /* Unit 0, in R300TextureSetup's order, then the cache invalidate and
     * the enable as R300PrepareComposite writes them. No mipmaps, no
     * tiling, border colour 0. */
    v9x_rs690_3d_add(list, V9X_RS690_TX_FILTER0_0, filter);
    v9x_rs690_3d_add(list, V9X_RS690_TX_FILTER1_0, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_TX_FORMAT0_0,
                     (texture->width - 1ul) |
                     ((texture->height - 1ul) << V9X_RS690_TXHEIGHT_SHIFT) |
                     V9X_RS690_TXPITCH_EN);
    v9x_rs690_3d_add(list, V9X_RS690_TX_FORMAT1_0, card_format);
    v9x_rs690_3d_add(list, V9X_RS690_TX_FORMAT2_0,
                     texture->pitch_bytes / bytes_per_texel - 1ul);
    v9x_rs690_3d_add(list, V9X_RS690_TX_OFFSET_0,
                     fb_gpu_base + texture->offset);
    v9x_rs690_3d_add(list, V9X_RS690_TX_BORDER_COLOR_0, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_TX_INVALTAGS, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_TX_ENABLE, V9X_RS690_TEX_0_ENABLE);

    /* The shader: a first node with one texture load and one ALU
     * instruction; both size fields 0 mean one. */
    v9x_rs690_3d_add(list, V9X_RS690_US_CONFIG, V9X_RS690_US_FIRST_TEX);
    v9x_rs690_3d_add(list, V9X_RS690_US_PIXSIZE, V9X_RS690_US_PIXSIZE_VALUE);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_OFFSET, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_0, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_1, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_2, 0ul);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_3,
                     V9X_RS690_US_LAST_NODE_RGBA_OUT);
    v9x_rs690_3d_add(list, V9X_RS690_US_TEX_INST_0, V9X_RS690_TEX_LD_0_FROM_0);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_RGB_ADDR_0, rgb_addr);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_RGB_INST_0, rgb_inst);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_ADDR_0, alpha_addr);
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_INST_0, alpha_inst);

    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTX_SIZE,
                     V9X_RS690_TEX_VERTEX_DWORDS);
    return V9X_STATUS_OK;
}

v9x_status v9x_rs690_build_3d_texture(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                      const struct v9x_rs690_3d_texture *texture,
                                      struct v9x_rs690_3d_list *list)
{
    if (texture == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_rs690_3d_begin(list);
    return v9x_rs690_add_texture(fb_gpu_base, fb_bytes, texture, list);
}

v9x_status v9x_rs690_build_3d_draw_textured(
    v9x_u32 primitive, const struct v9x_rs690_3d_tex_vertex *vertices,
    v9x_u32 count, v9x_u32 width, v9x_u32 height,
    struct v9x_rs690_3d_list *list)
{
    v9x_status status = V9X_STATUS_OK;
    v9x_u32 index;

    if (vertices == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_rs690_check_primitive(primitive, count, &status) == 0ul) {
        return status;
    }
    for (index = 0ul; index < count; ++index) {
        const struct v9x_rs690_3d_tex_vertex *vertex = &vertices[index];

        if (vertex->x > width || vertex->y > height ||
            vertex->red > V9X_RS690_CHANNEL_MAX ||
            vertex->green > V9X_RS690_CHANNEL_MAX ||
            vertex->blue > V9X_RS690_CHANNEL_MAX ||
            vertex->alpha > V9X_RS690_CHANNEL_MAX) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    v9x_rs690_3d_begin(list);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VF_CNTL,
                     primitive | V9X_RS690_VF_PRIM_WALK_DATA |
                     (count << V9X_RS690_VF_NUM_VERTICES_SHIFT));
    for (index = 0ul; index < count; ++index) {
        const struct v9x_rs690_3d_tex_vertex *vertex = &vertices[index];

        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_integer(vertex->x));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_integer(vertex->y));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->red,
                                               V9X_RS690_CHANNEL_MAX));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->green,
                                               V9X_RS690_CHANNEL_MAX));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->blue,
                                               V9X_RS690_CHANNEL_MAX));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0,
                         v9x_rs690_float_ratio(vertex->alpha,
                                               V9X_RS690_CHANNEL_MAX));
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0, vertex->s);
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0, vertex->t);
    }
    return V9X_STATUS_OK;
}

/* The checks of the state's own fields - Z, blend, alpha test, cull and
 * texture alpha - before anything is built. */
static v9x_status v9x_rs690_check_state(const struct v9x_rs690_3d_state *state)
{
    const struct v9x_rs690_3d_target *target = &state->target;
    v9x_u32 depth_pixels;
    v9x_u32 depth_bytes;

    if (state->textured != 0ul &&
        state->texture_alpha > V9X_RS690_TEXALPHA_MODULATE) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->cull > V9X_RS690_CULL_BACK) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->blend_enable != 0ul &&
        (state->blend_src < V9X_RS690_BLEND_ZERO ||
         state->blend_src > V9X_RS690_BLEND_SRC_ALPHA_SAT ||
         state->blend_dst < V9X_RS690_BLEND_ZERO ||
         state->blend_dst > V9X_RS690_BLEND_SRC_ALPHA_SAT)) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (state->alpha_test_enable != 0ul) {
        if (state->alpha_func > V9X_RS690_AFUNC_ALWAYS) {
            return V9X_STATUS_UNSUPPORTED;
        }
        if (state->alpha_ref > V9X_RS690_CHANNEL_MAX) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    if (state->depth_enable == 0ul) {
        return V9X_STATUS_OK;
    }

    /* Z16 under the target: aligned, a row at least as wide, every row in
     * the frame buffer. */
    if (state->depth_func > V9X_RS690_ZFUNC_ALWAYS ||
        state->depth_offset % V9X_RS690_DEPTH_OFFSET_ALIGN != 0ul ||
        state->depth_pitch_bytes % 2ul != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    depth_pixels = state->depth_pitch_bytes / 2ul;
    if (depth_pixels % V9X_RS690_DEPTH_PITCH_ALIGN != 0ul ||
        (depth_pixels & ~V9X_RS690_DEPTHPITCH_MASK) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (depth_pixels < target->width) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    depth_bytes = state->depth_pitch_bytes * target->height;
    if (state->depth_offset >= target->fb_bytes ||
        depth_bytes > target->fb_bytes - state->depth_offset) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_rs690_build_3d_state(const struct v9x_rs690_3d_state *state,
                                    struct v9x_rs690_3d_list *list)
{
    v9x_status status;
    v9x_u32 blend = 0ul;
    v9x_u32 alpha_test = 0ul;

    if (state == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_rs690_check_state(state);
    if (status != V9X_STATUS_OK) {
        return status;
    }

    /* The proven sequences first - the colour-buffer state, then the
     * texture's - and then the writes that differ from them: a register
     * written twice takes the later value, and the FIFO cost of the
     * repeats is a few dozen writes per state change. */
    v9x_rs690_3d_begin(list);
    status = v9x_rs690_add_target(&state->target, list);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    if (state->textured != 0ul) {
        status = v9x_rs690_add_texture(state->target.fb_gpu_base,
                                       state->target.fb_bytes,
                                       &state->texture, list);
        if (status != V9X_STATUS_OK) {
            return status;
        }
        if (state->texture_alpha == V9X_RS690_TEXALPHA_TEXEL) {
            v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_ADDR_0,
                             V9X_RS690_ALU_ALPHA_ADDR_OUT);
            v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_INST_0,
                             V9X_RS690_ALU_ALPHA_MOV);
        } else if (state->texture_alpha == V9X_RS690_TEXALPHA_DIFFUSE) {
            v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_ADDR_0,
                             V9X_RS690_ALU_ALPHA_ADDR_OUT |
                             V9X_RS690_ALU_ADDR1_TEMP1);
            v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_INST_0,
                             V9X_RS690_ALU_ALPHA_DIFFUSE);
        } else if (state->texture_alpha == V9X_RS690_TEXALPHA_MODULATE) {
            v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_ADDR_0,
                             V9X_RS690_ALU_ALPHA_ADDR_OUT |
                             V9X_RS690_ALU_ADDR1_TEMP1);
            v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_INST_0,
                             V9X_RS690_ALU_ALPHA_MUL);
        }
    }

    /* The TL vertex, whichever routing the rasterizer has. */
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_0,
                     V9X_RS690_STREAMS_TL_POS4_DIFFUSE);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_1,
                     V9X_RS690_STREAMS_TL_SPECULAR_TEX0);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_0,
                     V9X_RS690_STREAM_SWIZZLE_XYZW |
                     (V9X_RS690_STREAM_SWIZZLE_BGRA << 16));
    v9x_rs690_3d_add(list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_1,
                     V9X_RS690_STREAM_SWIZZLE_BGRA |
                     (V9X_RS690_STREAM_SWIZZLE_XYZW << 16));
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTX_STATE_CNTL,
                     V9X_RS690_VTX_STATE_USER_COLOR);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VSM_VTX_ASSM,
                     V9X_RS690_VSM_POS_COLOR | V9X_RS690_VSM_TC0);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_OUTPUT_VTX_FMT_0,
                     V9X_RS690_OUT_VTX_POS_COLOR01);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_OUTPUT_VTX_FMT_1,
                     V9X_RS690_OUT_VTX_TEX0_ST);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTE_CNTL,
                     V9X_RS690_VTE_WINDOW_COORDS);

    /* Z16, linear, at the GPU address; the compare, and writes or not. */
    if (state->depth_enable != 0ul) {
        v9x_rs690_3d_add(list, V9X_RS690_ZB_FORMAT,
                         V9X_RS690_DEPTHFORMAT_16BIT);
        v9x_rs690_3d_add(list, V9X_RS690_ZB_DEPTHOFFSET,
                         state->target.fb_gpu_base + state->depth_offset);
        v9x_rs690_3d_add(list, V9X_RS690_ZB_DEPTHPITCH,
                         state->depth_pitch_bytes / 2ul);
        v9x_rs690_3d_add(list, V9X_RS690_ZB_ZSTENCILCNTL, state->depth_func);
        v9x_rs690_3d_add(list, V9X_RS690_ZB_CNTL,
                         V9X_RS690_Z_ENABLE |
                         (state->depth_write != 0ul ?
                          V9X_RS690_Z_WRITE_ENABLE : 0ul));
    } else {
        v9x_rs690_3d_add(list, V9X_RS690_ZB_ZSTENCILCNTL, 0ul);
        v9x_rs690_3d_add(list, V9X_RS690_ZB_CNTL, 0ul);
    }

    /* Blend: on, reading the destination, ADD_CLAMP; alpha the same pair
     * (SEPARATE_ALPHA off). */
    if (state->blend_enable != 0ul) {
        blend = V9X_RS690_BLEND_ON_READ |
                (state->blend_src << V9X_RS690_SRC_BLEND_SHIFT) |
                (state->blend_dst << V9X_RS690_DST_BLEND_SHIFT);
    }
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_CBLEND, blend);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_ABLEND, blend);

    if (state->alpha_test_enable != 0ul) {
        alpha_test = state->alpha_ref |
                     (state->alpha_func << V9X_RS690_AFUNC_SHIFT) |
                     V9X_RS690_ALPHA_TEST_ENABLE;
    }
    v9x_rs690_3d_add(list, V9X_RS690_FG_ALPHA_FUNC, alpha_test);
    v9x_rs690_3d_add(list, V9X_RS690_SU_CULL_MODE,
                     V9X_RS690_SU_FACE_NEG | state->cull);

    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTX_SIZE,
                     V9X_RS690_TL_VERTEX_DWORDS);
    return V9X_STATUS_OK;
}

v9x_u32 v9x_rs690_3d_vf_cntl(v9x_u32 primitive, v9x_u32 count)
{
    v9x_u32 per_primitive;

    if (primitive == V9X_RS690_PRIM_TRIANGLES) {
        per_primitive = 3ul;
    } else if (primitive == V9X_RS690_PRIM_QUADS) {
        per_primitive = 4ul;
    } else {
        return 0ul;
    }
    if (count == 0ul || count > V9X_RS690_VF_NUM_VERTICES_MAX ||
        count % per_primitive != 0ul) {
        return 0ul;
    }
    return primitive | V9X_RS690_VF_PRIM_WALK_DATA |
           (count << V9X_RS690_VF_NUM_VERTICES_SHIFT);
}

v9x_status v9x_rs690_build_3d_draw_tl(v9x_u32 primitive,
                                      const v9x_u32 *vertices,
                                      v9x_u32 count,
                                      struct v9x_rs690_3d_list *list)
{
    v9x_status status = V9X_STATUS_OK;
    v9x_u32 index;

    if (vertices == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_rs690_check_primitive(primitive, count, &status) == 0ul) {
        return status;
    }
    v9x_rs690_3d_begin(list);
    v9x_rs690_3d_add(list, V9X_RS690_VAP_VF_CNTL,
                     v9x_rs690_3d_vf_cntl(primitive, count));
    for (index = 0ul; index < count * V9X_RS690_TL_VERTEX_DWORDS; ++index) {
        v9x_rs690_3d_add(list, V9X_RS690_SE_PORT_DATA0, vertices[index]);
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_rs690_build_3d_finish(struct v9x_rs690_3d_list *list)
{
    if (list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_rs690_3d_begin(list);
    v9x_rs690_3d_add(list, V9X_RS690_SC_CLIP_RULE,
                     V9X_RS690_SC_CLIP_RULE_VALUE);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_DSTCACHE_CTLSTAT,
                     V9X_RS690_RB3D_DC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_ZB_ZCACHE_CTLSTAT,
                     V9X_RS690_ZC_FLUSH_ALL);
    v9x_rs690_3d_add(list, V9X_RS690_WAIT_UNTIL, V9X_RS690_WAIT_3D_IDLECLEAN);
    return V9X_STATUS_OK;
}
