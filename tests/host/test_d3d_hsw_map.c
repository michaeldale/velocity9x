#include <stdio.h>
#include <string.h>

#include "../../src/display32/d3d/d3d_hsw_map.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static int surface_object;
static V9X_R3D_LEVEL level;

/* An explicit 640x480 draw with a full-target scissor and an RGB mask, as
 * the render interface sends one (d3d_core.c, v9x_r3d_describe). */
static void base_draw(V9X_R3D_DRAW *draw)
{
    memset(draw, 0, sizeof(*draw));
    draw->target.width = 640ul;
    draw->target.height = 480ul;
    draw->explicit_state = 1ul;
    draw->scissor_right = 640ul;
    draw->scissor_bottom = 480ul;
    draw->write_mask = V9X_D3D_HSW_WRITE_RGB;
}

static void test_accepted(void)
{
    V9X_R3D_DRAW draw;

    base_draw(&draw);
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == 0ul);
    /* A surface texture is the engine's to sample. */
    draw.texture.object = &surface_object;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == 0ul);
    /* A scissor past the target still covers it. */
    draw.scissor_right = 2048ul;
    draw.scissor_bottom = 2048ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == 0ul);
    /* Direct3D's draws state none of it. */
    memset(&draw, 0, sizeof(draw));
    draw.target.width = 640ul;
    draw.target.height = 480ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == 0ul);
}

static void test_refused(void)
{
    V9X_R3D_DRAW draw;

    /* CPU levels: the ICD's and Glide's default, which this engine drew
     * untextured before (plan step 1). */
    base_draw(&draw);
    draw.texture.levels = &level;
    draw.texture.level_count = 1ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) ==
          V9X_D3D_HSW_REFUSE_CPU_TEXTURE);
    base_draw(&draw);
    draw.texture1.levels = &level;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) ==
          V9X_D3D_HSW_REFUSE_CPU_TEXTURE);

    /* Step 2: a partial mask and a partial scissor are drawn, not refused
     * (v9x_d3d_hsw_write_disable, v9x_d3d_hsw_scissor below). */
    base_draw(&draw);
    draw.write_mask = 3ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == 0ul);
    base_draw(&draw);
    draw.scissor_left = 1ul;
    draw.scissor_bottom = 479ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == 0ul);
}

/* BLEND_STATE's write disables, 27:24 = A R G B (Vol 2d), from the render
 * interface's mask, R 1, G 2, B 4. Alpha is never masked: no ABI bit. */
static void test_write_disable(void)
{
    CHECK(v9x_d3d_hsw_write_disable(7ul) == 0u);
    CHECK(v9x_d3d_hsw_write_disable(0ul) == 7u);
    CHECK(v9x_d3d_hsw_write_disable(1ul) == 3u);  /* R only: G, B off */
    CHECK(v9x_d3d_hsw_write_disable(2ul) == 5u);  /* G only */
    CHECK(v9x_d3d_hsw_write_disable(4ul) == 6u);  /* B only */
    CHECK(v9x_d3d_hsw_write_disable(3ul) == 1u);  /* R G: B off */
}

/* The scissor as an inclusive drawing rectangle within the target: the
 * render interface's right and bottom are exclusive. Non-explicit draws
 * and covering scissors give the whole target; an empty one draws
 * nothing. */
