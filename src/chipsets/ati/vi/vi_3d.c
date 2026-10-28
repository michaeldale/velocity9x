/*
 * AMD Carrizo (GFX8) PM4 streams; see include\velocity9x\vi_3d.h.
 */
#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_clear_state.h"
#include "velocity9x/vi_shader.h"

/* cp_gfx_start (Linux v4.9 gfx_v8_0.c). */
#define VI_PREAMBLE_BEGIN_CLEAR     0x20000000ul
#define VI_PREAMBLE_END_CLEAR       0x30000000ul
#define VI_CONTEXT_CONTROL_LOAD     0x80000000ul
#define VI_CE_PARTITION_BASE        3ul
#define VI_CE_PARTITION_SIZE        0x8000ul
/* PA_SC_RASTER_CONFIG and _1 for CHIP_CARRIZO. */
#define VI_PA_SC_RASTER_CONFIG      0xa0d4ul
#define VI_RASTER_CONFIG            0x00000002ul
#define VI_RASTER_CONFIG_1          0x00000000ul

/* Context registers (gfx_8_0_d.h dword indices). */
#define VI_DB_RENDER_CONTROL        0xa000ul
#define VI_PA_SC_SCREEN_SCISSOR_TL  0xa00cul
#define VI_DB_Z_INFO                0xa010ul
#define VI_DB_DEPTH_VIEW            0xa002ul
#define VI_DB_RENDER_OVERRIDE       0xa003ul
#define VI_DB_HTILE_DATA_BASE       0xa005ul
#define VI_DB_DEPTH_INFO            0xa00ful
#define VI_PA_SC_WINDOW_OFFSET      0xa080ul
#define VI_PA_SC_CLIPRECT_RULE      0xa083ul
#define VI_CB_TARGET_MASK           0xa08eul
#define VI_PA_SC_GENERIC_SCISSOR_TL 0xa090ul
#define VI_PA_SC_VPORT_SCISSOR_0_TL 0xa094ul
#define VI_SPI_PS_INPUT_CNTL_0      0xa191ul
#define VI_SPI_VS_OUT_CONFIG        0xa1b1ul
#define VI_SPI_PS_INPUT_ENA         0xa1b3ul
#define VI_SPI_PS_IN_CONTROL        0xa1b6ul
#define VI_SPI_BARYC_CNTL           0xa1b8ul
#define VI_SPI_SHADER_POS_FORMAT    0xa1c3ul
#define VI_CB_BLEND0_CONTROL        0xa1e0ul
#define VI_DB_DEPTH_CONTROL         0xa200ul
#define VI_CB_COLOR_CONTROL         0xa202ul
#define VI_DB_SHADER_CONTROL        0xa203ul
#define VI_PA_CL_CLIP_CNTL          0xa204ul
#define VI_PA_SU_SC_MODE_CNTL       0xa205ul
#define VI_PA_CL_VTE_CNTL           0xa206ul
#define VI_PA_CL_VS_OUT_CNTL        0xa207ul
#define VI_VGT_GS_MODE              0xa290ul
#define VI_VGT_SHADER_STAGES_EN     0xa2d5ul
#define VI_PA_SU_VTX_CNTL           0xa2f9ul
#define VI_PA_SC_AA_MASK            0xa30eul
#define VI_CB_COLOR0_BASE           0xa318ul
/* SH registers: each stage's RSRC3 sits just before its PGM_LO, and the
 * VS's LATE_ALLOC between them. */
#define VI_SPI_SHADER_PGM_RSRC3_PS  0x2c07ul
#define VI_SPI_SHADER_USER_DATA_PS  0x2c0cul
#define VI_SPI_SHADER_PGM_RSRC3_VS  0x2c46ul
#define VI_SPI_SHADER_USER_DATA_VS  0x2c4cul
#define VI_PA_SC_VPORT_ZMIN_0       0xa0b4ul
#define VI_VGT_MAX_VTX_INDX         0xa100ul
/* UCONFIG registers. */
#define VI_VGT_PRIMITIVE_TYPE       0xc242ul

/* Field values (gfx_8_0_sh_mask.h, _enum.h). */
#define VI_WINDOW_OFFSET_DISABLE    0x80000000ul
#define VI_CLIPRECT_RULE_ALL        0x0000fffful
#define VI_CB_TARGET_MASK_RGBA      0x0000000ful
/* CB_COLOR_CONTROL: MODE CB_NORMAL (1) at 6:4, ROP3 copy (CCh) at 23:16. */
#define VI_CB_COLOR_CONTROL_NORMAL  0x00cc0010ul
/* PA_CL_CLIP_CNTL: CLIP_DISABLE (bit 16), DX_CLIP_SPACE_DEF (bit 19): the
 * positions are screen pixels, so nothing is clipped. */
#define VI_CLIP_CNTL_DISABLED       0x00090000ul
/* PA_CL_VTE_CNTL: VTX_XY_FMT and VTX_Z_FMT and no viewport transform: the
 * position arrives in screen space. VTX_W0_FMT stays clear, so the
 * position's w, Direct3D's rhw, weights the perspective barycentrics
 * right. Set, the weighting came out inverted: V9XDDP's perspective row
 * turned at 20% of the edge, where correct is 80% (M-XP7053, 2026-10-29),
 * as Cedar measured on the same PA (eg_3d.c, EG_VTE_XY_Z_DIVIDED). */
#define VI_VTE_SCREEN_SPACE         0x00000300ul
/* PA_SU_VTX_CNTL: PIX_CENTER 1, ROUND_MODE X_ROUND_TO_EVEN (2), QUANT_MODE
 * 1/256th (5): radeonsi's. PIX_CENTER 0 puts pixel centres on whole
 * coordinates, Direct3D's (Gallium Nine's half_pixel_center false). */
