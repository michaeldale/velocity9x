/*
 * SiS 6326 3D engine: register values for one triangle.
 *
 * Pure policy, no I/O, integer-only so it builds into tools with no float
 * runtime. The vertices arrive as IEEE single bit patterns (what Direct3D's
 * TLVERTEX holds); the builder sorts them by Y and names the order in the
 * primitive word, because the setup engine does not sort (datasheet 7.14.4;
 * docs\decisions\2026-10-04-sis6326-3d-state-under-sis-hal.md).
 *
 * Register meanings and citations: docs\specifications\sis6326-registers.md
 * sections 6-8.
 */
#ifndef VELOCITY9X_SIS6326_3D_H
#define VELOCITY9X_SIS6326_3D_H

#include "velocity9x/status.h"

/* Vertex registers: A at 8800h, B at 8820h, C at 8840h, 32 bytes each. */
#define V9X_SIS3D_VERTEX_A       0x8800ul
#define V9X_SIS3D_VERTEX_STRIDE  0x0020ul
#define V9X_SIS3D_VERTEX_FS      0x00ul
#define V9X_SIS3D_VERTEX_Z       0x04ul
#define V9X_SIS3D_VERTEX_X       0x08ul
#define V9X_SIS3D_VERTEX_Y       0x0cul
#define V9X_SIS3D_VERTEX_ARGB    0x10ul
#define V9X_SIS3D_VERTEX_U       0x14ul
#define V9X_SIS3D_VERTEX_V       0x18ul
#define V9X_SIS3D_VERTEX_W       0x1cul

#define V9X_SIS3D_PRIMITIVE      0x89f8ul
#define V9X_SIS3D_STATUS         0x89fcul
#define V9X_SIS3D_ENABLE         0x8a00ul
#define V9X_SIS3D_Z_SET          0x8a04ul
#define V9X_SIS3D_Z_BASE         0x8a08ul
#define V9X_SIS3D_ALPHA_SET      0x8a0cul
#define V9X_SIS3D_DST_SET        0x8a14ul
#define V9X_SIS3D_DST_BASE       0x8a18ul
#define V9X_SIS3D_FOG            0x8a20ul
#define V9X_SIS3D_BLEND          0x8a28ul
#define V9X_SIS3D_CLIP_TB        0x8a30ul
#define V9X_SIS3D_CLIP_LR        0x8a34ul
#define V9X_SIS3D_TEXTURE_SET    0x8a38ul
#define V9X_SIS3D_TEXTURE_BLEND  0x8a3cul
#define V9X_SIS3D_TEXTURE_BASE0  0x8a44ul
#define V9X_SIS3D_TEXTURE_PITCH01 0x8a6cul
#define V9X_SIS3D_TEXTURE_SIZE   0x8a80ul

/* 89FCh read: D1 engine idle and 3D queue empty, D0 engine idle. */
#define V9X_SIS3D_STATUS_IDLE_EMPTY 0x00000002ul
#define V9X_SIS3D_STATUS_IDLE       0x00000001ul

/* 8A00h enable bits (datasheet 7.14.6). */
#define V9X_SIS3D_ENABLE_DITHER     0x00000001ul
#define V9X_SIS3D_ENABLE_BLEND      0x00000004ul
#define V9X_SIS3D_ENABLE_LARGE_CACHE 0x00000020ul
#define V9X_SIS3D_ENABLE_TEXTURE_CACHE 0x00000080ul
#define V9X_SIS3D_ENABLE_PERSPECTIVE 0x00000200ul
#define V9X_SIS3D_ENABLE_TEXTURE    0x00000400ul
#define V9X_SIS3D_ENABLE_PRIM_SETUP 0x00000800ul
/* Reserved in the datasheet; SiS's HAL sets it with texturing on. */
#define V9X_SIS3D_ENABLE_BIT15      0x00008000ul
#define V9X_SIS3D_ENABLE_ALPHA_TEST 0x00020000ul
#define V9X_SIS3D_ENABLE_Z_TEST     0x00100000ul
#define V9X_SIS3D_ENABLE_Z_WRITE    0x00200000ul

