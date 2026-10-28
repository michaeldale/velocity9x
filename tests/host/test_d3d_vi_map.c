#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_vi_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The carve-out's high half and the heap's low half, as M-XP7053's. */
#define HEAP_HIGH 0xf4ul
#define HEAP_LOW  0x00100000ul

static int surface_object;
static int depth_object;
static int texture_object;

/* A 640x480 XRGB8888 target at the start of the heap, Gouraud, no depth,
 * no blend, no texture. */
static void base_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.offset = 0ul;
    draw->target.pitch = 2560ul;
    draw->target.width = 640ul;
    draw->target.height = 480ul;
    draw->target.format = V9X_R3D_FORMAT_XRGB8888;
    draw->target.object = &surface_object;
    draw->depth_func = V9X_R3D_CMP_LESSEQUAL;
    draw->src_blend = V9X_R3D_BLEND_ONE;
    draw->dst_blend = V9X_R3D_BLEND_ZERO;
    draw->alpha_func = V9X_R3D_CMP_ALWAYS;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
}

/* A 64x32 ARGB8888 texture, pitch 64 texels, 256-aligned. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_VI_TEXTURE *texture)
{
    draw->texture.object = &texture_object;
    draw->texture.min_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    draw->texture.wrap_u = 0ul;
    draw->texture.wrap_v = 0ul;
    draw->color_key_enable = 0ul;
    texture->format = V9X_VI_IMG_FORMAT_8_8_8_8;
    texture->has_alpha = 1ul;
    texture->width = 64ul;
    texture->height = 32ul;
    texture->pitch_texels = 64ul;
    texture->offset = 0x00200000ul;
    texture->has_key = 0ul;
    texture->key = 0ul;
    texture->levels = 1ul;
}

static struct v9x_vi_d3d_ps built;

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_VI_TEXTURE *texture, v9x_u32 depth_bytes,
                   struct v9x_vi_tl_draw *out, v9x_u32 *ps)
{
    return v9x_d3d_vi_map_draw(draw, texture, HEAP_HIGH, HEAP_LOW,
                               depth_bytes, out, ps, &built);
}

static v9x_u32 float_bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;

    converted.value = value;
    return converted.bits;
}

static void test_target(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    /* XRGB8888: 8_8_8_8 linear aligned, the heap's address, alpha not
     * written, Direct3D pixel centres, the Gouraud PS. */
    base_draw(&draw);
    draw.target.offset = 0x00001000ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.target_low == HEAP_LOW + 0x00001000ul);
    CHECK(out.target_high == HEAP_HIGH);
    CHECK(out.format == V9X_VI_CB_FORMAT_8_8_8_8);
    CHECK(out.tile_mode_index == V9X_VI_TILE_LINEAR_ALIGNED);
    CHECK(out.pitch_pixels == 640ul);
    CHECK(out.height == 480ul);
    CHECK(out.target_mask == 0x7ul);
    CHECK(out.d3d_pixel_centres == 1ul);
    CHECK(out.depth_low == 0ul);
    CHECK(out.texture_low == 0ul);
    CHECK(ps == V9X_D3D_VI_PS_GOURAUD);

    /* RGB565: 5_6_5, every channel written. */
    base_draw(&draw);
    draw.target.format = V9X_R3D_FORMAT_RGB565;
    draw.target.pitch = 1280ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.format == V9X_VI_CB_FORMAT_5_6_5);
    CHECK(out.pitch_pixels == 640ul);
    CHECK(out.target_mask == 0xful);

    /* Refused: a 1555 target; rows not whole 64 bytes; an unaligned base;
     * a base the low half would carry out of; an explicit draw. */
    base_draw(&draw);
    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.pitch = 2592ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.offset = 0x80ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.offset = 0xfff00000ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_TARGET);
    /* An explicit draw with an empty scissor. */
    base_draw(&draw);
    draw.explicit_state = 1ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_EXPLICIT);
}

/* The render interface's explicit draws: the scissor carried, the write
 * mask to CB_TARGET_MASK's B, G, R bits, none written as no mask at all;
 * a scissor outside the target, a second unit or CPU levels refused. */