#define VI_SU_VTX_CNTL              0x0000002dul
#define VI_SU_VTX_CNTL_D3D          0x0000002cul
#define VI_AA_MASK_ALL              0xfffffffful
/* SPI_SHADER_POS_FORMAT POS0 SPI_SHADER_4COMP; SPI_SHADER_COL_FORMAT COL0
 * SPI_SHADER_32_ABGR; SPI_SHADER_Z_FORMAT SPI_SHADER_ZERO. */
#define VI_POS_FORMAT_4COMP         0x00000004ul
#define VI_COL_FORMAT_32_ABGR       0x00000009ul
/* SPI_SHADER_FP16_ABGR: the Direct3D shaders' compressed export
 * (vi_shader.h), which an 8-bit target blends from right. */
#define VI_COL_FORMAT_FP16_ABGR     0x00000004ul
/* SPI_PS_INPUT_ENA/ADDR PERSP_CENTER_ENA: one barycentric must be on. */
#define VI_PS_INPUT_PERSP_CENTER    0x00000002ul
/* SPI_VS_OUT_CONFIG VS_EXPORT_COUNT at 5:1, parameters less one;
 * SPI_PS_IN_CONTROL NUM_INTERP at 5:0; SPI_PS_INPUT_CNTL_n OFFSET at 5:0,
 * the VS parameter PS input n reads. */
#define VI_VS_EXPORT_COUNT(n)       ((v9x_u32)(n) << 1)
#define VI_RECT_PARAMS              1ul
/* Colour, (tu, tv) and specular (vi_shader.h). */
#define VI_TL_PARAMS                3ul
/* SPI_SHADER_PGM_RSRC1: VGPRS 1 (8), SGPRS 2 (24), FLOAT_MODE C0h, the
 * value radeonsi's float mode gives. RSRC2 USER_SGPR at 5:1. */
/* VGPRS 5:0 in granules of four, less one; SGPRS 9:6 2 (24); FLOAT_MODE
 * C0h at 19:12. VI_RSRC1(8) is 000C0081h, the value the rectangle ran. */
#define VI_RSRC1(vgprs)             ((((v9x_u32)(vgprs) + 3ul) / 4ul - 1ul) | \
                                     0x000c0080ul)
#define VI_SMALL_VGPRS              8ul
#define VI_RSRC2_USER_SGPRS(n)      ((v9x_u32)(n) << 1)
/* SPI_SHADER_PGM_RSRC3: CU_EN 15:0 every CU, WAVE_LIMIT 21:16 the most,
 * radeonsi's for both stages. Nothing else writes it, and with CU_EN 0 no
 * wave launches (V9XCZ3D runs 1 and 2: the fence landed, no pixel did). */
#define VI_RSRC3_ALL_CUS            0x003ffffful
#define VI_FLOAT_ONE                0x3f800000ul
#define VI_DI_PT_RECTLIST           0x11ul
#define VI_DI_PT_TRILIST            0x04ul
#define VI_DI_SRC_SEL_AUTO_INDEX    0x2ul
/* ring_emit_fence_gfx: EOP_TCL1_ACTION_EN, EOP_TC_ACTION_EN,
 * EOP_TC_WB_ACTION_EN, CACHE_FLUSH_AND_INV_TS_EVENT (14h), EVENT_INDEX 5;
 * DATA_SEL 1 (32 bits), INT_SEL 0. */
#define VI_EOP_FLUSH_EVENT          0x00038514ul
#define VI_EOP_DATA_SEL_32          (1ul << 29)

/* gfx_v8_0_emit_mem_sync's CP_COHER_CNTL: TC_WB_ACTION_ENA (18),
 * TCL1_ACTION_ENA (22), TC_ACTION_ENA (23), SH_KCACHE_ACTION_ENA (27),
 * SH_ICACHE_ACTION_ENA (29); the whole address range, vid.h's poll
 * interval. */
#define VI_COHER_SHADER_CACHES      0x28c40000ul
#define VI_COHER_SIZE_ALL           0xfffffffful
#define VI_COHER_POLL_INTERVAL      0x0000000aul

/* Depth (gfx_8_0_sh_mask.h). DB_Z_INFO: FORMAT Z_16 (1) at 1:0,
 * TILE_SPLIT 15:13 and TILE_MODE_INDEX 22:20, both from GB_TILE_MODE5.
 * DB_DEPTH_INFO: ADDR5_SWIZZLE_MASK (bit 0, radeonsi's for a surface
 * without TC-compatible HTILE), ARRAY_MODE 1D_TILED_THIN1 (2) at 7:4,
 * PIPE_CONFIG P2 (0). No HTILE: HiZ and HiS forced off (FORCE_DISABLE at
 * 1:0, 3:2, 5:4), depth and stencil compression disabled (bits 6, 5), as
 * Cedar's Z16 ran. DB_DEPTH_CONTROL: Z_ENABLE bit 1, Z_WRITE_ENABLE bit 2,
 * ZFUNC 6:4. */
#define VI_Z_INFO_Z16_TILE5         (1ul | (5ul << 13) | \
                                     (V9X_VI_TILE_DEPTH_1D << 20))
#define VI_DEPTH_INFO_1D_P2         (1ul | (2ul << 4))
#define VI_RENDER_OVERRIDE_NO_HI    0x0000002aul
#define VI_RENDER_CONTROL_NO_COMPRESS 0x00000060ul
#define VI_DEPTH_CONTROL_Z_ENABLE   0x00000002ul
#define VI_DEPTH_CONTROL_Z_WRITE    0x00000004ul
#define VI_DEPTH_CONTROL_ZFUNC(f)   (((v9x_u32)(f) & 7ul) << 4)

