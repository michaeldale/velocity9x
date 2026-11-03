/*
 * NVIDIA RIVA 128 (NV3) 3D: the D3D object (class 17h, envytools' D3D,
 * "Direct3D 5 textured triangle with zeta buffer") as RAMIN, register and
 * method writes.
 *
 * Pure policy, integer only, no I/O, like nv3_2d.h: the HAL writes what
 * these builders return. Vertices arrive as Direct3D's TLVERTEX holds them,
 * IEEE single bit patterns and ARGB words, and go to the chip unconverted:
 * it converts them itself (envytools nvhw/pgraph_d3d_nv3.c).
 *
 * Everything below was drawn on M-7V636C by V9XN32D /d3d and /d3dstate
 * unless its comment says otherwise:
 * docs\decisions\2026-11-03-riva128-first-triangle.md (the object, the
 * trigger, modulation, fog) and docs\decisions\2026-11-03-riva128-d3d-state.md
 * (swizzle, filters, perspective, Z, edges, alpha test, blend, colour key,
 * the whole-memory DMA object).
 */
#ifndef VELOCITY9X_NV3_3D_H
#define VELOCITY9X_NV3_3D_H

#include "velocity9x/status.h"
#include "velocity9x/nv3_2d.h"

/*
 * The object, from rivafb's tables (riva_tbl.h): RAMHT entry 80000013h
 * with context 00D70347h (type 17h, instance 347h), instance words
 * 10419208h (X1R5G5B5, zeta enabled, destination surface 2 with zeta in
 * surface 3) and the DMA object's instance, bound to subchannel 7.
 */
#define V9X_NV3_SUB_D3D             7ul
#define V9X_NV3_D3D_HANDLE          0x80000013ul
#define V9X_NV3_D3D_RAMHT           0x0930ul
#define V9X_NV3_D3D_CONTEXT         0x00d70347ul
#define V9X_NV3_D3D_INSTANCE        0x3470ul
#define V9X_NV3_D3D_OPTIONS         0x10419208ul
#define V9X_NV3_D3D_SURFACE_COLOUR  2ul
#define V9X_NV3_D3D_SURFACE_ZETA    3ul

/*
 * The DMA object textures are read through: flags (bit 16, page table
 * present; bits 24-25, target 0, video memory; 86Box vid_nv3.h), the
 * limit, then one entry per 4 KiB page, (video memory offset) | 3. One
 * object covers all of video memory at RAMIN 4000h, clear of RAMHT (0),
 * RAMRO (2000h), RAMFC (2200h) and the instances (3400h); 8 MiB is 800h
 * entries, to 6007h. A texture offset is then its video memory offset.
 */
#define V9X_NV3_D3D_DMA             0x4000ul
#define V9X_NV3_D3D_DMA_INSTANCE    0x0400ul
#define V9X_NV3_D3D_DMA_FLAGS       0x00010000ul
#define V9X_NV3_D3D_DMA_PAGE        0x1000ul
#define V9X_NV3_D3D_DMA_PRESENT_RW  0x00000003ul
#define V9X_NV3_D3D_DMA_ENTRIES_MAX 0x800ul

/*
 * The texture an untextured draw samples: 8x8 A1R5G5B5 white, the size
 * the probes drew, in the lowest 128 bytes of RAMIN's video memory (RAMIN
 * 7F80h-7FFFh, which nothing else uses). Every texel is FFFFh, so RAMIN's
 * 16-byte reversal does not matter and it is written straight through the
 * framebuffer at RAMIN's start.
 */
#define V9X_NV3_D3D_WHITE_BYTES     0x80ul
#define V9X_NV3_D3D_WHITE_LOG2      3ul

/* Methods (nv3_3d.xml). */
#define V9X_NV3_M_D3D_TEX_OFFSET    0x304ul
#define V9X_NV3_M_D3D_TEX_FORMAT    0x308ul
#define V9X_NV3_M_D3D_FILTER        0x30cul
#define V9X_NV3_M_D3D_FOG_COLOUR    0x310ul
#define V9X_NV3_M_D3D_CONFIG        0x314ul
#define V9X_NV3_M_D3D_ALPHA         0x318ul
#define V9X_NV3_M_D3D_VERTEX        0x1000ul
#define V9X_NV3_D3D_VERTEX_METHODS  8u

/*
 * TEXTURE_FORMAT: colour key in 15:0 and its enable in bit 16, the texel
 * format in 21:20, then log2 sizes in 27:24 and 31:28. Textures are square:
 * envytools converts both u and v with the 31:28 size (convert_uv), and
 * hwtest allows 0-11 (up to 2048). 27:24 is taken as the smallest mipmap
 * level; only equal sizes, one level, were drawn.
 */
