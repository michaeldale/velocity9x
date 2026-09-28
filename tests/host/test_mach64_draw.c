#include <stdio.h>
#include <string.h>

#include "velocity9x/ati_mach64_engine.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define CAP 32ul

static v9x_u32 bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;
    converted.value = value;
    return converted.bits;
}

/* The Phase 3/4 diagnostic target: 64x28 RGB565 at 0x200100, pitch 128. */
static void base_state(struct v9x_m64_draw_state *state)
{
    memset(state, 0, sizeof(*state));
    state->color.vram_bytes = 0x00400000ul;
    state->color.target_offset = 0x00200100ul;
    state->color.target_pitch_bytes = 128ul;
    state->color.target_width = 64ul;
    state->color.target_height = 28ul;
    state->color.scissor_right = 64ul;
    state->color.scissor_bottom = 28ul;
}

static void texture_fields(struct v9x_m64_draw_state *state, v9x_u32 format)
{
    state->textured = 1ul;
    state->texture_offset = 0x00204000ul;
    state->texture_pitch_bytes = 16ul;
    state->texture_width = 8ul;
    state->texture_height = 8ul;
    state->texture_format = format;
}

static void test_untextured_matches_gouraud_builder(void)
{
    struct v9x_m64_draw_state state;
    struct v9x_m64_draw_decision decision;
    v9x_u32 offsets[CAP], values[CAP], written;
    v9x_u32 ref_offsets[CAP], ref_values[CAP], ref_written;
    unsigned int index;

    base_state(&state);
    memset(&decision, 0, sizeof(decision));
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(v9x_m64_build_gouraud_state(&state.color, ref_offsets, ref_values,
                                      CAP, &ref_written) == V9X_STATUS_OK);
    CHECK(written == 17ul && ref_written == 17ul);
    for (index = 0u; index < 17u; ++index) {
        CHECK(offsets[index] == ref_offsets[index]);
        CHECK(values[index] == ref_values[index]);
    }
    /* Phase 3's words: 0x000100C1, full-target scissor, Gouraud setup. */
    CHECK(values[10] == 0x000100c1ul);
    CHECK(values[4] == 0x003f0000ul && values[5] == 0x001b0000ul);
    CHECK(values[14] == V9X_M64_SETUP_GOURAUD);
}

static void test_blend_words_match_item9(void)
{
    /* Hardware field codes for the six source and six destination factors
     * item 9 measured, in D3D order. */
    static const v9x_u32 src_d3d[6] = { 1ul, 2ul, 5ul, 6ul, 9ul, 10ul };
    static const v9x_u32 src_field[6] = { 0ul, 1ul, 4ul, 5ul, 2ul, 3ul };
    static const v9x_u32 dst_d3d[6] = { 1ul, 2ul, 3ul, 4ul, 5ul, 6ul };
    static const v9x_u32 dst_field[6] = { 0ul, 1ul, 2ul, 3ul, 4ul, 5ul };
    struct v9x_m64_draw_state state;
    struct v9x_m64_draw_decision decision;
    v9x_u32 offsets[CAP], values[CAP], written;
    unsigned int s, d;

    memset(&decision, 0, sizeof(decision));
    for (s = 0u; s < 6u; ++s) {
        for (d = 0u; d < 6u; ++d) {
            base_state(&state);
            state.blend_enable = 1ul;
            state.src_blend = src_d3d[s];
            state.dst_blend = dst_d3d[d];
            CHECK(v9x_m64_build_draw_state(&state, &decision, offsets,
                                           values, CAP, &written) ==
                  V9X_STATUS_OK);
            CHECK(values[10] == (0x000008c1ul | (src_field[s] << 16) |
                                 (dst_field[d] << 19)));
        }
    }
    /* The two single-pair gates' literal words. */
    base_state(&state);
    state.blend_enable = 1ul;
    state.src_blend = 2ul;
    state.dst_blend = 2ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x000908c1ul);
    state.src_blend = 5ul;
    state.dst_blend = 6ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x002c08c1ul);
}

