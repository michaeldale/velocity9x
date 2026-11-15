/*
 * ATI Radeon 9250 (RV280, an R200-class part) 3D engine register values,
 * written directly over the register BAR with the CP off: phase 5 of
 * docs\plans\ati-rv280-radeon-9250.md. Pure builders, host-tested
 * (tests\host\test_rv280_3d.c); src\chipsets\ati\rv280\rv280_3d.c.
 *
 * Sources: xf86-video-ati 6.14.6 (build\external\rs690\xf86-video-ati-
 * 6.14.6\src\), its ACCEL_MMIO build:
 *   - RADEONInit3DEngineMMIO's R200 branch, radeon_commonfuncs.c:777-803;
 *   - R200PrepareComposite and RadeonCompositeTile, radeon_exa_render.c:
 *     995-1132 and 2455-2509: colour buffer, texture environment, SE_VF_CNTL
 *     without RADEON_MODE, vertex dwords to SE_PORT_DATA0;
 *   - RadeonFinishComposite's WAIT_UNTIL 3D idle-clean, :2213-2216.
 * Offsets and fields: radeon_reg.h there. The order of a vertex's dwords
 * follows the register's bit order, as Linux r200.c r200_get_vtx_size_0
 * counts them (build\external\rv280\): X, Y, then colour 0.
 *
 * Not in those files, and so a hypothesis the draw probe measures:
 *   - diffuse-only output, TXCBLEND_0 = ARG_C diffuse with A = B = 0 and the
 *     stage's output in R0, after the single-argument pass-through at
 *     radeon_render.c:925-930, with no texture unit enabled;
 *   - the byte order of a packed colour (PK_RGBA), with and without
 *     SE_VF_CNTL's COLOR_ORDER_RGBA.
 */
#ifndef VELOCITY9X_RV280_3D_H
#define VELOCITY9X_RV280_3D_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

/* Registers (radeon_reg.h). */
#define V9X_RV280_PP_MISC               0x1c14ul
#define V9X_RV280_PP_FOG_COLOR          0x1c18ul
#define V9X_RV280_RB3D_BLENDCNTL        0x1c20ul
#define V9X_RV280_RB3D_DEPTHOFFSET      0x1c24ul
#define V9X_RV280_RB3D_DEPTHPITCH       0x1c28ul
#define V9X_RV280_RB3D_ZSTENCILCNTL     0x1c2cul
#define V9X_RV280_RB3D_ZCACHE_CTLSTAT   0x3254ul
#define V9X_RV280_PP_CNTL               0x1c38ul
#define V9X_RV280_RB3D_CNTL             0x1c3cul
#define V9X_RV280_RB3D_COLOROFFSET      0x1c40ul
#define V9X_RV280_RE_WIDTH_HEIGHT       0x1c44ul
#define V9X_RV280_RB3D_COLORPITCH       0x1c48ul
#define V9X_RV280_SE_CNTL               0x1c4cul
#define V9X_RV280_RE_CNTL               0x1c50ul
#define V9X_RV280_RB3D_PLANEMASK        0x1d84ul
#define V9X_RV280_WAIT_UNTIL            0x1720ul
#define V9X_RV280_SE_PORT_DATA0         0x2000ul
#define V9X_RV280_SE_VAP_CNTL           0x2080ul
#define V9X_RV280_SE_VF_CNTL            0x2084ul
#define V9X_RV280_SE_VTX_FMT_0          0x2088ul
#define V9X_RV280_SE_VTX_FMT_1          0x208cul
#define V9X_RV280_SE_VTE_CNTL           0x20b0ul
#define V9X_RV280_SE_VAP_CNTL_STATUS    0x2140ul
#define V9X_RV280_SE_VTX_STATE_CNTL     0x2180ul
#define V9X_RV280_RE_TOP_LEFT           0x26c0ul
#define V9X_RV280_RE_AUX_SCISSOR_CNTL   0x26f0ul
#define V9X_RV280_PP_TXMULTI_CTL_0      0x2c1cul
#define V9X_RV280_PP_CNTL_X             0x2cc4ul
#define V9X_RV280_PP_TXFILTER_0         0x2c00ul
#define V9X_RV280_PP_TXFORMAT_0         0x2c04ul
#define V9X_RV280_PP_TXFORMAT_X_0       0x2c08ul
#define V9X_RV280_PP_TXSIZE_0           0x2c0cul
#define V9X_RV280_PP_TXPITCH_0          0x2c10ul
#define V9X_RV280_PP_TXOFFSET_0         0x2d00ul
#define V9X_RV280_PP_TXCBLEND_0         0x2f00ul
#define V9X_RV280_PP_TXCBLEND2_0        0x2f04ul
#define V9X_RV280_PP_TXABLEND_0         0x2f08ul
#define V9X_RV280_PP_TXABLEND2_0        0x2f0cul

