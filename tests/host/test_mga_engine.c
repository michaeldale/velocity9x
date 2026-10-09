#include <stdio.h>
#include <string.h>

#include "velocity9x/mga_engine.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The value a builder wrote to one register, or 0xdeadbeef if none. */
static v9x_u32 reg_value(const struct v9x_mga_writes *writes, v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0u; index < writes->count; ++index) {
        if (writes->offsets[index] == offset) {
            return writes->values[index];
        }
    }
    return 0xdeadbeeful;
}

/* Exactly one write goes to the +100h mirror, and it is the last. */
static int go_is_last(const struct v9x_mga_writes *writes, v9x_u32 offset)
{
    v9x_u32 index;

    if (writes->count == 0u ||
        writes->offsets[writes->count - 1u] != offset + V9X_MGA_GO) {
        return 0;
    }
    for (index = 0u; index + 1u < writes->count; ++index) {
        if (writes->offsets[index] >= V9X_MGA_DWGCTL + V9X_MGA_GO &&
            writes->offsets[index] < V9X_MGA_DWGCTL + 2u * V9X_MGA_GO) {
            return 0;
        }
    }
    return 1;
}

/* 800x600x16 as the 2064W's BIOS sets it: 1920 bytes per line, 8 MiB. */
static void base_fill(struct v9x_mga_fill *fill)
{
    memset(fill, 0, sizeof(*fill));
    fill->vram_bytes = 8388608ul;
    fill->target_offset = 0ul;
    fill->pitch_bytes = 1920ul;
    fill->bytes_per_pixel = 2ul;
    fill->left = 4ul;
    fill->top = 458ul;
    fill->width = 50ul;
    fill->height = 18ul;
    fill->color = 0x1234ul;
}

static void base_copy(struct v9x_mga_copy *copy)
{
    memset(copy, 0, sizeof(*copy));
    copy->vram_bytes = 8388608ul;
    copy->source_pitch_bytes = 1920ul;
    copy->destination_pitch_bytes = 1920ul;
    copy->bytes_per_pixel = 2ul;
    copy->source_left = 10ul;
    copy->source_top = 20ul;
    copy->destination_left = 10ul;
    copy->destination_top = 20ul;
    copy->width = 30ul;
    copy->height = 5ul;
}

static void test_setup(void)
{
    struct v9x_mga_writes writes;

    CHECK(v9x_mga_build_setup(2ul, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_MACCESS) == 1ul);
    CHECK(reg_value(&writes, V9X_MGA_PLNWT) == 0xfffffffful);
    CHECK(reg_value(&writes, V9X_MGA_CXBNDRY) == 0x07ff0000ul);
    CHECK(reg_value(&writes, V9X_MGA_YTOP) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_YBOT) == 0x007fffe0ul);
    CHECK((reg_value(&writes, V9X_MGA_YBOT) & 31ul) == 0ul);
    /* Setup starts nothing. */
    CHECK(writes.offsets[writes.count - 1u] < V9X_MGA_DWGCTL + V9X_MGA_GO);

    CHECK(v9x_mga_build_setup(1ul, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_MACCESS) == 0ul);
    CHECK(v9x_mga_build_setup(4ul, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_MACCESS) == 2ul);
    CHECK(v9x_mga_build_setup(3ul, &writes) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_mga_build_setup(2ul, 0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_fill_16bpp(void)
{
    struct v9x_mga_fill fill;
    struct v9x_mga_writes writes;

    base_fill(&fill);
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x000c7804ul);
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 960ul);
    CHECK(reg_value(&writes, V9X_MGA_YDSTORG) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0x12341234ul);
    /* The right edge is exclusive for a trapezoid. */
    CHECK(reg_value(&writes, V9X_MGA_FXBNDRY) == ((54ul << 16) | 4ul));
    CHECK(reg_value(&writes, V9X_MGA_YDSTLEN + V9X_MGA_GO) ==
          ((458ul << 16) | 18ul));
    CHECK(go_is_last(&writes, V9X_MGA_YDSTLEN));
    CHECK(writes.count <= V9X_MGA_MAX_WRITES);
}