static void test_fog_matches_item12(void)
{
    struct v9x_m64_draw_state state;
    struct v9x_m64_draw_decision decision;
    v9x_u32 offsets[CAP], values[CAP], written;

    memset(&decision, 0, sizeof(decision));
    base_state(&state);
    state.fog_enable = 1ul;
    state.fog_color = 0xff20d0e0ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(written == 17ul);
    CHECK(values[10] == 0x002c10c1ul);
    CHECK(offsets[11] == V9X_M64_DP_FOG_CLR);
    CHECK(values[11] == 0x0020d0e0ul);

    /* Fog with blending or texture never reaches the builder. */
    state.blend_enable = 1ul;
    state.src_blend = 2ul;
    state.dst_blend = 1ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    base_state(&state);
    state.fog_enable = 1ul;
    texture_fields(&state, 0ul);
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_depth_words_match_depth_builder(void)
{
    struct v9x_m64_draw_state state;
    struct v9x_m64_draw_decision decision;
    struct v9x_m64_depth_state depth;
    v9x_u32 offsets[CAP], values[CAP], written;
    v9x_u32 ref_offsets[CAP], ref_values[CAP], ref_written;

    memset(&decision, 0, sizeof(decision));
    base_state(&state);
    state.depth_enable = 1ul;
    state.depth_offset = 0x00202100ul;
    state.depth_pitch_bytes = 128ul;
    state.depth_compare = 2ul;
    state.depth_write = 1ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);

    depth.color = state.color;
    depth.depth_offset = state.depth_offset;
    depth.depth_pitch_bytes = 128ul;
    depth.depth_width = 64ul;
    depth.depth_height = 28ul;
    depth.compare = 2ul;
    depth.write_enable = 1ul;
    CHECK(v9x_m64_build_depth_state(&depth, ref_offsets, ref_values, CAP,
                                    &ref_written) == V9X_STATUS_OK);
    CHECK(values[7] == ref_values[7] && values[8] == ref_values[8]);
    /* Item 3's words. */
    CHECK(values[7] == 0x02040420ul);
    CHECK(values[8] == 0x00000111ul);

    /* A depth surface overlapping the colour target is refused. */
    state.depth_offset = 0x00200100ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) != V9X_STATUS_OK);
    CHECK(written == 0ul);
}

