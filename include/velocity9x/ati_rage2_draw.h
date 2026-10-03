/*
 * The Rage IIC's Direct3D draw: what a neutral draw may ask of the chip,
 * the register stream for a batch's state, and each triangle's packets.
 *
 * Pure policy and arithmetic, host-tested (tests\host\test_rage2_draw.c);
 * src\display32\d3d\d3d_rage2.c resolves surfaces, converts floats and
 * emits. Everything here was measured on A8U4I5 before it was allowed:
 * docs\decisions\2026-10-02-rage-iic-*.md.
 *
 * The request is the Mobility-M's (struct v9x_m64_draw_request, mapped
 * from the neutral draw by d3d_mach64_map.c), and so are the refusal
 * reasons: one vocabulary for the family's two engines, and V9XTRACE's
 * m64_policy_counts keep their meaning.
 */
#ifndef VELOCITY9X_ATI_RAGE2_DRAW_H
#define VELOCITY9X_ATI_RAGE2_DRAW_H

#include "velocity9x/types.h"
#include "velocity9x/ati_rage2.h"
#include "velocity9x/ati_mach64_engine.h"

/* Texture edges the policy accepts: powers of two, the sizes the 4 MiB
 * card can hold beside a 640x480 front, back and Z buffer. */
#define V9X_R2_DRAW_TEXTURE_MIN     8ul
#define V9X_R2_DRAW_TEXTURE_MAX     256ul

/* Perspective: a triangle whose quadratic strays further than this from
 * exact (v9x_r2_texture_error), in 1/1000 texel, is split in four, up to
 * V9X_R2_DRAW_SPLIT_DEPTH times. */
#define V9X_R2_DRAW_SPLIT_MILLI     500ul
#define V9X_R2_DRAW_SPLIT_DEPTH     2u
#define V9X_R2_DRAW_SPLIT_MAX       16u

/*
 * Vertices are snapped to a quarter pixel (the Mobility-M's 14.2), so
 * every midpoint of two splits is exact in the setup's sixteenths and a
 * split vertex lies exactly on its parent's edge: no crack against an
 * unsplit neighbour.
 */
#define V9X_R2_DRAW_SNAP            4l

/* A trapezoid taller than V9X_R2_TRAP_LENGTH_MAX is cut into pieces; a
 * triangle's packets then hold up to this many trapezoids. */
#define V9X_R2_DRAW_TRAPS_MAX       34u

/* The per-trapezoid registers: colour 9, alpha 3, Z 3, S/T 12, the
 * trajectory 9. */
#define V9X_R2_DRAW_TRAP_DWORDS     36ul
#define V9X_R2_DRAW_STATE_DWORDS    24ul

/* What an accepted draw programs. */
struct v9x_r2_draw_decision {
    v9x_u32 scale_3d_cntl;      /* the whole register */
    v9x_u32 texture_format;     /* V9X_R2_TEX_FORMAT_*, if textured */
    v9x_u32 alpha_from_fog;     /* the alpha interpolator carries fog */
    v9x_u32 clamp_in_unit;      /* CLAMP accepted as WRAP (see below) */
    v9x_u32 mip_mapped;         /* MIP_MAP_DISABLE clear, every level set */
};

/*
 * The policy. Returns V9X_M64_REFUSE_NONE and fills `decision`, or the
 * first refusal. Passive. `coords_in_unit` is non-zero when the caller has
 * seen every tu and tv of the batch inside [0, 1]: the chip only wraps
 * (no TEX_CNTL), and on those coordinates CLAMP and WRAP agree.
 */
v9x_u32 v9x_r2_check_draw(const struct v9x_m64_draw_request *request,
                          v9x_u32 coords_in_unit,
                          struct v9x_r2_draw_decision *decision);

/* The surfaces of an accepted draw. */
struct v9x_r2_draw_state {
    struct v9x_r2_target target;        /* scissor inclusive */
    v9x_u32 depth_enable;
    v9x_u32 depth_offset;
    v9x_u32 depth_pitch_bytes;          /* Z_OFF_PITCH has its own */
    v9x_u32 depth_func;                 /* V9X_R3D_CMP_* */
    v9x_u32 depth_write;
    v9x_u32 textured;
    struct v9x_r2_texture texture;      /* scale_3d_extra unused */
    /* For a mip-mapped decision, level n's VRAM offset (TEX_n_OFF: the
     * level whose larger edge is 2^n), for n up to the top level's, whose
     * entry is texture.offset. Each level is laid out at its own width. */
    v9x_u32 mip_offsets[V9X_R2_TEX_LEVEL_MAX + 1ul];
    v9x_u32 fog_color;                  /* 0x00RRGGBB */
};

/* SCALE_3D_CNTL first, then the datapath, target, scissor, Z surface and
 * test, texture map and fog colour. */