/* Textures (Mesa's sid.h). T# word 1: IMG_DATA_FORMAT at 25:20,
 * IMG_NUM_FORMAT_UNORM (0) at 29:26. Word 3: DST_SEL_X..W at 2:0, 5:3,
 * 8:6, 11:9, SQ_SEL_X..W 4..7; TYPE SQ_RSRC_IMG_2D (9) at 31:28. S# word 0: CLAMP_X/Y/Z SQ_TEX_CLAMP_LAST_TEXEL (2) at 2:0, 5:3,
 * 8:6. Word 2: XY_MAG_FILTER and XY_MIN_FILTER at 21:20 and 23:22,
 * point 0, bilinear 1. WIDTH and HEIGHT are 14-bit fields of the size
 * less one. */
#define VI_IMG_NUM_FORMAT_UNORM     0ul
/* X, Y, Z, W: memory order. The VS's colour and the CB keep it too, so a
 * texel's bytes come out where they went in (vi_shader.h). */
#define VI_IMG_SWIZZLE_IDENTITY     (4ul | (5ul << 3) | (6ul << 6) | (7ul << 9))
/* T# word 3 POW2_PAD, bit 25: radeonsi sets it for a texture with more
 * than one level, the levels' layout then padded to powers of two. */
#define VI_IMG_POW2_PAD             0x02000000ul
/* v9x_vi_3d_mip_layout's measured steps (vi_3d.h). */
#define VI_MIP_ROW_ALIGN            64ul
#define VI_MIP_LEVEL_ALIGN          256ul
/* X, Y, Z and SQ_SEL_1 for W. */
#define VI_IMG_SWIZZLE_OPAQUE       (4ul | (5ul << 3) | (6ul << 6) | (1ul << 9))
#define VI_IMG_TYPE_2D              9ul
#define VI_IMG_MAX_SIZE             16384ul
#define VI_SQ_TEX_WRAP              0ul
#define VI_SQ_TEX_MIRROR            1ul
#define VI_SQ_TEX_CLAMP_LAST_TEXEL  2ul
#define VI_SAMP_XY_BILINEAR         ((1ul << 20) | (1ul << 22))

/* Blending (Mesa's sid.h): CB_BLEND0_CONTROL COLOR_SRCBLEND 4:0,
 * COLOR_COMB_FCN 7:5 (DST_PLUS_SRC 0), COLOR_DESTBLEND 12:8, the alpha's
 * at 20:16, 23:21 and 28:24, ENABLE bit 30; CB_COLOR0_INFO BLEND_CLAMP
 * bit 15, which radeonsi sets on a UNORM target. DB_SHADER_CONTROL
 * KILL_ENABLE bit 6, for a PS that kills pixels. */
#define VI_BLEND_ENABLE             0x40000000ul
/* CB_COLOR_CONTROL DISABLE_DUAL_QUAD, bit 0, with blending: the CB's
 * 4x2 dual quads blended wrong on M-XP7053 - V9XD32's T7 block came out
 * in a pattern four pixels wide and two high, one column in four right
 * (docs\decisions\2026-10-28-carrizo-d3d-blend-and-shader-fixed-function.md). */
#define VI_CB_DISABLE_DUAL_QUAD     0x00000001ul
#define VI_CB_INFO_BLEND_CLAMP      0x00008000ul
#define VI_DB_SHADER_KILL_ENABLE    0x00000040ul

#define VI_SHADER_ALIGN             0xfful

void v9x_vi_pm4_init(struct v9x_vi_pm4 *stream, v9x_u32 *dwords,
                     v9x_u32 capacity)
{
    stream->dwords = dwords;
    stream->count = 0ul;
    stream->capacity = capacity;
    stream->overflow = 0ul;
}

void v9x_vi_pm4_put(struct v9x_vi_pm4 *stream, v9x_u32 value)
{
    if (stream->count >= stream->capacity) {
        stream->overflow = 1ul;
        return;
    }
    stream->dwords[stream->count++] = value;
}

static void v9x_vi_pm4_set(struct v9x_vi_pm4 *stream, v9x_u32 opcode,
                           v9x_u32 start, v9x_u32 reg,
                           const v9x_u32 *values, v9x_u32 count)
{
    v9x_u32 index;

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(opcode, count));
    v9x_vi_pm4_put(stream, reg - start);
    for (index = 0ul; index < count; ++index) {
        v9x_vi_pm4_put(stream, values[index]);
    }
}

void v9x_vi_pm4_set_context(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                            const v9x_u32 *values, v9x_u32 count)
{
    v9x_vi_pm4_set(stream, V9X_VI_P3_SET_CONTEXT_REG,
                   V9X_VI_CONTEXT_REG_START, reg, values, count);
}

void v9x_vi_pm4_set_sh(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                       const v9x_u32 *values, v9x_u32 count)
{
    v9x_vi_pm4_set(stream, V9X_VI_P3_SET_SH_REG, V9X_VI_SH_REG_START, reg,
                   values, count);
}

void v9x_vi_pm4_set_uconfig(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                            v9x_u32 value)
{
    v9x_vi_pm4_set(stream, V9X_VI_P3_SET_UCONFIG_REG,
                   V9X_VI_UCONFIG_REG_START, reg, &value, 1ul);
}

void v9x_vi_pm4_pad(struct v9x_vi_pm4 *stream)
{
    while ((stream->count % V9X_VI_RING_ALIGN_DWORDS) != 0ul &&
           stream->overflow == 0ul) {
        v9x_vi_pm4_put(stream, V9X_VI_RING_NOP);
    }
}

static void v9x_vi_context1(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                            v9x_u32 value)
{
    v9x_vi_pm4_set_context(stream, reg, &value, 1ul);
}

