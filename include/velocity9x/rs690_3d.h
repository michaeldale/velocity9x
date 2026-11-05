/*
 * ATI Radeon X1200 IGP (RS690) 3D engine: register values for drawing
 * colour-interpolated triangles by direct register writes, without the CP.
 *
 * Pure policy, no I/O, no floating point: vertex data is built as IEEE
 * single-precision bit patterns by integer arithmetic, so the same code
 * serves a host test, the Win32 probe and, later, the HAL. The caller writes
 * what these builders return, every dword in order, to the register BAR
 * (BAR2) + offset, with the CP's queues off as VBE leaves them
 * (docs\decisions\2026-11-04-rs690-register-probe.md), and waits for free
 * FIFO entries before each run of writes as the 2D engine's users do.
 *
 * Sources, none of them an AMD register guide:
 *   - The engine init is xf86-video-ati 6.14.6 RADEONInit3DEngine
 *     (radeon_commonfuncs.c:55-776), the IS_R300_3D branch without a CS,
 *     for a chip at or past CHIP_FAMILY_R420 (RS690 is) and without TCL
 *     (radeon_driver.c:2178-2186 gives RS690 has_tcl = FALSE).
 *   - The vertex stream, the colour buffer, the scissor and the draw
 *     follow the same driver's R300PrepareComposite and the MMIO branch of
 *     RadeonCompositeTile (radeon_exa_render.c:1501-2172, 2455-2503).
 *   - Texture unit 0, the texture coordinate stream and the shader's
 *     texture load follow R300TextureSetup and R300PrepareComposite
 *     (radeon_exa_render.c:1194-1400, 1559-1884), formats from its
 *     R300TexFormats table.
 *   - The X driver only ever draws textured quads. Routing a per-vertex
 *     colour through the rasterizer to the fragment shader is Mesa's r300
 *     software-TCL path (Mesa 22.3 r300_state_derived.c:304-638,
 *     r300_update_rs_block), and the one-instruction shader is the
 *     "MAD o0, r0, 1, 0" its register header describes (r300_reg.h:1823).
 * Offsets and fields are Mesa's r300_reg.h, cross-checked against
 * xf86-video-ati's radeon_reg.h where both define them.
 */
#ifndef VELOCITY9X_RS690_3D_H
#define VELOCITY9X_RS690_3D_H

#include "velocity9x/status.h"

/* Engine sync and cache control. */
#define V9X_RS690_WAIT_UNTIL            0x1720ul
#define V9X_RS690_DST_PIPE_CONFIG       0x170cul
#define V9X_RS690_GB_PIPE_SELECT        0x402cul
#define V9X_RS690_RB3D_DSTCACHE_CTLSTAT 0x4e4cul
#define V9X_RS690_RB2D_DSTCACHE_MODE    0x3428ul
#define V9X_RS690_SU_REG_DEST           0x42c8ul
#define V9X_RS690_ZB_ZCACHE_CTLSTAT     0x4f18ul

/* WAIT_UNTIL: hold the FIFO until the engine is idle and clean. */
#define V9X_RS690_WAIT_2D_IDLECLEAN     0x00010000ul
#define V9X_RS690_WAIT_3D_IDLECLEAN     0x00020000ul
/* R300_DC_FLUSH_3D | R300_DC_FREE_3D; R300_ZC_FLUSH | R300_ZC_FREE. */
#define V9X_RS690_RB3D_DC_FLUSH_ALL     0x0000000aul
#define V9X_RS690_ZC_FLUSH_ALL          0x00000003ul
#define V9X_RS690_PIPE_AUTO_CONFIG      0x80000000ul

/* Vertex fetch and the vertex assembler (VAP). */
#define V9X_RS690_SE_PORT_DATA0         0x2000ul
#define V9X_RS690_VAP_CNTL              0x2080ul
#define V9X_RS690_VAP_VF_CNTL           0x2084ul
#define V9X_RS690_VAP_OUTPUT_VTX_FMT_0  0x2090ul
#define V9X_RS690_VAP_OUTPUT_VTX_FMT_1  0x2094ul
#define V9X_RS690_VAP_VTE_CNTL          0x20b0ul
#define V9X_RS690_VAP_VTX_SIZE          0x20b4ul
#define V9X_RS690_VAP_CNTL_STATUS       0x2140ul
#define V9X_RS690_VAP_PROG_STREAM_CNTL_0 0x2150ul
#define V9X_RS690_VAP_PROG_STREAM_CNTL_1 0x2154ul
#define V9X_RS690_VAP_VTX_STATE_CNTL    0x2180ul
#define V9X_RS690_VAP_VSM_VTX_ASSM      0x2184ul
#define V9X_RS690_VAP_PSC_SGN_NORM_CNTL 0x21dcul
#define V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_0 0x21e0ul
#define V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_1 0x21e4ul
#define V9X_RS690_VAP_PVS_STATE_FLUSH_REG 0x2284ul

