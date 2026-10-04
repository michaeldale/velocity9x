/*
 * SIS3D.EXE: the SiS 6326 3D engine write probe, phases 1 and 2.
 *
 * Publishes C:\V9XDIAG\SIS3D.TXT. Drives the 3D registers through
 * SIS2D.VXD, which executes op lists inside the sequencer, BAR1 and BAR0 and
 * refuses anything else (docs\plans\sis-6326-hardware-3d.md).
 *
 * Every register value comes from the host-tested builder
 * (src\chipsets\sis\sis6326_3d.c), compiled in. Phase 1 (no switch) answers:
 *
 *   - which value of the direction bit (89F8h D7) a triangle needs, for a
 *     middle vertex right of the long edge and for one left of it - each
 *     triangle is fired once with each value;
 *   - whether the engine covers the pixels whose centres are inside, and
 *     whether its edges are inclusive;
 *   - whether 3D runs with the Turbo Queue off and SR39 D2 set.
 *
 * Phase 2 (/phase2) answers:
 *
 *   - the smallest up-left vertex shift that still gives Direct3D's
 *     top-left tie rule: 1/16, 1/256, 1/1024, 1/4096 and 1/65536 pixel;
 *   - whether Gouraud colour matches barycentric interpolation at the
 *     sample point;
 *   - Z16 with LESS: occlusion both ways, and the values written;
 *   - the alpha test (GREATER is strict);
 *   - SRCALPHA/INVSRCALPHA blending, then additive ONE/ONE with
 *     saturation - last, because A8U4I5 locked on additive sprites under
 *     another engine.
 *
 * Safety contract, as SIS2D.EXE's: run under Velocity9x at 16 bpp (the
 * targets are RGB565), with the desktop below 2 MiB; every target is
 * off-screen at 2 MiB and above and guarded; every wait is bounded in the
 * VxD; SR39 and SR05 are written back as found. SRB and SR27 are the
 * driver's (it enables the engine window after every mode set) and are only
 * checked.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/sis6326_3d.h"

#include "../../src/chipsets/sis/sis6326_3d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define SIS3D_MAGIC 0x44325349ul
#define SIS3D_OP_MAX 512u
#define SIS3D_TIMEOUT 0xfffffffful

#define SIS3D_OP_SR_READ      1ul
#define SIS3D_OP_SR_WRITE     2ul
#define SIS3D_OP_MMIO_WRITE32 3ul
#define SIS3D_OP_MMIO_READ32  5ul
#define SIS3D_OP_LFB_READ32   7ul
#define SIS3D_OP_LFB_FILL32   8ul
#define SIS3D_OP_WAIT_SET     10ul

#define SIS3D_RAN 0x00000004ul

#define SIS_SR05 0x05ul
#define SIS_SRB  0x0bul
#define SIS_SR27 0x27ul
#define SIS_SR39 0x39ul
#define SIS_SR05_KEY 0x86ul
#define SIS_SR05_UNLOCKED 0xa1ul
#define SIS_SR05_RELOCK 0x00ul
#define SIS_SRB_MMIO_BAR1 0x60ul
#define SIS_SR27_ENGINE 0x40ul
/* SR39 D2: enable the 3D accelerator (datasheet 7.7.60). */
#define SIS_SR39_3D 0x04ul

#define SIS3D_VRAM_BYTES 0x00400000ul
#define SIS3D_TEST_BASE  0x00200000ul
#define SIS3D_REGION_STRIDE 0x00001000ul
#define SIS3D_SIDE 32ul
#define SIS3D_PITCH 64ul
#define SIS3D_GUARD 0xa5a5u
/* Flat green: ARGB FF00FF00 is 07E0h in RGB565. */
#define SIS3D_ARGB 0xff00ff00ul
#define SIS3D_PIXEL 0x07e0u

struct sis3d_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct sis3d_request {
    DWORD count;
    struct sis3d_op ops[SIS3D_OP_MAX];
};

struct sis3d_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar1;
    DWORD executed;
    DWORD refused;
    struct sis3d_op ops[SIS3D_OP_MAX];
};

static struct sis3d_request sis3d_request_buffer;
static struct sis3d_result sis3d_result_buffer;
static HANDLE sis3d_device = INVALID_HANDLE_VALUE;
static HANDLE sis3d_output = INVALID_HANDLE_VALUE;
static unsigned short sis3d_pixels[SIS3D_SIDE][SIS3D_SIDE];

static void sis3d_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void sis3d_decimal(char *text, DWORD value)
{
    char reverse[16];
    int count = 0;
    int index;

    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0; index < count; ++index) {
        text[index] = reverse[count - index - 1];
    }
    text[count] = '\0';
}

static void sis3d_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(sis3d_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(sis3d_output, "=", 1u, &written, 0);
    WriteFile(sis3d_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(sis3d_output, "\r\n", 2u, &written, 0);
}

static void sis3d_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    sis3d_hex(text + 2, value, 8);
    sis3d_write(key, text);
}

static void sis3d_write_decimal(const char *key, DWORD value)
{
    char text[16];

    sis3d_decimal(text, value);
    sis3d_write(key, text);
}

static void sis3d_key(char *key, const char *prefix, const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
}

static void sis3d_begin(void)
{
    sis3d_request_buffer.count = 0ul;
}

static DWORD sis3d_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct sis3d_op *op;
    DWORD index = sis3d_request_buffer.count;

    if (index >= SIS3D_OP_MAX) {
        return SIS3D_OP_MAX;
    }
    op = &sis3d_request_buffer.ops[index];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0ul;
    ++sis3d_request_buffer.count;
    return index;
}

static int sis3d_run(void)
{
    DWORD returned = 0ul;

    if (!DeviceIoControl(sis3d_device, 1u, &sis3d_request_buffer,
                         sizeof(sis3d_request_buffer), &sis3d_result_buffer,
                         sizeof(sis3d_result_buffer), &returned, 0) ||
        returned != sizeof(sis3d_result_buffer) ||
        sis3d_result_buffer.magic != SIS3D_MAGIC) {
        return 0;
    }
    return (sis3d_result_buffer.status & SIS3D_RAN) != 0ul &&
           sis3d_result_buffer.executed == sis3d_request_buffer.count;
}

static DWORD sis3d_value(DWORD index)
{
    return index < SIS3D_OP_MAX ? sis3d_result_buffer.ops[index].result : 0ul;
}

static void sis3d_add_writes(const struct v9x_sis3d_writes *writes)
{
    DWORD index;

    for (index = 0ul; index < writes->count; ++index) {
        sis3d_add(SIS3D_OP_MMIO_WRITE32, writes->offsets[index],
                  writes->values[index], 0ul);
    }
}

