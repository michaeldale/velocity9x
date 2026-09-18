/*
 * Phase 5: float transport, the three builders, the assembled stream and the
 * decoder.
 *
 * The highest-value case here is the golden stream - the exact expected dword
 * array written out in full, plus its CRC - because it is what the generator,
 * the mini-VDD's arm table and the validator all ultimately agree with. If any
 * of those three drifts, this is where it shows.
 *
 * Nothing in this file has been near the hardware. It proves that the builders
 * produce what the audit says the hardware wants, which is a different claim
 * from the hardware wanting it.
 */
#include <stdio.h>

#include "velocity9x/intel_gen3_3d.h"
#include "velocity9x/intel_gma.h"
/* The Gen3 render-target binding is a leaf unit precisely so this file can
 * reach it; the engine that calls it needs the DDHAL headers and cannot be
 * built here. */
#include "../../src/display32/d3d/d3d_i9xx_target.h"

static unsigned int failures = 0u;

/*
 * The five scenes' total staged dwords: 64 + 114 + 104 + 64 + 80.
 *
 * Scene 3 fell from 94 to 64 on 2026-09-17 when the alpha test retired
 * and the Gouraud triangle took its slot: one triangle instead of three,
 * and the plain state block instead of the alpha one. The total is
 * asserted rather than derived so a change to the table has to be
 * deliberate - a test that recomputed it from the same builders would
 * be comparing a number with itself.
 */
#define V9X_TEST_STAGED_DWORDS 426ul

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/* --------------------------------------------------------------------- */
/* 1. Float transport                                                     */
/* --------------------------------------------------------------------- */