/* Graphics block, setup unit, rasterizer, scan converter. */
#define V9X_RS690_GB_ENABLE             0x4008ul
#define V9X_RS690_GB_MSPOS0             0x4010ul
#define V9X_RS690_GB_MSPOS1             0x4014ul
#define V9X_RS690_GB_TILE_CONFIG        0x4018ul
#define V9X_RS690_GB_SELECT             0x401cul
#define V9X_RS690_GB_AA_CONFIG          0x4020ul
#define V9X_RS690_TX_INVALTAGS          0x4100ul
#define V9X_RS690_TX_ENABLE             0x4104ul
#define V9X_RS690_GA_ENHANCE            0x4274ul
#define V9X_RS690_GA_COLOR_CONTROL      0x4278ul
#define V9X_RS690_GA_POLY_MODE          0x4288ul
#define V9X_RS690_GA_ROUND_MODE         0x428cul
#define V9X_RS690_GA_OFFSET             0x4290ul
#define V9X_RS690_SU_TEX_WRAP           0x42a0ul
#define V9X_RS690_SU_POLY_OFFSET_ENABLE 0x42b4ul
#define V9X_RS690_SU_CULL_MODE          0x42b8ul
#define V9X_RS690_SU_DEPTH_SCALE        0x42c0ul
#define V9X_RS690_SU_DEPTH_OFFSET       0x42c4ul
#define V9X_RS690_RS_COUNT              0x4300ul
#define V9X_RS690_RS_INST_COUNT         0x4304ul
#define V9X_RS690_RS_IP_0               0x4310ul
#define V9X_RS690_RS_IP_1               0x4314ul
#define V9X_RS690_RS_INST_0             0x4330ul
#define V9X_RS690_RS_INST_1             0x4334ul
#define V9X_RS690_SC_EDGERULE           0x43a8ul
#define V9X_RS690_SC_CLIP_0_A           0x43b0ul
#define V9X_RS690_SC_CLIP_0_B           0x43b4ul
#define V9X_RS690_SC_CLIP_RULE          0x43d0ul
#define V9X_RS690_SC_SCISSOR0           0x43e0ul
#define V9X_RS690_SC_SCISSOR1           0x43e4ul
#define V9X_RS690_SC_SCREENDOOR         0x43e8ul

/* Texture unit 0. */
#define V9X_RS690_TX_FILTER0_0          0x4400ul
#define V9X_RS690_TX_FILTER1_0          0x4440ul
#define V9X_RS690_TX_FORMAT0_0          0x4480ul
#define V9X_RS690_TX_FORMAT1_0          0x44c0ul
#define V9X_RS690_TX_FORMAT2_0          0x4500ul
#define V9X_RS690_TX_OFFSET_0           0x4540ul
#define V9X_RS690_TX_BORDER_COLOR_0     0x45c0ul

