/*
 * 3dfx VSA-100 3D: register writes for one draw through the triangle setup
 * unit.
 *
 * Pure policy, no I/O. The caller writes every (offset, value) in order to
 * BAR0 + V9X_TDFX_3D_BASE + offset. The description is in chip terms - Z in
 * depth counts, colours 0.0-255.0, function and factor codes as the chip
 * numbers them - so the Direct3D mapping above it decides how a draw becomes
 * one, and the probe can drive it directly. Register meanings, with Napalm
 * spec rev 1.13 citations: docs\specifications\vsa100-registers.md,
 * section 8. Plan: docs\plans\3dfx-vsa100-d3d.md.
 *
 * Phase 5 scope: a linear 16-bit RGB565 target, an optional linear Z16
 * buffer, the setup unit fed by direct writes, one TMU. Anything outside it
 * is refused rather than approximated.
 */
#ifndef VELOCITY9X_TDFX_3D_H
#define VELOCITY9X_TDFX_3D_H

#include "velocity9x/status.h"

/* The 3D register space in BAR0 (Napalm section 4, section 8 p.81). */
#define V9X_TDFX_3D_BASE            0x200000ul

/* Address bits 13:10 pick the unit a write reaches: 0 every unit, lsb FBI,
 * then TREX0 and TREX1 (section 8 p.81, 8.21 p.102). TMU0 alone is the
 * second bit. */
#define V9X_TDFX_3D_TMU0            0x00000800ul

/* 3D registers (section 8 map pp.82-86). */
#define V9X_TDFX_3D_STATUS          0x000ul
#define V9X_TDFX_3D_FBZ_COLOR_PATH  0x104ul
#define V9X_TDFX_3D_FOG_MODE        0x108ul
#define V9X_TDFX_3D_ALPHA_MODE      0x10cul
#define V9X_TDFX_3D_FBZ_MODE        0x110ul
#define V9X_TDFX_3D_CLIP_LEFT_RIGHT 0x118ul
#define V9X_TDFX_3D_CLIP_TOP_BOTTOM 0x11cul
#define V9X_TDFX_3D_NOP_CMD         0x120ul
#define V9X_TDFX_3D_RENDER_MODE     0x1e0ul
#define V9X_TDFX_3D_COL_BUFFER_ADDR 0x1ecul
#define V9X_TDFX_3D_COL_BUFFER_STRIDE 0x1f0ul
#define V9X_TDFX_3D_AUX_BUFFER_ADDR 0x1f4ul
#define V9X_TDFX_3D_AUX_BUFFER_STRIDE 0x1f8ul
#define V9X_TDFX_3D_S_SETUP_MODE    0x260ul
#define V9X_TDFX_3D_S_VX            0x264ul
#define V9X_TDFX_3D_S_VY            0x268ul
#define V9X_TDFX_3D_S_ARGB          0x26cul
#define V9X_TDFX_3D_S_RED           0x270ul
#define V9X_TDFX_3D_S_GREEN         0x274ul
#define V9X_TDFX_3D_S_BLUE          0x278ul
#define V9X_TDFX_3D_S_ALPHA         0x27cul
#define V9X_TDFX_3D_S_VZ            0x280ul
#define V9X_TDFX_3D_S_WB            0x284ul
#define V9X_TDFX_3D_S_WTMU0         0x288ul
#define V9X_TDFX_3D_S_S_W0          0x28cul
#define V9X_TDFX_3D_S_T_W0          0x290ul
#define V9X_TDFX_3D_S_DRAW_TRI_CMD  0x2a0ul
#define V9X_TDFX_3D_S_BEGIN_TRI_CMD 0x2a4ul
#define V9X_TDFX_3D_TEXTURE_MODE    0x300ul
#define V9X_TDFX_3D_TLOD            0x304ul
#define V9X_TDFX_3D_TEX_BASE_ADDR   0x30cul

/* Comparison codes for depth and alpha tests (sections 8.23, 8.25). */
#define V9X_TDFX_CMP_NEVER          0ul
#define V9X_TDFX_CMP_LESS           1ul
#define V9X_TDFX_CMP_EQUAL          2ul
#define V9X_TDFX_CMP_LEQUAL         3ul
#define V9X_TDFX_CMP_GREATER        4ul
#define V9X_TDFX_CMP_NOTEQUAL       5ul
#define V9X_TDFX_CMP_GEQUAL         6ul
#define V9X_TDFX_CMP_ALWAYS         7ul

/* Blend factors, alphaMode (section 8.23). */
#define V9X_TDFX_BLEND_ZERO         0x0ul
#define V9X_TDFX_BLEND_SRC_ALPHA    0x1ul
#define V9X_TDFX_BLEND_DST_ALPHA    0x3ul
#define V9X_TDFX_BLEND_ONE          0x4ul
#define V9X_TDFX_BLEND_INV_SRC_ALPHA 0x5ul
#define V9X_TDFX_BLEND_INV_DST_ALPHA 0x7ul

