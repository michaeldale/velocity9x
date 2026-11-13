#include <stdio.h>
#include <string.h>

#include "velocity9x/r6_shader.h"
#include "../../src/display32/d3d/d3d_r6_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define HEAP_GPU 0xe0100000ul

static int surface_object;
static int depth_object;
static int texture_object;

/* A 640x480 RGB565 target at the start of the heap, Gouraud, no depth,
 * no blend, no texture: test_d3d_eg_map.c's base draw. */
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

/* A 64x32 RGB565 texture, pitch 128 texels (256 bytes), 256-aligned. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_R6_TEXTURE *texture)
{
    draw->texture.object = &texture_object;
    draw->texture.min_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    texture->format = V9X_R6_COLOR_5_6_5;
    texture->width = 64ul;
    texture->height = 32ul;
    texture->pitch_texels = 128ul;
    texture->offset = 0x00200000ul;
    texture->has_key = 0ul;
    texture->opaque = 0ul;
}

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_R6_TEXTURE *texture, v9x_u32 depth_bytes,
                   struct v9x_r6_3d_state *state, v9x_u32 *vs, v9x_u32 *ps)
{
    return v9x_d3d_r6_map_draw(draw, texture, HEAP_GPU, depth_bytes, state,
                               vs, ps);
}

static void test_untextured(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_r6_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;

    base_draw(&draw);
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.d3d_order == 1ul &&
          state.target_format == V9X_R6_COLOR_5_6_5 &&
          state.target_gpu == HEAP_GPU && state.target_pitch == 640ul &&
          state.target_height == 480ul && state.scissor_right == 640ul &&
          state.scissor_bottom == 480ul && state.write_alpha == 0ul);
    CHECK(vs == V9X_R6_SLOT_VS &&
          ps == V9X_R6_SLOT_PS_FIRST + V9X_R6_D3D_OP_NONE &&
          state.vs_gprs == 2ul && state.ps_gprs == 1ul &&
          state.interpolants == 1ul);
    CHECK(state.perspective == 1ul && state.pixel_centre_half == 0ul &&
          state.vertex_cache == 1ul &&
          state.vertex_stride == V9X_R6_D3D_STRIDE);
    CHECK(state.has_depth == 0ul && state.blend_enable == 0ul &&
          state.blend_control == v9x_r6_3d_blend(0ul, 0ul, 0ul));

    /* XRGB8888: ARGB8888 in d3d_order, alpha not written. */
    draw.target.format = V9X_R3D_FORMAT_XRGB8888;
    draw.target.pitch = 2560ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE &&
          state.target_format == V9X_R6_COLOR_8_8_8_8 &&
          state.target_pitch == 640ul && state.write_alpha == 0ul);
    /* A pitch not a whole 8-pixel tile, and a target off 256 bytes. */
    draw.target.pitch = 2568ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.offset = 0x80ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_TARGET);
}

static void test_depth_blend_alpha(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_r6_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;

    /* Z16 LESSEQUAL with writes: ZFUNC 3, rows padded to 8. */
    base_draw(&draw);
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    draw.depth = draw.target;
    draw.depth.object = &depth_object;
    draw.depth.offset = 0x00400000ul;
    draw.depth.height = 483ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.has_depth == 1ul &&
          state.depth_gpu == HEAP_GPU + 0x00400000ul &&
          state.depth_pitch == 640ul && state.depth_height == 488ul &&
          state.depth_control == v9x_r6_3d_depth_control(1ul, 1ul, 3ul));
    /* A Z32 buffer, and a pitch not 16 pixels. */
    CHECK(map(&draw, 0, 4ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_DEPTH_SURFACE);
    draw.depth.pitch = 1296ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_DEPTH_SURFACE);

    /* SRCALPHA, INVSRCALPHA: blending on in CB_COLOR_CONTROL. */
    base_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE &&
          state.blend_enable == 1ul &&
          state.blend_control ==
              v9x_r6_3d_blend(1ul, V9X_R6_BLEND_SRC_ALPHA,
                              V9X_R6_BLEND_ONE_MINUS_SRC_ALPHA));
    /* Destination alpha is refused. */
    draw.dst_blend = V9X_R3D_BLEND_DESTALPHA;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_BLEND);

    /* Alpha test GREATER 128: ALPHA_FUNC 4, ref 128 / 255. */
    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 128ul;
    CHECK(map(&draw, 0, 0ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE &&
          state.alpha_test_control == v9x_r6_3d_alpha_test(1ul, 4ul) &&
          state.alpha_ref == 0x3f008081ul);
}