static void test_float_round_trip(void)
{
    v9x_u32 bits = 0ul;
    v9x_u32 back = 0ul;
    v9x_u32 value;

    /* The exact patterns the triangle needs, checked against the values a
     * conforming single-precision encoder produces. */
    CHECK(v9x_i9xx_float_from_int(0ul, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(bits == 0x00000000ul);
    CHECK(v9x_i9xx_float_from_int(1ul, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(bits == 0x3f800000ul);
    CHECK(v9x_i9xx_float_from_int(120ul, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(bits == 0x42f00000ul);
    CHECK(v9x_i9xx_float_from_int(160ul, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(bits == 0x43200000ul);
    CHECK(v9x_i9xx_float_from_int(320ul, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(bits == 0x43a00000ul);
    CHECK(v9x_i9xx_float_from_int(400ul, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(bits == 0x43c80000ul);
    CHECK(v9x_i9xx_float_from_int(480ul, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(bits == 0x43f00000ul);

    /* Exhaustive round trip over every value a screen coordinate can take,
     * plus the powers of two either side of the exactness boundary. */
    for (value = 0ul; value < 4096ul; ++value) {
        CHECK(v9x_i9xx_float_from_int(value, &bits) == V9X_I9XX_FLOAT_OK);
        CHECK(v9x_i9xx_float_to_int(bits, &back) == V9X_I9XX_FLOAT_OK);
        CHECK(back == value);
    }
    CHECK(v9x_i9xx_float_from_int(0x00fffffful, &bits) == V9X_I9XX_FLOAT_OK);
    CHECK(v9x_i9xx_float_to_int(bits, &back) == V9X_I9XX_FLOAT_OK);
    CHECK(back == 0x00fffffful);
}

static void test_float_refusals(void)
{
    v9x_u32 bits = 0ul;
    v9x_u32 value = 0ul;

    /* At and above 2^24 a single cannot represent every integer, so the
     * converter refuses rather than rounding. */
    CHECK(v9x_i9xx_float_from_int(0x01000000ul, &bits) ==
          V9X_I9XX_FLOAT_TOO_LARGE);
    CHECK(bits == 0ul);
    CHECK(v9x_i9xx_float_from_int(0xfffffffful, &bits) ==
          V9X_I9XX_FLOAT_TOO_LARGE);
    CHECK(v9x_i9xx_float_from_int(1ul, 0) == V9X_I9XX_FLOAT_NOT_FINITE);

    /* Each refusal has its own reason. "The vertex was wrong" is not a
     * diagnosis; Phase 4 cost eight boots learning that. */
    CHECK(v9x_i9xx_float_to_int(0xbf800000ul, &value) ==
          V9X_I9XX_FLOAT_NEGATIVE);          /* -1.0 */
    CHECK(v9x_i9xx_float_to_int(0x80000000ul, &value) ==
          V9X_I9XX_FLOAT_NEGATIVE_ZERO);
    CHECK(v9x_i9xx_float_to_int(0x7f800000ul, &value) ==
          V9X_I9XX_FLOAT_NOT_FINITE);        /* +inf */
    CHECK(v9x_i9xx_float_to_int(0x7fc00000ul, &value) ==
          V9X_I9XX_FLOAT_NOT_FINITE);        /* quiet NaN */
    CHECK(v9x_i9xx_float_to_int(0x00000001ul, &value) ==
          V9X_I9XX_FLOAT_DENORMAL);
    CHECK(v9x_i9xx_float_to_int(0x3f000000ul, &value) ==
          V9X_I9XX_FLOAT_FRACTIONAL);        /* 0.5 */
    CHECK(v9x_i9xx_float_to_int(0x3fc00000ul, &value) ==
          V9X_I9XX_FLOAT_FRACTIONAL);        /* 1.5 */
    CHECK(v9x_i9xx_float_to_int(0x4b800000ul, &value) ==
          V9X_I9XX_FLOAT_TOO_LARGE);         /* 2^24 */
    CHECK(v9x_i9xx_float_to_int(0ul, 0) == V9X_I9XX_FLOAT_NOT_FINITE);

    /* Positive zero is the one zero that round-trips. */
    CHECK(v9x_i9xx_float_to_int(0x00000000ul, &value) == V9X_I9XX_FLOAT_OK);
    CHECK(value == 0ul);
}

/* --------------------------------------------------------------------- */
/* 2. The fragment program - the audit's derived dwords, now validated    */
/* --------------------------------------------------------------------- */

static void test_fragment_program(void)
{
    v9x_u32 stream[8];
    v9x_u32 written = 0ul;

    CHECK(v9x_i9xx_fragment_program_extent() == 7ul);
    CHECK(v9x_i9xx_build_fragment_program(stream, 8ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 7ul);

    /*
     * These seven dwords are what
     * docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md derived from the
     * field placements and marked UNVALIDATED. This is the validation the
     * audit asked for: the builder, written independently from those field
     * placements, produces exactly them.
     */
    CHECK(stream[0] == 0x7d050005ul);   /* header, payload 6, length 5 */
    CHECK(stream[1] == 0x190a3c00ul);   /* dcl T8, channels xyzw        */
    CHECK(stream[2] == 0x00000000ul);   /* D1 must be zero              */
    CHECK(stream[3] == 0x00000000ul);   /* D2 must be zero              */
    CHECK(stream[4] == 0x02203ca0ul);   /* mov oC, T8                   */
    CHECK(stream[5] == 0x01230000ul);   /* src0 swizzle .xyzw           */
    CHECK(stream[6] == 0x00000000ul);   /* A2: no src1, no src2         */

    CHECK(v9x_i9xx_build_fragment_program(stream, 6ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_fragment_program(0, 8ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* --------------------------------------------------------------------- */
/* 3. The vertex run                                                      */
/* --------------------------------------------------------------------- */

static void test_vertex_run(void)
{
    v9x_u32 stream[24];
    v9x_u32 written = 0ul;
    v9x_u32 vertex;

    CHECK(v9x_i9xx_vertex_run_extent() == 16ul);
    CHECK(v9x_i9xx_build_vertex_run(640ul, 480ul, stream, 24ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 16ul);

    /* Inline form (bit 23 clear), TRILIST, length = vertex dwords - 1. */
    CHECK(stream[0] == (V9X_I9XX_3DPRIMITIVE_INLINE | 14ul));
    CHECK((stream[0] & 0x00800000ul) == 0ul);

    /* Five dwords each: x, y, z, w, then ONE packed colour dword. */
    for (vertex = 0ul; vertex < 3ul; ++vertex) {
        v9x_u32 base = 1ul + vertex * 5ul;
        CHECK(stream[base + 2ul] == 0x00000000ul);   /* z = 0.0 */
        CHECK(stream[base + 3ul] == 0x3f800000ul);   /* w = 1.0 */
        CHECK(stream[base + 4ul] == V9X_I9XX_TRI_COLOR_BGRA);   /* packed BGRA */
    }
    /* The asymmetric triangle, so a transposed X/Y is visible. */
    CHECK(stream[1] == 0x43200000ul && stream[2] == 0x42f00000ul);
    CHECK(stream[6] == 0x43f00000ul && stream[7] == 0x42f00000ul);
    CHECK(stream[11] == 0x43a00000ul && stream[12] == 0x43c80000ul);

    /* Every vertex the same colour, which is what makes shading mode moot. */
    CHECK(stream[5] == stream[10] && stream[10] == stream[15]);

    /* A rectangle too small to contain the triangle is refused, not clipped. */
    CHECK(v9x_i9xx_build_vertex_run(320ul, 480ul, stream, 24ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_vertex_run(640ul, 200ul, stream, 24ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_vertex_run(0ul, 480ul, stream, 24ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /*
     * Insufficient capacity is INSUFFICIENT_MEMORY, not INVALID_ARGUMENT.
     *
     * It was INVALID_ARGUMENT until this builder became a call into
     * v9x_i9xx_build_triangle_run, which separates the two the way every
     * other builder in this tree does. Changed deliberately, and recorded
     * here rather than in a commit message alone: the only caller maps any
     * non-OK to INSUFFICIENT_MEMORY, so nothing in the driver distinguishes
     * them, and the emitted dwords are asserted byte-identical elsewhere.
     */
    CHECK(v9x_i9xx_build_vertex_run(640ul, 480ul, stream, 15ul, &written) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(written == 0ul);
}

/* --------------------------------------------------------------------- */
/* 4. The state block                                                     */
/* --------------------------------------------------------------------- */

static void test_3d_state(void)
{
    v9x_u32 stream[64];
    v9x_u32 written = 0ul;
    v9x_u32 index;
    v9x_u32 buf_infos;

    /*
     * Thirty-one, not thirty-four. The depth BUF_INFO and its two dwords left
     * on 2026-09-15: it declared a buffer at graphics address zero that the S6
     * depth enables guaranteed nothing would read.
     */
    CHECK(v9x_i9xx_3d_state_extent() == 31ul);
    CHECK(v9x_i9xx_build_3d_state(0x006c2000ul, 1280ul, 640ul, 480ul,
                                  stream, 64ul, &written) == V9X_STATUS_OK);
    CHECK(written == 31ul);

    /* The invariant block's first and last packets. */
    CHECK(stream[0] == 0x66014140ul);
    CHECK(stream[13] == V9X_I9XX_3DSTATE_LOAD_INDIRECT);
    CHECK(stream[14] == 0ul);

    /* Colour target: linear, correct pitch, correct address. */
    CHECK(stream[15] == V9X_I9XX_3DSTATE_BUF_INFO);
    CHECK(stream[16] == (V9X_I9XX_BUF_3D_ID_COLOR_BACK | 1280ul));
    CHECK((stream[16] & (V9X_I9XX_BUF_3D_USE_FENCE |
                         V9X_I9XX_BUF_3D_TILED_SURFACE)) == 0ul);
    CHECK(stream[17] == 0x006c2000ul);

    /*
     * NO second BUF_INFO. Asserted over the WHOLE stream rather than at the
     * offset the old one occupied: checking stream[18] alone would pass if a
     * depth binding reappeared anywhere else, and the point is that the stream
     * contains none.
     */
    buf_infos = 0ul;
    for (index = 0ul; index + 1ul < written; ++index) {
        if (stream[index] != V9X_I9XX_3DSTATE_BUF_INFO) {
            continue;
        }
        ++buf_infos;
        /* Its id field must be the colour back buffer, never depth. */
        CHECK((stream[index + 1ul] & V9X_I9XX_BUF_3D_ID_DEPTH) !=
              V9X_I9XX_BUF_3D_ID_DEPTH);
    }
    /* Exactly one, asserted: zero BUF_INFO packets would satisfy the loop
     * above and would also mean the colour target was never declared. */
    CHECK(buf_infos == 1ul);

    /* RGB565 with the half-pixel bias in both axes. */
    CHECK(stream[18] == V9X_I9XX_3DSTATE_DST_BUF_VARS);
    CHECK(stream[19] == 0x00880200ul);

    /* Draw rect is INCLUSIVE: 639 and 479, not 640 and 480. */
    CHECK(stream[20] == V9X_I9XX_3DSTATE_DRAW_RECT);
    CHECK(stream[23] == ((479ul << 16) | 639ul));

    /* S2..S6 in one load, length 4, and S4 agreeing with the vertex dwords. */
    CHECK(stream[25] == 0x7d0407c4ul);
    CHECK(stream[26] == 0xfffffffful);
    CHECK(stream[28] == 0x00902480ul);
    CHECK((stream[30] & (V9X_I9XX_S6_DEPTH_TEST_ENABLE |
                         V9X_I9XX_S6_DEPTH_WRITE_ENABLE)) == 0ul);
    /*
     * And the enable that was missing. S6 was zero until 2026-09-15 on the
     * reasoning that every relevant bit in it disables when clear; that holds
     * for depth, blend and alpha, and not for this one. With it clear the GPU
     * accepted the primitive, reported no error, and wrote no colour.
     */
    CHECK((stream[30] & V9X_I9XX_S6_COLOR_WRITE_ENABLE) != 0ul);

    /* A pitch the BUF_INFO encoding would silently truncate is refused. */
    CHECK(v9x_i9xx_build_3d_state(0x006c2000ul, 1281ul, 640ul, 480ul,
                                  stream, 64ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_3d_state(0x006c2000ul, 0ul, 640ul, 480ul,
                                  stream, 64ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_3d_state(0x006c2001ul, 1280ul, 640ul, 480ul,
                                  stream, 64ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_3d_state(0x006c2000ul, 1280ul, 0ul, 480ul,
                                  stream, 64ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_3d_state(0x006c2000ul, 1280ul, 640ul, 480ul,
                                  stream, 30ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* --------------------------------------------------------------------- */
/* 5. The assembled stream and its parameters                             */
/* --------------------------------------------------------------------- */

static void test_phase5_parameters(void)
{
    struct v9x_i9xx_phase5_parameters parameters;
    struct v9x_i9xx_sandbox_layout layout;

    v9x_i9xx_phase5_parameters(&parameters);
    CHECK(parameters.target_pitch == 1280ul);
    CHECK(parameters.target_bytes == 0x00096000ul);
    CHECK(parameters.width == 640ul);
    CHECK(parameters.height == 480ul);
    CHECK(parameters.triangle_color == V9X_I9XX_TRI_COLOR_BGRA);
    /* 34 state + 7 shader + 2 probe + 16 vertices. */
    /* 7 fill + 34 state + 7 shader + 2 probe + 16 vertices. */
    CHECK(parameters.stream_dwords == 64ul);

    /* The parameters and the layout must describe the same target. */
    CHECK(v9x_i9xx_sandbox_calculate(
              0x007b0000ul, 0x7f800000ul, &layout) == V9X_STATUS_OK);
    CHECK(layout.target_bytes == parameters.target_bytes);
    CHECK(layout.target_pitch == parameters.target_pitch);
    CHECK(parameters.target_bytes ==
          parameters.target_pitch * parameters.height);

    /*
     * The colour must DISCRIMINATE the candidate 8-bit to 5/6-bit conversions,
     * which is what it is now for.
     *
     * It used to be asserted 565-exact under truncation, on the audit's note
     * that dithering is on by default and not cleanly disableable. Two things
     * sit against that premise: S5 leaves S5_COLOR_DITHER_ENABLE clear, and on
     * 6c81c52 all seven interior probes read an identical value, which a
     * spatial dither would not produce. The probes agreeing is now the check
     * for it - if they ever disagree, dithering is live and any conversion
     * reading is void.
     *
     * Between them the three channels must separate round from every other
     * candidate, or the experiment cannot conclude anything. See
     * plans\intel-phase5-colour-conversion-experiment.md.
     */
    {
        static const v9x_u32 widths[3] = { 5ul, 6ul, 5ul };
        v9x_u32 channel;
        v9x_u32 covered = 0ul;

        for (channel = 0ul; channel < 3ul; ++channel) {
            v9x_u32 n = widths[channel];
            v9x_u32 mx = (v9x_u32)((1ul << n) - 1ul);
            v9x_u32 v = (parameters.triangle_color >> ((2ul - channel) * 8ul)) &
                        0xfful;
            v9x_u32 trunc_v = v >> (8ul - n);
            v9x_u32 round8_v = (v + (1ul << (7ul - n))) >> (8ul - n);
            v9x_u32 floor_v = (v * mx) / 255ul;
            v9x_u32 round_v = (v * mx + 127ul) / 255ul;
            v9x_u32 ceil_v = (v * mx + 254ul) / 255ul;

            if (round8_v > mx) { round8_v = mx; }
            if (round_v != trunc_v)  { covered |= 1ul; }
            if (round_v != round8_v) { covered |= 2ul; }
            if (round_v != floor_v)  { covered |= 4ul; }
            if (round_v != ceil_v)   { covered |= 8ul; }
        }
        /* All four alternatives separated somewhere across the three. */
        CHECK(covered == 15ul);
    }

    /* No repeated byte, so a channel swap is visible in the artefact. */
    CHECK(((parameters.triangle_color >> 24) & 0xfful) !=
          ((parameters.triangle_color >> 16) & 0xfful));
    CHECK(((parameters.triangle_color >> 16) & 0xfful) !=
          ((parameters.triangle_color >> 8) & 0xfful));
    CHECK(((parameters.triangle_color >> 8) & 0xfful) !=
          (parameters.triangle_color & 0xfful));
    CHECK(parameters.fill_word != V9X_I9XX_TRI_COLOR_RGB565);
}

/* The golden stream, in full. See the file header for why. */
static const v9x_u32 v9x_i9xx_phase5_golden[64] = {
    0x54300004ul, 0x03f00500ul, 0x00000000ul, 0x01e00140ul, 0x006c2000ul,
    0x08420842ul, 0x02000000ul, 0x66014140ul, 0x7d990000ul, 0x00000000ul,
    0x7d9a0000ul, 0x00000000ul, 0x7d980000ul, 0x00000000ul, 0x76fac688ul,
    0x7d810001ul, 0x00000000ul, 0x00000000ul, 0x7c800002ul, 0x7c880002ul,
    0x7d070000ul, 0x00000000ul, 0x7d8e0001ul, 0x03000500ul, 0x006c2000ul,
    0x7d850000ul, 0x00880200ul, 0x7d800003ul, 0x00000000ul, 0x00000000ul,
    0x01df027ful, 0x00000000ul, 0x7d0407c4ul, 0xfffffffful, 0x00000000ul,
    0x00902480ul, 0x00000000ul, 0x00000004ul, 0x7d050005ul, 0x190a3c00ul,
    0x00000000ul, 0x00000000ul, 0x02203ca0ul, 0x01230000ul, 0x00000000ul,
    0x00000000ul, 0x02000000ul, 0x00000000ul, 0x7f00000eul, 0x43200000ul,
    0x42f00000ul, 0x00000000ul, 0x3f800000ul, 0xff1587f9ul, 0x43f00000ul,
    0x42f00000ul, 0x00000000ul, 0x3f800000ul, 0xff1587f9ul, 0x43a00000ul,
    0x43c80000ul, 0x00000000ul, 0x3f800000ul, 0xff1587f9ul
};

static void test_golden_stream(void)
{
    v9x_u32 stream[96];
    v9x_u32 written = 0ul;
    v9x_u32 index;

    CHECK(v9x_i9xx_build_phase5_stream(stream, 96ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 64ul);
    for (index = 0ul; index < 64ul; ++index) {
        CHECK(stream[index] == v9x_i9xx_phase5_golden[index]);
    }

    /*
     * The CRC the arm gate compares, over the stream as built. Pinned as a
     * literal as well as checked against the golden array, because step 4's
     * generator must put this exact value in the mini-VDD's arm table and a
     * self-consistent pair of computations would not catch both of them
     * drifting together.
     */
    CHECK(v9x_i9xx_phase5_execution_crc() ==
          v9x_i9xx_crc32_dwords(v9x_i9xx_phase5_golden, 64ul));
    CHECK(v9x_i9xx_phase5_execution_crc() == 0x01a4de25ul);

    CHECK(v9x_i9xx_build_phase5_stream(stream, 63ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_phase5_stream(0, 96ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* --------------------------------------------------------------------- */
/* 6. The decoder: accept the golden stream, reject targeted mutations    */
/* --------------------------------------------------------------------- */

/*
 * The limits a plain Phase 5 stream runs under: its target, no texture and no
 * depth. File-scope constants because most of the decoder tests want exactly
 * this and repeating seven fields at each of them would bury what each test is
 * actually varying.
 */
static const struct v9x_i9xx_decode_limits v9x_test_plain = {
    0x006c2000ul, 0x00096000ul,
    V9X_I9XX_TARGET_PITCH, V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
    0ul, 0ul, 0ul, 0ul, V9X_I9XX_SCENE_PLAIN
};
/* The same with a zero target size, for the refusal that checks it. */
static const struct v9x_i9xx_decode_limits v9x_test_zero_bytes = {
    0x006c2000ul, 0ul,
    V9X_I9XX_TARGET_PITCH, V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
    0ul, 0ul, 0ul, 0ul, V9X_I9XX_SCENE_PLAIN
};

/* And a filler for the tests that build their own. */
/* Equal strings? The host tests avoid <string.h> for the same reason the
 * driver does: these units are built for two memory models. */
static v9x_u16 v9x_test_streq(const char *left, const char *right)
{
    if (left == 0 || right == 0) {
        return V9X_FALSE;
    }
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return (*left == *right) ? V9X_TRUE : V9X_FALSE;
}

static void v9x_test_limits(struct v9x_i9xx_decode_limits *limits,
                            v9x_u32 target_offset, v9x_u32 target_bytes,
                            v9x_u32 kind)
{
    limits->target_offset = target_offset;
    limits->target_bytes = target_bytes;
    limits->target_pitch = V9X_I9XX_TARGET_PITCH;
    limits->target_width = V9X_I9XX_TARGET_WIDTH;
    limits->target_height = V9X_I9XX_TARGET_HEIGHT;
    limits->texture_offset = 0ul;
    limits->texture_bytes = 0ul;
    limits->depth_offset = 0ul;
    limits->depth_bytes = 0ul;
    limits->kind = kind;
    /* The appended fields, zeroed so a test that does not set one gets the
     * scene answer rather than whatever was on the stack. */
    limits->texture_width = 0ul;
    limits->texture_height = 0ul;
    limits->texture_pitch = 0ul;
    limits->depth_pitch = 0ul;
    limits->depth_writes = 0ul;
    limits->texture_format = 0ul;
    limits->texture_wrap = 0ul;
    limits->texture_mag_linear = 0ul;
    limits->texture_min_linear = 0ul;
    limits->blend_src = 0ul;
    limits->blend_dst = 0ul;
    limits->texture_program = 0ul;
    limits->breadcrumb_offset = 0ul;
}

static void test_decoder_accepts_golden(void)
{
    v9x_u32 index = 0xfffffffful;

    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 64ul, &v9x_test_plain,
              &index) == V9X_I9XX_P5_OK);
    CHECK(index == 0ul);
    /* The index argument is optional. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 64ul, &v9x_test_plain,
              0) == V9X_I9XX_P5_OK);
}

/*
 * Single-dword mutations, each with the reason AND the index it must be
 * rejected at. The index matters: a reason alone cannot distinguish which of
 * the two BUF_INFO packets was wrong, which is exactly the ambiguity the
 * Phase 4 record blames for its boot count.
 */
static void test_decoder_rejects_mutations(void)
{
    static const struct {
        v9x_u32 index;
        v9x_u32 value;
        v9x_u16 reason;
        v9x_u32 rejected_at;
    } mutations[] = {
        /* The fill BLT, which the GPU now performs instead of the CPU. */
        /* The BLT's bounds are checked as a packet, so a refusal points at
         * the command dword rather than the field that was wrong. That is
         * the right granularity here: the six dwords are one statement. */
        { 4ul, 0x00000000ul, V9X_I9XX_P5_TARGET_RANGE, 0ul },
        { 5ul, 0x08420843ul, V9X_I9XX_P5_FORMAT, 5ul },
        { 3ul, 0x01e00141ul, V9X_I9XX_P5_TARGET_RANGE, 0ul },
        /* Tiled or fenced colour target. */
        { 23ul, 0x03400500ul, V9X_I9XX_P5_TILED_FORBIDDEN, 23ul },
        { 23ul, 0x03800500ul, V9X_I9XX_P5_TILED_FORBIDDEN, 23ul },
        /* Wrong colour pitch. */
        { 23ul, 0x03000200ul, V9X_I9XX_P5_PITCH, 23ul },
        /* Colour target pointing somewhere else. */
        { 24ul, 0x00000000ul, V9X_I9XX_P5_TARGET_RANGE, 24ul },
        { 24ul, 0x006c3000ul, V9X_I9XX_P5_TARGET_RANGE, 24ul },
        /*
         * A depth BUF_INFO reintroduced. The stream carries none since
         * 2026-09-15, so this mutation turns the DST_BUF_VARS packet into one
         * and checks the decoder refuses it - including at address zero,
         * which it used to accept.
         */
        { 23ul, 0x07000500ul, V9X_I9XX_P5_DEPTH_FORBIDDEN, 23ul },
        /* Wrong destination format, and the bias silently dropped. */
        { 26ul, 0x00880300ul, V9X_I9XX_P5_FORMAT, 26ul },
        { 26ul, 0x00000200ul, V9X_I9XX_P5_FORMAT, 26ul },
        /* Exclusive rather than inclusive draw rect. */
        { 30ul, 0x01e00280ul, V9X_I9XX_P5_DRAW_RECT, 30ul },
        { 28ul, 0x00000001ul, V9X_I9XX_P5_DRAW_RECT, 28ul },
        /* Scissor turned on. */
        { 18ul, 0x7c800003ul, V9X_I9XX_P5_SCISSOR_ENABLED, 18ul },
        /* Indirect state enabled rather than disabled. */
        { 21ul, 0x00000001ul, V9X_I9XX_P5_INDIRECT_FORBIDDEN, 21ul },
        /*
         * A REAL texture packet, which the decoder used to MISCLASSIFY.
         *
         * Its forbidden-opcode constants were 0x7d1d0000 and 0x7d180000 - not
         * commands - and the comparison was unmasked, so the texture branch
         * never fired. The packet was still rejected, by the unknown-opcode
         * fallback, as BAD_OPCODE: there was no acceptance hole, and these
         * cases pin the REASON rather than the refusal.
         *
         * Worth pinning anyway. A capture saying "unknown opcode at index n"
         * sends a reader looking for a corrupt stream; "a texture packet in a
         * stream that forbids them" names what happened.
         */
        { 18ul, 0x7d000003ul, V9X_I9XX_P5_TEXTURE_FORBIDDEN, 18ul },
        { 18ul, 0x7d010003ul, V9X_I9XX_P5_TEXTURE_FORBIDDEN, 18ul },
        /* A texture coordinate declared present. */
        { 33ul, 0xfffffffeul, V9X_I9XX_P5_TEXTURE_FORBIDDEN, 33ul },
        /* S4 disagreeing with the vertex dwords - the silent-hang case. */
        { 35ul, 0x009024c0ul, V9X_I9XX_P5_VERTEX_FORMAT, 35ul },
        { 35ul, 0x00902400ul, V9X_I9XX_P5_VERTEX_FORMAT, 35ul },
        /* Depth test or write enabled in S6. */
        { 37ul, 0x00080000ul, V9X_I9XX_P5_DEPTH_FORBIDDEN, 37ul },
        { 37ul, 0x00000008ul, V9X_I9XX_P5_DEPTH_FORBIDDEN, 37ul },
        /* A shader of the wrong length. */
        { 38ul, 0x7d050007ul, V9X_I9XX_P5_SHADER, 38ul },
        /* The indirect primitive form, which would fetch from a buffer. */
        { 48ul, 0x7f80000eul, V9X_I9XX_P5_INDIRECT_FORBIDDEN, 48ul },
        /* Wrong vertex count. */
        { 48ul, 0x7f000009ul, V9X_I9XX_P5_VERTEX_COUNT, 48ul },
        /* A vertex outside the drawing rectangle. */
        { 49ul, 0x44800000ul, V9X_I9XX_P5_VERTEX_RANGE, 49ul },
        /* A fractional coordinate, which the float decoder refuses. */
        { 50ul, 0x42f10000ul, V9X_I9XX_P5_VERTEX_RANGE, 50ul },
        /* Non-zero Z, and W other than one. */
        { 51ul, 0x3f800000ul, V9X_I9XX_P5_VERTEX_RANGE, 51ul },
        { 52ul, 0x40000000ul, V9X_I9XX_P5_VERTEX_RANGE, 52ul },
        /* One vertex a different colour from the other two. */
        { 58ul, 0xff286428ul, V9X_I9XX_P5_VERTEX_FORMAT, 58ul }
    };
    v9x_u32 stream[64];
    v9x_u32 index;
    v9x_u32 mutation;
    const v9x_u32 count =
        (v9x_u32)(sizeof(mutations) / sizeof(mutations[0]));

    for (mutation = 0ul; mutation < count; ++mutation) {
        v9x_u32 rejected = 0xfffffffful;
        v9x_u16 reason;
        for (index = 0ul; index < 64ul; ++index) {
            stream[index] = v9x_i9xx_phase5_golden[index];
        }
        stream[mutations[mutation].index] = mutations[mutation].value;
        reason = v9x_i9xx_decode_phase5_stream(
            stream, 64ul, &v9x_test_plain, &rejected);
        CHECK(reason == mutations[mutation].reason);
        CHECK(rejected == mutations[mutation].rejected_at);
    }
}

/*
 * The texture packets are allowed in a textured stream and refused in an
 * untextured one - and the SAME bytes decide it, so the guard is the mode and
 * not the packets.
 *
 * Built from the textured state block rather than by hand: a hand-written
 * packet would test the decoder against a second author's idea of the
 * encoding, and agreement between the two would then mean nothing about what
 * the driver submits.
 */
static void test_decoder_texture_mode(void)
{
    struct v9x_i9xx_texture texture;
    struct v9x_i9xx_sandbox_layout layout;
    struct v9x_i9xx_decode_limits textured;
    struct v9x_i9xx_decode_limits plain;
    struct v9x_i9xx_decode_limits golden_textured;
    v9x_u32 stream[256];
    v9x_u32 written = 0ul;
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index = 0ul;
    v9x_u32 map_address = 0ul;
    v9x_u32 scan;

    CHECK(v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) ==
          V9X_STATUS_OK);
    texture.offset = layout.texture_offset;
    texture.width = V9X_I9XX_TEXTURE_WIDTH;
    texture.height = V9X_I9XX_TEXTURE_HEIGHT;
    texture.pitch = layout.texture_pitch;
    texture.format = V9X_I9XX_MAPSURF_16BIT_RGB565;
    texture.wrap = 0ul;
    texture.mag_linear = 0ul;
    texture.min_linear = 0ul;

    /*
     * A complete textured stream, assembled from the same builders the scene
     * uses. The PAINT is included: the decoder requires it, because without
     * the blits the sampler reads a page holding whatever the last boot left,
     * which under a nearest filter is a picture - just not one of this
     * build's making.
     */
    CHECK(v9x_i9xx_build_texture_paint(&texture, stream + at, 256ul - at,
                                       &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_color_blt(
              layout.target_offset,
              V9X_I9XX_FILL_BLT_WIDTH, V9X_I9XX_FILL_BLT_HEIGHT,
              (v9x_u16)layout.target_pitch, V9X_I9XX_FILL_DWORD,
              layout.target_offset, layout.target_bytes,
              stream + at, 256ul - at, &produced) == V9X_STATUS_OK);
    at += produced;
    stream[at++] = V9X_I9XX_MI_FLUSH;
    CHECK(v9x_i9xx_build_textured_state(
              layout.target_offset, layout.target_pitch,
              V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT, &texture,
              stream + at, 256ul - at, &produced) == V9X_STATUS_OK);
    at += produced;
    /*
     * The MODULATE program, because that is what the surviving textured scene
     * runs. The sampling program and the TEXTURED kind remain supported by the
     * builders and the decoder - the kind is part of the decoder's contract -
     * but no scene emits them since the texture scene retired.
     */
    CHECK(v9x_i9xx_build_modulate_program(
              stream + at, 256ul - at, &produced) == V9X_STATUS_OK);
    at += produced;
    {
        struct v9x_i9xx_scene scene;
        static const v9x_u32 u_bits[3] = {
            0x00000000ul, 0x3f800000ul, 0x3f000000ul
        };
        static const v9x_u32 v_bits[3] = {
            0x00000000ul, 0x00000000ul, 0x3f800000ul
        };

        CHECK(v9x_i9xx_scene_at(1ul, &scene) == V9X_STATUS_OK);
        CHECK(v9x_i9xx_build_textured_run(
                  scene.triangles, 1ul, u_bits, v_bits,
                  V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT,
                  stream + at, 256ul - at, &produced) == V9X_STATUS_OK);
        at += produced;
    }
    written = at;

    v9x_test_limits(&textured, layout.target_offset, layout.target_bytes,
                    V9X_I9XX_SCENE_MODULATED);
    textured.texture_offset = layout.texture_offset;
    textured.texture_bytes = layout.texture_bytes;
    v9x_test_limits(&plain, layout.target_offset, layout.target_bytes,
                    V9X_I9XX_SCENE_PLAIN);
    /* The golden stream's own target, declared textured - for the refusal at
     * the end, where S2 says no coordinate set. */
    v9x_test_limits(&golden_textured, 0x006c2000ul, 0x00096000ul,
                    V9X_I9XX_SCENE_MODULATED);
    golden_textured.texture_offset = layout.texture_offset;
    golden_textured.texture_bytes = layout.texture_bytes;

    /* Textured: accepted. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              stream, written, &textured, &index) ==
          V9X_I9XX_P5_OK);

    /*
     * The very same dwords, declared PLAIN: refused at the first packet the
     * plain kind cannot account for, which is the paint - a blit into memory
     * a plain stream has no range for.
     */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &plain, &index) ==
          V9X_I9XX_P5_TARGET_RANGE);
    CHECK(index == 0ul);

    /*
     * And the texture PACKETS themselves, refused under the plain kind. The
     * state block alone, so the refusal is the MAP_STATE rather than anything
     * before it - a stream whose first offending dword is the paint would not
     * prove the packet check is there at all.
     */
    {
        v9x_u32 block[64];
        v9x_u32 block_dwords = 0ul;

        CHECK(v9x_i9xx_build_textured_state(
                  layout.target_offset, layout.target_pitch,
                  V9X_I9XX_TARGET_WIDTH, V9X_I9XX_TARGET_HEIGHT, &texture,
                  block, 64ul, &block_dwords) == V9X_STATUS_OK);
        CHECK(v9x_i9xx_decode_phase5_stream(block, block_dwords, &plain,
                                            &index) ==
              V9X_I9XX_P5_TEXTURE_FORBIDDEN);
        /* And accepted under the textured kind, up to the packets it is
         * missing - which is a different refusal, not this one. */
        CHECK(v9x_i9xx_decode_phase5_stream(block, block_dwords, &textured,
                                            &index) ==
              V9X_I9XX_P5_MISSING_PACKET);
    }

    /* Locate MAP_STATE's address dword, to mutate it. */
    for (scan = 0ul; scan < written; ++scan) {
        if ((stream[scan] & 0xffff0000ul) == V9X_I9XX_3DSTATE_MAP_STATE) {
            map_address = scan + 2ul;
        }
    }
    CHECK(map_address != 0ul);
    CHECK(stream[map_address] == layout.texture_offset);

    /*
     * A map pointing somewhere other than the reserve is refused. This is the
     * one dword in either packet that makes the GPU read memory of the
     * driver's choosing, and on this part reading a page that is not ours is
     * the access that hangs it.
     */
    stream[map_address] = layout.target_offset;
    CHECK(v9x_i9xx_decode_phase5_stream(
              stream, written, &textured, &index) ==
          V9X_I9XX_P5_TARGET_RANGE);
    CHECK(index == map_address);
    stream[map_address] = layout.texture_offset;

    /*
     * The map's FOOTPRINT, not only its base.
     *
     * MS4 carries the pitch. A pitch four times the real one leaves the
     * address untouched and every check above satisfied, while the sampler
     * reads 32 rows of 8192 bytes from a 2048-byte allocation - 126 KiB past
     * the end of the texture, into pages that are not ours. On this part
     * reading a page that is not ours is the access that hangs it.
     */
    {
        v9x_u32 ms4 = stream[map_address + 2ul];

        /* Pitch is encoded as dwords less one, so a pitch of 8192 is
         * ((8192 >> 2) - 1) in the same field. */
        stream[map_address + 2ul] = ((8192ul >> 2) - 1ul) <<
                                    V9X_I9XX_MS4_PITCH_SHIFT;
        CHECK(v9x_i9xx_decode_phase5_stream(
                  stream, written, &textured, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        CHECK(index == map_address + 2ul);
        stream[map_address + 2ul] = ms4;
    }

    /*
     * And MS3, which carries the dimensions. A height of 2048 reads past the
     * end for the same reason and by the same route.
     */
    {
        v9x_u32 ms3 = stream[map_address + 1ul];

        stream[map_address + 1ul] =
            V9X_I9XX_MAPSURF_16BIT_RGB565 |
            ((2048ul - 1ul) << V9X_I9XX_MS3_HEIGHT_SHIFT) |
            ((V9X_I9XX_TEXTURE_WIDTH - 1ul) << V9X_I9XX_MS3_WIDTH_SHIFT);
        CHECK(v9x_i9xx_decode_phase5_stream(
                  stream, written, &textured, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        CHECK(index == map_address + 1ul);
        stream[map_address + 1ul] = ms3;
    }

    /*
     * The sampler's MAP INDEX.
     *
     * SS3 names which map the sampler reads, and the two are not implicitly
     * paired - both reference emitters write the index. Pointed at map 1,
     * which this stream never declares, the sampler reads whatever map 1 held
     * from an earlier client. Nothing above notices: the packet is
     * well-formed, one unit is enabled, and MAP_STATE's own address is right.
     */
    {
        v9x_u32 sampler = 0ul;
        v9x_u32 ss3;

        for (scan = 0ul; scan < written; ++scan) {
            if ((stream[scan] & 0xffff0000ul) ==
                    V9X_I9XX_3DSTATE_SAMPLER_STATE) {
                sampler = scan;
            }
        }
        CHECK(sampler != 0ul);
        /* SS2, SS3, SS4 follow the command and the enable mask. */
        ss3 = stream[sampler + 3ul];
        stream[sampler + 3ul] = ss3 | (1ul << V9X_I9XX_SS3_MAP_INDEX_SHIFT);
        CHECK(v9x_i9xx_decode_phase5_stream(
                  stream, written, &textured, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        CHECK(index == sampler + 3ul);
        stream[sampler + 3ul] = ss3;

        /* And the filter, which the audit fixed at nearest with no mips. A
         * sampler this build never configured is state from somewhere else. */
        stream[sampler + 2ul] = 0xfffffffful;
        CHECK(v9x_i9xx_decode_phase5_stream(
                  stream, written, &textured, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        CHECK(index == sampler + 2ul);
        stream[sampler + 2ul] = V9X_I9XX_SS2_NEAREST_NO_MIP;
    }

    /*
     * And a textured stream with the texture packets REMOVED, which S2 alone
     * would not catch: without MAP_STATE the sampler reads whatever map the
     * engine last had.
     */
    {
        v9x_u32 trimmed[256];
        v9x_u32 map_start = map_address - 2ul;
        v9x_u32 packets = v9x_i9xx_map_state_extent(1ul) +
                          v9x_i9xx_sampler_state_extent(1ul);

        for (scan = 0ul; scan < map_start; ++scan) {
            trimmed[scan] = stream[scan];
        }
        for (scan = map_start + packets; scan < written; ++scan) {
            trimmed[scan - packets] = stream[scan];
        }
        CHECK(v9x_i9xx_decode_phase5_stream(
                  trimmed, written - packets, &textured, &index) ==
              V9X_I9XX_P5_MISSING_PACKET);
    }

    /*
     * The other direction: the untextured Phase 5 stream declared textured is
     * refused too, because its S2 says no coordinate set. A sampler reading
     * coordinate zero everywhere draws one flat texel over the triangle, which
     * looks like a plausible picture.
     */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 64ul, &golden_textured, &index) ==
          V9X_I9XX_P5_TEXTURE_FORBIDDEN);
}

static void test_decoder_structural_refusals(void)
{
    v9x_u32 index = 0ul;

    /* Truncation anywhere is refused, never read past. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 23ul, &v9x_test_plain,
              &index) != V9X_I9XX_P5_OK);
    /* A stream missing its target description decodes clean packet by packet
     * and must still be refused. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 22ul, &v9x_test_plain,
              &index) == V9X_I9XX_P5_MISSING_PACKET);
    CHECK(v9x_i9xx_decode_phase5_stream(
              0, 59ul, &v9x_test_plain, &index) ==
          V9X_I9XX_P5_TRUNCATED);
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 0ul, &v9x_test_plain,
              &index) == V9X_I9XX_P5_TRUNCATED);
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 66ul, &v9x_test_zero_bytes,
              &index) == V9X_I9XX_P5_TRUNCATED);
    /* A Phase 4 stream must not satisfy the Phase 5 decoder. */
    {
        static const v9x_u32 phase4[8] = {
            0x54300004ul, 0x03f00020ul, 0x00000000ul, 0x00080008ul,
            0x006c1100ul, 0x55aa33ccul, 0x02000000ul, 0x00000000ul
        };
        CHECK(v9x_i9xx_decode_phase5_stream(
                  phase4, 8ul, &v9x_test_plain, &index) !=
              V9X_I9XX_P5_OK);
    }
}

/*
 * The published packet offsets must LOCATE the packets, not merely be arithmetic
 * that looks plausible.
 *
 * intel_3d16.c derives the offsets it publishes and the vertex base it reads
 * from the builders' extents, under a comment claiming they therefore cannot
 * disagree with the stream. They did. When the fill moved to the GPU the stream
 * gained a seven-dword prefix and the derivation was not updated, so the
 * capture published fragment-program words as vertex bits and reported the
 * geometry as wrong while the stream was correct. The CRC gate could not catch
 * it: the stream was right, only the account of it was wrong.
 *
 * So this asserts the content at each offset rather than the offset's value.
 */
static void test_published_offsets_locate_the_packets(void)
{
    v9x_u32 stream[160];
    v9x_u32 written = 0ul;
    v9x_u32 fill;
    v9x_u32 state;
    v9x_u32 shader;
    v9x_u32 vertices;

    CHECK(v9x_i9xx_build_phase5_stream(stream, 160ul, &written) ==
          V9X_STATUS_OK);

    fill = v9x_i9xx_phase5_fill_extent();
    state = v9x_i9xx_3d_state_extent();
    shader = v9x_i9xx_fragment_program_extent();
    /* The fill prefix is the BLT plus one MI_FLUSH, and the flush is the last
     * dword of it. */
    CHECK(fill > 1ul);
    CHECK(stream[fill - 1ul] == V9X_I9XX_MI_FLUSH);

    /* The vertex run: the prefix, the state, the shader, two probe dwords and
     * the _3DPRIMITIVE header, after which the first vertex begins. */
    /*
     * The offset THE DRIVER PUBLISHES, not one recomputed here.
     *
     * This test summed the prefix itself and so kept passing while the
     * driver's own vertex reader published the _3DPRIMITIVE header as a
     * coordinate: two independent computations, one of them tested and the
     * other shipped. A test that derives the answer it is checking is testing
     * its own arithmetic.
     *
     * The first vertex dword is one past the primitive header.
     */
    CHECK(v9x_i9xx_phase5_primitive_offset() == fill + state + shader + 2ul +
          ((fill + state + shader + 2ul) & 1ul));
    vertices = v9x_i9xx_phase5_primitive_offset() + 1ul;
    CHECK(vertices + (V9X_I9XX_VERTEX_COUNT * V9X_I9XX_VERTEX_DWORDS) - 1ul <
          written);

    /*
     * Vertex 0 is (160,120) and vertex 1 is (480,120). Asserting the float bits
     * at the computed base is what ties the offset to the geometry: an offset
     * that drifts by any amount stops finding these.
     */
    CHECK(stream[vertices + 0ul] == 0x43200000ul);
    CHECK(stream[vertices + 1ul] == 0x42f00000ul);
    CHECK(stream[vertices + 3ul] == 0x3f800000ul);
    CHECK(stream[vertices + 4ul] == V9X_I9XX_TRI_COLOR_BGRA);
    CHECK(stream[vertices + V9X_I9XX_VERTEX_DWORDS + 0ul] == 0x43f00000ul);
    CHECK(stream[vertices + V9X_I9XX_VERTEX_DWORDS + 1ul] == 0x42f00000ul);

    /*
     * And the figure that was actually wrong on hardware. The capture from
     * 2026-09-15 published vertex bits from dword 44 when the geometry was at
     * 51; it is at 48 since the depth BUF_INFO and its two dwords left the
     * state block. Pinned as a literal as well as computed, because the
     * computation above and the driver's own published offset must not be
     * able to drift together.
     */
    CHECK(vertices == 49ul);
    /*
     * And the boundary itself, which is what OffsetVertices publishes and what
     * the executor submits to. Pinned as a literal beside the computed value:
     * the two must not be able to drift together.
     */
    CHECK(v9x_i9xx_phase5_primitive_offset() == 48ul);
    CHECK(stream[v9x_i9xx_phase5_primitive_offset()] ==
          (V9X_I9XX_3DPRIMITIVE_INLINE | V9X_I9XX_PRIM3D_TRILIST | 14ul));
    /* The pad before it is an MI_NOOP, not stale buffer content. */
    CHECK(stream[v9x_i9xx_phase5_primitive_offset() - 1ul] ==
          V9X_I9XX_MI_NOOP);
}

/*
 * The measured 8-bit-to-565 conversion.
 *
 * The two triangle colours are the whole evidential basis of the rule, so they
 * are the first cases and they assert the OBSERVED value, not a recomputation
 * of the formula. A test that re-derives the expected number from the same
 * arithmetic the function uses would pass for any formula.
 */
static void test_rgb565_round(void)
{
    /*
     * 0xfff86428, read back as 0xf325 on 6c81c52. Truncation predicted 0xfb25
     * and was wrong in red alone.
     */
    CHECK(v9x_i9xx_rgb565_round(0xf8ul, 0x64ul, 0x28ul) == 0xf325u);

    /*
     * 0xff1587f9, read back as 0x1c3e on 83f24ec. Truncation predicted 0x143f
     * and was wrong in red and blue.
     */
    CHECK(v9x_i9xx_rgb565_round(0x15ul, 0x87ul, 0xf9ul) == 0x1c3eu);

    /* And the current constant agrees with its header, which is what keeps a
     * colour change from silently leaving the expectation behind. */
    CHECK(v9x_i9xx_rgb565_round(
              (V9X_I9XX_TRI_COLOR_BGRA >> 16) & 0xfful,
              (V9X_I9XX_TRI_COLOR_BGRA >> 8) & 0xfful,
              V9X_I9XX_TRI_COLOR_BGRA & 0xfful) ==
          (v9x_u16)V9X_I9XX_TRI_COLOR_RGB565);

    /* Endpoints must be exact, or a white triangle is not white. */
    CHECK(v9x_i9xx_rgb565_round(0ul, 0ul, 0ul) == 0x0000u);
    CHECK(v9x_i9xx_rgb565_round(0xfful, 0xfful, 0xfful) == 0xffffu);

    /*
     * Where round and truncate differ by construction: the half-way point of
     * one output level. 0x04 truncates to 0 in a 5-bit channel and rounds to
     * 1 (4*31+127 = 251, /255 = 0).
     *
     * That is 0, not 1 - written out because the boundary is not where a
     * reading of "rounds up at the half-way point" would put it. The step to
     * 1 happens at 0x05 (5*31+127 = 282, /255 = 1). Asserting both sides
     * pins the boundary rather than the direction.
     */
    CHECK(v9x_i9xx_rgb565_round(0x04ul, 0ul, 0ul) == 0x0000u);
    CHECK(v9x_i9xx_rgb565_round(0x05ul, 0ul, 0ul) == 0x0800u);

    /* No channel may bleed into another: each at maximum, alone. */
    CHECK(v9x_i9xx_rgb565_round(0xfful, 0ul, 0ul) == 0xf800u);
    CHECK(v9x_i9xx_rgb565_round(0ul, 0xfful, 0ul) == 0x07e0u);
    CHECK(v9x_i9xx_rgb565_round(0ul, 0ul, 0xfful) == 0x001fu);
}

/* Scene 0's triangle, as a fixture the refusal tests can mutate freely. */
static void v9x_test_scene_triangle(struct v9x_i9xx_triangle *out)
{
    struct v9x_i9xx_scene scene;

    if (v9x_i9xx_scene_at(0ul, &scene) != V9X_STATUS_OK) {
        return;
    }
    *out = scene.triangles[0];
}

/*
 * Scene 0's stream must be byte-identical to the Phase 5 stream.
 *
 * This is the whole safety argument for the refactor. The scene machinery
 * reaches the same builders by a different route, and if it produced even one
 * different dword the regression comparison against C:\temp\intel42 - which is
 * the evidence every other scene is read against - would be worthless.
 *
 * Compared dword by dword rather than by CRC, so a failure names the offset.
 */
static void test_scene_zero_matches_phase5(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 scene_stream[160];
    v9x_u32 phase5_stream[160];
    v9x_u32 scene_written = 0ul;
    v9x_u32 phase5_written = 0ul;
    v9x_u32 index;

    CHECK(v9x_i9xx_scene_at(0ul, &scene) == V9X_STATUS_OK);
    CHECK(scene.id == 0ul);
    CHECK(scene.triangle_count == 1ul);
    CHECK(scene.triangles[0].color == V9X_I9XX_TRI_COLOR_BGRA);

    CHECK(v9x_i9xx_build_scene_stream(
              &scene, scene_stream, 160ul, &scene_written) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_phase5_stream(
              phase5_stream, 160ul, &phase5_written) == V9X_STATUS_OK);

    CHECK(scene_written == phase5_written);
    if (scene_written == phase5_written) {
        for (index = 0ul; index < scene_written; ++index) {
            CHECK(scene_stream[index] == phase5_stream[index]);
        }
    }
    CHECK(v9x_i9xx_scene_crc(0ul) == v9x_i9xx_phase5_execution_crc());

    /* The extent must agree with what was produced, or the arm table would
     * reserve the wrong number of dwords. */
    CHECK(v9x_i9xx_scene_extent(&scene) == scene_written);
}

/*
 * Is a probe's SAMPLE CENTRE strictly inside the triangle?
 *
 * HOST-SIDE ONLY. It is a property check on the scene table, not behaviour the
 * driver has any use for, and the cross products are signed 32-bit multiplies
 * - on 16-bit Watcom those are __I4M calls in the default CODE segment that a
 * near call out of I9XXCODE cannot reach. The edge predicate this replaces was
 * in the driver module first and produced exactly two E2052 relocations.
 *
 * The centre is (x + 1/2, y + 1/2), where DSTORG's half-pixel bias puts it, so
 * the centre terms are doubled to clear the halves. That scales every cross
 * product by the same factor of two, which changes no sign. Integer
 * throughout: a float would decide an exactness question by rounding.
 */
static v9x_u16 v9x_test_probe_inside(
    const struct v9x_i9xx_triangle *triangle, v9x_u16 x, v9x_u16 y)
{
    long sign = 0L;
    v9x_u32 edge;

    if (triangle == 0) {
        return V9X_FALSE;
    }
    for (edge = 0ul; edge < 3ul; ++edge) {
        v9x_u32 next = (edge + 1ul) % 3ul;
        long dx = (long)triangle->x[next] - (long)triangle->x[edge];
        long dy = (long)triangle->y[next] - (long)triangle->y[edge];
        long cx = (2L * (long)x) + 1L - (2L * (long)triangle->x[edge]);
        long cy = (2L * (long)y) + 1L - (2L * (long)triangle->y[edge]);
        long cross = (dx * cy) - (dy * cx);

        /* Exactly ON an edge is not inside. Such a probe's colour would
         * depend on the fill rule, which is a separate open question and not
         * one this scene is equipped to answer. */
        if (cross == 0L) {
            return V9X_FALSE;
        }
        if (sign == 0L) {
            sign = (cross > 0L) ? 1L : -1L;
        } else if ((cross > 0L) != (sign > 0L)) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

/*
 * Scene 1 is the modulated texture, and it is scene 0's triangle.
 *
 * Identical geometry is the point: the rasteriser contributes nothing new, so
 * anything scene 1 shows that scene 0 does not is the texture path and the
 * multiply. It carries both since the sampling-only scene retired.
 */
static void test_scene_one_is_modulated(void)
{
    struct v9x_i9xx_scene zero;
    struct v9x_i9xx_scene one;
    v9x_u32 stream[200];
    v9x_u32 written = 0ul;
    v9x_u32 vertex;
    v9x_u32 primitive;

    CHECK(v9x_i9xx_scene_at(0ul, &zero) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_at(1ul, &one) == V9X_STATUS_OK);

    /* Id 6, not 1 or 5: 1 through 4 were the colour and edge scenes and 5 the
     * sampling-only texture scene, all retired and none reused. */
    CHECK(one.id == 6ul);
    CHECK(one.kind == V9X_I9XX_SCENE_MODULATED);
    CHECK(v9x_i9xx_scene_kind_textured(one.kind) == V9X_TRUE);
    CHECK(v9x_i9xx_scene_kind_textured(zero.kind) == V9X_FALSE);

    CHECK(one.triangle_count == zero.triangle_count);
    for (vertex = 0ul; vertex < 3ul; ++vertex) {
        CHECK(one.triangles[0].x[vertex] == zero.triangles[0].x[vertex]);
        CHECK(one.triangles[0].y[vertex] == zero.triangles[0].y[vertex]);
    }

    /*
     * The vertex colour is HALF intensity, and is neither scene 0's colour nor
     * the fill. Half so that every product differs visibly from the texel it
     * came from - the check that the multiply happened is that no probe reads
     * its raw quadrant colour, and a colour near white would multiply to
     * within a level or two of the texel and make that check worthless.
     */
    CHECK(one.triangles[0].color == V9X_I9XX_TEX_MODULATE_COLOR_BGRA);
    CHECK(one.triangles[0].color != zero.triangles[0].color);
    CHECK(one.triangles[0].color_measured == V9X_FALSE);

    CHECK(v9x_i9xx_build_scene_stream(&one, stream, 200ul, &written) ==
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_extent(&one) == written);
    CHECK((written & 1ul) == 0ul);

    /*
     * The paint comes FIRST, before the fill and before any state. Its last
     * dword is the MI_FLUSH that gets the texels out of the render cache; a
     * sampler reading a texture the blits had not reached would return
     * whatever the page held and look like an addressing fault.
     */
    CHECK(stream[0] == V9X_I9XX_XY_COLOR_BLT);
    CHECK(stream[v9x_i9xx_texture_paint_extent() - 1ul] == V9X_I9XX_MI_FLUSH);

    /* The primitive is where the executor's published boundary says, it is
     * qword-aligned, and it carries SEVEN dwords per vertex. */
    primitive = v9x_i9xx_scene_primitive_offset(&one);
    CHECK((primitive & 1ul) == 0ul);
    CHECK(stream[primitive] == (V9X_I9XX_3DPRIMITIVE_INLINE |
                                V9X_I9XX_PRIM3D_TRILIST | 20ul));
    CHECK(written - primitive == v9x_i9xx_textured_run_dwords(1ul));

    /* Scene 0's stream is still the untextured one, five dwords per vertex.
     * Asserted because every change here runs through a shared builder. */
    CHECK(v9x_i9xx_build_scene_stream(&zero, stream, 200ul, &written) ==
          V9X_STATUS_OK);
    primitive = v9x_i9xx_scene_primitive_offset(&zero);
    CHECK(stream[primitive] == (V9X_I9XX_3DPRIMITIVE_INLINE |
                                V9X_I9XX_PRIM3D_TRILIST | 14ul));
}

/*
 * The emitted texture coordinates are the affine map of the triangle's own
 * bounding box onto 0..1, and each quadrant probe sits in the quadrant it
 * claims.
 *
 * Recomputed from the STREAM, not from the table: the coordinates the scene
 * table never sees are the ones the GPU reads, and a probe list that agreed
 * with a comment while the emitter disagreed with both is exactly the defect
 * this project keeps finding.
 *
 * Which TEXEL a given (u, v) reads is the open question - that is what the
 * hardware run answers. What is established here is only that the four probes
 * fall one per quarter of coordinate space, so that whatever the answer is,
 * four different quadrant colours can distinguish it.
 */
static void test_texture_probe_quadrants(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 stream[200];
    v9x_u32 written = 0ul;
    v9x_u32 primitive;
    v9x_u32 vertex;
    v9x_u32 probe;
    v9x_u32 min_x;
    v9x_u32 max_x;
    v9x_u32 min_y;
    v9x_u32 max_y;
    v9x_u32 seen = 0ul;
    /* 0, 0.5 and 1 as IEEE-754 bit patterns, indexed in halves. The emitter
     * writes bit patterns because the driver's converter takes integers and
     * cannot express a half. */
    static const v9x_u32 half_bits[3] = {
        0x00000000ul, 0x3f000000ul, 0x3f800000ul
    };

    CHECK(v9x_i9xx_scene_at(1ul, &scene) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 200ul, &written) ==
          V9X_STATUS_OK);
    primitive = v9x_i9xx_scene_primitive_offset(&scene);

    min_x = scene.triangles[0].x[0];
    max_x = scene.triangles[0].x[0];
    min_y = scene.triangles[0].y[0];
    max_y = scene.triangles[0].y[0];
    for (vertex = 1ul; vertex < 3ul; ++vertex) {
        if (scene.triangles[0].x[vertex] < min_x) {
            min_x = scene.triangles[0].x[vertex];
        }
        if (scene.triangles[0].x[vertex] > max_x) {
            max_x = scene.triangles[0].x[vertex];
        }
        if (scene.triangles[0].y[vertex] < min_y) {
            min_y = scene.triangles[0].y[vertex];
        }
        if (scene.triangles[0].y[vertex] > max_y) {
            max_y = scene.triangles[0].y[vertex];
        }
    }
    CHECK(max_x > min_x);
    CHECK(max_y > min_y);

    /*
     * Each vertex's u and v are its position in that box. Every vertex of this
     * triangle lands on 0, a half or 1 in both axes, so the check is exact:
     * (p - min) * 2 must divide the span, and the quotient indexes half_bits.
     */
    for (vertex = 0ul; vertex < 3ul; ++vertex) {
        v9x_u32 at = primitive + 1ul +
                     (vertex * V9X_I9XX_TEXTURED_VERTEX_DWORDS);
        v9x_u32 span_x = max_x - min_x;
        v9x_u32 span_y = max_y - min_y;
        v9x_u32 halves_x = (scene.triangles[0].x[vertex] - min_x) * 2ul;
        v9x_u32 halves_y = (scene.triangles[0].y[vertex] - min_y) * 2ul;

        CHECK((halves_x % span_x) == 0ul);
        CHECK((halves_y % span_y) == 0ul);
        /* Coordinates come AFTER the colour: dwords 5 and 6 of the seven. */
        CHECK(stream[at + 5ul] == half_bits[halves_x / span_x]);
        CHECK(stream[at + 6ul] == half_bits[halves_y / span_y]);
    }

    for (probe = 0ul; probe < scene.probe_count; ++probe) {
        v9x_u32 x = (v9x_u32)scene.probes[probe].x;
        v9x_u32 y = (v9x_u32)scene.probes[probe].y;
        v9x_u16 inside = v9x_test_probe_inside(&scene.triangles[0],
                                               scene.probes[probe].x,
                                               scene.probes[probe].y);

        if (scene.probes[probe].expect == V9X_I9XX_PROBE_FILL) {
            /* Outside the triangle, or it is not reading the fill. */
            CHECK(inside == V9X_FALSE);
            continue;
        }

        /*
         * A quadrant probe must be INSIDE, and its quadrant follows from the
         * affine map: u >= 1/2 when the doubled centre offset reaches the
         * span. All integer, all exact.
         */
        CHECK(inside == V9X_TRUE);
        {
            v9x_u32 right = (((2ul * x) + 1ul - (2ul * min_x)) >=
                             (max_x - min_x)) ? 1ul : 0ul;
            v9x_u32 lower = (((2ul * y) + 1ul - (2ul * min_y)) >=
                             (max_y - min_y)) ? 1ul : 0ul;
            v9x_u32 quadrant = (lower * 2ul) + right;

            /* MODQUAD, not QUADRANT: the surviving textured scene modulates,
             * and its expectations say so - the validator treats the two
             * differently and a probe carrying the wrong one would be judged
             * against a raw texel. */
            CHECK(scene.probes[probe].expect ==
                  (v9x_u16)(V9X_I9XX_PROBE_MODQUAD0 + quadrant));
            /* Each quadrant claimed once, so four probes cannot all be
             * asserting the same one. */
            CHECK((seen & (1ul << quadrant)) == 0ul);
            seen |= (1ul << quadrant);
        }
    }
    CHECK(seen == 0x0ful);

    /* The predicate's own refusals, and a point far outside. */
    CHECK(v9x_test_probe_inside(0, 250u, 200u) == V9X_FALSE);
    CHECK(v9x_test_probe_inside(&scene.triangles[0], 0u, 0u) == V9X_FALSE);
}

/*
 * The aperture-read budget. Bulk reads hang this part - three succeeded and
 * 153,600 hung - so the probe total is a number the build states rather than
 * a consequence nobody counted.
 */
static void test_scene_probe_budget(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 index;
    v9x_u32 total = 0ul;

    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        CHECK(scene.probe_count >= 1ul);
        CHECK(scene.probe_count <= V9X_I9XX_SCENE_MAX_PROBES);
        total += scene.probe_count;
    }
    CHECK(v9x_i9xx_scene_total_probes() == total);

    /*
     * 14 for the Phase 5 regression, 6 each for modulate, depth and alpha,
     * and 5 for the blend scene - which has two triangles rather than three
     * and so one region fewer to sample.
     *
     * Written out because the budget rule is that no boot roughly doubles the
     * last one that completed. Changing the scene table past this without
     * re-reading that rule should fail here.
     */
    CHECK(total == 37ul);

    /*
     * And every scene's stream together must fit the driver's accumulator,
     * which is 512 dwords. The five scenes take 494, so the headroom is 18 -
     * thin enough that the next scene added without checking would overflow
     * it. The driver refuses cleanly in that case rather than hanging, but a
     * boot spent producing ACCUMULATOR-FULL is still a boot; failing here
     * costs nothing.
     */
    {
        v9x_u32 staged = 0ul;

        for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
            CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
            staged += v9x_i9xx_scene_extent(&scene);
        }
        /*
         * Printed by the stream generator, not derived here: the sum is what
         * the five scenes actually stage, and a test that recomputed it from
         * the same builders would be comparing a number with itself.
         */
        CHECK(staged == V9X_TEST_STAGED_DWORDS);
        CHECK(staged <= 512ul);
    }
}

/* The table itself, and its refusals. */
static void test_scene_table(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 index;
    v9x_u32 stream[160];
    v9x_u32 written = 0ul;

    /*
     * Five scenes against five authorised. Both asserted, and their
     * RELATIONSHIP asserted, because the failure this guards against is a
     * build quietly defining more draws than were agreed - which the count
     * alone would not show. The bound is now REACHED, so a sixth scene fails
     * here rather than on the machine, which is what the separate constants
     * are for.
     */
    CHECK(v9x_i9xx_scene_count() == 5ul);
    CHECK(v9x_i9xx_scene_authorised_draws() == 5ul);
    CHECK(v9x_i9xx_scene_count() <= v9x_i9xx_scene_authorised_draws());
    CHECK(v9x_i9xx_scene_count() != 0ul);

    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        struct v9x_i9xx_scene other;
        v9x_u32 compare;

        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        CHECK(scene.triangle_count >= 1ul);
        CHECK(scene.triangle_count <= V9X_I9XX_SCENE_MAX_TRIANGLES);
        CHECK(scene.fill_dword == V9X_I9XX_FILL_DWORD);
        /*
         * Ids must be unique. The capture attributes probe sets by id, so a
         * duplicate would silently merge two scenes' evidence.
         */
        for (compare = 0ul; compare < index; ++compare) {
            CHECK(v9x_i9xx_scene_at(compare, &other) == V9X_STATUS_OK);
            CHECK(other.id != scene.id);
        }
        /*
         * Every scene must build. A zero CRC is what no arm path accepts, so
         * without this the failure would surface on the machine instead of
         * here.
         */
        CHECK(v9x_i9xx_scene_crc(index) != 0ul);
    }

    /* Out of range refuses AND clears, rather than leaving the caller's stack
     * contents looking like a scene. */
    CHECK(v9x_i9xx_scene_at(5ul, &scene) != V9X_STATUS_OK);
    CHECK(scene.triangle_count == 0ul);
    CHECK(scene.id == 0ul);
    CHECK(scene.kind == V9X_I9XX_SCENE_PLAIN);
    CHECK(v9x_i9xx_scene_crc(5ul) == 0ul);
    CHECK(v9x_i9xx_scene_at(0ul, 0) != V9X_STATUS_OK);

    /* Capacity, at the boundary rather than far from it. */
    CHECK(v9x_i9xx_scene_at(0ul, &scene) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 63ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 64ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 64ul);

    /* A scene claiming more triangles than it can hold is refused, not
     * clamped. */
    scene.triangle_count = V9X_I9XX_SCENE_MAX_TRIANGLES + 1ul;
    CHECK(v9x_i9xx_scene_extent(&scene) == 0ul);
    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 160ul, &written) !=
          V9X_STATUS_OK);
    scene.triangle_count = 0ul;
    CHECK(v9x_i9xx_scene_extent(&scene) == 0ul);
}

/*
 * EVERY scene decodes under the allowlist, textured or not.
 *
 * The driver runs this check before staging each scene. Asserted here too
 * because the driver's copy only fires on the machine, and a scene that the
 * decoder refuses is a boot spent producing "DECODE-REFUSED" and nothing else.
 */
static void test_every_scene_decodes(void)
{
    struct v9x_i9xx_scene scene;
    struct v9x_i9xx_sandbox_layout layout;
    v9x_u32 stream[200];
    v9x_u32 written = 0ul;
    v9x_u32 rejected = 0ul;
    v9x_u32 index;
    v9x_u32 other;

    CHECK(v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) ==
          V9X_STATUS_OK);

    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        struct v9x_i9xx_decode_limits limits;

        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 200ul, &written) ==
              V9X_STATUS_OK);

        v9x_test_limits(&limits, layout.target_offset, layout.target_bytes,
                        scene.kind);
        if (v9x_i9xx_scene_kind_textured(scene.kind) != V9X_FALSE) {
            limits.texture_offset = layout.texture_offset;
            limits.texture_bytes = layout.texture_bytes;
        }
        if (v9x_i9xx_scene_kind_depth(scene.kind) != V9X_FALSE) {
            limits.depth_offset = layout.depth_offset;
            limits.depth_bytes = layout.depth_bytes;
        }
        CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits,
                                            &rejected) == V9X_I9XX_P5_OK);

        /*
         * And refused under EVERY other kind. Without this the decoder could
         * be ignoring the kind entirely and every assertion above would still
         * hold - which is exactly what a mode check that is never exercised
         * negatively looks like.
         *
         * The ranges stay as this scene's, so what differs between the
         * accepted call and each refused one is the kind alone.
         */
        for (other = 0ul; other <= V9X_I9XX_SCENE_DEPTH_WRITE; ++other) {
            struct v9x_i9xx_decode_limits wrong = limits;

            if (other == scene.kind) {
                continue;
            }
            wrong.kind = other;
            CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &wrong,
                                                &rejected) !=
                  V9X_I9XX_P5_OK);
        }
    }
}

/*
 * The two flush forms the decoder accepts, and one it must not.
 *
 * intel76: the runtime batch now leads with MI_FLUSH bit 0 (MI_READ_FLUSH),
 * which invalidates the map cache. The decoder accepts exactly that form and
 * the bare flush; a flush that inhibits the render-cache write (bit 2) is
 * refused, because a batch ending in one would flip unfinished pixels.
 */
static void test_decoder_flush_forms(void)
{
    struct v9x_i9xx_scene scene;
    struct v9x_i9xx_sandbox_layout layout;
    struct v9x_i9xx_decode_limits limits;
    v9x_u32 stream[200];
    v9x_u32 written = 0ul;
    v9x_u32 index = 0ul;
    v9x_u32 flush = 0ul;

    CHECK(v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) ==
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_at(2ul, &scene) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 200ul, &written) ==
          V9X_STATUS_OK);
    v9x_test_limits(&limits, layout.target_offset, layout.target_bytes,
                    scene.kind);
    limits.depth_offset = layout.depth_offset;
    limits.depth_bytes = layout.depth_bytes;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);

    while (flush < written && stream[flush] != V9X_I9XX_MI_FLUSH) {
        ++flush;
    }
    CHECK(flush < written);

    stream[flush] = V9X_I9XX_MI_FLUSH_READ;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);

    stream[flush] = V9X_I9XX_MI_FLUSH | 0x4ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    CHECK(index == flush);
    stream[flush] = V9X_I9XX_MI_FLUSH;
}

/*
 * The breadcrumb store: accepted only where the limits license it, only to
 * that address, and never in a stream with no licence.
 */
static void test_decoder_breadcrumb(void)
{
    struct v9x_i9xx_scene scene;
    struct v9x_i9xx_sandbox_layout layout;
    struct v9x_i9xx_decode_limits limits;
    v9x_u32 stream[200];
    v9x_u32 written = 0ul;
    v9x_u32 index = 0ul;
    v9x_u32 flush = 0ul;

    CHECK(v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) ==
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_at(2ul, &scene) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 200ul, &written) ==
          V9X_STATUS_OK);
    v9x_test_limits(&limits, layout.target_offset, layout.target_bytes,
                    scene.kind);
    limits.depth_offset = layout.depth_offset;
    limits.depth_bytes = layout.depth_bytes;

    /* Licensed but ABSENT: a submit would wait for a store that is not
     * there. Refused at the end of the stream. */
    limits.breadcrumb_offset = V9X_I9XX_HWS_BREADCRUMB_BYTE;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_BREADCRUMB);
    CHECK(index == written);
    limits.breadcrumb_offset = 0ul;

    /* The fill goes after the drawing, where the runtime puts it: an
     * MI_FLUSH, then the six-dword one-pixel XY_COLOR_BLT the builder
     * makes, then a NOOP pad. Destination: the status page's dword. */
    flush = written;
    stream[flush] = V9X_I9XX_MI_FLUSH;
    {
        v9x_u32 count = 0ul;

        CHECK(v9x_i9xx_build_breadcrumb_stream(0x006c0080ul, 0x42ul,
                                               stream + flush + 1ul, 6ul,
                                               &count) == V9X_STATUS_OK);
        CHECK(count == 6ul);
    }
    stream[flush + 7ul] = V9X_I9XX_MI_NOOP;
    written = flush + 8ul;

    /* No licence: the fill is then an ordinary one, bounded by the
     * target, and the status page is not in the target. Refused. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);

    /* Licensed to that destination: accepted. */
    limits.breadcrumb_offset = 0x006c0080ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);

    /* Licensed to another destination: the fill is unlicensed again. */
    limits.breadcrumb_offset = 0x006c0084ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    limits.breadcrumb_offset = 0x006c0080ul;

    /* Truncated inside the packet: refused, not read past the end. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, flush + 4ul, &limits,
                                        &index) != V9X_I9XX_P5_OK);

    /* Not behind a flush: the mark would not cover the drawing. */
    stream[flush] = V9X_I9XX_MI_NOOP;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_BREADCRUMB);
    CHECK(index == flush + 1ul);
    stream[flush] = V9X_I9XX_MI_FLUSH;

    /* Not one pixel: a two-pixel fill at that address is refused. */
    stream[flush + 4ul] = (1ul << 16) | 2ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_BREADCRUMB);
    CHECK(index == flush + 1ul);
    stream[flush + 4ul] = (1ul << 16) | 1ul;

    /* Rendering after the fill: anything but a NOOP behind it. */
    stream[flush + 7ul] = V9X_I9XX_MI_FLUSH;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_BREADCRUMB);
    CHECK(index == flush + 7ul);
    stream[flush + 7ul] = V9X_I9XX_MI_NOOP;

    /* Two breadcrumbs: refused at the second. */
    stream[flush + 7ul] = V9X_I9XX_MI_FLUSH;
    {
        v9x_u32 count = 0ul;

        CHECK(v9x_i9xx_build_breadcrumb_stream(0x006c0080ul, 0x43ul,
                                               stream + flush + 8ul, 6ul,
                                               &count) == V9X_STATUS_OK);
    }
    CHECK(v9x_i9xx_decode_phase5_stream(stream, flush + 14ul, &limits,
                                        &index) == V9X_I9XX_P5_BREADCRUMB);
    stream[flush + 7ul] = V9X_I9XX_MI_NOOP;

    /* Neither MI store form is a command this decoder knows any more. */
    stream[flush + 1ul] = V9X_I9XX_MI_STORE_DWORD_INDEX;
    stream[flush + 2ul] = V9X_I9XX_HWS_BREADCRUMB_BYTE;
    stream[flush + 3ul] = 0x42ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    stream[flush + 1ul] = V9X_I9XX_MI_STORE_DWORD_IMM;
    stream[flush + 2ul] = 0ul;
    stream[flush + 3ul] = 0x006c0800ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);

    /* The builder's own refusals: unaligned, first page, no room. */
    {
        v9x_u32 only[6];
        v9x_u32 count = 0ul;

        CHECK(v9x_i9xx_build_breadcrumb_stream(0x006c0082ul, 1ul, only, 6ul,
                                               &count) != V9X_STATUS_OK);
        CHECK(v9x_i9xx_build_breadcrumb_stream(0x80ul, 1ul, only, 6ul,
                                               &count) != V9X_STATUS_OK);
        CHECK(v9x_i9xx_build_breadcrumb_stream(0x006c0080ul, 1ul, only, 5ul,
                                               &count) != V9X_STATUS_OK);
        CHECK(v9x_i9xx_build_breadcrumb_stream(0x006c0080ul, 0x600d0001ul,
                                               only, 6ul, &count) ==
              V9X_STATUS_OK);
        CHECK(only[0] == V9X_I9XX_XY_COLOR_BLT);
        CHECK(only[3] == ((1ul << 16) | 1ul));
        CHECK(only[4] == 0x006c0080ul);
        CHECK(only[5] == 0x600d0001ul);
    }

    /* And the clear helper zeroes the appended fields too. */
    limits.breadcrumb_offset = 7ul;
    limits.texture_program = 7ul;
    v9x_i9xx_decode_limits_clear(&limits);
    CHECK(limits.breadcrumb_offset == 0ul);
    CHECK(limits.texture_program == 0ul);
    CHECK(limits.target_offset == 0ul);
}

/*
 * What a DEPTH stream may not do, and the decoder must refuse.
 *
 * Every mutation here passed the decoder when it was written. Each is the same
 * defect in a different place: a rule the BUILDER enforces and the decoder,
 * which is meant to be the independent opinion, did not.
 */
static void test_decoder_depth_refusals(void)
{
    struct v9x_i9xx_scene scene;
    struct v9x_i9xx_sandbox_layout layout;
    struct v9x_i9xx_decode_limits limits;
    v9x_u32 stream[200];
    v9x_u32 written = 0ul;
    v9x_u32 index = 0ul;
    v9x_u32 primitive;
    v9x_u32 scan;
    v9x_u32 saved;

    CHECK(v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) ==
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_at(2ul, &scene) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_kind_depth(scene.kind) == V9X_TRUE);
    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 200ul, &written) ==
          V9X_STATUS_OK);

    v9x_test_limits(&limits, layout.target_offset, layout.target_bytes,
                    scene.kind);
    limits.depth_offset = layout.depth_offset;
    limits.depth_bytes = layout.depth_bytes;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);

    primitive = v9x_i9xx_scene_primitive_offset(&scene);

    /*
     * A vertex BELOW the depth buffer.
     *
     * The depth buffer is 256 rows where the render target is 480, so a vertex
     * at y = 400 is inside the drawing rectangle and outside the depth
     * allocation - the hardware would address depth memory past the end of it,
     * and the first thing past the end is the guard page. The builder refused
     * this; the decoder checked Y against the TARGET's height and accepted it.
     */
    saved = stream[primitive + 2ul];
    stream[primitive + 2ul] = 0x43c80000ul;   /* 400.0f */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_VERTEX_RANGE);
    CHECK(index == primitive + 2ul);
    stream[primitive + 2ul] = saved;

    /* And the row immediately below the buffer, which is the boundary the
     * off-by-one lives at. 256.0f. */
    stream[primitive + 2ul] = 0x43800000ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_VERTEX_RANGE);
    stream[primitive + 2ul] = saved;

    /*
     * A PARTIAL clear.
     *
     * The blit still lands inside the depth range, still carries the far
     * value, and still sits before the draw - so every check the decoder had
     * was satisfied while 255 of the buffer's 256 rows kept whatever the
     * previous scene left in them. The triangles would then test against
     * memory nobody cleared, which is the exact condition the clear exists to
     * remove.
     *
     * Three ways to shrink it, because the rectangle has three fields and any
     * one of them can do it.
     */
    saved = stream[3];
    /* One row instead of 256. */
    stream[3] = (1ul << 16) | (V9X_I9XX_DEPTH_PITCH / 4ul);
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_TARGET_RANGE);
    CHECK(index == 3ul);
    /* Full height, half the width. */
    stream[3] = (V9X_I9XX_DEPTH_HEIGHT << 16) | (V9X_I9XX_DEPTH_PITCH / 8ul);
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_TARGET_RANGE);
    stream[3] = saved;

    /* And started one row down, which covers the right AREA and the wrong
     * rows - the last row of the buffer stays as it was. */
    saved = stream[4];
    stream[4] = layout.depth_offset + V9X_I9XX_DEPTH_PITCH;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    stream[4] = saved;

    /* And at a pitch that is not the buffer's, which walks a different grid
     * over the same bytes. */
    saved = stream[1];
    stream[1] = V9X_I9XX_BLT_DEPTH_32 | V9X_I9XX_BLT_ROP_PATCOPY |
                (V9X_I9XX_DEPTH_PITCH / 2ul);
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    stream[1] = saved;

    /* Unmutated, it still decodes. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);

    /*
     * A depth scene with no CLEAR. The decoder checked the clear's colour
     * where it found one and never required one, so a stream that simply
     * omitted it passed - and its result would depend on whatever the depth
     * buffer held from the previous scene or the previous boot.
     */
    CHECK(stream[0] == V9X_I9XX_XY_COLOR_BLT);
    for (scan = 0ul; scan < 6ul; ++scan) {
        stream[scan] = V9X_I9XX_MI_NOOP;
    }
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_MISSING_PACKET);

    CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 200ul, &written) ==
          V9X_STATUS_OK);

    /*
     * And a clear that happens AFTER the draw, which clears the evidence
     * rather than preparing for it. Present, complete, correctly addressed,
     * and useless.
     */
    {
        v9x_u32 moved[200];
        v9x_u32 at = 0ul;

        for (scan = 6ul; scan < written; ++scan) {
            moved[at++] = stream[scan];
        }
        for (scan = 0ul; scan < 6ul; ++scan) {
            moved[at++] = stream[scan];
        }
        CHECK(at == written);
        CHECK(v9x_i9xx_decode_phase5_stream(moved, written, &limits,
                                            &index) != V9X_I9XX_P5_OK);
    }

    /*
     * The TEXTURE paint has the same ordering hole, and the same argument: a
     * paint after the draw samples whatever the page held.
     */
    {
        v9x_u32 moved[200];
        v9x_u32 paint = v9x_i9xx_texture_paint_extent();
        v9x_u32 at = 0ul;
        struct v9x_i9xx_decode_limits textured;

        CHECK(v9x_i9xx_scene_at(1ul, &scene) == V9X_STATUS_OK);
        CHECK(v9x_i9xx_build_scene_stream(&scene, stream, 200ul, &written) ==
              V9X_STATUS_OK);
        v9x_test_limits(&textured, layout.target_offset, layout.target_bytes,
                        scene.kind);
        textured.texture_offset = layout.texture_offset;
        textured.texture_bytes = layout.texture_bytes;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &textured,
                                            &index) == V9X_I9XX_P5_OK);

        for (scan = paint; scan < written; ++scan) {
            moved[at++] = stream[scan];
        }
        for (scan = 0ul; scan < paint; ++scan) {
            moved[at++] = stream[scan];
        }
        CHECK(at == written);
        CHECK(v9x_i9xx_decode_phase5_stream(moved, written, &textured,
                                            &index) != V9X_I9XX_P5_OK);

        /*
         * And a PARTIAL paint, which is the depth clear's defect in the other
         * buffer: four blits inside the texture range satisfied every check
         * while covering one quadrant between them, leaving three holding
         * whatever the page held. Under a nearest filter that is a picture,
         * just not one this build made.
         */
        saved = stream[4ul];
        stream[4ul] = stream[10ul];   /* two blits at quadrant 1 */
        CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &textured,
                                            &index) != V9X_I9XX_P5_OK);
        stream[4ul] = saved;

        /* A quadrant blit shrunk to a single row. */
        saved = stream[3ul];
        stream[3ul] = (1ul << 16) | (V9X_I9XX_TEXTURE_BLOCK / 2ul);
        CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &textured,
                                            &index) ==
              V9X_I9XX_P5_TARGET_RANGE);
        CHECK(index == 3ul);
        stream[3ul] = saved;

        /* Unmutated, it still decodes. */
        CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &textured,
                                            &index) == V9X_I9XX_P5_OK);
    }
}