void v9x_vi_3d_start(struct v9x_vi_pm4 *stream)
{
    v9x_u32 run;
    v9x_u32 first = 0ul;
    v9x_u32 raster[2];

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    v9x_vi_pm4_put(stream, VI_PREAMBLE_BEGIN_CLEAR);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_CONTEXT_CONTROL, 1ul));
    v9x_vi_pm4_put(stream, VI_CONTEXT_CONTROL_LOAD);
    v9x_vi_pm4_put(stream, VI_CONTEXT_CONTROL_LOAD);
    for (run = 0ul; run < V9X_VI_CLEAR_STATE_RUNS; ++run) {
        v9x_vi_pm4_set_context(stream, v9x_vi_clear_state_runs[run].first,
                               v9x_vi_clear_state_values + first,
                               v9x_vi_clear_state_runs[run].count);
        first += v9x_vi_clear_state_runs[run].count;
    }
    raster[0] = VI_RASTER_CONFIG;
    raster[1] = VI_RASTER_CONFIG_1;
    v9x_vi_pm4_set_context(stream, VI_PA_SC_RASTER_CONFIG, raster, 2ul);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    v9x_vi_pm4_put(stream, VI_PREAMBLE_END_CLEAR);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_CLEAR_STATE, 0ul));
    v9x_vi_pm4_put(stream, 0ul);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_SET_BASE, 2ul));
    v9x_vi_pm4_put(stream, VI_CE_PARTITION_BASE);
    v9x_vi_pm4_put(stream, VI_CE_PARTITION_SIZE);
    v9x_vi_pm4_put(stream, VI_CE_PARTITION_SIZE);
}

/* A scissor pair: top-left with the window offset off, bottom-right. */
static void v9x_vi_scissor(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                           v9x_u32 width, v9x_u32 height, int offset_bit)
{
    v9x_u32 values[2];

    values[0] = offset_bit ? VI_WINDOW_OFFSET_DISABLE : 0ul;
    values[1] = width | (height << 16);
    v9x_vi_pm4_set_context(stream, reg, values, 2ul);
}

/* A pixel coordinate as IEEE single bits, by integer arithmetic: every
 * value below 2^24 is exact, and the targets are far smaller. */
static v9x_u32 v9x_vi_float_bits(v9x_u32 value)
{
    v9x_u32 exponent = 0ul;

    if (value == 0ul) {
        return 0ul;
    }
    while ((value >> exponent) > 1ul) {
        ++exponent;
    }
    return ((127ul + exponent) << 23) |
           ((value << (23ul - exponent)) & 0x007ffffful);
}

/* The 40-bit address >> 8, as a 32-bit register takes it: bits 39:32 in
 * 31:24. */
static v9x_u32 v9x_vi_address_256(v9x_u32 low, v9x_u32 high)
{
    return (low >> 8) | ((high & 0xfful) << 24);
}

/* Linux's gfx ring puts this before every IB: the shaders and their data
 * were written by the CPU, and the SQ's instruction and constant caches
 * and TC L2 may hold what was at those addresses before. The end-of-pipe
 * event flushes TC but leaves the SQ caches; without this M-XP7053 ran an
 * earlier run's shaders. */
static void v9x_vi_3d_cache_sync(struct v9x_vi_pm4 *stream)
{
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_SURFACE_SYNC, 3ul));
    v9x_vi_pm4_put(stream, VI_COHER_SHADER_CACHES);
    v9x_vi_pm4_put(stream, VI_COHER_SIZE_ALL);
    v9x_vi_pm4_put(stream, 0ul);
    v9x_vi_pm4_put(stream, VI_COHER_POLL_INTERVAL);
}

/* The target's checks both draws share: a pitch of whole 8-pixel tiles,
 * a slice of whole 64-pixel tiles, a 256-byte-aligned base. */
static int v9x_vi_3d_target_ok(v9x_u32 low, v9x_u32 pitch, v9x_u32 height)
{
    return pitch != 0ul && (pitch % 8ul) == 0ul && height != 0ul &&
           (low & VI_SHADER_ALIGN) == 0ul && (pitch * height) % 64ul == 0ul;
}

/* The colour buffer, no depth or stencil, screen-space positions with
 * nothing clipped or culled, every scissor the target, and the shader
 * interface: params VS parameters (1 or 2), four position components,
 * MRT0 as four 32-bit floats, params perspective-correct interpolants,
 * PS input n from VS parameter n. target_mask is CB_TARGET_MASK's RGBA
 * bits for MRT0; vtx_cntl is PA_SU_VTX_CNTL. */
