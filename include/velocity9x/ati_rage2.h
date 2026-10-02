/*
 * ATI Rage II class (264GT2C, the Rage IIC): trapezoid register encoding.
 *
 * The chip has no triangle setup engine. The 3D RAGE's drawing primitive is
 * the trapezoid: the line engine walks the leading edge, a second Bresenham
 * engine walks the trailing edge, and the pixels between the two on each
 * scanline are drawn. A write to DST_BRES_LNTH with DRAW_TRAP set starts it.
 * docs\specifications\ati-rage2-3d-engine.md has the register notes and
 * their sources; nothing in them was measured when this was written.
 *
 * This module only encodes: a described trapezoid in, an ordered
 * (offset, value) stream out, bounds refused rather than clamped. It does
 * not compute Bresenham terms from edge endpoints, because how the engine
 * counts the leading-edge length and which span pixels it includes are
 * what the Phase 2 scenes measure (docs\plans\ati-rage-iic-hardware-3d.md).
 *
 * Offsets are 4 KiB-window offsets, block 0 at +400h, as for the Mach64.
 */
#ifndef VELOCITY9X_ATI_RAGE2_H
#define VELOCITY9X_ATI_RAGE2_H

#include "velocity9x/ati_mach64_engine.h"

/* GT trapezoid registers (RRG-G02700 p.4-46, 4-59; atiregs.h "GT"). */
#define V9X_R2_DST_BRES_LNTH        0x00000520ul  /* 0_48, the trigger */
#define V9X_R2_TRAIL_BRES_ERR       0x00000538ul  /* 0_4E */
#define V9X_R2_TRAIL_BRES_INC       0x0000053cul  /* 0_4F */
#define V9X_R2_TRAIL_BRES_DEC       0x00000540ul  /* 0_50 */

/* DST_BRES_LNTH fields (RRG p.4-46). */
#define V9X_R2_LNTH_LOAD_TRAIL      0x80000000ul  /* bit 31: load TRAIL_X */
#define V9X_R2_LNTH_DRAW_TRAP       0x00008000ul  /* bit 15: draw */
#define V9X_R2_LNTH_TRAIL_X_SHIFT   16u           /* 28:16 */
#define V9X_R2_LNTH_LENGTH_MASK     0x00007ffful  /* 14:0 */

/* DST_CNTL bits a trapezoid may use (atiregs.h; RRG p.4-48). X_DIR and
 * Y_DIR are the Mach64 ones in ati_mach64_regs.h. */
#define V9X_R2_DST_Y_MAJOR          0x00000004ul
#define V9X_R2_DST_BRES_SIGN        0x00000800ul
#define V9X_R2_TRAIL_X_DIR          0x00002000ul
#define V9X_R2_TRAP_FILL_DIR        0x00004000ul
#define V9X_R2_TRAIL_BRES_SIGN      0x00008000ul
#define V9X_R2_BRES_SIGN_AUTO       0x00020000ul
#define V9X_R2_TRAP_DST_CNTL_MASK   (V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR | \
    V9X_R2_DST_Y_MAJOR | V9X_R2_DST_BRES_SIGN | V9X_R2_TRAIL_X_DIR |     \
    V9X_R2_TRAP_FILL_DIR | V9X_R2_TRAIL_BRES_SIGN | V9X_R2_BRES_SIGN_AUTO)

/* Field limits: X 13 bits, Y 15 bits, terms signed 18 bits (RRG). The
 * length cap is this module's, not the chip's: the first scenes are small
 * on purpose, so a mis-modelled length cannot sweep the aperture. */
#define V9X_R2_X_MAX                8191ul
#define V9X_R2_BRES_MIN             (-131072l)
#define V9X_R2_BRES_MAX             131071l
#define V9X_R2_BRES_MASK            0x0003fffful
#define V9X_R2_TRAP_LENGTH_MAX      64ul

#define V9X_R2_FLAT_STATE_DWORDS    11ul
#define V9X_R2_TRAP_DWORDS          9ul

