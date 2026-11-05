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

/* GA_ENHANCE: deadlock and fast-sync control on, and bits 2 and 3 as Linux
 * r420_pipes_init sets them ("workaround TCL deadlock issue"); the X driver
 * writes 3. GA_POLY_MODE: front and back faces drawn as triangles.
 * GA_ROUND_MODE: geometry and colour round to nearest. GA_COLOR_CONTROL:
 * all four colours Gouraud-shaded. */
#define V9X_RS690_GA_ENHANCE_VALUE      0x0000000ful

/* RB2D_DSTCACHE_MODE bits (Linux r500_reg.h:89-91). */
#define V9X_RS690_DC_AUTOFLUSH_ENABLE   0x00000100ul
#define V9X_RS690_DC_DISABLE_IGNORE_PE  0x00020000ul
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

/* RB3D_COLOR_CHANNEL_MASK: blue, green, red and alpha writable (bits 0-3,
 * Mesa r300_reg.h:2356-2359); without alpha, for an XRGB target. */
#define V9X_RS690_CHANNELS_ALL          0x0000000ful
#define V9X_RS690_CHANNELS_RGB          0x00000007ul

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
/* a1r5g5b5 (X, Y, Z, W, W1Z5Y5X5 0Bh), R300TexFormats; B4G4R4A4 (X, Y, Z,
 * W, W4Z4Y4X4 0Ah), Mesa r300_translate_texformat. */
#define V9X_RS690_TXFMT1_ARGB1555       0x0000a60bul
#define V9X_RS690_TXFMT1_ARGB4444       0x0000a60aul
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

/* Flat shading: every colour FLAT (1 in each 2-bit field), provoking
 * vertex FIRST (0 in 17:16), Mesa r300_reg.h:1073-1110. */
#define V9X_RS690_GA_ALL_FLAT_FIRST     0x00005555ul

/* Specular. Interpolator 1 carries colour 1 (COL_PTR 1 at 8:6, RGBA);
 * instruction 1 writes it (COL_ID 1 at 13:11, COL_CN_WRITE bit 14) to the
 * temp in COL_ADDR (bit 17 up). RS_COUNT: two colours (IC_COUNT 2 << 7),
 * HIRES_EN, and two texture components when textured. The RGB MAD takes
 * source 2 (ADDR2, 17:12) as C, SRC2_RGB (8 << 14): out = temp0 * 1 +
 * src2, or temp0 * temp1 + src2 when modulated. */
#define V9X_RS690_RS_IP_COLOR1          0x00000040ul
#define V9X_RS690_RS_INST_COLOR1        0x00004800ul
#define V9X_RS690_RS_INST_COL_ADDR_SHIFT 17
#define V9X_RS690_RS_COUNT_TWO_COLORS   0x00040100ul
#define V9X_RS690_RS_COUNT_ST           0x00000002ul
#define V9X_RS690_ALU_ADDR2_SHIFT       12
#define V9X_RS690_ALU_RGB_MOV_PLUS_SRC2 0x40020a80ul
#define V9X_RS690_ALU_RGB_MUL_PLUS_SRC2 0x40020200ul

/*
 * Fog, two more ALU instructions (xf86-video-ati 6.14.6 radeon_reg.h:4875-
 * 5030 for the fields, radeon_textured_videofuncs.c's YUV program for
 * their use). Instruction 0 writes the result temp - the one after the
 * specular temp - instead of the output: its output masks (RGB OMASK
 * 28:26, alpha OMASK bit 24) become write masks (RGB WMASK 25:23, alpha
 * WMASK bit 23) with ADDRD 22:18. Instruction 1: result.rgb = SRC1_RGB
 * (4) * ONE (21 << 7) + SRC0_RGB (0 << 14) negated (MOD_C NEG, 1 << 19),
 * not clamped - result - fog, src0 the constant (CONST bit 5), src1 the
 * result; its alpha a MAD of zeros writing nothing. Instruction 2: out.rgb
 * = SRC1_RGB (4) * SRC2_AAA (14 << 7) + SRC0_RGB (0 << 14), clamped -
 * (colour - fog) * f + fog; out.a = result.a. SRC2_AAA reads the alpha
 * unit's source 2, so the alpha address word names the specular temp as
 * ADDR2 too; with it 0, f read as 1 (V9XRS3D FogInstFactor, M-GB8KRE).
 *
 * A two-instruction form with the presubtract (SRCP = src1 - src0) read
 * the colour wrongly on the RS690: in place in temp 0 the specular add was
 * lost, and from a fresh temp red read 0, while a plain move from the
 * same temp was right (TlFog/TlFogSpecular, 2026-11-05).
 *
 * US_CODE_OFFSET counts instructions (ALU_CODE_SIZE 3, TEX_CODE_SIZE 1
 * when textured), US_CODE_ADDR_3 counts less one (ALU_SIZE 2), as
 * radeon_textured_videofuncs.c writes them.
 */
