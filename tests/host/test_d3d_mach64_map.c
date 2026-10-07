#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_mach64_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static int surface_token;

/* A Direct3D draw into the diagnostics' 64x28 RGB565 target. */
static void d3d_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.offset = 0x00200100ul;
    draw->target.pitch = 128ul;
    draw->target.width = 64ul;
    draw->target.height = 28ul;
    draw->target.format = V9X_R3D_FORMAT_RGB565;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
    draw->depth_func = V9X_R3D_CMP_LESSEQUAL;
    draw->src_blend = V9X_R3D_BLEND_ONE;
    draw->dst_blend = V9X_R3D_BLEND_ZERO;
}

static void texture_8x8(V9X_D3D_MACH64_TEXTURE *texture, v9x_u32 format)
{
    texture->format = format;
    texture->width = 8ul;
    texture->height = 8ul;
    texture->levels = 1ul;
    texture->offset = 0x00204000ul;
    texture->pitch_bytes = 16ul;
}

static v9x_u32 accept(const struct v9x_m64_draw_request *request)
{
    struct v9x_m64_draw_decision decision;
    return v9x_m64_check_draw(request, &decision);
}

static void test_direct3d_defaults(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_m64_draw_request request;

    d3d_draw(&draw);
    /* The explicit fields are not read for a Direct3D draw. */
    draw.scissor_right = 3ul;
    draw.write_mask = 1ul;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(request.scissor_left == 0ul && request.scissor_top == 0ul);
    CHECK(request.scissor_right == 64ul && request.scissor_bottom == 28ul);
    CHECK(request.write_mask == 7ul);
    CHECK(request.textured == 0ul && request.depth_enable == 0ul);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);

    /* The render interface states both. */
    draw.explicit_state = 1ul;
    draw.scissor_left = 8ul;
    draw.scissor_top = 4ul;
    draw.scissor_right = 20ul;
    draw.scissor_bottom = 12ul;
    draw.write_mask = 7ul;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(request.scissor_left == 8ul && request.scissor_right == 20ul);
    CHECK(request.scissor_top == 4ul && request.scissor_bottom == 12ul);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
    draw.write_mask = 5ul;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(accept(&request) == V9X_M64_REFUSE_WRITE_MASK);

    d3d_draw(&draw);
    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(accept(&request) == V9X_M64_REFUSE_TARGET_FORMAT);
}

static void test_depth_needs_a_bound_surface(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_m64_draw_request request;

    d3d_draw(&draw);
    draw.depth_enable = 1ul;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(request.depth_enable == 0ul);
    draw.depth.object = &surface_token;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(request.depth_enable == 0ul);
    draw.depth.pitch = 128ul;
    draw.depth_write = 1ul;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(request.depth_enable == 1ul && request.depth_bits == 16ul);
    CHECK(request.depth_func == V9X_R3D_CMP_LESSEQUAL);
    CHECK(request.depth_write == 1ul);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
}

static void test_texture_mapping(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MACH64_TEXTURE texture;
    struct v9x_m64_draw_request request;

    d3d_draw(&draw);
    draw.texture.object = &surface_token;
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;
    texture_8x8(&texture, V9X_M64_TEXTURE_FORMAT_ARGB4444);
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(request.textured == 1ul);
    CHECK(request.texture_format == V9X_M64_TEXTURE_FORMAT_ARGB4444);
    CHECK(request.texture_width == 8ul && request.texture_levels == 1ul);
    CHECK(request.texture_op == V9X_R3D_TEXOP_MODULATE);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);

    /* The folded WRAPU/WRAPV state reaches both axes and refuses. */
    draw.texture.wrap_either = 1ul;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(request.texture_wrap_u == 1ul && request.texture_wrap_v == 1ul);
    CHECK(accept(&request) == V9X_M64_REFUSE_TEXTURE_ADDRESS);

    /* A format the engine could not name is refused by the policy. */
    draw.texture.wrap_either = 0ul;
    texture.format = V9X_D3D_MACH64_TEXTURE_UNKNOWN;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(accept(&request) == V9X_M64_REFUSE_TEXTURE_FORMAT);

    /* CPU-resident levels have no VRAM copy yet. */
    d3d_draw(&draw);
    draw.texture.level_count = 1ul;
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    texture_8x8(&texture, V9X_M64_TEXTURE_FORMAT_RGB565);
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(request.textured == 1ul);
    CHECK(request.texture_format == V9X_D3D_MACH64_TEXTURE_UNKNOWN);
    CHECK(accept(&request) == V9X_M64_REFUSE_TEXTURE_FORMAT);
}