/* SE_VAP_CNTL: FORCE_W_TO_ONE 1<<16, VF_MAX_VTX_NUM 9<<18; TCL_ENABLE
 * (bit 0) clear, so the vertices bypass TCL. */
#define V9X_RV280_VAP_CNTL_BYPASS       0x00250000ul
/* SE_CNTL: SOLID front and back faces, OGL pixel centre, ROUND_MODE_ROUND,
 * ROUND_PREC_4TH_PIX; the diffuse shade model in bits 9:8. The init's
 * value is the Gouraud one. */
#define V9X_RV280_SE_CNTL_BASE          0x9800001eul
#define V9X_RV280_SE_DIFFUSE_FLAT       0x00000100ul
#define V9X_RV280_SE_DIFFUSE_GOURAUD    0x00000200ul
/* SE_CNTL's alpha shade model, bits 11:10 (radeon_reg.h:2271-2274). SOLID,
 * the init's value, gave every fragment alpha 0 on M-HNBJNV: blending left
 * the destination as it was and GREATER 128 dropped alpha 200
 * (docs\decisions\2026-11-15-rv280-z-blend-probe.md). A draw shades alpha,
 * specular (13:12), fog (15:14) and discrete fog (25:24) as it shades the
 * colour, as Mesa's r200ShadeModel does (r200_state.c:1306-1336). */
#define V9X_RV280_SE_ALPHA_FLAT         0x00000400ul
#define V9X_RV280_SE_ALPHA_GOURAUD      0x00000800ul
#define V9X_RV280_SE_SHADE_FLAT_REST    0x01005000ul
#define V9X_RV280_SE_SHADE_GOURAUD_REST 0x0200a000ul
/* SE_CNTL's face controls (Mesa r200_reg.h:220-227): FFACE_CULL_CCW bit 0
 * (clear: clockwise triangles are front faces), BFACE_SOLID 2:1 and
 * FFACE_SOLID 4:3, cleared to cull. Which screen winding the hardware calls
 * clockwise, with y down, is measured by V9XR23D's Cull test. */
#define V9X_RV280_SE_FFACE_CULL_CCW     0x00000001ul
#define V9X_RV280_SE_BFACE_SOLID        0x00000006ul
#define V9X_RV280_SE_FFACE_SOLID        0x00000018ul
/* SE_VTE_CNTL's VTX_XY_FMT and VTX_Z_FMT (X, Y and Z already divided by W)
 * with VTX_W0_FMT clear (the W field holds 1/W), and SE_VAP_CNTL without
 * FORCE_W_TO_ONE: Mesa's projected path with W for textures
 * (r200_swtcl.c:259-269), Direct3D's TL vertex as it is (sx, sy, sz,
 * rhw). */
#define V9X_RV280_VTE_XYZ_DIVIDED       0x00000300ul
#define V9X_RV280_VAP_CNTL_KEEP_W       0x00240000ul
/* RE_CNTL: PERSPECTIVE_ENABLE bit 3 (Mesa r200_reg.h:266-270), on in
 * Mesa's initial state (r200_state_init.c:1001). The init's RE_CNTL of 0,
 * the X driver's, drew a texture affine with 1/W in the vertex on M-HNBJNV
 * (docs\decisions\2026-11-15-rv280-cull-rhw-fog-probe.md); every draw
 * sets it. */
#define V9X_RV280_RE_PERSPECTIVE        0x00000008ul
/* PP_CNTL: SPECULAR_ENABLE bit 21, FOG_ENABLE bit 22 (Mesa r200_reg.h:
 * 177-178). */
