/*
 * Matrox MGA-2164W 3D: trapezoids for the Direct3D engine.
 *
 * Phase 1 of docs\plans\matrox-mga2164w-hardware-3d.md: flat and Gouraud
 * trapezoids, no Z, no texture. Pure policy, no I/O, as mga_engine.h: the
 * caller writes what v9x_mga3d_build_trap returns, every dword in order,
 * the last to its +100h mirror. Register meanings and page citations are
 * in docs\specifications\mga2164w-3d-engine.md (the MGA-2164W Developer
 * Specification, whose trapezoid section is 4.5.5).
 *
 * v9x_mga3d_model_trap is the other half: what this module believes the
 * engine draws for the same trapezoid. The write probe (MGA2D.EXE /tri)
 * compares the two on the card; until a decision record says they agree,
 * the model is a hypothesis taken from the specification's edge formulas
 * and 86Box's emulation of them.
 *
 * Only the 2164W gets 3D. Nothing here checks the chip; the caller does.
 */
#ifndef VELOCITY9X_MGA_3D_H
#define VELOCITY9X_MGA_3D_H

#include "velocity9x/mga_engine.h"

/* Edge and interpolant registers (spec 2-6..2-7, 3-23..3-44). AR0, AR5,
 * DWGCTL, FCOL, SGN, FXBNDRY, YDSTLEN, PITCH and YDSTORG are in
 * mga_engine.h. */
#define V9X_MGA_AR1   0x1c64ul
#define V9X_MGA_AR2   0x1c68ul
#define V9X_MGA_AR4   0x1c70ul
#define V9X_MGA_AR6   0x1c78ul
#define V9X_MGA_DR4   0x1cd0ul
#define V9X_MGA_DR6   0x1cd8ul
#define V9X_MGA_DR7   0x1cdcul
#define V9X_MGA_DR8   0x1ce0ul
#define V9X_MGA_DR10  0x1ce8ul
#define V9X_MGA_DR11  0x1cecul
#define V9X_MGA_DR12  0x1cf0ul
#define V9X_MGA_DR14  0x1cf8ul
#define V9X_MGA_DR15  0x1cfcul

/* Depth (spec 2-11, 3-39..3-44, 3-70, 3-91): ZORG, the 17.15 interpolants
 * DR0/DR2/DR3 for 16-bit Z, and the 33.15 pairs for 32-bit Z in the second
 * drawing-register block, which only the 2164W has. */
#define V9X_MGA_ZORG          0x1c0cul
#define V9X_MGA_DR0           0x1cc0ul
#define V9X_MGA_DR2           0x1cc8ul
#define V9X_MGA_DR3           0x1cccul
#define V9X_MGA_DR0_Z32_LSB   0x2c50ul
#define V9X_MGA_DR0_Z32_MSB   0x2c54ul
#define V9X_MGA_DR2_Z32_LSB   0x2c60ul
#define V9X_MGA_DR2_Z32_MSB   0x2c64ul
#define V9X_MGA_DR3_Z32_LSB   0x2c68ul
#define V9X_MGA_DR3_Z32_MSB   0x2c6cul

/* Colour interpolants are signed 9.15 in <23:0> (spec 3-45..3-53): one
 * colour level is 1 << 15. Depth is 17.15 (16-bit Z) or 33.15 (32-bit
 * Z): one depth unit is also 1 << 15. */
#define V9X_MGA3D_COLOR_ONE 0x00008000L
#define V9X_MGA3D_Z_ONE     0x00008000L

#define V9X_MGA3D_MAX_WRITES 48u

/*
 * Texture registers, NOT in Matrox's public 2164W specification, which
 * leaves 2C00h-2C34h out of its register map. Offsets and fields are
 * hypotheses from 86Box's vid_mga.c (docs\specifications\
 * mga2164w-3d-engine.md section 10) until a decision record measures them:
 * phase 3 of docs\plans\matrox-mga2164w-hardware-3d.md.
 */
#define V9X_MGA_TMR0       0x2c00ul
#define V9X_MGA_TEXORG     0x2c24ul
#define V9X_MGA_TEXWIDTH   0x2c28ul
#define V9X_MGA_TEXHEIGHT  0x2c2cul
#define V9X_MGA_TEXCTL     0x2c30ul
#define V9X_MGA_TEXTRANS   0x2c34ul

/* TEXCTL texformat<2:0>, hypothesised. */
#define V9X_MGA3D_TEX_TW4  0ul
#define V9X_MGA3D_TEX_TW8  1ul
#define V9X_MGA3D_TEX_TW15 2ul
#define V9X_MGA3D_TEX_TW16 3ul
#define V9X_MGA3D_TEX_TW12 4ul

