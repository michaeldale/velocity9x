/*
 * Tests for ATI3DCIF state mapping (src\cif\cif_state.c): ATI3DCIF.H's
 * defaults, storing ContextSetState values, and the map to the render
 * interface - depth, blending, fog, texel ops, texture lighting, filters,
 * addressing, shading, the viewport and scissor, and what is flagged as
 * approximate. Values are the census's
 * (docs\decisions\2026-10-14-3dcif-demo-census.md).
 */
#include <stdio.h>
#include "../../src/cif/cif_state.h"

static unsigned int cif_state_failures;

#define SCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++cif_state_failures; \
    } \
} while (0)

/* Direct3D's numbers, which the interface takes. */
#define D3DCMP_NEVER        1ul
#define D3DCMP_LESS         2ul
#define D3DCMP_EQUAL        3ul
#define D3DCMP_LESSEQUAL    4ul
#define D3DCMP_GREATER      5ul
#define D3DCMP_NOTEQUAL     6ul
#define D3DCMP_GREATEREQUAL 7ul
#define D3DCMP_ALWAYS       8ul
#define D3DBLEND_ZERO         1ul
#define D3DBLEND_ONE          2ul
#define D3DBLEND_SRCCOLOR     3ul
#define D3DBLEND_INVSRCCOLOR  4ul
#define D3DBLEND_SRCALPHA     5ul
#define D3DBLEND_INVSRCALPHA  6ul
#define D3DBLEND_DESTALPHA    7ul
#define D3DBLEND_INVDESTALPHA 8ul
#define D3DBLEND_DESTCOLOR    9ul
#define D3DBLEND_INVDESTCOLOR 10ul

static void set1(V9X_CIF_STATE *state, v9x_u32 id, v9x_u32 value)
{
    SCHECK(v9x_cif_state_set(state, id, &value) == V9X_C3D_EC_OK);
}

static void set_rect(V9X_CIF_STATE *state, v9x_u32 id, v9x_s32 top,
                     v9x_s32 left, v9x_s32 bottom, v9x_s32 right)
{
    v9x_s32 rect[4];

    rect[V9X_CIF_RECT_TOP] = top;
    rect[V9X_CIF_RECT_LEFT] = left;
    rect[V9X_CIF_RECT_BOTTOM] = bottom;
    rect[V9X_CIF_RECT_RIGHT] = right;
    SCHECK(v9x_cif_state_set(state, id, rect) == V9X_C3D_EC_OK);
}

static void textured_state(V9X_CIF_STATE *state)
{
    v9x_cif_state_init(state);
    set1(state, V9X_C3D_ERS_TMAP_EN, 1ul);
    set1(state, V9X_C3D_ERS_TMAP_SELECT, 0x0077c0c8ul);
}

static void test_defaults(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;

    v9x_cif_state_init(&state);
    SCHECK(state.value[V9X_C3D_ERS_VERTEX_TYPE] == V9X_C3D_EV_VTCF);
    SCHECK(state.value[V9X_C3D_ERS_PRIM_TYPE] == V9X_C3D_EPRIM_TRI);
    SCHECK(state.value[V9X_C3D_ERS_SHADE_MODE] == V9X_C3D_ESH_SMOOTH);
    SCHECK(state.value[V9X_C3D_ERS_TMAP_PERSP_COR] == 3ul);
    SCHECK(state.value[V9X_C3D_ERS_TMAP_FILTER] == V9X_C3D_ETFILT_MINPNT_MAG2BY2);
    SCHECK(state.value[V9X_C3D_ERS_ALPHA_SRC] == V9X_C3D_EBLEND_ONE);
    SCHECK(state.value[V9X_C3D_ERS_ALPHA_DST] == V9X_C3D_EBLEND_ZERO);
    SCHECK(state.value[V9X_C3D_ERS_DITHER_EN] == 1ul);
    SCHECK(state.value[V9X_C3D_ERS_Z_CMP_FNC] == V9X_C3D_EZCMP_ALWAYS);
    SCHECK(state.value[V9X_C3D_ERS_COMPOSITE_FNC] == 3ul);
    SCHECK(state.value[V9X_C3D_ERS_COMPOSITE_FACTOR] == 8ul);
    SCHECK(state.value[V9X_C3D_ERS_COMPOSITE_FILTER] == V9X_C3D_ETFILT_MIN2BY2_MAG2BY2);
    SCHECK(state.value[V9X_C3D_ERS_ALPHA_DST_TEST_FNC] == 7ul);
    SCHECK(!state.viewport_set && !state.scissor_set);

    /* Mapped untouched: nothing on, the whole target, exact. */
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_enable == 0ul);
    SCHECK(out.state.blend_enable == 0ul);
    SCHECK(out.state.fog_enable == 0ul);
    SCHECK(out.state.alpha_test_enable == 0ul);
    SCHECK(out.state.write_mask == V9X_R3D_ABI_WRITE_RGB);
    SCHECK(out.state.scissor_left == 0ul && out.state.scissor_top == 0ul);
    SCHECK(out.state.scissor_right == 640ul && out.state.scissor_bottom == 480ul);
    SCHECK(out.textured == 0ul);
    SCHECK(out.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT);
    SCHECK(out.shade == V9X_C3D_ESH_SMOOTH);
    SCHECK(out.origin_x == 0.0f && out.origin_y == 0.0f);
    SCHECK(out.recognized == V9X_TRUE);
}