#define V9X_RV280_PP_SPECULAR_ENABLE    0x00200000ul
#define V9X_RV280_PP_FOG_ENABLE         0x00400000ul
/* PP_FOG_COLOR: the fog RGB in 23:0, FOG_VERTEX (bit 24 clear), the factor
 * from the specular alpha, FOG_USE_SPEC_ALPHA 3<<25: Mesa's swtcl state
 * (r200_state_init.c:920-921, r200_swtcl.c:176-181). */
#define V9X_RV280_FOG_COLOR_MASK        0x00fffffful
#define V9X_RV280_FOG_USE_SPEC_ALPHA    0x06000000ul
/* SE_VTX_FMT_0: W0 bit 1; COLOR_1 packed RGBA (1 << 13). */
#define V9X_RV280_VTX_FMT_W0            0x00000002ul
#define V9X_RV280_VTX_FMT_PK_COLOR1     0x00002000ul
/* RB3D_CNTL COLOR_FORMAT, bits 13:10. */
#define V9X_RV280_COLOR_FORMAT_RGB565   0x00001000ul
#define V9X_RV280_COLOR_FORMAT_ARGB8888 0x00001800ul
/* RB3D_BLENDCNTL: source GL_ONE, destination GL_ZERO (no blending). The
 * factor codes, source at 16 and destination at 24, COMB_FCN_ADD_CLAMP 0
 * (radeon_reg.h:2091-2119): ZERO 32, ONE 33, SRC_COLOR 34,
 * ONE_MINUS_SRC_COLOR 35, DST_COLOR 36, ONE_MINUS_DST_COLOR 37, SRC_ALPHA
 * 38, ONE_MINUS_SRC_ALPHA 39, DST_ALPHA 40, ONE_MINUS_DST_ALPHA 41. */
#define V9X_RV280_BLEND_ONE_ZERO        0x20210000ul
#define V9X_RV280_BLEND_SRC_SHIFT       16u
#define V9X_RV280_BLEND_DST_SHIFT       24u
#define V9X_RV280_BLEND_FACTOR_ZERO     32ul
#define V9X_RV280_BLEND_FACTOR_ONE      33ul
#define V9X_RV280_BLEND_FACTOR_SRC_ALPHA 38ul
#define V9X_RV280_BLEND_FACTOR_INV_SRC_ALPHA 39ul
#define V9X_RV280_BLEND_FACTOR_FIRST    32ul
#define V9X_RV280_BLEND_FACTOR_LAST     41ul
/* RB3D_CNTL: ALPHA_BLEND_ENABLE bit 0, Z_ENABLE bit 8. */
#define V9X_RV280_RB3D_ALPHA_BLEND      0x00000001ul
#define V9X_RV280_RB3D_Z_ENABLE         0x00000100ul
/* RB3D_ZSTENCILCNTL: DEPTH_FORMAT_16BIT_INT_Z 0, Z_TEST at 4 (NEVER 0,
 * LESS 1, LEQUAL 2, EQUAL 3, GEQUAL 4, GREATER 5, NEQUAL 6, ALWAYS 7),
 * Z_WRITE_ENABLE bit 30; stencil left at 0 with STENCIL_ENABLE clear. */
#define V9X_RV280_Z_TEST_SHIFT          4u
#define V9X_RV280_Z_TEST_ALWAYS         7ul
#define V9X_RV280_Z_WRITE_ENABLE        0x40000000ul
/* RB3D_DEPTHPITCH: the pitch in pixels, a multiple of 8, at most 1FF8h. */
#define V9X_RV280_DEPTH_PITCH_MAX       0x1ff8ul
/* RB3D_ZCACHE_CTLSTAT: RB3D_ZC_FLUSH_ALL. */
#define V9X_RV280_ZC_FLUSH_ALL          0x00000005ul
/* PP_MISC: the alpha reference in 7:0, ALPHA_TEST op at 8 (FAIL 0, LESS
 * 1, ... PASS 7); PP_CNTL ALPHA_TEST_ENABLE bit 23 (radeon_reg.h:
 * 1756-1786). */