/*
 * The reference coverage: pixel (x, y) is inside when the point (x, y) -
 * its integer corner, Direct3D's pixel centre - is inside all three edges.
 * Measured on 2026-10-05: sampling at (x + 0.5, y + 0.5) disagreed with the
 * engine on 20-24 rows per triangle, at (x, y) on none.
 *
 * A sample exactly on an edge is decided by owner. 0 counts it outside
 * (phase 1, whose spans the tie rule was read from). Otherwise the sample is
 * nudged by (owner * e, owner * e^2): +1 gives Direct3D's top-left rule (a
 * non-horizontal edge owns the sample if the inside lies to its right, a
 * horizontal one if the inside lies below), -1 the engine's measured
 * bottom-right rule.
 */
static long sis3d_edge(const long *a, const long *b, long px, long py)
{
    return (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
}

static int sis3d_side(const long *a, const long *b, long px, long py,
                      int owner)
{
    long e = sis3d_edge(a, b, px, py);
    long nudge;

    if (e != 0l) {
        return e > 0l ? 1 : -1;
    }
    if (owner == 0) {
        return 0;
    }
    /* de/dpx = -dy dominates; de/dpy = dx decides a horizontal edge. */
    nudge = (b[1] != a[1]) ? -(b[1] - a[1]) : (b[0] - a[0]);
    return ((nudge > 0l) == (owner > 0)) ? 1 : -1;
}

static int sis3d_inside(const long v[3][2], DWORD x, DWORD y, int owner)
{
    long px = (long)x * 16l;
    long py = (long)y * 16l;
    int s0 = sis3d_side(v[0], v[1], px, py, owner);
    int s1 = sis3d_side(v[1], v[2], px, py, owner);
    int s2 = sis3d_side(v[2], v[0], px, py, owner);

    return s0 != 0 && s0 == s1 && s1 == s2;
}

static void sis3d_span_text(char *text, DWORD left, DWORD right,
                            DWORD count)
{
    if (count == 0ul) {
        lstrcpyA(text, "-");
        return;
    }
    sis3d_decimal(text, left);
    lstrcatA(text, "-");
    sis3d_decimal(text + lstrlenA(text), right);
    lstrcatA(text, "/");
    sis3d_decimal(text + lstrlenA(text), count);
}

/* A 32x32 RGB565 or Z16 surface at base is exactly 512 dwords: one op
 * list. */
static int sis3d_read_region(DWORD base,
                             unsigned short pixels[SIS3D_SIDE][SIS3D_SIDE])
{
    DWORD first = SIS3D_OP_MAX;
    DWORD index;
    DWORD row;
    DWORD col;
    DWORD value;

    sis3d_begin();
    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; col += 2ul) {
            index = sis3d_add(SIS3D_OP_LFB_READ32,
                              base + row * SIS3D_PITCH + col * 2ul, 0ul, 0ul);
            if (first == SIS3D_OP_MAX) {
                first = index;
            }
        }
    }
    if (!sis3d_run()) {
        return 0;
    }
    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; col += 2ul) {
            value = sis3d_value(first + row * (SIS3D_SIDE / 2ul) + col / 2ul);
            pixels[row][col] = (unsigned short)value;
            pixels[row][col + 1ul] = (unsigned short)(value >> 16);
        }
    }
    return 1;
}

/* One shot: guard, fire, wait, read back, compare row by row. */
static void sis3d_shot(const char *name, DWORD region, const long q[3][2],
                       int direction)
{
    struct v9x_sis3d_vertex vertices[3];
    struct v9x_sis3d_target target;
    struct v9x_sis3d_writes state;
    struct v9x_sis3d_writes vertex_writes;
    char key[48];
    char text[96];
    char part[32];
    DWORD base = SIS3D_TEST_BASE + region * SIS3D_REGION_STRIDE;
    DWORD index;
    DWORD row;
    DWORD col;
    DWORD wait_index;
    DWORD status_index;
    DWORD painted = 0ul;
    DWORD expected_total = 0ul;
    DWORD wrong_colour = 0ul;
    DWORD differing_rows = 0ul;

    for (index = 0ul; index < 3ul; ++index) {
        vertices[index].x = v9x_sis3d_float_q4((v9x_s32)q[index][0]);
        vertices[index].y = v9x_sis3d_float_q4((v9x_s32)q[index][1]);
        vertices[index].z = 0ul;
        vertices[index].argb = SIS3D_ARGB;
        vertices[index].u = 0ul;
        vertices[index].v = 0ul;
        vertices[index].w = v9x_sis3d_float_q4(16);
        vertices[index].fog_specular = 0ul;
    }
    target.vram_bytes = SIS3D_VRAM_BYTES;
    target.offset = base;
    target.pitch_bytes = SIS3D_PITCH;
    target.width = SIS3D_SIDE;
    target.height = SIS3D_SIDE;
    if (v9x_sis3d_build_flat_state(&target, &state) != V9X_STATUS_OK) {
        sis3d_key(key, name, "Result");
        sis3d_write(key, "BUILD-REFUSED");
        return;
    }
    v9x_sis3d_build_vertices(vertices, &vertex_writes);
    sis3d_key(key, name, "Primitive");
    sis3d_write_hex(key, v9x_sis3d_primitive(vertices,
                                             V9X_SIS3D_SHADE_FLAT_TOP,
                                             direction));

    sis3d_begin();
    /* Guard the target and the 2 KiB after it. */
    sis3d_add(SIS3D_OP_LFB_FILL32, base, 0xa5a5a5a5ul,
              SIS3D_REGION_STRIDE / 4ul);
    sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
              V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    sis3d_add_writes(&state);
    sis3d_add(SIS3D_OP_MMIO_WRITE32, V9X_SIS3D_PRIMITIVE,
              v9x_sis3d_primitive(vertices, V9X_SIS3D_SHADE_FLAT_TOP,
                                  direction), 0ul);
    sis3d_add_writes(&vertex_writes);
    status_index = sis3d_add(SIS3D_OP_MMIO_READ32, V9X_SIS3D_STATUS, 0ul,
                             0ul);
    wait_index = sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                           V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    if (!sis3d_run()) {
        sis3d_key(key, name, "Result");
        sis3d_write(key, "RUN-FAILED");
        return;
    }
    sis3d_key(key, name, "StatusAfterFire");
    sis3d_write_hex(key, sis3d_value(status_index));
    sis3d_key(key, name, "IdleWaitReads");
    sis3d_write_hex(key, sis3d_value(wait_index));

    if (!sis3d_read_region(base, sis3d_pixels)) {
        sis3d_key(key, name, "Result");
        sis3d_write(key, "READBACK-FAILED");
        return;
    }

    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        DWORD actual_left = 0ul;
        DWORD actual_right = 0ul;
        DWORD actual_count = 0ul;
        DWORD expected_left = 0ul;
        DWORD expected_right = 0ul;
        DWORD expected_count = 0ul;
        int same = 1;

        for (col = 0ul; col < SIS3D_SIDE; ++col) {
            int hit = sis3d_pixels[row][col] != SIS3D_GUARD;
            int want = sis3d_inside(q, col, row, 0);

            if (hit) {
                if (actual_count == 0ul) {
                    actual_left = col;
                }
                actual_right = col;
                ++actual_count;
                if (sis3d_pixels[row][col] != SIS3D_PIXEL) {
                    ++wrong_colour;
                }
            }
            if (want) {
                if (expected_count == 0ul) {
                    expected_left = col;
                }
                expected_right = col;
                ++expected_count;
            }
            if (hit != want) {
                same = 0;
            }
        }
        painted += actual_count;
        expected_total += expected_count;
        if (!same) {
            ++differing_rows;
        }
        if (actual_count == 0ul && expected_count == 0ul) {
            continue;
        }
        sis3d_span_text(text, expected_left, expected_right, expected_count);
        lstrcatA(text, " actual ");
        sis3d_span_text(part, actual_left, actual_right, actual_count);
        lstrcatA(text, part);
        if (!same) {
            lstrcatA(text, " DIFF");
        }
        sis3d_key(key, name, "Row");
        sis3d_decimal(key + lstrlenA(key), row);
        sis3d_write(key, text);
    }
    sis3d_key(key, name, "Painted");
    sis3d_write_decimal(key, painted);
    sis3d_key(key, name, "Expected");
    sis3d_write_decimal(key, expected_total);
    sis3d_key(key, name, "WrongColour");
    sis3d_write_decimal(key, wrong_colour);
    sis3d_key(key, name, "DifferingRows");
    sis3d_write_decimal(key, differing_rows);
    sis3d_key(key, name, "Result");
    sis3d_write(key, painted == 0ul ? "NOTHING-DRAWN"
                     : (differing_rows == 0ul && wrong_colour == 0ul)
                       ? "MATCH" : "DIFF");
}