#define V9X_NV3_TEX_KEY_MASK        0x0000fffful
#define V9X_NV3_TEX_KEY_ENABLE      0x00010000ul
#define V9X_NV3_TEX_COLOUR_SHIFT    20u
#define V9X_NV3_TEX_SIZE_MIN_SHIFT  24u
#define V9X_NV3_TEX_SIZE_MAX_SHIFT  28u
#define V9X_NV3_TEX_LOG2_MAX        11ul
/* Texel formats. Only A1R5G5B5 was drawn; X1R5G5B5 is the same bits read
 * with alpha 1, which is how an opaque draw keeps A1R5G5B5 texels opaque. */
#define V9X_NV3_TEX_A1R5G5B5        0ul
#define V9X_NV3_TEX_X1R5G5B5        1ul
#define V9X_NV3_TEX_A4R4G4B4        2ul
#define V9X_NV3_TEX_R5G6B5          3ul

/* CONFIG fields (nv3_3d.xml; hwtest's valid mask F77FBDF3h). */
#define V9X_NV3_CFG_INTERP_ZOH      0x00000000ul /* point, as Direct3D */
#define V9X_NV3_CFG_INTERP_FOH      0x00000002ul /* bilinear */
#define V9X_NV3_CFG_WRAP_U_SHIFT    4u
#define V9X_NV3_CFG_WRAP_V_SHIFT    6u
#define V9X_NV3_CFG_SOURCE_NORMAL   0x00000100ul /* texture x colour */
#define V9X_NV3_CFG_CULL_NONE       0x00001000ul
#define V9X_NV3_CFG_ZFUNC_SHIFT     16u
#define V9X_NV3_CFG_ZWRITE_SHIFT    20u
#define V9X_NV3_CFG_CWRITE_SHIFT    24u
#define V9X_NV3_CFG_ROP_ADD         0x10000000ul
#define V9X_NV3_CFG_BETA_DSTCOLOUR  0x20000000ul
#define V9X_NV3_CFG_DST_ZERO        0x40000000ul
#define V9X_NV3_CFG_SRC_ZERO        0x80000000ul

/* Write enables, for zeta (22:20) and colour (26:24). */
#define V9X_NV3_WREN_NEVER          0ul
#define V9X_NV3_WREN_ALPHA          1ul
#define V9X_NV3_WREN_ALPHA_AND_Z    2ul
#define V9X_NV3_WREN_Z              3ul
#define V9X_NV3_WREN_ALWAYS         4ul

/* Comparisons, for Z (CONFIG 19:16) and alpha (ALPHA 11:8): Direct3D's
 * D3DCMP values, NEVER 1 to ALWAYS 8, compared in Direct3D's sense
 * (LESS and GREATER measured; the rest from nv3_3d.xml). */
#define V9X_NV3_CMP_NEVER           1ul
#define V9X_NV3_CMP_LESS            2ul
#define V9X_NV3_CMP_ALWAYS          8ul
#define V9X_NV3_ALPHA_FUNC_SHIFT    8u

/* Wrap modes: Direct3D's D3DTADDRESS values, WRAP 1, MIRROR 2, CLAMP 3
 * (nv3_3d.xml; only WRAP drawn). */
#define V9X_NV3_WRAP                1ul
#define V9X_NV3_CLAMP               3ul

/* Zeta holds z x 65536, not inverted; FFFFh is far. */
#define V9X_NV3_ZETA_FAR            0x0000fffful

/*
 * The specular word: the slot the vertex is stored into in 3:0, two
 * triangle triples in 11:0 (slots 0-2) and 23:12 (slots 3-5), each drawn
 * when its three slots differ, and the fog amount in 31:24 (0 none, FFh
 * all fog colour: the reverse of Direct3D's specular alpha).
 * envytools nvhw/pgraph.c pgraph_prep_draw.
 *
 * The builders take the vertex's slot and "others": null to store it and
 * draw nothing (every nibble the slot, as the probe's v0 and v1), or the
 * two other slots of the triangle this vertex completes, which make the
 * first triple; the second triple repeats the slot and never draws.
 */
#define V9X_NV3_SLOTS               16ul
#define V9X_NV3_FOG_SHIFT           24u

/* Direct3D's blend pairs the chip has. The chip's blend is
 * dst x (1 - beta) + src x beta, with beta from source alpha or the
 * destination colour and either side zeroable, or a saturating add. */
#define V9X_NV3_BLEND_OPAQUE        0ul /* ONE, ZERO */
#define V9X_NV3_BLEND_SRC_ALPHA     1ul /* SRCALPHA, INVSRCALPHA */
#define V9X_NV3_BLEND_ADD           2ul /* ONE, ONE */
#define V9X_NV3_BLEND_MODULATE      3ul /* DESTCOLOR, ZERO */

#define V9X_NV3_FILTER_POINT        0ul
#define V9X_NV3_FILTER_LINEAR       1ul

/* One draw's Direct3D state, already in the chip's vocabulary where the
 * two agree (comparisons, wrap modes). */