/*
 * The depth scene's probe expectations, recomputed from the geometry.
 *
 * Every expectation in the table is a claim about which of three overlapping
 * triangles owns a pixel, and those were placed by hand. This derives them
 * independently: which triangles cover the probe, and which of those wins
 * under the scene's rule - the nearest, because the scene writes depth.
 *
 * The companion scene that tested without writing retired after intel46
 * measured the contrast between them. What survives here is the rule, which
 * is still derived rather than written out twice.
 */
static void test_depth_scene_expectations(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 probe;
    v9x_u32 triple = 0ul;
    /* The depths the builder emits, in submission order: 0.75, 0.25, 0.90.
     * Compared as bit patterns, which for positive floats orders exactly as
     * the values do - and the scene depends on nothing but that order. */
    static const v9x_u32 z_bits[3] = {
        0x3f400000ul, 0x3e800000ul, 0x3f666666ul
    };

    CHECK(v9x_i9xx_scene_at(2ul, &scene) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_kind_depth(scene.kind) == V9X_TRUE);
    CHECK(v9x_i9xx_scene_kind_depth_writes(scene.kind) == V9X_TRUE);
    CHECK(scene.triangle_count == 3ul);

    /* The three colours must differ, or "which triangle won" is not a
     * question the capture can answer. */
    CHECK(scene.triangles[0].color != scene.triangles[1].color);
    CHECK(scene.triangles[0].color != scene.triangles[2].color);
    CHECK(scene.triangles[1].color != scene.triangles[2].color);

    for (probe = 0ul; probe < scene.probe_count; ++probe) {
        v9x_u32 triangle;
        v9x_u32 winner = 3ul;
        v9x_u32 covering = 0ul;
        v9x_u16 expect;

        for (triangle = 0ul; triangle < 3ul; ++triangle) {
            if (v9x_test_probe_inside(&scene.triangles[triangle],
                                      scene.probes[probe].x,
                                      scene.probes[probe].y) != V9X_TRUE) {
                continue;
            }
            ++covering;
            /* Nearest wins: the first two record their depths and the third
             * is tested against what they wrote. */
            if (winner == 3ul || z_bits[triangle] < z_bits[winner]) {
                winner = triangle;
            }
        }
        if (covering == 3ul) { ++triple; }

        expect = (winner == 3ul)
                     ? V9X_I9XX_PROBE_FILL
                     : (v9x_u16)(V9X_I9XX_PROBE_TRIANGLE0 + winner);
        CHECK(scene.probes[probe].expect == expect);
    }
    /* At least one probe under all three, or the rejection this scene exists
     * to show is never asked for. */
    CHECK(triple >= 1ul);
}