/*
 * A mip filter on a texture of one level is its base filter (Direct3D's
 * rule, and what the probe's plain_mipnear cells draw). A chain keeps the
 * filter and carries its levels into the state; a chain drawn with a
 * filter that selects no level samples level 0 alone.
 */
static void test_mip_mapping(void)
{
    static const v9x_u32 folded[4][2] = {
        /* "LINEAR" after "MIP" is the filter within a level (the DDK's
         * ViRGE HAL, as d3d_i9xx.c records). */
        { V9X_R3D_FILTER_MIPNEAREST, V9X_R3D_FILTER_NEAREST },
        { V9X_R3D_FILTER_MIPLINEAR, V9X_R3D_FILTER_LINEAR },
        { V9X_R3D_FILTER_LINEARMIPNEAREST, V9X_R3D_FILTER_NEAREST },
        { V9X_R3D_FILTER_LINEARMIPLINEAR, V9X_R3D_FILTER_LINEAR }
    };
    V9X_R3D_DRAW draw;
    V9X_D3D_MACH64_TEXTURE texture;
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_state state;
    v9x_u32 index;

    d3d_draw(&draw);
    draw.texture.object = &surface_token;
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    texture_8x8(&texture, V9X_M64_TEXTURE_FORMAT_ARGB1555);
    for (index = 0ul; index < 4ul; ++index) {
        draw.texture.min_filter = folded[index][0];
        v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
        CHECK(request.texture_min_filter == folded[index][1]);
        CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
    }

    /* 8, 4, 2, 1. */
    texture.levels = 4ul;
    for (index = 0ul; index < 4ul; ++index) {
        texture.level_offsets[index] = 0x00204000ul + index * 0x1000ul;
    }
    draw.texture.min_filter = V9X_R3D_FILTER_MIPLINEAR;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(request.texture_min_filter == V9X_R3D_FILTER_MIPLINEAR);
    CHECK(request.texture_levels == 4ul);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
    v9x_d3d_mach64_map_state(&draw, &request, &texture, 0x00400000ul,
                             &state);
    CHECK(state.level_count == 4ul && state.bilinear_min == 1ul);
    CHECK(state.level_offsets[0] == 0x00204000ul);
    CHECK(state.level_offsets[3] == 0x00207000ul);

    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    v9x_d3d_mach64_map_state(&draw, &request, &texture, 0x00400000ul,
                             &state);
    CHECK(state.level_count == 4ul && state.bilinear_min == 0ul);

    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
    v9x_d3d_mach64_map_state(&draw, &request, &texture, 0x00400000ul,
                             &state);
    CHECK(state.level_count == 1ul && state.bilinear_min == 1ul);

    /* Trilinear on the chain: bilinear_min 2 and every level. */
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
    v9x_d3d_mach64_map_state(&draw, &request, &texture, 0x00400000ul,
                             &state);
    CHECK(state.level_count == 4ul && state.bilinear_min == 2ul);

    /* Nearest in each of two levels, blended: no engine function. */
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPNEAREST;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(accept(&request) == V9X_M64_REFUSE_TEXTURE_FILTER);
}

/* Opaque only when every vertex's alpha byte is 0xFF; nothing is opaque. */
static void test_vertex_alpha_opaque(void)
{
    V9X_R3D_VERTEX vertices[3];

    memset(vertices, 0, sizeof(vertices));
    vertices[0].color = 0xFF102030ul;
    vertices[1].color = 0xFFFFFFFFul;
    vertices[2].color = 0xFE000000ul;
    CHECK(v9x_d3d_mach64_vertices_opaque(vertices, 3ul) == 0ul);
    CHECK(v9x_d3d_mach64_vertices_opaque(vertices, 2ul) == 1ul);
    CHECK(v9x_d3d_mach64_vertices_opaque(vertices, 0ul) == 0ul);
    CHECK(v9x_d3d_mach64_vertices_opaque(0, 3ul) == 0ul);
    /* The least alpha; 0 for nothing to read. */
    CHECK(v9x_d3d_mach64_vertices_alpha_min(vertices, 3ul) == 0xFEul);
    CHECK(v9x_d3d_mach64_vertices_alpha_min(vertices, 2ul) == 0xFFul);
    vertices[1].color = 0x01FFFFFFul;
    CHECK(v9x_d3d_mach64_vertices_alpha_min(vertices, 3ul) == 0x01ul);
    CHECK(v9x_d3d_mach64_vertices_alpha_min(vertices, 0ul) == 0ul);
    CHECK(v9x_d3d_mach64_vertices_alpha_min(0, 3ul) == 0ul);
}

