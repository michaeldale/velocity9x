/*
 * ATI Radeon X1300 (RV515, R500 class) 3D engine: register values for
 * drawing by direct register writes, without the CP.
 *
 * The RS690's builder (rs690_3d.c) makes the R300-class state both chips
 * share: the init, the vertex streams, the colour buffer, the texture
 * unit, Z, blend, alpha test and cull. This module passes its lists
 * through an R500 fixup and adds what the R500 does differently:
 *
 *   - the R300 rasterizer and shader writes are dropped: RS_IP_0-3
 *     (4310h-431Ch), RS_INST_0-3 (4330h-433Ch), US_CODE_OFFSET to
 *     US_TEX_INST (4608h-462Ch), the R300 ALU instruction blocks
 *     (46C0h-49FCh) and the R300 shader constants (4C00h-4DFCh). On the
 *     R500 those addresses are other registers (4624h is US_FC_CTRL,
 *     46C0h RB3D_COLOR_CLEAR_VALUE_AR);
 *   - scan-converter points (SC_CLIP_0_A/B, SC_SCISSOR0/1) lose the R300's
 *     1440-pixel guard offset (xf86-video-ati 6.14.6 radeon_exa_render.c's
 *     IS_R300_3D / else scissor branch);
 *   - an enabled alpha test gets R500_FG_ALPHA_FUNC_8BIT, as Mesa sets it
 *     for an 8-bit colour buffer (r300_emit.c r300_emit_dsa_state);
 *   - the init gets R500_VAP_INDEX_OFFSET = 0 (radeon_commonfuncs.c:97-104);
 *   - the rasterizer setup is R500_RS_IP / R500_RS_INST (Mesa
 *     r300_state_derived.c r500_rs_col / r500_rs_tex), and the fragment
 *     program is R500 code from r500_fp.c, loaded in Mesa's r500 order
 *     (r300_fs.c), with the fog colour as an R500 constant.
 *
 * Measured on M-5CAPM3's X1300 (docs\decisions\2026-11-21-rv515-3d-first-
 * draw.md, 2026-11-21-rv515-r500-encoder-and-texture.md): the colour and
 * textured REPLACE / MODULATE states. The TL state's other programs are
 * built the same way and not all have run; the probe that ran each is in
 * docs\decisions.
 *
 * Do not read GB_TILE_CONFIG after the init on this card: the read leaves
 * GA_BUSY set until a reboot (/readafter=4018, M-5CAPM3 boot 43).
 */
#ifndef VELOCITY9X_RV515_3D_H
#define VELOCITY9X_RV515_3D_H

#include "velocity9x/rs690_3d.h"

/* Room for the RS690 builder's longest list after the fixup plus the
 * R500 program, its rasterizer setup and the fog constant. */
#define V9X_RV515_3D_WRITES 256u

struct v9x_rv515_3d_list {
    v9x_u32 offsets[V9X_RV515_3D_WRITES];
    v9x_u32 values[V9X_RV515_3D_WRITES];
    v9x_u32 count;
    /* R300-only writes the fixup dropped, for a probe's report. */
    v9x_u32 dropped;
};

/* The R500's texture row and mip-level alignment in bytes: rows of every
 * level step 32 apart, where the RS690 steps 64 (V9XR53D MipLayout on
 * M-5CAPM3, docs\decisions\2026-11-21-rv515-colour-key-mipmaps.md). The
 * builders below give it to the RS690 builder as row_align; the HAL places
 * mip chains with it. */
#define V9X_RV515_TEX_ROW_ALIGN 32ul

/* R500 registers this module writes. */
#define V9X_RV515_VAP_INDEX_OFFSET      0x208cul
#define V9X_RV515_RS_IP_0               0x4074ul
#define V9X_RV515_GA_US_VECTOR_INDEX    0x4250ul
#define V9X_RV515_GA_US_VECTOR_DATA     0x4254ul
#define V9X_RV515_RS_INST_0             0x4320ul
#define V9X_RV515_US_CONFIG             0x4600ul
#define V9X_RV515_US_PIXSIZE            0x4604ul
#define V9X_RV515_US_FC_CTRL            0x4624ul
#define V9X_RV515_US_CODE_ADDR          0x4630ul
#define V9X_RV515_US_CODE_RANGE         0x4634ul
#define V9X_RV515_US_CODE_OFFSET        0x4638ul
#define V9X_RV515_FG_ALPHA_FUNC_8BIT    0x00001000ul

/* Appends in to out through the fixup. Writes that do not fit set
 * out->count to the capacity and are dropped. */
void v9x_rv515_3d_fixup(const struct v9x_rs690_3d_list *in,
                        struct v9x_rv515_3d_list *out);

/* Empties a list. */
void v9x_rv515_3d_begin(struct v9x_rv515_3d_list *list);

/* The engine init: the RS690's, fixed, and R500_VAP_INDEX_OFFSET. */
v9x_status v9x_rv515_build_3d_init(v9x_u32 pipe_select,
                                   v9x_u32 rb2d_dstcache_mode,
                                   struct v9x_rv515_3d_list *list);

/* The colour-only state for v9x_rs690_build_3d_draw: the RS690 target,
 * fixed, then colour 0 into temp 0 and an OUT of it. */
v9x_status v9x_rv515_build_3d_target(const struct v9x_rs690_3d_target *target,
                                     struct v9x_rv515_3d_list *list);

/* The textured state for v9x_rs690_build_3d_draw_textured, after the
 * target: the RS690 texture unit, fixed, then texture coordinates into
 * temp 0, colour 0 into temp 1, and LD + the combine. */
v9x_status v9x_rv515_build_3d_texture(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                      const struct v9x_rs690_3d_texture *texture,
                                      struct v9x_rv515_3d_list *list);

/* The whole TL-vertex state for v9x_rs690_build_3d_draw_tl: the RS690's,
 * fixed, then the R500 rasterizer setup and program for its combine,
 * texture alpha, specular and fog. Refused as the RS690 builder refuses. */
v9x_status v9x_rv515_build_3d_state(const struct v9x_rs690_3d_state *state,
                                    struct v9x_rv515_3d_list *list);

/* Appends an RS690 list (a draw, the finish) through the fixup. */
v9x_status v9x_rv515_append(const struct v9x_rs690_3d_list *in,
                            struct v9x_rv515_3d_list *list);

#endif
