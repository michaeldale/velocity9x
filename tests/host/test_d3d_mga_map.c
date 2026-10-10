#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_mga_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static int dummy_object;

/* An 800x600 565 back buffer below the screen at the BIOS's padded 1920
 * bytes, a Z buffer after it at the same pitch, and no texture. */
static void base_draw(V9X_R3D_DRAW *draw, V9X_D3D_MGA_TEXTURE *texture)
{
    memset(draw, 0, sizeof(*draw));
    memset(texture, 0, sizeof(*texture));
    draw->target.offset = 1920ul * 600ul;
    draw->target.pitch = 1920ul;
    draw->target.width = 800ul;
    draw->target.height = 600ul;
    draw->target.format = V9X_R3D_FORMAT_RGB565;
    draw->depth.offset = 2ul * 1920ul * 600ul;
    draw->depth.pitch = 1920ul;
    draw->depth.object = &dummy_object;
    draw->depth_func = V9X_R3D_CMP_LESSEQUAL;
    draw->depth_write = 1ul;
    draw->shade_mode = V9X_R3D_SHADE_GOURAUD;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    draw->alpha_func = V9X_R3D_CMP_ALWAYS;
}

static void bind_texture(V9X_R3D_DRAW *draw, V9X_D3D_MGA_TEXTURE *texture,
                         v9x_u32 format)
{
    draw->texture.object = &dummy_object;
    texture->valid = 1ul;
    texture->format = format;
    texture->offset = 0x00600000ul;
    texture->log2_width = 6ul;
    texture->log2_height = 5ul;
    texture->log2_pitch = 6ul;
}

static void test_plain(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MGA_TEXTURE texture;
    V9X_D3D_MGA_MAPPED mapped;

    base_draw(&draw, &texture);
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.depth == V9X_MGA3D_DEPTH_NONE);
    CHECK(mapped.base.shade == V9X_MGA3D_SHADE_GOURAUD);
    CHECK(mapped.base.texture.enabled == 0ul);
    CHECK(mapped.stipple == 0ul && mapped.skip == 0ul && mapped.flat == 0ul);

    /* Depth on: ZORG is the gap to the target, 512-aligned here. */
    draw.depth_enable = 1ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.depth == V9X_MGA3D_DEPTH_16);
    CHECK(mapped.base.zmode == V9X_MGA3D_ZMODE_ZLTE);
    CHECK(mapped.base.z_write == 1ul);
    CHECK(mapped.base.z_offset == 2ul * 1920ul * 600ul);

    /* A Z buffer at another pitch, or off the alignment, is refused. */
    draw.depth.pitch = 1600ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_DEPTH);
    draw.depth.pitch = 1920ul;
    draw.depth.offset += 0x100ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_DEPTH);
    draw.depth.offset -= 0x100ul;

    /* NEVER draws nothing. */
    draw.depth_func = V9X_R3D_CMP_NEVER;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.skip == 1ul);

    /* 1555 targets dither 5:5:5; other targets are refused. */
    base_draw(&draw, &texture);
    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.dither_555 == 1ul);
    draw.target.format = 7ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_TARGET);

    /* A pitch the linearizer has no entry for. */
    base_draw(&draw, &texture);
    draw.target.pitch = 1000ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_TARGET_SHAPE);
}

static void test_blend_fog_specular(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MGA_TEXTURE texture;
    V9X_D3D_MGA_MAPPED mapped;

    base_draw(&draw, &texture);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.stipple == 1ul);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_BLEND);
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.stipple == 0ul);

    base_draw(&draw, &texture);
    draw.fog_enable = 1ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_FOG);
    base_draw(&draw, &texture);
    draw.specular_enable = 1ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0x10ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_SPECULAR);

    /* Stipple densities. */
    CHECK(v9x_d3d_mga_stipple(255ul) == 0ul);
    CHECK(v9x_d3d_mga_stipple(128ul) == 1ul);
    CHECK(v9x_d3d_mga_stipple(64ul) == 3ul);
    CHECK(v9x_d3d_mga_stipple(20ul) == 7ul);
    CHECK(v9x_d3d_mga_stipple(5ul) == V9X_D3D_MGA_STIPPLE_NONE);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MGA_TEXTURE texture;
    V9X_D3D_MGA_MAPPED mapped;

    base_draw(&draw, &texture);
    bind_texture(&draw, &texture, V9X_MGA3D_TEX_TW16);
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.enabled == 1ul);
    CHECK(mapped.base.texture.modulate == 1ul);
    CHECK(mapped.base.texture.log2_width == 6ul);
    CHECK(mapped.base.texture.log2_height == 5ul);
    /* No key: a zero mask with a key no texel matches. */
    CHECK(mapped.base.texture.key_mask == 0ul);
    CHECK(mapped.base.texture.key == 1ul);

    /* Decal shows every texel: tamask 0, takey 1. */
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.modulate == 0ul);
    CHECK(mapped.base.texture.alpha_mask == 0ul);
    CHECK(mapped.base.texture.alpha_key == 1ul);

    /* DECALALPHA on 1555: Gouraud where the alpha bit is 0. */
    texture.format = V9X_MGA3D_TEX_TW15;
    draw.texture.op = V9X_R3D_TEXOP_DECALALPHA;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.alpha_mask == 1ul);
    CHECK(mapped.base.texture.alpha_key == 0ul);

    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_TEXTURE_OP);

    /* Clamp; mirror refused. */
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.clamp_u == 1ul &&
          mapped.base.texture.clamp_v == 1ul);
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_ADDRESS);

    /* A texture the sampler cannot read. */
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    texture.valid = 0ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_TEXTURE);

    /* Colour key from the surface. */
    texture.valid = 1ul;
    texture.format = V9X_MGA3D_TEX_TW16;
    texture.has_color_key = 1ul;
    texture.color_key = 0x0000f81ful;
    draw.color_key_enable = 1ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.key == 0xf81ful);
    CHECK(mapped.base.texture.key_mask == 0xfffful);

    /* On 1555 the key is a colour: the alpha bit is left out of the
     * compare, as Matrox's HAL does on the same card (V9XDDP ColorKeyOk,
     * texel FC1Fh against key 7C1Fh, boot 383). */
    texture.format = V9X_MGA3D_TEX_TW15;
    texture.color_key = 0x00007c1ful;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.key == 0x7c1ful);
    CHECK(mapped.base.texture.key_mask == 0x7ffful);
}