static void test_specular_needs_colour(void)
{
    V9X_R3D_DRAW draw;
    V9X_R3D_VERTEX vertices[3];
    struct v9x_m64_draw_request request;

    memset(vertices, 0, sizeof(vertices));
    vertices[1].specular = 0x80000000ul;
    CHECK(v9x_d3d_mach64_specular_rgb(vertices, 3ul) == 0ul);
    vertices[2].specular = 0x00000100ul;
    CHECK(v9x_d3d_mach64_specular_rgb(vertices, 3ul) == 1ul);
    CHECK(v9x_d3d_mach64_specular_rgb(vertices, 2ul) == 0ul);
    CHECK(v9x_d3d_mach64_specular_rgb(0, 3ul) == 0ul);

    d3d_draw(&draw);
    draw.specular_enable = 1ul;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(request.specular_enable == 0ul);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
    v9x_d3d_mach64_map_request(&draw, 0, 1ul, &request);
    CHECK(request.specular_enable == 1ul);
    CHECK(accept(&request) == V9X_M64_REFUSE_NONE);
    {
        struct v9x_m64_draw_decision decision;
        struct v9x_m64_draw_state state;
        v9x_u32 offsets[32], values[32], written;

        CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
        v9x_d3d_mach64_map_state(&draw, &request, 0, 0x00400000ul, &state);
        CHECK(state.specular_enable == 1ul);
        CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values,
                                       32ul, &written) == V9X_STATUS_OK);
        /* ALPHA_TST_CNTL SPECULAR_LIGHT_EN (bit 31). */
        CHECK(offsets[9] == V9X_M64_ALPHA_TST_CNTL &&
              (values[9] & V9X_M64_SPECULAR_LIGHT_EN) != 0ul);
    }
    draw.fog_enable = 1ul;
    v9x_d3d_mach64_map_request(&draw, 0, 1ul, &request);
    CHECK(accept(&request) == V9X_M64_REFUSE_SPECULAR);
}

static void test_state_end_to_end(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MACH64_TEXTURE texture;
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_decision decision;
    struct v9x_m64_draw_state state;
    v9x_u32 offsets[32], values[32], written;

    /* Item 9's SRCALPHA/INVSRCALPHA pair over an untextured draw. */
    d3d_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
    v9x_d3d_mach64_map_state(&draw, &request, 0, 0x00400000ul, &state);
    CHECK(state.color.target_offset == 0x00200100ul);
    CHECK(state.color.scissor_right == 64ul);
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, 32ul,
                                   &written) == V9X_STATUS_OK);
    CHECK(written == 17ul && values[10] == 0x002c08c1ul);

    /* Item 12's fog word, colour carried through. */
    d3d_draw(&draw);
    draw.fog_enable = 1ul;
    draw.fog_color = 0x0020d0e0ul;
    v9x_d3d_mach64_map_request(&draw, 0, 0ul, &request);
    CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
    v9x_d3d_mach64_map_state(&draw, &request, 0, 0x00400000ul, &state);
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, 32ul,
                                   &written) == V9X_STATUS_OK);
    CHECK(values[10] == 0x002c10c1ul && values[11] == 0x0020d0e0ul);

    /* Item 6's wrap and bilinear on ARGB1555 with DECALALPHA. */
    d3d_draw(&draw);
    draw.texture.object = &surface_token;
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    texture_8x8(&texture, V9X_M64_TEXTURE_FORMAT_ARGB1555);
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
    v9x_d3d_mach64_map_state(&draw, &request, &texture, 0x00400000ul,
                             &state);
    CHECK(state.wrap_s == 1ul && state.wrap_t == 1ul);
    CHECK(state.bilinear_min == 1ul && state.bilinear_mag == 1ul);
    CHECK(state.texture_offset == 0x00204000ul);
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, 32ul,
                                   &written) == V9X_STATUS_OK);
    CHECK(written == 19ul);
    /* 0x0A010081 (item 6) + AEN + ALPHA_DECAL; wrap clears both clamps. */
    CHECK(values[10] == 0x4b810081ul);
    CHECK(values[16] == 0x40800000ul);
}