/* Compare functions, shared by the Z test (8A04h D[18:16]) and the alpha
 * test (8A0Ch D[26:24]) (7.14.7-8). */
#define V9X_SIS3D_CMP_NEVER         0ul
#define V9X_SIS3D_CMP_LESS          1ul
#define V9X_SIS3D_CMP_EQUAL         2ul
#define V9X_SIS3D_CMP_LEQUAL        3ul
#define V9X_SIS3D_CMP_GREATER       4ul
#define V9X_SIS3D_CMP_NOTEQUAL      5ul
#define V9X_SIS3D_CMP_GEQUAL        6ul
#define V9X_SIS3D_CMP_ALWAYS        7ul

/* 8A04h D[21:20] = 01: Z16. */
#define V9X_SIS3D_Z16               0x00100000ul

/* Blend factors, 8A28h: destination in D[31:28], source in D[27:24]
 * (7.14.12). Source codes 2 and 3 are reserved; destination codes above
 * 7 are reserved. */
#define V9X_SIS3D_BLEND_ZERO          0ul
#define V9X_SIS3D_BLEND_ONE           1ul
#define V9X_SIS3D_BLEND_SRC_COLOR     2ul
#define V9X_SIS3D_BLEND_INV_SRC_COLOR 3ul
#define V9X_SIS3D_BLEND_SRC_ALPHA     4ul
#define V9X_SIS3D_BLEND_INV_SRC_ALPHA 5ul
#define V9X_SIS3D_BLEND_DST_ALPHA     6ul
#define V9X_SIS3D_BLEND_INV_DST_ALPHA 7ul
#define V9X_SIS3D_BLEND_DST_COLOR     8ul
#define V9X_SIS3D_BLEND_INV_DST_COLOR 9ul
#define V9X_SIS3D_BLEND_SRC_ALPHA_SAT 10ul
#define V9X_SIS3D_BLEND_BOTH_SRC_ALPHA 11ul
#define V9X_SIS3D_BLEND_BOTH_INV_SRC_ALPHA 12ul

/* Texel formats, 8A38h D[31:24] (registers section 8.1). */
#define V9X_SIS3D_TEXEL_RGB555      0x50ul
#define V9X_SIS3D_TEXEL_RGB565      0x51ul
#define V9X_SIS3D_TEXEL_ARGB1555    0x52ul
#define V9X_SIS3D_TEXEL_ARGB4444    0x53ul
#define V9X_SIS3D_TEXEL_ARGB8888    0x73ul

/* 8A38h D[23:16] mapping: wrap beats mirror beats clamp. */
#define V9X_SIS3D_TEXTURE_WRAP_U    0x01ul
#define V9X_SIS3D_TEXTURE_WRAP_V    0x02ul
#define V9X_SIS3D_TEXTURE_MIRROR_U  0x04ul
#define V9X_SIS3D_TEXTURE_MIRROR_V  0x08ul
#define V9X_SIS3D_TEXTURE_CLAMP_U   0x10ul
#define V9X_SIS3D_TEXTURE_CLAMP_V   0x20ul

/* 8A38h D3 magnification, D[2:0] minification. */
#define V9X_SIS3D_MAG_LINEAR        0x08ul
#define V9X_SIS3D_MIN_NEAREST       0ul
#define V9X_SIS3D_MIN_LINEAR        1ul
#define V9X_SIS3D_MIN_NEAREST_MIP_NEAREST 2ul
#define V9X_SIS3D_MIN_LINEAR_MIP_LINEAR 5ul

/* 8A3Ch D[31:26] colour mode and D[25:24] alpha mode; only the three
 * unambiguous colour modes are named (registers section 8). */