/* The fragment shader (US). */
#define V9X_RS690_US_CONFIG             0x4600ul
#define V9X_RS690_US_PIXSIZE            0x4604ul
#define V9X_RS690_US_CODE_OFFSET        0x4608ul
#define V9X_RS690_US_CODE_ADDR_0        0x4610ul
#define V9X_RS690_US_CODE_ADDR_1        0x4614ul
#define V9X_RS690_US_CODE_ADDR_2        0x4618ul
#define V9X_RS690_US_CODE_ADDR_3        0x461cul
#define V9X_RS690_US_TEX_INST_0         0x4620ul
#define V9X_RS690_US_OUT_FMT_0          0x46a4ul
#define V9X_RS690_US_OUT_FMT_1          0x46a8ul
#define V9X_RS690_US_OUT_FMT_2          0x46acul
#define V9X_RS690_US_OUT_FMT_3          0x46b0ul
#define V9X_RS690_US_W_FMT              0x46b4ul
#define V9X_RS690_US_ALU_RGB_ADDR_0     0x46c0ul
#define V9X_RS690_US_ALU_ALPHA_ADDR_0   0x47c0ul
#define V9X_RS690_US_ALU_RGB_INST_0     0x48c0ul
#define V9X_RS690_US_ALU_ALPHA_INST_0   0x49c0ul
#define V9X_RS690_US_ALU_RGB_ADDR_1     0x46c4ul
#define V9X_RS690_US_ALU_ALPHA_ADDR_1   0x47c4ul
#define V9X_RS690_US_ALU_RGB_INST_1     0x48c4ul
#define V9X_RS690_US_ALU_ALPHA_INST_1   0x49c4ul
#define V9X_RS690_US_ALU_RGB_ADDR_2     0x46c8ul
#define V9X_RS690_US_ALU_ALPHA_ADDR_2   0x47c8ul
#define V9X_RS690_US_ALU_RGB_INST_2     0x48c8ul
#define V9X_RS690_US_ALU_ALPHA_INST_2   0x49c8ul
/* Shader constant 0, R300 float24 per channel (radeon_reg.h:5053). */
#define V9X_RS690_US_ALU_CONST_R_0      0x4c00ul
#define V9X_RS690_US_ALU_CONST_G_0      0x4c04ul
#define V9X_RS690_US_ALU_CONST_B_0      0x4c08ul
#define V9X_RS690_US_ALU_CONST_A_0      0x4c0cul

/* Fog, alpha test, the render backend and the Z buffer. */
#define V9X_RS690_FG_FOG_BLEND          0x4bc0ul
#define V9X_RS690_FG_ALPHA_FUNC         0x4bd4ul
#define V9X_RS690_FG_DEPTH_SRC          0x4bd8ul
#define V9X_RS690_RB3D_CCTL             0x4e00ul
#define V9X_RS690_RB3D_CBLEND           0x4e04ul
#define V9X_RS690_RB3D_ABLEND           0x4e08ul
#define V9X_RS690_RB3D_COLOR_CHANNEL_MASK 0x4e0cul
#define V9X_RS690_RB3D_ROPCNTL          0x4e18ul
#define V9X_RS690_RB3D_COLOROFFSET0     0x4e28ul
#define V9X_RS690_RB3D_COLORPITCH0      0x4e38ul
#define V9X_RS690_RB3D_DITHER_CTL       0x4e50ul
#define V9X_RS690_RB3D_AARESOLVE_CTL    0x4e88ul
#define V9X_RS690_ZB_CNTL               0x4f00ul
#define V9X_RS690_ZB_ZSTENCILCNTL       0x4f04ul
#define V9X_RS690_ZB_FORMAT             0x4f10ul
#define V9X_RS690_ZB_ZTOP               0x4f14ul
#define V9X_RS690_RB3D_BW_CNTL          0x4f1cul
#define V9X_RS690_ZB_DEPTHOFFSET        0x4f20ul
#define V9X_RS690_ZB_DEPTHPITCH         0x4f24ul

/* VAP_VF_CNTL: primitive type in 3:0, vertices embedded in the data
 * written after it (PRIM_WALK 3 << 4), vertex count from bit 16. */
#define V9X_RS690_PRIM_TRIANGLES        4ul
#define V9X_RS690_PRIM_QUADS            13ul
#define V9X_RS690_VF_PRIM_WALK_DATA     0x00000030ul
#define V9X_RS690_VF_NUM_VERTICES_SHIFT 16
#define V9X_RS690_VF_NUM_VERTICES_MAX   0xfffful

/* RB3D_COLORPITCH0: pitch in pixels in 13:1, the format in 24:21. */
#define V9X_RS690_COLORPITCH_MASK       0x00003ffeul
#define V9X_RS690_COLOR_FORMAT_RGB565   0x00800000ul /* 4 << 21 */
#define V9X_RS690_COLOR_FORMAT_ARGB8888 0x00c00000ul /* 6 << 21 */
/* R300PrepareComposite refuses a destination pitch not a multiple of 8
 * pixels, and a colour offset that is not 16-byte aligned. */
#define V9X_RS690_COLOR_PITCH_ALIGN     8ul
#define V9X_RS690_COLOR_OFFSET_ALIGN    16ul