/*
 * Phase 2. Each scene clears its target (and Z buffer), loads the whole
 * state once, fires its triangles in order, and is compared pixel by pixel
 * with a reference computed here in integer arithmetic.
 */
#define SIS3D_FLOAT_QUARTER    0x3e800000ul
#define SIS3D_FLOAT_HALF       0x3f000000ul
#define SIS3D_FLOAT_3_QUARTERS 0x3f400000ul
/* A Z16 buffer cleared to FFFFh holds 1.0, the far plane. */
#define SIS3D_FLOAT_ONE        0x3f800000ul
#define SIS3D_Z_CLEAR          0xfffffffful
#define SIS3D_Z_FAR            0xffffu

#define SIS3D_RED        0xffff0000ul
#define SIS3D_GREEN      0xff00ff00ul
#define SIS3D_BLUE       0xff0000fful
#define SIS3D_BLUE_565   0x001ful
#define SIS3D_GREY_565   0x8410ul
#define SIS3D_ENGINE_OWNER  (-1)
#define SIS3D_D3D_OWNER     1
/* Largest 5/6-bit field difference accepted: the reference does not know
 * whether the engine truncates or rounds. */
#define SIS3D_COLOUR_TOLERANCE 1ul
#define SIS3D_HISTOGRAM_MAX 8u

struct sis3d_draw {
    long q[3][2];       /* sixteenths of a pixel */
    DWORD z;            /* IEEE single bits, 0 to 1 */
    DWORD argb[3];
};

struct sis3d_scene {
    const char *name;
    DWORD region;
    DWORD z_region;           /* 0: no Z buffer */
    DWORD background;         /* RGB565 the target is cleared to */
    DWORD shade;              /* V9X_SIS3D_SHADE_* */
    int fraction_bits;        /* precision of the vertex floats sent */
    long shift;               /* added to x and y, 2^-fraction_bits pixel */
    int owner;                /* the reference's tie rule, as sis3d_inside */
    DWORD enable;
    DWORD z_compare;
    DWORD alpha_compare;
    DWORD alpha_reference;
    DWORD blend_source;
    DWORD blend_destination;
    const struct sis3d_draw *draws;
    DWORD draw_count;
};

static unsigned short sis3d_expected[SIS3D_SIDE][SIS3D_SIDE];
static unsigned short sis3d_depth[SIS3D_SIDE][SIS3D_SIDE];
static DWORD sis3d_depth_expected[SIS3D_SIDE][SIS3D_SIDE];

/* The tie triangles of phase 1, integer vertices. */
static const struct sis3d_draw sis3d_tie_draws[2] = {
    { { { 64l, 64l }, { 320l, 64l }, { 64l, 320l } }, 0ul,
      { SIS3D_GREEN, SIS3D_GREEN, SIS3D_GREEN } },
    { { { 320l, 64l }, { 320l, 320l }, { 64l, 320l } }, 0ul,
      { SIS3D_GREEN, SIS3D_GREEN, SIS3D_GREEN } }
};

static const struct sis3d_draw sis3d_gouraud_draws[1] = {
    { { { 36l, 36l }, { 476l, 104l }, { 136l, 476l } }, 0ul,
      { SIS3D_RED, SIS3D_GREEN, SIS3D_BLUE } }
};

/* Colour a function of x alone, then of y alone: separates a horizontal
 * from a vertical error in where the colour is evaluated. */
static const struct sis3d_draw sis3d_gouraud_x_draws[1] = {
    { { { 36l, 36l }, { 476l, 36l }, { 476l, 476l } }, 0ul,
      { SIS3D_RED, SIS3D_GREEN, SIS3D_GREEN } }
};

static const struct sis3d_draw sis3d_gouraud_y_draws[1] = {
    { { { 36l, 36l }, { 476l, 36l }, { 36l, 476l } }, 0ul,
      { SIS3D_RED, SIS3D_RED, SIS3D_GREEN } }
};

/* Green at 0.5; red at 0.25 across green's long edge, so it wins where they
 * overlap; blue at 0.75 inside green, so it must not appear; blue at 0.75
 * clear of both, so it must. */
static const struct sis3d_draw sis3d_depth_draws[4] = {
    { { { 36l, 36l }, { 484l, 44l }, { 44l, 484l } }, SIS3D_FLOAT_HALF,
      { SIS3D_GREEN, SIS3D_GREEN, SIS3D_GREEN } },
    { { { 260l, 260l }, { 500l, 300l }, { 300l, 500l } },
      SIS3D_FLOAT_QUARTER, { SIS3D_RED, SIS3D_RED, SIS3D_RED } },
    { { { 68l, 68l }, { 200l, 76l }, { 76l, 200l } },
      SIS3D_FLOAT_3_QUARTERS, { SIS3D_BLUE, SIS3D_BLUE, SIS3D_BLUE } },
    { { { 420l, 420l }, { 500l, 428l }, { 428l, 500l } },
      SIS3D_FLOAT_3_QUARTERS, { SIS3D_BLUE, SIS3D_BLUE, SIS3D_BLUE } }
};