static void test_scissor(void)
{
    V9X_R3D_DRAW draw;
    v9x_u32 rect[4];

    base_draw(&draw);
    CHECK(v9x_d3d_hsw_scissor(&draw, rect) == 1);
    CHECK(rect[0] == 0ul && rect[1] == 0ul && rect[2] == 639ul &&
          rect[3] == 479ul);
    draw.scissor_left = 16ul;
    draw.scissor_top = 8ul;
    draw.scissor_right = 320ul;
    draw.scissor_bottom = 240ul;
    CHECK(v9x_d3d_hsw_scissor(&draw, rect) == 1);
    CHECK(rect[0] == 16ul && rect[1] == 8ul && rect[2] == 319ul &&
          rect[3] == 239ul);
    /* Past the target: clamped. */
    draw.scissor_right = 2048ul;
    draw.scissor_bottom = 2048ul;
    CHECK(v9x_d3d_hsw_scissor(&draw, rect) == 1);
    CHECK(rect[2] == 639ul && rect[3] == 479ul);
    /* Empty or inverted, or wholly outside: nothing to draw. */
    draw.scissor_right = 16ul;
    CHECK(v9x_d3d_hsw_scissor(&draw, rect) == 0);
    base_draw(&draw);
    draw.scissor_top = 480ul;
    draw.scissor_bottom = 600ul;
    CHECK(v9x_d3d_hsw_scissor(&draw, rect) == 0);
    /* Direct3D's draw states no scissor: the whole target. */
    memset(&draw, 0, sizeof(draw));
    draw.target.width = 640ul;
    draw.target.height = 480ul;
    CHECK(v9x_d3d_hsw_scissor(&draw, rect) == 1);
    CHECK(rect[0] == 0ul && rect[2] == 639ul && rect[3] == 479ul);
}

/*
 * Two units, step 4. Table 3.18 per unit, Cf the incoming colour (unit 1's
 * is unit 0's result), against the generator's stages: TEXTURE (Ct, At),
 * DECAL (Ct, Af), MODULATE (Cf Ct, Af At), MODULATE_TEXALPHA (Cf Ct, At);
 * stage 1 MODULATE_TEX1 (C0 Ct1, A0 At1).
 */
static void test_two_unit_stages(void)
{
    v9x_u8 s0 = 0xffu;
    v9x_u8 s1 = 0xffu;

    /* Half-Life's lightmap pass: base REPLACE, lightmap MODULATE. */
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_REPLACE, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              &s0, &s1) == 0ul);
    CHECK(s0 == V9X_HSW_PS_TEXTURE && s1 == V9X_HSW_PS_MODULATE_TEX1);
    /* REPLACE keeping the fragment's alpha is DECAL. */
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_FRAGMENT, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              &s0, &s1) == 0ul);
    CHECK(s0 == V9X_HSW_PS_DECAL);
    /* MODULATE with modulated alpha, and with the texture's alpha. */
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              &s0, &s1) == 0ul);
    CHECK(s0 == V9X_HSW_PS_MODULATE);
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_REPLACE, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              &s0, &s1) == 0ul);
    CHECK(s0 == V9X_HSW_PS_MODULATE_TEXALPHA);
    /* Without texture alpha (565), At is 1: MODULATE keeping the fragment's
     * alpha is MODULATE, REPLACE with modulated alpha is DECAL, and unit 1
     * keeping the fragment's alpha is MODULATE_TEX1. */
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_FRAGMENT, 0,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_FRAGMENT, 0,
              &s0, &s1) == 0ul);
    CHECK(s0 == V9X_HSW_PS_MODULATE && s1 == V9X_HSW_PS_MODULATE_TEX1);
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_MODULATE, 0,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 0,
              &s0, &s1) == 0ul);
    CHECK(s0 == V9X_HSW_PS_DECAL);

    /* With texture alpha those three are not exact: refused. */
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_FRAGMENT, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 0,
              &s0, &s1) == V9X_D3D_HSW_REFUSE_COMBINE);
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 0,
              &s0, &s1) == V9X_D3D_HSW_REFUSE_COMBINE);
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_REPLACE, 0,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_FRAGMENT, 1,
              &s0, &s1) == V9X_D3D_HSW_REFUSE_COMBINE);
    /* No stage for DECALALPHA or BLEND on either unit, nor for unit 1
     * replacing or keeping unit 0's colour. */
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_DECALALPHA, V9X_R3D_ABI_ALPHAOP_FRAGMENT, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              &s0, &s1) == V9X_D3D_HSW_REFUSE_COMBINE);
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_REPLACE, 1,
              V9X_R3D_ABI_COLOROP_BLEND, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              &s0, &s1) == V9X_D3D_HSW_REFUSE_COMBINE);
    CHECK(v9x_d3d_hsw_two_unit_stages(
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_REPLACE, 1,
              V9X_R3D_ABI_COLOROP_REPLACE, V9X_R3D_ABI_ALPHAOP_REPLACE, 1,
              &s0, &s1) == V9X_D3D_HSW_REFUSE_COMBINE);
    CHECK(v9x_d3d_hsw_two_unit_stages(
              0ul, V9X_R3D_ABI_ALPHAOP_REPLACE, 1,
              V9X_R3D_ABI_COLOROP_MODULATE, V9X_R3D_ABI_ALPHAOP_MODULATE, 1,
              &s0, &s1) == V9X_D3D_HSW_REFUSE_COMBINE);
}