/*
 * The GOURAUD scene: one triangle, three colours, and what a first
 * measurement of an interpolator is allowed to assert.
 *
 * It replaced the alpha-test scene on 2026-09-17, when the draw bound was
 * five and the table was full. What is asserted here is everything a build
 * can know without the hardware - the geometry, the colours, which probes
 * are measurements and which are requirements - and nothing about the values
 * the interpolator will produce, because that is the question.
 */
static void test_gouraud_scene_expectations(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 probe;
    v9x_u32 measured = 0ul;
    v9x_u32 asserted = 0ul;

    CHECK(v9x_i9xx_scene_at(3ul, &scene) == V9X_STATUS_OK);
    CHECK(scene.kind == V9X_I9XX_SCENE_GOURAUD);
    CHECK(scene.triangle_count == 1ul);

    /* Three colours, actually carried per vertex rather than one repeated -
     * which is the entire difference between this scene and every other one
     * this driver draws. */
    CHECK(scene.triangles[0].per_vertex != V9X_FALSE);
    CHECK(scene.triangles[0].vertex_color[0] != scene.triangles[0].vertex_color[1]);
    CHECK(scene.triangles[0].vertex_color[0] != scene.triangles[0].vertex_color[2]);
    CHECK(scene.triangles[0].vertex_color[1] != scene.triangles[0].vertex_color[2]);

    /*
     * A PRIMARY at each corner, one channel each, and the same alpha on all
     * three. A shared channel between two corners would make a probe reading
     * it unable to say which vertex it came from, which is the whole reading.
     */
    CHECK(scene.triangles[0].vertex_color[0] == V9X_I9XX_GOURAUD_COLOR_A);
    CHECK(scene.triangles[0].vertex_color[1] == V9X_I9XX_GOURAUD_COLOR_B);
    CHECK(scene.triangles[0].vertex_color[2] == V9X_I9XX_GOURAUD_COLOR_C);
    CHECK((scene.triangles[0].vertex_color[0] >> 24) ==
          (scene.triangles[0].vertex_color[1] >> 24));
    CHECK((scene.triangles[0].vertex_color[1] >> 24) ==
          (scene.triangles[0].vertex_color[2] >> 24));
    /*
     * And WHICH primary, in the layout the 565 test above pins for the
     * measured colour: red in bits 23..16, blue in 7..0. The first cut had
     * these two swapped, which would have read a correct interpolator as a
     * reversed one - and no host test noticed, because none named the
     * channel.
     */
    CHECK((scene.triangles[0].vertex_color[0] & 0x00fffffful) == 0x00ff0000ul);
    CHECK((scene.triangles[0].vertex_color[1] & 0x00fffffful) == 0x0000ff00ul);
    CHECK((scene.triangles[0].vertex_color[2] & 0x00fffffful) == 0x000000fful);

    /* Nothing about the interpolated result is claimed as known. */
    CHECK(scene.triangles[0].color_measured == V9X_FALSE);

    /*
     * The probe set: DRAWN inside, fill outside, and BOTH kinds present.
     * All-measure would pass on a boot that drew nothing - the exterior
     * probes read the fill either way - and all-assert would require
     * values nobody has measured. DRAWN is the middle: the value is
     * reported and only the fill fails. No probe of this scene is a bare
     * MEASURE, because every interior pixel has that one thing to say.
     */
    for (probe = 0ul; probe < scene.probe_count; ++probe) {
        v9x_u16 expect = scene.probes[probe].expect;
        v9x_u16 inside = v9x_test_probe_inside(&scene.triangles[0],
                                               scene.probes[probe].x,
                                               scene.probes[probe].y);

        CHECK(expect != V9X_I9XX_PROBE_MEASURE);
        if (expect == V9X_I9XX_PROBE_DRAWN) {
            ++measured;
            /* A DRAWN probe outside the triangle would fail every boot. */
            CHECK(inside != V9X_FALSE);
        } else {
            ++asserted;
            /* The only assertion this scene may make is the fill, and it must
             * be somewhere the triangle cannot reach. */
            CHECK(expect == V9X_I9XX_PROBE_FILL);
            CHECK(inside == V9X_FALSE);
        }
        /* Every probe names itself, or the capture cannot attribute it. */
        CHECK(v9x_test_streq(
                  v9x_i9xx_probe_expectation_name(expect), "unknown") ==
              V9X_FALSE);
    }
    CHECK(measured >= 4ul);
    CHECK(asserted >= 2ul);

    /*
     * The three corner probes must be near DIFFERENT corners. Three
     * measurements clustered at one vertex read the same colour three times
     * and answer nothing - and that is a mistake a probe table can make
     * silently, because every one of them would still be inside.
     */
    CHECK(scene.probes[0].x != scene.probes[1].x);
    CHECK(scene.probes[2].y != scene.probes[0].y);
    CHECK(scene.probes[2].y != scene.probes[1].y);
}

/*
 * The BLEND scene's expectations.
 *
 * Two triangles, so the rule is simpler and the checks are about what the
 * probes must be able to distinguish rather than about coverage arithmetic.
 */
static void test_blend_scene_expectations(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 probe;
    v9x_u32 blended = 0ul;
    v9x_u32 over_fill = 0ul;

    CHECK(v9x_i9xx_scene_at(4ul, &scene) == V9X_STATUS_OK);
    CHECK(scene.kind == V9X_I9XX_SCENE_BLEND);
    CHECK(scene.triangle_count == 2ul);

    /* The first is OPAQUE and the second is not, or there is no blend to
     * observe - and the opaque one is drawn first so the second blends over a
     * colour this part is measured to store. */
    CHECK(((scene.triangles[0].color >> 24) & 0xfful) == 0xfful);
    CHECK(((scene.triangles[1].color >> 24) & 0xfful) != 0xfful);
    CHECK(((scene.triangles[1].color >> 24) & 0xfful) != 0x00ul);
    CHECK(scene.triangles[0].color_measured == V9X_TRUE);
    CHECK(scene.triangles[1].color_measured == V9X_FALSE);

    for (probe = 0ul; probe < scene.probe_count; ++probe) {
        v9x_u16 in0 = v9x_test_probe_inside(&scene.triangles[0],
                                            scene.probes[probe].x,
                                            scene.probes[probe].y);
        v9x_u16 in1 = v9x_test_probe_inside(&scene.triangles[1],
                                            scene.probes[probe].x,
                                            scene.probes[probe].y);

        if (in0 == V9X_TRUE && in1 == V9X_TRUE) {
            CHECK(scene.probes[probe].expect == V9X_I9XX_PROBE_BLENDED);
            ++blended;
        } else if (in1 == V9X_TRUE) {
            /* Over the fill: still a blend, on a different background. */
            CHECK(scene.probes[probe].expect == V9X_I9XX_PROBE_BLENDED);
            ++over_fill;
        } else if (in0 == V9X_TRUE) {
            /* Nothing drawn over it, so its own measured colour - the one
             * failable interior probe, and the evidence that it ran. */
            CHECK(scene.probes[probe].expect == V9X_I9XX_PROBE_TRIANGLE0);
        } else {
            CHECK(scene.probes[probe].expect == V9X_I9XX_PROBE_FILL);
        }
    }
    /* Both backgrounds must be sampled: a blend over a triangle and a blend
     * over the fill. One coincidence cannot produce both. */
    CHECK(blended >= 2ul);
    CHECK(over_fill >= 1ul);
}

/*
 * Every expectation the build defines must have a NAME, and every scene's
 * probes must use one.
 *
 * The intel46 capture published "unknown" for nine probes - four modulated
 * quadrants and five triangle-2 probes - because the mapper lived in the
 * display driver and never learned the constants added beside it. The result
 * survived, because the validator compares the numeric expectation from the
 * generated table and not this word. What did not survive is the capture's
 * readability: the function's own comment says a probe with no expectation
 * and a probe whose expectation was lost look identical, and nine of them
 * looked lost.
 *
 * So the mapping moved here, where it can be checked exhaustively rather than
 * discovered on a machine.
 */
static void test_probe_expectation_names(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 index;
    v9x_u32 probe;
    v9x_u32 which;
    static const v9x_u16 defined[14] = {
        V9X_I9XX_PROBE_FILL, V9X_I9XX_PROBE_TRIANGLE0,
        V9X_I9XX_PROBE_TRIANGLE1, V9X_I9XX_PROBE_TRIANGLE2,
        V9X_I9XX_PROBE_QUADRANT0, V9X_I9XX_PROBE_QUADRANT1,
        V9X_I9XX_PROBE_QUADRANT2, V9X_I9XX_PROBE_QUADRANT3,
        V9X_I9XX_PROBE_MODQUAD0, V9X_I9XX_PROBE_MODQUAD1,
        V9X_I9XX_PROBE_MODQUAD2, V9X_I9XX_PROBE_MODQUAD3,
        V9X_I9XX_PROBE_BLENDED, V9X_I9XX_PROBE_MEASURE
    };

    for (which = 0ul; which < 14ul; ++which) {
        const char *name = v9x_i9xx_probe_expectation_name(defined[which]);
        v9x_u32 other;

        CHECK(name != 0);
        CHECK(v9x_test_streq(name, "unknown") == V9X_FALSE);
        /* And DISTINCT from every other, or two expectations read the same in
         * a capture and the word stops distinguishing them. */
        for (other = 0ul; other < which; ++other) {
            CHECK(v9x_test_streq(
                      name,
                      v9x_i9xx_probe_expectation_name(defined[other])) ==
                  V9X_FALSE);
        }
    }

    /* A value no constant names still returns a word rather than null. */
    CHECK(v9x_test_streq(v9x_i9xx_probe_expectation_name((v9x_u16)999u),
                         "unknown") == V9X_TRUE);

    /* And no scene in the table carries an expectation without a name, which
     * is the property the capture actually depends on. */
    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        for (probe = 0ul; probe < scene.probe_count; ++probe) {
            CHECK(v9x_test_streq(
                      v9x_i9xx_probe_expectation_name(
                          scene.probes[probe].expect),
                      "unknown") == V9X_FALSE);
        }
    }
}

/*
 * Binding an ARBITRARY surface as a Gen3 render target.
 *
 * Every draw this project has performed renders into one page of the sandbox
 * reserve, at an address and pitch the build chose. A draw that serves an
 * application renders into a surface the application chose, and this is the
 * first arithmetic that can be wrong about one.
 *
 * The constraints are BUF_INFO's, restated against a surface: the pitch field
 * discards its low two bits, the address is a dword-aligned graphics offset,
 * and the whole surface must be inside the aperture. Each is refused rather
 * than clamped, because a clamped surface draws a wrong picture with no error.
 */
