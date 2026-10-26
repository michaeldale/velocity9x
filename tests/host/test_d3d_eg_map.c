#include <stdio.h>
#include <string.h>

#include "velocity9x/eg_shader.h"
#include "../../src/display32/d3d/d3d_eg_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define HEAP_GPU 0x00100000ul

static int surface_object;
static int depth_object;
static int texture_object;

/* A 640x480 RGB565 target at the start of the heap, Gouraud, no depth,
 * no blend, no texture. */
static void base_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.offset = 0ul;
    draw->target.pitch = 1280ul;
    draw->target.width = 640ul;
    draw->target.height = 480ul;
    draw->target.format = V9X_R3D_FORMAT_RGB565;
    draw->target.object = &surface_object;
    draw->depth_func = V9X_R3D_CMP_LESSEQUAL;
    draw->src_blend = V9X_R3D_BLEND_ONE;
    draw->dst_blend = V9X_R3D_BLEND_ZERO;
    draw->alpha_func = V9X_R3D_CMP_ALWAYS;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
}

/* A 64x32 ARGB8888 texture, pitch 64 texels, 256-aligned. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_EG_TEXTURE *texture)
{
    draw->texture.object = &texture_object;
    draw->texture.min_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    draw->texture.wrap_u = 0ul;
    draw->texture.wrap_v = 0ul;
    draw->color_key_enable = 0ul;
    texture->format = V9X_EG_COLOR_8_8_8_8;
    texture->has_alpha = 1ul;
    texture->width = 64ul;
    texture->height = 32ul;
    texture->pitch_texels = 64ul;
    texture->offset = 0x00200000ul;
}

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_EG_TEXTURE *texture, v9x_u32 depth_bytes,
                   struct v9x_eg_3d_state *state, v9x_u32 *vs, v9x_u32 *ps)
{
    return v9x_d3d_eg_map_draw(draw, texture, HEAP_GPU, depth_bytes, state,
                               vs, ps);
}

static void test_untextured(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_eg_3d_state state;
    struct v9x_eg_pm4 stream;
    static v9x_u32 buffer[512];
    v9x_u32 vs = 99ul;
    v9x_u32 ps = 99ul;

    base_draw(&draw);
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(vs == V9X_EG_SLOT_VS && ps == V9X_EG_SLOT_PS_FIRST);
    CHECK(state.vs_gprs == 2ul && state.ps_gprs == 1ul &&
          state.interpolants == 1ul);
    CHECK(state.target_gpu == HEAP_GPU && state.target_pitch == 640ul &&
          state.target_height == 480ul &&
          state.target_format == V9X_EG_COLOR_5_6_5);
    CHECK(state.scissor_left == 0ul && state.scissor_right == 640ul &&
          state.scissor_bottom == 480ul);
    CHECK(state.has_depth == 0ul && state.has_texture == 0ul);
    CHECK(state.blend_control == 0x00010001ul);
    CHECK(state.alpha_test_control == 7ul);
    CHECK(state.perspective == 1ul && state.pixel_centre_half == 0ul);

    /* With the engine's addresses filled, the builder takes it. */
    state.vs_gpu = 0x0f015000ul;
    state.ps_gpu = 0x0f015200ul;
    state.vertex_gpu = 0x0f200000ul;
    state.vertex_stride = V9X_EG_D3D_STRIDE;
    state.vertex_bytes = 96ul;
    v9x_eg_pm4_begin(&stream, buffer, 512ul);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);

    /* XRGB8888 at a 2560-byte pitch. */
    base_draw(&draw);
    draw.target.format = V9X_R3D_FORMAT_XRGB8888;
    draw.target.pitch = 2560ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.target_format == V9X_EG_COLOR_8_8_8_8 &&
          state.target_pitch == 640ul && state.write_alpha == 0ul);

    /* V9XD32 at 32 bpp: the back buffer at 2560 bytes a row with a Z16 of
     * 1280, both 640x480; the builder takes what the map makes. */
    draw.depth.object = &depth_object;
    draw.depth.offset = 0x00258000ul;
    draw.depth.pitch = 1280ul;
    draw.depth.width = 640ul;
    draw.depth.height = 480ul;
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    state.vs_gpu = 0x0f015000ul;
    state.ps_gpu = 0x0f015200ul;
    state.vertex_gpu = 0x0f200000ul;
    state.vertex_stride = V9X_EG_D3D_STRIDE;
    state.vertex_bytes = 96ul;
    v9x_eg_pm4_begin(&stream, buffer, 512ul);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
}

static void test_target(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_eg_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;

    base_draw(&draw);
    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.pitch = 1288ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.offset = 0x80ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_TARGET);
    base_draw(&draw);
    draw.explicit_state = 1ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_EXPLICIT);
}