/* Z written unconditionally at the largest single below 1.0, 1.0, 1.5 and
 * just under 2.0, one quadrant each: where the Z16 scale ends. 0.5 wrote
 * 4000h; build sis3d-20261005-p2c, with 0 in the first quadrant, wrote
 * 0000h in all four. */
static const struct sis3d_draw sis3d_depth_scale_draws[4] = {
    { { { 36l, 36l }, { 220l, 36l }, { 36l, 220l } }, 0x3f7ffffful,
      { SIS3D_GREEN, SIS3D_GREEN, SIS3D_GREEN } },
    { { { 292l, 36l }, { 476l, 36l }, { 292l, 220l } }, SIS3D_FLOAT_ONE,
      { SIS3D_GREEN, SIS3D_GREEN, SIS3D_GREEN } },
    { { { 36l, 292l }, { 220l, 292l }, { 36l, 476l } }, 0x3fc00000ul,
      { SIS3D_GREEN, SIS3D_GREEN, SIS3D_GREEN } },
    { { { 292l, 292l }, { 476l, 292l }, { 292l, 476l } }, 0x3ffffffful,
      { SIS3D_GREEN, SIS3D_GREEN, SIS3D_GREEN } }
};

/* Three bands at alpha 40h, 80h and C0h against GREATER 80h: only the
 * last passes, and the equal one shows whether GREATER is strict. */
static const struct sis3d_draw sis3d_alpha_draws[3] = {
    { { { 36l, 20l }, { 476l, 40l }, { 72l, 156l } }, 0ul,
      { 0x4000ff00ul, 0x4000ff00ul, 0x4000ff00ul } },
    { { { 36l, 180l }, { 476l, 200l }, { 72l, 316l } }, 0ul,
      { 0x8000ff00ul, 0x8000ff00ul, 0x8000ff00ul } },
    { { { 36l, 340l }, { 476l, 360l }, { 72l, 476l } }, 0ul,
      { 0xc000ff00ul, 0xc000ff00ul, 0xc000ff00ul } }
};

/* Half-transparent red over blue: about 800Fh. */
static const struct sis3d_draw sis3d_blend_draws[1] = {
    { { { 36l, 36l }, { 476l, 72l }, { 104l, 476l } }, 0ul,
      { 0x80ff0000ul, 0x80ff0000ul, 0x80ff0000ul } }
};

/* C0h grey added to 8410h grey: every channel saturates, FFFFh. */
static const struct sis3d_draw sis3d_additive_draws[1] = {
    { { { 36l, 36l }, { 476l, 72l }, { 104l, 476l } }, 0ul,
      { 0xffc0c0c0ul, 0xffc0c0c0ul, 0xffc0c0c0ul } }
};

#define SIS3D_FLAT_ENABLE V9X_SIS3D_ENABLE_PRIM_SETUP

static const struct sis3d_scene sis3d_feature_scenes[8] = {
    { "Gouraud", 10ul, 0ul, SIS3D_GUARD, V9X_SIS3D_SHADE_GOURAUD, 4, 0l,
      SIS3D_ENGINE_OWNER, SIS3D_FLAT_ENABLE, V9X_SIS3D_CMP_ALWAYS,
      V9X_SIS3D_CMP_ALWAYS, 0ul, V9X_SIS3D_BLEND_ONE, V9X_SIS3D_BLEND_ZERO,
      sis3d_gouraud_draws, 1ul },
    { "GouraudX", 16ul, 0ul, SIS3D_GUARD, V9X_SIS3D_SHADE_GOURAUD, 4, 0l,
      SIS3D_ENGINE_OWNER, SIS3D_FLAT_ENABLE, V9X_SIS3D_CMP_ALWAYS,
      V9X_SIS3D_CMP_ALWAYS, 0ul, V9X_SIS3D_BLEND_ONE, V9X_SIS3D_BLEND_ZERO,
      sis3d_gouraud_x_draws, 1ul },
    { "GouraudY", 17ul, 0ul, SIS3D_GUARD, V9X_SIS3D_SHADE_GOURAUD, 4, 0l,
      SIS3D_ENGINE_OWNER, SIS3D_FLAT_ENABLE, V9X_SIS3D_CMP_ALWAYS,
      V9X_SIS3D_CMP_ALWAYS, 0ul, V9X_SIS3D_BLEND_ONE, V9X_SIS3D_BLEND_ZERO,
      sis3d_gouraud_y_draws, 1ul },
    { "DepthLess", 11ul, 12ul, SIS3D_GUARD, V9X_SIS3D_SHADE_FLAT_TOP, 4, 0l,
      SIS3D_ENGINE_OWNER,
      SIS3D_FLAT_ENABLE | V9X_SIS3D_ENABLE_Z_TEST | V9X_SIS3D_ENABLE_Z_WRITE,
      V9X_SIS3D_CMP_LESS, V9X_SIS3D_CMP_ALWAYS, 0ul, V9X_SIS3D_BLEND_ONE,
      V9X_SIS3D_BLEND_ZERO, sis3d_depth_draws, 4ul },
    /* ZCoverageOff counts the 1.0 quadrant unless the engine leaves it at
     * FFFFh; the Z histogram is the answer. */
    { "DepthScale", 18ul, 19ul, SIS3D_GUARD, V9X_SIS3D_SHADE_FLAT_TOP, 4,
      0l, SIS3D_ENGINE_OWNER,
      SIS3D_FLAT_ENABLE | V9X_SIS3D_ENABLE_Z_TEST | V9X_SIS3D_ENABLE_Z_WRITE,
      V9X_SIS3D_CMP_ALWAYS, V9X_SIS3D_CMP_ALWAYS, 0ul, V9X_SIS3D_BLEND_ONE,
      V9X_SIS3D_BLEND_ZERO, sis3d_depth_scale_draws, 4ul },
    { "AlphaGreater", 13ul, 0ul, SIS3D_GUARD, V9X_SIS3D_SHADE_FLAT_TOP, 4,
      0l, SIS3D_ENGINE_OWNER,
      SIS3D_FLAT_ENABLE | V9X_SIS3D_ENABLE_ALPHA_TEST, V9X_SIS3D_CMP_ALWAYS,
      V9X_SIS3D_CMP_GREATER, 0x80ul, V9X_SIS3D_BLEND_ONE,
      V9X_SIS3D_BLEND_ZERO, sis3d_alpha_draws, 3ul },
    { "BlendSrcAlpha", 14ul, 0ul, SIS3D_BLUE_565, V9X_SIS3D_SHADE_FLAT_TOP,
      4, 0l, SIS3D_ENGINE_OWNER,
      SIS3D_FLAT_ENABLE | V9X_SIS3D_ENABLE_BLEND, V9X_SIS3D_CMP_ALWAYS,
      V9X_SIS3D_CMP_ALWAYS, 0ul, V9X_SIS3D_BLEND_SRC_ALPHA,
      V9X_SIS3D_BLEND_INV_SRC_ALPHA, sis3d_blend_draws, 1ul },
    { "BlendAdditive", 15ul, 0ul, SIS3D_GREY_565, V9X_SIS3D_SHADE_FLAT_TOP,
      4, 0l, SIS3D_ENGINE_OWNER,
      SIS3D_FLAT_ENABLE | V9X_SIS3D_ENABLE_BLEND, V9X_SIS3D_CMP_ALWAYS,
      V9X_SIS3D_CMP_ALWAYS, 0ul, V9X_SIS3D_BLEND_ONE, V9X_SIS3D_BLEND_ONE,
      sis3d_additive_draws, 1ul }
};