static void test_i9xx_bind_target(void)
{
    v9x_u32 identity = 0ul;
    v9x_u32 address = 0ul;
    const v9x_u32 aperture = 0x00800000ul;

    /* A plain 640x480x16 surface at the start of the aperture. */
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1280ul, 640ul, 480ul, aperture,
                                   &identity, &address) == V9X_TRUE);
    CHECK(identity == (V9X_I9XX_BUF_3D_ID_COLOR_BACK | 1280ul));
    CHECK(address == 0ul);

    /* And one at an offset, which is what a second surface in a heap is. */
    CHECK(v9x_d3d_i9xx_bind_target(0x00100000ul, 1280ul, 640ul, 480ul,
                                   aperture, &identity, &address) == V9X_TRUE);
    CHECK(address == 0x00100000ul);

    /*
     * A pitch that does not survive the encoding. 1282 is a plausible
     * application pitch and its low two bits are DISCARDED by the field, so
     * every row but the first would land two bytes early - a sheared picture,
     * no error.
     */
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1282ul, 640ul, 480ul, aperture,
                                   &identity, &address) == V9X_FALSE);
    CHECK(identity == 0ul);
    CHECK(address == 0ul);

    /* A pitch too narrow for its own row: 640 pixels at 16bpp need 1280. */
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1024ul, 640ul, 480ul, aperture,
                                   &identity, &address) == V9X_FALSE);
    /* Exactly wide enough is accepted, which is the boundary either side of
     * that refusal. */
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1280ul, 640ul, 480ul, aperture,
                                   &identity, &address) == V9X_TRUE);

    /* A misaligned address. */
    CHECK(v9x_d3d_i9xx_bind_target(2ul, 1280ul, 640ul, 480ul, aperture,
                                   &identity, &address) == V9X_FALSE);

    /*
     * A surface that runs off the end of the aperture, and the one that ends
     * exactly at it. The second must be ACCEPTED: computing the footprint as
     * height rows rather than (height - 1) rows plus one would refuse a legal
     * surface, and refusing a legal surface is how a driver comes to work only
     * on the modes someone tried.
     */
    CHECK(v9x_d3d_i9xx_bind_target(aperture - 1280ul, 1280ul, 640ul, 480ul,
                                   aperture, &identity, &address) == V9X_FALSE);
    CHECK(v9x_d3d_i9xx_bind_target(aperture - (1280ul * 480ul), 1280ul,
                                   640ul, 480ul, aperture,
                                   &identity, &address) == V9X_TRUE);

    /*
     * Numbers chosen to OVERFLOW the footprint arithmetic. These come from an
     * application, so a product that wraps would pass a bounds test by being
     * small - and the surface it described would be written wherever the wrap
     * landed.
     */
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 0x00fffffcul, 2048ul, 2048ul,
                                   aperture, &identity, &address) == V9X_FALSE);
    CHECK(v9x_d3d_i9xx_bind_target(0xfffffff0ul, 1280ul, 640ul, 480ul,
                                   aperture, &identity, &address) == V9X_FALSE);

    /* Degenerate arguments. */
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1280ul, 0ul, 480ul, aperture,
                                   &identity, &address) == V9X_FALSE);
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1280ul, 640ul, 0ul, aperture,
                                   &identity, &address) == V9X_FALSE);
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 0ul, 640ul, 480ul, aperture,
                                   &identity, &address) == V9X_FALSE);
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1280ul, 640ul, 480ul, 0ul,
                                   &identity, &address) == V9X_FALSE);
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1280ul, 640ul, 480ul, aperture,
                                   0, &address) == V9X_FALSE);
    CHECK(v9x_d3d_i9xx_bind_target(0ul, 1280ul, 640ul, 480ul, aperture,
                                   &identity, 0) == V9X_FALSE);

    /*
     * And the identity it produces is the one the DECODER accepts for a
     * target, which is what ties this to the streams that have run: the
     * diagnostic scenes' own pitch through this function yields the dword
     * their BUF_INFO carries.
     */
    CHECK(v9x_d3d_i9xx_bind_target(0x00012000ul, V9X_I9XX_TARGET_PITCH,
                                   V9X_I9XX_TARGET_WIDTH,
                                   V9X_I9XX_TARGET_HEIGHT, aperture,
                                   &identity, &address) == V9X_TRUE);
    CHECK(identity == (V9X_I9XX_BUF_3D_ID_COLOR_BACK |
                       (V9X_I9XX_TARGET_PITCH &
                        V9X_I9XX_BUF_3D_PITCH_MASK)));
}

/*
 * The float range predicate, which every runtime coordinate check rests on.
 *
 * IEEE-754 positive magnitudes order as unsigned integers, which is what makes
 * the check one comparison - but only while the sign bit is clear. A predicate
 * that forgot the sign would accept every negative coordinate, because a
 * negative float has bit 31 set and compares as a very large positive one.
 */
static void test_float_in_range(void)
{
    const v9x_u32 one = 0x3f800000ul;
    const v9x_u32 w640 = 0x44200000ul;   /* 640.0f */

    CHECK(v9x_i9xx_float_in_range(0ul, one) == V9X_TRUE);
    CHECK(v9x_i9xx_float_in_range(0x3f000000ul, one) == V9X_TRUE);  /* 0.5 */
    CHECK(v9x_i9xx_float_in_range(one, one) == V9X_TRUE);
    /* Just above the limit: 1.0f's neighbour. */
    CHECK(v9x_i9xx_float_in_range(one + 1ul, one) == V9X_FALSE);
    CHECK(v9x_i9xx_float_in_range(0x40000000ul, one) == V9X_FALSE); /* 2.0 */

    /*
     * NEGATIVE. -1.0f is 0xBF800000, which as an unsigned integer is larger
     * than any positive float - so a comparison that ignored the sign would
     * reject it for the wrong reason, and one that compared signed would
     * ACCEPT it. Both are wrong and this is the case that separates them.
     */
    CHECK(v9x_i9xx_float_in_range(0xbf800000ul, one) == V9X_FALSE);
    /* Negative zero, which is a legal float and refused deliberately. */
    CHECK(v9x_i9xx_float_in_range(0x80000000ul, one) == V9X_FALSE);

    /* Infinity and a NaN, excluded by any limit below infinity. */
    CHECK(v9x_i9xx_float_in_range(0x7f800000ul, w640) == V9X_FALSE);
    CHECK(v9x_i9xx_float_in_range(0x7fc00000ul, w640) == V9X_FALSE);

    /* A plausible coordinate against a plausible bound. */
    CHECK(v9x_i9xx_float_in_range(0x43200000ul, w640) == V9X_TRUE); /* 160 */
    CHECK(v9x_i9xx_float_in_range(w640, w640) == V9X_TRUE);         /* 640 */
    CHECK(v9x_i9xx_float_in_range(0x44210000ul, w640) == V9X_FALSE);/* 644 */
}

/*
 * A vertex run built from APPLICATION geometry.
 *
 * The scene builders take whole-pixel integers and one colour per triangle.
 * This takes float bit patterns and a colour per vertex, which is what a
 * DirectDraw execute buffer actually contains, and the stream it produces has
 * to be one the decoder accepts.
 */
static void test_runtime_run(void)
{
    v9x_u32 xyzw[3ul * 4ul];
    v9x_u32 colors[3];
    v9x_u32 stream[64];
    v9x_u32 written = 0ul;
    v9x_u32 index;
    const v9x_u32 one = 0x3f800000ul;

    /* A triangle with fractional coordinates - which is the whole point: the
     * scene builders could not express one. */
    xyzw[0] = 0x43204000ul;  /* 160.25 */
    xyzw[1] = 0x42f00000ul;  /* 120    */
    xyzw[2] = 0ul;
    xyzw[3] = one;
    xyzw[4] = 0x43f00000ul;  /* 480    */
    xyzw[5] = 0x42f00000ul;
    xyzw[6] = 0x3f000000ul;  /* z 0.5  */
    xyzw[7] = one;
    xyzw[8] = 0x43a00000ul;  /* 320    */
    xyzw[9] = 0x43c80000ul;  /* 400    */
    xyzw[10] = one;          /* z 1.0, the far plane */
    xyzw[11] = one;
    colors[0] = 0xff112233ul;
    colors[1] = 0xff445566ul;
    colors[2] = 0xff778899ul;

    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == v9x_i9xx_triangle_run_dwords(1ul));
    CHECK(stream[0] == (V9X_I9XX_3DPRIMITIVE_INLINE |
                        V9X_I9XX_PRIM3D_TRILIST | 14ul));

    /* The bit patterns pass through UNCHANGED. A converter here would be a
     * second opinion about numbers the caller already holds. */
    for (index = 0ul; index < 12ul; ++index) {
        CHECK(stream[1ul + (index / 4ul) * 5ul + (index % 4ul)] ==
              xyzw[index]);
    }
    /* And a colour PER VERTEX, which the scene builders cannot express. */
    CHECK(stream[5] == colors[0]);
    CHECK(stream[10] == colors[1]);
    CHECK(stream[15] == colors[2]);
    CHECK(stream[5] != stream[10]);

    /*
     * A vertex outside the rectangle is REFUSED, not clipped. The core clips
     * before the engine is called, so one arriving here means the core and
     * the engine disagree about the target - and the consequence is a write
     * outside the surface, not a wrong picture.
     */
    xyzw[4] = 0x44210000ul;   /* 644, past a 640-wide target */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    xyzw[4] = 0x43f00000ul;

    /* A negative coordinate, which a signed comparison would accept. */
    xyzw[5] = 0xc2f00000ul;   /* -120 */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[5] = 0x42f00000ul;

    /* A NaN, which is a rasteriser walking an undefined span. */
    xyzw[0] = 0x7fc00000ul;
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[0] = 0x43204000ul;

    /* Z beyond the far plane. */
    xyzw[2] = 0x40000000ul;   /* 2.0 */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[2] = 0ul;

    /*
     * RHW IS NOT REQUIRED TO BE ONE, and requiring it was a defect.
     *
     * The fourth float of a D3DTLVERTEX is rhw - the reciprocal of homogeneous
     * W - and Direct3D defines it as varying with projection. This builder
     * demanded exactly 1.0f, which is true only of geometry that happens to
     * sit on the near plane's scale, so ordinary projected triangles were
     * refused before they reached the stream. Every scene this project has
     * measured used 1.0f, which is why nothing caught it: the diagnostic
     * geometry is the one case the rule was right about.
     *
     * X and Y are still bounded and Z is still [0, 1]. Those are about where
     * the rasteriser may write; rhw is about how it interpolates between the
     * vertices, and asserting a value for it asserts what may be drawn.
     */
    xyzw[3] = 0x40000000ul;   /* 2.0 */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) ==
          V9X_STATUS_OK);
    /* Vertex 0's W: one _3DPRIMITIVE header dword, then x, y, z, w. */
    CHECK(stream[4] == 0x40000000ul);
    xyzw[3] = 0x3d800000ul;   /* 1/16, a far vertex */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) ==
          V9X_STATUS_OK);

    /*
     * What is still refused, because none of these is a reciprocal of
     * anything and all of them hand the interpolator an undefined span.
     */
    xyzw[3] = 0ul;                        /* W infinite */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[3] = 0x80000000ul;               /* negative zero */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[3] = 0xbf800000ul;               /* -1.0 */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[3] = 0x7f800000ul;               /* +infinity */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[3] = 0x7fc00000ul;               /* NaN */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    xyzw[3] = one;

    /* Unmutated, it still builds. */
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) ==
          V9X_STATUS_OK);

    /* Refusals of its own arguments. */
    CHECK(v9x_i9xx_build_runtime_run(0, colors, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_runtime_run(xyzw, 0, 1ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 0ul, 640ul, 480ul,
                                     stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, 640ul, 480ul,
                                     stream, 15ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
}


/*
 * A RUNTIME stream that samples an application texture and tests depth.
 *
 * The point of this test is the pairing. The builders and the decoder are two
 * opinions about one stream, and every earlier kind was pinned to constants
 * this build owns - a runtime textured draw is the first where both sides are
 * reading numbers an application chose, so a disagreement between them is a
 * draw the engine refuses at submission and an application sees as a failure
 * with nothing to explain it.
 */
static void test_runtime_textured_and_depth(void)
{
    struct v9x_i9xx_decode_limits limits;
    struct v9x_i9xx_texture map;
    v9x_u32 stream[400];
    v9x_u32 xyzw[3ul * 4ul];
    v9x_u32 uv[3ul * 2ul];
    v9x_u32 colors[3];
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index = 0ul;
    const v9x_u32 one = 0x3f800000ul;
    const v9x_u32 surface = 0x00200000ul;
    const v9x_u32 pitch = 1024ul;
    const v9x_u32 width = 512ul;
    const v9x_u32 height = 384ul;
    /* A page-aligned map, which is what bind_map requires, and a size the
     * limits admit but the scene table does not use. */
    const v9x_u32 map_offset = 0x00300000ul;
    const v9x_u32 map_edge = 64ul;
    const v9x_u32 depth_offset = 0x00400000ul;
    const v9x_u32 depth_pitch = 1024ul;

    map.offset = map_offset;
    map.width = map_edge;
    map.height = map_edge;
    map.pitch = map_edge * 2ul;
    map.format = V9X_I9XX_MAPSURF_16BIT_RGB565;
    map.wrap = 0ul;
    map.mag_linear = 0ul;
    map.min_linear = 0ul;

    v9x_test_limits(&limits, surface, pitch * height,
                    V9X_I9XX_SCENE_RUNTIME);
    limits.target_pitch = pitch;
    limits.target_width = width;
    limits.target_height = height;
    limits.texture_offset = map_offset;
    limits.texture_bytes = map.height * map.pitch;
    limits.texture_width = map.width;
    limits.texture_height = map.height;
    limits.texture_pitch = map.pitch;
    limits.depth_offset = depth_offset;
    limits.depth_bytes = height * depth_pitch;
    limits.depth_pitch = depth_pitch;
    limits.depth_writes = 1ul;

    CHECK(v9x_i9xx_build_runtime_state(surface, pitch, width, height, &map,
                                       depth_offset, depth_pitch, 1ul,
                                       0ul, 0ul,
                                       stream + at, 400ul - at,
                                       &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_modulate_program(stream + at, 400ul - at,
                                          &produced) == V9X_STATUS_OK);
    at += produced;

    xyzw[0] = 0x43204000ul; xyzw[1] = 0x42f00000ul;
    xyzw[2] = 0ul;          xyzw[3] = one;
    xyzw[4] = 0x43c80000ul; xyzw[5] = 0x42f00000ul;
    xyzw[6] = 0ul;          xyzw[7] = one;
    xyzw[8] = 0x43a00000ul; xyzw[9] = 0x43480000ul;
    xyzw[10] = 0ul;         xyzw[11] = one;
    colors[0] = 0x00c0ffeeul;
    colors[1] = 0xdeadbeeful;
    colors[2] = 0x12345678ul;
    /* Coordinates outside [0, 1] and one negative: ordinary under a
     * normalizing sampler, and the values a wrapped tile is addressed by. */
    uv[0] = 0ul;            uv[1] = 0ul;
    uv[2] = 0x40000000ul;   uv[3] = one;          /* 2.0, 1.0 */
    uv[4] = 0xbf800000ul;   uv[5] = 0x3f000000ul; /* -1.0, 0.5 */

    CHECK(v9x_i9xx_build_textured_runtime_run(xyzw, colors, uv, 1ul,
                                              width, height, stream + at,
                                              400ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;

    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_OK);

    /* The map the stream describes must be the one the limits declare. A
     * pitch the engine did not intend is the defect that reads 126 KiB past a
     * 2 KiB allocation, and it is the same defect whoever chose the number. */
    {
        struct v9x_i9xx_decode_limits wrong = limits;

        wrong.texture_pitch = map.pitch * 2ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) !=
              V9X_I9XX_P5_OK);
        wrong = limits;
        wrong.texture_height = map.height * 2ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) !=
              V9X_I9XX_P5_OK);
        wrong = limits;
        wrong.texture_width = map.width / 2ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) !=
              V9X_I9XX_P5_OK);
        /*
         * And the depth WRITE enable, which for a runtime stream comes from
         * the application rather than from a kind. A stream that wrote depth
         * the engine did not intend would modify the application's buffer
         * unasked.
         */
        wrong = limits;
        wrong.depth_writes = 0ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) !=
              V9X_I9XX_P5_OK);
        /* Declaring no texture at all against a stream that samples one. */
        wrong = limits;
        wrong.texture_bytes = 0ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) !=
              V9X_I9XX_P5_OK);
    }
}

/*
 * A batch of more than three triangles, which the runtime path refused until
 * 2026-09-16 because it shared the scene table's bound.
 *
 * Sixty-four is the ceiling and it is exercised rather than assumed: the run
 * builders multiply the count in sixteen bits, so the largest legal batch is
 * the one most likely to have been got wrong.
 */
static void test_runtime_batch_bound(void)
{
    struct v9x_i9xx_decode_limits limits;
    static v9x_u32 stream[2200];
    static v9x_u32 xyzw[64ul * 3ul * 4ul];
    static v9x_u32 colors[64ul * 3ul];
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index = 0ul;
    v9x_u32 vertex;
    const v9x_u32 one = 0x3f800000ul;
    const v9x_u32 surface = 0x00200000ul;
    const v9x_u32 pitch = 1024ul;
    const v9x_u32 width = 512ul;
    const v9x_u32 height = 384ul;

    for (vertex = 0ul; vertex < 64ul * 3ul; ++vertex) {
        xyzw[(vertex * 4ul) + 0ul] = 0x43204000ul;  /* 160.25 */
        xyzw[(vertex * 4ul) + 1ul] = 0x42f00000ul;  /* 120.0  */
        xyzw[(vertex * 4ul) + 2ul] = 0ul;
        xyzw[(vertex * 4ul) + 3ul] = one;
        colors[vertex] = 0x00ff00fful;
    }

    v9x_test_limits(&limits, surface, pitch * height,
                    V9X_I9XX_SCENE_RUNTIME);
    limits.target_pitch = pitch;
    limits.target_width = width;
    limits.target_height = height;

    CHECK(v9x_i9xx_build_runtime_state(surface, pitch, width, height, 0,
                                       0ul, 0ul, 0ul, 0ul, 0ul, stream + at,
                                       2200ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_fragment_program(stream + at, 2200ul - at,
                                          &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 64ul, width, height,
                                     stream + at, 2200ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_OK);

    /* One past the ceiling is refused by the builder rather than truncated. */
    CHECK(v9x_i9xx_runtime_run_dwords(V9X_I9XX_RUNTIME_MAX_TRIANGLES + 1ul) ==
          0ul);
    CHECK(v9x_i9xx_runtime_textured_run_dwords(
              V9X_I9XX_RUNTIME_MAX_TRIANGLES + 1ul) == 0ul);
    /* And the scene bound is untouched: a scene still draws at most three. */
    CHECK(v9x_i9xx_triangle_run_dwords(V9X_I9XX_SCENE_MAX_TRIANGLES) != 0ul);
    CHECK(v9x_i9xx_triangle_run_dwords(V9X_I9XX_SCENE_MAX_TRIANGLES + 1ul) ==
          0ul);
}

/*
 * The RUNTIME decoder mode, and - the point of this test - everything it does
 * NOT relax.
 *
 * The 2026-09-16 amendment authorised sustained 3D and, in doing so, gave up
 * the combined-CRC gate: application geometry cannot be pinned to a constant
 * generated at build time. What it put in that gate's place is this decoder.
 * So the useful assertions here are the negative ones: a mode that quietly
 * relaxed one thing too many would pass every positive check and leave the
 * amendment resting on nothing.
 */
static void test_decoder_runtime_mode(void)
{
    struct v9x_i9xx_decode_limits limits;
    struct v9x_i9xx_decode_limits plain;
    v9x_u32 stream[200];
    v9x_u32 xyzw[3ul * 4ul];
    v9x_u32 colors[3];
    v9x_u32 written = 0ul;
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index = 0ul;
    v9x_u32 primitive;
    v9x_u32 saved;
    const v9x_u32 one = 0x3f800000ul;
    /* A surface that is NOT the diagnostic target: a different address, a
     * different pitch and a different size, which is the whole point. */
    const v9x_u32 surface = 0x00200000ul;
    const v9x_u32 pitch = 1024ul;
    const v9x_u32 width = 512ul;
    const v9x_u32 height = 384ul;

    v9x_test_limits(&limits, surface, pitch * height,
                    V9X_I9XX_SCENE_RUNTIME);
    limits.target_pitch = pitch;
    limits.target_width = width;
    limits.target_height = height;

    /* The state block for that surface, then the program, then the geometry.
     * No fill and no probe: the surface is the application's. */
    CHECK(v9x_i9xx_build_3d_state(surface, pitch, width, height,
                                  stream + at, 200ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_fragment_program(stream + at, 200ul - at,
                                          &produced) == V9X_STATUS_OK);
    at += produced;
    primitive = at;

    xyzw[0] = 0x43204000ul; xyzw[1] = 0x42f00000ul;
    xyzw[2] = 0ul;          xyzw[3] = one;
    xyzw[4] = 0x43c80000ul; xyzw[5] = 0x42f00000ul;
    xyzw[6] = 0ul;          xyzw[7] = one;
    xyzw[8] = 0x43a00000ul; xyzw[9] = 0x43480000ul;
    xyzw[10] = 0ul;         xyzw[11] = one;
    colors[0] = 0x00c0ffeeul;
    colors[1] = 0xdeadbeeful;
    colors[2] = 0x12345678ul;

    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, width, height,
                                     stream + at, 200ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;
    written = at;

    /* It decodes. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);

    /*
     * RHW, THE SECOND JUDGEMENT. The builder accepts a projected value and
     * this must accept the same one, or a draw the builder allowed would be
     * refused at submission and the engine would look broken from the other
     * side. The two checks are independent by design; agreeing is not
     * optional.
     */
    saved = stream[primitive + 4ul];
    stream[primitive + 4ul] = 0x40000000ul;          /* rhw 2.0 */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);
    stream[primitive + 4ul] = 0x3d800000ul;          /* rhw 1/16 */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);
    /* And the values that are not reciprocals of anything. */
    stream[primitive + 4ul] = 0ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    stream[primitive + 4ul] = 0xbf800000ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    stream[primitive + 4ul] = 0x7f800000ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    stream[primitive + 4ul] = 0x7fc00000ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    stream[primitive + 4ul] = saved;

    /*
     * That the SCENE kinds still pin rhw to one is not asserted here, and the
     * attempt is worth recording: decoding this stream with SCENE_PLAIN was
     * tried, and it refuses long before the vertices - a plain scene's limits
     * describe the diagnostic target, not this surface. The refusal was real
     * and said nothing about rhw, which is the shape of assertion this project
     * keeps catching. The scene rule is exercised where it can be: the
     * generated-table self-test in check-intel-3d-capture.ps1 mutates scene
     * vertices and requires each rejection.
     */

    /*
     * WHAT IT RELAXES, asserted so the relaxation is real rather than assumed:
     * arbitrary colours, which no other kind accepts.
     */
    CHECK(colors[0] != V9X_I9XX_TRI_COLOR_BGRA);
    CHECK(colors[1] != V9X_I9XX_TRI_COLOR_BGRA);

    /* And fractional coordinates, which no scene can express. */
    saved = stream[primitive + 1ul];
    stream[primitive + 1ul] = 0x43204ccdul;   /* 160.3 */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);
    stream[primitive + 1ul] = saved;

    /* ------------------------------------------------------------------ */
    /* WHAT IT DOES NOT RELAX. Each of these must still be refused.        */
    /* ------------------------------------------------------------------ */

    /* The target ADDRESS. This is the dword that decides where the GPU
     * writes, and a runtime stream gets no more latitude with it than a
     * scene does. */
    {
        /* The BUF_INFO is not the first dword: the invariant block comes
         * first. Located by scanning rather than by a counted offset, which
         * is what this project keeps learning about published offsets. */
        v9x_u32 buf = 0ul;

        for (index = 0ul; index < primitive; ++index) {
            if (stream[index] == V9X_I9XX_3DSTATE_BUF_INFO) {
                buf = index;
            }
        }
        CHECK(buf != 0ul);

        saved = stream[buf + 2ul];
        stream[buf + 2ul] = surface + 0x1000ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits,
                                            &index) ==
              V9X_I9XX_P5_TARGET_RANGE);
        stream[buf + 2ul] = saved;

        /* The PITCH. A stream whose pitch disagrees with the surface shears
         * every row but the first. */
        saved = stream[buf + 1ul];
        stream[buf + 1ul] = V9X_I9XX_BUF_3D_ID_COLOR_BACK | (pitch * 2ul);
        CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits,
                                            &index) == V9X_I9XX_P5_PITCH);
        stream[buf + 1ul] = saved;
    }

    /* A vertex OUTSIDE the declared rectangle. The builder refuses these, and
     * so must the decoder - the two are independent opinions and this is the
     * one that runs against a stream nobody built. */
    saved = stream[primitive + 1ul];
    stream[primitive + 1ul] = 0x44100000ul;   /* 576, past a 512-wide target */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_VERTEX_RANGE);
    stream[primitive + 1ul] = saved;

    /* A payload that is not a whole number of vertices: the last one would be
     * short and the loop would read past what was submitted. */
    saved = stream[primitive];
    stream[primitive] = V9X_I9XX_3DPRIMITIVE_INLINE |
                        V9X_I9XX_PRIM3D_TRILIST | 13ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    /* And one that is whole vertices but not whole TRIANGLES. */
    stream[primitive] = V9X_I9XX_3DPRIMITIVE_INLINE |
                        V9X_I9XX_PRIM3D_TRILIST | 9ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_VERTEX_COUNT);
    stream[primitive] = saved;

    /* Depth is still forbidden without a depth range, and texture packets
     * without a texture range - a runtime stream is a PLAIN stream whose
     * geometry is free, not a stream that may do anything. */
    CHECK(limits.depth_bytes == 0ul);
    CHECK(limits.texture_bytes == 0ul);

    /*
     * And the same stream under the PLAIN kind is refused, because a plain
     * stream must have filled before it draws. That is the one precondition
     * the runtime kind drops, and dropping it is deliberate: the surface
     * belongs to the application, which decides when it is cleared, and a
     * driver that filled it every draw would erase the frame it is drawing.
     */
    v9x_test_limits(&plain, surface, pitch * height, V9X_I9XX_SCENE_PLAIN);
    plain.target_pitch = pitch;
    plain.target_width = width;
    plain.target_height = height;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &plain, &index) ==
          V9X_I9XX_P5_MISSING_PACKET);

    /* Limits that cannot describe a target at all. A zero width would make
     * the DRAW_RECT comparison underflow to 0xffff and accept a rectangle
     * nobody asked for. */
    limits.target_width = 0ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    limits.target_width = width;
    limits.target_pitch = pitch + 1ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) !=
          V9X_I9XX_P5_OK);
    limits.target_pitch = pitch;

    /* Unmutated, it still decodes. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, written, &limits, &index) ==
          V9X_I9XX_P5_OK);
}

/* The combined CRC, which is what the arm gate compares. */
static void test_scene_combined_crc(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 combined[512];
    v9x_u32 at = 0ul;
    v9x_u32 index;
    v9x_u32 produced = 0ul;

    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        CHECK(v9x_i9xx_build_scene_stream(
                  &scene, combined + at, 512ul - at, &produced) ==
              V9X_STATUS_OK);
        at += produced;
    }

    /*
     * Over the concatenated dwords in execution order, not over the per-scene
     * CRCs - a CRC of CRCs would not notice a length change that hashed the
     * same.
     */
    CHECK(v9x_i9xx_scene_combined_crc() ==
          v9x_i9xx_crc32_dwords(combined, at));

    /*
     * And it is not any single scene's CRC. Without this the function could
     * return scene 0's value and every other assertion here would still hold.
     */
    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        CHECK(v9x_i9xx_scene_combined_crc() != v9x_i9xx_scene_crc(index));
    }
}