/* A 16-bpp destination surface in VRAM and the scissor over it, in the
 * surface's own pixel coordinates, inclusive. */
struct v9x_r2_target {
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 vram_bytes;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
};

/* One flat trapezoid, as raw register terms. */
struct v9x_r2_flat_trap {
    v9x_u32 x;          /* leading edge start, DST_Y_X */
    v9x_u32 y;
    v9x_u32 length;     /* DST_BRES_LNTH[14:0] */
    v9x_u32 trail_x;    /* DST_BRES_LNTH[28:16] */
    v9x_s32 lead_err;
    v9x_s32 lead_inc;
    v9x_s32 lead_dec;
    v9x_s32 trail_err;
    v9x_s32 trail_inc;
    v9x_s32 trail_dec;
    v9x_u32 dst_cntl;   /* only V9X_R2_TRAP_DST_CNTL_MASK bits */
};

/*
 * Triangle setup (rage2_setup.c): a screen-space triangle into at most two
 * trapezoids whose engine coverage is exactly centre sampling with the
 * top-left rule. Vertices are fixed point, V9X_R2_SUBPIXEL per pixel.
 * Coordinates must lie in [0, 2047] pixels, which keeps every product in
 * the setup inside 31 bits and every edge term inside the 18-bit fields.
 *
 * The edge terms follow the walk measured on A8U4I5
 * (docs\decisions\2026-10-02-rage-iic-trapezoid-edge-model.md): per row
 * an edge steps while its error is >= 0, adding DEC, then adds INC. For an
 * edge spanning dyF by dxF sixteenths that is DEC = -dyF, INC = dxF, and
 * an ERR in [-dyF, 0) that carries the first row's rounding.
 */
#define V9X_R2_SUBPIXEL             16l
#define V9X_R2_SETUP_COORD_MAX      (2047l * V9X_R2_SUBPIXEL)
#define V9X_R2_SETUP_TRAPS          2u

struct v9x_r2_vertex {
    v9x_s32 x;
    v9x_s32 y;
};

/* The trapezoids for one flat triangle inside `target` (its whole width
 * and height, not only the scissor). *count is 0 for a triangle that
 * covers no pixel centre. Only trajectory fields are filled; the colour
 * and datapath come from v9x_r2_build_flat_state. */
v9x_status v9x_r2_setup_triangle(const struct v9x_r2_target *target,
                                 const struct v9x_r2_vertex *vertices,
                                 struct v9x_r2_flat_trap *traps,
                                 v9x_u32 *count);

/*
 * Interpolator formats, measured by write and read-back on A8U4I5 (boot
 * 136, ATIRX /regs): the colour and alpha registers implement bits 24:4,
 * S.8.12 with the integer in 23:16; Z implements 28:0, S.16.12. A value in
 * 16.16 fixed point masked with these is the register encoding.
 */
#define V9X_R2_COLOR_MASK           0x01fffff0ul
#define V9X_R2_Z_MASK               0x1ffffffful

#define V9X_R2_RED_X_INC            0x000007c0ul  /* 0_F0 */
#define V9X_R2_RED_Y_INC            0x000007c4ul
#define V9X_R2_RED_START            0x000007c8ul
#define V9X_R2_GREEN_X_INC          0x000007ccul
#define V9X_R2_GREEN_Y_INC          0x000007d0ul
#define V9X_R2_GREEN_START          0x000007d4ul
#define V9X_R2_BLUE_X_INC           0x000007d8ul
#define V9X_R2_BLUE_Y_INC           0x000007dcul
#define V9X_R2_BLUE_START           0x000007e0ul
/* Alpha, which is also the fog factor (RRG p.6-14): S.8.12 like colour. */
#define V9X_R2_ALPHA_X_INC          0x000007f0ul  /* 0_FC */
#define V9X_R2_ALPHA_Y_INC          0x000007f4ul
#define V9X_R2_ALPHA_START          0x000007f8ul