static int sis3d_compare(DWORD function, DWORD value, DWORD reference)
{
    switch (function) {
    case V9X_SIS3D_CMP_NEVER:
        return 0;
    case V9X_SIS3D_CMP_LESS:
        return value < reference;
    case V9X_SIS3D_CMP_EQUAL:
        return value == reference;
    case V9X_SIS3D_CMP_LEQUAL:
        return value <= reference;
    case V9X_SIS3D_CMP_GREATER:
        return value > reference;
    case V9X_SIS3D_CMP_NOTEQUAL:
        return value != reference;
    case V9X_SIS3D_CMP_GEQUAL:
        return value >= reference;
    }
    return 1;
}

/* 89F8h D7: 1 when the middle vertex lies left of the long edge (measured
 * 2026-10-05). Integer cross product on the sixteenth-pixel vertices. */
static int sis3d_direction(const struct v9x_sis3d_vertex *vertices,
                           const long q[3][2])
{
    v9x_u32 top;
    v9x_u32 middle;
    v9x_u32 bottom;

    v9x_sis3d_order(vertices, &top, &middle, &bottom);
    return (q[middle][0] - q[top][0]) * (q[bottom][1] - q[top][1]) <
           (q[bottom][0] - q[top][0]) * (q[middle][1] - q[top][1]);
}

static DWORD sis3d_channel(DWORD argb, int shift)
{
    return (argb >> shift) & 0xfful;
}

/* RGB565 by truncation, as phase 1's flat green (FFh) came back 07E0h. */
static DWORD sis3d_pack565(DWORD argb)
{
    return ((sis3d_channel(argb, 16) >> 3) << 11) |
           ((sis3d_channel(argb, 8) >> 2) << 5) |
           (sis3d_channel(argb, 0) >> 3);
}

/* RGB565 to xRGB8888, replicating the high bits into the low ones. */
static DWORD sis3d_unpack565(DWORD pixel)
{
    DWORD r = (pixel >> 11) & 0x1ful;
    DWORD g = (pixel >> 5) & 0x3ful;
    DWORD b = pixel & 0x1ful;

    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) |
           ((b << 3) | (b >> 2));
}

/* The largest field difference between two RGB565 pixels. */
static DWORD sis3d_difference(DWORD a, DWORD b)
{
    static const int shifts[3] = { 11, 5, 0 };
    static const DWORD masks[3] = { 0x1ful, 0x3ful, 0x1ful };
    DWORD worst = 0ul;
    DWORD fa;
    DWORD fb;
    DWORD d;
    int index;

    for (index = 0; index < 3; ++index) {
        fa = (a >> shifts[index]) & masks[index];
        fb = (b >> shifts[index]) & masks[index];
        d = fa > fb ? fa - fb : fb - fa;
        if (d > worst) {
            worst = d;
        }
    }
    return worst;
}

/* The draw's ARGB at sample (x, y): flat, or barycentric between the
 * vertex colours. Edge values reach 2^19 on a 32x32 target, so the
 * weighted sums stay inside 31 bits. */
static DWORD sis3d_source(const struct sis3d_scene *scene,
                          const struct sis3d_draw *draw, DWORD x, DWORD y)
{
    long px = (long)x * 16l;
    long py = (long)y * 16l;
    long w0;
    long w1;
    long w2;
    long area;
    long sum;
    DWORD result = 0ul;
    int shift;

    if (scene->shade != V9X_SIS3D_SHADE_GOURAUD) {
        return draw->argb[0];
    }
    w0 = sis3d_edge(draw->q[1], draw->q[2], px, py);
    w1 = sis3d_edge(draw->q[2], draw->q[0], px, py);
    w2 = sis3d_edge(draw->q[0], draw->q[1], px, py);
    area = w0 + w1 + w2;
    for (shift = 0; shift < 32; shift += 8) {
        sum = w0 * (long)sis3d_channel(draw->argb[0], shift) +
              w1 * (long)sis3d_channel(draw->argb[1], shift) +
              w2 * (long)sis3d_channel(draw->argb[2], shift);
        result |= ((DWORD)(sum / area) & 0xfful) << shift;
    }
    return result;
}

static DWORD sis3d_factor(DWORD factor, DWORD alpha)
{
    switch (factor) {
    case V9X_SIS3D_BLEND_ZERO:
        return 0ul;
    case V9X_SIS3D_BLEND_SRC_ALPHA:
        return alpha;
    case V9X_SIS3D_BLEND_INV_SRC_ALPHA:
        return 255ul - alpha;
    }
    return 255ul;
}

static DWORD sis3d_blend(const struct sis3d_scene *scene, DWORD source,
                         DWORD destination)
{
    DWORD dst = sis3d_unpack565(destination);
    DWORD alpha = sis3d_channel(source, 24);
    DWORD sf = sis3d_factor(scene->blend_source, alpha);
    DWORD df = sis3d_factor(scene->blend_destination, alpha);
    DWORD result = 0ul;
    DWORD value;
    int shift;

    if ((scene->enable & V9X_SIS3D_ENABLE_BLEND) == 0ul) {
        return sis3d_pack565(source);
    }
    for (shift = 0; shift < 24; shift += 8) {
        value = (sis3d_channel(source, shift) * sf +
                 sis3d_channel(dst, shift) * df + 127ul) / 255ul;
        if (value > 255ul) {
            value = 255ul;
        }
        result |= value << shift;
    }
    return sis3d_pack565(result);
}