static void test_set(void)
{
    V9X_CIF_STATE state;
    v9x_u32 value = 1ul;

    v9x_cif_state_init(&state);
    SCHECK(v9x_cif_state_set(&state, V9X_C3D_ERS_NUM, &value) == V9X_C3D_EC_BADPARAM);
    SCHECK(v9x_cif_state_set(&state, V9X_C3D_ERS_FOG_EN, 0) == V9X_C3D_EC_BADPARAM);
    SCHECK(state.value[V9X_C3D_ERS_FOG_EN] == 0ul);

    set_rect(&state, V9X_C3D_ERS_SURF_VPORT, 10, 20, 110, 220);
    SCHECK(state.viewport_set);
    SCHECK(state.viewport[V9X_CIF_RECT_LEFT] == 20 && state.viewport[V9X_CIF_RECT_BOTTOM] == 110);
}

static void test_depth(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;

    /* Pixie: test and write, LEQUAL. */
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_Z_MODE, V9X_C3D_EZMODE_TESTON_WRITEZ);
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 2ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_enable == 1ul && out.state.depth_write == 1ul);
    SCHECK(out.state.depth_func == D3DCMP_LESSEQUAL);

    /* Test only; ATi-3D's GEQUAL; Knight's NEVER and LESS. */
    set1(&state, V9X_C3D_ERS_Z_MODE, V9X_C3D_EZMODE_TESTON);
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 4ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_enable == 1ul && out.state.depth_write == 0ul);
    SCHECK(out.state.depth_func == D3DCMP_GREATEREQUAL);
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 0ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_func == D3DCMP_NEVER);
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 1ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_func == D3DCMP_LESS);

    /* The orders differ: CIF's EQUAL and GREATER follow LEQUAL and GEQUAL,
     * Direct3D's precede them. */
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 3ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_func == D3DCMP_EQUAL);
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 5ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_func == D3DCMP_GREATER);
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 6ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_func == D3DCMP_NOTEQUAL);
    set1(&state, V9X_C3D_ERS_Z_CMP_FNC, 7ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_func == D3DCMP_ALWAYS);

    set1(&state, V9X_C3D_ERS_Z_MODE, V9X_C3D_EZMODE_OFF);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.depth_enable == 0ul);
}

