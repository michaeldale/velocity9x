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

static unsigned int failures = 0u;

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

static void test_decoder_accepts_golden(void)
{
    v9x_u32 index = 0xfffffffful;

    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 64ul, 0x006c2000ul, 0x00096000ul,
              &index) == V9X_I9XX_P5_OK);
    CHECK(index == 0ul);
    /* The index argument is optional. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 64ul, 0x006c2000ul, 0x00096000ul,
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
            stream, 64ul, 0x006c2000ul, 0x00096000ul, &rejected);
        CHECK(reason == mutations[mutation].reason);
        CHECK(rejected == mutations[mutation].rejected_at);
    }
}

static void test_decoder_structural_refusals(void)
{
    v9x_u32 index = 0ul;

    /* Truncation anywhere is refused, never read past. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 23ul, 0x006c2000ul, 0x00096000ul,
              &index) != V9X_I9XX_P5_OK);
    /* A stream missing its target description decodes clean packet by packet
     * and must still be refused. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 22ul, 0x006c2000ul, 0x00096000ul,
              &index) == V9X_I9XX_P5_MISSING_PACKET);
    CHECK(v9x_i9xx_decode_phase5_stream(
              0, 59ul, 0x006c2000ul, 0x00096000ul, &index) ==
          V9X_I9XX_P5_TRUNCATED);
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 0ul, 0x006c2000ul, 0x00096000ul,
              &index) == V9X_I9XX_P5_TRUNCATED);
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 66ul, 0x006c2000ul, 0ul,
              &index) == V9X_I9XX_P5_TRUNCATED);
    /* A Phase 4 stream must not satisfy the Phase 5 decoder. */
    {
        static const v9x_u32 phase4[8] = {
            0x54300004ul, 0x03f00020ul, 0x00000000ul, 0x00080008ul,
            0x006c1100ul, 0x55aa33ccul, 0x02000000ul, 0x00000000ul
        };
        CHECK(v9x_i9xx_decode_phase5_stream(
                  phase4, 8ul, 0x006c2000ul, 0x00096000ul, &index) !=
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
     * Prefix, the qword PAD, then the two probe dwords and the primitive
     * header, after which the first vertex begins. The pad is what the
     * 2026-09-16 capture was spent finding; leaving it out of this sum would
     * put the expected vertex bits one dword early.
     */
    vertices = fill + state + shader + 2ul;
    vertices += (vertices & 1ul);
    vertices += 1ul;
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
 * Scene 1 differs from scene 0 in the colour dwords and NOTHING else.
 *
 * Identical geometry is the point: it makes the conversion question
 * answerable without the rasteriser's behaviour entering the comparison. If
 * any non-colour dword differed, that claim would be false.
 */
static void test_scene_one_differs_only_in_colour(void)
{
    struct v9x_i9xx_scene zero;
    struct v9x_i9xx_scene one;
    v9x_u32 zero_stream[160];
    v9x_u32 one_stream[160];
    v9x_u32 zero_written = 0ul;
    v9x_u32 one_written = 0ul;
    v9x_u32 index;
    v9x_u32 differences = 0ul;

    CHECK(v9x_i9xx_scene_at(0ul, &zero) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_at(1ul, &one) == V9X_STATUS_OK);
    CHECK(one.id == 1ul);
    CHECK(one.triangles[0].color != zero.triangles[0].color);

    CHECK(v9x_i9xx_build_scene_stream(
              &zero, zero_stream, 160ul, &zero_written) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_scene_stream(
              &one, one_stream, 160ul, &one_written) == V9X_STATUS_OK);
    CHECK(zero_written == one_written);

    if (zero_written == one_written) {
        for (index = 0ul; index < zero_written; ++index) {
            if (zero_stream[index] != one_stream[index]) {
                ++differences;
                /* Every difference must BE a colour dword. */
                CHECK(zero_stream[index] == V9X_I9XX_TRI_COLOR_BGRA);
                CHECK(one_stream[index] == 0xff2e03c8ul);
            }
        }
    }
    /*
     * One per vertex, three vertices. Asserted rather than left implicit:
     * zero differences would also satisfy a loop that only checks that
     * differences are colours.
     */
    CHECK(differences == 3ul);

    /*
     * The prediction, recorded here so a colour change cannot quietly leave
     * it behind. Green 3 is the value that separates rounding from
     * truncation, which is the open question this scene exists to close.
     */
    CHECK(v9x_i9xx_rgb565_round(0x2eul, 0x03ul, 0xc8ul) == 0x3038u);
}

/*
 * Does a probe's SAMPLE CENTRE lie exactly on the given triangle edge?
 *
 * HOST-SIDE ONLY, and deliberately so. It is a property check on the scene
 * table, not behaviour the driver has any use for, and it does not belong in
 * I9XXCODE: the cross product is two signed 32-bit multiplies, which on
 * 16-bit Watcom are __I4M calls in the default CODE segment that a near call
 * cannot reach. It was in the driver module first and produced exactly two
 * E2052 relocations.
 *
 * The centre is (x + 1/2, y + 1/2) - where DSTORG's half-pixel bias puts it -
 * so every term is doubled once to clear the halves. Integer throughout: this
 * decides an exactness question, and a float would decide it by rounding.
 */
static v9x_u16 v9x_test_probe_on_edge(
    const struct v9x_i9xx_triangle *triangle, v9x_u32 first, v9x_u32 second,
    v9x_u16 x, v9x_u16 y)
{
    long dx;
    long dy;
    long cx;
    long cy;

    if (triangle == 0 || first > 2ul || second > 2ul || first == second) {
        return V9X_FALSE;
    }
    dx = (long)triangle->x[second] - (long)triangle->x[first];
    dy = (long)triangle->y[second] - (long)triangle->y[first];
    cx = (2L * (long)x) + 1L - (2L * (long)triangle->x[first]);
    cy = (2L * (long)y) + 1L - (2L * (long)triangle->y[first]);

    if (cx * dy == cy * dx) {
        return V9X_TRUE;
    }
    return V9X_FALSE;
}

/*
 * The shared edge must pass through SAMPLE CENTRES, or the edge scenes cannot
 * observe an inclusion rule at all.
 *
 * This is the test the first version of the scene needed and did not have. Its
 * diagonal ran (200,150)-(440,330), satisfying 4y = 3x, and substituting a
 * centre gives 3i - 4j = 0.5 - an integer equal to a non-integer, so no centre
 * in the square lies on it. The scene would have produced a capture that
 * looked like evidence and answered nothing.
 *
 * Both directions are asserted. A predicate that returned V9X_TRUE for
 * everything would satisfy the first half alone.
 */
static void test_edge_probes_sit_on_sample_centres(void)
{
    struct v9x_i9xx_scene scene;
    struct v9x_i9xx_triangle bad;
    v9x_u32 index;
    v9x_u32 on_edge = 0ul;
    v9x_u32 probe;

    /*
     * The shared edge runs from vertex 0 to the vertex diagonally opposite,
     * and WHICH vertex that is depends on the triangle, not on the scene.
     * The upper triangle is (top-left, top-right, bottom-right), so its
     * diagonal is 0->2; the lower is (top-left, bottom-right, bottom-left),
     * so its diagonal is 0->1. Scene 4's triangles[0] is the upper one, so it
     * takes 2 like scene 2 - getting that wrong is what this test caught on
     * its first run, against vertex 1, which is the upper triangle's TOP
     * edge.
     */
    for (index = 2ul; index <= 4ul; ++index) {
        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        on_edge = 0ul;
        for (probe = 0ul; probe < scene.probe_count; ++probe) {
            v9x_u16 hit = v9x_test_probe_on_edge(
                &scene.triangles[0],
                0ul, (index == 3ul) ? 1ul : 2ul,
                scene.probes[probe].x, scene.probes[probe].y);
            if (scene.probes[probe].expect == V9X_I9XX_PROBE_MEASURE) {
                /* Every MEASURE probe is on the edge, or it measures
                 * nothing. */
                CHECK(hit == V9X_TRUE);
                ++on_edge;
            } else {
                /* And every other probe is off it, or a flank reading would
                 * be an edge reading under another name. */
                CHECK(hit == V9X_FALSE);
            }
        }
        /* Three, asserted: zero MEASURE probes would satisfy the loop. */
        CHECK(on_edge == 3ul);
    }

    /*
     * The predicate, against the geometry that failed. Slope 3/4 from
     * (200,150): no sample centre anywhere on it.
     */
    CHECK(v9x_i9xx_scene_at(2ul, &scene) == V9X_STATUS_OK);
    bad = scene.triangles[0];
    bad.x[2] = 440ul;
    bad.y[2] = 330ul;
    for (probe = 200ul; probe < 441ul; ++probe) {
        CHECK(v9x_test_probe_on_edge(&bad, 0ul, 2ul,
                                     (v9x_u16)probe,
                                     (v9x_u16)((probe * 3ul) / 4ul)) ==
              V9X_FALSE);
    }

    /* Its own argument refusals. */
    CHECK(v9x_test_probe_on_edge(0, 0ul, 2ul, 250u, 200u) == V9X_FALSE);
    CHECK(v9x_test_probe_on_edge(&bad, 1ul, 1ul, 250u, 200u) == V9X_FALSE);
    CHECK(v9x_test_probe_on_edge(&bad, 0ul, 3ul, 250u, 200u) == V9X_FALSE);
}

/*
 * The three edge scenes must share probe COORDINATES exactly, or coverage
 * cannot be compared pixel by pixel - which is the only way double coverage
 * is observable.
 *
 * Two opaque triangles in one scene cannot reveal it: the second overwrites
 * the first, and the result is indistinguishable from coverage by the second
 * alone. So each triangle is drawn on its own and the two are compared, and
 * that comparison is meaningless unless the probes are at the same pixels.
 */
static void test_edge_scenes_share_probe_coordinates(void)
{
    struct v9x_i9xx_scene upper;
    struct v9x_i9xx_scene lower;
    struct v9x_i9xx_scene both;
    v9x_u32 probe;
    v9x_u32 disagreements = 0ul;

    CHECK(v9x_i9xx_scene_at(2ul, &upper) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_at(3ul, &lower) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_at(4ul, &both) == V9X_STATUS_OK);

    CHECK(upper.probe_count == lower.probe_count);
    CHECK(upper.probe_count == both.probe_count);
    CHECK(upper.probe_count == 8ul);

    for (probe = 0ul; probe < upper.probe_count; ++probe) {
        CHECK(upper.probes[probe].x == lower.probes[probe].x);
        CHECK(upper.probes[probe].y == lower.probes[probe].y);
        CHECK(upper.probes[probe].x == both.probes[probe].x);
        CHECK(upper.probes[probe].y == both.probes[probe].y);
        /* The expectations must DIFFER between the two single-triangle
         * scenes at the flanks, or drawing them separately tells us
         * nothing new. */
        if (upper.probes[probe].expect != lower.probes[probe].expect) {
            ++disagreements;
        }
    }
    /* Two flanks plus the two bodies. Asserted as a number: identical
     * expectations everywhere would pass a "coordinates match" loop and
     * make the whole experiment vacuous. */
    CHECK(disagreements == 4ul);

    /*
     * Each single-triangle scene draws ONE triangle. If either drew both,
     * the overwrite problem would be back and the comparison would be
     * between two identical scenes.
     */
    CHECK(upper.triangle_count == 1ul);
    CHECK(lower.triangle_count == 1ul);
    CHECK(both.triangle_count == 2ul);

    /* The combined scene's two triangles are exactly the two drawn alone,
     * or it is not the combination of them. */
    CHECK(both.triangles[0].color == upper.triangles[0].color);
    CHECK(both.triangles[1].color == lower.triangles[0].color);
    for (probe = 0ul; probe < 3ul; ++probe) {
        CHECK(both.triangles[0].x[probe] == upper.triangles[0].x[probe]);
        CHECK(both.triangles[0].y[probe] == upper.triangles[0].y[probe]);
        CHECK(both.triangles[1].x[probe] == lower.triangles[0].x[probe]);
        CHECK(both.triangles[1].y[probe] == lower.triangles[0].y[probe]);
    }

    /* Both edge colours are already measured, so the edge rule is the single
     * unknown in these scenes. */
    CHECK(upper.triangles[0].color == V9X_I9XX_TRI_COLOR_BGRA);
    CHECK(lower.triangles[0].color == 0xfff86428ul);
}

/* The combined edge scene emits both triangles under one primitive command. */
static void test_edge_combined_is_one_primitive(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 stream[160];
    v9x_u32 written = 0ul;
    v9x_u32 header;

    CHECK(v9x_i9xx_scene_at(4ul, &scene) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_build_scene_stream(
              &scene, stream, 160ul, &written) == V9X_STATUS_OK);
    CHECK(v9x_i9xx_scene_extent(&scene) == written);

    /*
     * Six vertices under a single _3DPRIMITIVE, not two commands of three.
     * The length field counts every vertex dword less one: 6 * 5 - 1 = 29.
     */
    /*
     * Located by the published primitive offset rather than counted back from
     * the end: two triangles make the stream odd, so it carries a TRAILING
     * qword pad and counting back would land on that instead.
     */
    header = stream[v9x_i9xx_scene_primitive_offset(&scene)];
    CHECK(header == (V9X_I9XX_3DPRIMITIVE_INLINE |
                     V9X_I9XX_PRIM3D_TRILIST | 29ul));

    /*
     * Prefix 48, six vertices at five dwords plus the command is 31, and the
     * 79 that makes is padded to 80. Written out because the pad is the whole
     * subject of the 2026-09-16 capture.
     */
    CHECK(written == 80ul);
    CHECK((written & 1ul) == 0ul);
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
     * 14 + 14 + 8 + 8 + 8. Written out because the budget rule is that no
     * boot roughly doubles the last one that completed, and the last capture
     * that completed performed about 45 aperture reads. Changing the scene
     * table past this without re-reading that rule should fail here.
     */
    CHECK(total == 52ul);
}

/* The table itself, and its refusals. */
static void test_scene_table(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 index;
    v9x_u32 stream[160];
    v9x_u32 written = 0ul;

    /*
     * Five scenes, and five is also what the 2026-09-16 errata amendment
     * authorises. Both asserted, and their RELATIONSHIP asserted, because the
     * failure this guards against is a build quietly defining more draws than
     * were agreed - which the count alone would not show.
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

        /* Fill 7 + state 31 + shader 7 + probe 2. The same for every scene:
         * only the triangle run after it varies. */
        CHECK(offset == 48ul);

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
        CHECK(written - offset >=
              v9x_i9xx_triangle_run_dwords(scene.triangle_count));
        CHECK(written - offset <=
              v9x_i9xx_triangle_run_dwords(scene.triangle_count) + 1ul);
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

    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        v9x_u32 primitive;

        CHECK(v9x_i9xx_scene_at(index, &scene) == V9X_STATUS_OK);
        written = 0ul;
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
         * The pad is an MI_NOOP, not whatever the buffer held.
         *
         * This build's prefix is 47 dwords before padding, so a pad IS
         * inserted and the dword before the primitive must be one. Tied to
         * the current figure deliberately: if the prefix becomes even the
         * assertion should fail and be re-examined rather than quietly
         * passing on a branch that stopped being taken.
         */
        CHECK(primitive >= 1ul);
        CHECK(stream[primitive - 1ul] == V9X_I9XX_MI_NOOP);
    }
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
    test_published_offsets_locate_the_packets();
    test_rgb565_round();
    test_scene_table();
    test_scene_zero_matches_phase5();
    test_scene_one_differs_only_in_colour();
    test_edge_probes_sit_on_sample_centres();
    test_edge_scenes_share_probe_coordinates();
    test_edge_combined_is_one_primitive();
    test_scene_probe_budget();
    test_scene_combined_crc();
    test_scene_primitive_offset();
    test_submission_boundaries_are_qword_aligned();
    test_phase6_chain();
    test_triangle_run_refusals();
    return failures;
}