static void test_alpha(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MGA_TEXTURE texture;
    V9X_D3D_MGA_MAPPED mapped;

    /* An alpha test with unknown vertex alpha is refused. */
    base_draw(&draw, &texture);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_ALPHA_TEST);

    /* Opaque vertices, 1555 texels: the alpha-0 texels are keyed out. */
    draw.vertex_alpha_opaque = 1ul;
    bind_texture(&draw, &texture, V9X_MGA3D_TEX_TW15);
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.key_mask == 0x8000ul);
    CHECK(mapped.base.texture.key == 0ul);

    /* 565 texels are opaque: the test passes everything. */
    texture.format = V9X_MGA3D_TEX_TW16;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.key_mask == 0ul);

    /* A test nothing opaque passes skips the draw. */
    draw.alpha_func = V9X_R3D_CMP_LESS;
    draw.alpha_ref = 10ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_ALPHA_TEST);
    draw.alpha_func = V9X_R3D_CMP_NEVER;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.skip == 1ul);

    /* Both keys wanted: refused. */
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0ul;
    texture.format = V9X_MGA3D_TEX_TW15;
    texture.has_color_key = 1ul;
    draw.color_key_enable = 1ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_KEYS);

    /* Explicit draws go to the CPU. */
    base_draw(&draw, &texture);
    draw.explicit_state = 1ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_EXPLICIT);
}

/*
 * Additive blending, which the chip does not have, on a texture: black adds
 * nothing, so its texels are keyed out and the rest drawn over the
 * destination. Exact for Half-Life's black-backed HUD sprites (A8U4I5 boot
 * 388 refused 33,449 ONE/ONE draws, leaving no HUD); elsewhere a colour
 * replaces where it should add.
 */
static void test_additive(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_MGA_TEXTURE texture;
    V9X_D3D_MGA_MAPPED mapped;

    /* ONE/ONE on 565: key 0 over all 16 bits, no stipple. */
    base_draw(&draw, &texture);
    bind_texture(&draw, &texture, V9X_MGA3D_TEX_TW16);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.stipple == 0ul);
    CHECK(mapped.base.texture.key == 0ul);
    CHECK(mapped.base.texture.key_mask == 0xfffful);

    /* On 1555 the colour bits only. */
    texture.format = V9X_MGA3D_TEX_TW15;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.base.texture.key == 0ul);
    CHECK(mapped.base.texture.key_mask == 0x7ffful);

    /* SRCALPHA/ONE: the same, at the stipple density of the alpha. */
    texture.format = V9X_MGA3D_TEX_TW16;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_NONE);
    CHECK(mapped.stipple == 1ul);
    CHECK(mapped.base.texture.key_mask == 0xfffful);

    /* With a colour key as well: two keys, refused. */
    draw.src_blend = V9X_R3D_BLEND_ONE;
    texture.has_color_key = 1ul;
    texture.color_key = 0x0000f81ful;
    draw.color_key_enable = 1ul;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_KEYS);

    /* Untextured: nothing to key, refused. */
    base_draw(&draw, &texture);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(v9x_d3d_mga_map_draw(&draw, &texture, 0x00800000ul, 0ul,
                               &mapped) == V9X_D3D_MGA_REFUSE_BLEND);
}

unsigned int v9x_run_d3d_mga_map_tests(void)
{
    failures = 0u;
    test_plain();
    test_blend_fog_specular();
    test_texture();
    test_alpha();
    test_additive();
    return failures;
}
