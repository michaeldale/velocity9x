#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_nv50_map.h"
#include "velocity9x/r3d_abi.h"

/*
 * The neutral draw to the GT218's Tesla state
 * (src\display32\d3d\d3d_nv50_map.c): targets, blends, the alpha test,
 * textures and the fragment program variant, and every refusal.
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static int surface_object;
static int depth_object;
static int texture_object;

static v9x_u32 registers[V9X_NV50_D3D_FP_VARIANTS] = {
    5ul, 16ul, 15ul, 16ul, 12ul, 16ul, 15ul, 16ul, 12ul, 16ul, 15ul, 16ul,
    12ul, 16ul, 15ul, 16ul
};

static v9x_u32 float_bits(float value)
{
    union {
        float f;
        v9x_u32 u;
    } converted;

    converted.f = value;
    return converted.u;
}

/* A 640x480 RGB565 target at 1 MiB, Gouraud, no depth, no blend, no
 * texture. */
static void base_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.offset = 0x00100000ul;
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

/* A 64x32 ARGB8888 texture, rows of 256 bytes, at 2 MiB. */
static void base_texture(V9X_R3D_DRAW *draw, V9X_D3D_NV50_TEXTURE *texture)
{
    draw->texture.object = &texture_object;
    draw->texture.min_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    draw->texture.op = V9X_R3D_TEXOP_MODULATE;
    draw->texture.address = V9X_R3D_ADDRESS_WRAP;
    draw->texture.wrap_u = 0ul;
    draw->texture.wrap_v = 0ul;
    draw->color_key_enable = 0ul;
    texture->format = V9X_NV50_D3D_TEX_B8G8R8A8;
    texture->width = 64ul;
    texture->height = 32ul;
    texture->pitch = 256ul;
    texture->offset = 0x00200000ul;
    texture->has_key = 0ul;
    texture->key = 0ul;
    texture->cube_address = 0ul;
    texture->mip_address = 0ul;
    texture->mip_levels = 0ul;
    texture->mip_tile_mode = 0ul;
}

/* Room for a two-unit draw's two entries. */
static v9x_u32 tic[2ul * V9X_NV50_3D_TIC_WORDS];
static v9x_u32 tsc[2ul * V9X_NV50_3D_TSC_WORDS];

static v9x_u32 map(const V9X_R3D_DRAW *draw,
                   const V9X_D3D_NV50_TEXTURE *texture,
                   struct v9x_nv50_d3d_state *state, v9x_u32 *specular)
{
    return v9x_d3d_nv50_map_draw(draw, texture, 0, registers, state, tic,
                                 tsc, specular);
}

/* A 640x480 Z16 buffer's shadow view: the colour through the kind-70h
 * window, the Z through the Z16 window, both tile mode 10h. */
static void base_shadow(V9X_D3D_NV50_SHADOW_VIEW *view)
{
    memset(view, 0, sizeof(*view));
    view->colour.address = V9X_NV50_VM_TILED + 0x00500000ul;
    view->colour.width = 640ul;
    view->colour.height = 480ul;
    view->colour.tile_mode = V9X_D3D_NV50_TILE_MODE;
    view->colour.tiled = 1ul;
    view->zeta.address = V9X_NV50_VM_Z16 + 0x00400000ul;
    view->zeta.width = 640ul;
    view->zeta.height = 480ul;
    view->zeta.tile_mode = V9X_D3D_NV50_TILE_MODE;
    view->zeta.tiled = 1ul;
    view->zeta_format = V9X_NV50_3D_ZETA_Z16;
    view->with_zeta = 1ul;
}

static v9x_u32 map_shadow(const V9X_R3D_DRAW *draw,
                          const V9X_D3D_NV50_SHADOW_VIEW *view,
                          struct v9x_nv50_d3d_state *state)
{
    v9x_u32 specular;

    return v9x_d3d_nv50_map_draw(draw, 0, view, registers, state, tic, tsc,
                                 &specular);
}