#define V9X_RV280_ALPHA_TEST_SHIFT      8u
#define V9X_RV280_PP_ALPHA_TEST_ENABLE  0x00800000ul
/* SE_VTX_FMT_0: Z0 bit 0, a Z after X and Y. */
#define V9X_RV280_VTX_FMT_Z0            0x00000001ul
/* PP_CNTL: TEX_BLEND_0_ENABLE, no texture unit. */
#define V9X_RV280_PP_BLEND_0_ONLY       0x00001000ul
/* TXCBLEND_0 / TXABLEND_0: ARG_C = diffuse colour / alpha (selector 4 in
 * bits 14:10), A = B = 0, OP MADD: out = 0 * 0 + diffuse. */
#define V9X_RV280_TXCBLEND_DIFFUSE      0x00001000ul
#define V9X_RV280_TXABLEND_DIFFUSE      0x00001000ul
/* PP_CNTL with texture unit 0 sampling into R0 for blend stage 0. */
#define V9X_RV280_PP_TEX0_BLEND0        0x00001010ul
/* Decal, R200PrepareComposite's no-mask blend: A = R0 (texel), B = ZERO
 * complemented, C = ZERO, MADD: out = texel * 1 + 0. */
#define V9X_RV280_TXCBLEND_TEXEL        0x0010000aul
#define V9X_RV280_TXABLEND_TEXEL        0x0010000aul
/* Modulate: A = R0 (texel), B = diffuse (selector 4 at bit 5), C = ZERO,
 * MADD: out = texel * diffuse. The X driver's mask path, with diffuse for
 * the mask (radeon_exa_render.c:1096-1106). */
#define V9X_RV280_TXCBLEND_MODULATE     0x0000008aul
#define V9X_RV280_TXABLEND_MODULATE     0x0000008aul
/* PP_TXFILTER_0: MAG and MIN LINEAR bits 0 and 1; CLAMP_S at 23, CLAMP_T
 * at 27, WRAP 0, CLAMP_LAST 2 (radeon_reg.h:2639-2684). */
#define V9X_RV280_TXFILTER_LINEAR       0x00000003ul
#define V9X_RV280_TXFILTER_CLAMP_LAST   0x11000000ul
/* PP_TXFORMAT_0: RGB565 4, ARGB8888 6, ALPHA_IN_MAP 1<<6, log2 width at
 * 8, log2 height at 12; ST route 0 (radeon_reg.h:2694-2717). */
#define V9X_RV280_TXFORMAT_RGB565       0x00000004ul
#define V9X_RV280_TXFORMAT_ARGB8888     0x00000006ul
#define V9X_RV280_TXFORMAT_ALPHA_IN_MAP 0x00000040ul
#define V9X_RV280_TXFORMAT_WIDTH_SHIFT  8u
#define V9X_RV280_TXFORMAT_HEIGHT_SHIFT 12u
/* PP_TXOFFSET_0's alignment (R200_TXO_OFFSET_MASK FFFFFFE0h), the pitch's
 * (R200TextureSetup: a multiple of 32 bytes, written less 32) and the
 * largest side, 2048 (log2 11 in a 4-bit field). */
#define V9X_RV280_TEX_ALIGN             32ul
#define V9X_RV280_TEX_MAX_SIDE          2048ul
/* SE_VTX_FMT_1: texture 0 has two components (S, T). */
#define V9X_RV280_VTX_FMT_1_TEX0_ST     0x00000002ul

/* How blend stage 0 combines texel and diffuse colour. */
#define V9X_RV280_COMBINE_DECAL         0u
#define V9X_RV280_COMBINE_MODULATE      1u

/* TXCBLEND2_0 / TXABLEND2_0: CLAMP_0_1, OUTPUT_REG_R0. */
#define V9X_RV280_TXBLEND2_TO_R0        0x00011000ul
/* SE_VTX_FMT_0: XY always; COLOR_0 packed RGBA (1 << 11). */
#define V9X_RV280_VTX_FMT_PK_COLOR0     0x00000800ul
/* SE_VF_CNTL: TRIANGLE_LIST 4, WALK_DATA 3<<4, COLOR_ORDER_RGBA 1<<6,
 * vertex count from bit 16. No RADEON_MODE on R200. */
