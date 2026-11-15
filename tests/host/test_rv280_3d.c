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

static void zero_raster(struct v9x_rv280_3d_raster *raster)
{
    raster->z_enable = 0ul;
    raster->z_offset = 0ul;
    raster->z_pitch_bytes = 0ul;
    raster->z_func = 0ul;
    raster->z_write = 0ul;
    raster->blend_enable = 0ul;
    raster->blend_src = 0ul;
    raster->blend_dst = 0ul;
    raster->alpha_test_enable = 0ul;
    raster->alpha_func = 0ul;
    raster->alpha_ref = 0ul;
    raster->front_ccw = 0ul;
    raster->cull_front = 0ul;
    raster->cull_back = 0ul;
    raster->fog_enable = 0ul;
    raster->fog_colour = 0ul;
    raster->specular_enable = 0ul;
    raster->keep_x = 0ul;
    raster->masked_channels = 0ul;
    raster->scissor_enable = 0ul;
    raster->scissor_left = 0ul;
    raster->scissor_top = 0ul;
    raster->scissor_right = 0ul;
    raster->scissor_bottom = 0ul;
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
    target.raster = 0;
    target.layout = 0u;

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
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9900551eul);

    /* The pitch whole 32 bytes: 72 pixels at 16 bpp (144 bytes) drew
     * sheared on M-HNBJNV (V9XR23D Pitch8), at 32 bpp (288 bytes) exact. */
    target.pitch_bytes = 144ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.bytes_per_pixel = 4ul;
    target.pitch_bytes = 288ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    target.bytes_per_pixel = 2ul;
    target.pitch_bytes = 128ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);

    CHECK(v9x_rv280_build_3d_target(&target, 1, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9a00aa1eul);

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
        vertices[index].z_bits = 0x3d000000ul + index;
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
    CHECK(v9x_rv280_build_3d_triangles(vertices, 3u, 0, V9X_RV280_LAYOUT_ST,
                                       &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + 3u * 5u);
    CHECK(list.values[3] == 0xff000000ul);
    CHECK(list.values[4] == 0x3e000000ul);
    CHECK(list.values[5] == 0x3f000000ul);
    CHECK(list.values[6] == 0x41000001ul);

    /* With Z: X, Y, Z, colour, S, T. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, 3u, 0,
                                       V9X_RV280_LAYOUT_Z |
                                           V9X_RV280_LAYOUT_ST,
                                       &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + 3u * 6u);
    CHECK(list.values[1] == 0x41000000ul);
    CHECK(list.values[2] == 0x42000000ul);
    CHECK(list.values[3] == 0x3d000000ul);
    CHECK(list.values[4] == 0xff000000ul);
    CHECK(list.values[5] == 0x3e000000ul);
    CHECK(list.values[6] == 0x3f000000ul);
    CHECK(list.values[7] == 0x41000001ul);
    /* Z without texture: X, Y, Z, colour. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, 3u, 0, V9X_RV280_LAYOUT_Z,
                                       &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + 3u * 4u);
    CHECK(list.values[4] == 0xff000000ul);
    /* The longest list, Z and texture, fits one list. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, V9X_RV280_3D_MAX_VERTICES,
                                       0, V9X_RV280_LAYOUT_Z |
                                              V9X_RV280_LAYOUT_ST,
                                       &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + V9X_RV280_3D_MAX_VERTICES * 6u);
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
    target.raster = 0;
    target.layout = 0u;
    /* The probe's texture: 32 x 16 at 16 bpp, rows packed, 64 KiB past
     * the surface. */
    texture.fb_gpu_base = 0ul;
    texture.fb_bytes = HN_FB_BYTES;
    texture.offset = HN_SURFACE + 0x10000ul;
    texture.pitch_bytes = 64ul;
    texture.bytes_per_pixel = 2ul;
    texture.format = V9X_RV280_TEXFMT_DEFAULT;
    texture.width = 32ul;
    texture.height = 16ul;
    texture.linear = 0ul;
    texture.clamp = 0ul;

    /* ARGB1555 and ARGB4444 with their alpha, XRGB8888 without; a format
     * whose texel size disagrees is refused. */
    texture.format = V9X_RV280_TEXFMT_ARGB1555;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXFORMAT_0) == 0x00004543ul);
    texture.format = V9X_RV280_TEXFMT_ARGB4444;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXFORMAT_0) == 0x00004545ul);
    texture.format = V9X_RV280_TEXFMT_XRGB8888;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) ==
          V9X_STATUS_UNSUPPORTED);
    texture.bytes_per_pixel = 4ul;
    texture.pitch_bytes = 128ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXFORMAT_0) == 0x00004506ul);
    texture.format = V9X_RV280_TEXFMT_ARGB1555;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) ==
          V9X_STATUS_UNSUPPORTED);
    texture.format = 4ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) ==
          V9X_STATUS_UNSUPPORTED);
    texture.format = V9X_RV280_TEXFMT_DEFAULT;
    texture.bytes_per_pixel = 2ul;
    texture.pitch_bytes = 64ul;

    list.count = 0u;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) == V9X_STATUS_OK);
    /* The colour buffer as untextured. */
    CHECK(list_value(&list, V9X_RV280_RB3D_CNTL) == 0x00001000ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_COLOROFFSET) == HN_SURFACE);
    CHECK(list_value(&list, V9X_RV280_RB3D_COLORPITCH) == 64ul);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9900551eul);
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
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9a00aa1eul);
    /* The alpha forms: texel alpha, diffuse alpha, decal with diffuse. */
    CHECK(v9x_rv280_build_3d_textured_target(
              &target, 1, &texture, V9X_RV280_COMBINE_MODULATE_TEXEL_ALPHA,
              &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND_0) == 0x0000008aul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND_0) == 0x0010000aul);
    CHECK(v9x_rv280_build_3d_textured_target(
              &target, 1, &texture,
              V9X_RV280_COMBINE_MODULATE_DIFFUSE_ALPHA, &list) ==
          V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND_0) == 0x0000008aul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND_0) == 0x00001000ul);
    CHECK(v9x_rv280_build_3d_textured_target(
              &target, 1, &texture, V9X_RV280_COMBINE_DECAL_DIFFUSE_ALPHA,
              &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXCBLEND_0) == 0x0010000aul);
    CHECK(list_value(&list, V9X_RV280_PP_TXABLEND_0) == 0x00001000ul);
    /* A small texture's rows are padded to 32 bytes: 8 x 8 at 16 bpp has
     * 16-byte rows and a 32-byte pitch. */
    texture.width = 8ul;
    texture.height = 8ul;
    texture.bytes_per_pixel = 2ul;
    texture.pitch_bytes = 32ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_TXPITCH_0) == 0ul);
    CHECK(list_value(&list, V9X_RV280_PP_TXFORMAT_0) == 0x00003304ul);
    texture.pitch_bytes = 16ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    texture.width = 32ul;
    texture.height = 16ul;
    texture.bytes_per_pixel = 4ul;
    texture.pitch_bytes = 128ul;

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

    CHECK(v9x_rv280_build_3d_finish(0, &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u);
    CHECK(list.offsets[0] == V9X_RV280_WAIT_UNTIL &&
          list.values[0] == 0x00020000ul);
    /* With the Z cache flushed first. */
    CHECK(v9x_rv280_build_3d_finish(1, &list) == V9X_STATUS_OK);
    CHECK(list.count == 2u);
    CHECK(list.offsets[0] == V9X_RV280_RB3D_ZCACHE_CTLSTAT &&
          list.values[0] == 0x00000005ul);
    CHECK(list.offsets[1] == V9X_RV280_WAIT_UNTIL);
}