static void test_fill_8bpp_and_32bpp(void)
{
    struct v9x_mga_fill fill;
    struct v9x_mga_writes writes;

    base_fill(&fill);
    fill.bytes_per_pixel = 1ul;
    fill.pitch_bytes = 640ul;
    fill.color = 0x107ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 640ul);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0x07070707ul);

    base_fill(&fill);
    fill.bytes_per_pixel = 4ul;
    fill.pitch_bytes = 3200ul;
    fill.color = 0x00a0b0c0ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 800ul);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0x00a0b0c0ul);
}

/* A back buffer after an 800x600x16 primary: 1152000 bytes in, 576000
 * pixels, a multiple of 32. */
static void test_fill_offscreen_origin(void)
{
    struct v9x_mga_fill fill;
    struct v9x_mga_writes writes;

    base_fill(&fill);
    fill.target_offset = 1152000ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_YDSTORG) == 576000ul);
    CHECK(reg_value(&writes, V9X_MGA_YDSTLEN + V9X_MGA_GO) ==
          ((458ul << 16) | 18ul));
}

static void test_fill_refusals(void)
{
    struct v9x_mga_fill fill;
    struct v9x_mga_writes writes;

    /* Pitches the linearizer has no entry for, or that the Millennium
     * list leaves out. */
    base_fill(&fill);
    fill.pitch_bytes = 200ul;
    fill.left = 0ul;
    fill.width = 50ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_UNSUPPORTED);
    CHECK(writes.count == 0ul);
    base_fill(&fill);
    fill.pitch_bytes = 1664ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_UNSUPPORTED);
    /* 800 pixels at 8 bpp is in the table but not a multiple of 64. */
    base_fill(&fill);
    fill.bytes_per_pixel = 1ul;
    fill.pitch_bytes = 800ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_UNSUPPORTED);

    /* An origin off the 32-pixel grid. */
    base_fill(&fill);
    fill.target_offset = 2000ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_UNSUPPORTED);
    /* An origin that is not a whole pixel. */
    base_fill(&fill);
    fill.target_offset = 1152001ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_UNSUPPORTED);

    base_fill(&fill);
    fill.bytes_per_pixel = 3ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) == V9X_STATUS_UNSUPPORTED);
    base_fill(&fill);
    fill.width = 0ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_fill(&fill);
    fill.left = 950ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* Past the end of VRAM. */
    base_fill(&fill);
    fill.top = 4360ul;
    CHECK(v9x_mga_build_fill(&fill, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_mga_build_fill(0, &writes) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_copy_forward(void)
{
    struct v9x_mga_copy copy;
    struct v9x_mga_writes writes;
    v9x_u32 row = 40ul * 960ul;

    /* Destination above the source: top to bottom, left to right. */
    base_copy(&copy);
    copy.source_top = 40ul;
    copy.destination_left = 100ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x040c4008ul);
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 960ul);
    CHECK(reg_value(&writes, V9X_MGA_YDSTORG) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_SGN) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_AR5) == 960ul);
    CHECK(reg_value(&writes, V9X_MGA_AR3) == row + 10ul);
    CHECK(reg_value(&writes, V9X_MGA_AR0) == row + 10ul + 29ul);
    /* The right edge is inclusive for a blit. */
    CHECK(reg_value(&writes, V9X_MGA_FXBNDRY) == ((129ul << 16) | 100ul));
    CHECK(reg_value(&writes, V9X_MGA_YDSTLEN + V9X_MGA_GO) ==
          ((20ul << 16) | 5ul));
    CHECK(go_is_last(&writes, V9X_MGA_YDSTLEN));
    CHECK(writes.count <= V9X_MGA_MAX_WRITES);
}

static void test_copy_down_is_bottom_up(void)
{
    struct v9x_mga_copy copy;
    struct v9x_mga_writes writes;

    base_copy(&copy);
    copy.destination_top = 22ul;
    copy.destination_left = 14ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_SGN) == 4ul);
    CHECK(reg_value(&writes, V9X_MGA_AR5) == 0ul - 960ul);
    CHECK(reg_value(&writes, V9X_MGA_AR3) == 24ul * 960ul + 10ul);
    CHECK(reg_value(&writes, V9X_MGA_AR0) == 24ul * 960ul + 39ul);
    CHECK(reg_value(&writes, V9X_MGA_YDSTLEN + V9X_MGA_GO) ==
          ((26ul << 16) | 5ul));
}

