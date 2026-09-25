/*
 * Tests for the Direct3D-to-neutral state translation.
 *
 * Two things are held: that the neutral vocabulary's numbers are
 * d3dtypes.h's (through a table, so the comparison is made at run time),
 * and that every raw field lands in the draw field an engine will read it
 * from - the engines were moved one at a time onto V9X_R3D_DRAW, and a raw
 * value routed to the wrong field would draw a plausible wrong picture
 * rather than fail.
 */
#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_state.h"

static unsigned int state_failures = 0u;

#define SCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++state_failures; \
    } \
} while (0)

/* d3dtypes.h, DirectX 6: value, then the neutral constant. */
static const unsigned long state_pairs[][2] = {
    { 1ul, V9X_R3D_CMP_NEVER }, { 2ul, V9X_R3D_CMP_LESS },
    { 3ul, V9X_R3D_CMP_EQUAL }, { 4ul, V9X_R3D_CMP_LESSEQUAL },
    { 5ul, V9X_R3D_CMP_GREATER }, { 6ul, V9X_R3D_CMP_NOTEQUAL },
    { 7ul, V9X_R3D_CMP_GREATEREQUAL }, { 8ul, V9X_R3D_CMP_ALWAYS },
    { 1ul, V9X_R3D_BLEND_ZERO }, { 2ul, V9X_R3D_BLEND_ONE },
    { 3ul, V9X_R3D_BLEND_SRCCOLOR }, { 4ul, V9X_R3D_BLEND_INVSRCCOLOR },
    { 5ul, V9X_R3D_BLEND_SRCALPHA }, { 6ul, V9X_R3D_BLEND_INVSRCALPHA },
    { 7ul, V9X_R3D_BLEND_DESTALPHA }, { 8ul, V9X_R3D_BLEND_INVDESTALPHA },
    { 9ul, V9X_R3D_BLEND_DESTCOLOR }, { 10ul, V9X_R3D_BLEND_INVDESTCOLOR },
    { 11ul, V9X_R3D_BLEND_SRCALPHASAT },
    { 1ul, V9X_R3D_FILTER_NEAREST }, { 2ul, V9X_R3D_FILTER_LINEAR },
    { 3ul, V9X_R3D_FILTER_MIPNEAREST }, { 4ul, V9X_R3D_FILTER_MIPLINEAR },
    { 5ul, V9X_R3D_FILTER_LINEARMIPNEAREST },
    { 6ul, V9X_R3D_FILTER_LINEARMIPLINEAR },
    { 1ul, V9X_R3D_ADDRESS_WRAP }, { 2ul, V9X_R3D_ADDRESS_MIRROR },
    { 3ul, V9X_R3D_ADDRESS_CLAMP }, { 4ul, V9X_R3D_ADDRESS_BORDER },
    { 1ul, V9X_R3D_TEXOP_DECAL }, { 2ul, V9X_R3D_TEXOP_MODULATE },
    { 3ul, V9X_R3D_TEXOP_DECALALPHA }, { 4ul, V9X_R3D_TEXOP_MODULATEALPHA },
    { 5ul, V9X_R3D_TEXOP_DECALMASK }, { 6ul, V9X_R3D_TEXOP_MODULATEMASK },
    { 7ul, V9X_R3D_TEXOP_COPY }, { 8ul, V9X_R3D_TEXOP_ADD },
    { 1ul, V9X_R3D_SHADE_FLAT }, { 2ul, V9X_R3D_SHADE_GOURAUD },
    { 1ul, V9X_R3D_FORMAT_RGB565 }, { 2ul, V9X_R3D_FORMAT_XRGB1555 }
};

static void test_values_match_d3dtypes(void)
{
    unsigned int index;

    for (index = 0u; index < sizeof(state_pairs) / sizeof(state_pairs[0]); ++index) {
        if (state_pairs[index][0] != state_pairs[index][1]) {
            printf("FAIL neutral constant %u is %lu, d3dtypes says %lu\n",
                   index, state_pairs[index][1], state_pairs[index][0]);
            ++state_failures;
        }
    }
}