static void test_raster(void)
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_raster raster;
    struct v9x_rv280_3d_texture texture;
    struct v9x_rv280_3d_list list;

    target.fb_gpu_base = 0ul;
    target.fb_bytes = HN_FB_BYTES;
    target.offset = HN_SURFACE;
    target.pitch_bytes = 128ul;
    target.bytes_per_pixel = 2ul;
    target.width = 64ul;
    target.height = 32ul;
    target.raster = 0;
    target.layout = 0u;

    /* None: Z off and ALWAYS without writes, blending off, the alpha test
     * off and PASS. */
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_CNTL) == 0x00001000ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_ZSTENCILCNTL) == 0x00000070ul);
    CHECK(list_value(&list, V9X_RV280_PP_MISC) == 0x00000700ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_DEPTHOFFSET) == 0xdeadbeeful);

    /* Z16 LESS with writes, SRCALPHA / INVSRCALPHA, GREATER 128. */
    zero_raster(&raster);
    target.layout = V9X_RV280_LAYOUT_Z;
    raster.z_enable = 1ul;
    raster.z_offset = HN_SURFACE + 0x20000ul;
    raster.z_pitch_bytes = 128ul;
    raster.z_func = 1ul;
    raster.z_write = 1ul;
    raster.blend_enable = 1ul;
    raster.blend_src = V9X_RV280_BLEND_FACTOR_SRC_ALPHA;
    raster.blend_dst = V9X_RV280_BLEND_FACTOR_INV_SRC_ALPHA;
    raster.alpha_test_enable = 1ul;
    raster.alpha_func = 5ul;
    raster.alpha_ref = 128ul;
    target.raster = &raster;
    CHECK(v9x_rv280_build_3d_target(&target, 1, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_CNTL) == 0x00001101ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_DEPTHOFFSET) ==
          HN_SURFACE + 0x20000ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_DEPTHPITCH) == 64ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_ZSTENCILCNTL) == 0x40000010ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_BLENDCNTL) == 0x27260000ul);
    CHECK(list_value(&list, V9X_RV280_PP_MISC) == 0x00000580ul);
    CHECK(list_value(&list, V9X_RV280_PP_CNTL) == 0x00801000ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_0) == 0x00000801ul);

    /* Textured, the same raster: the alpha test joins texture unit 0. */
    texture.fb_gpu_base = 0ul;
    texture.fb_bytes = HN_FB_BYTES;
    texture.offset = HN_SURFACE + 0x10000ul;
    texture.pitch_bytes = 64ul;
    texture.bytes_per_pixel = 2ul;
    texture.format = V9X_RV280_TEXFMT_DEFAULT;
    texture.width = 32ul;
    texture.height = 16ul;
    texture.linear = 0ul;
    texture.clamp = 0ul;
    CHECK(v9x_rv280_build_3d_textured_target(&target, 0, &texture,
                                             V9X_RV280_COMBINE_DECAL,
                                             &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_CNTL) == 0x00801010ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_0) == 0x00000801ul);

    /* Z with no write, blending alone. */
    raster.z_write = 0ul;
    raster.alpha_test_enable = 0ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_ZSTENCILCNTL) == 0x00000010ul);
    CHECK(list_value(&list, V9X_RV280_PP_CNTL) == 0x00001000ul);

    /* Refusals: Z misaligned, its pitch not a multiple of 8 pixels, past
     * the frame buffer; codes out of range. */
    raster.z_offset = HN_SURFACE + 0x20008ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.z_offset = HN_SURFACE + 0x20000ul;
    raster.z_pitch_bytes = 120ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.z_pitch_bytes = 128ul;
    raster.z_offset = HN_FB_BYTES - 1024ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.z_offset = HN_SURFACE + 0x20000ul;
    raster.z_func = 8ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.z_func = 1ul;
    raster.blend_src = 31ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.blend_src = 38ul;
    raster.blend_dst = 42ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.blend_dst = 39ul;
    raster.alpha_test_enable = 1ul;
    raster.alpha_func = 8ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.alpha_func = 5ul;
    raster.alpha_ref = 256ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.alpha_ref = 128ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    /* A Z buffer needs Z in the vertex. */
    target.layout = 0u;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_cull_w_fog(void)
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_raster raster;
    struct v9x_rv280_3d_list list;

    target.fb_gpu_base = 0ul;
    target.fb_bytes = HN_FB_BYTES;
    target.offset = HN_SURFACE;
    target.pitch_bytes = 128ul;
    target.bytes_per_pixel = 2ul;
    target.width = 64ul;
    target.height = 32ul;
    target.raster = &raster;
    target.layout = 0u;
    zero_raster(&raster);

    /* No W: no viewport divide flags, W forced to one, as the init. */
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_SE_VTE_CNTL) == 0ul);
    CHECK(list_value(&list, V9X_RV280_SE_VAP_CNTL) == 0x00250000ul);
    CHECK(list_value(&list, V9X_RV280_PP_FOG_COLOR) == 0x06000000ul);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0xfffffffful);
    /* An XRGB8888 target keeps its X byte; RGB565 has none to keep. */
    raster.keep_x = 1ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0xfffffffful);
    CHECK((list_value(&list, V9X_RV280_RB3D_CNTL) & 0x00000002ul) == 0ul);
    target.bytes_per_pixel = 4ul;
    target.pitch_bytes = 256ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0x00fffffful);
    /* The mask is applied only with RB3D_CNTL's PLANE_MASK_ENABLE (bit 1):
     * without it V9XD32 on M-HNBJNV read FF13579Bh for 0013579Bh. */
    CHECK(list_value(&list, V9X_RV280_RB3D_CNTL) ==
          (0x00001800ul | 0x00000002ul));
    target.bytes_per_pixel = 2ul;
    target.pitch_bytes = 128ul;
    raster.keep_x = 0ul;

    /* Cull back faces, clockwise front: BFACE_SOLID cleared. */
    raster.cull_back = 1ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x99005518ul);
    /* Cull front faces, counter-clockwise front: FFACE_SOLID cleared and
     * bit 0 set. */
    raster.cull_back = 0ul;
    raster.cull_front = 1ul;
    raster.front_ccw = 1ul;
    CHECK(v9x_rv280_build_3d_target(&target, 1, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_SE_CNTL) == 0x9a00aa07ul);
    raster.cull_front = 0ul;
    raster.front_ccw = 0ul;

    /* W in the vertex: X, Y, Z divided, the W field 1/W, W kept. */
    target.layout = V9X_RV280_LAYOUT_Z | V9X_RV280_LAYOUT_W;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_SE_VTE_CNTL) == 0x00000300ul);
    CHECK(list_value(&list, V9X_RV280_SE_VAP_CNTL) == 0x00240000ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_0) == 0x00000803ul);
    /* Perspective-correct interpolation on, as Mesa's initial state: the
     * init's RE_CNTL of 0 drew affine with 1/W in the vertex. */
    CHECK(list_value(&list, V9X_RV280_RE_CNTL) == 0x00000008ul);

    /* Fog toward blue from the specular alpha, and specular added: both
     * need the specular colour in the vertex. */
    raster.fog_enable = 1ul;
    raster.fog_colour = 0x000000fful;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.layout |= V9X_RV280_LAYOUT_SPECULAR;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_FOG_COLOR) == 0x060000fful);
    CHECK(list_value(&list, V9X_RV280_PP_CNTL) == 0x00401000ul);
    CHECK(list_value(&list, V9X_RV280_SE_VTX_FMT_0) == 0x00002803ul);
    raster.specular_enable = 1ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_PP_CNTL) == 0x00601000ul);
    /* The fog colour is RGB only. */
    raster.fog_colour = 0x01000000ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_full_vertex(void)
{
    struct v9x_rv280_3d_vertex vertices[V9X_RV280_3D_MAX_VERTICES];
    struct v9x_rv280_3d_list list;
    v9x_u32 index;

    for (index = 0u; index < V9X_RV280_3D_MAX_VERTICES; ++index) {
        vertices[index].x_bits = 0x10u + index;
        vertices[index].y_bits = 0x20u + index;
        vertices[index].z_bits = 0x30u + index;
        vertices[index].w_bits = 0x40u + index;
        vertices[index].colour = 0x50u + index;
        vertices[index].specular = 0x60u + index;
        vertices[index].s_bits = 0x70u + index;
        vertices[index].t_bits = 0x80u + index;
    }
    /* X, Y, Z, W, colour, specular, S, T. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, 3u, 0,
                                       V9X_RV280_LAYOUT_Z |
                                           V9X_RV280_LAYOUT_W |
                                           V9X_RV280_LAYOUT_SPECULAR |
                                           V9X_RV280_LAYOUT_ST,
                                       &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + 3u * 8u);
    CHECK(list.values[1] == 0x10u && list.values[2] == 0x20u &&
          list.values[3] == 0x30u && list.values[4] == 0x40u &&
          list.values[5] == 0x50u && list.values[6] == 0x60u &&
          list.values[7] == 0x70u && list.values[8] == 0x80u);
    CHECK(list.values[9] == 0x11u);
    /* W without Z: X, Y, W, colour. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, 3u, 0, V9X_RV280_LAYOUT_W,
                                       &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + 3u * 4u);
    CHECK(list.values[3] == 0x40u && list.values[4] == 0x50u);
    /* The longest list fits one list. */
    CHECK(v9x_rv280_build_3d_triangles(vertices, V9X_RV280_3D_MAX_VERTICES,
                                       0, V9X_RV280_LAYOUT_Z |
                                              V9X_RV280_LAYOUT_W |
                                              V9X_RV280_LAYOUT_SPECULAR |
                                              V9X_RV280_LAYOUT_ST,
                                       &list) == V9X_STATUS_OK);
    CHECK(list.count == 1u + V9X_RV280_3D_MAX_VERTICES * 8u);
    CHECK(list.values[list.count - 1u] == 0x8bu);
}

