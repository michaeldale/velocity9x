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
        CHECK(stream[base + 4ul] == 0xfff86428ul);   /* packed BGRA */
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
    CHECK(v9x_i9xx_build_vertex_run(640ul, 480ul, stream, 15ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* --------------------------------------------------------------------- */
/* 4. The state block                                                     */
/* --------------------------------------------------------------------- */

static void test_3d_state(void)
{
    v9x_u32 stream[64];
    v9x_u32 written = 0ul;

    CHECK(v9x_i9xx_3d_state_extent() == 34ul);
    CHECK(v9x_i9xx_build_3d_state(0x006c2000ul, 1280ul, 640ul, 480ul,
                                  stream, 64ul, &written) == V9X_STATUS_OK);
    CHECK(written == 34ul);

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

    /* Depth: declared, dummy pitch, address zero, never referenced. */
    CHECK(stream[18] == V9X_I9XX_3DSTATE_BUF_INFO);
    CHECK(stream[19] == (V9X_I9XX_BUF_3D_ID_DEPTH | 4096ul));
    CHECK(stream[20] == 0ul);

    /* RGB565 with the half-pixel bias in both axes. */
    CHECK(stream[21] == V9X_I9XX_3DSTATE_DST_BUF_VARS);
    CHECK(stream[22] == 0x00880200ul);

    /* Draw rect is INCLUSIVE: 639 and 479, not 640 and 480. */
    CHECK(stream[23] == V9X_I9XX_3DSTATE_DRAW_RECT);
    CHECK(stream[26] == ((479ul << 16) | 639ul));

    /* S2..S6 in one load, length 4, and S4 agreeing with the vertex dwords. */
    CHECK(stream[28] == 0x7d0407c4ul);
    CHECK(stream[29] == 0xfffffffful);
    CHECK(stream[31] == 0x00902480ul);
    CHECK((stream[33] & (V9X_I9XX_S6_DEPTH_TEST_ENABLE |
                         V9X_I9XX_S6_DEPTH_WRITE_ENABLE)) == 0ul);

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
                                  stream, 33ul, &written) ==
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
    CHECK(parameters.triangle_color == 0xfff86428ul);
    /* 34 state + 7 shader + 2 probe + 16 vertices. */
    /* 7 fill + 34 state + 7 shader + 2 probe + 16 vertices. */
    CHECK(parameters.stream_dwords == 66ul);

    /* The parameters and the layout must describe the same target. */
    CHECK(v9x_i9xx_sandbox_calculate(
              0x007b0000ul, 0x7f800000ul, &layout) == V9X_STATUS_OK);
    CHECK(layout.target_bytes == parameters.target_bytes);
    CHECK(layout.target_pitch == parameters.target_pitch);
    CHECK(parameters.target_bytes ==
          parameters.target_pitch * parameters.height);

    /*
     * The triangle colour must survive RGB565 exactly, because dithering is on
     * by default on this hardware and cannot be cleanly disabled. A colour the
     * hardware had to dither would disagree with the software reference across
     * the interior, not only at the edges the plan licenses.
     */
    CHECK((parameters.triangle_color & 0x00070000ul) == 0ul);  /* red   */
    CHECK((parameters.triangle_color & 0x00000300ul) == 0ul);  /* green */
    CHECK((parameters.triangle_color & 0x00000007ul) == 0ul);  /* blue  */

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
static const v9x_u32 v9x_i9xx_phase5_golden[66] = {
    0x54300004ul, 0x03f00500ul, 0x00000000ul, 0x01e00140ul, 0x006c2000ul,
    0x08420842ul, 0x02000000ul, 0x66014140ul, 0x7d990000ul, 0x00000000ul,
    0x7d9a0000ul, 0x00000000ul, 0x7d980000ul, 0x00000000ul, 0x76fac688ul,
    0x7d810001ul, 0x00000000ul, 0x00000000ul, 0x7c800002ul, 0x7c880002ul,
    0x7d070000ul, 0x00000000ul, 0x7d8e0001ul, 0x03000500ul, 0x006c2000ul,
    0x7d8e0001ul, 0x07001000ul, 0x00000000ul, 0x7d850000ul, 0x00880200ul,
    0x7d800003ul, 0x00000000ul, 0x00000000ul, 0x01df027ful, 0x00000000ul,
    0x7d0407c4ul, 0xfffffffful, 0x00000000ul, 0x00902480ul, 0x00000000ul,
    0x00000000ul, 0x7d050005ul, 0x190a3c00ul, 0x00000000ul, 0x00000000ul,
    0x02203ca0ul, 0x01230000ul, 0x00000000ul, 0x00000000ul, 0x02000000ul,
    0x7f00000eul, 0x43200000ul, 0x42f00000ul, 0x00000000ul, 0x3f800000ul,
    0xfff86428ul, 0x43f00000ul, 0x42f00000ul, 0x00000000ul, 0x3f800000ul,
    0xfff86428ul, 0x43a00000ul, 0x43c80000ul, 0x00000000ul, 0x3f800000ul,
    0xfff86428ul
};

static void test_golden_stream(void)
{
    v9x_u32 stream[96];
    v9x_u32 written = 0ul;
    v9x_u32 index;

    CHECK(v9x_i9xx_build_phase5_stream(stream, 96ul, &written) ==
          V9X_STATUS_OK);
    CHECK(written == 66ul);
    for (index = 0ul; index < 66ul; ++index) {
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
          v9x_i9xx_crc32_dwords(v9x_i9xx_phase5_golden, 66ul));
    CHECK(v9x_i9xx_phase5_execution_crc() == 0x0ed8c9a3ul);

    CHECK(v9x_i9xx_build_phase5_stream(stream, 65ul, &written) !=
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
              v9x_i9xx_phase5_golden, 66ul, 0x006c2000ul, 0x00096000ul,
              &index) == V9X_I9XX_P5_OK);
    CHECK(index == 0ul);
    /* The index argument is optional. */
    CHECK(v9x_i9xx_decode_phase5_stream(
              v9x_i9xx_phase5_golden, 66ul, 0x006c2000ul, 0x00096000ul,
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
        { 4ul, 0x00000000ul, V9X_I9XX_P5_TARGET_RANGE,       0ul },
        { 5ul, 0x08420843ul, V9X_I9XX_P5_FORMAT,             5ul },
        { 3ul, 0x01e00141ul, V9X_I9XX_P5_TARGET_RANGE,       0ul },
        /* Tiled or fenced colour target. */
        { 23ul, 0x03400500ul, V9X_I9XX_P5_TILED_FORBIDDEN, 23ul },
        { 23ul, 0x03800500ul, V9X_I9XX_P5_TILED_FORBIDDEN, 23ul },
        /* Wrong colour pitch. */
        { 23ul, 0x03000200ul, V9X_I9XX_P5_PITCH, 23ul },
        /* Colour target pointing somewhere else. */
        { 24ul, 0x00000000ul, V9X_I9XX_P5_TARGET_RANGE, 24ul },
        { 24ul, 0x006c3000ul, V9X_I9XX_P5_TARGET_RANGE, 24ul },
        /* A depth buffer with real memory behind it. */
        { 26ul, 0x07400400ul, V9X_I9XX_P5_TILED_FORBIDDEN, 26ul },
        { 27ul, 0x006c2000ul, V9X_I9XX_P5_DEPTH_FORBIDDEN, 27ul },
        /* Wrong destination format, and the bias silently dropped. */
        { 29ul, 0x00880300ul, V9X_I9XX_P5_FORMAT, 29ul },
        { 29ul, 0x00000200ul, V9X_I9XX_P5_FORMAT, 29ul },
        /* Exclusive rather than inclusive draw rect. */
        { 33ul, 0x01e00280ul, V9X_I9XX_P5_DRAW_RECT, 33ul },
        { 31ul, 0x00000001ul, V9X_I9XX_P5_DRAW_RECT, 31ul },
        /* Scissor turned on. */
        { 18ul, 0x7c800003ul, V9X_I9XX_P5_SCISSOR_ENABLED, 18ul },
        /* Indirect state enabled rather than disabled. */
        { 21ul, 0x00000001ul, V9X_I9XX_P5_INDIRECT_FORBIDDEN, 21ul },
        /* A texture coordinate declared present. */
        { 36ul, 0xfffffffeul, V9X_I9XX_P5_TEXTURE_FORBIDDEN, 36ul },
        /* S4 disagreeing with the vertex dwords - the silent-hang case. */
        { 38ul, 0x009024c0ul, V9X_I9XX_P5_VERTEX_FORMAT, 38ul },
        { 38ul, 0x00902400ul, V9X_I9XX_P5_VERTEX_FORMAT, 38ul },
        /* Depth test or write enabled in S6. */
        { 40ul, 0x00080000ul, V9X_I9XX_P5_DEPTH_FORBIDDEN, 40ul },
        { 40ul, 0x00000008ul, V9X_I9XX_P5_DEPTH_FORBIDDEN, 40ul },
        /* A shader of the wrong length. */
        { 41ul, 0x7d050007ul, V9X_I9XX_P5_SHADER, 41ul },
        /* The indirect primitive form, which would fetch from a buffer. */
        { 50ul, 0x7f80000eul, V9X_I9XX_P5_INDIRECT_FORBIDDEN, 50ul },
        /* Wrong vertex count. */
        { 50ul, 0x7f000009ul, V9X_I9XX_P5_VERTEX_COUNT, 50ul },
        /* A vertex outside the drawing rectangle. */
        { 51ul, 0x44800000ul, V9X_I9XX_P5_VERTEX_RANGE, 51ul },
        /* A fractional coordinate, which the float decoder refuses. */
        { 52ul, 0x42f10000ul, V9X_I9XX_P5_VERTEX_RANGE, 52ul },
        /* Non-zero Z, and W other than one. */
        { 53ul, 0x3f800000ul, V9X_I9XX_P5_VERTEX_RANGE, 53ul },
        { 54ul, 0x40000000ul, V9X_I9XX_P5_VERTEX_RANGE, 54ul },
        /* One vertex a different colour from the other two. */
        { 60ul, 0xff286428ul, V9X_I9XX_P5_VERTEX_FORMAT, 60ul }
    };
    v9x_u32 stream[66];
    v9x_u32 index;
    v9x_u32 mutation;
    const v9x_u32 count =
        (v9x_u32)(sizeof(mutations) / sizeof(mutations[0]));

    for (mutation = 0ul; mutation < count; ++mutation) {
        v9x_u32 rejected = 0xfffffffful;
        v9x_u16 reason;
        for (index = 0ul; index < 66ul; ++index) {
            stream[index] = v9x_i9xx_phase5_golden[index];
        }
        stream[mutations[mutation].index] = mutations[mutation].value;
        reason = v9x_i9xx_decode_phase5_stream(
            stream, 66ul, 0x006c2000ul, 0x00096000ul, &rejected);
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
    return failures;
}