static void v9x_vi_3d_common_state(struct v9x_vi_pm4 *stream, v9x_u32 low,
                                   v9x_u32 high, v9x_u32 pitch,
                                   v9x_u32 height, v9x_u32 format,
                                   v9x_u32 tile_mode_index, v9x_u32 params,
                                   v9x_u32 target_mask, v9x_u32 vtx_cntl,
                                   v9x_u32 blend_control,
                                   v9x_u32 db_shader_control,
                                   v9x_u32 col_format)
{
    v9x_u32 values[13];
    v9x_u32 index;

    /* CB_COLOR0_BASE .. CLEAR_WORD1. PITCH and SLICE are TILE_MAX fields,
     * 8 pixels and 64 pixels a tile, less one. */
    for (index = 0ul; index < 13ul; ++index) {
        values[index] = 0ul;
    }
    values[0] = v9x_vi_address_256(low, high);
    values[1] = pitch / 8ul - 1ul;
    values[2] = (pitch * height) / 64ul - 1ul;
    values[4] = ((format & 0x1ful) << 2) |
                (blend_control != 0ul ? VI_CB_INFO_BLEND_CLAMP : 0ul);
    values[5] = tile_mode_index & 0x1ful;
    v9x_vi_pm4_set_context(stream, VI_CB_COLOR0_BASE, values, 13ul);
    values[0] = target_mask & VI_CB_TARGET_MASK_RGBA;
    values[1] = VI_CB_TARGET_MASK_RGBA;
    v9x_vi_pm4_set_context(stream, VI_CB_TARGET_MASK, values, 2ul);
    v9x_vi_context1(stream, VI_CB_COLOR_CONTROL,
                    VI_CB_COLOR_CONTROL_NORMAL |
                    (blend_control != 0ul ? VI_CB_DISABLE_DUAL_QUAD : 0ul));
    v9x_vi_context1(stream, VI_CB_BLEND0_CONTROL, blend_control);

    /* No depth or stencil. */
    v9x_vi_context1(stream, VI_DB_RENDER_CONTROL, 0ul);
    values[0] = 0ul;
    values[1] = 0ul;
    v9x_vi_pm4_set_context(stream, VI_DB_Z_INFO, values, 2ul);
    v9x_vi_context1(stream, VI_DB_DEPTH_CONTROL, 0ul);
    v9x_vi_context1(stream, VI_DB_SHADER_CONTROL, db_shader_control);

    v9x_vi_context1(stream, VI_PA_CL_CLIP_CNTL, VI_CLIP_CNTL_DISABLED);
    v9x_vi_context1(stream, VI_PA_SU_SC_MODE_CNTL, 0ul);
    v9x_vi_context1(stream, VI_PA_CL_VTE_CNTL, VI_VTE_SCREEN_SPACE);
    v9x_vi_context1(stream, VI_PA_CL_VS_OUT_CNTL, 0ul);
    v9x_vi_context1(stream, VI_PA_SU_VTX_CNTL, vtx_cntl);
    v9x_vi_scissor(stream, VI_PA_SC_SCREEN_SCISSOR_TL, pitch, height, 0);
    v9x_vi_context1(stream, VI_PA_SC_WINDOW_OFFSET, 0ul);
    v9x_vi_scissor(stream, VI_PA_SC_WINDOW_OFFSET + 1ul, pitch, height, 1);
    v9x_vi_context1(stream, VI_PA_SC_CLIPRECT_RULE, VI_CLIPRECT_RULE_ALL);
    v9x_vi_scissor(stream, VI_PA_SC_GENERIC_SCISSOR_TL, pitch, height, 1);
    v9x_vi_scissor(stream, VI_PA_SC_VPORT_SCISSOR_0_TL, pitch, height, 1);
    values[0] = VI_AA_MASK_ALL;
    values[1] = VI_AA_MASK_ALL;
    v9x_vi_pm4_set_context(stream, VI_PA_SC_AA_MASK, values, 2ul);
    values[0] = 0ul;
    values[1] = VI_FLOAT_ONE;
    v9x_vi_pm4_set_context(stream, VI_PA_SC_VPORT_ZMIN_0, values, 2ul);

    v9x_vi_context1(stream, VI_SPI_VS_OUT_CONFIG,
                    VI_VS_EXPORT_COUNT(params - 1ul));
    v9x_vi_context1(stream, VI_SPI_SHADER_POS_FORMAT, VI_POS_FORMAT_4COMP);
    values[0] = 0ul;
    values[1] = col_format;
    v9x_vi_pm4_set_context(stream, VI_SPI_SHADER_POS_FORMAT + 1ul, values,
                           2ul);
    values[0] = VI_PS_INPUT_PERSP_CENTER;
    values[1] = VI_PS_INPUT_PERSP_CENTER;
    v9x_vi_pm4_set_context(stream, VI_SPI_PS_INPUT_ENA, values, 2ul);
    v9x_vi_context1(stream, VI_SPI_PS_IN_CONTROL, params);
    for (index = 0ul; index < params; ++index) {
        v9x_vi_context1(stream, VI_SPI_PS_INPUT_CNTL_0 + index, index);
    }
    v9x_vi_context1(stream, VI_SPI_BARYC_CNTL, 0ul);
    v9x_vi_context1(stream, VI_VGT_SHADER_STAGES_EN, 0ul);
    v9x_vi_context1(stream, VI_VGT_GS_MODE, 0ul);
}

/* A Z16 depth buffer of pitch x height in the 1D depth tile mode. It
 * follows the shared state's no-depth writes, so these are the ones that
 * hold. Stencil stays invalid; its bases name the depth buffer, as
 * radeonsi's do. */
static void v9x_vi_3d_depth(struct v9x_vi_pm4 *stream, v9x_u32 low,
                            v9x_u32 high, v9x_u32 pitch, v9x_u32 height,
                            v9x_u32 func, v9x_u32 write)
{
    v9x_u32 values[9];
    v9x_u32 base = v9x_vi_address_256(low, high);

    values[0] = VI_DEPTH_INFO_1D_P2;
    values[1] = VI_Z_INFO_Z16_TILE5;
    values[2] = 0ul;
    values[3] = base;
    values[4] = base;
    values[5] = base;
    values[6] = base;
    /* PITCH_TILE_MAX 10:0, HEIGHT_TILE_MAX 21:11, in 8-pixel tiles less
     * one; SLICE_TILE_MAX in 64-pixel tiles less one. */
    values[7] = (pitch / 8ul - 1ul) | ((height / 8ul - 1ul) << 11);
    values[8] = (pitch * height) / 64ul - 1ul;
    v9x_vi_pm4_set_context(stream, VI_DB_DEPTH_INFO, values, 9ul);
    v9x_vi_context1(stream, VI_DB_DEPTH_VIEW, 0ul);
    v9x_vi_context1(stream, VI_DB_HTILE_DATA_BASE, 0ul);
    v9x_vi_context1(stream, VI_DB_RENDER_CONTROL,
                    VI_RENDER_CONTROL_NO_COMPRESS);
    v9x_vi_context1(stream, VI_DB_RENDER_OVERRIDE, VI_RENDER_OVERRIDE_NO_HI);
    v9x_vi_context1(stream, VI_DB_DEPTH_CONTROL,
                    VI_DEPTH_CONTROL_Z_ENABLE |
                    (write != 0ul ? VI_DEPTH_CONTROL_Z_WRITE : 0ul) |
                    VI_DEPTH_CONTROL_ZFUNC(func));
}

