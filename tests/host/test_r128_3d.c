#include <stdio.h>
#include <string.h>

#include "velocity9x/r128_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define R3_VRAM 33554432ul

/* Index of the value written to reg by a stream's type-0 bursts, or -1. */
static int r3_find(const struct v9x_r128_3d_stream *stream, v9x_u32 reg)
{
    v9x_u32 at = 0u;

    while (at < stream->count) {
        v9x_u32 header = stream->dwords[at];
        v9x_u32 first = (header & 0x7ffful) << 2;
        v9x_u32 count = ((header >> 16) & 0x3ffful) + 1ul;
        v9x_u32 index;

        if ((header & 0xc0000000ul) != 0ul) {
            return -1;
        }
        for (index = 0u; index < count; ++index) {
            if (first + index * 4u == reg) {
                return (int)(at + 1u + index);
            }
        }
        at += 1u + count;
    }
    return -1;
}

static v9x_u32 r3_value(const struct v9x_r128_3d_stream *stream, v9x_u32 reg)
{
    int at = r3_find(stream, reg);

    return at < 0 ? 0xdeadbeeful : stream->dwords[at];
}

static void r3_base(struct v9x_r128_3d_state *state)
{
    memset(state, 0, sizeof(*state));
    state->vram_bytes = R3_VRAM;
    state->target_offset = 0ul;
    state->target_pitch_bytes = 1600ul;
    state->target_bytes_per_pixel = 2ul;
    state->src_blend = V9X_R128_BLEND_ONE;
    state->dst_blend = V9X_R128_BLEND_ZERO;
    state->alpha_func = V9X_R128_CMP_ALWAYS;
}

/* Untextured, no Z: the context block with the 3D scissor opened, the
 * setup registers, the mask burst, and no texture block. */
static void test_plain(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;

    r3_base(&state);
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(stream.count == 2u + 13u + 2u * 4u + 6u);
    /* The GUI pixel cache flushed and its reads invalidated first. */
    CHECK(stream.dwords[0] == (0x1748ul >> 2));
    CHECK(stream.dwords[1] == 0x00000007ul);
    CHECK(stream.dwords[2] == ((11ul << 16) | (0x1c80ul >> 2)));
    CHECK(r3_value(&stream, 0x1c80ul) == (100ul << 21));
    CHECK(r3_value(&stream, 0x1c84ul) == 0x7acc34daul);
    CHECK(r3_value(&stream, 0x1c88ul) == 0ul);
    CHECK(r3_value(&stream, 0x1c8cul) == 0x1fff1ffful);
    CHECK(r3_value(&stream, 0x1c98ul) == 0x00007070ul);
    CHECK(r3_value(&stream, 0x1c9cul) == 0x3f800000ul);
    CHECK(r3_value(&stream, 0x1ca0ul) == 0x07010600ul);
    CHECK(r3_value(&stream, 0x1bc4ul) == 0x000f4020ul);
    CHECK(r3_value(&stream, 0x071cul) == 0x000003dful);
    CHECK(r3_value(&stream, 0x1a00ul) == 0xc701018aul);
    CHECK(r3_value(&stream, 0x1bccul) == 0ul);
    CHECK(r3_value(&stream, 0x1d44ul) == 0xfffffffful);
    CHECK(r3_find(&stream, 0x1cb0ul) < 0);
}

/* Z16 less-equal with writes, blending source alpha over, an alpha test,
 * flat shading, fog. */