static void test_texture_matches_texture_builder(void)
{
    struct v9x_m64_draw_state state;
    struct v9x_m64_draw_decision decision;
    struct v9x_m64_texture_state texture;
    v9x_u32 offsets[CAP], values[CAP], written;
    v9x_u32 ref_offsets[CAP], ref_values[CAP], ref_written;
    unsigned int index;

    /* REPLACE on RGB565: the texture builder's stream, unchanged. */
    base_state(&state);
    texture_fields(&state, V9X_M64_TEXTURE_FORMAT_RGB565);
    decision.light_fcn = V9X_M64_TEX_LIGHT_FCN_REPLACE;
    decision.texture_alpha = 0ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    memset(&texture, 0, sizeof(texture));
    texture.color = state.color;
    texture.texture_offset = state.texture_offset;
    texture.texture_pitch_bytes = 16ul;
    texture.texture_width = 8ul;
    texture.texture_height = 8ul;
    CHECK(v9x_m64_build_texture_state(&texture, ref_offsets, ref_values, CAP,
                                      &ref_written) == V9X_STATUS_OK);
    CHECK(written == 19ul && ref_written == 19ul);
    for (index = 0u; index < 19u; ++index) {
        CHECK(offsets[index] == ref_offsets[index]);
        CHECK(values[index] == ref_values[index]);
    }
    CHECK(values[10] == 0x01010081ul && values[16] == 0x40860000ul);

    /* Item 10's words: TEX_MAP_AEN and TEX_LIGHT_FCN around the proven
     * unblended and SRCALPHA/INVSRCALPHA values. */
    base_state(&state);
    texture_fields(&state, V9X_M64_TEXTURE_FORMAT_ARGB4444);
    decision.light_fcn = V9X_M64_TEX_LIGHT_FCN_MODULATE;
    decision.texture_alpha = V9X_M64_TEX_MAP_AEN;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x41410081ul);
    CHECK(values[13] == 0xf0040444ul);
    state.blend_enable = 1ul;
    state.src_blend = 5ul;
    state.dst_blend = 6ul;
    decision.light_fcn = V9X_M64_TEX_LIGHT_FCN_ALPHA_DECAL;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x41ac0881ul);

    /* Item 7's alpha-test word on ARGB1555: GREATER, reference 127. */
    base_state(&state);
    texture_fields(&state, V9X_M64_TEXTURE_FORMAT_ARGB1555);
    decision.light_fcn = V9X_M64_TEX_LIGHT_FCN_REPLACE;
    decision.texture_alpha = V9X_M64_TEX_MAP_AEN;
    state.alpha_test_enable = 1ul;
    state.alpha_compare = 5ul;
    state.alpha_reference = 127ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(values[9] == 0x007f0051ul);
    CHECK(values[10] == 0x41010081ul);
    CHECK(values[13] == 0x30040444ul);

    /* Depth and texture together: both builders' words. */
    state.depth_enable = 1ul;
    state.depth_offset = 0x00202100ul;
    state.depth_pitch_bytes = 128ul;
    state.depth_compare = 4ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_OK);
    CHECK(written == 19ul);
    CHECK(values[7] == 0x02040420ul && values[8] == 0x00000021ul);
    CHECK(offsets[18] == ref_offsets[18]);
}