static void test_depth(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_eg_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;

    /* Bound and enabled: LESSEQUAL with writes, rows padded to 8. */
    base_draw(&draw);
    draw.depth.object = &depth_object;
    draw.depth.offset = 0x00100000ul;
    draw.depth.pitch = 1280ul;
    draw.depth.width = 640ul;
    draw.depth.height = 477ul;
    draw.target.height = 477ul;
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.depth_gpu == HEAP_GPU + 0x00100000ul &&
          state.depth_pitch == 640ul && state.depth_height == 480ul);
    CHECK(state.depth_control == 0x36ul);
    /* Writes off; NEVER is code 0. */
    draw.depth_write = 0ul;
    draw.depth_func = V9X_R3D_CMP_NEVER;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.depth_control == 0x02ul);
    /* Bound and disabled: no depth buffer at all. */
    draw.depth_enable = 0ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.has_depth == 0ul);
    /* Refusals: 32-bit cells, a pitch off the tile, an offset off 256, a
     * function past ALWAYS. */
    draw.depth_enable = 1ul;
    draw.depth_func = V9X_R3D_CMP_LESS;
    CHECK(map(&draw, 0, 4ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_DEPTH_SURFACE);
    draw.depth.pitch = 1288ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_DEPTH_SURFACE);
    draw.depth.pitch = 1280ul;
    draw.depth.offset = 0x00100040ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_DEPTH_SURFACE);
    draw.depth.offset = 0x00100000ul;
    draw.depth_func = 9ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_DEPTH_FUNC);
}

static void test_blend_and_alpha(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_eg_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;
    union {
        float value;
        v9x_u32 bits;
    } ref;

    base_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.blend_control == 0x45040504ul);
    draw.dst_blend = V9X_R3D_BLEND_DESTALPHA;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_BLEND);
    draw.dst_blend = V9X_R3D_BLEND_SRCALPHASAT;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK((state.blend_control & 0x1ful) == 10ul);

    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 128ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.alpha_test_control == 0x0cul);
    ref.value = 128.0f / 255.0f;
    CHECK(state.alpha_ref == ref.bits);
    draw.alpha_func = 0ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_ALPHA_FUNC);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_EG_TEXTURE texture;
    struct v9x_eg_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;

    base_draw(&draw);
    base_texture(&draw, &texture);
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE);
    CHECK(vs == V9X_EG_SLOT_VS_TEXTURED &&
          ps == V9X_EG_SLOT_PS_FIRST + V9X_EG_D3D_OP_MODULATE);
    CHECK(state.vs_gprs == 3ul && state.ps_gprs == 2ul &&
          state.interpolants == 2ul);
    CHECK(state.texture_gpu == HEAP_GPU + 0x00200000ul &&
          state.texture_width == 64ul && state.texture_height == 32ul &&
          state.texture_pitch == 64ul &&
          state.texture_format == V9X_EG_COLOR_8_8_8_8);
    CHECK(state.sampler_word0 ==
          v9x_eg_3d_sampler(1ul, 1ul, V9X_EG_TEX_WRAP, V9X_EG_TEX_WRAP));

    /* DECAL and COPY replace; MODULATEALPHA modulates; DECALALPHA and ADD
     * are refused. */
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE &&
          ps == V9X_EG_SLOT_PS_FIRST + V9X_EG_D3D_OP_REPLACE);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE &&
          ps == V9X_EG_SLOT_PS_FIRST + V9X_EG_D3D_OP_REPLACE);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE &&
          ps == V9X_EG_SLOT_PS_FIRST + V9X_EG_D3D_OP_MODULATE);
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_OP);
    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_OP);

    /* Mirror and clamp; border and the cylindrical wraps refused. */
    base_texture(&draw, &texture);
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE);
    CHECK((state.sampler_word0 & 0x3ful) ==
          (V9X_EG_TEX_MIRROR | (V9X_EG_TEX_MIRROR << 3)));
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE);
    CHECK((state.sampler_word0 & 0x3ful) == 0x12ul);
    draw.texture.address = V9X_R3D_ADDRESS_BORDER;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_ADDRESS);

    /* Filters within level 0; a mip filter on magnification refused. */
    base_texture(&draw, &texture);
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.sampler_word0 ==
          v9x_eg_3d_sampler(0ul, 1ul, V9X_EG_TEX_WRAP, V9X_EG_TEX_WRAP));
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.sampler_word0 ==
          v9x_eg_3d_sampler(0ul, 0ul, V9X_EG_TEX_WRAP, V9X_EG_TEX_WRAP));
    draw.texture.mag_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_FILTER);

    /* Shape and format. */
    base_texture(&draw, &texture);
    texture.pitch_texels = 60ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &texture);
    texture.offset = 0x00200020ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &texture);
    texture.format = V9X_D3D_EG_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_FORMAT);
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_FORMAT);

    /* A colour key with a texture needs the kill shader. */
    base_texture(&draw, &texture);
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_COLOR_KEY);
}

unsigned int v9x_run_d3d_eg_map_tests(void)
{
    failures = 0u;
    test_untextured();
    test_target();
    test_depth();
    test_blend_and_alpha();
    test_texture();
    return failures;
}