static void test_target(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_nv50_d3d_state state;
    v9x_u32 specular = 7ul;

    base_draw(&draw);
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.target.address == 0x00100000ul);
    CHECK(state.target.pitch == 1280ul && state.target.width == 640ul &&
          state.target.height == 480ul && state.target.tiled == 0ul);
    CHECK(state.target_format == V9X_NV50_3D_RT_B5G6R5);
    CHECK(state.scissor_left == 0ul && state.scissor_top == 0ul &&
          state.scissor_right == 640ul && state.scissor_bottom == 480ul);
    CHECK(state.color_mask == V9X_NV50_D3D_MASK_RGB);
    CHECK(state.has_zeta == 0ul && state.blend == 0ul &&
          state.alpha_test == 0ul && state.textured == 0ul);
    CHECK(state.fp_variant == 0ul && state.fp_registers == 5ul);
    CHECK(specular == 0ul);

    draw.target.format = V9X_R3D_FORMAT_XRGB8888;
    draw.target.pitch = 2560ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.target_format == V9X_NV50_3D_RT_BGRX8);

    /* XRGB1555, a pitch off 64 bytes, a base off 256, a pitch narrower
     * than the width: refused. */
    draw.target.format = V9X_R3D_FORMAT_XRGB1555;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.pitch = 1300ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.offset = 0x00100080ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_TARGET);
    base_draw(&draw);
    draw.target.pitch = 1216ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_TARGET);
}

static void test_depth(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_nv50_d3d_state state;
    v9x_u32 specular;

    V9X_D3D_NV50_SHADOW_VIEW view;

    /* With no shadow, a Z-tested draw on a bound buffer is refused; a
     * bound buffer with Z off draws without it. */
    base_draw(&draw);
    draw.depth.object = &depth_object;
    draw.depth.offset = 0x00300000ul;
    draw.depth.pitch = 1280ul;
    draw.depth.width = 640ul;
    draw.depth.height = 480ul;
    draw.depth_enable = 1ul;
    draw.depth_write = 1ul;
    CHECK(map(&draw, 0, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_DEPTH_SURFACE);
    draw.depth_enable = 0ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.has_zeta == 0ul);

    /* Step 5b: with the shadow, the draw goes to the tiled colour with
     * the tiled Z, its function and write as asked; the scissor stays
     * the target's. */
    base_shadow(&view);
    draw.depth_enable = 1ul;
    draw.depth_func = V9X_R3D_CMP_LESSEQUAL;
    CHECK(map_shadow(&draw, &view, &state) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.target.address == view.colour.address &&
          state.target.tiled == 1ul &&
          state.target.tile_mode == V9X_D3D_NV50_TILE_MODE &&
          state.target.width == 640ul && state.target.height == 480ul);
    CHECK(state.target_format == V9X_NV50_3D_RT_B5G6R5);
    CHECK(state.has_zeta == 1ul &&
          state.zeta.address == view.zeta.address &&
          state.zeta_format == V9X_NV50_3D_ZETA_Z16);
    CHECK(state.depth_test == 1ul && state.depth_write == 1ul &&
          state.depth_func == 0x0203ul);
    CHECK(state.scissor_right == 640ul && state.scissor_bottom == 480ul);
    draw.depth_write = 0ul;
    CHECK(map_shadow(&draw, &view, &state) == V9X_D3D_NV50_REFUSE_NONE &&
          state.depth_write == 0ul);
    draw.depth_func = 0ul;
    CHECK(map_shadow(&draw, &view, &state) ==
          V9X_D3D_NV50_REFUSE_DEPTH_FUNC);

    /* A shadow smaller than the target cannot hold it. */
    draw.depth_func = V9X_R3D_CMP_LESS;
    view.colour.width = 320ul;
    CHECK(map_shadow(&draw, &view, &state) == V9X_D3D_NV50_REFUSE_TARGET);

    /* The shadow holding the target, the draw without Z: into the
     * shadow, untested. */
    base_shadow(&view);
    view.with_zeta = 0ul;
    draw.depth_enable = 0ul;
    CHECK(map_shadow(&draw, &view, &state) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.target.address == view.colour.address &&
          state.has_zeta == 0ul);
    /* ... but a Z-tested draw needs a shadow with its Z buffer. */
    draw.depth_enable = 1ul;
    CHECK(map_shadow(&draw, &view, &state) ==
          V9X_D3D_NV50_REFUSE_DEPTH_SURFACE);
}