/* What the target and the Z buffer should hold after the scene. */
static void sis3d_reference(const struct sis3d_scene *scene)
{
    const struct sis3d_draw *draw;
    DWORD row;
    DWORD col;
    DWORD index;
    DWORD pixel;
    DWORD depth;
    DWORD source;

    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; ++col) {
            pixel = scene->background;
            depth = SIS3D_FLOAT_ONE;
            for (index = 0ul; index < scene->draw_count; ++index) {
                draw = &scene->draws[index];
                if (!sis3d_inside(draw->q, col, row, scene->owner)) {
                    continue;
                }
                source = sis3d_source(scene, draw, col, row);
                if ((scene->enable & V9X_SIS3D_ENABLE_ALPHA_TEST) != 0ul &&
                    !sis3d_compare(scene->alpha_compare,
                                   sis3d_channel(source, 24),
                                   scene->alpha_reference)) {
                    continue;
                }
                /* Positive IEEE singles order like their bit patterns. */
                if ((scene->enable & V9X_SIS3D_ENABLE_Z_TEST) != 0ul &&
                    !sis3d_compare(scene->z_compare, draw->z, depth)) {
                    continue;
                }
                if ((scene->enable & V9X_SIS3D_ENABLE_Z_WRITE) != 0ul) {
                    depth = draw->z;
                }
                pixel = sis3d_blend(scene, source, pixel);
            }
            sis3d_expected[row][col] = (unsigned short)pixel;
            sis3d_depth_expected[row][col] = depth;
        }
    }
}

/* Up to eight distinct values with their counts, then the rest. */
static void sis3d_histogram(const char *key,
                            unsigned short values[SIS3D_SIDE][SIS3D_SIDE])
{
    DWORD seen[SIS3D_HISTOGRAM_MAX];
    DWORD counts[SIS3D_HISTOGRAM_MAX];
    DWORD used = 0ul;
    DWORD other = 0ul;
    DWORD row;
    DWORD col;
    DWORD index;
    char text[160];
    char part[16];

    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; ++col) {
            for (index = 0ul; index < used; ++index) {
                if (seen[index] == values[row][col]) {
                    break;
                }
            }
            if (index < used) {
                ++counts[index];
            } else if (used < SIS3D_HISTOGRAM_MAX) {
                seen[used] = values[row][col];
                counts[used] = 1ul;
                ++used;
            } else {
                ++other;
            }
        }
    }
    text[0] = '\0';
    for (index = 0ul; index < used; ++index) {
        sis3d_hex(part, seen[index], 4);
        lstrcatA(text, part);
        lstrcatA(text, ":");
        sis3d_decimal(part, counts[index]);
        lstrcatA(text, part);
        lstrcatA(text, " ");
    }
    if (other != 0ul) {
        lstrcatA(text, "other:");
        sis3d_decimal(part, other);
        lstrcatA(text, part);
    }
    sis3d_write(key, text);
}

/* Clear, load the state once, fire every draw. */
static int sis3d_scene_draw(const struct sis3d_scene *scene, DWORD base,
                            DWORD z_base)
{
    struct v9x_sis3d_state state;
    struct v9x_sis3d_writes writes;
    struct v9x_sis3d_vertex vertices[3];
    const struct sis3d_draw *draw;
    char key[48];
    long scale = 1l << (scene->fraction_bits - V9X_SIS3D_Q4_SHIFT);
    DWORD index;
    DWORD vertex;
    DWORD wait_index;
    DWORD primitive;

    state.target.vram_bytes = SIS3D_VRAM_BYTES;
    state.target.offset = base;
    state.target.pitch_bytes = SIS3D_PITCH;
    state.target.width = SIS3D_SIDE;
    state.target.height = SIS3D_SIDE;
    state.enable = scene->enable;
    state.z_offset = z_base;
    state.z_pitch_bytes = SIS3D_PITCH;
    state.z_compare = scene->z_compare;
    state.alpha_compare = scene->alpha_compare;
    state.alpha_reference = scene->alpha_reference;
    state.blend_source = scene->blend_source;
    state.blend_destination = scene->blend_destination;
    if (v9x_sis3d_build_state(&state, &writes) != V9X_STATUS_OK) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "BUILD-REFUSED");
        return 0;
    }

    sis3d_begin();
    sis3d_add(SIS3D_OP_LFB_FILL32, base,
              scene->background | (scene->background << 16),
              SIS3D_REGION_STRIDE / 4ul);
    if (scene->z_region != 0ul) {
        sis3d_add(SIS3D_OP_LFB_FILL32, z_base, SIS3D_Z_CLEAR,
                  SIS3D_REGION_STRIDE / 4ul);
    }
    sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
              V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    sis3d_add_writes(&writes);

    for (index = 0ul; index < scene->draw_count; ++index) {
        draw = &scene->draws[index];
        for (vertex = 0ul; vertex < 3ul; ++vertex) {
            vertices[vertex].x = v9x_sis3d_float_fixed(
                (v9x_s32)(draw->q[vertex][0] * scale + scene->shift),
                scene->fraction_bits);
            vertices[vertex].y = v9x_sis3d_float_fixed(
                (v9x_s32)(draw->q[vertex][1] * scale + scene->shift),
                scene->fraction_bits);
            vertices[vertex].z = draw->z;
            vertices[vertex].argb = draw->argb[vertex];
            vertices[vertex].u = 0ul;
            vertices[vertex].v = 0ul;
            vertices[vertex].w = v9x_sis3d_float_q4(16);
            vertices[vertex].fog_specular = 0ul;
        }
        primitive = v9x_sis3d_primitive(vertices, scene->shade,
                                        sis3d_direction(vertices, draw->q));
        if (index == 0ul) {
            sis3d_key(key, scene->name, "Primitive");
            sis3d_write_hex(key, primitive);
        }
        v9x_sis3d_build_vertices(vertices, &writes);
        /* One triangle at a time: the Turbo Queue is off. */
        sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                  V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
        sis3d_add(SIS3D_OP_MMIO_WRITE32, V9X_SIS3D_PRIMITIVE, primitive, 0ul);
        sis3d_add_writes(&writes);
    }
    wait_index = sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                           V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    if (!sis3d_run()) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "RUN-FAILED");
        return 0;
    }
    sis3d_key(key, scene->name, "IdleWaitReads");
    sis3d_write_hex(key, sis3d_value(wait_index));
    return 1;
}