/* The R300 scan converter's coordinates carry a 1440-pixel guard offset
 * (radeon_commonfuncs.c:763, radeon_exa_render.c:2141); 13-bit fields. */
#define V9X_RS690_SC_OFFSET             1440ul
#define V9X_RS690_SC_Y_SHIFT            13
#define V9X_RS690_SC_CLIP_MAX           4080ul

/* A vertex as these builders send it: x, y as FLOAT_2, then r, g, b, a as
 * FLOAT_4 - six dwords (VAP_VTX_SIZE). A textured vertex adds s, t as a
 * second FLOAT_2. */
#define V9X_RS690_VERTEX_DWORDS         6ul
#define V9X_RS690_TEX_VERTEX_DWORDS     8ul

/* Texture formats the builder takes; RGB565 and ARGB8888 are the same
 * layouts as the colour buffer formats of those names. ARGB1555 and
 * ARGB4444 are R300TexFormats' a1r5g5b5 and Mesa's B4G4R4A4 encodings,
 * not yet sampled on the chip. */
#define V9X_RS690_TEXFMT_RGB565         1ul
#define V9X_RS690_TEXFMT_ARGB8888       2ul
#define V9X_RS690_TEXFMT_ARGB1555       3ul
#define V9X_RS690_TEXFMT_ARGB4444       4ul

/* Texture filter, addressing (both axes) and how the texel meets the
 * interpolated colour: REPLACE takes the texel, MODULATE multiplies the
 * two per channel (Direct3D's DECAL and MODULATE for an opaque texture). */
#define V9X_RS690_TEXFILTER_NEAREST     0ul
#define V9X_RS690_TEXFILTER_LINEAR      1ul
#define V9X_RS690_TEXADDR_WRAP          0ul
#define V9X_RS690_TEXADDR_CLAMP         1ul
#define V9X_RS690_TEXCOMBINE_REPLACE    0ul
#define V9X_RS690_TEXCOMBINE_MODULATE   1ul
/* Direct3D's: texel + diffuse; and the diffuse blended toward the texel by
 * the texel's alpha. TL state only (v9x_rs690_build_3d_state builds the
 * longer shader they need); the texture builder alone refuses them. */
#define V9X_RS690_TEXCOMBINE_ADD        2ul
#define V9X_RS690_TEXCOMBINE_DECALALPHA 3ul

/* Texture limits: R300 width and height fields are 11 bits (texels - 1);
 * offsets 32-byte aligned (R300TextureSetup's CHECK_OFFSET). Pitches 64
 * bytes: the RS690 rounds a linear texture's row up to 64 bytes whatever
 * TX_FORMAT2 says - an 8-texel ARGB1555 texture at a 32-byte pitch was
 * sampled from rows 64 bytes apart on M-GB8KRE (2026-11-05,
 * docs\decisions\2026-11-05-rs690-d3d-hal-first-boot.md) - and Mesa's
 * r300_get_pixel_alignment gives the RS690 alone 64 / bytes-per-texel. */
#define V9X_RS690_TEX_SIZE_MAX          2048ul
#define V9X_RS690_TEX_ALIGN             32ul
#define V9X_RS690_TEX_PITCH_ALIGN       64ul

/* The largest surface the builders accept: the scan converter's clip
 * rectangle less its guard offset. */
#define V9X_RS690_SURFACE_MAX           (V9X_RS690_SC_CLIP_MAX - V9X_RS690_SC_OFFSET)

/* Capacity of one register list: the init writes 66 registers, the
 * colour-buffer state 33, the texture state 31, the TL state more than
 * 100 when textured with specular and fog (the host test holds it under
 * this), a draw 1 + 6 or 8 per vertex up to 12 vertices. */
#define V9X_RS690_3D_VERTICES_MAX       12u
#define V9X_RS690_3D_WRITES             128u

struct v9x_rs690_3d_list {
    v9x_u32 offsets[V9X_RS690_3D_WRITES];
    v9x_u32 values[V9X_RS690_3D_WRITES];
    v9x_u32 count;
};

/* One vertex: pixel coordinates on the colour buffer, colour 0-255 per
 * channel. Sent as x, y and c / 255, each rounded to the nearest single. */