static void test_tiled(void)
{
    V9X_D3D_NV50_SHADOW_VIEW view;
    struct v9x_nv50_d3d_state state;
    V9X_R3D_VERTEX vertices[6];
    v9x_u32 pitch;
    v9x_u32 rows;
    v9x_u32 index;

    /* Rows of 64 bytes and whole 8-row blocks, as the probe's 128x72
     * Z16 and colour surfaces were. */
    v9x_d3d_nv50_tiled_layout(640ul, 480ul, 2ul, &pitch, &rows);
    CHECK(pitch == 1280ul && rows == 480ul);
    v9x_d3d_nv50_tiled_layout(100ul, 75ul, 4ul, &pitch, &rows);
    CHECK(pitch == 448ul && rows == 80ul);
    v9x_d3d_nv50_tiled_layout(1ul, 1ul, 2ul, &pitch, &rows);
    CHECK(pitch == 64ul && rows == 8ul);

    /* A depth fill: Z ALWAYS with writes, colour masked, the scissor the
     * rectangle, two triangles at value / 65535, no half-pixel move. */
    base_shadow(&view);
    CHECK(v9x_d3d_nv50_depth_fill_draw(&view, 10ul, 20ul, 110ul, 70ul,
                                       0xfffful, registers, &state,
                                       vertices));
    CHECK(state.target.address == view.colour.address &&
          state.target.tiled == 1ul);
    CHECK(state.color_mask == 0ul);
    CHECK(state.has_zeta == 1ul && state.depth_test == 1ul &&
          state.depth_write == 1ul &&
          state.depth_func == V9X_NV50_D3D_CMP_ALWAYS);
    CHECK(state.scissor_left == 10ul && state.scissor_top == 20ul &&
          state.scissor_right == 110ul && state.scissor_bottom == 70ul);
    CHECK(state.blend == 0ul && state.alpha_test == 0ul &&
          state.textured == 0ul && state.fp_variant == 0ul &&
          state.fp_registers == registers[0]);
    for (index = 0ul; index < 6ul; ++index) {
        CHECK(vertices[index].sz == 1.0f && vertices[index].rhw == 1.0f);
    }
    CHECK(vertices[0].sx == 10.0f && vertices[0].sy == 20.0f);
    CHECK(vertices[4].sx == 110.0f && vertices[4].sy == 70.0f);
    CHECK(v9x_d3d_nv50_depth_fill_draw(&view, 0ul, 0ul, 640ul, 480ul, 0ul,
                                       registers, &state, vertices) &&
          vertices[0].sz == 0.0f);
    /* Empty, or past the buffer. */
    CHECK(!v9x_d3d_nv50_depth_fill_draw(&view, 5ul, 5ul, 5ul, 9ul, 0ul,
                                        registers, &state, vertices));
    CHECK(!v9x_d3d_nv50_depth_fill_draw(&view, 0ul, 0ul, 641ul, 9ul, 0ul,
                                        registers, &state, vertices));
}