/*
 * The perspective path takes s and t in eighths of a texel, adds one
 * eighth, and only then divides by q: one eighth of a texel at q = 1, and
 * less as s, t and q are scaled up together
 * (docs\decisions\2026-10-10-mga2164w-d3d-engine.md). A setup that wants
 * floor(s / q) takes the eighth off s and t.
 */
#define V9X_MGA3D_TEX_PERSPECTIVE_BIAS_BITS 3u

/*
 * A texture for a textured trapezoid (opcode 0110, hypothesised). The
 * texels are 1 << log2_width by 1 << log2_height, stored row after row
 * from byte `offset`, 1 << log2_pitch texels to a row (8 to 1024).
 * `perspective` zero sets TEXCTL.npcen: s and t are then linear. `tmr` is
 * TMR0-TMR8 as written - s, t, q per pixel (0, 2, 4), per row (1, 3, 5)
 * and at the left edge of the first row (6, 7, 8) - with s and t scaled so
 * that 1 << 20 spans the texture, and q 16.16. `modulate` multiplies the
 * texel by the Gouraud colour; otherwise the texel replaces it (decal).
 * A texel whose bits under key_mask equal key is transparent (TEXTRANS).
 * alpha_mask and alpha_key are TEXCTL's tamask and takey: in decal, a
 * texel whose alpha under the mask equals the key shows the Gouraud
 * colour instead. With the mask 0 every texel does. TW16 has no alpha bit
 * and counts as 0, so its texels show in decal only with both set
 * (docs\decisions\2026-10-10-mga2164w-textures.md).
 */
struct v9x_mga3d_texture {
    v9x_u32 enabled;
    v9x_u32 offset;
    v9x_u32 format;
    v9x_u32 log2_width;
    v9x_u32 log2_height;
    v9x_u32 log2_pitch;
    v9x_u32 clamp_u;
    v9x_u32 clamp_v;
    v9x_u32 modulate;
    v9x_u32 perspective;
    v9x_u32 key;
    v9x_u32 key_mask;
    v9x_u32 alpha_mask;
    v9x_u32 alpha_key;
    v9x_s32 tmr[9];
};

/* The model's view of texture memory: the texel bits at (s, t), already
 * wrapped or clamped into the texture. */
typedef v9x_u32 (*v9x_mga3d_texel_fn)(void *context, v9x_u32 s, v9x_u32 t);

/* Depth buffer width, MACCESS.zwidth (3-70). */
#define V9X_MGA3D_DEPTH_NONE 0ul
#define V9X_MGA3D_DEPTH_16   1ul
#define V9X_MGA3D_DEPTH_32   2ul

/* DWGCTL zmode<10:8> (3-56); 001 is reserved. */
#define V9X_MGA3D_ZMODE_NOZCMP 0ul
#define V9X_MGA3D_ZMODE_ZE     2ul
#define V9X_MGA3D_ZMODE_ZNE    3ul
#define V9X_MGA3D_ZMODE_ZLT    4ul
#define V9X_MGA3D_ZMODE_ZLTE   5ul
#define V9X_MGA3D_ZMODE_ZGT    6ul
#define V9X_MGA3D_ZMODE_ZGTE   7ul

/* A 48-bit signed 33.15 depth value: hi carries bits 47:32 (sign
 * extended), lo bits 31:0. The 32-bit Z interpolants are this wide. */
struct v9x_mga3d_z48 {
    v9x_s32 hi;
    v9x_u32 lo;
};

struct v9x_mga3d_writes {
    v9x_u32 offsets[V9X_MGA3D_MAX_WRITES];
    v9x_u32 values[V9X_MGA3D_MAX_WRITES];
    v9x_u32 count;
};

/*
 * One edge: it starts at column x on the trapezoid's first row and moves dx
 * columns over dy rows (dy >= 1), stepped by the engine's integer
 * Bresenham. The left edge's column is drawn; the right edge's is not
 * (spec 4-34: "the bottom and right edges exist just beyond the object's
 * extents").
 */
struct v9x_mga3d_edge {
    v9x_s32 x;
    v9x_s32 dx;
    v9x_s32 dy;
    /*
     * Added to the specification's starting error term (AR1 or AR4). Zero
     * gives the specification's line from a pixel boundary; triangle setup
     * (mga_setup.c) uses it to carry the first row's rounding, so the walk
     * lands on the exact edge on every row. |error_bias| < dy.
     */
    v9x_s32 error_bias;
};

#define V9X_MGA3D_SHADE_FLAT    0ul
#define V9X_MGA3D_SHADE_GOURAUD 1ul

