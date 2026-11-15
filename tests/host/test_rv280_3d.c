/*
 * The Radeon 9250 (RV280) 3D builders: the R200 init, colour-buffer state
 * and triangle lists, against the values xf86-video-ati 6.14.6 writes
 * (include\velocity9x\rv280_3d.h), on M-HNBJNV's frame buffer: GPU base 0,
 * a 64-pixel test surface at 16 MiB.
 */
#include <stdio.h>

#include "velocity9x/rv280_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define HN_FB_BYTES 0x02000000ul
#define HN_SURFACE  0x01000000ul

/* The value written to offset in the list, or 0xdeadbeef if none; the last
 * write wins. */
static v9x_u32 list_value(const struct v9x_rv280_3d_list *list,
                          v9x_u32 offset)
{
    v9x_u32 value = 0xdeadbeeful;
    v9x_u32 index;

    for (index = 0u; index < list->count; ++index) {
        if (list->offsets[index] == offset) {
            value = list->values[index];
        }
    }
    return value;
}

static void test_init(void)
{
    struct v9x_rv280_3d_list list;

    CHECK(v9x_rv280_build_3d_init(&list) == V9X_STATUS_OK);
    /* RB3D_CNTL cleared first, as RADEONEngineInit does. */
    CHECK(list.count > 0u && list.offsets[0] == V9X_RV280_RB3D_CNTL &&
          list.values[0] == 0ul);
    CHECK(list_value(&list, V9X_RV280_SE_VAP_CNTL_STATUS) == 0ul);
    CHECK(list_value(&list, V9X_RV280_PP_CNTL_X) == 0ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXMULTI_CTL_0) == 0ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_STATE_CNTL) == 0ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTE_CNTL) == 0ul);
    CHECK(list_value(&list, V9X_RV280_SE_VAP_CNTL) == 0x00250000ul);
    CHECK(list_value(&list, V9X_RV280_RE_AUX_SCISSOR_CNTL) == 0ul);
    CHECK(list_value(&list, V9X_RV280_RE_CNTL) == 0ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0xfffffffful);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9800021eul);
    CHECK(v9x_rv280_build_3d_init(0) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_target(void)
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_list list;

    target.fb_gpu_base = 0ul;
    target.fb_bytes = HN_FB_BYTES;
    target.offset = HN_SURFACE;
    target.pitch_bytes = 128ul;
    target.bytes_per_pixel = 2ul;
    target.width = 64ul;
    target.height = 32ul;

    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_CNTL) == 0x00001000ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_COLOROFFSET) == HN_SURFACE);
    /* The pitch in pixels. */
    CHECK(list_value(&list, V9X_RV280_RB3D_COLORPITCH) == 64ul);
    CHECK(list_value(&list, V9X_RV280_RE_TOP_LEFT) == 0ul);
    CHECK(list_value(&list, V9X_RV280_RE_WIDTH_HEIGHT) ==
          (64ul | (32ul << 16)));
    CHECK(list_value(&list, V9X_RV280_RB3D_BLENDCNTL) == 0x20210000ul);
    CHECK(list_value(&list, V9X_RV280_PP_CNTL) == 0x00001000ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND_0) == 0x00001000ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND2_0) == 0x00011000ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND_0) == 0x00001000ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND2_0) == 0x00011000ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_0) == 0x00000800ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_1) == 0ul);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9800011eul);

    CHECK(v9x_rv280_build_3d_target(&target, 1, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9800021eul);

    /* 32 bpp, a non-zero frame buffer base. */
    target.fb_gpu_base = 0x00100000ul;
    target.pitch_bytes = 256ul;
    target.bytes_per_pixel = 4ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_CNTL) == 0x00001800ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_COLOROFFSET) ==
          0x00100000ul + HN_SURFACE);
    CHECK(list_value(&list, V9X_RV280_RB3D_COLORPITCH) == 64ul);

    /* Refusals: 8 bpp has no format here; misaligned offset; a pitch not a
     * multiple of 8 pixels; past the frame buffer; too large. */
    target.fb_gpu_base = 0ul;
    target.bytes_per_pixel = 1ul;
    target.pitch_bytes = 64ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_UNSUPPORTED);
    target.bytes_per_pixel = 2ul;
    target.pitch_bytes = 128ul;
    target.offset = HN_SURFACE + 8ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.offset = HN_SURFACE;
    target.pitch_bytes = 120ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.pitch_bytes = 128ul;
    target.offset = HN_FB_BYTES - 1024ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.offset = HN_SURFACE;
    target.width = 2048ul;
    target.pitch_bytes = 4096ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.width = 80ul;
    target.pitch_bytes = 128ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rv280_build_3d_target(0, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_triangles(void)
{
    struct v9x_rv280_3d_vertex vertices[V9X_RV280_3D_MAX_VERTICES + 3u];
    struct v9x_rv280_3d_list list;
    v9x_u32 index;

    for (index = 0u; index < V9X_RV280_3D_MAX_VERTICES + 3u; ++index) {
        vertices[index].x_bits = 0x41000000ul + index;
        vertices[index].y_bits = 0x42000000ul + index;
        vertices[index].colour = 0xff000000ul + index;
        vertices[index].s_bits = 0x3e000000ul + index;
        vertices[index].t_bits = 0x3f000000ul + index;
    }

    CHECK(v9x_rv280_build_3d_triangles(vertices, 3u, 0, 0, &list) ==
          V9X_STATUS_OK);
    CHECK(list.count == 1u + 3u * 3u);
    CHECK(list.offsets[0] == V9X_RV280_SE_VF_CNTL);
    CHECK(list.values[0] == 0x00030034ul);
    /* X, Y, colour per vertex, in order, all to SE_PORT_DATA0. */
    CHECK(list.offsets[1] == V9X_RV280_SE_PORT_DATA0 &&
          list.values[1] == 0x41000000ul);
    CHECK(list.values[2] == 0x42000000ul);
    CHECK(list.values[3] == 0xff000000ul);
    CHECK(list.offsets[9] == V9X_RV280_SE_PORT_DATA0 &&
          list.values[9] == 0xff000002ul);

    CHECK(v9x_rv280_build_3d_triangles(vertices, 6u, 1, 0, &list) ==
          V9X_STATUS_OK);
    CHECK(list.values[0] == 0x00060074ul);
    CHECK(list.count == 1u + 6u * 3u);

    /* Textured: S and T after the colour. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, 3u, 0, 1, &list) ==
          V9X_STATUS_OK);
    CHECK(list.count == 1u + 3u * 5u);
    CHECK(list.values[3] == 0xff000000ul);
    CHECK(list.values[4] == 0x3e000000ul);
    CHECK(list.values[5] == 0x3f000000ul);
    CHECK(list.values[6] == 0x41000001ul);
    /* The longest textured list fits one list. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, V9X_RV280_3D_MAX_VERTICES,
                                       0, 1, &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + V9X_RV280_3D_MAX_VERTICES * 5u);
    CHECK(list.values[list.count - 1u] == 0x3f00000bul);

    CHECK(v9x_rv280_build_3d_triangles(vertices, 4u, 0, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rv280_build_3d_triangles(vertices, 0u, 0, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rv280_build_3d_triangles(vertices,
                                       V9X_RV280_3D_MAX_VERTICES + 3u, 0, 0,
                                       &list) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_rv280_build_3d_triangles(0, 3u, 0, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_textured(void)
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_texture texture;
    struct v9x_rv280_3d_list list;

    target.fb_gpu_base = 0ul;
    target.fb_bytes = HN_FB_BYTES;
    target.offset = HN_SURFACE;
    target.pitch_bytes = 128ul;
    target.bytes_per_pixel = 2ul;
    target.width = 64ul;
    target.height = 32ul;
    /* The probe's texture: 32 x 16 at 16 bpp, rows packed, 64 KiB past
     * the surface. */
    texture.fb_gpu_base = 0ul;
    texture.fb_bytes = HN_FB_BYTES;
    texture.offset = HN_SURFACE + 0x10000ul;
    texture.pitch_bytes = 64ul;
    texture.bytes_per_pixel = 2ul;
    texture.width = 32ul;
    texture.height = 16ul;
    texture.linear = 0ul;
    texture.clamp = 0ul;

    list.count = 0u;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) == V9X_STATUS_OK);
    /* The colour buffer as untextured. */
    CHECK(list_value(&list, V9X_RV280_RB3D_CNTL) == 0x00001000ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_COLOROFFSET) == HN_SURFACE);
    CHECK(list_value(&list, V9X_RV280_RB3D_COLORPITCH) == 64ul);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9800011eul);
    /* R200TextureSetup's unit 0: nearest, wrap; RGB565, log2 5 x 4. */
    CHECK(list_value(&list, V9X_RV280_PP_CNTL) == 0x00001010ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXFILTER_0) == 0ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXFORMAT_0) == 0x00004504ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXFORMAT_X_0) == 0ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXSIZE_0) ==
          (31ul | (15ul << 16)));
    CHECK(list_value(&list, V9X_RV280_PP_TXPITCH_0) == 32ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXOFFSET_0) ==
          HN_SURFACE + 0x10000ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND_0) == 0x0010000aul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND_0) == 0x0010000aul);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND2_0) == 0x00011000ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND2_0) == 0x00011000ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_0) == 0x00000800ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_1) == 0x00000002ul);

    /* Modulate, bilinear, clamped, ARGB8888 with alpha. */
    texture.linear = 1ul;
    texture.clamp = 1ul;
    texture.bytes_per_pixel = 4ul;
    texture.pitch_bytes = 128ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 1, &texture,
                                             V9X_RV280_COMBINE_MODULATE,
                                             &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXFILTER_0) == 0x11000003ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXFORMAT_0) == 0x00004546ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXPITCH_0) == 96ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND_0) == 0x0000008aul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND_0) == 0x0000008aul);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9800021eul);

    /* Refusals: not a power of two; pitch not packed; misaligned; past the
     * frame buffer; an unknown combine; 8 bpp. */
    texture.bytes_per_pixel = 2ul;
    texture.pitch_bytes = 64ul;
    texture.width = 24ul;
    texture.pitch_bytes = 48ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture, 0u,
                                             &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    texture.width = 32ul;
    texture.pitch_bytes = 96ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture, 0u,
                                             &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    texture.pitch_bytes = 64ul;
    texture.offset = HN_SURFACE + 0x10010ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture, 0u,
                                             &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    texture.offset = HN_FB_BYTES - 512ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture, 0u,
                                             &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    texture.offset = HN_SURFACE + 0x10000ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture, 7u,
                                             &list) ==
          V9X_STATUS_UNSUPPORTED);
    texture.bytes_per_pixel = 1ul;
    texture.pitch_bytes = 32ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture, 0u,
                                             &list) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, 0, 0u, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_finish(void)
{
    struct v9x_rv280_3d_list list;

    CHECK(v9x_rv280_build_3d_finish(&list) == V9X_STATUS_OK);
    CHECK(list.count == 1u);
    CHECK(list.offsets[0] == V9X_RV280_WAIT_UNTIL &&
          list.values[0] == 0x00020000ul);
}

unsigned int v9x_run_rv280_3d_tests(void)
{
    test_init();
    test_target();
    test_triangles();
    test_textured();
    test_finish();
    return failures;
}