static void test_blend_and_alpha(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_nv50_d3d_state state;
    v9x_u32 specular;

    base_draw(&draw);
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.blend == 1ul &&
          state.blend_src == V9X_NV50_D3D_BLEND_SRC_ALPHA &&
          state.blend_dst == V9X_NV50_D3D_BLEND_ONE_MINUS_SRC_ALPHA);
    draw.src_blend = V9X_R3D_BLEND_DESTCOLOR;
    draw.dst_blend = V9X_R3D_BLEND_SRCCOLOR;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.blend_src == V9X_NV50_D3D_BLEND_DST_COLOR &&
          state.blend_dst == V9X_NV50_D3D_BLEND_SRC_COLOR);
    draw.src_blend = V9X_R3D_BLEND_SRCALPHASAT;
    draw.dst_blend = V9X_R3D_BLEND_ONE;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.blend_src == V9X_NV50_D3D_BLEND_SRC_ALPHA_SATURATE &&
          state.blend_dst == V9X_NV50_D3D_BLEND_ONE);
    /* Destination alpha: neither target holds it. SRCALPHASAT is a
     * source factor only. */
    draw.src_blend = V9X_R3D_BLEND_DESTALPHA;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_BLEND);
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_SRCALPHASAT;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_BLEND);

    /* The alpha test: Direct3D's NEVER..ALWAYS as 200h..207h, the
     * reference on 0..1. */
    base_draw(&draw);
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATER;
    draw.alpha_ref = 0x80ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.alpha_test == 1ul && state.alpha_func == 0x0204ul &&
          state.alpha_ref == float_bits(128.0f / 255.0f));
    draw.alpha_func = V9X_R3D_CMP_NEVER;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE &&
          state.alpha_func == V9X_NV50_D3D_CMP_NEVER);
    draw.alpha_func = V9X_R3D_CMP_ALWAYS;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE &&
          state.alpha_func == V9X_NV50_D3D_CMP_ALWAYS);
    draw.alpha_func = 0ul;
    CHECK(map(&draw, 0, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_ALPHA_FUNC);
}