/* SCALE_3D_CNTL SCALE_3D_FCN = 3, shading (RRG p.6-4), and DP_SRC
 * FRGD_SRC = 5, the scaler/3D pipe (RRG p.4-97). */
#define V9X_R2_SCALE_3D_SHADE       0x000000c0ul
#define V9X_R2_DP_SRC_3D            0x00000500ul
/* DP_PIX_WIDTH destination and source 16 bpp (565); DP_MIX foreground
 * source, background destination (RRG p.4-93, p.4-94). */
#define V9X_R2_DP_PIX_WIDTH_565     0x00040004ul
#define V9X_R2_DP_MIX_FRGD_SRC      0x00070003ul

#define V9X_R2_SHADE_STATE_DWORDS   20ul

/* Per-channel start, X and Y increments, each 16.16 fixed point in the
 * channel's 0..255 range; masked to the register format when emitted. */
struct v9x_r2_shade {
    v9x_s32 start[3];   /* red, green, blue */
    v9x_s32 x_inc[3];
    v9x_s32 y_inc[3];
};

/*
 * Gouraud setup: the colour plane through three vertices, evaluated for one
 * trapezoid. `colors` are 0x00RRGGBB per vertex. The engine's colour is a
 * plane anchored at the trapezoid's DST_Y_X pixel (measured: an edge's X
 * step adds X_INC too), so START is the plane at that pixel's centre, plus
 * one half for round-to-nearest, because the engine truncates.
 *
 * The accumulators wrap with a 9-bit integer part: 0..255 is the colour,
 * 256..383 saturates to 255 and 384..511 reads as 0 (measured), so START
 * may wrap freely while every drawn pixel stays well inside -128..383.
 * V9X_STATUS_UNSUPPORTED when a gradient exceeds the S.8.12 range (a
 * sliver steeper than 255 levels a pixel).
 */
v9x_status v9x_r2_setup_shade(const struct v9x_r2_vertex *vertices,
                              const v9x_u32 *colors,
                              const struct v9x_r2_flat_trap *trap,
                              struct v9x_r2_shade *shade);

/* The engine's colour arithmetic, as measured: the 8-bit channel value a
 * pixel gets from a 16.16 accumulator value (masked to S.8.12). Shared by
 * the host test and the scene runner's expected-image model. */
v9x_u32 v9x_r2_channel_out(v9x_s32 accumulator);

/*
 * Z16. Z_CNTL implements bits 0, 1, 2, 6:4 and 8 (read back on A8U4I5);
 * the meanings are the Rage Pro's (Mesa mach64_reg.h): Z_EN bit 0,
 * Z_TEST 6:4 (never, <, <=, ==, >=, >, !=, always), Z_MASK bit 8 = write.
 * The Z interpolators are S.16.12, integer 27:12. The Z surface has its
 * own offset and pitch in Z_OFF_PITCH and tracks the destination in X and
 * Y (RRG p.4-60).
 */
#define V9X_R2_Z_X_INC              0x000007e4ul  /* 0_F9 */
#define V9X_R2_Z_Y_INC              0x000007e8ul
#define V9X_R2_Z_START              0x000007ecul
#define V9X_R2_Z_EN                 0x00000001ul
#define V9X_R2_Z_TEST_SHIFT         4u
#define V9X_R2_Z_TEST_MASK          0x00000070ul
#define V9X_R2_Z_WRITE              0x00000100ul
#define V9X_R2_Z_STATE_DWORDS       5ul

/* Z values are 16.16 fixed point in depth units (0..65535). */
struct v9x_r2_depth {
    v9x_u32 offset;          /* the Z surface, same pitch as the target */
    v9x_u32 z_cntl;          /* Z_EN | test << 4 | Z_WRITE */
    v9x_s32 start;
    v9x_s32 x_inc;
    v9x_s32 y_inc;
};

/* Z_OFF_PITCH, Z_CNTL and the three Z interpolators, after the shading
 * state (which puts SCALE_3D_CNTL's function first). */