static void test_explicit(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_VI_TEXTURE texture;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    base_draw(&draw);
    draw.explicit_state = 1ul;
    draw.scissor_left = 8ul;
    draw.scissor_top = 16ul;
    draw.scissor_right = 600ul;
    draw.scissor_bottom = 400ul;
    draw.write_mask = V9X_D3D_VI_WRITE_RED | V9X_D3D_VI_WRITE_GREEN |
                      V9X_D3D_VI_WRITE_BLUE;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.scissor_left == 8ul && out.scissor_top == 16ul);
    CHECK(out.scissor_right == 600ul && out.scissor_bottom == 400ul);
    CHECK(out.target_mask == 0x7ul && out.target_mask_none == 0ul);
    draw.write_mask = V9X_D3D_VI_WRITE_RED;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.target_mask == V9X_D3D_VI_CB_MASK_R);
    draw.write_mask = 0ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.target_mask_none == 1ul);
    /* RGB565 keeps its fourth bit; only B, G, R decide. */
    draw.target.format = V9X_R3D_FORMAT_RGB565;
    draw.target.pitch = 1280ul;
    draw.write_mask = V9X_D3D_VI_WRITE_BLUE;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.target_mask == (V9X_D3D_VI_CB_MASK_B | 0x8ul));
    CHECK(out.target_mask_none == 0ul);

    base_draw(&draw);
    draw.explicit_state = 1ul;
    draw.scissor_right = 641ul;
    draw.scissor_bottom = 480ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_EXPLICIT);
    draw.scissor_right = 640ul;
    base_texture(&draw, &texture);
    draw.texture.levels = (void *)&texture;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_EXPLICIT);
    draw.texture.levels = 0;
    draw.texture1.object = &texture_object;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_EXPLICIT);
}

static void test_depth(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    /* Z16, Direct3D's functions less one, its own pitch, rows padded to
     * the 8-row tile. */
    base_draw(&draw);
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    draw.depth_func = V9X_R3D_CMP_LESS;
    draw.depth.object = &depth_object;
    draw.depth.offset = 0x00400000ul;
    draw.depth.pitch = 1280ul;
    draw.target.height = 475ul;
    draw.depth.height = 475ul;
    CHECK(map(&draw, 0, 2ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.depth_low == HEAP_LOW + 0x00400000ul);
    CHECK(out.depth_high == HEAP_HIGH);
    CHECK(out.depth_func == V9X_VI_ZFUNC_LESS);
    CHECK(out.depth_write == 1ul);
    CHECK(out.depth_pitch == 640ul);
    CHECK(out.depth_height == 480ul);
    draw.depth_func = V9X_R3D_CMP_ALWAYS;
    CHECK(map(&draw, 0, 2ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.depth_func == V9X_VI_ZFUNC_ALWAYS);

    /* Depth off, or no buffer: none bound. */
    draw.depth_enable = 0ul;
    CHECK(map(&draw, 0, 2ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.depth_low == 0ul);

    /* Refused: a Z32, a pitch not whole tiles, a buffer smaller than the
     * target, a function out of range. */
    draw.depth_enable = 1ul;
    CHECK(map(&draw, 0, 4ul, &out, &ps) == V9X_D3D_VI_REFUSE_DEPTH_SURFACE);
    draw.depth.pitch = 1288ul;
    CHECK(map(&draw, 0, 2ul, &out, &ps) == V9X_D3D_VI_REFUSE_DEPTH_SURFACE);
    draw.depth.pitch = 1280ul;
    draw.depth.height = 400ul;
    CHECK(map(&draw, 0, 2ul, &out, &ps) == V9X_D3D_VI_REFUSE_DEPTH_SURFACE);
    draw.depth.height = 475ul;
    draw.depth_func = 9ul;
    CHECK(map(&draw, 0, 2ul, &out, &ps) == V9X_D3D_VI_REFUSE_DEPTH_FUNC);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_VI_TEXTURE texture;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    /* MODULATE, wrap, bilinear: the modulate PS and its VGPRs, the
     * texture at the heap's address. */
    base_draw(&draw);
    base_texture(&draw, &texture);
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_MODULATE);
    CHECK(out.ps_vgprs == 12ul);
    CHECK(out.texture_low == HEAP_LOW + 0x00200000ul);
    CHECK(out.texture_high == HEAP_HIGH);
    CHECK(out.texture_width == 64ul && out.texture_height == 32ul);
    CHECK(out.texture_pitch == 64ul);
    CHECK(out.texture_format == V9X_VI_IMG_FORMAT_8_8_8_8);
    CHECK(out.texture_tile_mode_index == V9X_VI_TILE_LINEAR_ALIGNED);
    CHECK(out.texture_bilinear == 1ul);
    CHECK(out.texture_address == V9X_VI_TEX_ADDRESS_WRAP);

    /* DECAL and COPY replace; MODULATEALPHA modulates; point filters;
     * mirror and clamp; RGB565. */
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_TEXTURE);
    CHECK(out.ps_vgprs == 8ul);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_TEXTURE);
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_MODULATE);
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.min_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_bilinear == 0ul);
    CHECK(out.texture_address == V9X_VI_TEX_ADDRESS_MIRROR);
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    texture.format = V9X_VI_IMG_FORMAT_5_6_5;
    texture.has_alpha = 0ul;
    texture.pitch_texels = 64ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_address == V9X_VI_TEX_ADDRESS_CLAMP);
    CHECK(out.texture_format == V9X_VI_IMG_FORMAT_5_6_5);

    /* Refused: an unknown format; rows not whole 64 bytes (16 texels of
     * 565 are 32); an unaligned base; ADD; border addressing; WRAPU. */
    base_draw(&draw);
    base_texture(&draw, &texture);
    texture.format = V9X_D3D_VI_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) ==
          V9X_D3D_VI_REFUSE_TEXTURE_FORMAT);
    base_texture(&draw, &texture);
    texture.format = V9X_VI_IMG_FORMAT_5_6_5;
    texture.width = 16ul;
    texture.pitch_texels = 16ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) ==
          V9X_D3D_VI_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &texture);
    texture.offset = 0x00200040ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) ==
          V9X_D3D_VI_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &texture);
    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) ==
          V9X_D3D_VI_REFUSE_TEXTURE_OP);
    base_texture(&draw, &texture);
    draw.texture.address = V9X_R3D_ADDRESS_BORDER;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) ==
          V9X_D3D_VI_REFUSE_TEXTURE_ADDRESS);
    base_texture(&draw, &texture);
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) ==
          V9X_D3D_VI_REFUSE_TEXTURE_ADDRESS);
    base_texture(&draw, &texture);
    draw.texture.mag_filter = 0ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) ==
          V9X_D3D_VI_REFUSE_TEXTURE_FILTER);
}

