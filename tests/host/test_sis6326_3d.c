#include <stdio.h>
#include <string.h>

#include "velocity9x/sis6326_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static v9x_u32 write_value(const struct v9x_sis3d_writes *writes,
                           v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0u; index < writes->count; ++index) {
        if (writes->offsets[index] == offset) {
            return writes->values[index];
        }
    }
    return 0xdeadbeeful;
}

static void test_float_q4(void)
{
    CHECK(v9x_sis3d_float_q4(0) == 0x00000000ul);
    CHECK(v9x_sis3d_float_q4(16) == 0x3f800000ul);   /* 1.0 */
    CHECK(v9x_sis3d_float_q4(8) == 0x3f000000ul);    /* 0.5 */
    CHECK(v9x_sis3d_float_q4(-16) == 0xbf800000ul);  /* -1.0 */
    /* The coordinates SiS's HAL wrote, read back on 2026-10-04. */
    CHECK(v9x_sis3d_float_q4(132) == 0x41040000ul);  /* 8.25 */
    CHECK(v9x_sis3d_float_q4(892) == 0x425f0000ul);  /* 55.75 */
    CHECK(v9x_sis3d_float_q4(16 * 640) == 0x44200000ul); /* 640.0 */
}

/*
 * SiS's HAL drew a triangle with a = (8.25, 55.75), b = (55.75, 8.25),
 * c = (8.25, 8.25) and primitive word 00118682h: Gouraud, top b, middle c,
 * bottom a, fire on TSWc, direction 1, triangle (A8U4I5 boot 207).
 */
static void test_primitive_matches_sis_capture(void)
{
    struct v9x_sis3d_vertex vertices[3];
    v9x_u32 top;
    v9x_u32 middle;
    v9x_u32 bottom;

    memset(vertices, 0, sizeof(vertices));
    vertices[0].x = v9x_sis3d_float_q4(132);
    vertices[0].y = v9x_sis3d_float_q4(892);
    vertices[1].x = v9x_sis3d_float_q4(892);
    vertices[1].y = v9x_sis3d_float_q4(132);
    vertices[2].x = v9x_sis3d_float_q4(132);
    vertices[2].y = v9x_sis3d_float_q4(132);
    v9x_sis3d_order(vertices, &top, &middle, &bottom);
    CHECK(top == 1u && middle == 2u && bottom == 0u);
    CHECK(v9x_sis3d_primitive(vertices, V9X_SIS3D_SHADE_GOURAUD, 1) ==
          0x00118682ul);
    CHECK(v9x_sis3d_primitive(vertices, V9X_SIS3D_SHADE_GOURAUD, 0) ==
          0x00118602ul);
}

/* Negative Y sorts below positive, and ties keep input order. */
static void test_order_signs_and_ties(void)
{
    struct v9x_sis3d_vertex vertices[3];
    v9x_u32 top;
    v9x_u32 middle;
    v9x_u32 bottom;

    memset(vertices, 0, sizeof(vertices));
    vertices[0].y = v9x_sis3d_float_q4(32);    /* 2.0 */
    vertices[1].y = v9x_sis3d_float_q4(-16);   /* -1.0 */
    vertices[2].y = v9x_sis3d_float_q4(-32);   /* -2.0 */
    v9x_sis3d_order(vertices, &top, &middle, &bottom);
    CHECK(top == 2u && middle == 1u && bottom == 0u);

    vertices[0].y = v9x_sis3d_float_q4(16);
    vertices[1].y = v9x_sis3d_float_q4(16);
    vertices[2].y = v9x_sis3d_float_q4(16);
    v9x_sis3d_order(vertices, &top, &middle, &bottom);
    CHECK(top == 0u && middle == 1u && bottom == 2u);
}