v9x_status v9x_r2_build_z_state(const struct v9x_r2_target *target,
                                const struct v9x_r2_depth *depth,
                                v9x_u32 *offsets, v9x_u32 *values,
                                v9x_u32 capacity, v9x_u32 *written);

/*
 * Texturing. Registers per RRG-G02700 ch.6 and xf86-video-mach64
 * atiregs.h ("GT"/"GTB"); field widths read back on A8U4I5, boot 136:
 * S/T_START 10.11 in bits 25:5, S/T_XINC_START and S/T_Y_INC S.11.16 in
 * 27:0, the *_INC2 terms S.10.16 in 26:0, TEX_SIZE_PITCH three nibbles
 * (pitch 3:0, size 7:4, height 11:8, each log2). The texel format is
 * DP_PIX_WIDTH[31:28]; TEX_k_OFF is the 2^k map's byte offset.
 * What they do on a draw is below, measured on the card.
 */
#define V9X_R2_TEX_0_OFF            0x000005c0ul
#define V9X_R2_S_X_INC2             0x00000740ul
#define V9X_R2_T_X_INC2             0x00000758ul
#define V9X_R2_TEX_SIZE_PITCH       0x00000770ul
#define V9X_R2_ST_START_MASK        0x03ffffe0ul
#define V9X_R2_ST_INC_MASK          0x0ffffffful
#define V9X_R2_ST_INC2_MASK         0x07fffffful

#define V9X_R2_SCALE_3D_TEXTURE     0x00000080ul  /* SCALE_3D_FCN = 2 */
#define V9X_R2_TEX_CACHE_DIS        0x00000020ul
#define V9X_R2_MIP_MAP_DISABLE      0x01000000ul
#define V9X_R2_BILINEAR_TEX_EN      0x02000000ul
/* TEX_BLEND_FCN, the minification filter, bits 27:26 (RRG p.6-6): 0
 * nearest, 2 a 2x2 blend in the nearest map. */
#define V9X_R2_TEX_BLEND_MASK       0x0c000000ul
#define V9X_R2_TEX_BLEND_2X2        0x08000000ul
/* The rest of SCALE_3D_CNTL a textured draw may set (RRG p.6-4..6-6):
 * ALPHA_FOG_EN 12:11 (1 blend, 2 fog), COLOR_OVERRIDE 13, the blend
 * factors 18:16 and 21:19, TEX_LIGHT_FCN 23:22 (1 modulate, 2 alpha
 * decal), TEX_AMASK_AEN 28 and TEX_MAP_AEN 30. */
#define V9X_R2_ALPHA_FOG_BLEND      0x00000800ul
#define V9X_R2_ALPHA_FOG_FOG        0x00001000ul
#define V9X_R2_COLOR_OVERRIDE       0x00002000ul
#define V9X_R2_BLEND_SRC_SHIFT      16u
#define V9X_R2_BLEND_DST_SHIFT      19u
#define V9X_R2_TEX_LIGHT_MODULATE   0x00400000ul
#define V9X_R2_TEX_LIGHT_DECAL      0x00800000ul
#define V9X_R2_TEX_AMASK_AEN        0x10000000ul
#define V9X_R2_TEX_MAP_AEN          0x40000000ul
#define V9X_R2_TEX_EXTRA_MASK       0x5eff3800ul
#define V9X_R2_TEX_FORMAT_SHIFT     28u
#define V9X_R2_TEX_FORMAT_1555      3ul
#define V9X_R2_TEX_FORMAT_565       4ul
#define V9X_R2_TEX_FORMAT_4444      15ul
#define V9X_R2_TEX_LEVEL_MAX        10ul
#define V9X_R2_TEXTURE_STATE_DWORDS 25ul