/* A chain: the minification filter's mip filter over its levels; one
 * level, or a filter without one, samples level 0 alone. */
static void test_mips(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_VI_TEXTURE texture;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    base_draw(&draw);
    base_texture(&draw, &texture);
    texture.levels = 7ul;
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_last_level == 6ul && out.texture_base_level == 0ul);
    CHECK(out.texture_mip_filter == V9X_VI_MIP_FILTER_LINEAR);
    CHECK(out.texture_bilinear == 1ul);
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_mip_filter == V9X_VI_MIP_FILTER_POINT);
    CHECK(out.texture_bilinear == 0ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_last_level == 0ul);
    CHECK(out.texture_mip_filter == V9X_VI_MIP_FILTER_NONE);
    texture.levels = 1ul;
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_last_level == 0ul);
    CHECK(out.texture_mip_filter == V9X_VI_MIP_FILTER_NONE);
}

/* Specular, on by default under DirectX 5 and 6: each PS's specular
 * variant and its VGPRs. */
static void test_specular(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_VI_TEXTURE texture;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    base_draw(&draw);
    draw.specular_enable = 1ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_GOURAUD_SPECULAR);
    CHECK(out.ps_vgprs == 12ul);
    base_texture(&draw, &texture);
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_TEXTURE_SPECULAR);
    CHECK(out.ps_vgprs == 12ul);
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_MODULATE_SPECULAR);
    CHECK(out.ps_vgprs == 16ul);
}

/* Blending: Direct3D's factors to CB_BLEND0_CONTROL's, the colour's and
 * the alpha's alike; the destination-alpha factors are refused, as on
 * Cedar (an XRGB target has no alpha to read). */
static void test_blend(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    base_draw(&draw);
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.blend_control == 0ul);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.blend_control == 0x45040504ul);
    draw.src_blend = V9X_R3D_BLEND_DESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.blend_control == v9x_vi_3d_blend(V9X_VI_BLEND_DST_COLOR,
                                               V9X_VI_BLEND_ZERO));
    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    draw.dst_blend = V9X_R3D_BLEND_SRCALPHASAT;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_DESTALPHA;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_INVDESTALPHA;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_BLEND);
}

/* The alpha test, fog and the colour key take the built PS, its constants
 * as float bits in memory order. */