static void test_state_arguments(void)
{
    struct v9x_m64_draw_state state;
    struct v9x_m64_draw_decision decision;
    v9x_u32 offsets[CAP], values[CAP], written = 7ul;

    memset(&decision, 0, sizeof(decision));
    base_state(&state);
    CHECK(v9x_m64_build_draw_state(0, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_m64_build_draw_state(&state, 0, offsets, values, CAP,
                                   &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, 18ul,
                                   &written) == V9X_STATUS_INVALID_ARGUMENT);
    state.alpha_test_enable = 1ul;
    state.alpha_compare = 9ul;
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, CAP,
                                   &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
}

static void setup_triangle(struct v9x_m64_setup_vertex *v)
{
    memset(v, 0, 3u * sizeof(*v));
    /* Phase 3's triangle, (8,6) (40,6) (8,22), in 14.2. */
    v[0].x_fixed = 32ul;  v[0].y_fixed = 24ul;
    v[1].x_fixed = 160ul; v[1].y_fixed = 24ul;
    v[2].x_fixed = 32ul;  v[2].y_fixed = 88ul;
    v[0].argb = v[1].argb = v[2].argb = 0xffff00fful;
    v[0].z16 = v[1].z16 = v[2].z16 = 0x4000ul;
    v[0].rhw = v[1].rhw = v[2].rhw = 1.0f;
}

static void test_setup_matches_phase3(void)
{
    struct v9x_m64_setup_vertex v[3];
    v9x_u32 offsets[CAP], values[CAP], written;
    v9x_u32 ref_offsets[CAP], ref_values[CAP], ref_written;
    struct v9x_m64_flat_triangle flat;
    unsigned int index;

    setup_triangle(v);
    CHECK(v9x_m64_build_setup(v, 0ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 19ul);

    flat.vertex[0].x = 8ul;  flat.vertex[0].y = 6ul;
    flat.vertex[1].x = 40ul; flat.vertex[1].y = 6ul;
    flat.vertex[2].x = 8ul;  flat.vertex[2].y = 22ul;
    flat.color = 0xffff00fful;
    CHECK(v9x_m64_build_flat_triangle(&flat, ref_offsets, ref_values, CAP,
                                      &ref_written) == V9X_STATUS_OK);
    /* Every word but Z, which this builder takes from the vertex. */
    for (index = 0u; index < 19u; ++index) {
        CHECK(offsets[index] == ref_offsets[index]);
        if (index % 6u != 3u || index == 18u) {
            CHECK(values[index] == ref_values[index]);
        }
    }
    CHECK(values[3] == 0x40000000ul);
    CHECK(values[18] == 0x3b000000ul);
}

static void test_setup_subpixel_texture_fog(void)
{
    struct v9x_m64_setup_vertex v[3];
    v9x_u32 offsets[CAP], values[CAP], written;

    setup_triangle(v);
    v[0].x_fixed = 33ul;                      /* x = 8.25 */
    CHECK(v9x_m64_build_setup(v, 0ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_OK);
    CHECK(values[5] == ((33ul << 16) | 24ul));
    /* cross = 127*64 - 0*... in 14.2: area scale 16/8128. */
    CHECK(values[18] == bits(16.0f / 8128.0f));

    setup_triangle(v);
    v[1].rhw = 0.25f;
    v[1].s = 1.25f;
    v[1].t = -0.25f;
    CHECK(v9x_m64_build_setup(v, 1ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_OK);
    CHECK(values[6] == bits(1.25f * 0.25f));
    CHECK(values[7] == bits(-0.25f * 0.25f));
    CHECK(values[8] == bits(0.25f));
    CHECK(values[2] == bits(1.0f));

    /* Fog: three specular words first, alpha only. */
    setup_triangle(v);
    v[0].specular = 0x80123456ul;
    v[1].specular = 0x40fffffful;
    v[2].specular = 0x00000000ul;
    CHECK(v9x_m64_build_setup(v, 0ul, 1ul, offsets, values, CAP, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 22ul);
    CHECK(offsets[0] == V9X_M64_VERTEX_1_SPEC_ARGB && values[0] == 0x80000000ul);
    CHECK(offsets[1] == V9X_M64_VERTEX_2_SPEC_ARGB && values[1] == 0x40000000ul);
    CHECK(offsets[2] == V9X_M64_VERTEX_3_SPEC_ARGB && values[2] == 0ul);
    CHECK(offsets[3] == V9X_M64_VERTEX_1_S);
    CHECK(offsets[21] == V9X_M64_ONE_OVER_AREA);
    CHECK(v9x_m64_build_setup(v, 0ul, 1ul, offsets, values, 21ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_setup_refusals(void)
{
    struct v9x_m64_setup_vertex v[3];
    v9x_u32 offsets[CAP], values[CAP], written = 9ul;

    /* Zero area draws nothing and says so distinctly. */
    setup_triangle(v);
    v[2].x_fixed = 288ul;
    v[2].y_fixed = 24ul;
    CHECK(v9x_m64_build_setup(v, 0ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(written == 0ul);

    setup_triangle(v);
    v[1].x_fixed = V9X_M64_SETUP_COORD_MAX_FIXED + 1ul;
    CHECK(v9x_m64_build_setup(v, 0ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    setup_triangle(v);
    v[1].z16 = 0x10000ul;
    CHECK(v9x_m64_build_setup(v, 0ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    setup_triangle(v);
    v[2].rhw = 0.0f;
    CHECK(v9x_m64_build_setup(v, 1ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* rhw is not read for an untextured triangle. */
    CHECK(v9x_m64_build_setup(v, 0ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_OK);
    CHECK(v9x_m64_build_setup(0, 0ul, 0ul, offsets, values, CAP, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_mach64_draw_tests(void)
{
    test_untextured_matches_gouraud_builder();
    test_blend_words_match_item9();
    test_fog_matches_item12();
    test_depth_words_match_depth_builder();
    test_texture_matches_texture_builder();
    test_state_arguments();
    test_setup_matches_phase3();
    test_setup_subpixel_texture_fog();
    test_setup_refusals();
    return failures;
}