static void test_copy_right_is_right_to_left(void)
{
    struct v9x_mga_copy copy;
    struct v9x_mga_writes writes;

    base_copy(&copy);
    copy.destination_left = 12ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_SGN) == 1ul);
    CHECK(reg_value(&writes, V9X_MGA_AR5) == 960ul);
    CHECK(reg_value(&writes, V9X_MGA_AR3) == 20ul * 960ul + 39ul);
    CHECK(reg_value(&writes, V9X_MGA_AR0) == 20ul * 960ul + 10ul);
    CHECK(reg_value(&writes, V9X_MGA_FXBNDRY) == ((41ul << 16) | 12ul));

    /* Left on the same rows is forward. */
    base_copy(&copy);
    copy.destination_left = 8ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_SGN) == 0ul);
}

/* A sprite in off-screen memory with a pitch the linearizer could not
 * address as a destination, copied into the back buffer: the source is
 * linear, so its pitch is free. */
static void test_copy_between_surfaces(void)
{
    struct v9x_mga_copy copy;
    struct v9x_mga_writes writes;

    base_copy(&copy);
    copy.source_offset = 2400000ul;
    copy.source_pitch_bytes = 200ul;
    copy.source_left = 0ul;
    copy.source_top = 3ul;
    copy.destination_offset = 1152000ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_YDSTORG) == 576000ul);
    CHECK(reg_value(&writes, V9X_MGA_AR5) == 100ul);
    CHECK(reg_value(&writes, V9X_MGA_AR3) == 1200000ul + 300ul);
    CHECK(reg_value(&writes, V9X_MGA_SGN) == 0ul);

    /* The reverse is refused: the destination pitch is not linearisable. */
    base_copy(&copy);
    copy.destination_offset = 2400000ul;
    copy.destination_pitch_bytes = 200ul;
    copy.destination_left = 0ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_UNSUPPORTED);

    /* Two surfaces whose bytes overlap are refused, not guessed. */
    base_copy(&copy);
    copy.source_offset = 1920ul * 2ul;
    copy.destination_offset = 0ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_UNSUPPORTED);
}

static void test_copy_refusals(void)
{
    struct v9x_mga_copy copy;
    struct v9x_mga_writes writes;

    base_copy(&copy);
    copy.width = 0ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.source_left = 940ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_copy(&copy);
    copy.bytes_per_pixel = 3ul;
    CHECK(v9x_mga_build_copy(&copy, &writes) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_mga_build_copy(0, &writes) == V9X_STATUS_INVALID_ARGUMENT);
}

/* A string bitmap at 800x600x16: 64 pixels (8 bytes) by 13 rows at
 * (100, 200), drawn through columns 102..150. */
static void base_expand(struct v9x_mga_expand *expand)
{
    memset(expand, 0, sizeof(*expand));
    expand->vram_bytes = 8388608ul;
    expand->pitch_bytes = 1920ul;
    expand->bytes_per_pixel = 2ul;
    expand->left = 100ul;
    expand->top = 200ul;
    expand->width = 64ul;
    expand->height = 13ul;
    expand->clip_left = 102ul;
    expand->clip_right = 150ul;
    expand->foreground = 0x1234ul;
    expand->background = 0x5678ul;
}

static void test_expand_opaque(void)
{
    struct v9x_mga_expand expand;
    struct v9x_mga_writes writes;

    base_expand(&expand);
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_OK);
    /* ILOAD, linear source, BMONOWF, sgnzero, shftzero, RPL, bop C: the
     * value FreeBE's PutMonoImage writes for a replace mix. */
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x080c6089ul);
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 960ul);
    CHECK(reg_value(&writes, V9X_MGA_YDSTORG) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0x12341234ul);
    CHECK(reg_value(&writes, V9X_MGA_BCOL) == 0x56785678ul);
    /* Linear source: AR0 is the total pixel count less one. */
    CHECK(reg_value(&writes, V9X_MGA_AR0) == 64ul * 13ul - 1ul);
    CHECK(reg_value(&writes, V9X_MGA_AR3) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_AR5) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_CXBNDRY) == ((150ul << 16) | 102ul));
    /* Inclusive right edge, as for a blit. */
    CHECK(reg_value(&writes, V9X_MGA_FXBNDRY) == ((163ul << 16) | 100ul));
    CHECK(reg_value(&writes, V9X_MGA_YDSTLEN + V9X_MGA_GO) ==
          ((200ul << 16) | 13ul));
    CHECK(go_is_last(&writes, V9X_MGA_YDSTLEN));
    CHECK(writes.count <= V9X_MGA_MAX_WRITES);
    CHECK(v9x_mga_expand_dwords(&expand) == 26ul);
}