static void test_texture(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_NV50_TEXTURE texture;
    struct v9x_nv50_d3d_state state;
    v9x_u32 specular;
    v9x_u32 expect_tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 expect_tsc[V9X_NV50_3D_TSC_WORDS];
    v9x_u32 index;

    base_draw(&draw);
    base_texture(&draw, &texture);
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.textured == 1ul);
    CHECK(state.fp_variant ==
          v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 0ul, 0ul));
    CHECK(state.fp_registers == registers[state.fp_variant]);
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B8G8R8A8, 0x00200000ul,
                               256ul, 64ul, 32ul, V9X_NV50_D3D_WRAP_REPEAT,
                               1ul, 1ul, expect_tic, expect_tsc));
    for (index = 0ul; index < V9X_NV50_3D_TIC_WORDS; ++index) {
        CHECK(tic[index] == expect_tic[index]);
        CHECK(tsc[index] == expect_tsc[index]);
    }

    /* A cube: the same texture's TIC turned into its tiled copy's cube
     * (nv50_d3d.h), wrap or not; then back to an ordinary texture. */
    texture.cube_address = 0x20400000ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
    v9x_nv50_d3d_tic_cube(0x20400000ul, V9X_NV50_D3D_CUBE_TILE_MODE,
                          expect_tic, expect_tsc);
    for (index = 0ul; index < V9X_NV50_3D_TIC_WORDS; ++index) {
        CHECK(tic[index] == expect_tic[index]);
        CHECK(tsc[index] == expect_tsc[index]);
    }
    texture.cube_address = 0ul;

    /* A mipmapped texture with a pooled chain: a mip min filter samples
     * the chain (nearest between levels for MIPNEAREST and MIPLINEAR,
     * linear for the LINEARMIP pair); a plain filter keeps level 0. */
    texture.mip_address = 0x20600000ul;
    texture.mip_levels = 6ul;
    texture.mip_tile_mode = 0x30ul;
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B8G8R8A8, 0x00200000ul,
                               256ul, 64ul, 32ul, V9X_NV50_D3D_WRAP_REPEAT,
                               0ul, 0ul, expect_tic, expect_tsc));
    v9x_nv50_d3d_tic_mip(0x20600000ul, 0x30ul, 5ul, 0ul, expect_tic,
                         expect_tsc);
    for (index = 0ul; index < V9X_NV50_3D_TIC_WORDS; ++index) {
        CHECK(tic[index] == expect_tic[index]);
        CHECK(tsc[index] == expect_tsc[index]);
    }
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
    CHECK((tsc[1] & 0xc0ul) == 0xc0ul && tic[1] == 0x20600000ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEAR;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
    CHECK(tic[1] == 0x00200000ul && (tsc[1] & 0xc0ul) == 0x40ul);
    draw.texture.mag_filter = V9X_R3D_FILTER_LINEAR;
    texture.mip_address = 0ul;
    texture.mip_levels = 0ul;
    texture.mip_tile_mode = 0ul;

    /* DECAL and COPY replace; MODULATEALPHA modulates. */
    draw.texture.op = V9X_R3D_TEXOP_DECAL;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_REPLACE, 0ul, 0ul));
    draw.texture.op = V9X_R3D_TEXOP_COPY;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_REPLACE, 0ul, 0ul));
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 0ul, 0ul));
    draw.texture.op = V9X_R3D_TEXOP_ADD;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_OP);

    /* DX5 MODULATE on a texture with alpha keeps the texel's alpha
     * (docs\issues\2026-11-17-oland-nature-tree-leaves-missing-most-
     * frames.md) where the alpha test or a blend reads it. */
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;
    draw.alpha_test_enable = 1ul;
    draw.alpha_func = V9X_R3D_CMP_GREATEREQUAL;
    draw.alpha_ref = 0x7ful;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE_RGB, 0ul,
                                      0ul));
    CHECK(state.fp_registers == registers[state.fp_variant]);
    draw.alpha_test_enable = 0ul;
    draw.blend_enable = 1ul;
    draw.src_blend = V9X_R3D_BLEND_SRCALPHA;
    draw.dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE_RGB, 0ul,
                                      0ul));
    /* MODULATEALPHA, the core's diffuse-alpha stage and a texture
     * without alpha multiply all four. */
    draw.texture.op = V9X_R3D_TEXOP_MODULATEALPHA;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 0ul, 0ul));
    draw.texture.op = V9X_R3D_TEXOP_MODULATE;
    draw.texture.alpha_diffuse = 1ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 0ul, 0ul));
    draw.texture.alpha_diffuse = 0ul;
    texture.format = V9X_NV50_D3D_TEX_B8G8R8X8;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 0ul, 0ul));
    /* Neither the test nor a blend: the alpha reaches nothing. */
    texture.format = V9X_NV50_D3D_TEX_B8G8R8A8;
    draw.blend_enable = 0ul;
    draw.src_blend = V9X_R3D_BLEND_ONE;
    draw.dst_blend = V9X_R3D_BLEND_ZERO;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_MODULATE, 0ul, 0ul));

    /* Address modes; cylindrical wrap is refused. */
    base_texture(&draw, &texture);
    draw.texture.address = V9X_R3D_ADDRESS_CLAMP;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          (tsc[0] & 7ul) == V9X_NV50_D3D_WRAP_CLAMP);
    draw.texture.address = V9X_R3D_ADDRESS_MIRROR;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          (tsc[0] & 7ul) == V9X_NV50_D3D_WRAP_MIRROR);
    draw.texture.address = V9X_R3D_ADDRESS_BORDER;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_ADDRESS);
    draw.texture.address = V9X_R3D_ADDRESS_WRAP;
    draw.texture.wrap_u = 1ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_ADDRESS);

    /* Filters: level 0 alone, so a mip filter reads as its filter within
     * the level; a mip filter as magnification is refused. */
    base_texture(&draw, &texture);
    draw.texture.mag_filter = V9X_R3D_FILTER_NEAREST;
    draw.texture.min_filter = V9X_R3D_FILTER_MIPNEAREST;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          tsc[1] == 0x51ul);
    draw.texture.min_filter = V9X_R3D_FILTER_LINEARMIPLINEAR;
    CHECK(map(&draw, &texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          tsc[1] == 0x61ul);
    draw.texture.mag_filter = V9X_R3D_FILTER_MIPLINEAR;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_FILTER);

    /* Shape and format. */
    base_texture(&draw, &texture);
    texture.format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_FORMAT);
    CHECK(map(&draw, 0, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_FORMAT);
    /* A signed texture only a shader stage reads: refused by the
     * fixed-function map (the core draws it through draw_shader), taken
     * by the shader path's unit, with the format's own TIC. */
    base_texture(&draw, &texture);
    texture.format = V9X_NV50_D3D_TEX_Q8W8V8U8;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_FORMAT);
    {
        v9x_u32 unit_tic[V9X_NV50_3D_TIC_WORDS];
        v9x_u32 unit_tsc[V9X_NV50_3D_TSC_WORDS];
        v9x_u32 want_tic[V9X_NV50_3D_TIC_WORDS];
        v9x_u32 want_tsc[V9X_NV50_3D_TSC_WORDS];

        CHECK(v9x_d3d_nv50_map_unit(&draw.texture, &texture, unit_tic,
                                    unit_tsc) == V9X_D3D_NV50_REFUSE_NONE);
        CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_Q8W8V8U8, 0x00200000ul,
                                   256ul, 64ul, 32ul,
                                   V9X_NV50_D3D_WRAP_REPEAT, 1ul, 1ul,
                                   want_tic, want_tsc));
        CHECK(unit_tic[0] == want_tic[0]);
        /* V8U8 at two bytes a texel: a 256-byte pitch holds 64 texels. */
        texture.format = V9X_NV50_D3D_TEX_V8U8;
        CHECK(v9x_d3d_nv50_map_unit(&draw.texture, &texture, unit_tic,
                                    unit_tsc) == V9X_D3D_NV50_REFUSE_NONE);
    }
    base_texture(&draw, &texture);
    texture.pitch = 200ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &texture);
    texture.offset = 0x00200040ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &texture);
    texture.width = 4096ul;
    texture.pitch = 16384ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE);
    base_texture(&draw, &texture);
    texture.pitch = 192ul; /* narrower than 64 texels of 4 bytes */
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE);

    /* A colour key in use waits for a kill instruction. */
    base_texture(&draw, &texture);
    texture.has_key = 1ul;
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_COLOR_KEY);
    draw.color_key_enable = 0ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
}