static void test_depth_blend_flat(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;

    r3_base(&state);
    state.depth_enable = 1ul;
    state.depth_write = 1ul;
    state.depth_func = V9X_R128_CMP_LESSEQUAL;
    state.depth_offset = 0x00400000ul;
    state.depth_pitch_bytes = 1600ul;
    state.depth_bytes_per_pixel = 2ul;
    state.blend_enable = 1ul;
    state.src_blend = V9X_R128_BLEND_SRCALPHA;
    state.dst_blend = V9X_R128_BLEND_INVSRCALPHA;
    state.alpha_test_enable = 1ul;
    state.alpha_func = V9X_R128_CMP_GREATER;
    state.alpha_ref = 0x80ul;
    state.flat_shade = 1ul;
    state.fog_enable = 1ul;
    state.fog_color = 0x00112233ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1c90ul) == 0x00400000ul);
    CHECK(r3_value(&stream, 0x1c94ul) == 100ul);
    CHECK(r3_value(&stream, 0x1c98ul) == 0x00007020ul);
    CHECK(r3_value(&stream, 0x1c9cul) == 0x3f800683ul);
    CHECK(r3_value(&stream, 0x1ca0ul) == 0x05540680ul);
    CHECK(r3_value(&stream, 0x1cacul) == 0x00112233ul);
    CHECK(r3_value(&stream, 0x1bc4ul) == 0x000f4008ul);
    CHECK(r3_value(&stream, 0x071cul) == 0x000003bful);
    CHECK(r3_value(&stream, 0x1a00ul) == 0xc554018aul);

    /* A depth surface that is not 32-byte aligned, and 24-bit Z. */
    state.depth_offset = 0x00400010ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    state.depth_offset = 0x00400000ul;
    state.depth_bytes_per_pixel = 4ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
}

/* A 64x32 RGB565 texture with a three-level chain, mip-nearest, modulate,
 * clamped in T. */
static void test_texture(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;
    int at;

    r3_base(&state);
    state.texture.enable = 1ul;
    state.texture.datatype = V9X_R128_TEX_RGB565;
    state.texture.width_log2 = 6ul;
    state.texture.height_log2 = 5ul;
    state.texture.levels = 3ul;
    state.texture.offsets[0] = 0x00800000ul;
    state.texture.offsets[1] = 0x00801000ul;
    state.texture.offsets[2] = 0x00801400ul;
    state.texture.min_filter = V9X_R128_FILTER_MIPNEAREST;
    state.texture.mag_filter = V9X_R128_FILTER_LINEAR;
    state.texture.clamp_t = 1ul;
    state.texture.combine = V9X_R128_COMBINE_MODULATE;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(stream.count == 2u + 13u + 2u * 4u + 6u + 15u);
    at = r3_find(&stream, 0x1cb0ul);
    CHECK(at > 0 && stream.dwords[at - 1] == ((13ul << 16) | (0x1cb0ul >> 2)));
    CHECK(r3_value(&stream, 0x1cb0ul) == 0x00041014ul);
    CHECK(r3_value(&stream, 0x1cb4ul) == 0x0418d043ul);
    CHECK(r3_value(&stream, 0x1cb8ul) == 0x00004566ul);
    /* The offset registers count up from the smallest level, as Mesa's
     * r128UploadTexImages writes them: the base level goes last. */
    CHECK(r3_value(&stream, 0x1cbcul) == 0x00801400ul);
    CHECK(r3_value(&stream, 0x1cc0ul) == 0x00801000ul);
    CHECK(r3_value(&stream, 0x1cc4ul) == 0x00800000ul);
    CHECK(r3_value(&stream, 0x1ce4ul) == 0x00800000ul);
    CHECK((r3_value(&stream, 0x1c9cul) & 0x10ul) != 0ul);

    /* One level: mip filtering refused, plain linear accepted with the
     * chain disabled. */
    state.texture.levels = 1ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    state.texture.min_filter = V9X_R128_FILTER_LINEAR;
    state.texture.combine = V9X_R128_COMBINE_REPLACE;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1cb0ul) == 0x00041092ul);
    CHECK(r3_value(&stream, 0x1cb4ul) == 0x04181040ul);
    CHECK(r3_value(&stream, 0x1cb8ul) == 0x00006566ul);
    /* Direct3D's MODULATE on a texture with alpha: colours multiplied,
     * the texel's alpha. */
    state.texture.combine = V9X_R128_COMBINE_MODULATE_TEXALPHA;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1cb4ul) == 0x04181043ul);
    state.texture.combine = 9ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    state.texture.combine = V9X_R128_COMBINE_REPLACE;

    /* More levels than the shape has, an unaligned level, a datatype the
     * builder does not offer. */
    state.texture.levels = 8ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    state.texture.levels = 1ul;
    state.texture.offsets[0] = 0x00800004ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    state.texture.offsets[0] = 0x00800000ul;
    state.texture.datatype = 2ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
}