/* The general triangle-run builder's own refusals. */
static void test_triangle_run_refusals(void)
{
    struct v9x_i9xx_triangle triangle;
    v9x_u32 stream[64];
    v9x_u32 written = 0ul;

    v9x_test_scene_triangle(&triangle);
    CHECK(v9x_i9xx_build_triangle_run(&triangle, 1ul, 640ul, 480ul,
                                      stream, 64ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 16ul);

    CHECK(v9x_i9xx_build_triangle_run(&triangle, 0ul, 640ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_triangle_run(&triangle,
                                      V9X_I9XX_SCENE_MAX_TRIANGLES + 1ul,
                                      640ul, 480ul, stream, 64ul,
                                      &written) != V9X_STATUS_OK);

    /*
     * A vertex exactly ON the boundary is outside: the rectangle is
     * inclusive, so 640 is one past the last addressable pixel. Both sides
     * are asserted, because a builder that refused everything would pass a
     * one-sided check.
     */
    triangle.x[1] = 639ul;
    CHECK(v9x_i9xx_build_triangle_run(&triangle, 1ul, 640ul, 480ul,
                                      stream, 64ul, &written) ==
          V9X_STATUS_OK);
    triangle.x[1] = 640ul;
    CHECK(v9x_i9xx_build_triangle_run(&triangle, 1ul, 640ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);

    v9x_test_scene_triangle(&triangle);
    triangle.y[2] = 480ul;
    CHECK(v9x_i9xx_build_triangle_run(&triangle, 1ul, 640ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);

    /* Capacity, at the boundary. */
    v9x_test_scene_triangle(&triangle);
    CHECK(v9x_i9xx_build_triangle_run(&triangle, 1ul, 640ul, 480ul,
                                      stream, 15ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_triangle_run(&triangle, 1ul, 640ul, 480ul,
                                      stream, 16ul, &written) ==
          V9X_STATUS_OK);

    CHECK(v9x_i9xx_build_triangle_run(0, 1ul, 640ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_triangle_run(&triangle, 1ul, 0ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);
}

/*
 * The submission boundary the executor stops at.
 *
 * This existed as a literal 50 in loader.asm, correct for the 66-dword stream,
 * and stayed 50 when the depth BUF_INFO removal moved the primitive to 47. The
 * probe submission then ran three dwords INTO the primitive - the parser would
 * take a _3DPRIMITIVE header and then whatever followed - and the draw
 * submitted through dword 66 of a 63-dword stream.
 *
 * Asserted three ways, because the defect was a second computation of a number
 * that had drifted from the first: against the literal 47, against the stream
 * the builder actually produces, and against the primitive header being there.
 */
static void test_scene_primitive_offset(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 stream[160];
    v9x_u32 written = 0ul;
    v9x_u32 offset;
    v9x_u32 index;

    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        offset = v9x_i9xx_scene_primitive_offset(&scene);

        /*
         * Untextured: fill 7 + state 31 + shader 7 + probe 2 = 47, padded to
         * 48. Textured: paint 25 + fill 7 + state 41 + shader 10 + probe 2 =
         * 85, padded to 86.
         *
         * Literals, deliberately. The point of this test is a SECOND opinion
         * on a number the builder also computes, and deriving it from the same
         * extent helpers would make it the same opinion twice - which is
         * exactly how loader.asm's boundary drifted.
         */
        /*
         * Per kind, as literals:
         *   plain      fill 7 + state 31 + shader 7 + probe 2 = 47, padded 48
         *   textured   paint 25 + fill 7 + state 41 + shader 10 + probe 2
         *              = 85, padded 86
         *   modulated  the same with the 16-dword program = 91, padded 92
         *   depth      clear 7 + fill 7 + state 34 + shader 7 + probe 2
         *              = 57, padded 58
         */
        if (scene.kind == V9X_I9XX_SCENE_MODULATED) {
            CHECK(offset == 92ul);
        } else if (v9x_i9xx_scene_kind_textured(scene.kind) != V9X_FALSE) {
            CHECK(offset == 86ul);
        } else if (v9x_i9xx_scene_kind_depth(scene.kind) != V9X_FALSE) {
            CHECK(offset == 58ul);
        } else {
            CHECK(offset == 48ul);
        }

        written = 0ul;
        CHECK(v9x_i9xx_build_scene_stream(
                  &scene, stream, 160ul, &written) == V9X_STATUS_OK);
        CHECK(offset < written);

        /*
         * And the dword AT the boundary is the primitive header, which is what
         * makes the boundary the right one rather than merely a number both
         * sides agree on.
         */
        CHECK((stream[offset] & 0xff000000ul) == V9X_I9XX_3DPRIMITIVE_INLINE);

        /*
         * Everything after it is the triangle run plus at most one trailing
         * qword pad. Bounded on both sides rather than asserted exactly: the
         * pad is present for two triangles and absent for one, and a test
         * that allowed any surplus would not notice a run that had grown.
         */
        {
            v9x_u32 run =
                (v9x_i9xx_scene_kind_textured(scene.kind) != V9X_FALSE)
                    ? v9x_i9xx_textured_run_dwords(scene.triangle_count)
                    : v9x_i9xx_triangle_run_dwords(scene.triangle_count);

            CHECK(written - offset >= run);
            CHECK(written - offset <= run + 1ul);
        }
    }

    /* A scene that cannot be built has no boundary, rather than a plausible
     * one computed from zero. */
    CHECK(v9x_i9xx_scene_at(0ul, &scene) == V9X_STATUS_OK);
    scene.triangle_count = 0ul;
    CHECK(v9x_i9xx_scene_primitive_offset(&scene) == 0ul);
    CHECK(v9x_i9xx_scene_primitive_offset(0) == 0ul);
}

/* A request that passes every check except the ones a test is exercising. */
static void v9x_test_arm_request(struct v9x_i9xx_arm_request *request)
{
    request->token = "p6-test-0001";
    request->in_flight = "p6-test-0001";
    request->configured_crc = 0ul;
    request->packet_crc = 0ul;
    request->enable_this_boot = 1u;
    request->safe_mode = V9X_FALSE;
    request->errata_gate = V9X_TRUE;
    request->vendor_id = 0x8086u;
    request->device_id = 0x27aeu;
    request->revision = 3u;
    request->phase = V9X_I9XX_PHASE6;
    request->expected_phase = V9X_I9XX_PHASE6;
}

/*
 * The Phase 6 arm chain.
 *
 * Reported against f3cf86e: the boot latch accepted a phase-6 token, the gate
 * and chain did not, and the token would have been consumed into flight and
 * then refused - one armed boot spent to learn nothing. These assert the whole
 * path rather than the entry it was noticed at.
 */
static void test_phase6_chain(void)
{
    struct v9x_i9xx_chain chain;
    struct v9x_i9xx_arm_request request;
    v9x_u32 phase4 = 0xa0da64a1ul;
    v9x_u32 scenes = v9x_i9xx_scene_combined_crc();
    v9x_u16 rejection = V9X_I9XX_ARM_REJECT_NONE;

    CHECK(scenes != 0ul);

    /* The gate. Phase 6 is a CHAINED draw, exactly as Phase 5 is. */
    CHECK(v9x_i9xx_arm_gate_for(1u, V9X_I9XX_PHASE6, 1u) ==
          V9X_I9XX_GATE_CHAINED);
    /* And a build with no submit path refuses it, rather than running a draw
     * it cannot perform. */
    CHECK(v9x_i9xx_arm_gate_for(1u, V9X_I9XX_PHASE6, 0u) ==
          V9X_I9XX_GATE_REFUSE);
    /* An unarmed boot is still no gate at all, whatever the phase claims. */
    CHECK(v9x_i9xx_arm_gate_for(0u, V9X_I9XX_PHASE6, 1u) ==
          V9X_I9XX_GATE_NONE);
    /* A phase this build has no vocabulary for is still refused. */
    CHECK(v9x_i9xx_arm_gate_for(1u, 7u, 1u) == V9X_I9XX_GATE_REFUSE);

    v9x_test_arm_request(&request);
    request.phase = V9X_I9XX_PHASE6;
    request.expected_phase = V9X_I9XX_PHASE6;
    request.packet_crc = v9x_i9xx_combined_arm_crc(phase4, scenes);
    /* The arm file's CRC and the one the driver computed must agree; the
     * contract checks them against each other, not each against itself. */
    request.configured_crc = request.packet_crc;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) == V9X_STATUS_OK);

    CHECK(v9x_i9xx_chain_begin(&chain, &request, request.packet_crc,
                               phase4, scenes) == V9X_I9XX_CHAIN_OK);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_ARMED);

    /*
     * A PHASE 5 token must not arm a Phase 6 build, and the reverse must not
     * happen either. Two separate ways it is caught, and both are asserted:
     * the phase fields disagreeing, and - with them made to agree - the
     * combined CRC being over the wrong draw stream.
     */
    request.phase = V9X_I9XX_PHASE5;
    CHECK(v9x_i9xx_chain_begin(&chain, &request, request.packet_crc,
                               phase4, scenes) ==
          V9X_I9XX_CHAIN_REJECT_PHASE);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_FAILED);

    request.expected_phase = V9X_I9XX_PHASE5;
    CHECK(v9x_i9xx_chain_begin(&chain, &request, request.packet_crc,
                               phase4, v9x_i9xx_phase5_execution_crc()) ==
          V9X_I9XX_CHAIN_REJECT_COMBINED);

    /* And a phase neither 5 nor 6 is refused before any CRC is considered. */
    v9x_test_arm_request(&request);
    request.phase = V9X_I9XX_PHASE4;
    request.expected_phase = V9X_I9XX_PHASE4;
    request.packet_crc = v9x_i9xx_combined_arm_crc(phase4, scenes);
    request.configured_crc = request.packet_crc;
    CHECK(v9x_i9xx_chain_begin(&chain, &request, request.packet_crc,
                               phase4, scenes) ==
          V9X_I9XX_CHAIN_REJECT_PHASE);

    /*
     * The full transaction: replay, then draw, then the token retires. A
     * chain that armed but could never retire would leave every Phase 6 boot
     * reporting INCOMPLETE - which is the failure the reported defect would
     * have produced one boot later.
     */
    v9x_test_arm_request(&request);
    request.phase = V9X_I9XX_PHASE6;
    request.expected_phase = V9X_I9XX_PHASE6;
    request.packet_crc = v9x_i9xx_combined_arm_crc(phase4, scenes);
    request.configured_crc = request.packet_crc;
    CHECK(v9x_i9xx_chain_begin(&chain, &request, request.packet_crc,
                               phase4, scenes) == V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_TRUE, phase4, phase4) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(chain.state == V9X_I9XX_CHAIN_STATE_REPLAYED);
    CHECK(v9x_i9xx_chain_draw_done(&chain, V9X_TRUE, scenes, scenes) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(chain.token_retired == V9X_TRUE);
    CHECK(chain.in_flight_cleared == V9X_TRUE);

    /*
     * A draw whose observed stream is not the armed one does NOT retire. The
     * token stays in flight and the next boot refuses, which is the correct
     * outcome for a run nobody can vouch for.
     */
    v9x_test_arm_request(&request);
    request.phase = V9X_I9XX_PHASE6;
    request.expected_phase = V9X_I9XX_PHASE6;
    request.packet_crc = v9x_i9xx_combined_arm_crc(phase4, scenes);
    request.configured_crc = request.packet_crc;
    CHECK(v9x_i9xx_chain_begin(&chain, &request, request.packet_crc,
                               phase4, scenes) == V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_replay_done(&chain, V9X_TRUE, phase4, phase4) ==
          V9X_I9XX_CHAIN_OK);
    CHECK(v9x_i9xx_chain_draw_done(&chain, V9X_TRUE, scenes ^ 1ul, scenes) !=
          V9X_I9XX_CHAIN_OK);
    CHECK(chain.token_retired == V9X_FALSE);
    CHECK(chain.in_flight_cleared == V9X_FALSE);
}

/*
 * EVERY submission boundary must be qword aligned.
 *
 * RING_TAIL holds a qword-aligned offset and bit 2 is not writable. Measured
 * on the part 2026-09-16: the mini-VDD wrote 0x10BC, read back 0x10B8, the
 * compare failed and the run was poisoned at scene 0.
 *
 * Nothing host-side knew the rule, so removing the depth BUF_INFO took the
 * Phase 5 stream from 66 dwords to 63 and its primitive from 50 to 47 - both
 * even to odd - and broke a path that had drawn correctly twice. The old
 * values were aligned by accident.
 *
 * Asserted in BYTES at the ring offset the executor actually submits, not as
 * "the dword count is even". The executor adds V9X_I9XX_P5_RING_OFFSET, and a
 * test that checked the count alone would still pass if that base moved.
 */
static void test_submission_boundaries_are_qword_aligned(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 stream[160];
    v9x_u32 written;
    v9x_u32 index;
    v9x_u32 padded;
    v9x_u32 unpadded;
    v9x_u32 poison;
    v9x_u32 offset;

    /*
     * The ring base itself is checked by check-tree, not here. Asserting a
     * compile-time constant against itself is a check that cannot fail - the
     * compiler says so, calling the failure branch unreachable - and the
     * cross-check that matters is that loader.asm and the header agree, which
     * no C test can see.
     */
    written = 0ul;
    CHECK(v9x_i9xx_build_phase5_stream(stream, 160ul, &written) ==
          V9X_STATUS_OK);
    offset = V9X_I9XX_P5_RING_OFFSET + (written * 4ul);
    CHECK((offset & 7ul) == 0ul);

    padded = 0ul;
    unpadded = 0ul;
    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        v9x_u32 primitive;

        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        written = 0ul;
        for (poison = 0ul; poison < 160ul; ++poison) {
            stream[poison] = 0xdeadbeeful;
        }
        CHECK(v9x_i9xx_build_scene_stream(
                  &scene, stream, 160ul, &written) == V9X_STATUS_OK);

        /* The draw boundary. */
        offset = V9X_I9XX_P5_RING_OFFSET + (written * 4ul);
        CHECK((offset & 7ul) == 0ul);

        /* And the probe boundary, which is where this one actually failed. */
        primitive = v9x_i9xx_scene_primitive_offset(&scene);
        offset = V9X_I9XX_P5_RING_OFFSET + (primitive * 4ul);
        CHECK((offset & 7ul) == 0ul);

        /* The extent must still describe what was built, pads included. */
        CHECK(v9x_i9xx_scene_extent(&scene) == written);
        /* And the boundary must still land ON the primitive header, or the
         * padding has been inserted in the wrong place. */
        CHECK((stream[primitive] & 0xff000000ul) ==
              V9X_I9XX_3DPRIMITIVE_INLINE);
        /*
         * Where a pad IS inserted it must be an MI_NOOP, not whatever the
         * buffer held.
         *
         * Not every scene needs one: the blend scene's prefix is even, because
         * its state block carries one extra dword for the IAB disable. So the
         * pad is asserted where the arithmetic says there is one and its
         * ABSENCE is asserted where it says there is not - a test that only
         * looked for a NOOP would pass on a scene that never padded, and one
         * that never looked would pass on a scene that padded with rubbish.
         */
        CHECK(primitive >= 1ul);
        /*
         * NO DWORD WAS LEFT UNWRITTEN.
         *
         * This asserted that the dword before the primitive is an MI_NOOP,
         * which was true only while every scene's prefix happened to be odd.
         * The blend scene's is even - its state block carries the IAB disable
         * - so that assertion started failing on a scene that pads nothing,
         * and predicting which scenes pad meant deriving the prefix a second
         * way.
         *
         * Poisoning the buffer first is stronger and predicts nothing: a pad
         * written as MI_NOOP passes, a pad the builder forgot to write shows
         * up as the poison, and so does any other gap anywhere in the stream.
         */
        for (poison = 0ul; poison < written; ++poison) {
            CHECK(stream[poison] != 0xdeadbeeful);
        }
        if (stream[primitive - 1ul] == V9X_I9XX_MI_NOOP) {
            ++padded;
        } else {
            ++unpadded;
        }
    }
    /*
     * And this table exercises BOTH: at least one scene pads its prefix and at
     * least one does not, so neither branch of the builder's alignment step is
     * a path nothing runs.
     */
    CHECK(padded >= 1ul);
    CHECK(unpadded >= 1ul);
}

/*
 * MAP_STATE.
 *
 * The expected dwords are written out as LITERALS derived from the audit, not
 * recomputed from the same shifts the builder uses. A test that reassembles
 * the value from the builder's own constants checks that the compiler works.
 */
static void test_map_state(void)
{
    struct v9x_i9xx_texture map;
    v9x_u32 stream[16];
    v9x_u32 written = 0ul;

    map.offset = 0x00700000ul;
    map.width = 16ul;
    map.height = 16ul;
    map.pitch = 32ul;
    map.format = V9X_I9XX_MAPSURF_16BIT_RGB565;
    map.wrap = 0ul;
    map.mag_linear = 0ul;
    map.min_linear = 0ul;

    CHECK(v9x_i9xx_map_state_extent(1ul) == 5ul);
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 16ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 5ul);

    /* Command: 0x7D000000 with the length field 3 per map. */
    CHECK(stream[0] == 0x7d000003ul);
    /* One unit enabled. */
    CHECK(stream[1] == 0x00000001ul);
    /* The address, unchanged. */
    CHECK(stream[2] == 0x00700000ul);
    /*
     * MS3: (16-1) << 21 | (16-1) << 10 | 0x100.
     * 15 << 21 = 0x01E00000, 15 << 10 = 0x00003C00, format 0x00000100.
     */
    CHECK(stream[3] == 0x01e03d00ul);
    /* Tiling bits clear - the texture is linear. */
    CHECK((stream[3] & (V9X_I9XX_MS3_TILED_SURFACE |
                        V9X_I9XX_MS3_TILE_WALK)) == 0ul);
    /* MS4: pitch 32 bytes is 8 dwords, minus one is 7, at shift 21. */
    CHECK(stream[4] == 0x00e00000ul);
    /* And nothing else. xf86 sets the pitch alone; the cube-face mask Mesa
     * ORs unconditionally is the recorded divergence and is NOT set. */
    CHECK((stream[4] & 0x001f8000ul) == 0ul);

    /* Dimensions are minus one, which is the field convention most likely to
     * be got backwards. A 1x1 map encodes zero in both fields. */
    map.width = 1ul;
    map.height = 1ul;
    map.pitch = 4ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 16ul, &written) ==
          V9X_STATUS_OK);
    CHECK((stream[3] & 0xffe00000ul) == 0ul);
    CHECK((stream[3] & 0x001ffc00ul) == 0ul);
    CHECK(stream[4] == 0ul);
}

/* Every bound MAP_STATE refuses, and both sides of each. */
static void test_map_state_refusals(void)
{
    struct v9x_i9xx_texture map;
    v9x_u32 stream[32];
    v9x_u32 written = 0ul;

    map.offset = 0x00700000ul;
    map.width = 16ul;
    map.height = 16ul;
    map.pitch = 32ul;
    map.format = V9X_I9XX_MAPSURF_16BIT_RGB565;
    map.wrap = 0ul;
    map.mag_linear = 0ul;
    map.min_linear = 0ul;

    CHECK(v9x_i9xx_build_map_state(0, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_map_state(&map, 0ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    /* Eight units exist; nine do not. Both sides. */
    CHECK(v9x_i9xx_map_state_extent(8ul) == 26ul);
    CHECK(v9x_i9xx_map_state_extent(9ul) == 0ul);
    CHECK(v9x_i9xx_build_map_state(&map, 9ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);

    /* Capacity, at the boundary. */
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 4ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 5ul, &written) ==
          V9X_STATUS_OK);

    /*
     * A dimension one past the field refuses. 2048 fits eleven bits as
     * 2047; 2049 does not, and truncating it would give a texture the
     * hardware reads at the wrong size from an address that is still valid.
     */
    map.width = 2048ul;
    map.pitch = 4096ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);
    map.width = 2049ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    map.width = 0ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);

    map.width = 16ul;
    map.height = 2049ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    map.height = 0ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);

    /* A pitch the dword encoding would truncate. */
    map.height = 16ul;
    map.pitch = 33ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    map.pitch = 0ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    /* A row too narrow for its own texels, at two bytes each. */
    map.pitch = 28ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    map.pitch = 32ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);

    /* An address that is not page aligned. */
    map.offset = 0x00700004ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
}