/* SiS's 64x64 RGB565 target, pitch 128: 8A14h 0C110080h, clips 3Fh. */
static void test_state_matches_sis_capture(void)
{
    struct v9x_sis3d_target target;
    struct v9x_sis3d_writes writes;

    target.vram_bytes = 4194304ul;
    target.offset = 0x00196900ul;
    target.pitch_bytes = 128ul;
    target.width = 64ul;
    target.height = 64ul;
    CHECK(v9x_sis3d_build_flat_state(&target, &writes) == V9X_STATUS_OK);
    CHECK(writes.count == V9X_SIS3D_STATE_DWORDS);
    CHECK(write_value(&writes, V9X_SIS3D_DST_SET) == 0x0c110080ul);
    CHECK(write_value(&writes, V9X_SIS3D_DST_BASE) == 0x00196900ul);
    CHECK(write_value(&writes, V9X_SIS3D_CLIP_TB) == 0x0000003ful);
    CHECK(write_value(&writes, V9X_SIS3D_CLIP_LR) == 0x0000003ful);
    CHECK(write_value(&writes, V9X_SIS3D_BLEND) == 0x01000000ul);
    CHECK(write_value(&writes, V9X_SIS3D_ALPHA_SET) == 0x07000000ul);
    CHECK(write_value(&writes, V9X_SIS3D_ENABLE) ==
          V9X_SIS3D_ENABLE_PRIM_SETUP);
    CHECK(write_value(&writes, V9X_SIS3D_FOG) == 0ul);
    /* The primitive word is written last of the state, by the caller's
     * order: the builder leaves it out. */
    CHECK(write_value(&writes, V9X_SIS3D_PRIMITIVE) == 0xdeadbeeful);
}

