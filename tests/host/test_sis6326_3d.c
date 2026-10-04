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

unsigned int v9x_run_sis6326_3d_tests(void)
{
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
