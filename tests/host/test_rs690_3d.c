#include <math.h>
#include <stdio.h>
#include <string.h>

#include "velocity9x/rs690_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* M-GB8KRE's frame buffer: 128 MiB at GPU address 78000000
 * (docs\decisions\2026-11-04-rs690-register-probe.md). */
#define RS690_FB_BASE  0x78000000ul
#define RS690_FB_BYTES 0x08000000ul

#define FLOAT_ONE 0x3f800000ul

/* The value of the last write to one register, or 0xdeadbeef if none. */
static v9x_u32 list_value(const struct v9x_rs690_3d_list *list, v9x_u32 offset)
{
    v9x_u32 index;
    v9x_u32 value = 0xdeadbeeful;

    for (index = 0u; index < list->count; ++index) {
        if (list->offsets[index] == offset) {
            value = list->values[index];
        }
    }
    return value;
}

/* Every write is a dword-aligned register in one of the blocks the probe
 * VxD lets through: the 2D block's WAIT_UNTIL and DST_PIPE_CONFIG, the
 * vertex port, the VAP, and the 3D state 4000h-4FFFh. */
static int list_in_windows(const struct v9x_rs690_3d_list *list)
{
    v9x_u32 index;

    for (index = 0u; index < list->count; ++index) {
        v9x_u32 offset = list->offsets[index];

        if ((offset & 3ul) != 0ul) {
            return 0;
        }
        if (offset == V9X_RS690_WAIT_UNTIL ||
            offset == V9X_RS690_DST_PIPE_CONFIG ||
            offset == V9X_RS690_RB2D_DSTCACHE_MODE ||
            offset == V9X_RS690_SE_PORT_DATA0 ||
            (offset >= 0x2080ul && offset < 0x2300ul) ||
            (offset >= 0x4000ul && offset < 0x5000ul)) {
            continue;
        }
        return 0;
    }
    return 1;
}

static double float_value(v9x_u32 bits)
{
    v9x_u32 exponent = (bits >> 23) & 0xfful;
    v9x_u32 mantissa = bits & 0x007ffffful;

    if (exponent == 0ul) {
        return ldexp((double)mantissa, -149);
    }
    return ldexp((double)(mantissa | 0x00800000ul), (int)exponent - 150);
}

/* bits is the single nearest to numerator / denominator: no neighbour is
 * strictly closer. */
static int float_is_nearest(v9x_u32 bits, v9x_u32 numerator,
                            v9x_u32 denominator)
{
    double exact = (double)numerator / (double)denominator;
    double here = fabs(float_value(bits) - exact);

    if (bits != 0ul && fabs(float_value(bits - 1ul) - exact) < here) {
        return 0;
    }
    if (fabs(float_value(bits + 1ul) - exact) < here) {
        return 0;
    }
    return (bits & 0x80000000ul) == 0ul;
}

static void test_floats(void)
{
    v9x_u32 n;

    CHECK(v9x_rs690_float_integer(0ul) == 0ul);
    CHECK(v9x_rs690_float_integer(1ul) == FLOAT_ONE);
    CHECK(v9x_rs690_float_integer(64ul) == 0x42800000ul);
    CHECK(v9x_rs690_float_integer(1440ul) == 0x44b40000ul);
    CHECK(v9x_rs690_float_integer(4080ul) == 0x457f0000ul);
    CHECK(v9x_rs690_float_integer(0x00ffffffful >> 4) == 0x4b7ffffful);

    CHECK(v9x_rs690_float_ratio(0ul, 255ul) == 0ul);
    CHECK(v9x_rs690_float_ratio(255ul, 255ul) == FLOAT_ONE);
    CHECK(v9x_rs690_float_ratio(1ul, 2ul) == 0x3f000000ul);
    CHECK(v9x_rs690_float_ratio(1ul, 3ul) == 0x3eaaaaabul);
    CHECK(v9x_rs690_float_ratio(2ul, 3ul) == 0x3f2aaaabul);
    for (n = 0ul; n <= 255ul; ++n) {
        if (!float_is_nearest(v9x_rs690_float_ratio(n, 255ul), n, 255ul)) {
            printf("  ratio %lu/255 -> %08lx\n", (unsigned long)n,
                   (unsigned long)v9x_rs690_float_ratio(n, 255ul));
            CHECK(0);
        }
    }
    for (n = 0ul; n <= 31ul; ++n) {
        CHECK(float_is_nearest(v9x_rs690_float_ratio(n, 31ul), n, 31ul));
    }
}