struct v9x_rs690_3d_vertex {
    v9x_u32 x;
    v9x_u32 y;
    v9x_u32 red;
    v9x_u32 green;
    v9x_u32 blue;
    v9x_u32 alpha;
};

/* A textured vertex: as above, plus texture coordinates as IEEE single bit
 * patterns, normalized (0 to 1 spans the texture), sent unchanged. */
struct v9x_rs690_3d_tex_vertex {
    v9x_u32 x;
    v9x_u32 y;
    v9x_u32 red;
    v9x_u32 green;
    v9x_u32 blue;
    v9x_u32 alpha;
    v9x_u32 s;
    v9x_u32 t;
};

/* A texture: one level, linear (untiled), at an offset into the frame
 * buffer. */
struct v9x_rs690_3d_texture {
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 format;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 filter;
    v9x_u32 address;
    v9x_u32 combine;
};

/* The colour buffer: a surface at an offset into the frame buffer. */
struct v9x_rs690_3d_target {
    v9x_u32 fb_gpu_base;
    v9x_u32 fb_bytes;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 width;
    v9x_u32 height;
};

/*
 * RADEONInit3DEngine's writes, once, with the engine idle, plus what Linux
 * r420_pipes_init (r420.c:90-160, run for the RS690 by rs690_gpu_init)
 * writes and the X driver does not: GA_ENHANCE's TCL deadlock bits,
 * SU_REG_DEST to the active pipes, and the 2D destination cache in
 * RB2D_DSTCACHE_MODE autoflushed with DC_DISABLE_IGNORE_PE. Taken while
 * hunting colour-buffer lines a second Direct3D process wrote into system
 * RAM (docs\issues\2026-11-05-rs690-t15-heap-words-zeroed-once.md). pipe_select
 * is GB_PIPE_SELECT as read: bits 13:12 hold the pipe count less one,
 * which picks GB_TILE_CONFIG's pipe field (radeon_accel.c:470-486).
 * rb2d_dstcache_mode is RB2D_DSTCACHE_MODE as read; the two bits are ORed
 * into it, as Linux does.
 */
v9x_status v9x_rs690_build_3d_init(v9x_u32 pipe_select,
                                   v9x_u32 rb2d_dstcache_mode,
                                   struct v9x_rs690_3d_list *list);

/*
 * The state for colour-interpolated drawing into one colour buffer: the
 * vertex stream (position, colour), the rasterizer routing colour 0 into
 * fragment shader temp 0, the one-instruction shader writing it out,
 * blending, Z, the alpha test and culling off, and the scissor opened to
 * the surface. 16 and 32 bpp only.
 */
v9x_status v9x_rs690_build_3d_target(const struct v9x_rs690_3d_target *target,
                                     struct v9x_rs690_3d_list *list);

/*
 * A primitive (V9X_RS690_PRIM_TRIANGLES or _QUADS) of count vertices: the
 * VF_CNTL write, then every vertex's six dwords to SE_PORT_DATA0. Refused
 * unless count is a whole number of primitives, every coordinate lies
 * inside the surface (0 to width, 0 to height, inclusive) and every colour
 * channel is at most 255.
 */
v9x_status v9x_rs690_build_3d_draw(v9x_u32 primitive,
                                   const struct v9x_rs690_3d_vertex *vertices,
                                   v9x_u32 count, v9x_u32 width,
                                   v9x_u32 height,
                                   struct v9x_rs690_3d_list *list);

/*
 * Texture unit 0 and the routing that uses it, written after
 * v9x_rs690_build_3d_target (whose stream, rasterizer and shader writes it
 * replaces; writing the target state again turns texturing off). The
 * vertex gains s, t (FLOAT_2 to vector slot 6, texture 0 in PVS bypass);
 * the rasterizer interpolates them into shader temp 0 and the colour into
 * temp 1; the shader is one texture load (temp 0 from temp 0) and one ALU
 * instruction, REPLACE or MODULATE. Unit setup is R300TextureSetup's, the
 * routing R300PrepareComposite's plus the colour as in the target state.
 * Refused unless the texture is a supported format, 1 to 2048 texels each
 * way, its offset and pitch 32-byte aligned, its pitch at least a row,
 * and all of it inside the frame buffer.
 */
v9x_status v9x_rs690_build_3d_texture(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                      const struct v9x_rs690_3d_texture *texture,
                                      struct v9x_rs690_3d_list *list);