static void test_target_refusals(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;

    r3_base(&state);
    state.target_bytes_per_pixel = 3ul;
    state.target_pitch_bytes = 2400ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    r3_base(&state);
    state.target_pitch_bytes = 1602ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    r3_base(&state);
    state.target_offset = 0x10ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    r3_base(&state);
    state.target_bytes_per_pixel = 4ul;
    state.target_pitch_bytes = 4096ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1c80ul) == (128ul << 21));
    CHECK((r3_value(&stream, 0x1c84ul) >> 8 & 0xful) == 6ul);
    /* An XRGB8888 target keeps its X byte: the ARGB8888 datatype writes
     * alpha there (V9XD32 read 0xFF13579B on A8U4I5, boot 526). */
    CHECK(r3_value(&stream, 0x1d44ul) == 0x00fffffful);
    CHECK(v9x_r128_3d_build_state(0, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* The scissor, half-open in, inclusive out as Mesa's r128UpdateClipping
 * writes it; the write mask as PLANE_3D_MASK_C per channel, each 16-bit
 * pixel's mask in both halves. */
static void test_scissor_and_mask(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;

    r3_base(&state);
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1c88ul) == 0ul);
    CHECK(r3_value(&stream, 0x1c8cul) == 0x1fff1ffful);

    state.scissor_enable = 1ul;
    state.scissor_left = 10ul;
    state.scissor_top = 20ul;
    state.scissor_right = 300ul;
    state.scissor_bottom = 200ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1c88ul) == ((20ul << 16) | 10ul));
    CHECK(r3_value(&stream, 0x1c8cul) == ((199ul << 16) | 299ul));
    state.scissor_right = 10ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    state.scissor_right = 8193ul;
    CHECK(v9x_r128_3d_build_state(&state, &stream) ==
          V9X_STATUS_UNSUPPORTED);
    state.scissor_enable = 0ul;

    state.masked_channels = V9X_R128_CHANNEL_RED;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1d44ul) == 0x07ff07fful);
    state.masked_channels = V9X_R128_CHANNEL_GREEN | V9X_R128_CHANNEL_BLUE;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1d44ul) == 0xf800f800ul);
    state.target_bytes_per_pixel = 4ul;
    state.target_pitch_bytes = 4096ul;
    state.masked_channels = V9X_R128_CHANNEL_BLUE;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x1d44ul) == 0x00ffff00ul);
}

/* The primitive header: one triangle is 2 + 24 dwords after the header,
 * so n = 25; the format is every TL bit; the count is the vertices. */