static void test_init(void)
{
    struct v9x_rs690_3d_list list;

    /* M-GB8KRE: one pipe (GB_PIPE_SELECT 13:12 = 0), RB2D_DSTCACHE_MODE
     * read as 0. */
    CHECK(v9x_rs690_build_3d_init(0ul, 0ul, &list) == V9X_STATUS_OK);
    CHECK(list.count == 66u);
    /* Linux r420_pipes_init, which runs for the RS690: GA_ENHANCE with the
     * TCL deadlock workaround's bits 2 and 3 too; SU_REG_DEST to the one
     * pipe; the 2D destination cache autoflushed and DC_DISABLE_IGNORE_PE,
     * ORed into what was read. */
    CHECK(list_value(&list, V9X_RS690_SU_REG_DEST) == 0x00000001ul);
    CHECK(list_value(&list, V9X_RS690_RB2D_DSTCACHE_MODE) == 0x00020100ul);
    CHECK(v9x_rs690_build_3d_init(0ul, 0x00000005ul, &list) ==
          V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_RB2D_DSTCACHE_MODE) == 0x00020105ul);
    CHECK(v9x_rs690_build_3d_init(0ul, 0ul, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.offsets[0] == V9X_RS690_RB3D_DSTCACHE_CTLSTAT);
    CHECK(list.values[0] == V9X_RS690_RB3D_DC_FLUSH_ALL);
    CHECK(list.offsets[2] == V9X_RS690_WAIT_UNTIL);
    CHECK(list.values[2] == (V9X_RS690_WAIT_2D_IDLECLEAN |
                             V9X_RS690_WAIT_3D_IDLECLEAN));
    CHECK(list_value(&list, V9X_RS690_GB_TILE_CONFIG) == 0x00000011ul);
    CHECK(list_value(&list, V9X_RS690_DST_PIPE_CONFIG) ==
          V9X_RS690_PIPE_AUTO_CONFIG);
    CHECK(list_value(&list, V9X_RS690_GB_MSPOS0) == 0x66666666ul);
    CHECK(list_value(&list, V9X_RS690_GB_MSPOS1) == 0x06666666ul);
    CHECK(list_value(&list, V9X_RS690_GA_ENHANCE) == 0x0000000ful);
    CHECK(list_value(&list, V9X_RS690_GA_POLY_MODE) == 0x00000120ul);
    CHECK(list_value(&list, V9X_RS690_GA_ROUND_MODE) == 0x00000005ul);
    CHECK(list_value(&list, V9X_RS690_GA_COLOR_CONTROL) == 0x0000aaaaul);
    CHECK(list_value(&list, V9X_RS690_SU_CULL_MODE) == 0x00000004ul);
    CHECK(list_value(&list, V9X_RS690_SU_DEPTH_SCALE) == 0x4b7ffffful);
    CHECK(list_value(&list, V9X_RS690_VAP_CNTL_STATUS) == 0x00000100ul);
    CHECK(list_value(&list, V9X_RS690_VAP_CNTL) == 0x0014025aul);
    CHECK(list_value(&list, V9X_RS690_VAP_VTE_CNTL) == 0x00000300ul);
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_0) ==
          0xf688f688ul);
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_1) ==
          0x0000f688ul);
    CHECK(list_value(&list, V9X_RS690_RS_IP_0) == 0x01610000ul);
    CHECK(list_value(&list, V9X_RS690_RS_IP_1) == 0x01610002ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_0) == 0x00000008ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_1) == 0x00000049ul);
    CHECK(list_value(&list, V9X_RS690_US_OUT_FMT_1) == 0x00001b0ful);
    CHECK(list_value(&list, V9X_RS690_RB3D_COLOR_CHANNEL_MASK) ==
          0x0000000ful);
    CHECK(list_value(&list, V9X_RS690_SC_EDGERULE) == 0x0a5294a5ul);
    CHECK(list_value(&list, V9X_RS690_SC_CLIP_0_A) ==
          (1440ul | (1440ul << 13)));
    CHECK(list_value(&list, V9X_RS690_SC_CLIP_0_B) ==
          (4080ul | (4080ul << 13)));
    CHECK(list_value(&list, V9X_RS690_SC_CLIP_RULE) == 0x0000aaaaul);
    CHECK(list_value(&list, V9X_RS690_SC_SCREENDOOR) == 0x00fffffful);
    CHECK(list.offsets[list.count - 1u] == V9X_RS690_SC_SCREENDOOR);

    /* Two, three and four pipes: R300, R420_3P, R420 encodings. */
    CHECK(v9x_rs690_build_3d_init(0x1000ul, 0ul, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_GB_TILE_CONFIG) == 0x00000017ul);
    CHECK(list_value(&list, V9X_RS690_SU_REG_DEST) == 0x00000003ul);
    CHECK(v9x_rs690_build_3d_init(0x2000ul, 0ul, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_GB_TILE_CONFIG) == 0x0000001dul);
    CHECK(list_value(&list, V9X_RS690_SU_REG_DEST) == 0x00000007ul);
    CHECK(v9x_rs690_build_3d_init(0x3000ul, 0ul, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_GB_TILE_CONFIG) == 0x0000001ful);
    CHECK(list_value(&list, V9X_RS690_SU_REG_DEST) == 0x0000000ful);

    CHECK(v9x_rs690_build_3d_init(0ul, 0ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void base_target(struct v9x_rs690_3d_target *target)
{
    memset(target, 0, sizeof(*target));
    target->fb_gpu_base = RS690_FB_BASE;
    target->fb_bytes = RS690_FB_BYTES;
    target->offset = 0x01000000ul;
    target->pitch_bytes = 128ul;
    target->bytes_per_pixel = 2ul;
    target->width = 64ul;
    target->height = 32ul;
}

static void test_target(void)
{
    struct v9x_rs690_3d_target target;
    struct v9x_rs690_3d_list list;

    base_target(&target);
    CHECK(v9x_rs690_build_3d_target(&target, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list_value(&list, V9X_RS690_RB3D_COLOROFFSET0) ==
          RS690_FB_BASE + 0x01000000ul);
    CHECK(list_value(&list, V9X_RS690_RB3D_COLORPITCH0) ==
          (64ul | V9X_RS690_COLOR_FORMAT_RGB565));
    CHECK(list_value(&list, V9X_RS690_SC_SCISSOR0) ==
          (1440ul | (1440ul << 13)));
    CHECK(list_value(&list, V9X_RS690_SC_SCISSOR1) ==
          ((1440ul + 63ul) | ((1440ul + 31ul) << 13)));

    /* Position FLOAT_2 to slot 0, colour FLOAT_4 to slot 2, last. */
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_0) ==
          0x62034001ul);
    CHECK(list_value(&list, V9X_RS690_VAP_VTX_SIZE) ==
          V9X_RS690_VERTEX_DWORDS);
    CHECK(list_value(&list, V9X_RS690_VAP_VTX_STATE_CNTL) == 0x00005555ul);
    CHECK(list_value(&list, V9X_RS690_VAP_VSM_VTX_ASSM) == 0x00000005ul);
    CHECK(list_value(&list, V9X_RS690_VAP_OUTPUT_VTX_FMT_0) == 0x00000003ul);
    CHECK(list_value(&list, V9X_RS690_VAP_OUTPUT_VTX_FMT_1) == 0ul);
    CHECK(list_value(&list, V9X_RS690_TX_ENABLE) == 0ul);

    /* One colour interpolated into shader temp 0. */
    CHECK(list_value(&list, V9X_RS690_RS_IP_0) == 0ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_0) == 0x00004000ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040080ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_COUNT) == 0ul);

    /* One ALU instruction, out = temp0 * 1 + 0, clamped. */
    CHECK(list_value(&list, V9X_RS690_US_CONFIG) == 0ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_0) == 0ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400000ul);
    CHECK(list_value(&list, V9X_RS690_US_OUT_FMT_0) == 0x00001b00ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c000000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40050a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x01000000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040889ul);
    CHECK(list_value(&list, V9X_RS690_RB3D_CBLEND) == 0ul);
    CHECK(list_value(&list, V9X_RS690_RB3D_ABLEND) == 0ul);
    CHECK(list_value(&list, V9X_RS690_ZB_CNTL) == 0ul);
    CHECK(list_value(&list, V9X_RS690_FG_ALPHA_FUNC) == 0ul);
    CHECK(list_value(&list, V9X_RS690_SU_CULL_MODE) == 0x00000004ul);
    CHECK(list.offsets[list.count - 1u] == V9X_RS690_VAP_VTX_SIZE);

    target.bytes_per_pixel = 4ul;
    target.pitch_bytes = 256ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_RB3D_COLORPITCH0) ==
          (64ul | V9X_RS690_COLOR_FORMAT_ARGB8888));

    /* 8 and 24 bpp; a pitch short of 8 pixels' alignment or the width; an
     * unaligned offset; a surface past the frame buffer or the scan
     * converter; nothing to write into. */
    base_target(&target);
    target.bytes_per_pixel = 1ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) == V9X_STATUS_UNSUPPORTED);
    target.bytes_per_pixel = 3ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) == V9X_STATUS_UNSUPPORTED);
    base_target(&target);
    target.pitch_bytes = 132ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) == V9X_STATUS_UNSUPPORTED);
    base_target(&target);
    target.width = 72ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_target(&target);
    target.offset = 0x01000008ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) == V9X_STATUS_UNSUPPORTED);
    base_target(&target);
    target.offset = RS690_FB_BYTES - 128ul * 31ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_target(&target);
    target.width = V9X_RS690_SURFACE_MAX + 8ul;
    target.pitch_bytes = target.width * 2ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_target(&target);
    target.height = 0ul;
    CHECK(v9x_rs690_build_3d_target(&target, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_target(&target);
    CHECK(v9x_rs690_build_3d_target(0, &list) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_target(&target, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void set_vertex(struct v9x_rs690_3d_vertex *vertex, v9x_u32 x,
                       v9x_u32 y, v9x_u32 red, v9x_u32 green, v9x_u32 blue)
{
    vertex->x = x;
    vertex->y = y;
    vertex->red = red;
    vertex->green = green;
    vertex->blue = blue;
    vertex->alpha = 255ul;
}

static void test_draw(void)
{
    struct v9x_rs690_3d_vertex vertices[V9X_RS690_3D_VERTICES_MAX + 1u];
    struct v9x_rs690_3d_list list;
    v9x_u32 index;

    set_vertex(&vertices[0], 4ul, 2ul, 255ul, 0ul, 0ul);
    set_vertex(&vertices[1], 40ul, 2ul, 0ul, 255ul, 0ul);
    set_vertex(&vertices[2], 4ul, 30ul, 0ul, 0ul, 255ul);
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices, 3ul,
                                  64ul, 32ul, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.count == 1u + 3u * 6u);
    CHECK(list.offsets[0] == V9X_RS690_VAP_VF_CNTL);
    CHECK(list.values[0] == 0x00030034ul);
    for (index = 1u; index < list.count; ++index) {
        CHECK(list.offsets[index] == V9X_RS690_SE_PORT_DATA0);
    }
    /* x, y, r, g, b, a of the first vertex; the second's green. */
    CHECK(list.values[1] == 0x40800000ul);
    CHECK(list.values[2] == 0x40000000ul);
    CHECK(list.values[3] == FLOAT_ONE);
    CHECK(list.values[4] == 0ul);
    CHECK(list.values[5] == 0ul);
    CHECK(list.values[6] == FLOAT_ONE);
    CHECK(list.values[7] == 0x42200000ul);
    CHECK(list.values[10] == FLOAT_ONE);

    set_vertex(&vertices[3], 40ul, 30ul, 0ul, 0ul, 0ul);
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_QUADS, vertices, 4ul, 64ul,
                                  32ul, &list) == V9X_STATUS_OK);
    CHECK(list.values[0] == 0x0004003dul);
    CHECK(list.count == 1u + 4u * 6u);

    /* Edges of the surface are coordinates; past them is not. */
    set_vertex(&vertices[0], 64ul, 32ul, 0ul, 0ul, 0ul);
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices, 3ul,
                                  64ul, 32ul, &list) == V9X_STATUS_OK);
    vertices[0].x = 65ul;
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices, 3ul,
                                  64ul, 32ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    vertices[0].x = 4ul;
    vertices[0].y = 33ul;
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices, 3ul,
                                  64ul, 32ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    vertices[0].y = 2ul;
    vertices[1].blue = 256ul;
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices, 3ul,
                                  64ul, 32ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    vertices[1].blue = 0ul;

    /* Not a whole number of primitives; none; too many; an unknown type. */
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices, 4ul,
                                  64ul, 32ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_QUADS, vertices, 3ul, 64ul,
                                  32ul, &list) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices, 0ul,
                                  64ul, 32ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    for (index = 0u; index <= V9X_RS690_3D_VERTICES_MAX; ++index) {
        set_vertex(&vertices[index], 1ul, 1ul, 0ul, 0ul, 0ul);
    }
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices,
                                  V9X_RS690_3D_VERTICES_MAX, 64ul, 32ul,
                                  &list) == V9X_STATUS_OK);
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, vertices,
                                  V9X_RS690_3D_VERTICES_MAX + 3u, 64ul, 32ul,
                                  &list) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_draw(5ul, vertices, 3ul, 64ul, 32ul, &list) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_rs690_build_3d_draw(V9X_RS690_PRIM_TRIANGLES, 0, 3ul, 64ul,
                                  32ul, &list) == V9X_STATUS_INVALID_ARGUMENT);
}