/* The target's SURFACE_STATE format, and the write disables it needs: an
 * XRGB8888 target renders as B8G8R8A8 with alpha writes off, so the X byte
 * the clear wrote stays (docs\plans\32bpp-acceleration-and-3d.md). */
static void test_target(void)
{
    v9x_u16 format = 0u;
    v9x_u8 disable = 0xffu;

    CHECK(v9x_d3d_hsw_target(V9X_R3D_FORMAT_RGB565, &format, &disable));
    CHECK(format == V9X_HSW_FMT_B5G6R5_UNORM && disable == 0u);
    CHECK(v9x_d3d_hsw_target(V9X_R3D_FORMAT_XRGB1555, &format, &disable));
    CHECK(format == V9X_HSW_FMT_B5G5R5A1_UNORM && disable == 0u);
    CHECK(v9x_d3d_hsw_target(V9X_R3D_FORMAT_XRGB8888, &format, &disable));
    CHECK(format == V9X_HSW_FMT_B8G8R8A8_UNORM && disable == 8u);
    format = 0x1234u;
    CHECK(!v9x_d3d_hsw_target(0ul, &format, &disable));
    CHECK(!v9x_d3d_hsw_target(3ul, &format, &disable));
    CHECK(format == 0x1234u);
}

/* The depth buffer's 3DSTATE_DEPTH_BUFFER format for its cell, and a
 * depth fill's value as the z the depth-only draw writes: D16 is
 * round(z x 65535), D24_UNORM_X8 round(z x 16777215) in bits 23:0 (Vol 2b
 * 3DSTATE_CLEAR_PARAMS). */
static void test_depth(void)
{
    v9x_u8 format = 0xffu;

    CHECK(v9x_d3d_hsw_depth_format(2ul, &format));
    CHECK(format == V9X_HSW_DEPTH_D16_UNORM);
    CHECK(v9x_d3d_hsw_depth_format(4ul, &format));
    CHECK(format == V9X_HSW_DEPTH_D24_UNORM_X8);
    format = 0x55u;
    CHECK(!v9x_d3d_hsw_depth_format(3ul, &format));
    CHECK(!v9x_d3d_hsw_depth_format(0ul, &format));
    CHECK(format == 0x55u);

    CHECK(v9x_d3d_hsw_fill_z(0xfffful, 2ul) == 1.0f);
    CHECK(v9x_d3d_hsw_fill_z(0ul, 2ul) == 0.0f);
    CHECK(v9x_d3d_hsw_fill_z(0x00fffffful, 4ul) == 1.0f);
    CHECK(v9x_d3d_hsw_fill_z(0ul, 4ul) == 0.0f);
    /* The cell's own bits only: above them is not depth. */
    CHECK(v9x_d3d_hsw_fill_z(0xfffffffful, 4ul) == 1.0f);
    CHECK(v9x_d3d_hsw_fill_z(0x0001fffful, 2ul) == 1.0f);
    /* A 24-bit midpoint is not rounded to a 16-bit one, which would be 128
     * steps out. Within one step is all a float z can carry here: at 0.5 its
     * own step is 6e-8, one 24-bit step, and the vertex's z is a float. */
    CHECK((double)v9x_d3d_hsw_fill_z(0x00800000ul, 4ul) * 16777215.0 >
          8388606.5);
    CHECK((double)v9x_d3d_hsw_fill_z(0x00800000ul, 4ul) * 16777215.0 <
          8388609.5);
}