static void test_prim(void)
{
    v9x_u32 header[V9X_R128_PRIM_HEADER_DWORDS];
    v9x_u32 flush[V9X_R128_FLUSH_DWORDS];

    v9x_r128_3d_build_flush(flush);
    CHECK(flush[0] == (0x1748ul >> 2));
    CHECK(flush[1] == 0x00000003ul);

    CHECK(v9x_r128_3d_build_prim(1ul, header) == V9X_STATUS_OK);
    CHECK(header[0] == 0xc0192500ul);
    CHECK(header[1] == 0x000000c9ul);
    CHECK(header[2] == 0x00030034ul);
    CHECK(v9x_r128_3d_build_prim(64ul, header) == V9X_STATUS_OK);
    CHECK(header[0] == (0xc0002500ul | ((2ul + 192ul * 8ul - 1ul) << 16)));
    CHECK(header[2] == (0x34ul | (192ul << 16)));
    CHECK(v9x_r128_3d_build_prim(0ul, header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_3d_build_prim(65ul, header) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* The indexed primitive: Linux's r128_cce_dispatch_indices header - the
 * vertex list offset, the walker's vertex limit, the TL format, the
 * primitive with WALK_IND and the index count - then the indices two to a
 * dword, the first in the low half, rebased to the first vertex copied. */
static void test_indexed(void)
{
    v9x_u32 header[V9X_R128_INDX_HEADER_DWORDS];
    v9x_u32 packed[3];
    static const v9x_u16 list[5] = { 10u, 11u, 12u, 12u, 11u };

    CHECK(v9x_r128_3d_build_indexed(6ul, 0, 0x00011000ul, 4ul, header) ==
          V9X_STATUS_OK);
    /* Four body dwords before the indices, three of indices, n one less:
     * 6, as Linux's dwords - 2 counting the header. */
    CHECK(header[0] == (0xc0002300ul | (6ul << 16)));
    CHECK(header[1] == 0x00011000ul);
    CHECK(header[2] == 4ul);
    CHECK(header[3] == 0x000000c9ul);
    CHECK(header[4] == (0x14ul | (6ul << 16)));
    /* A strip; an odd count rounds the index dwords up. */
    CHECK(v9x_r128_3d_build_indexed(5ul, 1, 0ul, 3ul, header) ==
          V9X_STATUS_OK);
    CHECK(header[0] == (0xc0002300ul | (6ul << 16)));
    CHECK(header[4] == (0x16ul | (5ul << 16)));
    CHECK(v9x_r128_3d_build_indexed(2ul, 0, 0ul, 3ul, header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_3d_build_indexed(V9X_R128_INDX_INDICES_MAX + 3ul, 0,
                                    0ul, 3ul, header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_3d_build_indexed(3ul, 0, 0ul, 0ul, header) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* A list walked from the vertex buffer, no indices: as Linux's
     * r128_cce_dispatch_vertex, 3D_RNDR_GEN_INDX_PRIM with n 3 and
     * PRIM_WALK_LIST (20h) on TRI_LIST. */
    CHECK(v9x_r128_3d_build_list(6ul, 0x00012000ul, header) ==
          V9X_STATUS_OK);
    CHECK(header[0] == (0xc0002300ul | (3ul << 16)));
    CHECK(header[1] == 0x00012000ul);
    CHECK(header[2] == 6ul);
    CHECK(header[3] == 0x000000c9ul);
    CHECK(header[4] == (0x24ul | (6ul << 16)));
    CHECK(v9x_r128_3d_build_list(4ul, 0ul, header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_3d_build_list(0ul, 0ul, header) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r128_3d_build_list(V9X_R128_LIST_VERTICES_MAX + 3ul, 0ul,
                                 header) == V9X_STATUS_INVALID_ARGUMENT);

    CHECK(v9x_r128_3d_pack_indices(list, 5ul, 10ul, packed) == 3ul);
    CHECK(packed[0] == 0x00010000ul);
    CHECK(packed[1] == 0x00020002ul);
    /* The odd one out pads with zero in the high half. */
    CHECK(packed[2] == 0x00000001ul);
}

/* Hardware culling in PM4_VC_FPU_SETUP: none draws both faces as before;
 * Direct3D's CCW keeps what runs clockwise on the screen. */
static void test_cull(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;

    r3_base(&state);
    state.cull = V9X_R128_CULL_NONE;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x071cul) == 0x000003dful);
    state.cull = V9X_R128_CULL_CCW;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x071cul) == 0x000003d9ul);
    state.cull = V9X_R128_CULL_CW;
    CHECK(v9x_r128_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(r3_value(&stream, 0x071cul) == 0x000003d8ul);
}

unsigned int v9x_run_r128_3d_tests(void)
{
    failures = 0u;
    test_plain();
    test_depth_blend_flat();
    test_texture();
    test_target_refusals();
    test_scissor_and_mask();
    test_prim();
    test_indexed();
    test_cull();
    return failures;
}