static void test_textured(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_R6_TEXTURE texture;
    struct v9x_r6_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;

    base_draw(&draw);
    base_texture(&draw, &texture);
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE);
    CHECK(vs == V9X_R6_SLOT_VS_TEXTURED &&
          ps == V9X_R6_SLOT_PS_FIRST + V9X_R6_D3D_OP_MODULATE &&
          state.vs_gprs == 3ul && state.ps_gprs == 3ul &&
          state.interpolants == 2ul);
    CHECK(state.has_texture == 1ul &&
          state.texture_gpu == HEAP_GPU + 0x00200000ul &&
          state.texture_width == 64ul && state.texture_height == 32ul &&
          state.texture_pitch == 128ul &&
          state.texture_format == V9X_R6_COLOR_5_6_5 &&
          state.sampler_word0 ==
              v9x_r6_3d_sampler(1ul, 1ul, V9X_R6_TEX_WRAP, V9X_R6_TEX_WRAP));

    /* DECAL replaces; clamp and point filters; a mip filter samples level
     * 0 with its in-level filter. */
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE &&
          ps == V9X_R6_SLOT_PS_FIRST + V9X_R6_D3D_OP_REPLACE &&
          state.ps_gprs == 2ul &&
          state.sampler_word0 ==
              v9x_r6_3d_sampler(0ul, 0ul, V9X_R6_TEX_CLAMP_LAST_TEXEL,
                                V9X_R6_TEX_CLAMP_LAST_TEXEL));
    /* A mip filter is no magnification filter. */
    draw.texture.mag_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_FILTER);

    /* A 16-bit texture at 64 texels a row is under the 256-byte group. */
    base_texture(&draw, &texture);
    texture.pitch_texels = 64ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_SHAPE);
    /* ARGB8888 at 64 texels is right; XRGB8888 is the same, opaque. */
    texture.format = V9X_R6_COLOR_8_8_8_8;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE && state.texture_opaque == 0ul);
    texture.opaque = 1ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE && state.texture_opaque == 1ul);
    texture.opaque = 0ul;
    /* An unknown format; a keyed texture with keying on. */
    texture.format = V9X_D3D_R6_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_FORMAT);
    base_texture(&draw, &texture);
    texture.has_key = 1ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_NONE);
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_COLOR_KEY);
    /* Cylindrical wrap, and a second unit. */
    base_texture(&draw, &texture);
    draw.color_key_enable = 0ul;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_TEXTURE_ADDRESS);
    draw.texture.wrap_u = 0ul;
    draw.texture1.object = &texture_object;
    CHECK(map(&draw, &texture, 0ul, &state, &vs, &ps) ==
          V9X_D3D_EG_REFUSE_EXPLICIT);
}

static void test_explicit_and_fill(void)
{
    V9X_R3D_DRAW draw;
    V9X_R3D_VERTEX vertices[6];
    struct v9x_r6_3d_state state;
    v9x_u32 vs;
    v9x_u32 ps;

    /* Evergreen's depth-fill draw maps: scissored, ZFUNC ALWAYS with
     * writes, every colour channel masked. */
    CHECK(v9x_d3d_eg_depth_fill_draw(0x00400000ul, 1280ul, 640ul, 480ul,
                                     16ul, 8ul, 32ul, 24ul, 0xfffful,
                                     &depth_object, &draw, vertices));
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE);
    CHECK(state.scissor_left == 16ul && state.scissor_top == 8ul &&
          state.scissor_right == 32ul && state.scissor_bottom == 24ul &&
          state.masked_channels == (V9X_R6_CHANNEL_RED |
                                    V9X_R6_CHANNEL_GREEN |
                                    V9X_R6_CHANNEL_BLUE) &&
          state.has_depth == 1ul &&
          state.depth_control == v9x_r6_3d_depth_control(1ul, 1ul, 7ul));
    /* A partial write mask goes to the fallback; all three is fine. */
    draw.write_mask = V9X_D3D_EG_WRITE_RED;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_EXPLICIT);
    draw.write_mask = V9X_D3D_EG_WRITE_RED | V9X_D3D_EG_WRITE_GREEN |
                      V9X_D3D_EG_WRITE_BLUE;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_NONE &&
          state.masked_channels == 0ul);
    /* A scissor past the target. */
    draw.scissor_right = 641ul;
    CHECK(map(&draw, 0, 2ul, &state, &vs, &ps) == V9X_D3D_EG_REFUSE_EXPLICIT);
}

unsigned int v9x_run_d3d_r6_map_tests(void)
{
    failures = 0u;
    test_untextured();
    test_depth_blend_alpha();
    test_textured();
    test_explicit_and_fill();
    return failures;
}