static void test_lit(void)
{
    V9X_R3D_DRAW draw;
    V9X_D3D_NV50_TEXTURE texture;
    struct v9x_nv50_d3d_state state;
    v9x_u32 specular;

    base_draw(&draw);
    draw.specular_enable = 1ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_NONE, 1ul, 0ul) &&
          specular == 1ul);
    draw.specular_enable = 0ul;
    draw.fog_enable = 1ul;
    draw.fog_color = 0x00123456ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.fp_variant ==
              v9x_nv50_d3d_fp_variant(V9X_NV50_D3D_OP_NONE, 0ul, 1ul) &&
          specular == 1ul && state.fog_color == 0x00123456ul);
    base_texture(&draw, &texture);
    draw.specular_enable = 1ul;
    CHECK(map(&draw, &texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.fp_variant == 11ul && state.fp_registers == 16ul);
}

static void test_explicit(void)
{
    V9X_R3D_DRAW draw;
    struct v9x_nv50_d3d_state state;
    v9x_u32 specular;

    base_draw(&draw);
    draw.explicit_state = 1ul;
    draw.scissor_left = 10ul;
    draw.scissor_top = 20ul;
    draw.scissor_right = 300ul;
    draw.scissor_bottom = 200ul;
    draw.write_mask = V9X_D3D_NV50_WRITE_RED | V9X_D3D_NV50_WRITE_BLUE;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.scissor_left == 10ul && state.scissor_top == 20ul &&
          state.scissor_right == 300ul && state.scissor_bottom == 200ul);
    CHECK(state.color_mask == 0x00000101ul);
    draw.scissor_right = 700ul;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_EXPLICIT);
    base_draw(&draw);
    draw.texcoords1 = (const float *)&surface_object;
    CHECK(map(&draw, 0, &state, &specular) == V9X_D3D_NV50_REFUSE_EXPLICIT);
}

/* Two units (step 5 of nvidia-gt218-ps11.md): both units' tables, the
 * combine in nv50_d3d.h's numbers, and the refusals. */