#define V9X_SIS3D_TBLEND_CTEX       0x00ul
#define V9X_SIS3D_TBLEND_CPIX       0x01ul
#define V9X_SIS3D_TBLEND_DECALALPHA 0x04ul
#define V9X_SIS3D_TBLEND_ATEX       0ul
#define V9X_SIS3D_TBLEND_APIX       1ul
#define V9X_SIS3D_TBLEND_APIX_ATEX  2ul

/* 89F8h fields (datasheet 7.14.4). */
#define V9X_SIS3D_DRAW_TRIANGLE     0x00000002ul
#define V9X_SIS3D_DIRECTION_BIT     0x00000080ul
#define V9X_SIS3D_FIRE_SHIFT        8
#define V9X_SIS3D_FIRE_ON_TSWC      6ul
#define V9X_SIS3D_BOTTOM_SHIFT      12
#define V9X_SIS3D_MIDDLE_SHIFT      14
#define V9X_SIS3D_TOP_SHIFT         16
#define V9X_SIS3D_SHADE_SHIFT       18
#define V9X_SIS3D_SHADE_FLAT_TOP    1ul
#define V9X_SIS3D_SHADE_FLAT_MIDDLE 2ul
#define V9X_SIS3D_SHADE_FLAT_BOTTOM 3ul
#define V9X_SIS3D_SHADE_GOURAUD     4ul

/* Destination formats, 8A14h D[22:16]: 16 bpp class, RGB565 (7.14.9). */
#define V9X_SIS3D_DST_RGB565        0x11ul
/* 8A14h D[27:24]: ROP2 COPY_PEN. */
#define V9X_SIS3D_ROP_COPY          0x0cul
#define V9X_SIS3D_DST_PITCH_MAX     0x00003ffful
/* 22-bit local addresses in the 2D engine; the 3D base registers take 23
 * (7.14.7-9), but no Rev. Ax/Bx board carries more than 4 MiB. */
#define V9X_SIS3D_ADDRESS_LIMIT     0x00400000ul
/* 13-bit sign-magnitude clip fields with 12 integer bits (7.14.12). */
#define V9X_SIS3D_CLIP_MAX          0x00000ffful

/* Dwords a triangle's state writes, and its vertex writes. */
#define V9X_SIS3D_STATE_DWORDS      9u
#define V9X_SIS3D_FULL_STATE_DWORDS 10u
#define V9X_SIS3D_VERTEX_DWORDS     24u
#define V9X_SIS3D_TEXTURE_DWORDS    5u

struct v9x_sis3d_vertex {
    v9x_u32 x;      /* IEEE single bits, pixels */
    v9x_u32 y;
    v9x_u32 z;
    v9x_u32 argb;
    v9x_u32 u;
    v9x_u32 v;
    v9x_u32 w;
    v9x_u32 fog_specular;
};

struct v9x_sis3d_target {
    v9x_u32 vram_bytes;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 width;
    v9x_u32 height;
};

struct v9x_sis3d_writes {
    v9x_u32 offsets[V9X_SIS3D_VERTEX_DWORDS];
    v9x_u32 values[V9X_SIS3D_VERTEX_DWORDS];
    v9x_u32 count;
};

/*
 * Everything a draw's state can switch: Gouraud or flat is the primitive
 * word's business, not this. A field whose feature is not enabled is
 * ignored and its register left at a harmless value.
 */
struct v9x_sis3d_state {
    struct v9x_sis3d_target target;
    v9x_u32 enable;          /* V9X_SIS3D_ENABLE_* */
    v9x_u32 z_offset;        /* Z16 buffer, same width and height */
    v9x_u32 z_pitch_bytes;
    v9x_u32 z_compare;       /* V9X_SIS3D_CMP_* */
    v9x_u32 alpha_compare;
    v9x_u32 alpha_reference; /* 0-255 */
    v9x_u32 blend_source;    /* V9X_SIS3D_BLEND_* */
    v9x_u32 blend_destination;
};

/* Levels after level 0: 8A38h D[11:8] holds the last level's index. */
#define V9X_SIS3D_MIP_LEVELS_MAX    9u