#define V9X_RS690_ALU_RGB_OMASK_XYZ     0x1c000000ul
#define V9X_RS690_ALU_RGB_WMASK_XYZ     0x03800000ul
#define V9X_RS690_ALU_ALPHA_OMASK_A     0x01000000ul
#define V9X_RS690_ALU_ALPHA_WMASK_A     0x00800000ul
#define V9X_RS690_ALU_ADDR0_CONST0      0x00000020ul
#define V9X_RS690_ALU_ADDR1_SHIFT       6
#define V9X_RS690_ALU_ADDRD_SHIFT       18
#define V9X_RS690_ALU_ADDR_FIELD        0x3ful
#define V9X_RS690_ALU_ADDRD_FIELD       0x1ful
/* The alpha unit idle: a MAD of zeros, no mask (the X driver's "alpha
 * nop"). */
#define V9X_RS690_ALU_ALPHA_NOP         0x00040810ul
#define V9X_RS690_US_ALU_CODE_SIZE_SHIFT 6
#define V9X_RS690_US_TEX_CODE_SIZE_ONE  0x00040000ul
#define V9X_RS690_US_ALU_SIZE_SHIFT     6

/* RGB MAD fields: SEL_A 4:0, SEL_B 11:7, SEL_C 18:14, MOD_C NEG bit 19,
 * CLAMP bit 30 (radeon_reg.h:4903-4960), and the selects used. */
#define V9X_RS690_ALU_MAX               5u
#define V9X_RS690_ALU_SEL_B_SHIFT       7
#define V9X_RS690_ALU_SEL_C_SHIFT       14
#define V9X_RS690_ALU_NEG_C             0x00080000ul
#define V9X_RS690_ALU_CLAMP             0x40000000ul
#define V9X_RS690_SEL_SRC0_RGB          0ul
#define V9X_RS690_SEL_SRC1_RGB          4ul
#define V9X_RS690_SEL_SRC2_RGB          8ul
#define V9X_RS690_SEL_SRC1_AAA          13ul
#define V9X_RS690_SEL_SRC2_AAA          14ul
#define V9X_RS690_SEL_ZERO              20ul
#define V9X_RS690_SEL_ONE               21ul

/* float24: the IEEE exponent re-biased from 127 to 63, the mantissa's top
 * 16 bits. */
#define V9X_RS690_FLOAT24_SIGN          0x00800000ul
#define V9X_RS690_FLOAT24_REBIAS        64ul
#define V9X_RS690_FLOAT24_EXP_SHIFT     16
#define V9X_RS690_FLOAT24_DROP_BITS     7

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
                                   v9x_u32 rb2d_dstcache_mode,
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
    /* Linux r420_pipes_init: the setup unit takes register writes for the
     * active pipes (bit per pipe). */
    v9x_rs690_3d_add(list, V9X_RS690_SU_REG_DEST,
                     (1ul << (pipes + 1ul)) - 1ul);
    v9x_rs690_3d_add(list, V9X_RS690_WAIT_UNTIL, idle_clean);
    v9x_rs690_3d_add(list, V9X_RS690_DST_PIPE_CONFIG,
                     V9X_RS690_PIPE_AUTO_CONFIG);
    /* Linux r420_pipes_init again: the 2D destination cache flushes
     * itself (DC_AUTOFLUSH_ENABLE) and DC_DC_DISABLE_IGNORE_PE, ORed into
     * the mode as read. The X driver writes neither. */
    v9x_rs690_3d_add(list, V9X_RS690_RB2D_DSTCACHE_MODE,
                     rb2d_dstcache_mode | V9X_RS690_DC_AUTOFLUSH_ENABLE |
                     V9X_RS690_DC_DISABLE_IGNORE_PE);
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
/* with_program: the TL state builder follows with its own shader, so the
 * combines only it can build (ADD, DECALALPHA) are taken; their words here
 * are MODULATE's and are overwritten. */