/* The render interface's scissor and write mask. */
static void test_scissor_mask(void)
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_raster raster;
    struct v9x_rv280_3d_list list;

    target.fb_gpu_base = 0ul;
    target.fb_bytes = HN_FB_BYTES;
    target.offset = HN_SURFACE;
    target.pitch_bytes = 128ul;
    target.bytes_per_pixel = 2ul;
    target.width = 64ul;
    target.height = 32ul;
    target.raster = &raster;
    target.layout = 0u;
    zero_raster(&raster);

    /* No scissor: RE_CNTL perspective alone, the target's extent as
     * before. */
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RE_CNTL) == 0x00000008ul);
    CHECK(list_value(&list, V9X_RV280_RE_TOP_LEFT) == 0ul);
    CHECK(list_value(&list, V9X_RV280_RE_WIDTH_HEIGHT) ==
          (64ul | (32ul << 16)));

    /* A scissor of columns 12-39, rows 2-5: SCISSOR_ENABLE, the corners
     * inclusive. */
    raster.scissor_enable = 1ul;
    raster.scissor_left = 12ul;
    raster.scissor_top = 2ul;
    raster.scissor_right = 40ul;
    raster.scissor_bottom = 6ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RE_CNTL) == 0x0000000aul);
    CHECK(list_value(&list, V9X_RV280_RE_TOP_LEFT) == (12ul | (2ul << 16)));
    CHECK(list_value(&list, V9X_RV280_RE_WIDTH_HEIGHT) ==
          (39ul | (5ul << 16)));
    /* The whole target. */
    raster.scissor_left = 0ul;
    raster.scissor_top = 0ul;
    raster.scissor_right = 64ul;
    raster.scissor_bottom = 32ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RE_WIDTH_HEIGHT) ==
          (63ul | (31ul << 16)));
    /* Empty, or past the target: refused. */
    raster.scissor_right = 0ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.scissor_right = 65ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.scissor_right = 64ul;
    raster.scissor_bottom = 33ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.scissor_top = 32ul;
    raster.scissor_bottom = 32ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
    raster.scissor_enable = 0ul;

    /* Red masked at 16 bpp: its bits in both halves, the enable set. */
    raster.masked_channels = V9X_RV280_CHANNEL_RED;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0x07ff07fful);
    CHECK((list_value(&list, V9X_RV280_RB3D_CNTL) & 0x00000002ul) != 0ul);
    raster.masked_channels = V9X_RV280_CHANNEL_GREEN |
                             V9X_RV280_CHANNEL_BLUE;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0xf800f800ul);
    /* At 32 bpp, with the X byte kept too. */
    target.bytes_per_pixel = 4ul;
    target.pitch_bytes = 256ul;
    raster.keep_x = 1ul;
    raster.masked_channels = V9X_RV280_CHANNEL_GREEN;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0x00ff00fful);
    CHECK((list_value(&list, V9X_RV280_RB3D_CNTL) & 0x00000002ul) != 0ul);
    raster.keep_x = 0ul;
    raster.masked_channels = V9X_RV280_CHANNEL_RED |
                             V9X_RV280_CHANNEL_GREEN |
                             V9X_RV280_CHANNEL_BLUE;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) == V9X_STATUS_OK);
    CHECK(list_value(&list, V9X_RV280_RB3D_PLANEMASK) == 0xff000000ul);
    /* A bit past the three channels is refused. */
    raster.masked_channels = 8ul;
    CHECK(v9x_rv280_build_3d_target(&target, 0, &list) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_rv280_3d_tests(void)
{
    test_scissor_mask();
    test_init();
    test_target();
    test_triangles();
    test_textured();
    test_raster();
    test_cull_w_fog();
    test_full_vertex();
    test_finish();
    return failures;
}
