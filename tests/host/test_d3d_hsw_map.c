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

unsigned int v9x_run_d3d_hsw_map_tests(void)
{
    failures = 0u;
    test_accepted();
    test_refused();
    test_write_disable();
    test_scissor();
    return failures;
}