static void test_blend(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;

    /* Pixie's translucency: SRCALPHA, INVSRCALPHA. */
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_ALPHA_SRC, V9X_C3D_EBLEND_SRCALPHA);
    set1(&state, V9X_C3D_ERS_ALPHA_DST, V9X_C3D_EBLEND_INVSRCALPHA);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.blend_enable == 1ul);
    SCHECK(out.state.src_blend == D3DBLEND_SRCALPHA);
    SCHECK(out.state.dst_blend == D3DBLEND_INVSRCALPHA);

    /* ATi-3D's multiply: the destination's colour as the source factor,
     * the source's as the destination's. */
    set1(&state, V9X_C3D_ERS_ALPHA_SRC, V9X_C3D_EBLEND_COLOR);
    set1(&state, V9X_C3D_ERS_ALPHA_DST, V9X_C3D_EBLEND_COLOR);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.src_blend == D3DBLEND_DESTCOLOR);
    SCHECK(out.state.dst_blend == D3DBLEND_SRCCOLOR);

    set1(&state, V9X_C3D_ERS_ALPHA_SRC, V9X_C3D_EBLEND_INVCOLOR);
    set1(&state, V9X_C3D_ERS_ALPHA_DST, V9X_C3D_EBLEND_INVCOLOR);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.src_blend == D3DBLEND_INVDESTCOLOR);
    SCHECK(out.state.dst_blend == D3DBLEND_INVSRCCOLOR);

    set1(&state, V9X_C3D_ERS_ALPHA_SRC, V9X_C3D_EBLEND_DSTALPHA);
    set1(&state, V9X_C3D_ERS_ALPHA_DST, V9X_C3D_EBLEND_INVDSTALPHA);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.src_blend == D3DBLEND_DESTALPHA);
    SCHECK(out.state.dst_blend == D3DBLEND_INVDESTALPHA);

    /* ONE, ZERO is the blender off. */
    set1(&state, V9X_C3D_ERS_ALPHA_SRC, V9X_C3D_EBLEND_ONE);
    set1(&state, V9X_C3D_ERS_ALPHA_DST, V9X_C3D_EBLEND_ZERO);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.blend_enable == 0ul);
    SCHECK(out.state.src_blend == D3DBLEND_ONE && out.state.dst_blend == D3DBLEND_ZERO);

    /* A factor past the eight is drawn as ONE and flagged. */
    set1(&state, V9X_C3D_ERS_ALPHA_SRC, 9ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.src_blend == D3DBLEND_ONE);
    SCHECK(out.recognized == V9X_FALSE);
}

static void test_fog(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;

    /* Cubes: fog on with FG_CLR 80808080. */
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_FG_CLR, 0x80808080ul);
    set1(&state, V9X_C3D_ERS_FOG_EN, 1ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.fog_enable == 1ul);
    SCHECK(out.fog == 1ul);
    SCHECK(out.state.fog_color == 0x00808080ul);
    SCHECK(out.recognized == V9X_TRUE);

    /* Fog and blending exclude each other: fog wins, and it is flagged. */
    set1(&state, V9X_C3D_ERS_ALPHA_SRC, V9X_C3D_EBLEND_SRCALPHA);
    set1(&state, V9X_C3D_ERS_ALPHA_DST, V9X_C3D_EBLEND_INVSRCALPHA);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.fog_enable == 1ul);
    SCHECK(out.state.blend_enable == 0ul);
    SCHECK(out.recognized == V9X_FALSE);
}

static void test_texture_ops(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;
    V9X_CIF_TEXTURE_INFO info;

    info.mipmapped = 0ul;
    info.clamp_s = 0ul;
    info.clamp_t = 0ul;

    /* Enabled without a texture selected is not textured. */
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_TMAP_EN, 1ul);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.textured == 0ul);

    /* Texture lighting as the colour op. */
    textured_state(&state);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.textured == 1ul);
    SCHECK(out.color_op == V9X_R3D_ABI_COLOROP_REPLACE);
    set1(&state, V9X_C3D_ERS_TMAP_LIGHT, V9X_C3D_ETL_MODULATE);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.color_op == V9X_R3D_ABI_COLOROP_MODULATE);
    set1(&state, V9X_C3D_ERS_TMAP_LIGHT, V9X_C3D_ETL_ALPHA_DECAL);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.color_op == V9X_R3D_ABI_COLOROP_DECALALPHA);

    /* No texel op: alpha from the fragment, no key, no test. */
    SCHECK(out.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT);
    SCHECK(out.key_texture == 0ul && out.state.alpha_test_enable == 0ul);

    /* Pixie's chroma key: keyed texels get alpha 0, which the test drops;
     * the fragment's alpha still scales the rest. */
    set1(&state, V9X_C3D_ERS_TMAP_TEXOP, V9X_C3D_ETEXOP_CHROMAKEY);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.key_texture == 1ul);
    SCHECK(out.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);
    SCHECK(out.state.alpha_test_enable == 1ul);
    SCHECK(out.state.alpha_func == D3DCMP_GREATER && out.state.alpha_ref == 0ul);
    SCHECK(out.recognized == V9X_TRUE);

    /* Knight's texel alpha to the blender. */
    set1(&state, V9X_C3D_ERS_TMAP_TEXOP, V9X_C3D_ETEXOP_ALPHA);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.key_texture == 0ul);
    SCHECK(out.alpha_op == V9X_R3D_ABI_ALPHAOP_REPLACE);
    SCHECK(out.state.alpha_test_enable == 0ul);

    /* The alpha mask: approximated, and flagged. */
    set1(&state, V9X_C3D_ERS_TMAP_TEXOP, V9X_C3D_ETEXOP_ALPHA_MASK);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.state.alpha_test_enable == 1ul);
    SCHECK(out.alpha_op == V9X_R3D_ABI_ALPHAOP_MODULATE);
    SCHECK(out.recognized == V9X_FALSE);

    /* An untextured draw ignores the texel op. */
    set1(&state, V9X_C3D_ERS_TMAP_TEXOP, V9X_C3D_ETEXOP_CHROMAKEY);
    set1(&state, V9X_C3D_ERS_TMAP_EN, 0ul);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.key_texture == 0ul && out.state.alpha_test_enable == 0ul);
    SCHECK(out.alpha_op == V9X_R3D_ABI_ALPHAOP_FRAGMENT);
}