static void base_texture(struct v9x_rs690_3d_texture *texture)
{
    memset(texture, 0, sizeof(*texture));
    texture->offset = 0x01010000ul;
    texture->pitch_bytes = 64ul;
    texture->format = V9X_RS690_TEXFMT_RGB565;
    texture->width = 32ul;
    texture->height = 16ul;
    texture->filter = V9X_RS690_TEXFILTER_NEAREST;
    texture->address = V9X_RS690_TEXADDR_CLAMP;
    texture->combine = V9X_RS690_TEXCOMBINE_REPLACE;
}

static v9x_status build_texture(const struct v9x_rs690_3d_texture *texture,
                                struct v9x_rs690_3d_list *list)
{
    return v9x_rs690_build_3d_texture(RS690_FB_BASE, RS690_FB_BYTES, texture,
                                      list);
}

static void test_texture(void)
{
    struct v9x_rs690_3d_texture texture;
    struct v9x_rs690_3d_list list;

    base_texture(&texture);
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.count == 31u);

    /* Unit 0: clamp to the edge texel on S and T, nearest both ways; 32 x 16
     * texels addressed by pitch; RGB565 with alpha forced to one; pitch in
     * texels less one; the GPU address. */
    CHECK(list_value(&list, V9X_RS690_TX_FILTER0_0) == 0x00000a12ul);
    CHECK(list_value(&list, V9X_RS690_TX_FILTER1_0) == 0ul);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT0_0) == 0x8000781ful);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT1_0) == 0x0000aa06ul);

    /* A chroma key: the mode in TX_FILTER1's 1:0 and the key, two more
     * writes; an unknown mode is refused. */
    texture.chroma_mode = V9X_RS690_CHROMA_FORCE;
    texture.chroma_key = 0xff00ff00ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.count == 33u);
    CHECK(list_value(&list, V9X_RS690_TX_FILTER1_0) == 1ul);
    CHECK(list_value(&list, V9X_RS690_TX_CHROMA_KEY_0) == 0xff00ff00ul);
    texture.chroma_mode = 3ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    texture.chroma_mode = V9X_RS690_CHROMA_DISABLE;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT2_0) == 31ul);
    CHECK(list_value(&list, V9X_RS690_TX_OFFSET_0) ==
          RS690_FB_BASE + 0x01010000ul);
    CHECK(list_value(&list, V9X_RS690_TX_BORDER_COLOR_0) == 0ul);
    CHECK(list_value(&list, V9X_RS690_TX_INVALTAGS) == 0ul);
    CHECK(list_value(&list, V9X_RS690_TX_ENABLE) == 1ul);

    /* Position slot 0, colour slot 2, texture 0 slot 6 and last. */
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_0) ==
          0x42034001ul);
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_1) ==
          0x00006601ul);
    CHECK(list_value(&list, V9X_RS690_VAP_VSM_VTX_ASSM) == 0x00000405ul);
    CHECK(list_value(&list, V9X_RS690_VAP_OUTPUT_VTX_FMT_0) == 0x00000003ul);
    CHECK(list_value(&list, V9X_RS690_VAP_OUTPUT_VTX_FMT_1) == 0x00000002ul);
    CHECK(list_value(&list, V9X_RS690_VAP_VTX_SIZE) ==
          V9X_RS690_TEX_VERTEX_DWORDS);
    CHECK(list.offsets[list.count - 1u] == V9X_RS690_VAP_VTX_SIZE);

    /* s, t, 0, 1 into temp 0; colour into temp 1; two texture components
     * and one colour. */
    CHECK(list_value(&list, V9X_RS690_RS_IP_0) == 0x01610000ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040082ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_COUNT) == 0ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_0) == 0x00024008ul);

    /* First node has a texture load: LD temp 0 <- unit 0 at temp 0; then
     * out = temp0 * 1 + 0. */
    CHECK(list_value(&list, V9X_RS690_US_CONFIG) == 0x00000008ul);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 1ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_0) == 0ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400000ul);
    CHECK(list_value(&list, V9X_RS690_US_TEX_INST_0) == 0x00008000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c000000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40050a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x01000000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040889ul);

    /* ARGB8888, bilinear, wrapping, modulated: out = temp0 * temp1 + 0. */
    texture.format = V9X_RS690_TEXFMT_ARGB8888;
    texture.pitch_bytes = 128ul;
    texture.filter = V9X_RS690_TEXFILTER_LINEAR;
    texture.address = V9X_RS690_TEXADDR_WRAP;
    texture.combine = V9X_RS690_TEXCOMBINE_MODULATE;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FILTER0_0) == 0x00001400ul);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT1_0) == 0x0000a60cul);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT2_0) == 31ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c000040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40050200ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x01000040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040509ul);

    /* A wider pitch than the row: the pitch field follows the pitch. */
    base_texture(&texture);
    texture.pitch_bytes = 128ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT2_0) == 63ul);
    texture.width = 2048ul;
    texture.height = 2048ul;
    texture.pitch_bytes = 4096ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT0_0) == 0x803ffffful);

    /* Mipmaps: the highest level in TX_FORMAT0's 29:26, the mip filter in
     * TX_FILTER0's 14:13 (1 nearest, 2 linear) beside the min filter.
     * 32x16 has levels to 1x1: six. More, or an unknown mip filter, is
     * refused; one level with a mip filter is the base alone. */
    base_texture(&texture);
    texture.levels = 6ul;
    texture.mip_filter = V9X_RS690_MIPFILTER_NEAREST;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    /* TXPITCH_EN cleared: with it set the chip steps every level's rows
     * by TX_FORMAT2's base pitch (measured, MipLayoutPitch at 32 bpp). */
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT0_0) == 0x1400781ful);
    CHECK(list_value(&list, V9X_RS690_TX_FILTER0_0) == 0x00002a12ul);

    /* The measured layout: each level's rows width x bytes rounded to 64,
     * each level straight after the last (V9XRS3D MipLayoutNoPitch). */
    CHECK(v9x_rs690_mip_pitch(32ul, 2ul, 0ul) == 64ul);
    CHECK(v9x_rs690_mip_pitch(32ul, 2ul, 1ul) == 64ul);
    CHECK(v9x_rs690_mip_pitch(32ul, 4ul, 0ul) == 128ul);
    CHECK(v9x_rs690_mip_pitch(32ul, 4ul, 1ul) == 64ul);
    CHECK(v9x_rs690_mip_pitch(256ul, 4ul, 1ul) == 512ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 2ul, 0ul) == 0ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 2ul, 1ul) == 1024ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 2ul, 2ul) == 1536ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 2ul, 3ul) == 1792ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 2ul, 4ul) == 1920ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 2ul, 5ul) == 1984ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 2ul, 6ul) == 2048ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 4ul, 1ul) == 2048ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 4ul, 2ul) == 2560ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 4ul, 3ul) == 2816ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 4ul, 4ul) == 2944ul);
    CHECK(v9x_rs690_mip_offset(32ul, 16ul, 4ul, 6ul) == 3072ul);

    /* A mipmapped base whose pitch is not the chip's own is refused, and
     * so is a chain past the frame buffer. */
    texture.pitch_bytes = 128ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    texture.pitch_bytes = 64ul;
    texture.offset = RS690_FB_BYTES - 1536ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.levels = 6ul;
    texture.mip_filter = V9X_RS690_MIPFILTER_NEAREST;
    texture.mip_filter = V9X_RS690_MIPFILTER_LINEAR;
    texture.filter = V9X_RS690_TEXFILTER_LINEAR;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FILTER0_0) == 0x00005412ul);
    texture.levels = 7ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    texture.levels = 6ul;
    texture.mip_filter = 3ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    base_texture(&texture);
    texture.levels = 1ul;
    texture.mip_filter = V9X_RS690_MIPFILTER_NEAREST;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT0_0) == 0x8000781ful);

    /* ARGB1555 and ARGB4444: two bytes a texel, every channel straight
     * (X, Y, Z, W), W1Z5Y5X5 0Bh and W4Z4Y4X4 0Ah. */
    base_texture(&texture);
    texture.format = V9X_RS690_TEXFMT_ARGB1555;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT1_0) == 0x0000a60bul);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT2_0) == 31ul);
    texture.format = V9X_RS690_TEXFMT_ARGB4444;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT1_0) == 0x0000a60aul);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT2_0) == 31ul);

    /* Unknown format, filter, addressing, combine. */
    base_texture(&texture);
    texture.format = 5ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    base_texture(&texture);
    texture.filter = 2ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    base_texture(&texture);
    texture.address = 2ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    base_texture(&texture);
    texture.combine = 2ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);

    /* Misaligned offset or pitch. */
    base_texture(&texture);
    texture.offset = 0x01010010ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    base_texture(&texture);
    texture.pitch_bytes = 80ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    /* 32-byte aligned is not enough: the RS690 steps 64-byte rows. An
     * 8-texel 16-bit texture needs its row padded to 64 bytes. */
    base_texture(&texture);
    texture.width = 8ul;
    texture.height = 8ul;
    texture.pitch_bytes = 32ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    texture.pitch_bytes = 96ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_UNSUPPORTED);
    texture.pitch_bytes = 64ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_TX_FORMAT2_0) == 31ul);

    /* Empty, too large, a pitch shorter than a row, past the frame buffer. */
    base_texture(&texture);
    texture.width = 0ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.height = 0ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.width = 2049ul;
    texture.pitch_bytes = 4160ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.width = 64ul;
    texture.pitch_bytes = 64ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.offset = RS690_FB_BYTES - 64ul * 15ul;
    texture.offset &= ~31ul;
    CHECK(build_texture(&texture, &list) == V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    CHECK(v9x_rs690_build_3d_texture(RS690_FB_BASE, RS690_FB_BYTES, 0,
                                     &list) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_texture(RS690_FB_BASE, RS690_FB_BYTES, &texture,
                                     0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void set_tex_vertex(struct v9x_rs690_3d_tex_vertex *vertex, v9x_u32 x,
                           v9x_u32 y, v9x_u32 s, v9x_u32 t)
{
    vertex->x = x;
    vertex->y = y;
    vertex->red = 255ul;
    vertex->green = 128ul;
    vertex->blue = 0ul;
    vertex->alpha = 255ul;
    vertex->s = s;
    vertex->t = t;
}

static void test_draw_textured(void)
{
    struct v9x_rs690_3d_tex_vertex vertices[V9X_RS690_3D_VERTICES_MAX + 1u];
    struct v9x_rs690_3d_list list;
    v9x_u32 index;

    set_tex_vertex(&vertices[0], 4ul, 0ul, 0ul, 0ul);
    set_tex_vertex(&vertices[1], 4ul, 16ul, 0ul, FLOAT_ONE);
    set_tex_vertex(&vertices[2], 36ul, 16ul, FLOAT_ONE, FLOAT_ONE);
    set_tex_vertex(&vertices[3], 36ul, 0ul, FLOAT_ONE, 0ul);
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_QUADS, vertices,
                                           4ul, 64ul, 16ul, &list) ==
          V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.count == 1u + 4u * 8u);
    CHECK(list.values[0] == 0x0004003dul);
    for (index = 1u; index < list.count; ++index) {
        CHECK(list.offsets[index] == V9X_RS690_SE_PORT_DATA0);
    }
    /* The second vertex: x, y, r, g, b, a, s, t. */
    CHECK(list.values[9] == 0x40800000ul);
    CHECK(list.values[10] == 0x41800000ul);
    CHECK(list.values[11] == FLOAT_ONE);
    CHECK(list.values[12] == v9x_rs690_float_ratio(128ul, 255ul));
    CHECK(list.values[13] == 0ul);
    CHECK(list.values[14] == FLOAT_ONE);
    CHECK(list.values[15] == 0ul);
    CHECK(list.values[16] == FLOAT_ONE);

    /* Coordinates past 1 (wrapping) go through unchanged. */
    vertices[2].s = 0x40000000ul;
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_QUADS, vertices,
                                           4ul, 64ul, 16ul, &list) ==
          V9X_STATUS_OK);
    CHECK(list.values[1u + 2u * 8u + 6u] == 0x40000000ul);

    /* Twelve vertices fill the list without overflowing it. */
    for (index = 0u; index <= V9X_RS690_3D_VERTICES_MAX; ++index) {
        set_tex_vertex(&vertices[index], 1ul, 1ul, 0ul, 0ul);
    }
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_TRIANGLES, vertices,
                                           V9X_RS690_3D_VERTICES_MAX, 64ul,
                                           16ul, &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + V9X_RS690_3D_VERTICES_MAX * 8u);

    /* The same refusals as the colour-only draw. */
    vertices[0].x = 65ul;
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_TRIANGLES, vertices,
                                           3ul, 64ul, 16ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    vertices[0].x = 1ul;
    vertices[0].alpha = 256ul;
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_TRIANGLES, vertices,
                                           3ul, 64ul, 16ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    vertices[0].alpha = 255ul;
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_TRIANGLES, vertices,
                                           V9X_RS690_3D_VERTICES_MAX + 3u,
                                           64ul, 16ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_QUADS, vertices,
                                           3ul, 64ul, 16ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_draw_textured(5ul, vertices, 3ul, 64ul, 16ul,
                                           &list) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_TRIANGLES, 0, 3ul,
                                           64ul, 16ul, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void base_state(struct v9x_rs690_3d_state *state)
{
    memset(state, 0, sizeof(*state));
    base_target(&state->target);
    base_texture(&state->texture);
    state->depth_offset = 0x01020000ul;
    state->depth_pitch_bytes = 128ul;
    state->depth_func = V9X_RS690_ZFUNC_LESS;
    state->depth_write = 1ul;
    state->blend_src = V9X_RS690_BLEND_SRC_ALPHA;
    state->blend_dst = V9X_RS690_BLEND_INV_SRC_ALPHA;
    state->alpha_func = V9X_RS690_AFUNC_GREATER;
    state->alpha_ref = 128ul;
}