static int close_to(float a, float b)
{
    float d = a - b;

    return d < 0.001f && d > -0.001f;
}

/* The perspective-correct centroid the wrap rebase rounds to. */
static void test_wrap_reference(void)
{
    struct v9x_m64_setup_vertex v[3];
    float s = 9.0f;
    float t = 9.0f;

    memset(v, 0, sizeof(v));
    /* The 3DMark tunnel wall's pass 1 (M64TRI-TUNNEL.TXT). */
    v[0].rhw = 0.25971f; v[0].s = 0.98f; v[0].t = 0.02f;
    v[1].rhw = 0.00500f; v[1].s = 0.00f; v[1].t = 1.00f;
    v[2].rhw = 0.25395f; v[2].s = 0.98f; v[2].t = 1.00f;
    CHECK(v9x_d3d_mach64_wrap_reference(v, &s, &t) == 1);
    CHECK(close_to(s, 0.9706f));
    CHECK(close_to(t, 0.5093f));

    /* Equal W: the plain mean. */
    v[0].rhw = v[1].rhw = v[2].rhw = 2.0f;
    v[0].s = 1.0f; v[1].s = 2.0f; v[2].s = 6.0f;
    CHECK(v9x_d3d_mach64_wrap_reference(v, &s, &t) == 1);
    CHECK(close_to(s, 3.0f));

    /* No positive W: no reference, and the outputs are left alone. */
    v[0].rhw = v[1].rhw = v[2].rhw = 0.0f;
    s = 9.0f;
    CHECK(v9x_d3d_mach64_wrap_reference(v, &s, &t) == 0);
    CHECK(s == 9.0f);
}

/* A render-interface two-unit draw: unit 0 an 8x8 chain under MIPLINEAR,
 * unit 1 a 16x16 lightmap modulated over it (r3d_abi.h's numbers). */
static void composite_draw(V9X_R3D_DRAW *draw,
                           V9X_D3D_MACH64_TEXTURE *texture,
                           V9X_D3D_MACH64_TEXTURE *texture1)
{
    static const float coordinates[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };

    d3d_draw(draw);
    draw->explicit_state = 1ul;
    draw->scissor_right = 64ul;
    draw->scissor_bottom = 28ul;
    draw->write_mask = 7ul;
    draw->texture.object = &surface_token;
    draw->texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    draw->texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    /* A two-unit draw leaves op zero and states REPLACE/REPLACE. */
    draw->texture.color_op = 1ul;
    draw->texture.alpha_op = 1ul;
    draw->texture1.object = &surface_token;
    draw->texture1.min_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture1.mag_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture1.address = V9X_R3D_ADDRESS_CLAMP;
    draw->texture1.color_op = 2ul;
    draw->texture1.alpha_op = 0ul;
    draw->texcoords1 = coordinates;

    texture_8x8(texture, V9X_M64_TEXTURE_FORMAT_RGB565);
    texture->levels = 4ul;
    texture->level_offsets[0] = texture->offset;
    texture->level_offsets[1] = 0x00204080ul;
    texture->level_offsets[2] = 0x00204100ul;
    texture->level_offsets[3] = 0x00204140ul;
    memset(texture1, 0, sizeof(*texture1));
    texture1->format = V9X_M64_TEXTURE_FORMAT_RGB565;
    texture1->width = 16ul;
    texture1->height = 16ul;
    texture1->levels = 1ul;
    texture1->offset = 0x00205000ul;
    texture1->pitch_bytes = 32ul;
    texture1->level_offsets[0] = texture1->offset;
}