/*
 * The three MS3 formats, and only those three.
 *
 * Added with intel56, where every Final Reality texture was 4:4:4:4 and the
 * one-format builder refused them all. The type code is bits 5:3 of MS3 and
 * the rest of the dword is unchanged, which is what the first two checks pin;
 * the last two are that a format nobody stated, or one this driver has no
 * audit for, is refused rather than emitted as 565.
 */
static void test_map_state_formats(void)
{
    struct v9x_i9xx_texture map;
    v9x_u32 stream[16];
    v9x_u32 written = 0ul;

    map.offset = 0x00700000ul;
    map.width = 16ul;
    map.height = 16ul;
    map.pitch = 32ul;

    CHECK(v9x_i9xx_map_format_known(V9X_I9XX_MAPSURF_16BIT_RGB565) !=
          V9X_FALSE);
    CHECK(v9x_i9xx_map_format_known(V9X_I9XX_MAPSURF_16BIT_ARGB1555) !=
          V9X_FALSE);
    CHECK(v9x_i9xx_map_format_known(V9X_I9XX_MAPSURF_16BIT_ARGB4444) !=
          V9X_FALSE);
    CHECK(v9x_i9xx_map_format_known(0ul) == V9X_FALSE);
    /* MAPSURF_16BIT with type 3 (AY88) and MAPSURF_32BIT ARGB8888: real
     * codes, not audited, not emitted. */
    CHECK(v9x_i9xx_map_format_known(0x00000118ul) == V9X_FALSE);
    CHECK(v9x_i9xx_map_format_known(0x00000180ul) == V9X_FALSE);

    /* ARGB1555: type 1, so 0x108 where 565 has 0x100. */
    map.format = V9X_I9XX_MAPSURF_16BIT_ARGB1555;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 16ul, &written) ==
          V9X_STATUS_OK);
    CHECK(stream[3] == 0x01e03d08ul);
    CHECK(stream[4] == 0x00e00000ul);

    /* ARGB4444: type 2, so 0x110. */
    map.format = V9X_I9XX_MAPSURF_16BIT_ARGB4444;
    map.wrap = 0ul;
    map.mag_linear = 0ul;
    map.min_linear = 0ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 16ul, &written) ==
          V9X_STATUS_OK);
    CHECK(stream[3] == 0x01e03d10ul);
    CHECK(stream[4] == 0x00e00000ul);

    /* Unstated, and unknown. */
    map.format = 0ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 16ul, &written) !=
          V9X_STATUS_OK);
    map.format = 0x00000118ul;
    CHECK(v9x_i9xx_build_map_state(&map, 1ul, stream, 16ul, &written) !=
          V9X_STATUS_OK);
}

/*
 * The decoder follows the format the engine declared, and a scene does not
 * get to declare one.
 *
 * A runtime stream built over a 4444 map passes when the limits say 4444 and
 * fails when they say 565 (or say nothing, which is 565): the same MS3
 * equality that catches a wrong pitch catches a wrong type. A scene never
 * reaches the declared-format path at all - the decoder pins it to the
 * constant - so the generated streams stay the audited 565 ones.
 */