static void test_two_units(void)
{
    static const float uv1[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
    V9X_R3D_DRAW draw;
    V9X_D3D_NV50_TEXTURE texture[2];
    struct v9x_nv50_d3d_state state;
    v9x_u32 expect_tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 expect_tsc[V9X_NV50_3D_TSC_WORDS];
    v9x_u32 specular;
    v9x_u32 index;

    base_draw(&draw);
    base_texture(&draw, &texture[0]);
    texture[1] = texture[0];
    texture[1].format = V9X_NV50_D3D_TEX_B5G6R5;
    texture[1].width = 16ul;
    texture[1].height = 16ul;
    texture[1].pitch = 64ul;
    texture[1].offset = 0x00300000ul;
    draw.texture1 = draw.texture;
    draw.texture1.address = V9X_R3D_ADDRESS_CLAMP;
    draw.texcoords1 = uv1;
    draw.texture.color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    draw.texture.alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
    draw.texture1.color_op = V9X_R3D_COLOROP_ADD;
    draw.texture1.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    draw.specular_enable = 1ul;
    CHECK(map(&draw, texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_NONE);
    CHECK(state.two_units == 1ul && state.textured == 1ul &&
          specular == 1ul && state.fog == 0ul);
    CHECK(state.combine.color[0] == V9X_NV50_D3D_COLOR_MODULATE &&
          state.combine.alpha[0] == V9X_NV50_D3D_ALPHA_REPLACE &&
          state.combine.color[1] == V9X_NV50_D3D_COLOR_ADD &&
          state.combine.alpha[1] == V9X_NV50_D3D_ALPHA_FRAGMENT &&
          state.combine.specular == 1ul && state.combine.fog == 0ul);
    CHECK(v9x_nv50_d3d_texture(V9X_NV50_D3D_TEX_B5G6R5, 0x00300000ul, 64ul,
                               16ul, 16ul, V9X_NV50_D3D_WRAP_CLAMP, 1ul, 1ul,
                               expect_tic, expect_tsc));
    for (index = 0ul; index < V9X_NV50_3D_TIC_WORDS; ++index) {
        CHECK(tic[V9X_NV50_3D_TIC_WORDS + index] == expect_tic[index]);
        CHECK(tsc[V9X_NV50_3D_TSC_WORDS + index] == expect_tsc[index]);
    }

    /* DECALALPHA and MODULATE map too. */
    draw.texture1.color_op = V9X_R3D_ABI_COLOROP_DECALALPHA;
    draw.texture1.alpha_op = V9X_R3D_ABI_ALPHAOP_MODULATE;
    CHECK(map(&draw, texture, &state, &specular) ==
              V9X_D3D_NV50_REFUSE_NONE &&
          state.combine.color[1] == V9X_NV50_D3D_COLOR_DECAL &&
          state.combine.alpha[1] == V9X_NV50_D3D_ALPHA_MODULATE);

    /* MODULATE2X is not claimed; BLEND has no program. */
    draw.texture1.color_op = V9X_R3D_COLOROP_MODULATE2X;
    CHECK(map(&draw, texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_OP);
    draw.texture1.color_op = V9X_R3D_ABI_COLOROP_BLEND;
    CHECK(map(&draw, texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_OP);
    draw.texture1.color_op = V9X_R3D_ABI_COLOROP_MODULATE;

    /* A colour key, a second texture of no known format, and a second
     * unit with no texture are refused. */
    draw.color_key_enable = 1ul;
    CHECK(map(&draw, texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_COLOR_KEY);
    draw.color_key_enable = 0ul;
    texture[1].format = V9X_D3D_NV50_TEXTURE_UNKNOWN;
    CHECK(map(&draw, texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_TEXTURE_FORMAT);
    texture[1].format = V9X_NV50_D3D_TEX_B5G6R5;
    draw.texture1.object = 0;
    CHECK(map(&draw, texture, &state, &specular) ==
          V9X_D3D_NV50_REFUSE_EXPLICIT);
}

unsigned int v9x_run_d3d_nv50_map_tests(void)
{
    failures = 0u;
    test_target();
    test_depth();
    test_tiled();
    test_blend_and_alpha();
    test_texture();
    test_lit();
    test_explicit();
    test_two_units();
    return failures;
}