static void test_state_refusals(void)
{
    struct v9x_sis3d_target target;
    struct v9x_sis3d_writes writes;

    target.vram_bytes = 4194304ul;
    target.offset = 0x00200000ul;
    target.pitch_bytes = 64ul;
    target.width = 32ul;
    target.height = 32ul;
    CHECK(v9x_sis3d_build_flat_state(0, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_sis3d_build_flat_state(&target, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);

    target.width = 33ul;   /* wider than the pitch holds at 16 bpp */
    CHECK(v9x_sis3d_build_flat_state(&target, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.width = 32ul;
    target.height = 0ul;
    CHECK(v9x_sis3d_build_flat_state(&target, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.height = 32ul;
    target.offset = 4194304ul - 1024ul;  /* runs past VRAM */
    CHECK(v9x_sis3d_build_flat_state(&target, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    target.offset = 0x00200000ul;
    target.pitch_bytes = 0x4000ul;       /* past the 14-bit pitch field */
    target.width = 64ul;
    target.vram_bytes = 0x40000000ul;
    CHECK(v9x_sis3d_build_flat_state(&target, &writes) ==
          V9X_STATUS_UNSUPPORTED);
    target.pitch_bytes = 64ul;
    target.width = 32ul;
    target.offset = 0x00400000ul;        /* past the address range */
    CHECK(v9x_sis3d_build_flat_state(&target, &writes) ==
          V9X_STATUS_UNSUPPORTED);
}

static void test_vertices(void)
{
    struct v9x_sis3d_vertex vertices[3];
    struct v9x_sis3d_writes writes;
    v9x_u32 index;

    memset(vertices, 0, sizeof(vertices));
    for (index = 0u; index < 3u; ++index) {
        vertices[index].x = 0x100ul + index;
        vertices[index].w = 0x200ul + index;
        vertices[index].argb = 0xff00ff00ul;
    }
    v9x_sis3d_build_vertices(vertices, &writes);
    CHECK(writes.count == V9X_SIS3D_VERTEX_DWORDS);
    CHECK(writes.offsets[0] == V9X_SIS3D_VERTEX_A + V9X_SIS3D_VERTEX_FS);
    /* The last write is vertex C's W, which fires the engine. */
    CHECK(writes.offsets[V9X_SIS3D_VERTEX_DWORDS - 1u] ==
          V9X_SIS3D_VERTEX_A + 2ul * V9X_SIS3D_VERTEX_STRIDE +
          V9X_SIS3D_VERTEX_W);
    CHECK(writes.values[V9X_SIS3D_VERTEX_DWORDS - 1u] == 0x202ul);
    CHECK(write_value(&writes, 0x8828ul) == 0x101ul);  /* B's X */
    CHECK(write_value(&writes, 0x8850ul) == 0xff00ff00ul);  /* C's ARGB */
}

static void test_float_fixed(void)
{
    CHECK(v9x_sis3d_float_fixed(1, 0) == 0x3f800000ul);    /* 1.0 */
    CHECK(v9x_sis3d_float_fixed(1, 8) == 0x3b800000ul);    /* 1/256 */
    CHECK(v9x_sis3d_float_fixed(1, 16) == 0x37800000ul);   /* 1/65536 */
    CHECK(v9x_sis3d_float_fixed(-3, 2) == 0xbf400000ul);   /* -0.75 */
    /* 4 - 1/256: the shifted tie vertex at its finest. */
    CHECK(v9x_sis3d_float_fixed(1023, 8) == 0x407fc000ul);
    CHECK(v9x_sis3d_float_fixed(132, 4) == v9x_sis3d_float_q4(132));
}

static void base_state(struct v9x_sis3d_state *state)
{
    memset(state, 0, sizeof(*state));
    state->target.vram_bytes = 4194304ul;
    state->target.offset = 0x00200000ul;
    state->target.pitch_bytes = 64ul;
    state->target.width = 32ul;
    state->target.height = 32ul;
    state->enable = V9X_SIS3D_ENABLE_PRIM_SETUP;
    state->z_offset = 0x00210000ul;
    state->z_pitch_bytes = 64ul;
    state->z_compare = V9X_SIS3D_CMP_ALWAYS;
    state->alpha_compare = V9X_SIS3D_CMP_ALWAYS;
    state->blend_source = V9X_SIS3D_BLEND_ONE;
    state->blend_destination = V9X_SIS3D_BLEND_ZERO;
}

static void test_full_state(void)
{
    struct v9x_sis3d_state state;
    struct v9x_sis3d_writes writes;

    base_state(&state);
    state.enable |= V9X_SIS3D_ENABLE_Z_TEST | V9X_SIS3D_ENABLE_Z_WRITE |
                    V9X_SIS3D_ENABLE_ALPHA_TEST | V9X_SIS3D_ENABLE_BLEND;
    state.z_compare = V9X_SIS3D_CMP_LESS;
    state.alpha_compare = V9X_SIS3D_CMP_GREATER;
    state.alpha_reference = 0x80ul;
    state.blend_source = V9X_SIS3D_BLEND_SRC_ALPHA;
    state.blend_destination = V9X_SIS3D_BLEND_INV_SRC_ALPHA;
    CHECK(v9x_sis3d_build_state(&state, &writes) == V9X_STATUS_OK);
    CHECK(writes.count == V9X_SIS3D_FULL_STATE_DWORDS);
    CHECK(writes.offsets[0] == V9X_SIS3D_ENABLE);
    CHECK(write_value(&writes, V9X_SIS3D_ENABLE) == 0x00320804ul);
    CHECK(write_value(&writes, V9X_SIS3D_Z_SET) == 0x00110040ul);
    CHECK(write_value(&writes, V9X_SIS3D_Z_BASE) == 0x00210000ul);
    CHECK(write_value(&writes, V9X_SIS3D_ALPHA_SET) == 0x04800000ul);
    CHECK(write_value(&writes, V9X_SIS3D_BLEND) == 0x54000000ul);
    CHECK(write_value(&writes, V9X_SIS3D_DST_SET) == 0x0c110040ul);
    CHECK(write_value(&writes, V9X_SIS3D_CLIP_LR) == 0x1ful);

    /* The values SiS's HAL left, as these encodings produce them: Z set
     * 00030000h is LEQUAL with format code 00 and pitch 0; alpha set
     * 07000000h is ALWAYS, reference 0. */
    base_state(&state);
    state.z_compare = V9X_SIS3D_CMP_ALWAYS;
    state.alpha_compare = V9X_SIS3D_CMP_ALWAYS;
    CHECK(v9x_sis3d_build_state(&state, &writes) == V9X_STATUS_OK);
    CHECK(write_value(&writes, V9X_SIS3D_ALPHA_SET) == 0x07000000ul);
    CHECK((write_value(&writes, V9X_SIS3D_Z_SET) & 0x00070000ul) ==
          0x00070000ul);

    /* Additive: ONE / ONE. */
    base_state(&state);
    state.blend_destination = V9X_SIS3D_BLEND_ONE;
    CHECK(v9x_sis3d_build_state(&state, &writes) == V9X_STATUS_OK);
    CHECK(write_value(&writes, V9X_SIS3D_BLEND) == 0x11000000ul);
}

static void test_full_state_refusals(void)
{
    struct v9x_sis3d_state state;
    struct v9x_sis3d_writes writes;

    base_state(&state);
    CHECK(v9x_sis3d_build_state(0, &writes) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_sis3d_build_state(&state, 0) == V9X_STATUS_INVALID_ARGUMENT);

    /* Reserved source factors and destination factors past 7. */
    base_state(&state);
    state.blend_source = V9X_SIS3D_BLEND_SRC_COLOR;
    CHECK(v9x_sis3d_build_state(&state, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.blend_destination = V9X_SIS3D_BLEND_DST_COLOR;
    CHECK(v9x_sis3d_build_state(&state, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.z_compare = 8ul;
    CHECK(v9x_sis3d_build_state(&state, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.alpha_reference = 256ul;
    CHECK(v9x_sis3d_build_state(&state, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* With Z enabled, the Z buffer must fit like the target. */
    base_state(&state);
    state.enable |= V9X_SIS3D_ENABLE_Z_TEST;
    state.z_offset = 4194304ul - 1024ul;
    CHECK(v9x_sis3d_build_state(&state, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_state(&state);
    state.enable |= V9X_SIS3D_ENABLE_Z_WRITE;
    state.z_pitch_bytes = 32ul;   /* narrower than 32 pixels of Z16 */
    CHECK(v9x_sis3d_build_state(&state, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* With Z disabled, the Z fields are not checked. */
    base_state(&state);
    state.z_pitch_bytes = 32ul;
    CHECK(v9x_sis3d_build_state(&state, &writes) == V9X_STATUS_OK);
}

static void base_texture(struct v9x_sis3d_texture *texture)
{
    texture->vram_bytes = 4194304ul;
    texture->format = V9X_SIS3D_TEXEL_ARGB4444;
    texture->log2_width = 6ul;
    texture->log2_height = 6ul;
    texture->levels = 1ul;
    texture->offset = 0x0019dc00ul;
    texture->pitch_bytes = 128ul;
    texture->mapping = V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V;
    texture->filter = V9X_SIS3D_MIN_NEAREST;
    texture->colour_mode = V9X_SIS3D_TBLEND_CTEX;
    texture->alpha_mode = V9X_SIS3D_TBLEND_ATEX;
    texture->clear_cache = 0;
    texture->blend_mask_bit = 0ul;
    /* Level field 1 is a two-level chain; SiS's level 1 base was not
     * captured, so it is placed after level 0. */
    texture->level_offsets[0] = 0x0019dc00ul + 128ul * 64ul;
}

/*
 * The pitch field measured on A8U4I5, 2026-10-05: exponent D[10:7],
 * mantissa D[6:0], (2m + 1) << (e + 2) bytes. 280h gave 128-byte rows,
 * 201h 192, 2FFh 32640, 001h 12, 080h 8.
 */
static void test_texture_pitch_field(void)
{
    v9x_u32 field = 0xdeadbeeful;

    CHECK(v9x_sis3d_texture_pitch_field(128ul, &field) == V9X_STATUS_OK);
    CHECK(field == 0x280ul);
    CHECK(v9x_sis3d_texture_pitch_field(64ul, &field) == V9X_STATUS_OK);
    CHECK(field == 0x200ul);
    CHECK(v9x_sis3d_texture_pitch_field(192ul, &field) == V9X_STATUS_OK);
    CHECK(field == 0x201ul);
    CHECK(v9x_sis3d_texture_pitch_field(32640ul, &field) == V9X_STATUS_OK);
    CHECK(field == 0x2fful);
    CHECK(v9x_sis3d_texture_pitch_field(12ul, &field) == V9X_STATUS_OK);
    CHECK(field == 0x001ul);
    CHECK(v9x_sis3d_texture_pitch_field(4ul, &field) == V9X_STATUS_OK);
    CHECK(field == 0x000ul);
    CHECK(v9x_sis3d_texture_pitch_field(131072ul, &field) == V9X_STATUS_OK);
    CHECK(field == 0x780ul);

    /* Not a multiple of 4, an odd factor past 255, past the exponent. */
    CHECK(v9x_sis3d_texture_pitch_field(0ul, &field) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_sis3d_texture_pitch_field(6ul, &field) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_sis3d_texture_pitch_field(4ul * 257ul, &field) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_sis3d_texture_pitch_field(262144ul, &field) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_sis3d_texture_pitch_field(128ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/*
 * SiS's HAL, 2026-10-04: 8A38h = 53030100h (ARGB4444, wrap U and V, level
 * field 1, nearest), 8A44h = 0019DC00h, 8A6Ch = 02800200h, 8A80h log2
 * size 6 x 6. Read as the last level's index, field 1 is a 64x64 level 0
 * (128-byte rows) and a 32x32 level 1 (64-byte rows): exactly SiS's
 * pitch word.
 */
static void test_texture_matches_sis_capture(void)
{
    struct v9x_sis3d_texture texture;
    struct v9x_sis3d_writes writes;

    base_texture(&texture);
    CHECK(v9x_sis3d_build_texture(&texture, &writes) == V9X_STATUS_OK);
    CHECK(writes.count == V9X_SIS3D_TEXTURE_DWORDS + 1u);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_SET) == 0x53030100ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_BLEND) == 0x00000000ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_BASE0) == 0x0019dc00ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_PITCH01) == 0x02800200ul);
    CHECK((write_value(&writes, V9X_SIS3D_TEXTURE_SIZE) & 0xff000000ul) ==
          0x66000000ul);

    /* Linear both ways, clear the cache, clamp, modulate with vertex
     * alpha. */
    base_texture(&texture);
    texture.format = V9X_SIS3D_TEXEL_RGB565;
    texture.log2_width = 8ul;
    texture.log2_height = 5ul;
    texture.levels = 0ul;
    texture.mapping = V9X_SIS3D_TEXTURE_CLAMP_U | V9X_SIS3D_TEXTURE_CLAMP_V;
    texture.filter = V9X_SIS3D_MAG_LINEAR | V9X_SIS3D_MIN_LINEAR;
    texture.clear_cache = 1;
    texture.colour_mode = V9X_SIS3D_TBLEND_CPIX;
    texture.alpha_mode = V9X_SIS3D_TBLEND_APIX;
    texture.pitch_bytes = 512ul;     /* 256 RGB565 texels */
    CHECK(v9x_sis3d_build_texture(&texture, &writes) == V9X_STATUS_OK);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_SET) == 0x51300019ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_BLEND) == 0x05000000ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_PITCH01) == 0x03800000ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_SIZE) == 0x85000000ul);
}

/* A 32x32 RGB565 chain of six levels at tight pitches, levels below four
 * bytes a row padded to the 4-byte pitch unit; masked blends read Atex
 * bit 7. */
static void test_texture_mip_chain(void)
{
    struct v9x_sis3d_texture texture;
    struct v9x_sis3d_writes writes;
    v9x_u32 level;

    base_texture(&texture);
    texture.format = V9X_SIS3D_TEXEL_RGB565;
    texture.log2_width = 5ul;
    texture.log2_height = 5ul;
    texture.pitch_bytes = 64ul;
    texture.offset = 0x0024b000ul;
    texture.levels = 5ul;
    texture.filter = V9X_SIS3D_MIN_NEAREST_MIP_NEAREST;
    texture.blend_mask_bit = 7ul;
    for (level = 1ul; level <= 5ul; ++level) {
        texture.level_offsets[level - 1ul] = 0x0024b000ul + 0x700ul +
                                             level * 0x100ul;
    }
    CHECK(v9x_sis3d_build_texture(&texture, &writes) == V9X_STATUS_OK);
    CHECK(writes.count == 12u);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_SET) == 0x51037502ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_BASE0) == 0x0024b000ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_BASE0 + 4ul) ==
          0x0024b800ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_BASE0 + 20ul) ==
          0x0024bc00ul);
    /* 64 and 32 bytes; 16 and 8; 4 and 2 -> 4. */
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_PITCH01) == 0x02000180ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_PITCH01 + 4ul) ==
          0x01000080ul);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_PITCH01 + 8ul) ==
          0x00000000ul);

    /* More levels than the larger side has. */
    texture.levels = 6ul;
    texture.level_offsets[5] = 0x0024c000ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* A level past VRAM. */
    texture.levels = 5ul;
    texture.level_offsets[0] = 4194304ul - 256ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* The mask bit field holds 0-7. */
    base_texture(&texture);
    texture.blend_mask_bit = 8ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_texture_refusals(void)
{
    struct v9x_sis3d_texture texture;
    struct v9x_sis3d_writes writes;

    base_texture(&texture);
    CHECK(v9x_sis3d_build_texture(0, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_sis3d_build_texture(&texture, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.log2_width = 10ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.levels = 10ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* The engine ORs the row term into the column offset: a pitch whose
     * power-of-two factor is below the row's bytes misaddresses. 192 bytes
     * is 3 x 64 for 128-byte rows. */
    base_texture(&texture);
    texture.pitch_bytes = 192ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* 3 x 128 is fine: rows padded to 384 bytes. */
    base_texture(&texture);
    texture.pitch_bytes = 384ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) == V9X_STATUS_OK);
    CHECK(write_value(&writes, V9X_SIS3D_TEXTURE_PITCH01) == 0x02810200ul);
    base_texture(&texture);
    texture.pitch_bytes = 6ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_UNSUPPORTED);
    /* 64 rows of 128 bytes must fit in VRAM. */
    base_texture(&texture);
    texture.offset = 4194304ul - 4096ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* A format whose texel size the builder does not know. */
    base_texture(&texture);
    texture.format = 0x20ul;         /* YUV422 */
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_UNSUPPORTED);
    base_texture(&texture);
    texture.filter = 6ul;          /* minification codes stop at 5 */
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.alpha_mode = 3ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.colour_mode = 0x40ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    base_texture(&texture);
    texture.offset = 4194304ul;
    CHECK(v9x_sis3d_build_texture(&texture, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_sis6326_3d_tests(void)
{
    test_texture_pitch_field();
    test_texture_matches_sis_capture();
    test_texture_mip_chain();
    test_texture_refusals();
    test_float_fixed();
    test_full_state();
    test_full_state_refusals();
    test_float_q4();
    test_primitive_matches_sis_capture();
    test_order_signs_and_ties();
    test_state_matches_sis_capture();
    test_state_refusals();
    test_vertices();
    return failures;
}