static void test_composite_mapping(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MACH64_TEXTURE texture;
    V9X_D3D_MACH64_TEXTURE texture1;
    struct v9x_m64_draw_request request;
    struct v9x_m64_draw_decision decision;
    struct v9x_m64_draw_state state;
    v9x_u32 offsets[32], values[32], written;

    composite_draw(&draw, &texture, &texture1);
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    v9x_d3d_mach64_map_composite(&draw, &texture1, &request);
    CHECK(request.composite == 1ul);
    CHECK(request.composite_format == V9X_M64_TEXTURE_FORMAT_RGB565);
    CHECK(request.composite_width == 16ul && request.composite_height == 16ul);
    CHECK(request.composite_min_filter == V9X_R3D_FILTER_LINEAR);
    CHECK(request.composite_address == V9X_R3D_ADDRESS_CLAMP);
    CHECK(request.composite_color_op == 2ul);
    CHECK(request.texture_op == V9X_R3D_TEXOP_DECAL);
    CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
    CHECK(decision.light_fcn == V9X_M64_TEX_LIGHT_FCN_REPLACE);

    /* Unit 0 MODULATE over RGB565 keeps the fragment's alpha: D3D's
     * MODULATE; BLEND has no op and is refused. */
    draw.texture.color_op = 2ul;
    draw.texture.alpha_op = 0ul;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    v9x_d3d_mach64_map_composite(&draw, &texture1, &request);
    CHECK(request.texture_op == V9X_R3D_TEXOP_MODULATE);
    CHECK(v9x_m64_check_draw(&request, &decision) == V9X_M64_REFUSE_NONE);
    CHECK(decision.light_fcn == V9X_M64_TEX_LIGHT_FCN_MODULATE);
    draw.texture.color_op = 4ul;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    v9x_d3d_mach64_map_composite(&draw, &texture1, &request);
    CHECK(request.texture_op == 0ul);
    CHECK(v9x_m64_check_draw(&request, &decision) ==
          V9X_M64_REFUSE_TEXTURE_OP);
    draw.texture.color_op = 1ul;
    draw.texture.alpha_op = 1ul;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    v9x_d3d_mach64_map_composite(&draw, &texture1, &request);

    /* Unit 0's chain keeps its levels, bilinear within the selected one:
     * the composite takes the trilinear function. Unit 1 clamps and
     * filters on its own. */
    v9x_d3d_mach64_map_state(&draw, &request, &texture, 0x00400000ul, &state);
    v9x_d3d_mach64_map_composite_state(&request, &texture1, &state);
    CHECK(state.level_count == 4ul && state.bilinear_min == 1ul);
    CHECK(state.composite == 1ul && state.composite_offset == 0x00205000ul);
    CHECK(state.composite_pitch_bytes == 32ul);
    CHECK(state.composite_wrap_s == 0ul && state.composite_wrap_t == 0ul);
    CHECK(state.composite_bilinear_min == 1ul);
    CHECK(state.composite_bilinear_mag == 1ul);
    CHECK(v9x_m64_build_draw_state(&state, &decision, offsets, values, 32ul,
                                   &written) == V9X_STATUS_OK);
    CHECK(written == 22ul);
    CHECK((values[16] & 0x80000300ul) == 0x80000300ul);
    CHECK(values[17] == 0x00205000ul);

    /* A one-unit draw maps no composite, whatever texture1 holds. */
    draw.texcoords1 = 0;
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    v9x_d3d_mach64_map_composite(&draw, &texture1, &request);
    CHECK(request.composite == 0ul);
    v9x_d3d_mach64_map_state(&draw, &request, &texture, 0x00400000ul, &state);
    v9x_d3d_mach64_map_composite_state(&request, &texture1, &state);
    CHECK(state.composite == 0ul && state.level_count == 4ul);

    /* No resolved second texture: an unknown format, refused. */
    composite_draw(&draw, &texture, &texture1);
    v9x_d3d_mach64_map_request(&draw, &texture, 0ul, &request);
    v9x_d3d_mach64_map_composite(&draw, 0, &request);
    CHECK(request.composite == 1ul);
    CHECK(v9x_m64_check_draw(&request, &decision) ==
          V9X_M64_REFUSE_COMPOSITE);
}

unsigned int v9x_run_d3d_mach64_map_tests(void)
{
    test_direct3d_defaults();
    test_depth_needs_a_bound_surface();
    test_texture_mapping();
    test_mip_mapping();
    test_vertex_alpha_opaque();
    test_specular_needs_colour();
    test_state_end_to_end();
    test_wrap_reference();
    test_composite_mapping();
    return failures;
}