static void sis3d_run_scene(const struct sis3d_scene *scene)
{
    char key[48];
    char text[96];
    char part[32];
    char dump[SIS3D_SIDE * 4ul + 1ul];
    DWORD base = SIS3D_TEST_BASE + scene->region * SIS3D_REGION_STRIDE;
    DWORD z_base = SIS3D_TEST_BASE + scene->z_region * SIS3D_REGION_STRIDE;
    DWORD row;
    DWORD col;
    DWORD difference;
    DWORD painted = 0ul;
    DWORD expected_total = 0ul;
    DWORD off = 0ul;
    DWORD worst = 0ul;
    DWORD z_off = 0ul;

    if (!sis3d_scene_draw(scene, base, z_base)) {
        return;
    }
    if (!sis3d_read_region(base, sis3d_pixels) ||
        (scene->z_region != 0ul &&
         !sis3d_read_region(z_base, sis3d_depth))) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "READBACK-FAILED");
        return;
    }
    sis3d_reference(scene);

    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        DWORD actual_left = 0ul;
        DWORD actual_right = 0ul;
        DWORD actual_count = 0ul;
        DWORD expected_left = 0ul;
        DWORD expected_right = 0ul;
        DWORD expected_count = 0ul;
        int same = 1;

        for (col = 0ul; col < SIS3D_SIDE; ++col) {
            int hit = sis3d_pixels[row][col] != scene->background;
            int want = sis3d_expected[row][col] != scene->background;

            if (hit) {
                if (actual_count == 0ul) {
                    actual_left = col;
                }
                actual_right = col;
                ++actual_count;
            }
            if (want) {
                if (expected_count == 0ul) {
                    expected_left = col;
                }
                expected_right = col;
                ++expected_count;
            }
            if (hit != want) {
                same = 0;
            }
            difference = sis3d_difference(sis3d_pixels[row][col],
                                          sis3d_expected[row][col]);
            if (difference > worst) {
                worst = difference;
            }
            if (difference > SIS3D_COLOUR_TOLERANCE) {
                ++off;
            }
            /* Z: written exactly where the reference moved off the far
             * plane. The values themselves are reported, not judged. */
            if (scene->z_region != 0ul &&
                ((sis3d_depth[row][col] == SIS3D_Z_FAR) !=
                 (sis3d_depth_expected[row][col] == SIS3D_FLOAT_ONE))) {
                ++z_off;
            }
        }
        painted += actual_count;
        expected_total += expected_count;
        if (same) {
            continue;
        }
        sis3d_span_text(text, expected_left, expected_right, expected_count);
        lstrcatA(text, " actual ");
        sis3d_span_text(part, actual_left, actual_right, actual_count);
        lstrcatA(text, part);
        sis3d_key(key, scene->name, "Row");
        sis3d_decimal(key + lstrlenA(key), row);
        sis3d_write(key, text);
    }
    sis3d_key(key, scene->name, "Painted");
    sis3d_write_decimal(key, painted);
    sis3d_key(key, scene->name, "Expected");
    sis3d_write_decimal(key, expected_total);
    sis3d_key(key, scene->name, "Off");
    sis3d_write_decimal(key, off);
    sis3d_key(key, scene->name, "MaxError");
    sis3d_write_decimal(key, worst);
    sis3d_key(key, scene->name, "Colours");
    sis3d_histogram(key, sis3d_pixels);
    sis3d_key(key, scene->name, "ColoursExpected");
    sis3d_histogram(key, sis3d_expected);
    /* Gouraud colour is judged offline against hypotheses about where the
     * engine evaluates it: every pixel, a row per line. */
    if (scene->shade == V9X_SIS3D_SHADE_GOURAUD) {
        for (row = 0ul; row < SIS3D_SIDE; ++row) {
            for (col = 0ul; col < SIS3D_SIDE; ++col) {
                sis3d_hex(dump + col * 4ul, sis3d_pixels[row][col], 4);
            }
            sis3d_key(key, scene->name, "Pixels");
            sis3d_decimal(key + lstrlenA(key), row);
            sis3d_write(key, dump);
        }
    }
    if (scene->z_region != 0ul) {
        sis3d_key(key, scene->name, "ZValues");
        sis3d_histogram(key, sis3d_depth);
        sis3d_key(key, scene->name, "ZCoverageOff");
        sis3d_write_decimal(key, z_off);
    }
    sis3d_key(key, scene->name, "Result");
    sis3d_write(key, (painted == 0ul && expected_total != 0ul)
                     ? "NOTHING-DRAWN"
                     : (off == 0ul && z_off == 0ul) ? "MATCH" : "DIFF");
}

static void sis3d_phase2(void)
{
    /* The tie triangles at each vertex precision, moved up and left by one
     * unit of it, against Direct3D's top-left rule on the unmoved
     * vertices. 1/16 repeats phase 1's shift as the control. */
    static const int precisions[5] = { 4, 8, 10, 12, 16 };
    static const char *const names[10] = {
        "Shift4TopLeft", "Shift4BottomRight",
        "Shift8TopLeft", "Shift8BottomRight",
        "Shift10TopLeft", "Shift10BottomRight",
        "Shift12TopLeft", "Shift12BottomRight",
        "Shift16TopLeft", "Shift16BottomRight"
    };
    struct sis3d_scene scene;
    DWORD index;

    for (index = 0ul; index < 10ul; ++index) {
        scene.name = names[index];
        scene.region = index;
        scene.z_region = 0ul;
        scene.background = SIS3D_GUARD;
        scene.shade = V9X_SIS3D_SHADE_FLAT_TOP;
        scene.fraction_bits = precisions[index / 2ul];
        scene.shift = -1l;
        scene.owner = SIS3D_D3D_OWNER;
        scene.enable = SIS3D_FLAT_ENABLE;
        scene.z_compare = V9X_SIS3D_CMP_ALWAYS;
        scene.alpha_compare = V9X_SIS3D_CMP_ALWAYS;
        scene.alpha_reference = 0ul;
        scene.blend_source = V9X_SIS3D_BLEND_ONE;
        scene.blend_destination = V9X_SIS3D_BLEND_ZERO;
        scene.draws = &sis3d_tie_draws[index % 2ul];
        scene.draw_count = 1ul;
        sis3d_run_scene(&scene);
    }
    /* Additive is the table's last entry. */
    for (index = 0ul;
         index < sizeof(sis3d_feature_scenes) / sizeof(sis3d_feature_scenes[0]);
         ++index) {
        sis3d_run_scene(&sis3d_feature_scenes[index]);
    }
}

/* A switch anywhere on the command line, case-insensitive. */
static int sis3d_has_switch(const char *name)
{
    const char *line = GetCommandLineA();
    int length = lstrlenA(name);
    int index;
    char c;

    for (; *line != '\0'; ++line) {
        for (index = 0; index < length; ++index) {
            c = line[index];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
            if (c != name[index]) {
                break;
            }
        }
        if (index == length) {
            return 1;
        }
    }
    return 0;
}

