/*
 * Glide render state to render-interface state (docs\plans\glide-2x-wrapper.md,
 * Phase 1). The DLL keeps every Glide state call here as given, and maps
 * the whole at draw time: depth, blending, alpha test, fog, the clip
 * window as scissor, texture addressing and filtering, and the colour and
 * alpha combine as texture ops plus where the vertex colour comes from.
 *
 * The combine is recognised, not emulated. A setting this module cannot
 * express exactly is drawn with the closest mapping and flagged in
 * `recognized`, so the DLL can log it once. NFS II SE used two settings
 * only, both exact (docs\decisions\2026-10-08-nfs2se-glide-census.md).
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_GLIDE_STATE_H
#define VELOCITY9X_GLIDE_STATE_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"
#include "glide_api.h"

typedef struct v9x_glide_combine {
    v9x_u32 function;
    v9x_u32 factor;
    v9x_u32 local;
    v9x_u32 other;
    v9x_u32 invert;
} V9X_GLIDE_COMBINE;

typedef struct v9x_glide_state {
    v9x_u32 origin;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 depth_mode;
    v9x_u32 depth_func;
    v9x_u32 depth_mask;
    v9x_u32 color_mask;     /* grColorMask's rgb: nonzero writes colour */
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    v9x_u32 blend_src;
    v9x_u32 blend_dst;
    V9X_GLIDE_COMBINE color;
    V9X_GLIDE_COMBINE alpha;
    v9x_u32 tex_rgb_function;
    v9x_u32 tex_alpha_function;
    v9x_u32 cull_mode;
    v9x_u32 fog_mode;
    v9x_u32 fog_color;      /* GrColor_t, ARGB */
    v9x_u32 chroma_mode;
    v9x_u32 chroma_value;   /* GrColor_t, ARGB */
    v9x_u32 constant_color; /* GrColor_t, ARGB */
    v9x_u32 delta0;         /* a _DELTA0 colour preset is in force */
    v9x_u32 delta0_argb;    /* grConstantColorValue4, ARGB */
    v9x_u32 clip_min_x;
    v9x_u32 clip_min_y;
    v9x_u32 clip_max_x;
    v9x_u32 clip_max_y;
    v9x_u32 clamp_s;
    v9x_u32 clamp_t;
    v9x_u32 min_filter;
    v9x_u32 mag_filter;
    v9x_u32 stw_hint;       /* grHints(GR_HINT_STWHINT, mask) */
} V9X_GLIDE_STATE;

typedef struct v9x_glide_draw_setup {
    V9X_R3D_ABI_STATE state;
    v9x_u32 textured;       /* the draw samples the current source */
    v9x_u32 color_op;       /* V9X_R3D_ABI_COLOROP_*, when textured */
    v9x_u32 alpha_op;       /* V9X_R3D_ABI_ALPHAOP_* */
    v9x_u32 address;        /* V9X_R3D_ABI_ADDRESS_* */
    v9x_u32 min_filter;     /* V9X_R3D_ABI_FILTER_* */
    v9x_u32 mag_filter;
    v9x_u32 color_source;   /* V9X_GLIDE_SOURCE_* for the vertex setup */
    v9x_u32 alpha_source;
    v9x_u32 key_texture;    /* convert the texture with the chroma key */
    v9x_u32 key_alpha;      /* alpha_op takes the texture's alpha for the
                               key alone; valid only where the converted
                               alpha is the key (texfmt_key_alpha_only) */
    v9x_u32 recognized;     /* V9X_FALSE: drawn with the closest mapping */
} V9X_GLIDE_DRAW_SETUP;

/*
 * The state before the game sets any. These are this module's choice, not
 * a measured retail default: Gouraud colour and alpha, depth, blending,
 * alpha test, fog, chroma key and culling off, the whole screen as clip
 * window, wrapping bilinear textures. NFS II SE sets everything it uses
 * before its first draw (census).
 */
void v9x_glide_state_init(V9X_GLIDE_STATE *state, v9x_u32 width,
                          v9x_u32 height, v9x_u32 origin);

void v9x_glide_state_map(const V9X_GLIDE_STATE *state,
                         V9X_GLIDE_DRAW_SETUP *out);

/* What grBufferClear writes. */
typedef struct v9x_glide_clear_plan {
    v9x_u32 color;          /* the colour buffer */
    v9x_u32 depth;          /* the aux (depth) buffer */
} V9X_GLIDE_CLEAR_PLAN;

/*
 * grBufferClear honours the write masks, as the Voodoo's fastfill does: the
 * colour buffer while grColorMask lets colour through, the depth buffer
 * while depth buffering is on, there is one (has_aux), and grDepthMask lets
 * depth through.
 */
void v9x_glide_clear_plan(const V9X_GLIDE_STATE *state, int has_aux,
                          V9X_GLIDE_CLEAR_PLAN *out);

/* grSstWinOpen's GrScreenResolution_t as pixels; V9X_FALSE for a value
 * outside the table (GR_RESOLUTION_NONE among them). */
v9x_u16 v9x_glide_resolution_size(v9x_u32 resolution, v9x_u32 *width,
                                  v9x_u32 *height);

/* grBufferClear's or grConstantColorValue's GrColor_t, in the colour
 * format grSstWinOpen named, as 0xAARRGGBB. GR_COLORFORMAT_ARGB (0) is
 * the identity; ABGR (1), RGBA (2) and BGRA (3) are reordered. */
v9x_u32 v9x_glide_color_to_argb(v9x_u32 color, v9x_u32 color_format);

/* The inverse: an ARGB colour in the game's GrColorFormat_t. */
v9x_u32 v9x_glide_argb_to_color(v9x_u32 argb, v9x_u32 color_format);

/* guColorCombineFunction's preset (GrColorCombineFnc_t) as the colour
 * combine grColorCombine would set; V9X_FALSE, and `out` untouched, for a
 * preset this does not know. */
v9x_u16 v9x_glide_gu_color_combine(v9x_u32 preset, V9X_GLIDE_COMBINE *out);

/*
 * Whether guColorCombineFunction's preset turns delta0 on: the iterated
 * colour and alpha become grConstantColorValue4's, flat, in place of the
 * vertex's. That register is not grConstantColorValue's, which the
 * CONSTANT inputs read; one does not overwrite the other. Taken from Dege
 * (dgVoodoo) on VOGONS, who replaces every vertex colour with it under
 * delta0, not from a 3dfx binary.
 */
v9x_u16 v9x_glide_gu_color_delta0(v9x_u32 preset);

/*
 * What a batch the render interface refused tries next, in the ICD's
 * order (gl_icd.c). A surface texture first goes again as its CPU copy:
 * the core gives an engine's software fallback no surface texture, so the
 * copy is what it can draw, fog and all. Then a fogged batch goes in its
 * first form without fog, for an engine with neither fog nor a fallback
 * (the ViRGE). Then nothing. `cpu_tried` is non-zero once the copy was
 * sent; `storage` is the texture's as it was refused.
 */
#define V9X_GLIDE_RETRY_NONE     0ul
#define V9X_GLIDE_RETRY_CPU_COPY 1ul
#define V9X_GLIDE_RETRY_NO_FOG   2ul

v9x_u32 v9x_glide_refusal_next(v9x_u32 result, v9x_u32 storage,
                               v9x_u32 fog_enable, v9x_u32 cpu_tried);

#endif /* VELOCITY9X_GLIDE_STATE_H */