static void test_built(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_VI_TEXTURE texture;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 128ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_BUILT);
    CHECK(built.op == V9X_VI_PS_COLOUR);
    CHECK(built.alpha_test == 1ul);
    CHECK(built.alpha_func == V9X_VI_VOPC_CMPX_GT_F32);
    CHECK(built.alpha_ref == float_bits(128.0f / 255.0f));
    CHECK(built.fog == 0ul && built.key == 0ul && built.specular == 0ul);
    CHECK(out.ps_kill == 1ul);
    CHECK(out.ps_vgprs == V9X_VI_D3D_PS_BUILT_VGPRS);
    draw.alpha_func = V9X_R3D_CMP_ALWAYS;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(built.alpha_func == V9X_VI_VOPC_CMPX_TRU_F32);
    draw.alpha_func = 0ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_ALPHA_TEST);

    /* Fog: red, in memory order B, G, R; no kill. With specular. */
    base_draw(&draw);
    draw.fog_enable = 1ul;
    draw.fog_color = 0x00ff0000ul;
    draw.specular_enable = 1ul;
    CHECK(map(&draw, 0, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_BUILT);
    CHECK(built.fog == 1ul && built.specular == 1ul);
    CHECK(built.fog_colour[0] == 0ul && built.fog_colour[1] == 0ul);
    CHECK(built.fog_colour[2] == 0x3f800000ul);
    CHECK(out.ps_kill == 0ul);

    /* A key in RGB565, pure green: B 0, G 1, R 0, the tolerance half a
     * step of green's 63, squared. */
    base_draw(&draw);
    base_texture(&draw, &texture);
    texture.format = V9X_VI_IMG_FORMAT_5_6_5;
    texture.has_alpha = 0ul;
    texture.has_key = 1ul;
    texture.key = 0x07e0ul;
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_BUILT);
    CHECK(built.op == V9X_VI_PS_MODULATE);
    CHECK(built.key == 1ul);
    CHECK(built.key_colour[0] == 0ul);
    CHECK(built.key_colour[1] == 0x3f800000ul);
    CHECK(built.key_colour[2] == 0ul);
    CHECK(built.key_tolerance2 ==
          float_bits((1.0f / 126.0f) * (1.0f / 126.0f)));
    CHECK(out.ps_kill == 1ul);
    /* A key on a texture without one is no key. */
    texture.has_key = 0ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_MODULATE);

    /* ARGB1555 and ARGB4444, 32 texels a row being 64 bytes. */
    base_texture(&draw, &texture);
    draw.color_key_enable = 0ul;
    texture.format = V9X_VI_IMG_FORMAT_1_5_5_5;
    texture.width = 32ul;
    texture.pitch_texels = 32ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_format == V9X_VI_IMG_FORMAT_1_5_5_5);
    texture.format = V9X_VI_IMG_FORMAT_4_4_4_4;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_format == V9X_VI_IMG_FORMAT_4_4_4_4);
}

/* DECALALPHA takes the built PS with nothing else on it, specular kept;
 * an 8_8_8_8 texture without alpha (XRGB8888) has the T# read W as 1,
 * one with it does not. */
static void test_decal_alpha_and_opaque(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_VI_TEXTURE texture;
    struct v9x_vi_tl_draw out;
    v9x_u32 ps;

    base_draw(&draw);
    base_texture(&draw, &texture);
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_BUILT);
    CHECK(built.op == V9X_VI_PS_DECALALPHA);
    CHECK(built.alpha_test == 0ul && built.fog == 0ul && built.key == 0ul);
    CHECK(built.specular == 0ul);
    CHECK(out.ps_kill == 0ul);
    CHECK(out.ps_vgprs == V9X_VI_D3D_PS_BUILT_VGPRS);
    CHECK(out.texture_opaque == 0ul);
    draw.specular_enable = 1ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_BUILT && built.specular == 1ul);

    base_draw(&draw);
    base_texture(&draw, &texture);
    texture.has_alpha = 0ul;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(ps == V9X_D3D_VI_PS_MODULATE);
    CHECK(out.texture_format == V9X_VI_IMG_FORMAT_8_8_8_8);
    CHECK(out.texture_opaque == 1ul);
    texture.format = V9X_VI_IMG_FORMAT_5_6_5;
    CHECK(map(&draw, &texture, 0ul, &out, &ps) == V9X_D3D_VI_REFUSE_NONE);
    CHECK(out.texture_opaque == 0ul);
}

unsigned int v9x_run_d3d_vi_map_tests(void)
{
    failures = 0u;
    test_target();
    test_depth();
    test_texture();
    test_mips();
    test_specular();
    test_blend();
    test_built();
    test_decal_alpha_and_opaque();
    test_explicit();
    return failures;
}