/* Every raw field gets its own distinct value, and each must arrive in its
 * own draw field and nowhere else. */
static void test_every_field_routes(void)
{
    V9X_D3D_STATE_RAW raw;
    V9X_R3D_DRAW draw;

    memset(&draw, 0xAA, sizeof(draw));
    raw.z_enable = 101ul; raw.z_write = 102ul; raw.z_func = 103ul;
    raw.alpha_blend_enable = 104ul; raw.src_blend = 105ul; raw.dest_blend = 106ul;
    raw.texture_min = 107ul; raw.texture_mag = 108ul; raw.texture_blend = 109ul;
    raw.texture_address = 110ul; raw.texture_border = 111ul; raw.texture_wrap = 112ul;
    raw.wrap_u = 113ul; raw.wrap_v = 114ul; raw.shade_mode = 115ul;
    raw.specular_enable = 116ul; raw.fog_enable = 117ul; raw.fog_color = 118ul;
    raw.alpha_test_enable = 119ul; raw.alpha_func = 120ul; raw.alpha_ref = 121ul;
    raw.alpha_force = 122ul; raw.color_key_enable = 123ul;
    v9x_d3d_state_fill(&raw, &draw);
    SCHECK(draw.depth_enable == 101ul);
    SCHECK(draw.depth_write == 102ul);
    SCHECK(draw.depth_func == 103ul);
    SCHECK(draw.blend_enable == 104ul);
    SCHECK(draw.src_blend == 105ul);
    SCHECK(draw.dst_blend == 106ul);
    SCHECK(draw.texture.min_filter == 107ul);
    SCHECK(draw.texture.mag_filter == 108ul);
    SCHECK(draw.texture.op == 109ul);
    SCHECK(draw.texture.address == 110ul);
    SCHECK(draw.texture.border == 111ul);
    SCHECK(draw.texture.wrap_either == 112ul);
    SCHECK(draw.texture.wrap_u == 113ul);
    SCHECK(draw.texture.wrap_v == 114ul);
    SCHECK(draw.shade_mode == 115ul);
    SCHECK(draw.specular_enable == 116ul);
    SCHECK(draw.fog_enable == 117ul);
    SCHECK(draw.fog_color == 118ul);
    SCHECK(draw.alpha_test_enable == 119ul);
    SCHECK(draw.alpha_func == 120ul);
    SCHECK(draw.alpha_ref == 121ul);
    SCHECK(draw.alpha_force == 122ul);
    SCHECK(draw.color_key_enable == 123ul);
    /* The surfaces are the core's and must be untouched. */
    SCHECK(draw.target.offset == 0xAAAAAAAAul);
    SCHECK(draw.depth.pitch == 0xAAAAAAAAul);
    SCHECK(draw.texture.object == (void *)0xAAAAAAAAul);
}

static void test_depth_active_needs_all_three(void)
{
    SCHECK(v9x_d3d_state_depth_active(1ul, 1ul, 1280ul) == 1ul);
    SCHECK(v9x_d3d_state_depth_active(0ul, 1ul, 1280ul) == 0ul);
    SCHECK(v9x_d3d_state_depth_active(1ul, 0ul, 1280ul) == 0ul);
    SCHECK(v9x_d3d_state_depth_active(1ul, 1ul, 0ul) == 0ul);
    /* D3DZB_USEW is 2: any non-zero enable counts, as the engines did. */
    SCHECK(v9x_d3d_state_depth_active(2ul, 1ul, 1280ul) == 1ul);
}

unsigned int v9x_run_d3d_state_tests(void)
{
    state_failures = 0u;
    test_values_match_d3dtypes();
    test_every_field_routes();
    test_depth_active_needs_all_three();
    if (state_failures == 0u) {
        puts("PASS: Direct3D state to neutral draw");
    }
    return state_failures;
}