/* As v9x_rs690_build_3d_draw, for textured vertices: eight dwords each. */
v9x_status v9x_rs690_build_3d_draw_textured(
    v9x_u32 primitive, const struct v9x_rs690_3d_tex_vertex *vertices,
    v9x_u32 count, v9x_u32 width, v9x_u32 height,
    struct v9x_rs690_3d_list *list);

/*
 * The Direct3D-shaped draw state: every vertex is a D3DTLVERTEX taken as it
 * is - sx, sy, sz, rhw (FLOAT_4 into vector slot 0; rhw is 1/W, VTE's W0
 * format clear), the diffuse and specular colours as normalized bytes in
 * BGRA order (into colour slots 0 and 1), tu, tv (FLOAT_2 into slot 6) -
 * eight dwords. Untextured draws carry tu, tv anyway, unread.
 */
#define V9X_RS690_TL_VERTEX_DWORDS      8ul

/* Z buffer compare, ZB_ZSTENCILCNTL's encoding. */
#define V9X_RS690_ZFUNC_NEVER           0ul
#define V9X_RS690_ZFUNC_LESS            1ul
#define V9X_RS690_ZFUNC_LEQUAL          2ul
#define V9X_RS690_ZFUNC_EQUAL           3ul
#define V9X_RS690_ZFUNC_GEQUAL          4ul
#define V9X_RS690_ZFUNC_GREATER         5ul
#define V9X_RS690_ZFUNC_NOTEQUAL        6ul
#define V9X_RS690_ZFUNC_ALWAYS          7ul

/* Alpha test compare, FG_ALPHA_FUNC's encoding (Direct3D's D3DCMP - 1). */
#define V9X_RS690_AFUNC_NEVER           0ul
#define V9X_RS690_AFUNC_LESS            1ul
#define V9X_RS690_AFUNC_EQUAL           2ul
#define V9X_RS690_AFUNC_LEQUAL          3ul
#define V9X_RS690_AFUNC_GREATER         4ul
#define V9X_RS690_AFUNC_NOTEQUAL        5ul
#define V9X_RS690_AFUNC_GEQUAL          6ul
#define V9X_RS690_AFUNC_ALWAYS          7ul

/* Blend factors, RB3D_CBLEND's GL encoding. */
#define V9X_RS690_BLEND_ZERO            32ul
#define V9X_RS690_BLEND_ONE             33ul
#define V9X_RS690_BLEND_SRC_COLOR       34ul
#define V9X_RS690_BLEND_INV_SRC_COLOR   35ul
#define V9X_RS690_BLEND_DST_COLOR       36ul
#define V9X_RS690_BLEND_INV_DST_COLOR   37ul
#define V9X_RS690_BLEND_SRC_ALPHA       38ul
#define V9X_RS690_BLEND_INV_SRC_ALPHA   39ul
#define V9X_RS690_BLEND_DST_ALPHA       40ul
#define V9X_RS690_BLEND_INV_DST_ALPHA   41ul
#define V9X_RS690_BLEND_SRC_ALPHA_SAT   42ul

/* SU_CULL_MODE's two cull bits, with the front face counter-clockwise
 * (FACE_NEG) as the init sets it. Which screen winding each removes is a
 * measurement (docs\decisions\2026-11-05-rs690-z-blend-probe.md). */
#define V9X_RS690_CULL_NONE             0ul
#define V9X_RS690_CULL_FRONT            1ul
#define V9X_RS690_CULL_BACK             2ul

/* What the shader puts in the fragment's alpha when textured: the
 * texture's combine (TEXEL for REPLACE, MODULATE for MODULATE), or one
 * named here. */
#define V9X_RS690_TEXALPHA_SAME         0ul
#define V9X_RS690_TEXALPHA_TEXEL        1ul
#define V9X_RS690_TEXALPHA_DIFFUSE      2ul
#define V9X_RS690_TEXALPHA_MODULATE     3ul

/* R300 float24, the shader constants' format (xf86-video-ati 6.14.6
 * F_TO_24): sign in 23, exponent biased 63 in 22:16, the top 16 mantissa
 * bits in 15:0; 0 for zero. From an IEEE single's bit pattern, for values
 * in the constant's range (|x| between 2^-63 and 2^64). */
