/*
 * Tests for the render interface's validators (src\display32\r3d\
 * r3d_validate.c): every refusal the plan's Phase 3 names - malformed
 * descriptors, arithmetic overflow, stale generations, batch bounds - and
 * the acceptance of a well-formed request next to each, so a validator that
 * refused everything would fail as surely as one that refused nothing.
 */
#include <stdio.h>
#include "../../src/display32/r3d/r3d_validate.h"

static unsigned int validate_failures;

#define VCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++validate_failures; \
    } \
} while (0)

#define VRAM 0x00800000ul       /* 8 MiB */
#define DIM_MAX 2048ul

static void surface_reset(V9X_R3D_SURFACE *surface)
{
    surface->offset = 0x00100000ul;
    surface->pitch = 1280ul;
    surface->width = 640ul;
    surface->height = 480ul;
    surface->format = V9X_R3D_FORMAT_RGB565;
    surface->object = 0;
}

static void test_surface_layout_and_extent(void)
{
    V9X_R3D_SURFACE s;

    surface_reset(&s);
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) == V9X_R3D_RESULT_OK);
    VCHECK(v9x_r3d_validate_surface(0, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);

    s.format = V9X_R3D_FORMAT_XRGB1555;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) == V9X_R3D_RESULT_OK);
    s.format = 3ul;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);

    surface_reset(&s);
    s.width = 0ul;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
    surface_reset(&s);
    s.height = DIM_MAX + 1ul;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);

    /* A pitch that does not hold the row, and an odd one. */
    surface_reset(&s);
    s.pitch = 1278ul;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
    s.pitch = 1281ul;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);

    /* The extent exactly at the end of VRAM, then one byte past it: the last
     * row starts at offset + pitch * 479 and holds 1280 bytes. */
    surface_reset(&s);
    s.offset = VRAM - 1280ul * 480ul;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) == V9X_R3D_RESULT_OK);
    s.offset += 2ul;
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);

    /* A pitch padded past the row needs only the last row's pixels, not a
     * whole last pitch. */
    surface_reset(&s);
    s.pitch = 2048ul;
    s.offset = VRAM - (2048ul * 479ul + 1280ul);
    VCHECK(v9x_r3d_validate_surface(&s, VRAM, DIM_MAX) == V9X_R3D_RESULT_OK);

    /* Arithmetic that would wrap in 32 bits: a pitch * rows product past
     * 2^32, and an offset whose sum wraps. Both must be refused, not wrap
     * round to something that fits. */
    surface_reset(&s);
    s.pitch = 0x00400000ul;   /* x 2047 rows = 2^33 - 2^22 */
    s.height = 2048ul;
    s.offset = 0ul;
    VCHECK(v9x_r3d_validate_surface(&s, 0xfffffffful, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
    surface_reset(&s);
    s.offset = 0xfffff000ul;
    VCHECK(v9x_r3d_validate_surface(&s, 0xfffffffful, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
}

static void test_depth_against_target(void)
{
    V9X_R3D_SURFACE target;
    V9X_R3D_SURFACE depth;

    surface_reset(&target);
    surface_reset(&depth);
    depth.offset = 0x00300000ul;
    depth.format = 0ul;         /* a depth surface carries no colour format */
    VCHECK(v9x_r3d_validate_depth(&depth, &target, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_OK);
    depth.width = 639ul;
    VCHECK(v9x_r3d_validate_depth(&depth, &target, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
    depth.width = 1024ul;
    depth.pitch = 2048ul;
    VCHECK(v9x_r3d_validate_depth(&depth, &target, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_OK);
    depth.height = 479ul;
    VCHECK(v9x_r3d_validate_depth(&depth, &target, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
    depth.height = 480ul;
    depth.offset = VRAM - 1000ul;
    VCHECK(v9x_r3d_validate_depth(&depth, &target, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
    VCHECK(v9x_r3d_validate_depth(0, &target, VRAM, DIM_MAX) ==
           V9X_R3D_RESULT_INVALID);
}

static v9x_u16 level_cells[64 * 16 + 32 * 8 + 16 * 4 + 8 * 2 + 4 + 2 + 1];

static void levels_reset(V9X_R3D_ABI_LEVEL *levels)
{
    unsigned int level;
    v9x_u32 width = 64ul;
    v9x_u32 height = 16ul;
    v9x_u32 offset = 0ul;

    for (level = 0u; level < 7u; ++level) {
        levels[level].pixels = &level_cells[offset];
        levels[level].pitch = width * 2ul;
        levels[level].width = width;
        levels[level].height = height;
        levels[level].bytes = width * height * 2ul;
        offset += width * height;
        width = width > 1ul ? width / 2ul : 1ul;
        height = height > 1ul ? height / 2ul : 1ul;
    }
}

static void test_cpu_levels(void)
{
    V9X_R3D_ABI_LEVEL levels[7];

    levels_reset(levels);
    VCHECK(v9x_r3d_validate_levels(levels, 7ul, 512ul) == V9X_R3D_RESULT_OK);
    VCHECK(v9x_r3d_validate_levels(levels, 1ul, 512ul) == V9X_R3D_RESULT_OK);
    VCHECK(v9x_r3d_validate_levels(levels, 0ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);
    VCHECK(v9x_r3d_validate_levels(0, 1ul, 512ul) == V9X_R3D_RESULT_INVALID);
    VCHECK(v9x_r3d_validate_levels(levels, 8ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);   /* past the 1x1 level */
    VCHECK(v9x_r3d_validate_levels(levels, V9X_R3D_ABI_LEVELS_MAX + 1ul,
                                   512ul) == V9X_R3D_RESULT_INVALID);
    VCHECK(v9x_r3d_validate_levels(levels, 7ul, 32ul) ==
           V9X_R3D_RESULT_INVALID);   /* level 0 wider than the limit */

    /* Declared storage one byte short of the level. */
    levels_reset(levels);
    levels[2].bytes -= 1ul;
    VCHECK(v9x_r3d_validate_levels(levels, 7ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);
    /* A padded pitch needs pitch * (h - 1) + w * 2. */
    levels_reset(levels);
    levels[0].pitch = 256ul;
    levels[0].bytes = 256ul * 15ul + 128ul;
    VCHECK(v9x_r3d_validate_levels(levels, 7ul, 512ul) == V9X_R3D_RESULT_OK);
    levels[0].pitch = 126ul;
    VCHECK(v9x_r3d_validate_levels(levels, 7ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);
    /* A skipped level, a non-power-of-two top, a null level. */
    levels_reset(levels);
    levels[3].width = 4ul;
    VCHECK(v9x_r3d_validate_levels(levels, 7ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);
    levels_reset(levels);
    levels[0].width = 48ul;
    VCHECK(v9x_r3d_validate_levels(levels, 1ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);
    levels_reset(levels);
    levels[5].pixels = 0;
    VCHECK(v9x_r3d_validate_levels(levels, 7ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);
    /* A pitch * rows product that would wrap. */
    levels_reset(levels);
    levels[0].pitch = 0x20000000ul;   /* x 15 rows is past 2^32 */
    levels[0].bytes = 0xfffffffful;
    VCHECK(v9x_r3d_validate_levels(levels, 1ul, 512ul) ==
           V9X_R3D_RESULT_INVALID);
}

static void texture_reset(V9X_R3D_ABI_TEXTURE *texture,
                          const V9X_R3D_ABI_LEVEL *levels)
{
    texture->storage = V9X_R3D_ABI_TEXTURE_CPU;
    texture->format = V9X_R3D_ABI_FORMAT_ARGB4444;
    texture->surface.lcl = 0;
    texture->levels = levels;
    texture->level_count = 7ul;
    texture->min_filter = V9X_R3D_ABI_FILTER_LINEAR;
    texture->mag_filter = V9X_R3D_ABI_FILTER_LINEAR;
    texture->mip = V9X_R3D_ABI_MIP_POINT;
    texture->address = V9X_R3D_ABI_ADDRESS_WRAP;
    texture->color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    texture->alpha_op = V9X_R3D_ABI_ALPHAOP_MODULATE;
    texture->env_color = 0ul;
}

static void test_texture_description(void)
{
    V9X_R3D_ABI_LEVEL levels[7];
    V9X_R3D_ABI_TEXTURE t;
    int hw_surface;

    levels_reset(levels);
    texture_reset(&t, levels);
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_OK);
    VCHECK(v9x_r3d_validate_texture(0, 512ul) == V9X_R3D_RESULT_INVALID);

    t.storage = V9X_R3D_ABI_TEXTURE_NONE;
    t.levels = 0;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_OK);

    texture_reset(&t, levels);
    t.storage = 3ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    texture_reset(&t, levels);
    t.format = V9X_R3D_ABI_FORMAT_XRGB1555;     /* a target layout */
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    texture_reset(&t, levels);
    t.min_filter = 3ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    texture_reset(&t, levels);
    t.mip = 0ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    texture_reset(&t, levels);
    t.address = 2ul;                            /* MIRROR: not in v1 */
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    texture_reset(&t, levels);
    t.color_op = 5ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    texture_reset(&t, levels);
    t.alpha_op = 3ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    texture_reset(&t, levels);
    t.env_color = 0x01000000ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    /* A mip filter with a one-level chain is an incomplete texture. */
    texture_reset(&t, levels);
    t.level_count = 1ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    t.mip = V9X_R3D_ABI_MIP_NONE;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_OK);
    /* The chain itself goes through validate_levels. */
    texture_reset(&t, levels);
    levels[1].bytes = 0ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);

    /* HW storage needs a surface and no CPU chain. */
    levels_reset(levels);
    texture_reset(&t, levels);
    t.storage = V9X_R3D_ABI_TEXTURE_HW;
    t.levels = 0;
    t.level_count = 0ul;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_INVALID);
    t.surface.lcl = &hw_surface;
    VCHECK(v9x_r3d_validate_texture(&t, 512ul) == V9X_R3D_RESULT_OK);
}

static void state_reset(V9X_R3D_ABI_STATE *s)
{
    s->depth_enable = 1ul;
    s->depth_write = 1ul;
    s->depth_func = V9X_R3D_CMP_LESSEQUAL;
    s->blend_enable = 1ul;
    s->src_blend = V9X_R3D_BLEND_SRCALPHA;
    s->dst_blend = V9X_R3D_BLEND_INVSRCALPHA;
    s->alpha_test_enable = 1ul;
    s->alpha_func = V9X_R3D_CMP_GREATER;
    s->alpha_ref = 170ul;
    s->fog_enable = 0ul;
    s->fog_color = 0ul;
    s->write_mask = V9X_R3D_ABI_WRITE_RGB;
    s->scissor_left = 0ul;
    s->scissor_top = 0ul;
    s->scissor_right = 640ul;
    s->scissor_bottom = 480ul;
}

static void test_state_ranges(void)
{
    V9X_R3D_ABI_STATE s;

    state_reset(&s);
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_OK);
    VCHECK(v9x_r3d_validate_state(0, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    s.depth_func = 9ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.src_blend = 12ul;         /* D3D's BOTHSRCALPHA shorthand */
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.dst_blend = 0ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.alpha_func = 0ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.alpha_ref = 256ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.fog_color = 0x01000000ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.write_mask = 8ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.write_mask = 0ul;         /* every channel masked is legal */
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_OK);

    /* Functions and factors are read only when their enable is set, as a
     * GL front end leaves the defaults in place under a disabled test. */
    state_reset(&s);
    s.blend_enable = 0ul;
    s.src_blend = 0ul;
    s.alpha_test_enable = 0ul;
    s.alpha_func = 0ul;
    s.depth_enable = 0ul;
    s.depth_func = 0ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_OK);

    /* The scissor: empty is fine, inverted or outside is not. */
    state_reset(&s);
    s.scissor_left = 100ul;
    s.scissor_right = 100ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_OK);
    s.scissor_right = 99ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
    state_reset(&s);
    s.scissor_bottom = 481ul;
    VCHECK(v9x_r3d_validate_state(&s, 640ul, 480ul) == V9X_R3D_RESULT_INVALID);
}

static V9X_R3D_ABI_VERTEX draw_vertices[3u * 65u];

static void draw_reset(V9X_R3D_ABI_DRAW *d, int *target_lcl)
{
    V9X_R3D_ABI_STATE state;

    d->struct_bytes = sizeof(*d);
    d->generation = 7ul;
    d->target.lcl = target_lcl;
    d->depth.lcl = 0;
    d->texture.storage = V9X_R3D_ABI_TEXTURE_NONE;
    d->texture.format = 0ul;
    d->texture.surface.lcl = 0;
    d->texture.levels = 0;
    d->texture.level_count = 0ul;
    d->texture.min_filter = V9X_R3D_ABI_FILTER_NEAREST;
    d->texture.mag_filter = V9X_R3D_ABI_FILTER_NEAREST;
    d->texture.mip = V9X_R3D_ABI_MIP_NONE;
    d->texture.address = V9X_R3D_ABI_ADDRESS_WRAP;
    d->texture.color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    d->texture.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    d->texture.env_color = 0ul;
    state_reset(&state);
    d->state = state;
    d->vertices = draw_vertices;
    d->triangle_count = 1ul;
}

static void test_draw_header(void)
{
    V9X_R3D_ABI_DRAW d;
    int target;

    draw_reset(&d, &target);
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_OK);
    VCHECK(v9x_r3d_validate_draw(0, 7ul, 512ul) == V9X_R3D_RESULT_INVALID);

    /* The size first: a caller built against another layout is ABI, even
     * when everything after it would also be wrong. */
    d.struct_bytes = sizeof(d) - 4ul;
    d.generation = 6ul;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_ABI);
    d.struct_bytes = sizeof(d) + 4ul;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_ABI);

    /* Then the generation, before the payload. */
    draw_reset(&d, &target);
    d.generation = 6ul;
    d.target.lcl = 0;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_STALE);

    draw_reset(&d, &target);
    d.target.lcl = 0;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_INVALID);
    draw_reset(&d, &target);
    d.vertices = 0;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_INVALID);

    /* The batch bounds, both sides: an oversized batch is refused, never
     * truncated. */
    draw_reset(&d, &target);
    d.triangle_count = 0ul;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_INVALID);
    d.triangle_count = V9X_R3D_ABI_BATCH_MAX;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_OK);
    d.triangle_count = V9X_R3D_ABI_BATCH_MAX + 1ul;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_INVALID);

    draw_reset(&d, &target);
    d.texture.storage = 9ul;
    VCHECK(v9x_r3d_validate_draw(&d, 7ul, 512ul) == V9X_R3D_RESULT_INVALID);
}

static void test_clear_header_and_rects(void)
{
    V9X_R3D_ABI_CLEAR c;
    V9X_R3D_ABI_RECT rects[2];
    int target;

    c.struct_bytes = sizeof(c);
    c.generation = 3ul;
    c.target.lcl = &target;
    c.depth.lcl = 0;
    c.clear_color = 1ul;
    c.clear_depth = 0ul;
    c.color_value = 0x00102030ul;
    c.depth_value = 0ul;
    c.write_mask = V9X_R3D_ABI_WRITE_RGB;
    c.write_depth = 0ul;
    c.rects = rects;
    c.rect_count = 2ul;
    VCHECK(v9x_r3d_validate_clear(&c, 3ul) == V9X_R3D_RESULT_OK);
    VCHECK(v9x_r3d_validate_clear(0, 3ul) == V9X_R3D_RESULT_INVALID);
    c.struct_bytes = 4ul;
    VCHECK(v9x_r3d_validate_clear(&c, 3ul) == V9X_R3D_RESULT_ABI);
    c.struct_bytes = sizeof(c);
    VCHECK(v9x_r3d_validate_clear(&c, 4ul) == V9X_R3D_RESULT_STALE);
    c.clear_depth = 1ul;        /* depth asked for, no depth surface */
    VCHECK(v9x_r3d_validate_clear(&c, 3ul) == V9X_R3D_RESULT_INVALID);
    c.clear_depth = 0ul;
    c.rects = 0;
    VCHECK(v9x_r3d_validate_clear(&c, 3ul) == V9X_R3D_RESULT_INVALID);
    c.rect_count = 0ul;         /* an empty list needs no array */
    VCHECK(v9x_r3d_validate_clear(&c, 3ul) == V9X_R3D_RESULT_OK);
    c.rect_count = 2ul;
    c.rects = rects;
    c.color_value = 0x01000000ul;
    VCHECK(v9x_r3d_validate_clear(&c, 3ul) == V9X_R3D_RESULT_INVALID);

    rects[0].left = 0ul;
    rects[0].top = 0ul;
    rects[0].right = 640ul;
    rects[0].bottom = 480ul;
    rects[1].left = 10ul;
    rects[1].top = 10ul;
    rects[1].right = 10ul;
    rects[1].bottom = 20ul;
    VCHECK(v9x_r3d_validate_rects(rects, 2ul, 640ul, 480ul) ==
           V9X_R3D_RESULT_OK);
    rects[1].right = 9ul;
    VCHECK(v9x_r3d_validate_rects(rects, 2ul, 640ul, 480ul) ==
           V9X_R3D_RESULT_INVALID);
    rects[1].right = 10ul;
    rects[0].bottom = 481ul;
    VCHECK(v9x_r3d_validate_rects(rects, 2ul, 640ul, 480ul) ==
           V9X_R3D_RESULT_INVALID);
    VCHECK(v9x_r3d_validate_rects(0, 1ul, 640ul, 480ul) ==
           V9X_R3D_RESULT_INVALID);
    VCHECK(v9x_r3d_validate_rects(0, 0ul, 640ul, 480ul) ==
           V9X_R3D_RESULT_OK);
}

unsigned int v9x_run_r3d_validate_tests(void)
{
    validate_failures = 0u;
    test_surface_layout_and_extent();
    test_depth_against_target();
    test_cpu_levels();
    test_texture_description();
    test_state_ranges();
    test_draw_header();
    test_clear_header_and_rects();
    if (validate_failures == 0u) {
        printf("PASS: render interface validators\n");
    }
    return validate_failures;
}