#define V9X_RV280_VF_TRIANGLE_LIST      0x00000004ul
#define V9X_RV280_VF_WALK_DATA          0x00000030ul
#define V9X_RV280_VF_COLOR_ORDER_RGBA   0x00000040ul
#define V9X_RV280_VF_NUM_SHIFT          16u
/* WAIT_UNTIL: WAIT_3D_IDLECLEAN. */
#define V9X_RV280_WAIT_3D_IDLECLEAN     0x00020000ul

/* RB3D_COLOROFFSET's alignment, RB3D_COLORPITCH's unit and field. */
#define V9X_RV280_COLOR_OFFSET_ALIGN    16ul
#define V9X_RV280_COLOR_PITCH_UNIT      8ul
#define V9X_RV280_COLOR_PITCH_MAX       0x1ff8ul
/* RE_WIDTH_HEIGHT holds width and height in 16 bits each; X uses up to
 * 2047. */
#define V9X_RV280_TARGET_MAX            2047ul

/* Vertices per SE_VF_CNTL in one triangle list here. A vertex is X, Y,
 * then Z with V9X_RV280_LAYOUT_Z, W (1/W) with V9X_RV280_LAYOUT_W, the
 * packed colour, the packed specular colour with V9X_RV280_LAYOUT_SPECULAR,
 * then S, T with V9X_RV280_LAYOUT_ST: the fields in SE_VTX_FMT_0's then
 * SE_VTX_FMT_1's bit order (r200.c r200_get_vtx_size_0; Mesa's swtcl emits
 * them so, r200_swtcl.c:100-170). */
#define V9X_RV280_3D_MAX_VERTICES       12u
#define V9X_RV280_LAYOUT_Z              1u
#define V9X_RV280_LAYOUT_ST             2u
#define V9X_RV280_LAYOUT_W              4u
#define V9X_RV280_LAYOUT_SPECULAR       8u

/* The longest list: a state list, or 1 + 12 vertices of 8 dwords. The
 * caller writes it in FIFO-sized batches. */
#define V9X_RV280_3D_LIST_MAX           104u

struct v9x_rv280_3d_list {
    v9x_u32 offsets[V9X_RV280_3D_LIST_MAX];
    v9x_u32 values[V9X_RV280_3D_LIST_MAX];
    v9x_u32 count;
};

/* Depth, blending and the alpha test, in the hardware's own codes. A zero
 * pointer in the target means none of them: no Z, blending off, no alpha
 * test. */
struct v9x_rv280_3d_raster {
    /* A 16-bit Z buffer in the target's frame buffer. */
    v9x_u32 z_enable;
    v9x_u32 z_offset;
    v9x_u32 z_pitch_bytes;
    /* Z_TEST, 0 (NEVER) to 7 (ALWAYS), and whether passing writes Z. */
    v9x_u32 z_func;
    v9x_u32 z_write;
    /* RB3D_BLENDCNTL factor codes, V9X_RV280_BLEND_FACTOR_FIRST..LAST. */
    v9x_u32 blend_enable;
    v9x_u32 blend_src;
    v9x_u32 blend_dst;
    /* ALPHA_TEST op, 0 (FAIL) to 7 (PASS), and the 8-bit reference. */
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    /* SE_CNTL's faces: which winding is front (non-zero: CCW), and which
     * faces are culled. */
    v9x_u32 front_ccw;
    v9x_u32 cull_front;
    v9x_u32 cull_back;
    /* Vertex fog from the specular alpha, toward fog_colour (RGB). */
    v9x_u32 fog_enable;
    v9x_u32 fog_colour;
    /* The specular colour added to the fragment colour. */
    v9x_u32 specular_enable;
};

/* The colour buffer: a surface in the frame buffer at a GPU address, its
 * raster state (may be zero), and the vertex layout the draws will send:
 * V9X_RV280_LAYOUT_Z, _W and _SPECULAR (S and T follow from the textured
 * builder). Z is required with a Z buffer, the specular colour with fog or
 * specular. */