/* A stage's program registers from RSRC3 on: PGM_LO is the address >> 8,
 * PGM_HI what is above bit 39, nothing. The VS has LATE_ALLOC between
 * RSRC3 and PGM_LO. */
static void v9x_vi_3d_program(struct v9x_vi_pm4 *stream, v9x_u32 rsrc3_reg,
                              int vs, v9x_u32 low, v9x_u32 high,
                              v9x_u32 vgprs, v9x_u32 user_sgprs)
{
    v9x_u32 values[6];
    v9x_u32 count = 0ul;

    values[count++] = VI_RSRC3_ALL_CUS;
    if (vs) {
        values[count++] = 0ul;
    }
    values[count++] = v9x_vi_address_256(low, high);
    values[count++] = 0ul;
    values[count++] = VI_RSRC1(vgprs);
    values[count++] = VI_RSRC2_USER_SGPRS(user_sgprs);
    v9x_vi_pm4_set_sh(stream, rsrc3_reg, values, count);
}

/* VGT_MAX_VTX_INDX, _MIN_VTX_INDX, _INDX_OFFSET, as radeonsi's init state
 * sets them; clearstate_vi.h leaves the maximum 0. Not needed for an
 * auto-indexed draw: without it M-XP7053 still drew, VertexIDs 0-2
 * (docs\decisions\2026-10-28-carrizo-first-rectangle.md). It is kept for
 * indexed draws, where the clamp applies. Then count auto-indexed
 * vertices of prim, and the fence once the draw is out of the pipe and the
 * colour caches are flushed to memory. */
static void v9x_vi_3d_draw_and_fence(struct v9x_vi_pm4 *stream, v9x_u32 prim,
                                     v9x_u32 count, v9x_u32 fence_low,
                                     v9x_u32 fence_high, v9x_u32 fence_value)
{
    v9x_u32 values[3];

    values[0] = 0xfffffffful;
    values[1] = 0ul;
    values[2] = 0ul;
    v9x_vi_pm4_set_context(stream, VI_VGT_MAX_VTX_INDX, values, 3ul);

    v9x_vi_pm4_set_uconfig(stream, VI_VGT_PRIMITIVE_TYPE, prim);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_NUM_INSTANCES, 0ul));
    v9x_vi_pm4_put(stream, 1ul);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_DRAW_INDEX_AUTO, 1ul));
    v9x_vi_pm4_put(stream, count);
    v9x_vi_pm4_put(stream, VI_DI_SRC_SEL_AUTO_INDEX);

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_EVENT_WRITE_EOP, 4ul));
    v9x_vi_pm4_put(stream, VI_EOP_FLUSH_EVENT);
    v9x_vi_pm4_put(stream, fence_low & ~3ul);
    v9x_vi_pm4_put(stream, (fence_high & 0xfffful) | VI_EOP_DATA_SEL_32);
    v9x_vi_pm4_put(stream, fence_value);
    v9x_vi_pm4_put(stream, 0ul);
}