static void test_state(void)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;

    /* Everything off: untextured, no Z, blend, alpha test or cull. */
    base_state(&state);
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.offsets[list.count - 1u] == V9X_RS690_VAP_VTX_SIZE);
    CHECK(list_value(&list, V9X_RS690_VAP_VTX_SIZE) ==
          V9X_RS690_TL_VERTEX_DWORDS);

    /* The TL vertex: position FLOAT_4 signed to slot 0; diffuse BYTE
     * normalized to slot 2; specular the same to slot 3; tu, tv FLOAT_2
     * signed to slot 6, last. Colours swizzled BGRA to RGBA. */
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_0) ==
          0x82044003ul);
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_1) ==
          0x66018304ul);
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_0) ==
          0xf60af688ul);
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_EXT_1) ==
          0xf688f60aul);
    CHECK(list_value(&list, V9X_RS690_VAP_VSM_VTX_ASSM) == 0x00000405ul);
    CHECK(list_value(&list, V9X_RS690_VAP_OUTPUT_VTX_FMT_0) == 0x00000007ul);
    CHECK(list_value(&list, V9X_RS690_VAP_OUTPUT_VTX_FMT_1) == 0x00000002ul);
    CHECK(list_value(&list, V9X_RS690_VAP_VTE_CNTL) == 0x00000300ul);

    /* The colour path of the target state. */
    CHECK(list_value(&list, V9X_RS690_TX_ENABLE) == 0ul);
    CHECK(list_value(&list, V9X_RS690_RS_IP_0) == 0ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040080ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_0) == 0x00004000ul);
    CHECK(list_value(&list, V9X_RS690_US_CONFIG) == 0ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40050a80ul);
    CHECK(list_value(&list, V9X_RS690_RB3D_COLORPITCH0) ==
          (64ul | V9X_RS690_COLOR_FORMAT_RGB565));
    CHECK(list_value(&list, V9X_RS690_SC_SCISSOR1) ==
          ((1440ul + 63ul) | ((1440ul + 31ul) << 13)));

    CHECK(list_value(&list, V9X_RS690_ZB_CNTL) == 0ul);
    CHECK(list_value(&list, V9X_RS690_ZB_ZSTENCILCNTL) == 0ul);
    CHECK(list_value(&list, V9X_RS690_ZB_DEPTHOFFSET) == 0xdeadbeeful);
    CHECK(list_value(&list, V9X_RS690_RB3D_CBLEND) == 0ul);
    CHECK(list_value(&list, V9X_RS690_RB3D_ABLEND) == 0ul);
    CHECK(list_value(&list, V9X_RS690_FG_ALPHA_FUNC) == 0ul);
    CHECK(list_value(&list, V9X_RS690_SU_CULL_MODE) == 0x00000004ul);

    /* Everything on. */
    state.textured = 1ul;
    state.texture.combine = V9X_RS690_TEXCOMBINE_MODULATE;
    state.texture_alpha = V9X_RS690_TEXALPHA_DIFFUSE;
    state.depth_enable = 1ul;
    state.blend_enable = 1ul;
    state.alpha_test_enable = 1ul;
    state.cull = V9X_RS690_CULL_BACK;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.count <= V9X_RS690_3D_WRITES);
    CHECK(list_value(&list, V9X_RS690_TX_ENABLE) == 1ul);
    CHECK(list_value(&list, V9X_RS690_TX_OFFSET_0) ==
          RS690_FB_BASE + 0x01010000ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_0) == 0x00024008ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040082ul);
    CHECK(list_value(&list, V9X_RS690_US_CONFIG) == 0x00000008ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40050200ul);
    /* Alpha from the diffuse colour alone: SRC1_A times one. */
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x01000040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x4004088aul);
    /* The stream stays the TL vertex's with a texture. */
    CHECK(list_value(&list, V9X_RS690_VAP_PROG_STREAM_CNTL_1) ==
          0x66018304ul);
    CHECK(list_value(&list, V9X_RS690_VAP_VTX_SIZE) ==
          V9X_RS690_TL_VERTEX_DWORDS);

    /* Z16: test and write, LESS, at the GPU address, pitch in pixels. */
    CHECK(list_value(&list, V9X_RS690_ZB_CNTL) == 0x00000006ul);
    CHECK(list_value(&list, V9X_RS690_ZB_ZSTENCILCNTL) == 0x00000001ul);
    CHECK(list_value(&list, V9X_RS690_ZB_FORMAT) == 0ul);
    CHECK(list_value(&list, V9X_RS690_ZB_DEPTHOFFSET) ==
          RS690_FB_BASE + 0x01020000ul);
    CHECK(list_value(&list, V9X_RS690_ZB_DEPTHPITCH) == 64ul);

    /* Blend enabled, reading the destination, ADD_CLAMP, the same factors
     * for alpha. */
    CHECK(list_value(&list, V9X_RS690_RB3D_CBLEND) == 0x27260005ul);
    CHECK(list_value(&list, V9X_RS690_RB3D_ABLEND) == 0x27260005ul);
    /* Reference 128, GREATER, enabled. */
    CHECK(list_value(&list, V9X_RS690_FG_ALPHA_FUNC) == 0x00000c80ul);
    CHECK(list_value(&list, V9X_RS690_SU_CULL_MODE) == 0x00000006ul);

    /* Z without writes; texture alpha following MODULATE and REPLACE. */
    state.depth_write = 0ul;
    state.texture_alpha = V9X_RS690_TEXALPHA_SAME;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_ZB_CNTL) == 0x00000002ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040509ul);
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    state.texture_alpha = V9X_RS690_TEXALPHA_MODULATE;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40050a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040509ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c000000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x01000040ul);
    state.texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040889ul);
    state.cull = V9X_RS690_CULL_FRONT;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_SU_CULL_MODE) == 0x00000005ul);

    /* Every channel written, unless the top byte is X. */
    CHECK(list_value(&list, V9X_RS690_RB3D_COLOR_CHANNEL_MASK) == 0x0000000ful);
    state.x_channel = 1ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_RB3D_COLOR_CHANNEL_MASK) == 0x00000007ul);
    state.x_channel = 0ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);

    /* Shading: Gouraud unless flat, first vertex provoking. */
    CHECK(list_value(&list, V9X_RS690_GA_COLOR_CONTROL) == 0x0000aaaaul);
    state.flat = 1ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_GA_COLOR_CONTROL) == 0x00005555ul);

    /* Specular, untextured: colour 1 through interpolator 1 into temp 1;
     * two colours; out.rgb = temp0 * 1 + temp1 (SEL_C SRC2_RGB, ADDR2 1). */
    base_state(&state);
    state.specular = 1ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list_value(&list, V9X_RS690_RS_IP_1) == 0x00000040ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_1) == 0x00024800ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040100ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_COUNT) == 1ul);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 1ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c001000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40020a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040889ul);
    CHECK(list.offsets[list.count - 1u] == V9X_RS690_VAP_VTX_SIZE);

    /* Specular, textured MODULATE: texel temp 0, diffuse temp 1,
     * specular temp 2; out.rgb = temp0 * temp1 + temp2. */
    state.textured = 1ul;
    state.texture.combine = V9X_RS690_TEXCOMBINE_MODULATE;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_RS_INST_0) == 0x00024008ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_1) == 0x00044800ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040102ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_COUNT) == 1ul);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 2ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c002040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40020200ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x40040509ul);

    /* Specular, textured REPLACE: out.rgb = temp0 * 1 + temp2. */
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c002000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40020a80ul);

    /* Without specular the colour-only routing stands. */
    state.specular = 0ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_RS_INST_COUNT) == 0ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040082ul);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 1ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0ul);

    /* Fog, untextured, no specular: colour 1 still into temp 1 for its
     * alpha; instruction 0 writes result temp 2 (WMASK, not OMASK);
     * instruction 1 makes temp2.rgb = temp2.rgb - const0, unclamped, its
     * alpha a write-nothing MAD; instruction 2 is out.rgb = temp2.rgb *
     * temp1.a + const0 (SEL_A SRC1_RGB, SEL_B SRC2_AAA, SEL_C SRC0_RGB),
     * out.a = temp2.a; three ALU instructions; fog colour FF8000 as
     * float24. */
    base_state(&state);
    state.fog = 1ul;
    state.fog_color = 0x00ff8000ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list_value(&list, V9X_RS690_RS_IP_1) == 0x00000040ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_1) == 0x00024800ul);
    CHECK(list_value(&list, V9X_RS690_RS_COUNT) == 0x00040100ul);
    CHECK(list_value(&list, V9X_RS690_RS_INST_COUNT) == 1ul);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 2ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x03880000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40050a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x00880000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_1) == 0x038800a0ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_1) == 0x00080a84ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_1) == 0x00080000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_1) == 0x00040810ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_2) == 0x1c0010a0ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_2) == 0x40000704ul);
    /* SRC2_AAA reads the alpha unit's source 2: ADDR2 = temp 1 there too
     * (measured: with ADDR2 0 f read as temp 0's alpha, 1). */
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_2) == 0x01001002ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_2) == 0x40040889ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0x000000c0ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400080ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_CONST_R_0) == 0x003f0000ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_CONST_G_0) == 0x003e0101ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_CONST_B_0) == 0ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_CONST_A_0) == 0ul);
    CHECK(list.offsets[list.count - 1u] == V9X_RS690_VAP_VTX_SIZE);
    CHECK(list.count < V9X_RS690_3D_WRITES);

    /* Fog over textured MODULATE with specular: specular temp 2 serves
     * both, result temp 3; instruction 0 keeps its sources; the texture
     * instruction counted in CODE_OFFSET. */
    state.specular = 1ul;
    state.textured = 1ul;
    state.texture.combine = V9X_RS690_TEXCOMBINE_MODULATE;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 3ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x038c2040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40020200ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x008c0040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_2) == 0x1c0020e0ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_2) == 0x01002003ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0x000400c0ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400080ul);
    CHECK(list.count < V9X_RS690_3D_WRITES);

    /* ADD, textured, diffuse alpha, one instruction: out.rgb = temp0 * 1 +
     * temp1 (SEL_C SRC1_RGB), out.a = temp1.a. */
    base_state(&state);
    state.textured = 1ul;
    state.texture.combine = V9X_RS690_TEXCOMBINE_ADD;
    state.texture_alpha = V9X_RS690_TEXALPHA_DIFFUSE;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x1c000040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40010a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x01000040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x4004088aul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400000ul);

    /* ADD with specular: the sum into result temp 3, then + temp2. */
    state.specular = 1ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 3ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x038c0040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x40010a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x008c0040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_1) == 0x1c002003ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_1) == 0x40020a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_1) == 0x01000003ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_1) == 0x40040889ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0x00040080ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400040ul);

    /* DECALALPHA: result temp 2 = texel - diffuse, unclamped; out.rgb =
     * temp2 * texel.a + diffuse, the texel's alpha through the alpha
     * unit's source 1 (SRC1_AAA, alpha ADDR1 = temp 0); out.a = temp2.a,
     * the diffuse alpha instruction 0 put there. */
    base_state(&state);
    state.textured = 1ul;
    state.texture.combine = V9X_RS690_TEXCOMBINE_DECALALPHA;
    state.texture_alpha = V9X_RS690_TEXALPHA_DIFFUSE;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 2ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_0) == 0x03880040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_0) == 0x00090a80ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_0) == 0x00880040ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_0) == 0x4004088aul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_ADDR_1) == 0x1c000042ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_RGB_INST_1) == 0x40010680ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_ADDR_1) == 0x01000002ul);
    CHECK(list_value(&list, V9X_RS690_US_ALU_ALPHA_INST_1) == 0x40040889ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0x00040080ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400040ul);

    /* The longest program: DECALALPHA, specular and fog, five
     * instructions, and the list still holds it. */
    state.specular = 1ul;
    state.fog = 1ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RS690_US_PIXSIZE) == 3ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_OFFSET) == 0x00040140ul);
    CHECK(list_value(&list, V9X_RS690_US_CODE_ADDR_3) == 0x00400100ul);
    CHECK(list.offsets[list.count - 1u] == V9X_RS690_VAP_VTX_SIZE);
    CHECK(list.count < V9X_RS690_3D_WRITES);

    /* The texture builder alone has no program to give them. */
    state.texture.offset = 0x20000ul;
    CHECK(v9x_rs690_build_3d_texture(RS690_FB_BASE, RS690_FB_BYTES,
                                     &state.texture, &list) ==
          V9X_STATUS_UNSUPPORTED);

    /* float24. */
    CHECK(v9x_rs690_float24(0ul) == 0ul);
    CHECK(v9x_rs690_float24(0x3f800000ul) == 0x003f0000ul);
    CHECK(v9x_rs690_float24(0x3f000000ul) == 0x003e0000ul);
    CHECK(v9x_rs690_float24(0xbf800000ul) == 0x00bf0000ul);
    CHECK(v9x_rs690_float24(0x3f008080ul) == 0x003e0101ul);

    /* Refusals. */
    base_state(&state);
    state.depth_enable = 1ul;
    state.depth_offset = 0x01020010ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.depth_enable = 1ul;
    state.depth_pitch_bytes = 144ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    /* 48 pixels: 16-aligned, not 32. */
    state.depth_pitch_bytes = 96ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.depth_enable = 1ul;
    state.depth_pitch_bytes = 64ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.depth_enable = 1ul;
    state.depth_offset = RS690_FB_BYTES - 128ul * 16ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.depth_enable = 1ul;
    state.depth_func = 8ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.alpha_test_enable = 1ul;
    state.alpha_func = 8ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.alpha_test_enable = 1ul;
    state.alpha_ref = 256ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.blend_enable = 1ul;
    state.blend_src = 31ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.blend_enable = 1ul;
    state.blend_dst = 43ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.cull = 3ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.textured = 1ul;
    state.texture_alpha = 4ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.textured = 1ul;
    state.texture.format = 5ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    base_state(&state);
    state.target.bytes_per_pixel = 1ul;
    CHECK(v9x_rs690_build_3d_state(&state, &list) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_rs690_build_3d_state(0, &list) == V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    CHECK(v9x_rs690_build_3d_state(&state, 0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_draw_tl(void)
{
    v9x_u32 vertices[(V9X_RS690_3D_VERTICES_MAX + 1u) * 8u];
    struct v9x_rs690_3d_list list;
    v9x_u32 index;

    CHECK(v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_TRIANGLES, 3ul) ==
          0x00030034ul);
    CHECK(v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_QUADS, 4ul) == 0x0004003dul);
    CHECK(v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_TRIANGLES, 65535ul) ==
          0xffff0034ul);
    CHECK(v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_TRIANGLES, 4ul) == 0ul);
    CHECK(v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_TRIANGLES, 0ul) == 0ul);
    CHECK(v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_TRIANGLES, 65538ul) == 0ul);
    CHECK(v9x_rs690_3d_vf_cntl(5ul, 3ul) == 0ul);

    for (index = 0u; index < (V9X_RS690_3D_VERTICES_MAX + 1u) * 8u; ++index) {
        vertices[index] = 0x1000ul + index;
    }
    CHECK(v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_TRIANGLES, vertices, 3ul,
                                     &list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.count == 1u + 3u * 8u);
    CHECK(list.offsets[0] == V9X_RS690_VAP_VF_CNTL);
    CHECK(list.values[0] == 0x00030034ul);
    for (index = 1u; index < list.count; ++index) {
        CHECK(list.offsets[index] == V9X_RS690_SE_PORT_DATA0);
        CHECK(list.values[index] == 0x1000ul + index - 1u);
    }
    CHECK(v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_TRIANGLES, vertices,
                                     V9X_RS690_3D_VERTICES_MAX, &list) ==
          V9X_STATUS_OK);
    CHECK(list.count == 1u + V9X_RS690_3D_VERTICES_MAX * 8u);
    CHECK(v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_TRIANGLES, vertices,
                                     V9X_RS690_3D_VERTICES_MAX + 3u,
                                     &list) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_QUADS, vertices, 3ul,
                                     &list) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rs690_build_3d_draw_tl(5ul, vertices, 3ul, &list) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_TRIANGLES, 0, 3ul,
                                     &list) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_finish(void)
{
    struct v9x_rs690_3d_list list;

    CHECK(v9x_rs690_build_3d_finish(&list) == V9X_STATUS_OK);
    CHECK(list_in_windows(&list));
    CHECK(list.count == 4u);
    CHECK(list.offsets[0] == V9X_RS690_SC_CLIP_RULE);
    CHECK(list.values[0] == 0x0000aaaaul);
    CHECK(list.offsets[1] == V9X_RS690_RB3D_DSTCACHE_CTLSTAT);
    CHECK(list.values[1] == V9X_RS690_RB3D_DC_FLUSH_ALL);
    /* The Z cache too, so a CPU read of the Z buffer sees the draw. */
    CHECK(list.offsets[2] == V9X_RS690_ZB_ZCACHE_CTLSTAT);
    CHECK(list.values[2] == V9X_RS690_ZC_FLUSH_ALL);
    CHECK(list.offsets[3] == V9X_RS690_WAIT_UNTIL);
    CHECK(list.values[3] == V9X_RS690_WAIT_3D_IDLECLEAN);
    CHECK(v9x_rs690_build_3d_finish(0) == V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_rs690_3d_tests(void)
{
    failures = 0u;
    test_floats();
    test_init();
    test_target();
    test_draw();
    test_texture();
    test_draw_textured();
    test_state();
    test_draw_tl();
    test_finish();
    return failures;
}