/*
 * The S/T arithmetic, measured on A8U4I5 (boot 136, ATIRX /tex; see
 * docs\decisions\2026-10-02-rage-iic-texture-addressing.md):
 *
 * - S and T are normalised: 1.0 = V9X_R2_ST_ONE spans the map's larger
 *   dimension (TEX_SIZE), on both axes.
 * - Each is walked by forward differences from the DST_Y_X pixel, which
 *   to within the accumulator's resolution is the quadratic
 *   START + XINC_START*dx + Y_INC*dy + X_INC2*dx(dx-1)/2
 *         + Y_INC2*dy(dy-1)/2 + XY_INC2*dx*dy,
 *   dx counting steps in DST_X_DIR's direction, dy rows.
 * - The value accumulator has START's resolution, bits 25:5: every
 *   increment is floored to a multiple of 32 as it is added (ATIRX
 *   /texprec). The increments themselves take the second differences at
 *   full precision.
 * - A leading-edge step adds the X increment and then X_INC2 to it, and
 *   XY_INC2 to the Y increment; a new row adds the Y increment, then
 *   Y_INC2 to it and XY_INC2 to the X increment. A span pixel in
 *   DST_X_DIR's direction adds the X increment and then X_INC2 to it;
 *   against it, X_INC2 comes off first and the increment is subtracted as
 *   its ones' complement, V9X_R2_ST_AGAINST_LOSS more.
 *   (ATIRX /textri: 9,753 pixels of 60 triangles, every one.)
 * - The texel is floor(): u = S >> (26 - size) wrapped at 2^pitch, v the
 *   same from T wrapped at 2^height. There is no clamp (no TEX_CNTL).
 */
#define V9X_R2_ST_ONE               0x04000000l
#define V9X_R2_ST_FRACTION_BITS     26u
#define V9X_R2_ST_AGAINST_LOSS      32ul

struct v9x_r2_texture {
    v9x_u32 offset;          /* byte offset of the largest map */
    v9x_u32 log2_width;
    v9x_u32 log2_height;
    v9x_u32 log2_pitch;      /* in texels; must equal log2_width */
    v9x_u32 format;          /* V9X_R2_TEX_FORMAT_* */
    v9x_u32 scale_3d_extra;  /* further SCALE_3D_CNTL bits, e.g. bilinear */
};

/* S and T register values, in the engine's unit: V9X_R2_ST_ONE spans the
 * larger dimension. */
struct v9x_r2_st {
    v9x_s32 start[2];        /* S, T */
    v9x_s32 xinc_start[2];
    v9x_s32 y_inc[2];
    v9x_s32 x_inc2[2];
    v9x_s32 y_inc2[2];
    v9x_s32 xy_inc2[2];
};

/* The datapath for a textured trapezoid: SCALE_3D_CNTL texture mapping
 * with MIP_MAP_DISABLE first, the 3D source, the texel format, the map's
 * size and every TEX_k_OFF up to it pointing at it, then the twelve S/T
 * registers. */
v9x_status v9x_r2_build_texture_state(const struct v9x_r2_target *target,
                                      const struct v9x_r2_texture *texture,
                                      const struct v9x_r2_st *st,
                                      v9x_u32 *offsets, v9x_u32 *values,
                                      v9x_u32 capacity, v9x_u32 *written);

/* The engine's S/T walk, for the host test and the scene runner: begin at
 * the DST_Y_X pixel, make each row's edge steps, copy the state for the
 * span and step it pixel by pixel, then go to the next row. Values are
 * modulo 2^26. */
struct v9x_r2_st_walk {
    v9x_u32 value[2];        /* S, T */
    v9x_u32 x_inc[2];
    v9x_u32 y_inc[2];
};

void v9x_r2_st_begin(const struct v9x_r2_st *st, struct v9x_r2_st_walk *w);
void v9x_r2_st_edge_step(const struct v9x_r2_st *st,
                         struct v9x_r2_st_walk *w);
void v9x_r2_st_next_row(const struct v9x_r2_st *st,
                        struct v9x_r2_st_walk *w);
