/*
 * ATI3DCIF rendering-context state to render-interface state
 * (docs\plans\ati-3dcif-wrapper.md, Phase 2). The DLL keeps every
 * ContextSetState value here as given and maps the whole at draw time:
 * depth, blending, fog, the texel op as chroma key or alpha test, texture
 * lighting as the colour op, filtering, the viewport's origin and the
 * viewport and scissor as one scissor.
 *
 * ContextSetState is cheap on purpose: the census measured the Knight
 * demos at 2,350 to 7,300 calls a frame
 * (docs\decisions\2026-10-14-3dcif-demo-census.md), so setting a state
 * only stores it, and the mapping runs once per draw.
 *
 * A state the interface cannot express is drawn with the closest mapping
 * and flagged in `recognized`, so the DLL can log it once.
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_CIF_STATE_H
#define VELOCITY9X_CIF_STATE_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"
#include "cif_api.h"

/* C3D_RECT's four C3D_INT32, in its order. */
#define V9X_CIF_RECT_TOP    0u
#define V9X_CIF_RECT_LEFT   1u
#define V9X_CIF_RECT_BOTTOM 2u
#define V9X_CIF_RECT_RIGHT  3u

typedef struct v9x_cif_state {
    v9x_u32 value[V9X_C3D_ERS_NUM];
    v9x_s32 viewport[4];    /* C3D_ERS_SURF_VPORT, valid once set */
    v9x_s32 scissor[4];     /* C3D_ERS_SURF_SCISSOR, valid once set */
    v9x_u32 viewport_set;
    v9x_u32 scissor_set;
} V9X_CIF_STATE;

/* A context's state on creation: ATI3DCIF.H's defaults. The surface
 * states default to "Desktop Surface", which only the DLL can resolve;
 * here they are zero and the rectangles unset. */
void v9x_cif_state_init(V9X_CIF_STATE *state);

/* ContextSetState(id, data): one 32-bit value, or four C3D_INT32 for the
 * two rectangles. V9X_C3D_EC_BADPARAM, and nothing stored, for an id past
 * the 37 or a null data pointer. */
v9x_u32 v9x_cif_state_set(V9X_CIF_STATE *state, v9x_u32 id, const void *data);

/* What the draw needs to know of the selected texture. */
typedef struct v9x_cif_texture_info {
    v9x_u32 mipmapped;      /* bMipMap and more than one level */
    v9x_u32 clamp_s;        /* C3D_TMAP bClampS, 0 when the TMAP lacks it */
    v9x_u32 clamp_t;
} V9X_CIF_TEXTURE_INFO;

typedef struct v9x_cif_draw_setup {
    V9X_R3D_ABI_STATE state;
    v9x_u32 textured;       /* TMAP_EN with a texture selected */
    v9x_u32 color_op;       /* V9X_R3D_ABI_COLOROP_*, when textured */
    v9x_u32 alpha_op;       /* V9X_R3D_ABI_ALPHAOP_* */
    v9x_u32 min_filter;     /* V9X_R3D_ABI_FILTER_* */
    v9x_u32 mag_filter;
    v9x_u32 mip;            /* V9X_R3D_ABI_MIP_* */
    v9x_u32 address;        /* V9X_R3D_ABI_ADDRESS_* */
    v9x_u32 key_texture;    /* convert the texture with the chroma key */
    v9x_u32 texel_alpha;    /* the draw reads the texture's own alpha */
    v9x_u32 white;          /* vertex colour white: an unlit texture as
                               MODULATE (the engines take D3D's ops only) */
    v9x_u32 shade;          /* V9X_C3D_ESH_*, for the vertex setup */
    v9x_u32 solid_argb;
    v9x_u32 fog;
    float origin_x;         /* the viewport's top-left */
    float origin_y;
    v9x_u32 recognized;     /* V9X_FALSE: drawn with the closest mapping */
} V9X_CIF_DRAW_SETUP;

/*
 * The state as a draw to a target `width` by `height` pixels. `texture`
 * may be null when nothing is selected. The scissor is the target, cut by
 * the viewport and by C3D_ERS_SURF_SCISSOR where they are set.
 */
void v9x_cif_state_map(const V9X_CIF_STATE *state, v9x_u32 width,
                       v9x_u32 height, const V9X_CIF_TEXTURE_INFO *texture,
                       V9X_CIF_DRAW_SETUP *out);

/* A C3D_COLOR (red in the low byte) as 0xAARRGGBB. */
v9x_u32 v9x_cif_color_to_argb(v9x_u32 color);

#endif /* VELOCITY9X_CIF_STATE_H */