void WINAPI V9xSis3dProbeEntry(void)
{
    /* Sixteenths of a pixel, on quarter-pixel positions. Right: the middle
     * vertex B lies right of the long edge A-C. Left: the mirror image. */
    static const long right[3][2] = {
        { 68l, 52l }, { 420l, 196l }, { 164l, 452l }
    };
    static const long left[3][2] = {
        { 444l, 52l }, { 92l, 196l }, { 348l, 452l }
    };
    /* Integer vertices, so edges run through sample points: the tie rule.
     * TieTopLeft has its axis-aligned edges on top and left, TieBottomRight
     * on the bottom and right; both diagonals pass through samples too.
     * Middle vertex right of the long edge in both: direction 0. */
    static const long tie_top_left[3][2] = {
        { 64l, 64l }, { 320l, 64l }, { 64l, 320l }
    };
    static const long tie_bottom_right[3][2] = {
        { 320l, 64l }, { 320l, 320l }, { 64l, 320l }
    };
    /* The same two, moved up and left by 1/16 pixel. The engine owns
     * samples on bottom and right edges (measured with the two above);
     * the shift should turn that into Direct3D's top-left ownership. */
    static const long shifted_top_left[3][2] = {
        { 63l, 63l }, { 319l, 63l }, { 63l, 319l }
    };
    static const long shifted_bottom_right[3][2] = {
        { 319l, 63l }, { 319l, 319l }, { 63l, 319l }
    };
    static const char header[] = "[Sis3dProbe]\r\n";
    HDC display;
    DWORD width = 0ul;
    DWORD height = 0ul;
    DWORD bits = 0ul;
    DWORD sr05;
    DWORD srb;
    DWORD sr27;
    DWORD sr39;
    DWORD index[8];
    DWORD written;
    int phase2 = sis3d_has_switch("/phase2");

    display = GetDC(0);
    if (display != 0) {
        width = (DWORD)GetDeviceCaps(display, HORZRES);
        height = (DWORD)GetDeviceCaps(display, VERTRES);
        bits = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                       GetDeviceCaps(display, PLANES));
        ReleaseDC(0, display);
    }

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    sis3d_output = CreateFileA(V9X_DIAG_SIS3D_TXT, GENERIC_WRITE,
                               FILE_SHARE_READ, 0, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, 0);
    if (sis3d_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(sis3d_output, header, (DWORD)lstrlenA(header), &written, 0);
    sis3d_write("Build", V9X_BUILD_ID);
    sis3d_write("Phase", phase2 ? "2" : "1");
    sis3d_write_decimal("DesktopWidth", width);
    sis3d_write_decimal("DesktopHeight", height);
    sis3d_write_decimal("DesktopBpp", bits);
    if (bits != 16ul) {
        sis3d_write("Result", "REFUSED-DEPTH");
        ExitProcess(5u);
    }
    if (width * height * 2ul >= SIS3D_TEST_BASE) {
        sis3d_write("Result", "REFUSED-DESKTOP-TOO-LARGE");
        ExitProcess(5u);
    }

    sis3d_device = CreateFileA("\\\\.\\SIS2D.VXD", 0, 0, 0, CREATE_NEW,
                               FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (sis3d_device == INVALID_HANDLE_VALUE) {
        sis3d_write("Result", "NO-VXD");
        ExitProcess(2u);
    }

    sis3d_begin();
    index[0] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR05, 0ul, 0ul);
    sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR05, SIS_SR05_KEY, 0ul);
    index[1] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR05, 0ul, 0ul);
    index[2] = sis3d_add(SIS3D_OP_SR_READ, SIS_SRB, 0ul, 0ul);
    index[3] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR27, 0ul, 0ul);
    index[4] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR39, 0ul, 0ul);
    if (!sis3d_run()) {
        sis3d_write("Result", "STATE-READ-FAILED");
        CloseHandle(sis3d_device);
        ExitProcess(3u);
    }
    sr05 = sis3d_value(index[0]);
    srb = sis3d_value(index[2]);
    sr27 = sis3d_value(index[3]);
    sr39 = sis3d_value(index[4]);
    sis3d_write_hex("SR05Found", sr05);
    sis3d_write_hex("SR05Unlocked", sis3d_value(index[1]));
    sis3d_write_hex("SRB", srb);
    sis3d_write_hex("SR27", sr27);
    sis3d_write_hex("SR39Found", sr39);
    if (sis3d_value(index[1]) != SIS_SR05_UNLOCKED ||
        (srb & SIS_SRB_MMIO_BAR1) != SIS_SRB_MMIO_BAR1 ||
        (sr27 & SIS_SR27_ENGINE) == 0ul) {
        /* The driver enables the engine window after every mode set; if it
         * is not on, this is not the driver this probe was written for. */
        sis3d_write("Result", "ENGINE-WINDOW-NOT-ENABLED");
        goto restore;
    }

    sis3d_begin();
    sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR39, sr39 | SIS_SR39_3D, 0ul);
    index[5] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR39, 0ul, 0ul);
    index[6] = sis3d_add(SIS3D_OP_MMIO_READ32, V9X_SIS3D_STATUS, 0ul, 0ul);
    index[7] = sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                         V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    if (!sis3d_run()) {
        sis3d_write("Result", "ENABLE-FAILED");
        goto restore;
    }
    sis3d_write_hex("SR39Enabled", sis3d_value(index[5]));
    sis3d_write_hex("StatusAtEnable", sis3d_value(index[6]));
    sis3d_write_hex("IdleWaitAtEnable", sis3d_value(index[7]));
    if (sis3d_value(index[7]) == SIS3D_TIMEOUT) {
        sis3d_write("Result", "3D-NOT-IDLE");
        goto restore;
    }

    if (phase2) {
        sis3d_phase2();
    } else {
        sis3d_shot("RightDir0", 0ul, right, 0);
        sis3d_shot("RightDir1", 1ul, right, 1);
        sis3d_shot("LeftDir0", 2ul, left, 0);
        sis3d_shot("LeftDir1", 3ul, left, 1);
        sis3d_shot("TieTopLeft", 4ul, tie_top_left, 0);
        sis3d_shot("TieBottomRight", 5ul, tie_bottom_right, 0);
        sis3d_shot("ShiftTopLeft", 6ul, shifted_top_left, 0);
        sis3d_shot("ShiftBottomRight", 7ul, shifted_bottom_right, 0);
    }
    sis3d_write("Result", "RAN");

restore:
    sis3d_begin();
    sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR39, sr39, 0ul);
    if (sr05 != SIS_SR05_UNLOCKED) {
        sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR05, SIS_SR05_RELOCK, 0ul);
    }
    index[0] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR39, 0ul, 0ul);
    if (sis3d_run()) {
        sis3d_write("Restored", "1");
        sis3d_write_hex("SR39Final", sis3d_value(index[0]));
    } else {
        sis3d_write("Restored", "0");
    }
    CloseHandle(sis3d_device);
    CloseHandle(sis3d_output);
    ExitProcess(0u);
}