/* A texture's layout as the sampler's format, its alpha and its texel
 * size: the three 16-bit layouts as before, and the two 32-bit ones. */
static void test_texel_format(void)
{
    v9x_u16 format = 0u;
    int alpha = -1;
    v9x_u32 bytes = 0ul;

    CHECK(v9x_d3d_hsw_texel_format(16ul, 0, 0xf800ul, 0x07e0ul, 0x001ful,
                                   0ul, &format, &alpha, &bytes));
    CHECK(format == V9X_HSW_FMT_B5G6R5_UNORM && alpha == 0 && bytes == 2ul);
    CHECK(v9x_d3d_hsw_texel_format(16ul, 1, 0x7c00ul, 0x03e0ul, 0x001ful,
                                   0x8000ul, &format, &alpha, &bytes));
    CHECK(format == V9X_HSW_FMT_B5G5R5A1_UNORM && alpha == 1 &&
          bytes == 2ul);
    CHECK(v9x_d3d_hsw_texel_format(16ul, 1, 0x0f00ul, 0x00f0ul, 0x000ful,
                                   0xf000ul, &format, &alpha, &bytes));
    CHECK(format == V9X_HSW_FMT_B4G4R4A4_UNORM && alpha == 1 &&
          bytes == 2ul);
    CHECK(v9x_d3d_hsw_texel_format(32ul, 1, 0x00ff0000ul, 0x0000ff00ul,
                                   0x000000fful, 0xff000000ul, &format,
                                   &alpha, &bytes));
    CHECK(format == V9X_HSW_FMT_B8G8R8A8_UNORM && alpha == 1 &&
          bytes == 4ul);
    CHECK(v9x_d3d_hsw_texel_format(32ul, 0, 0x00ff0000ul, 0x0000ff00ul,
                                   0x000000fful, 0ul, &format, &alpha,
                                   &bytes));
    CHECK(format == V9X_HSW_FMT_B8G8R8X8_UNORM && alpha == 0 &&
          bytes == 4ul);

    /* An alpha mask without the alpha flag is not alpha, as the 565 path
     * always read it: XRGB8888. */
    CHECK(v9x_d3d_hsw_texel_format(32ul, 0, 0x00ff0000ul, 0x0000ff00ul,
                                   0x000000fful, 0xff000000ul, &format,
                                   &alpha, &bytes));
    CHECK(format == V9X_HSW_FMT_B8G8R8X8_UNORM && alpha == 0);

    /* Refused, and nothing written: 1555 with no alpha flag (XRGB1555 is
     * not offered), ABGR order, 24-bit, and a 565 claiming alpha. */
    format = 0x1234u;
    alpha = -1;
    bytes = 7ul;
    CHECK(!v9x_d3d_hsw_texel_format(16ul, 0, 0x7c00ul, 0x03e0ul, 0x001ful,
                                    0ul, &format, &alpha, &bytes));
    CHECK(!v9x_d3d_hsw_texel_format(32ul, 1, 0x000000fful, 0x0000ff00ul,
                                    0x00ff0000ul, 0xff000000ul, &format,
                                    &alpha, &bytes));
    CHECK(!v9x_d3d_hsw_texel_format(24ul, 0, 0x00ff0000ul, 0x0000ff00ul,
                                    0x000000fful, 0ul, &format, &alpha,
                                    &bytes));
    CHECK(!v9x_d3d_hsw_texel_format(16ul, 1, 0xf800ul, 0x07e0ul, 0x001ful,
                                    0ul, &format, &alpha, &bytes));
    CHECK(format == 0x1234u && alpha == -1 && bytes == 7ul);
}

unsigned int v9x_run_d3d_hsw_map_tests(void)
{
    failures = 0u;
    test_texel_format();
    test_target();
    test_depth();
    test_accepted();
    test_refused();
    test_write_disable();
    test_scissor();
    test_two_unit_stages();
    return failures;
}