static v9x_status v9x_rs690_add_texture(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                        const struct v9x_rs690_3d_texture *texture,
                                        int with_program,
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
    } else if (texture->format == V9X_RS690_TEXFMT_ARGB1555) {
        bytes_per_texel = 2ul;
        card_format = V9X_RS690_TXFMT1_ARGB1555;
    } else if (texture->format == V9X_RS690_TEXFMT_ARGB4444) {
        bytes_per_texel = 2ul;
        card_format = V9X_RS690_TXFMT1_ARGB4444;
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
    } else if (texture->combine == V9X_RS690_TEXCOMBINE_MODULATE ||
               (with_program &&
                (texture->combine == V9X_RS690_TEXCOMBINE_ADD ||
                 texture->combine == V9X_RS690_TEXCOMBINE_DECALALPHA))) {
        rgb_addr = V9X_RS690_ALU_RGB_ADDR_OUT | V9X_RS690_ALU_ADDR1_TEMP1;
        rgb_inst = V9X_RS690_ALU_RGB_MUL;
        alpha_addr = V9X_RS690_ALU_ALPHA_ADDR_OUT | V9X_RS690_ALU_ADDR1_TEMP1;
        alpha_inst = V9X_RS690_ALU_ALPHA_MUL;
    } else {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (texture->offset % V9X_RS690_TEX_ALIGN != 0ul ||
        texture->pitch_bytes % V9X_RS690_TEX_PITCH_ALIGN != 0ul) {
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
    return v9x_rs690_add_texture(fb_gpu_base, fb_bytes, texture, 0, list);
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

static void v9x_rs690_add_shader(const struct v9x_rs690_3d_state *state,
                                 struct v9x_rs690_3d_list *list);

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
                                       &state->texture, 1, list);
        if (status != V9X_STATUS_OK) {
            return status;
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
    v9x_rs690_3d_add(list, V9X_RS690_GA_COLOR_CONTROL,
                     state->flat != 0ul ? V9X_RS690_GA_ALL_FLAT_FIRST
                                        : V9X_RS690_GA_ALL_GOURAUD);
    v9x_rs690_3d_add(list, V9X_RS690_RB3D_COLOR_CHANNEL_MASK,
                     state->x_channel != 0ul ? V9X_RS690_CHANNELS_RGB
                                             : V9X_RS690_CHANNELS_ALL);

    /* Specular: colour 1 through interpolator 1 into the temp after the
     * colour's, the ALU's third operand, added to RGB only. Fog needs
     * colour 1 too, for its alpha, whether or not it is added. */
    if (state->specular != 0ul || state->fog != 0ul) {
        v9x_u32 temp = state->textured != 0ul ? 2ul : 1ul;

        v9x_rs690_3d_add(list, V9X_RS690_RS_IP_1, V9X_RS690_RS_IP_COLOR1);
        v9x_rs690_3d_add(list, V9X_RS690_RS_INST_1,
                         V9X_RS690_RS_INST_COLOR1 |
                         (temp << V9X_RS690_RS_INST_COL_ADDR_SHIFT));
        v9x_rs690_3d_add(list, V9X_RS690_RS_COUNT,
                         V9X_RS690_RS_COUNT_TWO_COLORS |
                         (state->textured != 0ul ?
                          V9X_RS690_RS_COUNT_ST : 0ul));
        v9x_rs690_3d_add(list, V9X_RS690_RS_INST_COUNT, 1ul);
    }
    v9x_rs690_add_shader(state, list);

    v9x_rs690_3d_add(list, V9X_RS690_VAP_VTX_SIZE,
                     V9X_RS690_TL_VERTEX_DWORDS);
    return V9X_STATUS_OK;
}

v9x_u32 v9x_rs690_float24(v9x_u32 ieee)
{
    v9x_u32 exponent = (ieee >> V9X_RS690_FLOAT_MANTISSA_BITS) & 0xfful;
    v9x_u32 sign = (ieee & 0x80000000ul) != 0ul ? V9X_RS690_FLOAT24_SIGN
                                                 : 0ul;

    if (exponent == 0ul) {
        return 0ul;
    }
    return sign |
           ((exponent - V9X_RS690_FLOAT24_REBIAS)
            << V9X_RS690_FLOAT24_EXP_SHIFT) |
           ((ieee & V9X_RS690_FLOAT_MANTISSA_MASK) >>
            V9X_RS690_FLOAT24_DROP_BITS);
}

/* One ALU instruction's four words, and the assembler's working state. */
struct v9x_rs690_alu {
    v9x_u32 rgb_addr;
    v9x_u32 rgb_inst;
    v9x_u32 alpha_addr;
    v9x_u32 alpha_inst;
};

struct v9x_rs690_program {
    struct v9x_rs690_alu alu[V9X_RS690_ALU_MAX];
    v9x_u32 count;
    v9x_u32 result;
};

static v9x_u32 v9x_rs690_sources(v9x_u32 src0, v9x_u32 src1, v9x_u32 src2)
{
    return src0 | (src1 << V9X_RS690_ALU_ADDR1_SHIFT) |
           (src2 << V9X_RS690_ALU_ADDR2_SHIFT);
}

static v9x_u32 v9x_rs690_mad(v9x_u32 a, v9x_u32 b, v9x_u32 c, v9x_u32 flags)
{
    return a | (b << V9X_RS690_ALU_SEL_B_SHIFT) |
           (c << V9X_RS690_ALU_SEL_C_SHIFT) | flags;
}

/*
 * Append an RGB step. Every step but the last writes the result temp's
 * RGB; the alpha unit idles (no mask) with the sources the RGB step's AAA
 * selects need. The caller's alpha sources ride in alpha_sources.
 */
static void v9x_rs690_rgb_step(struct v9x_rs690_program *program,
                               v9x_u32 sources, v9x_u32 inst,
                               v9x_u32 alpha_sources)
{
    struct v9x_rs690_alu *alu;

    if (program->count >= V9X_RS690_ALU_MAX) {
        return;
    }
    alu = &program->alu[program->count++];
    alu->rgb_addr = sources | V9X_RS690_ALU_RGB_WMASK_XYZ |
                    (program->result << V9X_RS690_ALU_ADDRD_SHIFT);
    alu->rgb_inst = inst;
    alu->alpha_addr = alpha_sources |
                      (program->result << V9X_RS690_ALU_ADDRD_SHIFT);
    alu->alpha_inst = V9X_RS690_ALU_ALPHA_NOP;
}

/*
 * The fragment shader for the TL state, in up to five MAD instructions.
 * Temps: the texel in 0 and the diffuse in 1 when textured, the diffuse
 * in 0 otherwise; the specular colour in the next (when specular or fog
 * needs it); the result temp after that, when there is more than one
 * instruction. Instruction 0 also makes the fragment's alpha (into the
 * result temp's W, or the output when it is the only one); a later last
 * instruction moves it out. Steps:
 *
 *   combine   REPLACE texel; MODULATE texel * diffuse; ADD texel +
 *             diffuse; DECALALPHA (texel - diffuse) unclamped, then
 *             * texel.a + diffuse; untextured the diffuse. Specular folds
 *             into the first instruction as its C where C is free.
 *   specular  + specular.rgb, as its own instruction after ADD or
 *             DECALALPHA.
 *   fog       - fog colour (constant 0), unclamped; then * f + fog.
 *
 * An RGB instruction's SRCn_AAA reads the alpha unit's source n, so a step
 * that needs an alpha as an RGB operand names its temp in the alpha
 * address word (measured, docs\decisions\2026-11-05-rs690-fog-probe.md).
 * One-instruction programs come out word for word as the earlier
 * builders wrote them.
 */
static void v9x_rs690_add_shader(const struct v9x_rs690_3d_state *state,
                                 struct v9x_rs690_3d_list *list)
{
    struct v9x_rs690_program program;
    struct v9x_rs690_alu *first;
    v9x_u32 textured = state->textured != 0ul ? 1ul : 0ul;
    v9x_u32 combine = textured != 0ul ? state->texture.combine
                                      : V9X_RS690_TEXCOMBINE_REPLACE;
    v9x_u32 diffuse = textured;
    v9x_u32 side = textured + 1ul;
    v9x_u32 side_used = state->specular != 0ul || state->fog != 0ul;
    v9x_u32 specular_folded = 0ul;
    v9x_u32 alpha_sources;
    v9x_u32 alpha_inst;
    v9x_u32 index;

    program.count = 0ul;
    program.result = side_used ? side + 1ul : textured + 1ul;

    /* The fragment's alpha, made by instruction 0. */
    alpha_sources = 0ul;
    alpha_inst = V9X_RS690_ALU_ALPHA_MOV;
    if (textured != 0ul) {
        v9x_u32 alpha = state->texture_alpha;

        if (alpha == V9X_RS690_TEXALPHA_SAME) {
            alpha = combine == V9X_RS690_TEXCOMBINE_REPLACE ?
                    V9X_RS690_TEXALPHA_TEXEL :
                    combine == V9X_RS690_TEXCOMBINE_MODULATE ?
                    V9X_RS690_TEXALPHA_MODULATE : V9X_RS690_TEXALPHA_DIFFUSE;
        }
        if (alpha == V9X_RS690_TEXALPHA_DIFFUSE) {
            alpha_sources = V9X_RS690_ALU_ADDR1_TEMP1;
            alpha_inst = V9X_RS690_ALU_ALPHA_DIFFUSE;
        } else if (alpha == V9X_RS690_TEXALPHA_MODULATE) {
            alpha_sources = V9X_RS690_ALU_ADDR1_TEMP1;
            alpha_inst = V9X_RS690_ALU_ALPHA_MUL;
        }
    }

    /* Instruction 0: the combine, specular as its C where C is free. */
    if (combine == V9X_RS690_TEXCOMBINE_MODULATE) {
        v9x_rs690_rgb_step(&program, v9x_rs690_sources(0ul, diffuse, side),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_SEL_SRC1_RGB,
                                         state->specular != 0ul ?
                                         V9X_RS690_SEL_SRC2_RGB :
                                         V9X_RS690_SEL_ZERO,
                                         V9X_RS690_ALU_CLAMP), 0ul);
        specular_folded = 1ul;
    } else if (combine == V9X_RS690_TEXCOMBINE_ADD) {
        v9x_rs690_rgb_step(&program, v9x_rs690_sources(0ul, diffuse, 0ul),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_SEL_ONE,
                                         V9X_RS690_SEL_SRC1_RGB,
                                         V9X_RS690_ALU_CLAMP), 0ul);
    } else if (combine == V9X_RS690_TEXCOMBINE_DECALALPHA) {
        v9x_rs690_rgb_step(&program, v9x_rs690_sources(0ul, diffuse, 0ul),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_SEL_ONE,
                                         V9X_RS690_SEL_SRC1_RGB,
                                         V9X_RS690_ALU_NEG_C), 0ul);
    } else {
        v9x_rs690_rgb_step(&program, v9x_rs690_sources(0ul, 0ul, side),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_SEL_ONE,
                                         state->specular != 0ul ?
                                         V9X_RS690_SEL_SRC2_RGB :
                                         V9X_RS690_SEL_ZERO,
                                         V9X_RS690_ALU_CLAMP), 0ul);
        specular_folded = 1ul;
    }
    first = &program.alu[0];
    first->alpha_addr = alpha_sources | V9X_RS690_ALU_ALPHA_WMASK_A |
                        (program.result << V9X_RS690_ALU_ADDRD_SHIFT);
    first->alpha_inst = alpha_inst;
    if (state->specular == 0ul) {
        first->rgb_addr &= ~(V9X_RS690_ALU_ADDR_FIELD <<
                             V9X_RS690_ALU_ADDR2_SHIFT);
    }

    /* DECALALPHA's second half: result * texel.a (alpha source 1 = temp
     * 0) + diffuse. */
    if (combine == V9X_RS690_TEXCOMBINE_DECALALPHA) {
        v9x_rs690_rgb_step(&program,
                           v9x_rs690_sources(program.result, diffuse, 0ul),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_SEL_SRC1_AAA,
                                         V9X_RS690_SEL_SRC1_RGB,
                                         V9X_RS690_ALU_CLAMP),
                           v9x_rs690_sources(program.result, 0ul, 0ul));
    }
    if (state->specular != 0ul && specular_folded == 0ul) {
        v9x_rs690_rgb_step(&program,
                           v9x_rs690_sources(program.result, 0ul, side),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_SEL_ONE,
                                         V9X_RS690_SEL_SRC2_RGB,
                                         V9X_RS690_ALU_CLAMP),
                           v9x_rs690_sources(program.result, 0ul, 0ul));
    }
    if (state->fog != 0ul) {
        v9x_rs690_rgb_step(&program,
                           v9x_rs690_sources(V9X_RS690_ALU_ADDR0_CONST0,
                                             program.result, 0ul),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC1_RGB,
                                         V9X_RS690_SEL_ONE,
                                         V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_ALU_NEG_C), 0ul);
        v9x_rs690_rgb_step(&program,
                           v9x_rs690_sources(V9X_RS690_ALU_ADDR0_CONST0,
                                             program.result, side),
                           v9x_rs690_mad(V9X_RS690_SEL_SRC1_RGB,
                                         V9X_RS690_SEL_SRC2_AAA,
                                         V9X_RS690_SEL_SRC0_RGB,
                                         V9X_RS690_ALU_CLAMP),
                           v9x_rs690_sources(program.result, 0ul, side));
    }

    /* The last instruction writes the output: RGB by the output mask, and
     * the alpha - instruction 0's own, or the result temp's moved out. */
    {
        struct v9x_rs690_alu *last = &program.alu[program.count - 1ul];

        last->rgb_addr = (last->rgb_addr &
                          ~(V9X_RS690_ALU_RGB_WMASK_XYZ |
                            (V9X_RS690_ALU_ADDRD_FIELD <<
                             V9X_RS690_ALU_ADDRD_SHIFT))) |
                         V9X_RS690_ALU_RGB_OMASK_XYZ;
        if (program.count == 1ul) {
            last->alpha_addr = alpha_sources | V9X_RS690_ALU_ALPHA_OMASK_A;
        } else {
            last->alpha_addr = (last->alpha_addr &
                                ~(V9X_RS690_ALU_ADDRD_FIELD <<
                                  V9X_RS690_ALU_ADDRD_SHIFT)) |
                               V9X_RS690_ALU_ALPHA_OMASK_A;
            last->alpha_inst = V9X_RS690_ALU_ALPHA_MOV;
        }
    }

    for (index = 0ul; index < program.count; ++index) {
        v9x_u32 step = index * 4ul;

        v9x_rs690_3d_add(list, V9X_RS690_US_ALU_RGB_ADDR_0 + step,
                         program.alu[index].rgb_addr);
        v9x_rs690_3d_add(list, V9X_RS690_US_ALU_RGB_INST_0 + step,
                         program.alu[index].rgb_inst);
        v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_ADDR_0 + step,
                         program.alu[index].alpha_addr);
        v9x_rs690_3d_add(list, V9X_RS690_US_ALU_ALPHA_INST_0 + step,
                         program.alu[index].alpha_inst);
    }

    /* Highest temp and the sizes: one instruction keeps what the target
     * and texture state wrote. */
    if (program.count == 1ul) {
        if (side_used) {
            v9x_rs690_3d_add(list, V9X_RS690_US_PIXSIZE, side);
        }
        return;
    }
    v9x_rs690_3d_add(list, V9X_RS690_US_PIXSIZE, program.result);
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_OFFSET,
                     (program.count << V9X_RS690_US_ALU_CODE_SIZE_SHIFT) |
                     (textured != 0ul ? V9X_RS690_US_TEX_CODE_SIZE_ONE
                                      : 0ul));
    v9x_rs690_3d_add(list, V9X_RS690_US_CODE_ADDR_3,
                     V9X_RS690_US_LAST_NODE_RGBA_OUT |
                     ((program.count - 1ul) << V9X_RS690_US_ALU_SIZE_SHIFT));
    if (state->fog == 0ul) {
        return;
    }

    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_CONST_R_0,
                     v9x_rs690_float24(v9x_rs690_float_ratio(
                         (state->fog_color >> 16) & 0xfful,
                         V9X_RS690_CHANNEL_MAX)));
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_CONST_G_0,
                     v9x_rs690_float24(v9x_rs690_float_ratio(
                         (state->fog_color >> 8) & 0xfful,
                         V9X_RS690_CHANNEL_MAX)));
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_CONST_B_0,
                     v9x_rs690_float24(v9x_rs690_float_ratio(
                         state->fog_color & 0xfful, V9X_RS690_CHANNEL_MAX)));
    v9x_rs690_3d_add(list, V9X_RS690_US_ALU_CONST_A_0, 0ul);
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