/*
 * Rows top .. top + length - 1 of a destination surface. Flat: every pixel
 * is `color`. Gouraud: red, green and blue each start at [0] on the left
 * edge of the first row and change by [1] per pixel and [2] per row
 * (signed 9.15); `color` is FCOL, whose top byte is the alpha stored at
 * 32 bpp (spec 3-62). Without depth, Gouraud is atype I with zmode NOZCMP:
 * the depth unit compares nothing and writes nothing (spec 3-56).
 *
 * Depth needs Gouraud. `depth` selects 16- or 32-bit Z; `zmode` the
 * compare, new against stored; `z_write` atype ZI (write Z where the
 * compare passes) rather than I (compare only). The Z buffer has the colour
 * surface's pitch in pixels, at 2 or 4 bytes each, and `z_offset` is the
 * byte offset in VRAM of the Z value for the surface's first pixel: the
 * builder turns that into ZORG, which the engine adds to the pixel's linear
 * address times the Z width (3-91). Depth runs like colour: z (16-bit) or
 * z32 (32-bit) start on the left edge of the first row and change by [1]
 * per pixel and [2] per row.
 */
struct v9x_mga3d_trap {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 top;
    v9x_u32 length;
    struct v9x_mga3d_edge left;
    struct v9x_mga3d_edge right;
    v9x_u32 shade;
    v9x_u32 color;
    v9x_s32 red[3];
    v9x_s32 green[3];
    v9x_s32 blue[3];
    v9x_u32 depth;
    v9x_u32 zmode;
    v9x_u32 z_write;
    v9x_u32 z_offset;
    v9x_s32 z[3];
    struct v9x_mga3d_z48 z32[3];
    struct v9x_mga3d_texture texture;
    /*
     * DWGCTL trans <23:20>: the 4 x 4 screen-door pattern a shaded or
     * textured trapezoid writes through, 0 for every pixel (2164W spec
     * 3-59; the patterns are decoded in mga2164w-3d-engine.md). The engine
     * model does not draw a pattern - where its grid falls on screen is not
     * measured - and refuses one.
     */
    v9x_u32 trans;
    /* MACCESS dit555 <31>: dither shading for a 5:5:5 target rather than
     * 5:6:5 (3-70). */
    v9x_u32 dither_555;
};

/* The model's view of the Z buffer: read the stored value at (x, row) of
 * the trapezoid, and write one. Values are the stored 16 or 32 bits. */
struct v9x_mga3d_depth_io {
    v9x_u32 (*read)(void *context, v9x_s32 x, v9x_u32 row);
    void (*write)(void *context, v9x_s32 x, v9x_u32 row, v9x_u32 value);
    void *context;
    /* Texels, for a textured trapezoid; may be 0 otherwise. */
    v9x_mga3d_texel_fn texel;
    void *texel_context;
};

/*
 * How the per-row colour increment is taken, the question phase 1 asks.
 * EDGE: the start value follows the left edge, so a row whose edge moved
 * dx columns also gains dx times the per-pixel step (86Box's model).
 * NONE: the start value gains only the per-row step.
 */
#define V9X_MGA3D_FOLD_NONE 0ul
#define V9X_MGA3D_FOLD_EDGE 1ul

typedef void (*v9x_mga3d_plot_fn)(void *context, v9x_s32 x, v9x_u32 row,
                                  v9x_u32 pixel);

/* The register writes for one trapezoid. OK; INVALID_ARGUMENT for one the
 * surface does not hold; UNSUPPORTED for one the engine's fields or the
 * linearizer cannot express. */
v9x_status v9x_mga3d_build_trap(const struct v9x_mga3d_trap *trap,
                                struct v9x_mga3d_writes *writes);

/* The pixels the engine is believed to draw for the trapezoid, row by row
 * from 0, left to right. Gouraud pixels are given at 32 bpp only, as
 * 8:8:8 with FCOL's alpha; other depths return UNSUPPORTED for Gouraud,
 * since 16 bpp shading is dithered (spec 3-70). With depth, `depth_io`
 * must be given: each pixel is compared with the stored value it reads,
 * plotted only where the compare passes, and its Z written there when
 * z_write is set. A stored Z is the interpolant's integer part, clamped
 * to 0 below and to the width's maximum above, and compares are unsigned
 * (docs\decisions\2026-10-10-mga2164w-depth.md). Interpolation folds the
 * left edge's steps in, as measured for colour
 * (docs\decisions\2026-10-10-mga2164w-trapezoids.md). */
v9x_status v9x_mga3d_model_trap(const struct v9x_mga3d_trap *trap,
                                v9x_u32 fold, v9x_mga3d_plot_fn plot,
                                void *context,
                                const struct v9x_mga3d_depth_io *depth_io);

#endif