static void check_filter(v9x_u32 filter, v9x_u32 mipmapped, v9x_u32 min,
                         v9x_u32 mag, v9x_u32 mip)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;
    V9X_CIF_TEXTURE_INFO info;

    info.mipmapped = mipmapped;
    info.clamp_s = 0ul;
    info.clamp_t = 0ul;
    textured_state(&state);
    set1(&state, V9X_C3D_ERS_TMAP_FILTER, filter);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.min_filter == min);
    SCHECK(out.mag_filter == mag);
    SCHECK(out.mip == mip);
}

static void test_filters(void)
{
    const v9x_u32 nearest = V9X_R3D_ABI_FILTER_NEAREST;
    const v9x_u32 linear = V9X_R3D_ABI_FILTER_LINEAR;

    /* Without mipmaps the level never changes. */
    check_filter(V9X_C3D_ETFILT_MINPNT_MAGPNT, 0ul, nearest, nearest, V9X_R3D_ABI_MIP_NONE);
    check_filter(V9X_C3D_ETFILT_MINPNT_MAG2BY2, 0ul, nearest, linear, V9X_R3D_ABI_MIP_NONE);
    check_filter(V9X_C3D_ETFILT_MIN2BY2_MAG2BY2, 0ul, linear, linear, V9X_R3D_ABI_MIP_NONE);
    check_filter(V9X_C3D_ETFILT_MIPLIN_MAGPNT, 0ul, nearest, nearest, V9X_R3D_ABI_MIP_NONE);
    check_filter(V9X_C3D_ETFILT_MIPLIN_MAG2BY2, 0ul, nearest, linear, V9X_R3D_ABI_MIP_NONE);
    check_filter(V9X_C3D_ETFILT_MIN2BY2_MAGPNT, 0ul, linear, nearest, V9X_R3D_ABI_MIP_NONE);

    /* With mipmaps: a MIP mode blends between levels, the others pick
     * the nearest level. */
    check_filter(V9X_C3D_ETFILT_MINPNT_MAGPNT, 1ul, nearest, nearest, V9X_R3D_ABI_MIP_POINT);
    check_filter(V9X_C3D_ETFILT_MIN2BY2_MAG2BY2, 1ul, linear, linear, V9X_R3D_ABI_MIP_POINT);
    check_filter(V9X_C3D_ETFILT_MIPLIN_MAGPNT, 1ul, nearest, nearest, V9X_R3D_ABI_MIP_LINEAR);
    check_filter(V9X_C3D_ETFILT_MIPLIN_MAG2BY2, 1ul, nearest, linear, V9X_R3D_ABI_MIP_LINEAR);
    check_filter(V9X_C3D_ETFILT_MIPTRI_MAG2BY2, 1ul, linear, linear, V9X_R3D_ABI_MIP_LINEAR);
}