static void test_expand_transparent_and_depths(void)
{
    struct v9x_mga_expand expand;
    struct v9x_mga_writes writes;

    base_expand(&expand);
    expand.transparent = 1ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x480c6089ul);

    base_expand(&expand);
    expand.bytes_per_pixel = 1ul;
    expand.pitch_bytes = 960ul;
    expand.foreground = 0xabul;
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0xabababab);
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 960ul);

    base_expand(&expand);
    expand.bytes_per_pixel = 4ul;
    expand.pitch_bytes = 4096ul;
    expand.foreground = 0x00c0ffeeul;
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0x00c0ffeeul);
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 1024ul);

    /* A bitmap that is not a whole number of dwords pads at its end. */
    base_expand(&expand);
    expand.width = 24ul;
    expand.height = 3ul;
    expand.clip_right = 110ul;
    CHECK(v9x_mga_expand_dwords(&expand) == 3ul);
}

static void test_expand_refusals(void)
{
    struct v9x_mga_expand expand;
    struct v9x_mga_writes writes;

    base_expand(&expand);
    expand.clip_right = 164ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_expand(&expand);
    expand.clip_left = 99ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_expand(&expand);
    expand.clip_left = 151ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_expand(&expand);
    expand.height = 0ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* AR0 holds 18 bits of pixel count (p.4-20). */
    base_expand(&expand);
    expand.width = 1024ul;
    expand.height = 257ul;
    expand.left = 0ul;
    expand.clip_left = 0ul;
    expand.clip_right = 799ul;
    expand.top = 0ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_UNSUPPORTED);
    /* Past the 11-bit clip window. */
    base_expand(&expand);
    expand.left = 2000ul;
    expand.clip_left = 2000ul;
    expand.clip_right = 2010ul;
    expand.pitch_bytes = 4096ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_UNSUPPORTED);
    /* A pitch the linearizer has no entry for. */
    base_expand(&expand);
    expand.pitch_bytes = 1700ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_UNSUPPORTED);
    base_expand(&expand);
    expand.bytes_per_pixel = 3ul;
    CHECK(v9x_mga_build_expand(&expand, &writes) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_mga_build_expand(0, &writes) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_opmode_and_surface(void)
{
    /* dmamod <3:2> becomes DMA BLIT write; dmaDataSiz and dirDataSiz stay. */
    CHECK(v9x_mga_opmode_for_iload(0ul) == 0x00000004ul);
    CHECK(v9x_mga_opmode_for_iload(0x0003030cul) == 0x00030304ul);

    CHECK(v9x_mga_surface_ok(1920ul, 0ul, 2ul) != 0);
    CHECK(v9x_mga_surface_ok(960ul, 0ul, 1ul) != 0);
    CHECK(v9x_mga_surface_ok(1700ul, 0ul, 2ul) == 0);
    /* An origin of 32 pixels is on the 16-bpp grid; 1 pixel is not. */
    CHECK(v9x_mga_surface_ok(1920ul, 64ul, 2ul) != 0);
    CHECK(v9x_mga_surface_ok(1920ul, 2ul, 2ul) == 0);
    CHECK(v9x_mga_surface_ok(1920ul, 0ul, 3ul) == 0);
}

static void test_status(void)
{
    CHECK(v9x_mga_status_busy(0x00010000ul) != 0ul);
    CHECK(v9x_mga_status_busy(0x00020024ul) == 0ul);
    /* Reset value: 32 free slots, bempty set (p.4-57). */
    CHECK(v9x_mga_fifo_free(0x00000220ul) == 32ul);
    CHECK(v9x_mga_fifo_free(0x00000100ul) == 0ul);
}

unsigned int v9x_run_mga_engine_tests(void)
{
    test_setup();
    test_fill_16bpp();
    test_fill_8bpp_and_32bpp();
    test_fill_offscreen_origin();
    test_fill_refusals();
    test_copy_forward();
    test_copy_down_is_bottom_up();
    test_copy_right_is_right_to_left();
    test_copy_between_surfaces();
    test_copy_refusals();
    test_expand_opaque();
    test_expand_transparent_and_depths();
    test_expand_refusals();
    test_opmode_and_surface();
    test_status();
    return failures;
}
