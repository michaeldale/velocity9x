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

    /* A mask other than RGB, including none. */
    base_draw(&draw);
    draw.write_mask = 3ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == V9X_D3D_HSW_REFUSE_MASK);
    draw.write_mask = 0ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == V9X_D3D_HSW_REFUSE_MASK);

    /* A scissor short of the target on any side. */
    base_draw(&draw);
    draw.scissor_left = 1ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == V9X_D3D_HSW_REFUSE_SCISSOR);
    base_draw(&draw);
    draw.scissor_top = 1ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == V9X_D3D_HSW_REFUSE_SCISSOR);
    base_draw(&draw);
    draw.scissor_right = 639ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == V9X_D3D_HSW_REFUSE_SCISSOR);
    base_draw(&draw);
    draw.scissor_bottom = 479ul;
    CHECK(v9x_d3d_hsw_explicit_refusal(&draw) == V9X_D3D_HSW_REFUSE_SCISSOR);
}

unsigned int v9x_run_d3d_hsw_map_tests(void)
{
    failures = 0u;
    test_accepted();
    test_refused();
    return failures;
}