static void test_runtime_texture_formats(void)
{
    struct v9x_i9xx_decode_limits limits;
    struct v9x_i9xx_texture map;
    v9x_u32 stream[400];
    v9x_u32 xyzw[3ul * 4ul];
    v9x_u32 uv[3ul * 2ul];
    v9x_u32 colors[3];
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index = 0ul;
    const v9x_u32 one = 0x3f800000ul;
    const v9x_u32 surface = 0x00200000ul;
    const v9x_u32 pitch = 1024ul;
    const v9x_u32 width = 512ul;
    const v9x_u32 height = 384ul;
    const v9x_u32 map_offset = 0x00300000ul;
    const v9x_u32 map_edge = 64ul;

    map.offset = map_offset;
    map.width = map_edge;
    map.height = map_edge;
    map.pitch = map_edge * 2ul;
    map.format = V9X_I9XX_MAPSURF_16BIT_ARGB4444;
    map.wrap = 0ul;
    map.mag_linear = 0ul;
    map.min_linear = 0ul;

    v9x_test_limits(&limits, surface, pitch * height,
                    V9X_I9XX_SCENE_RUNTIME);
    limits.target_pitch = pitch;
    limits.target_width = width;
    limits.target_height = height;
    limits.texture_offset = map_offset;
    limits.texture_bytes = map.height * map.pitch;
    limits.texture_width = map.width;
    limits.texture_height = map.height;
    limits.texture_pitch = map.pitch;
    limits.texture_format = V9X_I9XX_MAPSURF_16BIT_ARGB4444;

    CHECK(v9x_i9xx_build_runtime_state(surface, pitch, width, height, &map,
                                       0ul, 0ul, 0ul, 0ul, 0ul,
                                       stream + at, 400ul - at,
                                       &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_modulate_program(stream + at, 400ul - at,
                                          &produced) == V9X_STATUS_OK);
    at += produced;

    xyzw[0] = 0x43204000ul; xyzw[1] = 0x42f00000ul;
    xyzw[2] = 0ul;          xyzw[3] = one;
    xyzw[4] = 0x43c80000ul; xyzw[5] = 0x42f00000ul;
    xyzw[6] = 0ul;          xyzw[7] = one;
    xyzw[8] = 0x43a00000ul; xyzw[9] = 0x43480000ul;
    xyzw[10] = 0ul;         xyzw[11] = one;
    colors[0] = 0xffffffful;
    colors[1] = 0xffffffful;
    colors[2] = 0xffffffful;
    uv[0] = 0ul;  uv[1] = 0ul;
    uv[2] = one;  uv[3] = 0ul;
    uv[4] = 0ul;  uv[5] = one;

    CHECK(v9x_i9xx_build_textured_runtime_run(xyzw, colors, uv, 1ul,
                                              width, height, stream + at,
                                              400ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;

    /* Declared 4444, built 4444. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_OK);

    /* Declared 565 - explicitly, and by saying nothing. */
    {
        struct v9x_i9xx_decode_limits wrong = limits;

        wrong.texture_format = V9X_I9XX_MAPSURF_16BIT_RGB565;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        wrong.texture_format = 0ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        /* Declared 1555 over a 4444 stream: two alpha types are still two
         * types. */
        wrong.texture_format = V9X_I9XX_MAPSURF_16BIT_ARGB1555;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        /* A declared format outside the three is refused before the compare,
         * whatever the stream carries. */
        wrong.texture_format = 0x00000118ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        /* The sampler words: a stream built clamp/nearest fails when the
         * limits declare wrap or linear, one field at a time. */
        wrong = limits;
        wrong.texture_wrap = 1ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
        wrong = limits;
        wrong.texture_mag_linear = 1ul;
        CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &wrong, &index) ==
              V9X_I9XX_P5_TEXTURE_STATE);
    }

    /*
     * And the other way: built wrap and linear, accepted only when declared
     * so. The same stream shape, so the map and vertex checks are unchanged
     * and only SS2/SS3 differ.
     */
    map.wrap = 1ul;
    map.mag_linear = 1ul;
    map.min_linear = 1ul;
    at = 0ul;
    CHECK(v9x_i9xx_build_runtime_state(surface, pitch, width, height, &map,
                                       0ul, 0ul, 0ul, 0ul, 0ul,
                                       stream + at, 400ul - at,
                                       &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_modulate_program(stream + at, 400ul - at,
                                          &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_textured_runtime_run(xyzw, colors, uv, 1ul,
                                              width, height, stream + at,
                                              400ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_TEXTURE_STATE);
    limits.texture_wrap = 1ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_TEXTURE_STATE);
    limits.texture_mag_linear = 1ul;
    /* MAG alone is not the stream: MIN is its own field. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_TEXTURE_STATE);
    limits.texture_min_linear = 1ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_OK);

    /*
     * The declared PROGRAM. The same stream with the alpha-keeping program
     * in place of the original is three dwords longer, and the decoder
     * accepts it only when the engine said which program it built. A 4444
     * map is the case where legacy MODULATE takes the texel's alpha.
     */
    at = 0ul;
    CHECK(v9x_i9xx_build_runtime_state(surface, pitch, width, height, &map,
                                       0ul, 0ul, 0ul, 0ul, 0ul,
                                       stream + at, 400ul - at,
                                       &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_texture_program(V9X_I9XX_TEXPROG_MODULATE_TEXALPHA,
                                         stream + at, 400ul - at,
                                         &produced) == V9X_STATUS_OK);
    CHECK(produced == 19ul);
    at += produced;
    CHECK(v9x_i9xx_build_textured_runtime_run(xyzw, colors, uv, 1ul,
                                              width, height, stream + at,
                                              400ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;
    /* Declared the original program: the length is wrong. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_SHADER);
    limits.texture_program = V9X_I9XX_TEXPROG_MODULATE_TEXALPHA;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_OK);
    /* A declaration the builder has no program for matches nothing. */
    limits.texture_program = 3ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_SHADER);
    limits.texture_program = 0ul;
}

/*
 * The runtime stream blends the one measured way, and only when declared.
 *
 * Added 2026-09-18 for 3DMark99 and the alpha textures. The blend scene
 * measured the packet (intel47); this pins the runtime builder to the same
 * two dwords - the IAB disable ahead of the load, and S6 with BLEND_ENABLE,
 * ADD, SRC_ALPHA, INV_SRC_ALPHA - and the decoder to the engine's word:
 * a blending stream declared opaque is refused at the IAB dword, and an
 * opaque stream declared blending is refused for the missing packet.
 */
static void test_runtime_blend(void)
{
    struct v9x_i9xx_decode_limits limits;
    v9x_u32 stream[400];
    v9x_u32 xyzw[3ul * 4ul];
    v9x_u32 colors[3];
    v9x_u32 produced = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 index = 0ul;
    v9x_u32 scan;
    v9x_u32 iab_seen = 0ul;
    const v9x_u32 one = 0x3f800000ul;
    const v9x_u32 surface = 0x00200000ul;
    const v9x_u32 pitch = 1024ul;
    const v9x_u32 width = 512ul;
    const v9x_u32 height = 384ul;

    /* One dword more than the opaque block, and only that. */
    CHECK(v9x_i9xx_runtime_state_extent(0ul, 0ul, 1ul) ==
          v9x_i9xx_runtime_state_extent(0ul, 0ul, 0ul) + 1ul);
    CHECK(v9x_i9xx_runtime_state_extent(1ul, 1ul, 1ul) ==
          v9x_i9xx_runtime_state_extent(1ul, 1ul, 0ul) + 1ul);

    v9x_test_limits(&limits, surface, pitch * height,
                    V9X_I9XX_SCENE_RUNTIME);
    limits.target_pitch = pitch;
    limits.target_width = width;
    limits.target_height = height;
    limits.blend_src = V9X_I9XX_BLENDFACT_SRC_ALPHA;
    limits.blend_dst = V9X_I9XX_BLENDFACT_INV_SRC_ALPHA;

    CHECK(v9x_i9xx_build_runtime_state(surface, pitch, width, height, 0,
                                       0ul, 0ul, 0ul,
                                       V9X_I9XX_BLENDFACT_SRC_ALPHA,
                                       V9X_I9XX_BLENDFACT_INV_SRC_ALPHA,
                                       stream + at, 400ul - at,
                                       &produced) == V9X_STATUS_OK);
    CHECK(produced == v9x_i9xx_runtime_state_extent(0ul, 0ul, 1ul));
    /* The IAB disable is in the block, once, as the exact dword, and the
     * S6 word that follows the load carries the measured blend bits. */
    for (scan = 0ul; scan < produced; ++scan) {
        if (stream[scan] == V9X_I9XX_IAB_DISABLE_DWORD) {
            ++iab_seen;
            CHECK((stream[scan + 6ul] & V9X_I9XX_S6_BLEND_ENABLE) != 0ul);
            CHECK(((stream[scan + 6ul] >> V9X_I9XX_S6_SRC_FACTOR_SHIFT) &
                   0xful) == V9X_I9XX_BLENDFACT_SRC_ALPHA);
            CHECK(((stream[scan + 6ul] >> V9X_I9XX_S6_DST_FACTOR_SHIFT) &
                   0xful) == V9X_I9XX_BLENDFACT_INV_SRC_ALPHA);
        }
    }
    CHECK(iab_seen == 1ul);
    at += produced;
    CHECK(v9x_i9xx_build_fragment_program(stream + at, 400ul - at,
                                          &produced) == V9X_STATUS_OK);
    at += produced;

    xyzw[0] = 0x43204000ul; xyzw[1] = 0x42f00000ul;
    xyzw[2] = 0ul;          xyzw[3] = one;
    xyzw[4] = 0x43c80000ul; xyzw[5] = 0x42f00000ul;
    xyzw[6] = 0ul;          xyzw[7] = one;
    xyzw[8] = 0x43a00000ul; xyzw[9] = 0x43480000ul;
    xyzw[10] = 0ul;         xyzw[11] = one;
    colors[0] = 0x80ffffful;
    colors[1] = 0x80ffffful;
    colors[2] = 0x80ffffful;
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, width, height,
                                     stream + at, 400ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;

    /* Declared blending, built blending. */
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_OK);
    /* Declared opaque: the IAB dword is the first thing refused. */
    limits.blend_src = 0ul;
    limits.blend_dst = 0ul;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_TEXTURE_STATE);
    /* Declared a DIFFERENT pair: the factors are two fields, not a flag. */
    limits.blend_src = V9X_I9XX_BLENDFACT_ONE;
    limits.blend_dst = V9X_I9XX_BLENDFACT_INV_SRC_ALPHA;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_DEPTH_FORBIDDEN);
    limits.blend_src = 0ul;
    limits.blend_dst = 0ul;

    /* Built opaque, declared blending: no IAB disable, refused at the
     * primitive for the missing packet - or earlier at S6, which lacks the
     * enable; either way not OK. */
    at = 0ul;
    CHECK(v9x_i9xx_build_runtime_state(surface, pitch, width, height, 0,
                                       0ul, 0ul, 0ul, 0ul, 0ul,
                                       stream + at, 400ul - at,
                                       &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_fragment_program(stream + at, 400ul - at,
                                          &produced) == V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_build_runtime_run(xyzw, colors, 1ul, width, height,
                                     stream + at, 400ul - at, &produced) ==
          V9X_STATUS_OK);
    at += produced;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) ==
          V9X_I9XX_P5_OK);
    limits.blend_src = V9X_I9XX_BLENDFACT_SRC_ALPHA;
    limits.blend_dst = V9X_I9XX_BLENDFACT_INV_SRC_ALPHA;
    CHECK(v9x_i9xx_decode_phase5_stream(stream, at, &limits, &index) !=
          V9X_I9XX_P5_OK);
}

/*
 * The blend factors are two fields, and every pairing of the four published
 * codes is built and decoded as itself.
 *
 * Review of the first blend commit: source and destination caps describe
 * independently selectable factors, so publishing SRCALPHA|ONE against
 * INVSRCALPHA|ZERO promised SRCALPHA/ZERO and ONE/INVSRCALPHA too, and both
 * drew opaque. Each pair now lands in S6's two 4-bit fields as declared. A
 * code outside the four is refused by the builder, and half a pair is
 * refused as well.
 */
static void test_runtime_blend_pairs(void)
{
    static const v9x_u32 codes[4] = {
        V9X_I9XX_BLENDFACT_ZERO, V9X_I9XX_BLENDFACT_ONE,
        V9X_I9XX_BLENDFACT_SRC_ALPHA, V9X_I9XX_BLENDFACT_INV_SRC_ALPHA
    };
    v9x_u32 stream[64];
    v9x_u32 produced = 0ul;
    v9x_u32 scan;
    v9x_u32 s;
    v9x_u32 d;

    CHECK(v9x_i9xx_blend_factor_known(V9X_I9XX_BLENDFACT_ZERO) != V9X_FALSE);
    CHECK(v9x_i9xx_blend_factor_known(V9X_I9XX_BLENDFACT_ONE) != V9X_FALSE);
    CHECK(v9x_i9xx_blend_factor_known(V9X_I9XX_BLENDFACT_SRC_ALPHA) !=
          V9X_FALSE);
    CHECK(v9x_i9xx_blend_factor_known(V9X_I9XX_BLENDFACT_INV_SRC_ALPHA) !=
          V9X_FALSE);
    /* DST_ALPHA (3) and INV_DST_ALPHA (4) exist on the part and are what
     * the audit excludes; 0 is "off" and never a factor. */
    CHECK(v9x_i9xx_blend_factor_known(0ul) == V9X_FALSE);
    CHECK(v9x_i9xx_blend_factor_known(3ul) == V9X_FALSE);
    CHECK(v9x_i9xx_blend_factor_known(4ul) == V9X_FALSE);

    for (s = 0ul; s < 4ul; ++s) {
        for (d = 0ul; d < 4ul; ++d) {
            v9x_u32 seen = 0ul;

            CHECK(v9x_i9xx_build_runtime_state(0x00200000ul, 1024ul, 512ul,
                                               384ul, 0, 0ul, 0ul, 0ul,
                                               codes[s], codes[d],
                                               stream, 64ul, &produced) ==
                  V9X_STATUS_OK);
            for (scan = 0ul; scan < produced; ++scan) {
                if (stream[scan] == V9X_I9XX_IAB_DISABLE_DWORD) {
                    v9x_u32 s6 = stream[scan + 6ul];

                    ++seen;
                    CHECK((s6 & V9X_I9XX_S6_BLEND_ENABLE) != 0ul);
                    CHECK(((s6 >> V9X_I9XX_S6_SRC_FACTOR_SHIFT) & 0xful) ==
                          codes[s]);
                    CHECK(((s6 >> V9X_I9XX_S6_DST_FACTOR_SHIFT) & 0xful) ==
                          codes[d]);
                }
            }
            CHECK(seen == 1ul);
        }
    }

    /* Half a pair, and a code the audit excludes. */
    CHECK(v9x_i9xx_build_runtime_state(0x00200000ul, 1024ul, 512ul, 384ul,
                                       0, 0ul, 0ul, 0ul,
                                       V9X_I9XX_BLENDFACT_SRC_ALPHA, 0ul,
                                       stream, 64ul, &produced) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_runtime_state(0x00200000ul, 1024ul, 512ul, 384ul,
                                       0, 0ul, 0ul, 0ul,
                                       V9X_I9XX_BLENDFACT_SRC_ALPHA, 3ul,
                                       stream, 64ul, &produced) !=
          V9X_STATUS_OK);
}

/*
 * The three textured programs, told apart by what they do with alpha.
 *
 * Review of the first blend commit: the one program multiplied all four
 * channels, which is MODULATEALPHA; legacy MODULATE takes the texture's
 * alpha (or the vertex's, for a format without one). The two new programs
 * share the modulate program's first four instructions dword for dword,
 * mask the MUL to xyz, and add a W-only MOV whose source is the difference
 * between them. The decoder pins a runtime stream to the declared program's
 * length, so the two new ones - equal in length - are declared by kind and
 * the original by default.
 */
/*
 * The ring flip stream: four dwords, exactly, for the declared plane, pitch
 * and base, and refused for anything the plane register itself would not
 * hold. docs\plans\intel-gen3-ring-flip.md; the constants are i915_reg.h's
 * and unmeasured on the part.
 */
static void test_flip_stream(void)
{
    v9x_u32 stream[8];
    v9x_u32 written = 0ul;
    v9x_u32 index = 99ul;

    CHECK(v9x_i9xx_flip_stream_extent() == 4ul);
    CHECK(v9x_i9xx_build_flip_stream(1ul, 2048ul, 0x00096000ul, 0x00800000ul,
                                     stream, 8ul, &written) == V9X_STATUS_OK);
    CHECK(written == 4ul);
    /* MI_INSTR(0x14, 1) with plane B in bits 21:20: 0x0a000001 | 1 << 20. */
    CHECK(stream[0] == 0x0a100001ul);
    CHECK(stream[1] == 2048ul);
    CHECK(stream[2] == 0x00096000ul);
    CHECK(stream[3] == V9X_I9XX_MI_NOOP);
    CHECK(v9x_i9xx_decode_flip_stream(stream, 4ul, 1ul, 2048ul, 0x00096000ul,
                                      0x00800000ul, &index) != V9X_FALSE);
    CHECK(index == 0ul);
    /* Plane A: no plane bits. */
    CHECK(v9x_i9xx_build_flip_stream(0ul, 1280ul, 0ul, 0x00800000ul,
                                     stream, 8ul, &written) == V9X_STATUS_OK);
    CHECK(stream[0] == 0x0a000001ul);
    CHECK(stream[2] == 0ul);

    /* Every argument the plane could not hold is refused, not rounded:
     * plane 2, a pitch that is not a multiple of 64, a base that is not a
     * dword, a base outside the framebuffer. */
    CHECK(v9x_i9xx_build_flip_stream(2ul, 2048ul, 0ul, 0x00800000ul,
                                     stream, 8ul, &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_flip_stream(1ul, 2040ul, 0ul, 0x00800000ul,
                                     stream, 8ul, &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_flip_stream(1ul, 0ul, 0ul, 0x00800000ul,
                                     stream, 8ul, &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_flip_stream(1ul, 2048ul, 2ul, 0x00800000ul,
                                     stream, 8ul, &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_flip_stream(1ul, 2048ul, 0x00800000ul, 0x00800000ul,
                                     stream, 8ul, &written) != V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_flip_stream(1ul, 2048ul, 0ul, 0x00800000ul,
                                     stream, 3ul, &written) != V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_flip_stream(1ul, 2048ul, 0ul, 0x00800000ul,
                                     0, 8ul, &written) != V9X_STATUS_OK);

    /* The decoder names the dword that differs, and refuses a stream of the
     * wrong length or arguments the builder would refuse. */
    CHECK(v9x_i9xx_build_flip_stream(1ul, 2048ul, 0x00096000ul, 0x00800000ul,
                                     stream, 8ul, &written) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_decode_flip_stream(stream, 4ul, 0ul, 2048ul, 0x00096000ul,
                                      0x00800000ul, &index) == V9X_FALSE);
    CHECK(index == 0ul);
    CHECK(v9x_i9xx_decode_flip_stream(stream, 4ul, 1ul, 1024ul, 0x00096000ul,
                                      0x00800000ul, &index) == V9X_FALSE);
    CHECK(index == 1ul);
    CHECK(v9x_i9xx_decode_flip_stream(stream, 4ul, 1ul, 2048ul, 0x00097000ul,
                                      0x00800000ul, &index) == V9X_FALSE);
    CHECK(index == 2ul);
    stream[3] = V9X_I9XX_MI_FLUSH;
    CHECK(v9x_i9xx_decode_flip_stream(stream, 4ul, 1ul, 2048ul, 0x00096000ul,
                                      0x00800000ul, &index) == V9X_FALSE);
    CHECK(index == 3ul);
    stream[3] = V9X_I9XX_MI_NOOP;
    CHECK(v9x_i9xx_decode_flip_stream(stream, 3ul, 1ul, 2048ul, 0x00096000ul,
                                      0x00800000ul, &index) == V9X_FALSE);
    CHECK(v9x_i9xx_decode_flip_stream(stream, 4ul, 1ul, 2048ul, 0x00096000ul,
                                      0x00090000ul, &index) == V9X_FALSE);

    /* The pending bit follows the plane: ISR bits 11 and 10, the audited
     * values, and NOT the MI_WAIT_FOR_EVENT operand bits 2 and 6 that the
     * first cut used and intel71 measured as never set. */
    CHECK(v9x_i9xx_flip_pending_bit(0ul) == 0x00000800ul);
    CHECK(v9x_i9xx_flip_pending_bit(1ul) == 0x00000400ul);
    CHECK(v9x_i9xx_flip_pending_bit(2ul) == 0ul);
}

static void test_texture_programs(void)
{
    v9x_u32 base[24];
    v9x_u32 tex[24];
    v9x_u32 dif[24];
    v9x_u32 written = 0ul;
    v9x_u32 index;

    CHECK(v9x_i9xx_texture_program_extent(V9X_I9XX_TEXPROG_MODULATE_ALPHA) ==
          v9x_i9xx_modulate_program_extent());
    CHECK(v9x_i9xx_texture_program_extent(
              V9X_I9XX_TEXPROG_MODULATE_TEXALPHA) == 19ul);
    CHECK(v9x_i9xx_texture_program_extent(
              V9X_I9XX_TEXPROG_MODULATE_DIFFALPHA) == 19ul);
    CHECK(v9x_i9xx_texture_program_extent(3ul) == 0ul);

    /* Kind 0 is the original, dword for dword. */
    CHECK(v9x_i9xx_build_modulate_program(base, 24ul, &written) ==
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_texture_program(V9X_I9XX_TEXPROG_MODULATE_ALPHA,
                                         tex, 24ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 16ul);
    for (index = 0ul; index < 16ul; ++index) {
        CHECK(tex[index] == base[index]);
    }

    CHECK(v9x_i9xx_build_texture_program(V9X_I9XX_TEXPROG_MODULATE_TEXALPHA,
                                         tex, 24ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 19ul);
    CHECK(v9x_i9xx_build_texture_program(V9X_I9XX_TEXPROG_MODULATE_DIFFALPHA,
                                         dif, 24ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 19ul);

    /* Header: six instructions, 18 payload dwords, length 17. */
    CHECK(tex[0] == (V9X_I9XX_3DSTATE_PIXEL_SHADER | 17ul));
    /* The first four instructions are the modulate program's. */
    for (index = 1ul; index < 13ul; ++index) {
        CHECK(tex[index] == base[index]);
        CHECK(dif[index] == base[index]);
    }
    /* The MUL: the modulate MUL with the destination mask xyz (0x1c00) in
     * place of xyzw (0x3c00); A1 and A2 unchanged. */
    CHECK(tex[13] == 0x03201c00ul);
    CHECK(tex[14] == base[14]);
    CHECK(tex[15] == base[15]);
    /* The MOV: dest oC, mask W only (0x2000), identity swizzle, no src1.
     * TEXALPHA reads R0 (type 0, nr 0: nothing added); DIFFALPHA reads T8
     * (type 1 at shift 7, nr 8 at shift 2). */
    CHECK(tex[16] == 0x02202000ul);
    CHECK(tex[17] == V9X_I9XX_FS_A1_SWIZZLE_XYZW);
    CHECK(tex[18] == 0ul);
    CHECK(dif[16] == 0x022020a0ul);
    CHECK(dif[17] == V9X_I9XX_FS_A1_SWIZZLE_XYZW);
    CHECK(dif[18] == 0ul);
    /* And the two differ in exactly that one dword. */
    for (index = 0ul; index < 19ul; ++index) {
        if (index != 16ul) {
            CHECK(tex[index] == dif[index]);
        }
    }

    /* Refusals. */
    CHECK(v9x_i9xx_build_texture_program(3ul, tex, 24ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_texture_program(V9X_I9XX_TEXPROG_MODULATE_TEXALPHA,
                                         tex, 18ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
}

/* SAMPLER_STATE. */
static void test_sampler_state(void)
{
    struct v9x_i9xx_texture maps[2];
    v9x_u32 stream[32];
    v9x_u32 written = 0ul;

    maps[0].offset = 0x00700000ul; maps[0].width = 16ul;
    maps[0].height = 16ul;         maps[0].pitch = 32ul;
    maps[0].format = V9X_I9XX_MAPSURF_16BIT_RGB565;
    maps[0].wrap = 0ul;
    maps[0].mag_linear = 0ul;
    maps[0].min_linear = 0ul;
    maps[1] = maps[0];

    CHECK(v9x_i9xx_sampler_state_extent(1ul) == 5ul);
    CHECK(v9x_i9xx_build_sampler_state(maps, 1ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 5ul);

    /* Command 0x7D010000 - the same major opcode as MAP_STATE with sub 1. */
    CHECK(stream[0] == 0x7d010003ul);
    CHECK(stream[1] == 0x00000001ul);

    /* SS2: nearest, no mips. Every filter field clear. */
    CHECK(stream[2] == 0ul);

    /*
     * SS3: normalized coordinates (1<<5), clamp-to-edge (2) on x, y and z at
     * shifts 12, 9 and 6, and map index 0 at shift 1.
     * 0x20 | (2<<12) | (2<<9) | (2<<6) = 0x20 | 0x2000 | 0x400 | 0x80.
     */
    CHECK(stream[3] == 0x000024a0ul);
    CHECK((stream[3] & V9X_I9XX_SS3_NORMALIZED_COORDS) != 0ul);

    /* SS4: the border colour, inert under clamp-to-edge. */
    CHECK(stream[4] == 0ul);

    /*
     * The map index is written, not assumed. A second sampler names map 1,
     * and if the field were left zero both would read map 0 - which is the
     * assumption the audit records both trees as refusing to make.
     */
    CHECK(v9x_i9xx_build_sampler_state(maps, 2ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 8ul);
    CHECK(stream[1] == 0x00000003ul);
    CHECK(((stream[3] >> V9X_I9XX_SS3_MAP_INDEX_SHIFT) & 0xful) == 0ul);
    CHECK(((stream[6] >> V9X_I9XX_SS3_MAP_INDEX_SHIFT) & 0xful) == 1ul);

    /*
     * WRAP and LINEAR, each one field, each from the map. Added with
     * intel62: Final Reality asked for both and the sampler gave it clamp
     * and nearest. WRAP is mode 0 on all three axes, so SS3 keeps only the
     * normalized bit and the index; LINEAR is 1 at the MIN (14) and MAG (17)
     * shifts with the mip field still zero, and the two are INDEPENDENT -
     * MIN=LINEAR with MAG=NEAREST is a real request and sets bit 14 alone.
     */
    maps[0].wrap = 1ul;
    CHECK(v9x_i9xx_build_sampler_state(maps, 1ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);
    CHECK(stream[2] == 0ul);
    CHECK(stream[3] == 0x00000020ul);
    maps[0].mag_linear = 1ul;
    CHECK(v9x_i9xx_build_sampler_state(maps, 1ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);
    CHECK(stream[2] == 0x00020000ul);
    CHECK(stream[3] == 0x00000020ul);
    maps[0].mag_linear = 0ul;
    maps[0].min_linear = 1ul;
    CHECK(v9x_i9xx_build_sampler_state(maps, 1ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);
    CHECK(stream[2] == 0x00004000ul);
    maps[0].mag_linear = 1ul;
    maps[0].wrap = 0ul;
    CHECK(v9x_i9xx_build_sampler_state(maps, 1ul, stream, 32ul, &written) ==
          V9X_STATUS_OK);
    CHECK(stream[2] == 0x00024000ul);
    CHECK(stream[3] == 0x000024a0ul);
    CHECK(v9x_i9xx_sampler_filter_word(0ul, 0ul) == 0ul);
    CHECK(v9x_i9xx_sampler_filter_word(1ul, 0ul) == 0x00004000ul);
    CHECK(v9x_i9xx_sampler_filter_word(0ul, 1ul) == 0x00020000ul);
    CHECK(v9x_i9xx_sampler_filter_word(1ul, 1ul) == 0x00024000ul);
    maps[0].mag_linear = 0ul;
    maps[0].min_linear = 0ul;

    /* Refusals, both sides of the unit bound and of the capacity. */
    CHECK(v9x_i9xx_build_sampler_state(maps, 0ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_sampler_state(maps, 9ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_sampler_state(maps, 1ul, stream, 4ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_sampler_state(maps, 1ul, 0, 32ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_sampler_state(0, 1ul, stream, 32ul, &written) !=
          V9X_STATUS_OK);
}

/*
 * The sampling fragment program.
 *
 * Three instructions, not four: texld writes the output colour directly,
 * which xf86 does in terms.
 */
/*
 * The MODULATE program, dword by dword against the audit's own arithmetic.
 *
 * The audit derived every field from the two trees' macros and wrote the
 * results down; this asserts the builder produces those numbers. That is the
 * only check available off the machine, and it is worth having precisely
 * because the failure mode here is a program the parser accepts and a picture
 * that is wrong in a plausible way.
 */
static void test_modulate_program(void)
{
    v9x_u32 stream[24];
    v9x_u32 written = 0ul;

    CHECK(v9x_i9xx_modulate_program_extent() == 16ul);
    CHECK(v9x_i9xx_build_modulate_program(stream, 24ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 16ul);

    CHECK(stream[0] == (V9X_I9XX_3DSTATE_PIXEL_SHADER | 14ul));

    /* dcl T0, then dcl S0 with no channel mask, then dcl T8 - the same three
     * dwords the sampling and untextured programs emit for the same
     * registers, which is the point: only the arithmetic is new. */
    CHECK(stream[1] == 0x19083c00ul);
    CHECK(stream[4] == 0x19180000ul);
    CHECK((stream[4] & V9X_I9XX_FS_CHANNEL_ALL) == 0ul);
    CHECK(stream[7] == 0x190a3c00ul);

    /*
     * texld R0, not oC. The sampling program writes oC directly because it
     * may; here the texel is an operand, and a texld that wrote oC would
     * produce the unmodulated picture - which is exactly what the previous
     * scene already draws, so it would look like a pass.
     */
    CHECK(stream[10] == 0x15000000ul);
    CHECK((stream[10] & (7ul << V9X_I9XX_T0_DEST_TYPE_SHIFT)) == 0ul);
    CHECK(stream[11] == 0x01000000ul);
    CHECK(stream[12] == 0ul);

    /*
     * mul oC, R0, T8 - the three dwords the audit derived:
     *   A0 = A0_MUL | dest oC | channel all | src0 R0
     *   A1 = src0 swizzle xyzw | src1 type T, nr 8, X and Y
     *   A2 = src1 Z and W
     */
    CHECK(stream[13] == 0x03203c00ul);
    CHECK(stream[14] == 0x01232801ul);
    CHECK(stream[15] == 0x23000000ul);

    /*
     * And the split stated as a property rather than as three magic numbers.
     * src1's Z and W selectors are in A2 and NOT in A1: a builder that put
     * all four in A1 would leave A2 zero, which reads as src1.zw = xx and
     * multiplies by (r, g, r, r).
     */
    CHECK(stream[15] != 0ul);
    CHECK(((stream[15] >> 28) & 0xful) == 2ul);   /* Z selects channel 2 */
    CHECK(((stream[15] >> 24) & 0xful) == 3ul);   /* W selects channel 3 */
    CHECK(((stream[14] >> 4) & 0xful) == 0ul);    /* X selects channel 0 */
    CHECK((stream[14] & 0xful) == 1ul);           /* Y selects channel 1 */

    /* Five instructions of three dwords plus a header. */
    CHECK((written - 1ul) % 3ul == 0ul);
    CHECK((written - 1ul) / 3ul == 5ul);

    /* Distinct from both other programs, in length and therefore in the
     * decoder's per-mode length check. */
    CHECK(v9x_i9xx_modulate_program_extent() !=
          v9x_i9xx_sampling_program_extent());
    CHECK(v9x_i9xx_modulate_program_extent() !=
          v9x_i9xx_fragment_program_extent());

    /* Refusals. */
    CHECK(v9x_i9xx_build_modulate_program(stream, 15ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_modulate_program(0, 24ul, &written) !=
          V9X_STATUS_OK);
}

static void test_sampling_program(void)
{
    v9x_u32 stream[16];
    v9x_u32 written = 0ul;
    v9x_u32 index;

    CHECK(v9x_i9xx_sampling_program_extent() == 10ul);
    CHECK(v9x_i9xx_build_sampling_program(stream, 16ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 10ul);

    /* Header, length = payload - 1. */
    CHECK(stream[0] == (V9X_I9XX_3DSTATE_PIXEL_SHADER | 8ul));

    /* dcl T0, all four channels: 0x19000000 | (1<<19) | (0<<14) | 0x3C00. */
    CHECK(stream[1] == 0x19083c00ul);
    CHECK(stream[2] == 0ul);
    CHECK(stream[3] == 0ul);

    /*
     * dcl S0 - and NO channel mask. Asserted explicitly, because a sampler
     * carrying one would be four bits of meaning nobody derived, and it is
     * the one declaration that differs from every other.
     */
    CHECK(stream[4] == 0x19180000ul);
    CHECK((stream[4] & V9X_I9XX_FS_CHANNEL_ALL) == 0ul);
    CHECK(stream[5] == 0ul);
    CHECK(stream[6] == 0ul);

    /* texld oC <- S0 using T0: 0x15000000 | (4<<19). */
    CHECK(stream[7] == 0x15200000ul);
    /* T1: coordinate register type 1 at shift 24, number 0 at shift 17. */
    CHECK(stream[8] == 0x01000000ul);
    /* T2 must be zero. */
    CHECK(stream[9] == 0ul);

    /* It is three instructions of three dwords plus a header, and every
     * instruction is three dwords - asserted so a fourth cannot creep in
     * without the extent and the body disagreeing. */
    CHECK((written - 1ul) % 3ul == 0ul);
    CHECK((written - 1ul) / 3ul == 3ul);

    /* Refusals. */
    CHECK(v9x_i9xx_build_sampling_program(stream, 9ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_sampling_program(0, 16ul, &written) !=
          V9X_STATUS_OK);

    /* And it differs from the untextured program, which it replaces rather
     * than extends. */
    for (index = 0ul; index < 16ul; ++index) { stream[index] = 0ul; }
    CHECK(v9x_i9xx_sampling_program_extent() !=
          v9x_i9xx_fragment_program_extent());
}

/*
 * The GPU-painted texture.
 *
 * Four quadrant blits and a flush. The CPU never touches the aperture, which
 * is the condition the errata gate opened on.
 */
static void test_texture_paint(void)
{
    struct v9x_i9xx_texture texture;
    v9x_u32 stream[40];
    v9x_u32 written = 0ul;
    v9x_u32 quadrant;
    v9x_u32 seen[4];

    texture.offset = 0x00759000ul;
    texture.width = V9X_I9XX_TEXTURE_WIDTH;
    texture.height = V9X_I9XX_TEXTURE_HEIGHT;
    texture.pitch = V9X_I9XX_TEXTURE_PITCH;
    texture.format = V9X_I9XX_MAPSURF_16BIT_RGB565;
    texture.wrap = 0ul;
    texture.mag_linear = 0ul;
    texture.min_linear = 0ul;

    CHECK(v9x_i9xx_texture_paint_extent() == 25ul);
    CHECK(v9x_i9xx_build_texture_paint(&texture, stream, 40ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 25ul);

    /* Four blits of six dwords, then the flush. */
    for (quadrant = 0ul; quadrant < 4ul; ++quadrant) {
        v9x_u32 base = quadrant * 6ul;

        CHECK(stream[base] == V9X_I9XX_XY_COLOR_BLT);
        /* 16 texels wide is EIGHT dwords - the blit counts dwords and two
         * 16-bit texels share one. Getting that backwards would paint half
         * the texture and leave the rest whatever the page held. */
        CHECK(stream[base + 3ul] ==
              ((V9X_I9XX_TEXTURE_BLOCK << 16) | (V9X_I9XX_TEXTURE_BLOCK / 2ul)));
        seen[quadrant] = stream[base + 5ul];
    }
    CHECK(stream[24] == V9X_I9XX_MI_FLUSH);

    /*
     * The four colours must all DIFFER, or a probe could not say which
     * quadrant it read and the whole addressing experiment is vacuous.
     * Asserted rather than trusted to the table.
     */
    CHECK(seen[0] != seen[1]);
    CHECK(seen[0] != seen[2]);
    CHECK(seen[0] != seen[3]);
    CHECK(seen[1] != seen[2]);
    CHECK(seen[1] != seen[3]);
    CHECK(seen[2] != seen[3]);

    /* Each is a 565 value doubled into a dword: both halves the same, since
     * two texels share the dword the blit fills. */
    for (quadrant = 0ul; quadrant < 4ul; ++quadrant) {
        CHECK((seen[quadrant] & 0xfffful) ==
              ((seen[quadrant] >> 16) & 0xfffful));
        CHECK(v9x_i9xx_texture_quadrant_color(quadrant) ==
              (seen[quadrant] & 0xfffful));
    }
    /* And every one is a colour this chip is MEASURED to store, so a probe
     * reading the wrong quadrant is an addressing fault and not a conversion
     * question. */
    CHECK(v9x_i9xx_texture_quadrant_color(0ul) == 0x1c3eul);
    CHECK(v9x_i9xx_texture_quadrant_color(1ul) == 0xf325ul);
    CHECK(v9x_i9xx_texture_quadrant_color(2ul) == 0x3038ul);
    CHECK(v9x_i9xx_texture_quadrant_color(3ul) == 0x07e0ul);
    /*
     * And none of them is the FILL. A quadrant painted the fill colour would
     * read the same whether the sampler worked or nothing drew at all, so the
     * scene would report a pass for a draw that never happened.
     */
    for (quadrant = 0ul; quadrant < 4ul; ++quadrant) {
        CHECK(v9x_i9xx_texture_quadrant_color(quadrant) !=
              (V9X_I9XX_FILL_DWORD & 0xfffful));
        CHECK(seen[quadrant] != V9X_I9XX_FILL_DWORD);
    }

    /* Not a quadrant: zero, which no caller may read as a colour. */
    CHECK(v9x_i9xx_texture_quadrant_color(4ul) == 0ul);

    /*
     * The quadrants must tile the texture: destinations 0, 32, 1024, 1056
     * from the base, which is one half-row across and one half-height down.
     * The last ends on the texture's final byte.
     */
    CHECK(stream[4] == texture.offset);
    CHECK(stream[10] == texture.offset + 32ul);
    CHECK(stream[16] == texture.offset + 1024ul);
    CHECK(stream[22] == texture.offset + 1056ul);

    /* Refusals. */
    CHECK(v9x_i9xx_build_texture_paint(&texture, stream, 24ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_texture_paint(0, stream, 40ul, &written) !=
          V9X_STATUS_OK);
    /* A texture the quadrant arithmetic was not written for is refused rather
     * than painted with the wrong geometry. */
    texture.width = 64ul;
    CHECK(v9x_i9xx_build_texture_paint(&texture, stream, 40ul, &written) !=
          V9X_STATUS_OK);
}

/* The textured vertex run: seven dwords per vertex, coordinates last. */
static void test_textured_run(void)
{
    struct v9x_i9xx_triangle triangle;
    struct v9x_i9xx_scene scene;
    v9x_u32 u[6];
    v9x_u32 v[6];
    v9x_u32 stream[64];
    v9x_u32 written = 0ul;
    v9x_u32 index;

    CHECK(v9x_i9xx_scene_at(0ul, &scene) == V9X_STATUS_OK);
    triangle = scene.triangles[0];
    for (index = 0ul; index < 6ul; ++index) {
        u[index] = 0x3f800000ul;   /* 1.0f */
        v[index] = 0x00000000ul;   /* 0.0f */
    }

    CHECK(v9x_i9xx_textured_run_dwords(1ul) == 22ul);
    CHECK(v9x_i9xx_build_textured_run(&triangle, 1ul, u, v, 640ul, 480ul,
                                      stream, 64ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 22ul);

    /* One primitive command, length = every vertex dword less one: 21 - 1. */
    CHECK(stream[0] == (V9X_I9XX_3DPRIMITIVE_INLINE |
                        V9X_I9XX_PRIM3D_TRILIST | 20ul));

    /*
     * Vertex 0: x, y, z, w, colour, u, v. The coordinates come AFTER the
     * colour, which is the ordering the audit took from Mesa's fixed
     * attribute sequence - putting them before it would be a plausible
     * guess that drew a wrong picture with no error.
     */
    CHECK(stream[1] == 0x43200000ul);        /* x = 160 */
    CHECK(stream[2] == 0x42f00000ul);        /* y = 120 */
    CHECK(stream[3] == 0x00000000ul);        /* z */
    CHECK(stream[4] == 0x3f800000ul);        /* w = 1 */
    CHECK(stream[5] == triangle.color);      /* the packed colour */
    CHECK(stream[6] == 0x3f800000ul);        /* u */
    CHECK(stream[7] == 0x00000000ul);        /* v */

    /*
     * Vertex 1 begins seven dwords later, not five - asserted against the
     * STREAM, which is the only form of this claim that can fail. Comparing
     * the two constants with each other cannot: the compiler calls the
     * failure branch unreachable, and that is the fourth such tautology this
     * file has grown and had removed.
     */
    CHECK(stream[1ul + V9X_I9XX_TEXTURED_VERTEX_DWORDS] == 0x43f00000ul);

    /* Two triangles, and the run grows by one triangle's worth. */
    CHECK(v9x_i9xx_textured_run_dwords(2ul) == 43ul);
    CHECK(v9x_i9xx_textured_run_dwords(0ul) == 0ul);
    CHECK(v9x_i9xx_textured_run_dwords(V9X_I9XX_SCENE_MAX_TRIANGLES + 1ul) ==
          0ul);

    /* Refusals, including the null coordinate arrays - a textured run with no
     * coordinates is not a shorter run, it is a mistake. */
    CHECK(v9x_i9xx_build_textured_run(&triangle, 1ul, 0, v, 640ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_textured_run(&triangle, 1ul, u, 0, 640ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_textured_run(&triangle, 1ul, u, v, 640ul, 480ul,
                                      stream, 21ul, &written) !=
          V9X_STATUS_OK);
    CHECK(written == 0ul);
    /* A vertex outside the drawing rectangle is still refused rather than
     * clipped, exactly as in the untextured run. */
    triangle.x[1] = 640ul;
    CHECK(v9x_i9xx_build_textured_run(&triangle, 1ul, u, v, 640ul, 480ul,
                                      stream, 64ul, &written) !=
          V9X_STATUS_OK);
}

unsigned int v9x_run_i9xx_3d_tests(void)
{
    test_float_round_trip();
    test_float_refusals();
    test_fragment_program();
    test_vertex_run();
    test_3d_state();
    test_phase5_parameters();
    test_golden_stream();
    test_decoder_accepts_golden();
    test_decoder_rejects_mutations();
    test_decoder_structural_refusals();
    test_decoder_texture_mode();
    test_published_offsets_locate_the_packets();
    test_rgb565_round();
    test_map_state();
    test_map_state_refusals();
    test_map_state_formats();
    test_runtime_texture_formats();
    test_runtime_blend();
    test_runtime_blend_pairs();
    test_texture_programs();
    test_flip_stream();
    test_sampler_state();
    test_sampling_program();
    test_modulate_program();
    test_texture_paint();
    test_textured_run();
    test_scene_table();
    test_scene_zero_matches_phase5();
    test_scene_one_is_modulated();
    test_texture_probe_quadrants();
    test_scene_probe_budget();
    test_depth_scene_expectations();
    test_gouraud_scene_expectations();
    test_blend_scene_expectations();
    test_decoder_runtime_mode();
    test_runtime_textured_and_depth();
    test_runtime_batch_bound();
    test_float_in_range();
    test_runtime_run();
    test_i9xx_bind_target();
    test_every_scene_decodes();
    test_decoder_depth_refusals();
    test_decoder_flush_forms();
    test_decoder_breadcrumb();
    test_probe_expectation_names();
    test_scene_combined_crc();
    test_scene_primitive_offset();
    test_submission_boundaries_are_qword_aligned();
    test_phase6_chain();
    test_triangle_run_refusals();
    return failures;
}