/* One span pixel: `forward` is DST_X_DIR's direction. */
void v9x_r2_st_pixel_step(const struct v9x_r2_st *st,
                          struct v9x_r2_st_walk *span, int forward);

/* The texel index a value selects: log2_size is TEX_SIZE, log2_wrap the
 * pitch for S or the height for T. */
v9x_u32 v9x_r2_texel_index(v9x_u32 value, v9x_u32 log2_size,
                           v9x_u32 log2_wrap);

/* A vertex's texture coordinates as Direct3D gives them: tu and tv each
 * normalised to its own axis (1.0 = the map's width, height), q = 1/w,
 * any positive scale (1.0 at every vertex is affine). */
struct v9x_r2_tex_coord {
    double tu;
    double tv;
    double q;
};

/*
 * S/T setup for one trapezoid of a triangle. The engine interpolates a
 * quadratic, not 1/w, so perspective is approximated: the quadratic that
 * takes the exact perspective value at the three vertices and the three
 * edge midpoints. One quadratic per triangle, re-anchored per trapezoid,
 * so the two trapezoids join exactly, and two triangles sharing an edge
 * agree at three points of it and so along all of it. Affine input gives
 * the plane. A sliver whose quadratic needs more curvature than the
 * S.10.16 second differences hold gets the plane through its vertices
 * instead, for both trapezoids, and *affine (if given) says so.
 * Fails with V9X_STATUS_INVALID_STATE when a q is not positive and
 * finite or a coordinate is not finite; V9X_STATUS_UNSUPPORTED for a
 * triangle of no area; V9X_STATUS_INTEGER_OVERFLOW when a difference
 * exceeds its register (S.11.16: two map widths a pixel); and
 * V9X_STATUS_INVALID_ARGUMENT when START cannot be reduced.
 *
 * Double arithmetic, converted without a cast: the HAL links no runtime,
 * and Open Watcom lowers a float-to-int cast to __CHP.
 */
v9x_status v9x_r2_setup_texture(const struct v9x_r2_vertex *vertices,
                                const struct v9x_r2_tex_coord *coords,
                                const struct v9x_r2_texture *texture,
                                const struct v9x_r2_flat_trap *trap,
                                struct v9x_r2_st *st, v9x_u32 *affine);

/* How far the surface v9x_r2_setup_texture emits (the quadratic, or a
 * sliver's plane) strays from exact perspective, in texels of the larger
 * dimension, read on a barycentric grid in eighths: what a caller
 * subdivides on. The error is the fit's, before register rounding. */
v9x_status v9x_r2_texture_error(const struct v9x_r2_vertex *vertices,
                                const struct v9x_r2_tex_coord *coords,
                                const struct v9x_r2_texture *texture,
                                double *texels);

/* The datapath for a Gouraud trapezoid: SCALE_3D_CNTL shading first (the
 * accumulators may only be written with SCALE_3D_FCN non-zero, RRG p.6-7),
 * source the 3D pipe, then the nine interpolator registers. */
v9x_status v9x_r2_build_shade_state(const struct v9x_r2_target *target,
                                    const struct v9x_r2_shade *shade,
                                    v9x_u32 *offsets, v9x_u32 *values,
                                    v9x_u32 capacity, v9x_u32 *written);

/* The datapath for a flat-coloured 2D-path trapezoid: 3D pipe off,
 * DP_FRGD_CLR as the source, the target and its scissor. */
v9x_status v9x_r2_build_flat_state(const struct v9x_r2_target *target,
                                   v9x_u32 color,
                                   v9x_u32 *offsets, v9x_u32 *values,
                                   v9x_u32 capacity, v9x_u32 *written);

/* The trajectory and its trigger, which is always the last write. */
v9x_status v9x_r2_build_trap(const struct v9x_r2_target *target,
                             const struct v9x_r2_flat_trap *trap,
                             v9x_u32 *offsets, v9x_u32 *values,
                             v9x_u32 capacity, v9x_u32 *written);

#endif