v9x_u32 v9x_rs690_float24(v9x_u32 ieee);

/* Z16 only: the pitch in pixels a multiple of 32 (DEPTHPITCH's field is in
 * 13:2; 32 is Mesa's RS690 linear alignment for 16 bits, the 64 bytes the
 * texture unit measured to need), the offset 32-byte aligned. */
#define V9X_RS690_DEPTH_PITCH_ALIGN     32ul
#define V9X_RS690_DEPTH_OFFSET_ALIGN    32ul

struct v9x_rs690_3d_state {
    struct v9x_rs690_3d_target target;
    v9x_u32 textured;
    struct v9x_rs690_3d_texture texture;
    v9x_u32 texture_alpha;
    v9x_u32 depth_enable;
    v9x_u32 depth_offset;
    v9x_u32 depth_pitch_bytes;
    v9x_u32 depth_func;
    v9x_u32 depth_write;
    v9x_u32 blend_enable;
    v9x_u32 blend_src;
    v9x_u32 blend_dst;
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    v9x_u32 cull;
    /* Flat shading: every colour from the first vertex (GA_COLOR_CONTROL
     * flat, provoking vertex first, Direct3D's convention). */
    v9x_u32 flat;
    /* Add the specular colour to the result's RGB, alpha untouched
     * (Direct3D's SPECULARENABLE): colour 1 interpolated into the next
     * free temp and taken as the ALU's third operand. */
    v9x_u32 specular;
    /* The target's top byte is X, not alpha: RB3D_COLOR_CHANNEL_MASK
     * leaves it as it was (Direct3D's XRGB8888, which a clear leaves 0);
     * zero writes alpha into it. No effect at 16 bpp. */
    v9x_u32 x_channel;
    /* Vertex fog, Direct3D's: RGB = f * RGB + (1 - f) * fog_color, f the
     * interpolated specular alpha (1 is no fog), alpha untouched, after
     * the texture combine and the specular add. fog_color is 00RRGGBBh.
     * Two more ALU instructions with shader constant 0, and one more
     * temp. */
    v9x_u32 fog;
    v9x_u32 fog_color;
};

/*
 * The whole state for TL-vertex drawing, after the init: the vertex
 * stream, the colour buffer and scissor, texturing on or off, Z16, blend,
 * alpha test and cull. The texture's offset is into the same frame buffer
 * as the target's (target.fb_gpu_base, target.fb_bytes), and so is the Z
 * buffer, whose rows are the target's height. Refused as the target and
 * texture builders refuse, and for an unknown Z or alpha function, blend
 * factor, cull, or texture alpha; a misaligned Z offset or pitch
 * (UNSUPPORTED); a Z pitch short of the target's row at two bytes a pixel,
 * a Z buffer past the frame buffer, or an alpha reference over 255
 * (INVALID_ARGUMENT).
 */
v9x_status v9x_rs690_build_3d_state(const struct v9x_rs690_3d_state *state,
                                    struct v9x_rs690_3d_list *list);

/* VAP_VF_CNTL for count embedded vertices of a primitive, or 0 when the
 * primitive is unknown, the count not a whole number of them, or past the
 * register's 16-bit field. The HAL writes this, then count * 8 dwords. */
v9x_u32 v9x_rs690_3d_vf_cntl(v9x_u32 primitive, v9x_u32 count);

/* The VF_CNTL write and count TL vertices (eight dwords each, as given),
 * for at most V9X_RS690_3D_VERTICES_MAX. */
v9x_status v9x_rs690_build_3d_draw_tl(v9x_u32 primitive,
                                      const v9x_u32 *vertices,
                                      v9x_u32 count,
                                      struct v9x_rs690_3d_list *list);

/* Flush the 3D destination and Z caches and hold the FIFO until the 3D
 * engine is idle and clean (RadeonFinishComposite, plus the Z cache). */
v9x_status v9x_rs690_build_3d_finish(struct v9x_rs690_3d_list *list);

/* The IEEE single nearest numerator / denominator, ties to even, for
 * 0 <= numerator <= denominator < 2^24, denominator > 0. And an integer's
 * single, exact below 2^24. */
v9x_u32 v9x_rs690_float_ratio(v9x_u32 numerator, v9x_u32 denominator);
v9x_u32 v9x_rs690_float_integer(v9x_u32 value);

#endif