v9x_status v9x_vi_3d_solid_rect(struct v9x_vi_pm4 *stream,
                                const struct v9x_vi_solid_rect *rect)
{
    v9x_u32 values[6];
    v9x_u32 index;
    v9x_u32 vs_sgprs;
    v9x_u32 ps_sgprs;

    if (stream == 0 || rect == 0 ||
        !v9x_vi_3d_target_ok(rect->target_low, rect->pitch_pixels,
                             rect->height) ||
        rect->left >= rect->right || rect->top >= rect->bottom ||
        rect->right > rect->pitch_pixels || rect->bottom > rect->height ||
        ((rect->vs_low | rect->ps_low) & VI_SHADER_ALIGN) != 0ul ||
        ((rect->fence_low | rect->probe_vs_low | rect->probe_ps_low) &
         3ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A probe shader's address follows the four user SGPRs as s4:s5. */
    vs_sgprs = rect->probe_vs_low != 0ul ? 6ul : 4ul;
    ps_sgprs = rect->probe_ps_low != 0ul ? 6ul : 4ul;

    v9x_vi_3d_cache_sync(stream);
    v9x_vi_3d_common_state(stream, rect->target_low, rect->target_high,
                           rect->pitch_pixels, rect->height, rect->format,
                           rect->tile_mode_index, VI_RECT_PARAMS,
                           VI_CB_TARGET_MASK_RGBA, VI_SU_VTX_CNTL, 0ul, 0ul,
                           VI_COL_FORMAT_32_ABGR);

    v9x_vi_3d_program(stream, VI_SPI_SHADER_PGM_RSRC3_VS, 1, rect->vs_low,
                      rect->shader_high, VI_SMALL_VGPRS, vs_sgprs);
    values[0] = v9x_vi_float_bits(rect->left);
    values[1] = v9x_vi_float_bits(rect->top);
    values[2] = v9x_vi_float_bits(rect->right);
    values[3] = v9x_vi_float_bits(rect->bottom);
    values[4] = rect->probe_vs_low;
    values[5] = rect->probe_high;
    v9x_vi_pm4_set_sh(stream, VI_SPI_SHADER_USER_DATA_VS, values, vs_sgprs);
    v9x_vi_3d_program(stream, VI_SPI_SHADER_PGM_RSRC3_PS, 0, rect->ps_low,
                      rect->shader_high, VI_SMALL_VGPRS, ps_sgprs);
    for (index = 0ul; index < 4ul; ++index) {
        values[index] = rect->color[index];
    }
    values[4] = rect->probe_ps_low;
    values[5] = rect->probe_high;
    v9x_vi_pm4_set_sh(stream, VI_SPI_SHADER_USER_DATA_PS, values, ps_sgprs);

    /* Three vertices of a RECTLIST. */
    v9x_vi_3d_draw_and_fence(stream, VI_DI_PT_RECTLIST, 3ul, rect->fence_low,
                             rect->fence_high, rect->fence_value);
    return stream->overflow == 0ul ? V9X_STATUS_OK : V9X_STATUS_INVALID_ARGUMENT;
}

/* The colour's and the alpha's factors alike, added. */
v9x_u32 v9x_vi_3d_blend(v9x_u32 source, v9x_u32 destination)
{
    v9x_u32 pair = (source & 0x1ful) | ((destination & 0x1ful) << 8);

    return pair | (pair << 16) | VI_BLEND_ENABLE;
}

static int v9x_vi_3d_power_of_two(v9x_u32 value)
{
    return value != 0ul && (value & (value - 1ul)) == 0ul;
}

int v9x_vi_3d_mip_layout(v9x_u32 bytes_per_texel, v9x_u32 width,
                         v9x_u32 height, v9x_u32 levels, v9x_u32 *offsets,
                         v9x_u32 *pitches, v9x_u32 *total_bytes)
{
    v9x_u32 offset = 0ul;
    v9x_u32 level;
    v9x_u32 row;
    v9x_u32 w = width;
    v9x_u32 h = height;
    v9x_u32 longest = width > height ? width : height;
    v9x_u32 possible = 1ul;

    if ((bytes_per_texel != 2ul && bytes_per_texel != 4ul) ||
        !v9x_vi_3d_power_of_two(width) || !v9x_vi_3d_power_of_two(height) ||
        levels == 0ul || levels > V9X_VI_MIP_LEVELS_MAX) {
        return 0;
    }
    while (longest > 1ul) {
        longest >>= 1;
        ++possible;
    }
    if (levels > possible) {
        return 0;
    }
    for (level = 0ul; level < levels; ++level) {
        row = (w * bytes_per_texel + VI_MIP_ROW_ALIGN - 1ul) &
              ~(VI_MIP_ROW_ALIGN - 1ul);
        while (((row * h) & (VI_MIP_LEVEL_ALIGN - 1ul)) != 0ul) {
            row += VI_MIP_ROW_ALIGN;
        }
        offsets[level] = offset;
        pitches[level] = row / bytes_per_texel;
        offset += row * h;
        w = w > 1ul ? w >> 1 : 1ul;
        h = h > 1ul ? h >> 1 : 1ul;
    }
    *total_bytes = offset;
    return 1;
}

/* CLAMP_X, _Y and _Z alike. */
static v9x_u32 v9x_vi_3d_sampler_clamp(v9x_u32 address)
{
    v9x_u32 clamp = VI_SQ_TEX_CLAMP_LAST_TEXEL;

    if (address == V9X_VI_TEX_ADDRESS_WRAP) {
        clamp = VI_SQ_TEX_WRAP;
    } else if (address == V9X_VI_TEX_ADDRESS_MIRROR) {
        clamp = VI_SQ_TEX_MIRROR;
    }
    return clamp | (clamp << 3) | (clamp << 6);
}

static v9x_u32 v9x_vi_3d_texture_format(const struct v9x_vi_tl_draw *draw)
{
    return draw->texture_format != 0ul ? draw->texture_format
                                       : V9X_VI_IMG_FORMAT_8_8_8_8;
}

/* The T# for a 2D texture in memory order and an S# that clamps to the last
 * texel at LOD 0, point or bilinear (Mesa's sid.h, SQ_IMG_RSRC_WORD0-7
 * and SQ_IMG_SAMP_WORD0-3). Twelve dwords to out. */
static void v9x_vi_3d_texture_descriptors(const struct v9x_vi_tl_draw *draw,
                                          v9x_u32 *out)
{
    v9x_u32 index;

    for (index = 0ul; index < V9X_VI_TEXTURE_USER_SGPRS; ++index) {
        out[index] = 0ul;
    }
    out[0] = v9x_vi_address_256(draw->texture_low, draw->texture_high);
    out[1] = ((draw->texture_high >> 8) & 0xfful) |
             (v9x_vi_3d_texture_format(draw) << 20) |
             (VI_IMG_NUM_FORMAT_UNORM << 26);
    out[2] = (draw->texture_width - 1ul) |
             ((draw->texture_height - 1ul) << 14);
    /* RGB565 has no fourth component, and a W asking for one reads 0:
     * MODULATE under SRCALPHA would draw nothing. It takes the constant 1,
     * Cedar's fix (docs\decisions\2026-10-27-cedar-texture-pitch-and-565-
     * alpha.md). */
    out[3] = (v9x_vi_3d_texture_format(draw) == V9X_VI_IMG_FORMAT_5_6_5 ||
              draw->texture_opaque != 0ul ?
              VI_IMG_SWIZZLE_OPAQUE : VI_IMG_SWIZZLE_IDENTITY) |
             ((draw->texture_base_level & 0xful) << 12) |
             ((draw->texture_last_level & 0xful) << 16) |
             ((draw->texture_tile_mode_index & 0x1ful) << 20) |
             (draw->texture_last_level != 0ul ? VI_IMG_POW2_PAD : 0ul) |
             (VI_IMG_TYPE_2D << 28);
    out[4] = (draw->texture_pitch - 1ul) << 13;
    out[8] = v9x_vi_3d_sampler_clamp(draw->texture_address);
    /* MAX_LOD in u4.8 at 23:12: the last level; MIN_LOD 0. */
    out[9] = (draw->texture_last_level & 0xful) << 20;
    out[10] = (draw->texture_bilinear != 0ul ? VI_SAMP_XY_BILINEAR : 0ul) |
              ((draw->texture_mip_filter & 3ul) << 26);
}

v9x_status v9x_vi_3d_tl_draw(struct v9x_vi_pm4 *stream,
                             const struct v9x_vi_tl_draw *draw)
{
    v9x_u32 values[V9X_VI_TEXTURE_USER_SGPRS];
    v9x_u32 ps_sgprs;
    v9x_u32 depth_pitch;
    v9x_u32 depth_height;
    v9x_u32 format;

    if (draw == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    depth_pitch = draw->depth_pitch != 0ul ? draw->depth_pitch
                                           : draw->pitch_pixels;
    depth_height = draw->depth_height != 0ul ? draw->depth_height
                                             : draw->height;
    format = v9x_vi_3d_texture_format(draw);
    if (stream == 0 ||
        !v9x_vi_3d_target_ok(draw->target_low, draw->pitch_pixels,
                             draw->height) ||
        ((draw->vs_low | draw->ps_low) & VI_SHADER_ALIGN) != 0ul ||
        ((draw->fence_low | draw->vertex_low) & 3ul) != 0ul ||
        draw->vertex_count == 0ul || (draw->vertex_count % 3ul) != 0ul ||
        (draw->depth_low & VI_SHADER_ALIGN) != 0ul ||
        draw->depth_func > V9X_VI_ZFUNC_ALWAYS ||
        (draw->depth_low != 0ul &&
         (depth_pitch == 0ul || (depth_pitch % 8ul) != 0ul ||
          depth_height == 0ul || (depth_height % 8ul) != 0ul)) ||
        (draw->texture_low & VI_SHADER_ALIGN) != 0ul ||
        (draw->texture_low != 0ul &&
         (format != V9X_VI_IMG_FORMAT_8_8_8_8 &&
          format != V9X_VI_IMG_FORMAT_5_6_5 &&
          format != V9X_VI_IMG_FORMAT_1_5_5_5 &&
          format != V9X_VI_IMG_FORMAT_4_4_4_4)) ||
        (draw->texture_low != 0ul &&
         (draw->texture_width == 0ul ||
          draw->texture_width > VI_IMG_MAX_SIZE ||
          draw->texture_height == 0ul ||
          draw->texture_height > VI_IMG_MAX_SIZE ||
          draw->texture_pitch < draw->texture_width ||
          draw->texture_pitch > VI_IMG_MAX_SIZE ||
          draw->texture_address > V9X_VI_TEX_ADDRESS_MIRROR ||
          draw->texture_last_level > 15ul ||
          draw->texture_base_level > draw->texture_last_level ||
          draw->texture_mip_filter > V9X_VI_MIP_FILTER_LINEAR))) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    v9x_vi_3d_cache_sync(stream);
    v9x_vi_3d_common_state(stream, draw->target_low, draw->target_high,
                           draw->pitch_pixels, draw->height, draw->format,
                           draw->tile_mode_index, VI_TL_PARAMS,
                           draw->target_mask_none != 0ul ? 0ul :
                           (draw->target_mask != 0ul ? draw->target_mask
                                                     : VI_CB_TARGET_MASK_RGBA),
                           draw->d3d_pixel_centres != 0ul ? VI_SU_VTX_CNTL_D3D
                                                          : VI_SU_VTX_CNTL,
                           draw->blend_control,
                           draw->ps_kill != 0ul ? VI_DB_SHADER_KILL_ENABLE
                                                : 0ul,
                           VI_COL_FORMAT_FP16_ABGR);
    if (draw->depth_low != 0ul) {
        v9x_vi_3d_depth(stream, draw->depth_low, draw->depth_high,
                        depth_pitch, depth_height, draw->depth_func,
                        draw->depth_write);
    }

    /* The VS's two user SGPRs are the vertex buffer's address; the PS has
     * the texture's descriptors or nothing (vi_shader.h). */
    v9x_vi_3d_program(stream, VI_SPI_SHADER_PGM_RSRC3_VS, 1, draw->vs_low,
                      draw->shader_high, V9X_VI_TL_VS_VGPRS, 2ul);
    values[0] = draw->vertex_low;
    values[1] = draw->vertex_high;
    v9x_vi_pm4_set_sh(stream, VI_SPI_SHADER_USER_DATA_VS, values, 2ul);
    ps_sgprs = draw->texture_low != 0ul ? V9X_VI_TEXTURE_USER_SGPRS : 0ul;
    v9x_vi_3d_program(stream, VI_SPI_SHADER_PGM_RSRC3_PS, 0, draw->ps_low,
                      draw->shader_high,
                      draw->ps_vgprs != 0ul ? draw->ps_vgprs : VI_SMALL_VGPRS,
                      ps_sgprs);
    if (ps_sgprs != 0ul) {
        v9x_vi_3d_texture_descriptors(draw, values);
        v9x_vi_pm4_set_sh(stream, VI_SPI_SHADER_USER_DATA_PS, values,
                          ps_sgprs);
    }

    v9x_vi_3d_draw_and_fence(stream, VI_DI_PT_TRILIST, draw->vertex_count,
                             draw->fence_low, draw->fence_high,
                             draw->fence_value);
    return stream->overflow == 0ul ? V9X_STATUS_OK : V9X_STATUS_INVALID_ARGUMENT;
}