struct v9x_rv280_3d_target {
    v9x_u32 fb_gpu_base;
    v9x_u32 fb_bytes;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 width;
    v9x_u32 height;
    const struct v9x_rv280_3d_raster *raster;
    v9x_u32 layout;
};

/* A screen-space vertex: x, y and z as IEEE single bits, w as 1/W in IEEE
 * single bits (D3D's rhw), a packed colour and a packed specular colour sent
 * as they are (D3DCOLORs, measured: docs\decisions\2026-11-15-rv280-3d-
 * draw-probe.md; the specular alpha is the fog factor), and normalised
 * texture coordinates as IEEE single bits. Each optional field is sent only
 * with its V9X_RV280_LAYOUT_* bit. */
struct v9x_rv280_3d_vertex {
    v9x_u32 x_bits;
    v9x_u32 y_bits;
    v9x_u32 z_bits;
    v9x_u32 w_bits;
    v9x_u32 colour;
    v9x_u32 specular;
    v9x_u32 s_bits;
    v9x_u32 t_bits;
};

/* Texture unit 0's source: a power-of-two surface with rows packed, in the
 * frame buffer at a GPU address. */
struct v9x_rv280_3d_texture {
    v9x_u32 fb_gpu_base;
    v9x_u32 fb_bytes;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 width;
    v9x_u32 height;
    /* Non-zero for bilinear (MAG and MIN LINEAR), else nearest. */
    v9x_u32 linear;
    /* Non-zero to clamp S and T to the last texel, else wrap. */
    v9x_u32 clamp;
};

/* RB3D_CNTL = 0 (RADEONEngineInit), then RADEONInit3DEngine's R200 branch:
 * fourteen-or-fewer writes, once per mode on an idle engine. */
v9x_status v9x_rv280_build_3d_init(struct v9x_rv280_3d_list *list);

/* The colour buffer, its raster state, the diffuse-only texture
 * environment and the shade model: V9X_STATUS_UNSUPPORTED for a depth the
 * colour buffer has no format for here (only 16 and 32 bpp),
 * INVALID_ARGUMENT for a surface or Z buffer outside the frame buffer,
 * misaligned, or too large, or a raster code out of range. With Z enabled
 * the vertex carries Z (SE_VTX_FMT_0 Z0): send it with
 * V9X_RV280_LAYOUT_Z. */
v9x_status v9x_rv280_build_3d_target(const struct v9x_rv280_3d_target *target,
                                     int gouraud,
                                     struct v9x_rv280_3d_list *list);

/* The colour buffer and shade model as v9x_rv280_build_3d_target, with
 * texture unit 0 sampled into blend stage 0 and combined as combine says
 * (V9X_RV280_COMBINE_*), and S, T in the vertex: R200TextureSetup's unit 0
 * for a power-of-two texture. INVALID_ARGUMENT for a texture that is not a
 * power of two on a side, larger than 2048, not 32-byte aligned, not packed
 * at a 32-byte multiple pitch, or outside the frame buffer; UNSUPPORTED
 * for a depth other than 16 or 32 bpp or an unknown combine. */
v9x_status v9x_rv280_build_3d_textured_target(
    const struct v9x_rv280_3d_target *target, int gouraud,
    const struct v9x_rv280_3d_texture *texture, v9x_u32 combine,
    struct v9x_rv280_3d_list *list);

/* A triangle list: SE_VF_CNTL then each vertex's dwords to SE_PORT_DATA0
 * in the layout V9X_RV280_LAYOUT_* names. count a multiple of 3,
 * 3..V9X_RV280_3D_MAX_VERTICES. */
v9x_status v9x_rv280_build_3d_triangles(
    const struct v9x_rv280_3d_vertex *vertices, v9x_u32 count,
    int colour_order_rgba, v9x_u32 layout, struct v9x_rv280_3d_list *list);

/* WAIT_UNTIL 3D idle-clean, as RadeonFinishComposite ends a draw; with
 * flush_z, the Z cache flushed first (RB3D_ZCACHE_CTLSTAT ZC_FLUSH_ALL),
 * so the Z buffer can be read back. */
v9x_status v9x_rv280_build_3d_finish(int flush_z,
                                     struct v9x_rv280_3d_list *list);

#endif