/* Texture formats, textureMode 11:8 (section 8.76). */
#define V9X_TDFX_TEX_RGB565         10ul
#define V9X_TDFX_TEX_ARGB1555       11ul
#define V9X_TDFX_TEX_ARGB4444       12ul

/* How the texel and the iterated colour combine at the FBI. */
#define V9X_TDFX_COMBINE_REPLACE    0ul
#define V9X_TDFX_COMBINE_MODULATE   1ul
/* Colour modulated, alpha from the texel. */
#define V9X_TDFX_COMBINE_MODULATE_TEXALPHA 2ul

/* Without tLOD bit 30 the largest level is 256 texels (section 8.77). */
#define V9X_TDFX_TEX_LOG2_MAX       8ul
/* tLOD's aspect field: the long side at most 2^3 times the short. */
#define V9X_TDFX_TEX_ASPECT_MAX     3ul

/* State, setup mode, three vertices of at most 13 writes each and their
 * commands: 64 is room to spare for one triangle. */
#define V9X_TDFX_3D_WRITES          64u

struct v9x_tdfx_3d_words {
    v9x_u32 offsets[V9X_TDFX_3D_WRITES];
    v9x_u32 values[V9X_TDFX_3D_WRITES];
    v9x_u32 count;
};

/* A linear surface in video memory: byte offset from its first byte. */
struct v9x_tdfx_3d_surface {
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
};

struct v9x_tdfx_3d_texture {
    /* Where level 0 of this texture's own data starts, 16-byte aligned; the
     * builder works out the base the chip wants (section 20 p.228). */
    v9x_u32 offset;
    v9x_u32 format;
    /* 2^log2_size texels on the long side, and the short side 2^aspect
     * times smaller; s_wider is 1 when S (the width) is the long side.
     * Zero aspect is square. */
    v9x_u32 log2_size;
    v9x_u32 aspect;
    v9x_u32 s_wider;
    /* How many levels the sampler may read: level 0 and the halvings that
     * follow it packed, each straight after the one before (section 20
     * p.228). 0 and 1 both mean level 0 alone. */
    v9x_u32 levels;
    v9x_u32 min_bilinear;
    v9x_u32 mag_bilinear;
    v9x_u32 clamp;
    v9x_u32 combine;
};

struct v9x_tdfx_3d_state {
    /* The video memory size, for bounds. */
    v9x_u32 vram_bytes;
    struct v9x_tdfx_3d_surface target;
    v9x_u32 width;
    v9x_u32 height;

    v9x_u32 depth_enable;
    struct v9x_tdfx_3d_surface depth;
    v9x_u32 depth_func;
    v9x_u32 depth_write;

    v9x_u32 blend_enable;
    v9x_u32 blend_src;
    v9x_u32 blend_dst;

    v9x_u32 alpha_test;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;

    v9x_u32 dither;

    v9x_u32 texture_enable;
    struct v9x_tdfx_3d_texture texture;
};

/* A vertex in chip units: pixels, colour channels 0.0-255.0, Z in depth
 * counts (0-65535 at 16 bpp), 1/W, and S/W, T/W. */
struct v9x_tdfx_3d_vertex {
    float x;
    float y;
    float z;
    float oow;
    float r;
    float g;
    float b;
    float a;
    float sow;
    float tow;
};

/*
 * The state writes for a draw: renderMode, colour and aux buffers, clip,
 * fbzMode, fbzColorPath, alphaMode, fogMode and, with a texture, TMU0's
 * textureMode, tLOD and texBaseAddr. V9X_STATUS_OK with the writes in
 * words; INVALID_ARGUMENT for a surface outside memory or misaligned;
 * UNSUPPORTED for anything outside the Phase 5 scope.
 */
v9x_status v9x_tdfx_3d_build_state(const struct v9x_tdfx_3d_state *state,
                                   struct v9x_tdfx_3d_words *words);

/*
 * One independent triangle through the setup unit, appended to words:
 * sSetupMode, then per vertex its parameters and sBeginTriCMD (first) or
 * sDrawTriCMD. The parameters sent follow the state: Z with depth, 1/W and
 * S/W, T/W with a texture. INVALID_ARGUMENT when words has no room.
 */
v9x_status v9x_tdfx_3d_build_triangle(const struct v9x_tdfx_3d_state *state,
                                      const struct v9x_tdfx_3d_vertex *v,
                                      struct v9x_tdfx_3d_words *words);

/* The IEEE single bits of f, as the setup registers take them. */
v9x_u32 v9x_tdfx_3d_float_bits(float f);

#endif