struct v9x_nv3_3d_state {
    v9x_u32 vram_bytes;
    v9x_u32 texture_offset;    /* video memory offset, swizzled texels */
    v9x_u32 texture_format;    /* V9X_NV3_TEX_* */
    v9x_u32 texture_log2;      /* the square texture's side, 0-11 */
    int colour_key;
    v9x_u32 colour_key_texel;  /* in the texture's own format */
    v9x_u32 filter;            /* V9X_NV3_FILTER_* */
    v9x_u32 wrap_u;            /* D3DTADDRESS */
    v9x_u32 wrap_v;
    int z_enable;
    int z_write;
    v9x_u32 z_func;            /* D3DCMP */
    int alpha_test;
    v9x_u32 alpha_func;        /* D3DCMP */
    v9x_u32 alpha_reference;   /* 0-255 */
    v9x_u32 blend;             /* V9X_NV3_BLEND_* */
    int fog;
    v9x_u32 fog_colour;        /* 0x00RRGGBB */
    /*
     * A Direct3D texture narrower or shorter than the square the chip
     * samples is stored repeated across it (the HAL's swizzle), and its
     * coordinates scaled by width / side and height / side, powers of two
     * given here as shifts: u 0 to 1 then spans one copy, and wrapping
     * past it lands in the next copy, which holds the same texels. Zero
     * for a square texture.
     */
    v9x_u32 u_shift;
    v9x_u32 v_shift;
    /* Direct3D's DECAL and COPY: the texture alone, so every vertex colour
     * is made white and opaque, which the modulation then ignores. */
    int vertex_white;
};

/*
 * The six state methods, in the order they go, plus what each vertex of
 * the draw needs: bits ORed into its colour (alpha FFh for an opaque draw,
 * whose blend is source alpha with alpha made 1; all of it for
 * vertex_white), whether its fog byte is used, and the coordinate shifts.
 */
struct v9x_nv3_3d_words {
    struct v9x_nv3_write methods[6];
    v9x_u32 colour_or;
    int fog;
    v9x_u32 u_shift;
    v9x_u32 v_shift;
};

struct v9x_nv3_3d_vertex {
    v9x_u32 sx;        /* IEEE single bits, Direct3D's TLVERTEX */
    v9x_u32 sy;
    v9x_u32 sz;
    v9x_u32 rhw;
    v9x_u32 colour;    /* ARGB */
    v9x_u32 specular;  /* ARGB; alpha is Direct3D's fog factor */
    v9x_u32 tu;
    v9x_u32 tv;
};

/* The object's RAMIN writes and the DMA object's header; its page entries
 * come from v9x_nv3_3d_dma_entry, one per page up to entry_count. */
#define V9X_NV3_3D_OBJECT_WRITES    6u

struct v9x_nv3_3d_object {
    struct v9x_nv3_write ramin[V9X_NV3_3D_OBJECT_WRITES];
    struct v9x_nv3_write bind;
    v9x_u32 ramin_count;
    v9x_u32 entry_count;
};

v9x_status v9x_nv3_3d_build_object(v9x_u32 vram_bytes,
                                   struct v9x_nv3_3d_object *object);
struct v9x_nv3_write v9x_nv3_3d_dma_entry(v9x_u32 page);
/*
 * The render target: PGRAPH surfaces 2 (colour) and 3 (zeta), written
 * while the engine is idle, and the clip over the target's width and
 * height. The 2D fills and copies move the clip, so a 3D draw after them
 * sets it again. The probe drew inside a clip of the surface's size; that
 * the D3D object honours the clip is envytools' pgraph_prep_draw, not
 * measured.
 */
struct v9x_nv3_3d_target {
    v9x_u32 vram_bytes;
    v9x_u32 colour_offset;
    v9x_u32 colour_pitch_bytes;
    v9x_u32 zeta_offset;
    v9x_u32 zeta_pitch_bytes;
    v9x_u32 width;
    v9x_u32 height;
};

v9x_status v9x_nv3_3d_build_target(const struct v9x_nv3_3d_target *target,
                                   struct v9x_nv3_op *op);
v9x_status v9x_nv3_3d_build_state(const struct v9x_nv3_3d_state *state,
                                  struct v9x_nv3_3d_words *words);
v9x_u32 v9x_nv3_3d_specular(v9x_u32 slot, const v9x_u32 *others,
                            const struct v9x_nv3_3d_words *words,
                            v9x_u32 d3d_specular);
v9x_status v9x_nv3_3d_build_vertex(const struct v9x_nv3_3d_vertex *vertex,
                                   v9x_u32 slot, const v9x_u32 *others,
                                   const struct v9x_nv3_3d_words *words,
                                   struct v9x_nv3_write
                                       methods[V9X_NV3_D3D_VERTEX_METHODS]);
v9x_u32 v9x_nv3_3d_swizzle(v9x_u32 u, v9x_u32 v, v9x_u32 log2_size);

/*
 * An A4R4G4B4 texel as X1R5G5B5, for the opaque copy the HAL keeps of
 * such a texture: build_state refuses an opaque draw from A4R4G4B4, since
 * the blend cannot ignore its partial alphas, and X1R5G5B5 reads alpha 1
 * (/d3dstate X1AlphaZero, boot 20).
 */
v9x_u16 v9x_nv3_3d_opaque_texel(v9x_u16 argb4444);

#endif