static void test_address(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;
    V9X_CIF_TEXTURE_INFO info;

    info.mipmapped = 0ul;
    info.clamp_s = 0ul;
    info.clamp_t = 0ul;
    textured_state(&state);
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.address == V9X_R3D_ABI_ADDRESS_WRAP);

    info.clamp_s = 1ul;
    info.clamp_t = 1ul;
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.address == V9X_R3D_ABI_ADDRESS_CLAMP);
    SCHECK(out.recognized == V9X_TRUE);

    /* One axis clamped: the interface has one mode for both. */
    info.clamp_t = 0ul;
    v9x_cif_state_map(&state, 640ul, 480ul, &info, &out);
    SCHECK(out.address == V9X_R3D_ABI_ADDRESS_WRAP);
    SCHECK(out.recognized == V9X_FALSE);
}

static void test_shade(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;

    /* Knight's solid colour, C3D_COLOR FF00FFFF: red and green full. */
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_SHADE_MODE, V9X_C3D_ESH_SOLID);
    set1(&state, V9X_C3D_ERS_SOLID_CLR, 0xff00fffful);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.shade == V9X_C3D_ESH_SOLID);
    SCHECK(out.solid_argb == 0xffffff00ul);

    set1(&state, V9X_C3D_ERS_SHADE_MODE, V9X_C3D_ESH_FLAT);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.shade == V9X_C3D_ESH_FLAT);
}

static void test_viewport_scissor(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;

    /* The viewport moves the origin and clips. */
    v9x_cif_state_init(&state);
    set_rect(&state, V9X_C3D_ERS_SURF_VPORT, 10, 20, 110, 220);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.origin_x == 20.0f && out.origin_y == 10.0f);
    SCHECK(out.state.scissor_left == 20ul && out.state.scissor_top == 10ul);
    SCHECK(out.state.scissor_right == 220ul && out.state.scissor_bottom == 110ul);

    /* The scissor, from the surface's top-left, cuts it further. */
    set_rect(&state, V9X_C3D_ERS_SURF_SCISSOR, 0, 0, 50, 600);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.scissor_left == 20ul && out.state.scissor_top == 10ul);
    SCHECK(out.state.scissor_right == 220ul && out.state.scissor_bottom == 50ul);

    /* Both are held to the target. */
    v9x_cif_state_init(&state);
    set_rect(&state, V9X_C3D_ERS_SURF_SCISSOR, -5, -5, 900, 900);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.scissor_left == 0ul && out.state.scissor_top == 0ul);
    SCHECK(out.state.scissor_right == 640ul && out.state.scissor_bottom == 480ul);

    /* A scissor outside the target leaves nothing to draw. */
    set_rect(&state, V9X_C3D_ERS_SURF_SCISSOR, 500, 700, 600, 800);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.state.scissor_right <= out.state.scissor_left ||
           out.state.scissor_bottom <= out.state.scissor_top);
}

static void test_unmapped(void)
{
    V9X_CIF_STATE state;
    V9X_CIF_DRAW_SETUP out;

    /* Hints the interface has no use for are not approximations. */
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_DITHER_EN, 0ul);
    set1(&state, V9X_C3D_ERS_TMAP_PERSP_COR, 9ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.recognized == V9X_TRUE);

    /* Rage PRO states that change pixels are. */
    set1(&state, V9X_C3D_ERS_COMPOSITE_EN, 1ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.recognized == V9X_FALSE);
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_SPECULAR_EN, 1ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.recognized == V9X_FALSE);
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_ALPHA_DST_TEST_ENABLE, 1ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.recognized == V9X_FALSE);
    v9x_cif_state_init(&state);
    set1(&state, V9X_C3D_ERS_LOD_BIAS_LEVEL, 3ul);
    v9x_cif_state_map(&state, 640ul, 480ul, 0, &out);
    SCHECK(out.recognized == V9X_FALSE);
}

static void test_color_to_argb(void)
{
    SCHECK(v9x_cif_color_to_argb(0x80808080ul) == 0x80808080ul);
    /* A palette entry from ATi-3D: flags 40, blue 2B, green 15, red 1A. */
    SCHECK(v9x_cif_color_to_argb(0x402b151aul) == 0x401a152bul);
}

unsigned int v9x_run_cif_state_tests(void)
{
    cif_state_failures = 0u;
    test_defaults();
    test_set();
    test_depth();
    test_blend();
    test_fog();
    test_texture_ops();
    test_filters();
    test_address();
    test_shade();
    test_viewport_scissor();
    test_unmapped();
    test_color_to_argb();
    return cif_state_failures;
}