/*
 * One texture level. The pitch field is a small float, measured on
 * A8U4I5 on 2026-10-05: exponent D[10:7], mantissa D[6:0], (2m + 1) <<
 * (e + 2) bytes. The engine ORs the row term into the column offset, so the
 * pitch's power-of-two factor must cover the row's bytes; a power-of-two
 * texture at its tight pitch always does.
 */
struct v9x_sis3d_texture {
    v9x_u32 vram_bytes;
    v9x_u32 format;          /* V9X_SIS3D_TEXEL_* */
    v9x_u32 log2_width;      /* 0-9 */
    v9x_u32 log2_height;
    v9x_u32 levels;          /* 8A38h D[11:8]: last level, 0 single */
    v9x_u32 offset;          /* level 0 */
    v9x_u32 pitch_bytes;
    v9x_u32 mapping;         /* V9X_SIS3D_TEXTURE_WRAP_U ... */
    v9x_u32 filter;          /* V9X_SIS3D_MAG_LINEAR | V9X_SIS3D_MIN_* */
    v9x_u32 colour_mode;     /* V9X_SIS3D_TBLEND_CTEX ... */
    v9x_u32 alpha_mode;      /* V9X_SIS3D_TBLEND_ATEX ... */
    int clear_cache;         /* 8A38h D4 */
    v9x_u32 blend_mask_bit;  /* 8A38h D[14:12]: Atex bit the masked modes read */
    /* Levels 1 to levels, at tight pitches (a row rounded up to 4 bytes). */
    v9x_u32 level_offsets[V9X_SIS3D_MIP_LEVELS_MAX];
};

/* The 11-bit pitch field for a row pitch in bytes. UNSUPPORTED for a
 * pitch it cannot express: not 4 x odd x 2^k with the odd factor up to
 * 255 and k up to 15. */
v9x_status v9x_sis3d_texture_pitch_field(v9x_u32 pitch_bytes,
                                         v9x_u32 *field);

/* Texture set, blend, level 0 base and pitch, size: five dwords. Refuses a
 * format whose texel size it does not know (UNSUPPORTED), a level past
 * VRAM, and a pitch the engine would misaddress. */
v9x_status v9x_sis3d_build_texture(const struct v9x_sis3d_texture *texture,
                                   struct v9x_sis3d_writes *writes);

/* q / 2^fraction_bits as IEEE single bits, exact for |q| below 2^24 and
 * fraction_bits 0-30. */
v9x_u32 v9x_sis3d_float_fixed(v9x_s32 q, int fraction_bits);

/* q / 16 as IEEE single bits, exact for |q| below 2^24. */
v9x_u32 v9x_sis3d_float_q4(v9x_s32 q);

/* The whole state, enable word first and clip last. Refuses a Z buffer or
 * target outside VRAM or the address range, a pitch past its field, and a
 * reserved blend factor or compare code. */
v9x_status v9x_sis3d_build_state(const struct v9x_sis3d_state *state,
                                 struct v9x_sis3d_writes *writes);

/* Indices of the top, middle and bottom vertex by Y; ties keep input
 * order. */
void v9x_sis3d_order(const struct v9x_sis3d_vertex *vertices,
                     v9x_u32 *top, v9x_u32 *middle, v9x_u32 *bottom);

/* The 89F8h word for a triangle, firing on the write of vertex C's W. */
v9x_u32 v9x_sis3d_primitive(const struct v9x_sis3d_vertex *vertices,
                            v9x_u32 shade, int direction);

/* Flat untextured state into an RGB565 target: no Z, no alpha test, no
 * blend beyond ONE/ZERO, no fog, clip to the target. */
v9x_status v9x_sis3d_build_flat_state(const struct v9x_sis3d_target *target,
                                      struct v9x_sis3d_writes *writes);

/* The 24 vertex dwords, A then B then C, vertex C's W last (the fire). */
void v9x_sis3d_build_vertices(const struct v9x_sis3d_vertex *vertices,
                              struct v9x_sis3d_writes *writes);

#endif