v9x_status v9x_r2_build_draw_state(const struct v9x_r2_draw_state *state,
                                   const struct v9x_r2_draw_decision *decision,
                                   v9x_u32 *offsets, v9x_u32 *values,
                                   v9x_u32 capacity, v9x_u32 *written);

/* One vertex, already converted: position in the setup's sixteenths
 * (snapped by v9x_r2_snap), Z in depth units, the colours as Direct3D
 * packs them, the texture coordinates and 1/w. */
struct v9x_r2_draw_vertex {
    v9x_s32 x;
    v9x_s32 y;
    v9x_u32 z;                  /* 0..65535 */
    v9x_u32 argb;
    v9x_u32 fog;                /* the specular alpha: 255 is no fog */
    double tu;
    double tv;
    double q;
};

/* A coordinate in sixteenths to the nearest quarter pixel. */
v9x_s32 v9x_r2_snap(v9x_s32 sixteenths);

/*
 * A triangle as the chip will draw it: up to V9X_R2_DRAW_SPLIT_MAX pieces
 * (split while the texture error is above V9X_R2_DRAW_SPLIT_MILLI), each
 * set up into trapezoids of at most V9X_R2_TRAP_LENGTH_MAX rows. Returns
 * V9X_STATUS_OK with *pieces 0 for a triangle that covers no pixel centre.
 * `fits_out` (may be null) receives each piece's texture fit where the
 * split decision made one, `valid` clear where it did not, for
 * v9x_r2_build_piece.
 */
v9x_status v9x_r2_split_triangle(const struct v9x_r2_draw_state *state,
                                 const struct v9x_r2_draw_decision *decision,
                                 const struct v9x_r2_draw_vertex *vertices,
                                 struct v9x_r2_draw_vertex *pieces_out,
                                 struct v9x_r2_texture_fit *fits_out,
                                 v9x_u32 *pieces);

/* The stage of v9x_r2_build_piece that failed, for its `stage` output. */
#define V9X_R2_PIECE_STAGE_NONE      0ul
#define V9X_R2_PIECE_STAGE_SETUP     1ul    /* v9x_r2_setup_triangle */
#define V9X_R2_PIECE_STAGE_SPLIT     2ul    /* v9x_r2_split_trap */
#define V9X_R2_PIECE_STAGE_CAPACITY  3ul
#define V9X_R2_PIECE_STAGE_COLOR     4ul    /* colour gradient past S.8.12 */
#define V9X_R2_PIECE_STAGE_ALPHA     5ul    /* alpha or fog likewise */
#define V9X_R2_PIECE_STAGE_DEPTH     6ul
#define V9X_R2_PIECE_STAGE_TEXTURE   7ul    /* v9x_r2_setup_texture */
#define V9X_R2_PIECE_STAGE_TRAP      8ul    /* v9x_r2_build_trap */
#define V9X_R2_PIECE_STAGES          9ul
/* `stage` carries the failing call's own status in bits 15:8. */
#define V9X_R2_PIECE_STATUS_SHIFT    8u
#define V9X_R2_PIECE_STAGE_MASK      0xfful

/*
 * One piece's packets: every trapezoid's interpolators then its trigger.
 * V9X_STATUS_UNSUPPORTED for a piece the interpolators cannot express (a
 * sliver steeper than 255 colour levels a pixel, a texture term past its
 * register), which the caller skips and counts. A sliver whose colour or
 * alpha gradient is past S.8.12 is drawn flat, at its centroid's colour,
 * rather than skipped: it covers a few pixels, and Quake 2's lost 24,152
 * pieces that way on boot 146. `fit` (may be null, or not valid) is the
 * piece's texture fit from v9x_r2_split_triangle; without one the piece
 * is fitted here, once for all its trapezoids. `traps` (may be null)
 * receives the trapezoids, for the host test's engine model; `stage` (may
 * be null) the V9X_R2_PIECE_STAGE_* that failed.
 */
v9x_status v9x_r2_build_piece(const struct v9x_r2_draw_state *state,
                              const struct v9x_r2_draw_decision *decision,
                              const struct v9x_r2_draw_vertex *vertices,
                              const struct v9x_r2_texture_fit *fit,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written,
                              struct v9x_r2_flat_trap *traps,
                              v9x_u32 *trap_count, v9x_u32 *stage);

/*
 * Cut a trapezoid into pieces of at most `rows_max` rows by running the
 * measured edge walk forward: each piece starts where the last one's
 * edges stood, with their errors, so together they draw exactly the
 * pixels the whole would.
 */
v9x_status v9x_r2_split_trap(const struct v9x_r2_flat_trap *trap,
                             v9x_u32 rows_max,
                             struct v9x_r2_flat_trap *pieces,
                             v9x_u32 capacity, v9x_u32 *count);

#endif
