/*
 * SIS3D.EXE: the SiS 6326 3D engine write probe, phases 1 to 3.
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
 * Phase 3 (/phase3; /phase3a for the raw pitch sweep, /phase3m for mips)
 * answers the texture questions: the pitch encoding, U/V scale and
 * rounding, the x prestep, the addressing modes, perspective, the five
 * Direct3D formats, bilinear, the texture-blend modes and mip selection.
 * Textures live from 2 MiB + 256 KiB, targets below and above them. A
 * scene that does not go idle stops the run: the engine stays hung until
 * a reboot (2026-10-05 textures record).
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
#define SIS3D_OP_LFB_WRITE32  6ul
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

/*
 * Phase 3a: texture addressing. The texture memory holds an index texture:
 * every 16-bit word is its own word offset from the base. Read as RGB565
 * and written unchanged to an RGB565 target (colour mode Ctex), each pixel
 * then names the word the engine fetched for it, so the pitch unit, the
 * U/V scale, the nearest-texel rounding and the x prestep are all read
 * from the dumps offline.
 *
 * Every scene fills its 32x32 target. U and V are affine in screen x and
 * y: u = u0 + du * x, v = v0 + dv * y, in units of 2^-12 of whatever value
 * goes to the vertex register. Vertices move up and left by 1/256 pixel,
 * the driver's tie shift (2026-10-05 shading-and-depth record).
 */
#define SIS3D_INDEX_BASE   0x00240000ul
#define SIS3D_INDEX_WORDS  16384ul
#define SIS3D_UV_BITS      12
#define SIS3D_XY_BITS      8
#define SIS3D_WHITE        0xfffffffful
/* Texturing as SiS's HAL enabled it (8A00h = 00208CA0h without Z). */
#define SIS3D_TEXTURE_ENABLE (V9X_SIS3D_ENABLE_PRIM_SETUP | \
                              V9X_SIS3D_ENABLE_TEXTURE | \
                              V9X_SIS3D_ENABLE_TEXTURE_CACHE | \
                              V9X_SIS3D_ENABLE_LARGE_CACHE | \
                              V9X_SIS3D_ENABLE_BIT15)

struct sis3d_tex_scene {
    const char *name;
    DWORD region;
    DWORD log2_width;
    DWORD log2_height;
    DWORD pitch_bytes;       /* through the builder */
    DWORD raw_pitch_field;   /* nonzero: written over the builder's (3a) */
    DWORD mapping;           /* V9X_SIS3D_TEXTURE_WRAP_U ... */
    DWORD filter;
    DWORD levels;            /* 8A38h D[11:8] */
    int hold_clear;          /* leave D4 set while drawing, as p3a did */
    int columns_only;        /* dump columns 0 and 31, not every pixel */
    int perspective;         /* RHW from rhw_left to rhw_right across x */
    long rhw_left;           /* 2^-12 */
    long rhw_right;
    long u0;
    long du;
    long v0;
    long dv;
    const long (*triangles)[3][2];   /* sixteenths of a pixel */
    DWORD triangle_count;
    /* Zero in all of these means the index texture's settings. */
    DWORD texture_base;      /* 0: SIS3D_INDEX_BASE */
    DWORD format;            /* 0: RGB565 */
    DWORD colour_mode;       /* 8A3Ch D[31:26] */
    DWORD alpha_mode;
    int alpha_test;          /* GREATER 80h against the texel alpha */
    DWORD argb;              /* vertex colour; 0: white */
    int kind;                /* SIS3D_KIND_* */
    DWORD blend_mask_bit;    /* 8A38h D[14:12] */
};

#define SIS3D_KIND_INDEX   0   /* compare against the index texture */
#define SIS3D_KIND_FORMAT  1   /* compare against the hashed texture */
#define SIS3D_KIND_DUMP    2   /* dump only */
#define SIS3D_KIND_CENTRE  3   /* report pixel (16, 16) only */
#define SIS3D_KIND_HISTOGRAM 4 /* report the value histogram only */

/* Level L of every chain sits at the base + 700h + 100h x L: level 1 at
 * 2 KiB, past a 32x32 16-bit level 0. Only the mip scenes sample them. */
#define SIS3D_MIP_LEVEL_OFFSET 0x00000700ul
#define SIS3D_MIP_LEVEL_STRIDE 0x00000100ul

/* The whole 32x32 target as two triangles. */
static const long sis3d_quad[2][3][2] = {
    { { 0l, 0l }, { 512l, 0l }, { 0l, 512l } },
    { { 512l, 0l }, { 512l, 512l }, { 0l, 512l } }
};

/* Phase 2's three-colour triangle: its left edge crosses rows at
 * fractional x, which is where a missing x prestep shows. */
static const long sis3d_prestep_triangle[1][3][2] = {
    { { 36l, 36l }, { 476l, 104l }, { 136l, 476l } }
};

/*
 * Norm*: U/V normalised, pixel x at texel centre x + 0.5 of a 32-texel
 * texture, with three pitch fields: 64 (bytes), 32 (texels) and 280h
 * (SiS's). Texel: the same mapping in texel units. Mag4: normalised, four
 * pixels per texel with no half-texel offset, so the rounding shows.
 * Prestep: eight texels per pixel across a 256-texel row, so the fetched
 * column resolves where the engine evaluated U to 1/8 pixel.
 *
 * Every scene uses SiS's level field 1. Build p3a used 0 with D4 held set,
 * and hung the engine; the last two scenes each repeat Norm64 with one of
 * those settings, and the run stops at the first scene that does not go
 * idle.
 */
static const struct sis3d_tex_scene sis3d_addressing_scenes[8] = {
    { "Norm64", 20ul, 5ul, 5ul, 64ul, 64ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul },
    { "Norm32", 21ul, 5ul, 5ul, 64ul, 32ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul },
    { "Norm280", 22ul, 5ul, 5ul, 64ul, 0x280ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul },
    { "Texel64", 23ul, 5ul, 5ul, 64ul, 64ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      2048l, 4096l, 2048l, 4096l, sis3d_quad, 2ul },
    { "Mag4", 24ul, 5ul, 5ul, 64ul, 64ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      0l, 32l, 0l, 32l, sis3d_quad, 2ul },
    { "Prestep", 25ul, 8ul, 5ul, 512ul, 512ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_prestep_triangle, 1ul },
    { "Levels0", 26ul, 5ul, 5ul, 64ul, 64ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 0ul, 0, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul },
    { "ClearHeld", 27ul, 5ul, 5ul, 64ul, 64ul,
      V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 1, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul }
};

/*
 * Phase 3 proper, on the builder's pitch encoding. Addr and Mag4 repeat
 * Norm280 and Mag4 at the tight 64-byte pitch. Prestep uses a 256 x 32
 * texture at 512 bytes. Wrap, Mirror, Clamp and NoMapping run U from texel
 * -14.5 to 47.5 (u x 32 = 2x - 14.5; build p3e sampled the texel edges at
 * 2x - 15). Persp sends RHW 1.0 at x = 0 and 0.25 at x = 32 with U from
 * half a texel to 1 + half a texel; PerspFlat is the same with RHW 1.0
 * throughout; PerspOff sends Persp's RHW with perspective disabled.
 */
#define SIS3D_WRAP_UV  (V9X_SIS3D_TEXTURE_WRAP_U | V9X_SIS3D_TEXTURE_WRAP_V)
#define SIS3D_CLAMP_UV (V9X_SIS3D_TEXTURE_CLAMP_U | V9X_SIS3D_TEXTURE_CLAMP_V)

static const struct sis3d_tex_scene sis3d_texture_scenes[10] = {
    { "Addr", 52ul, 5ul, 5ul, 64ul, 0ul, SIS3D_WRAP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul },
    { "Mag4", 53ul, 5ul, 5ul, 64ul, 0ul, SIS3D_WRAP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      0l, 32l, 0l, 32l, sis3d_quad, 2ul },
    { "Prestep", 54ul, 8ul, 5ul, 512ul, 0ul, SIS3D_WRAP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_prestep_triangle, 1ul },
    { "Wrap", 55ul, 5ul, 5ul, 64ul, 0ul, SIS3D_WRAP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      -1856l, 256l, 64l, 128l, sis3d_quad, 2ul },
    { "Mirror", 56ul, 5ul, 5ul, 64ul, 0ul,
      V9X_SIS3D_TEXTURE_MIRROR_U | V9X_SIS3D_TEXTURE_MIRROR_V,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      -1856l, 256l, 64l, 128l, sis3d_quad, 2ul },
    { "Clamp", 57ul, 5ul, 5ul, 64ul, 0ul, SIS3D_CLAMP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      -1856l, 256l, 64l, 128l, sis3d_quad, 2ul },
    { "NoMapping", 58ul, 5ul, 5ul, 64ul, 0ul, 0ul,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 4096l,
      -1856l, 256l, 64l, 128l, sis3d_quad, 2ul },
    { "Persp", 59ul, 5ul, 5ul, 64ul, 0ul, SIS3D_CLAMP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 1, 4096l, 1024l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul },
    { "PerspFlat", 60ul, 5ul, 5ul, 64ul, 0ul, SIS3D_CLAMP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 1, 4096l, 4096l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul },
    { "PerspOff", 61ul, 5ul, 5ul, 64ul, 0ul, SIS3D_CLAMP_UV,
      V9X_SIS3D_MIN_NEAREST, 1ul, 0, 0, 0, 4096l, 1024l,
      64l, 128l, 64l, 128l, sis3d_quad, 2ul }
};

/* 16384 words, each holding its own index: 16 op lists of 512 dwords. */
static int sis3d_fill_index_texture(void)
{
    DWORD dword;
    DWORD word;

    for (dword = 0ul; dword < SIS3D_INDEX_WORDS / 2ul; ++dword) {
        if (dword % SIS3D_OP_MAX == 0ul) {
            sis3d_begin();
        }
        word = dword * 2ul;
        sis3d_add(SIS3D_OP_LFB_WRITE32, SIS3D_INDEX_BASE + dword * 4ul,
                  word | ((word + 1ul) << 16), 0ul);
        if (dword % SIS3D_OP_MAX == SIS3D_OP_MAX - 1ul && !sis3d_run()) {
            return 0;
        }
    }
    return 1;
}

#define SIS3D_UNPREDICTED 0xfffffffful

static void sis3d_set_write(struct v9x_sis3d_writes *writes, DWORD offset,
                            DWORD value)
{
    DWORD index;

    for (index = 0ul; index < writes->count; ++index) {
        if (writes->offsets[index] == offset) {
            writes->values[index] = value;
        }
    }
}

/* Floor division for a positive divisor. */
static long sis3d_floor_div(long n, long d)
{
    if (n >= 0l) {
        return n / d;
    }
    return -((-n + d - 1l) / d);
}

/* A texel coordinate through the mapping mode, or -1 where the mode
 * makes no prediction (no mapping bit set). Wrap beats mirror beats
 * clamp, as the datasheet orders them. */
static long sis3d_map_texel(long t, long size, DWORD mapping, DWORD wrap,
                            DWORD mirror, DWORD clamp)
{
    long m;

    if ((mapping & wrap) != 0ul) {
        return t - sis3d_floor_div(t, size) * size;
    }
    if ((mapping & mirror) != 0ul) {
        m = t - sis3d_floor_div(t, 2l * size) * 2l * size;
        return m < size ? m : 2l * size - 1l - m;
    }
    if ((mapping & clamp) != 0ul) {
        return t < 0l ? 0l : (t >= size ? size - 1l : t);
    }
    return -1l;
}

/*
 * The index-texture word a pixel should fetch: nearest is floor(u x W)
 * (Mag4, build p3c), U and V normalised, the sample at the integer pixel
 * (phase 1). Perspective follows Direct3D: U x RHW and RHW interpolate
 * linearly in screen x. RHW drops to 2^-8 so the products stay in 31
 * bits; the advisory count this feeds can disagree at texel boundaries.
 */
static DWORD sis3d_tex_expected(const struct sis3d_tex_scene *scene, long x,
                                long y)
{
    long width = 1l << scene->log2_width;
    long height = 1l << scene->log2_height;
    long u;
    long v;
    long tu;
    long tv;
    long rl;
    long rr;
    long ul;
    long ur;
    long index;

    if (scene->perspective) {
        rl = scene->rhw_left >> 4;
        rr = scene->rhw_right >> 4;
        ul = scene->u0;
        ur = scene->u0 + scene->du * 32l;
        u = sis3d_floor_div(ul * rl * (32l - x) + ur * rr * x,
                            rl * (32l - x) + rr * x);
    } else {
        u = scene->u0 + scene->du * x;
    }
    v = scene->v0 + scene->dv * y;
    tu = sis3d_map_texel(sis3d_floor_div(u * width, 4096l), width,
                         scene->mapping, V9X_SIS3D_TEXTURE_WRAP_U,
                         V9X_SIS3D_TEXTURE_MIRROR_U,
                         V9X_SIS3D_TEXTURE_CLAMP_U);
    tv = sis3d_map_texel(sis3d_floor_div(v * height, 4096l), height,
                         scene->mapping, V9X_SIS3D_TEXTURE_WRAP_V,
                         V9X_SIS3D_TEXTURE_MIRROR_V,
                         V9X_SIS3D_TEXTURE_CLAMP_V);
    if (tu < 0l || tv < 0l || scene->raw_pitch_field != 0ul) {
        return SIS3D_UNPREDICTED;
    }
    index = tv * (long)(scene->pitch_bytes / 2ul) + tu;
    return index < (long)SIS3D_INDEX_WORDS ? (DWORD)index
                                           : SIS3D_UNPREDICTED;
}

/* Covered pixels against the expected word; the dumps carry the rest. */
static void sis3d_tex_compare(const struct sis3d_tex_scene *scene)
{
    char key[48];
    DWORD row;
    DWORD col;
    DWORD expected;
    DWORD covered = 0ul;
    DWORD predicted = 0ul;
    DWORD mismatched = 0ul;

    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; ++col) {
            if (sis3d_pixels[row][col] == SIS3D_GUARD) {
                continue;
            }
            ++covered;
            expected = sis3d_tex_expected(scene, (long)col, (long)row);
            if (expected == SIS3D_UNPREDICTED) {
                continue;
            }
            ++predicted;
            if (sis3d_pixels[row][col] != expected) {
                ++mismatched;
            }
        }
    }
    sis3d_key(key, scene->name, "Covered");
    sis3d_write_decimal(key, covered);
    sis3d_key(key, scene->name, "Predicted");
    sis3d_write_decimal(key, predicted);
    sis3d_key(key, scene->name, "Mismatched");
    sis3d_write_decimal(key, mismatched);
}

/*
 * Textures beyond the index texture: 32x32 16-bit texels at a 64-byte
 * pitch, each in 2 KiB above it.
 */
#define SIS3D_HASH565_BASE   0x00248000ul
#define SIS3D_HASH1555_BASE  0x00248800ul
#define SIS3D_HASH4444_BASE  0x00249000ul
#define SIS3D_COLUMNS_BASE   0x00249800ul
#define SIS3D_ROWS_BASE      0x0024a000ul
#define SIS3D_SOLID_BASE     0x0024a800ul
#define SIS3D_HASH555_BASE   0x0024b000ul
/* 32x32 ARGB8888 at a 128-byte pitch: 4 KiB. */
#define SIS3D_HASH8888_BASE  0x0024c000ul
/* The marker chain: six levels, 32x32 down to 1x1. */
#define SIS3D_MIP_BASE       0x0024e000ul
/* ARGB4444 8F84h: Atex 88h, Ctex FFh/88h/44h. */
#define SIS3D_SOLID_TEXEL    0x8f84ul
/* Vertex colour for the blend modes: Apix 40h, Cpix 40h/FFh/80h. Build
 * p3f used Apix 80h, which cannot tell a mix's direction. */
#define SIS3D_BLEND_PIXEL    0x4040ff80ul
/* Masked modes read Atex bit 7, which is 1 in 88h. Build p3f left the
 * field at 0, a bit that is 0. */
#define SIS3D_BLEND_MASK_BIT 7ul

#define SIS3D_FILL_HASH      1
#define SIS3D_FILL_COLUMNS   2
#define SIS3D_FILL_ROWS      3
#define SIS3D_FILL_SOLID     4

/* Every texel different, every bit exercised. */
static DWORD sis3d_hash_texel(DWORD i, DWORD j)
{
    return (i * 0x2f1bul + j * 0x6c3dul + i * j * 0x0101ul) & 0xfffful;
}

static DWORD sis3d_fill_texel(int fill, DWORD i, DWORD j)
{
    if (fill == SIS3D_FILL_HASH) {
        return sis3d_hash_texel(i, j);
    }
    if (fill == SIS3D_FILL_COLUMNS) {
        return (i & 1ul) != 0ul ? 0xfffful : 0ul;
    }
    if (fill == SIS3D_FILL_ROWS) {
        return (j & 1ul) != 0ul ? 0xfffful : 0ul;
    }
    return SIS3D_SOLID_TEXEL;
}

/* The hashed texel of a format: 16-bit, or two hashes for ARGB8888. */
static DWORD sis3d_format_texel(DWORD format, DWORD i, DWORD j)
{
    if (format == V9X_SIS3D_TEXEL_ARGB8888) {
        return (sis3d_hash_texel(i, j) << 16) | sis3d_hash_texel(j, i);
    }
    return sis3d_hash_texel(i, j);
}

/* The hashed ARGB8888 texture: 1024 dwords, two op lists. */
static int sis3d_fill_texture32(DWORD base)
{
    DWORD j;
    DWORD i;

    for (j = 0ul; j < SIS3D_SIDE; ++j) {
        if (j % 16ul == 0ul) {
            sis3d_begin();
        }
        for (i = 0ul; i < SIS3D_SIDE; ++i) {
            sis3d_add(SIS3D_OP_LFB_WRITE32, base + j * 128ul + i * 4ul,
                      sis3d_format_texel(V9X_SIS3D_TEXEL_ARGB8888, i, j),
                      0ul);
        }
        if (j % 16ul == 15ul && !sis3d_run()) {
            return 0;
        }
    }
    return 1;
}

/* One 2 KiB texture: 512 dwords, one op list. */
static int sis3d_fill_texture(DWORD base, int fill)
{
    DWORD j;
    DWORD i;

    sis3d_begin();
    for (j = 0ul; j < SIS3D_SIDE; ++j) {
        for (i = 0ul; i < SIS3D_SIDE; i += 2ul) {
            sis3d_add(SIS3D_OP_LFB_WRITE32, base + j * 64ul + i * 2ul,
                      sis3d_fill_texel(fill, i, j) |
                      (sis3d_fill_texel(fill, i + 1ul, j) << 16), 0ul);
        }
    }
    return sis3d_run();
}

/* A texel as ARGB8888 by bit replication, which the datasheet does not
 * specify; the comparison is what tests it. */
static DWORD sis3d_expand(DWORD format, DWORD texel)
{
    DWORD a;
    DWORD r;
    DWORD g;
    DWORD b;

    if (format == V9X_SIS3D_TEXEL_ARGB1555) {
        a = (texel & 0x8000ul) != 0ul ? 0xfful : 0ul;
        r = (texel >> 10) & 0x1ful;
        g = (texel >> 5) & 0x1ful;
        b = texel & 0x1ful;
        return (a << 24) | (((r << 3) | (r >> 2)) << 16) |
               (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
    }
    if (format == V9X_SIS3D_TEXEL_RGB555) {
        r = (texel >> 10) & 0x1ful;
        g = (texel >> 5) & 0x1ful;
        b = texel & 0x1ful;
        return 0xff000000ul | (((r << 3) | (r >> 2)) << 16) |
               (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
    }
    if (format == V9X_SIS3D_TEXEL_ARGB8888) {
        return texel;
    }
    if (format == V9X_SIS3D_TEXEL_ARGB4444) {
        a = (texel >> 12) & 0xful;
        r = (texel >> 8) & 0xful;
        g = (texel >> 4) & 0xful;
        b = texel & 0xful;
        return ((a * 17ul) << 24) | ((r * 17ul) << 16) | ((g * 17ul) << 8) |
               (b * 17ul);
    }
    return 0xff000000ul | sis3d_unpack565(texel);
}

/* The hashed texture through Ctex replace into RGB565, with the alpha test
 * (GREATER 80h) removing texels at or below it. The mapping is Addr's:
 * pixel (x, y) takes texel (x, y). */
static void sis3d_format_compare(const struct sis3d_tex_scene *scene)
{
    char key[48];
    DWORD row;
    DWORD col;
    DWORD argb;
    DWORD expected;
    DWORD mismatched = 0ul;
    DWORD alpha_wrong = 0ul;
    DWORD worst = 0ul;
    DWORD difference;
    int drawn;

    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; ++col) {
            argb = sis3d_expand(scene->format,
                                sis3d_format_texel(scene->format, col, row));
            drawn = sis3d_pixels[row][col] != SIS3D_GUARD;
            if (drawn != (sis3d_channel(argb, 24) > 0x80ul)) {
                ++alpha_wrong;
                continue;
            }
            if (!drawn) {
                continue;
            }
            expected = sis3d_pack565(argb);
            difference = sis3d_difference(sis3d_pixels[row][col], expected);
            if (difference > worst) {
                worst = difference;
            }
            if (difference != 0ul) {
                ++mismatched;
            }
        }
    }
    sis3d_key(key, scene->name, "AlphaWrong");
    sis3d_write_decimal(key, alpha_wrong);
    sis3d_key(key, scene->name, "Mismatched");
    sis3d_write_decimal(key, mismatched);
    sis3d_key(key, scene->name, "MaxError");
    sis3d_write_decimal(key, worst);
}

static int sis3d_run_tex_scene(const struct sis3d_tex_scene *scene)
{
    struct v9x_sis3d_state state;
    struct v9x_sis3d_texture texture;
    struct v9x_sis3d_writes writes;
    struct v9x_sis3d_writes texture_writes;
    struct v9x_sis3d_writes clear_writes;
    struct v9x_sis3d_vertex vertices[3];
    const long (*triangle)[2];
    DWORD status_index;
    DWORD second_wait_index;
    int timed_out;
    char key[48];
    char dump[SIS3D_SIDE * 4ul + 1ul];
    DWORD base = SIS3D_TEST_BASE + scene->region * SIS3D_REGION_STRIDE;
    DWORD index;
    DWORD vertex;
    DWORD row;
    DWORD col;
    DWORD primitive;
    DWORD wait_index;

    state.target.vram_bytes = SIS3D_VRAM_BYTES;
    state.target.offset = base;
    state.target.pitch_bytes = SIS3D_PITCH;
    state.target.width = SIS3D_SIDE;
    state.target.height = SIS3D_SIDE;
    state.enable = SIS3D_TEXTURE_ENABLE |
                   (scene->perspective ? V9X_SIS3D_ENABLE_PERSPECTIVE : 0ul);
    state.z_offset = 0ul;
    state.z_pitch_bytes = SIS3D_PITCH;
    state.z_compare = V9X_SIS3D_CMP_ALWAYS;
    state.alpha_compare = scene->alpha_test ? V9X_SIS3D_CMP_GREATER
                                            : V9X_SIS3D_CMP_ALWAYS;
    state.alpha_reference = scene->alpha_test ? 0x80ul : 0ul;
    if (scene->alpha_test) {
        state.enable |= V9X_SIS3D_ENABLE_ALPHA_TEST;
    }
    state.blend_source = V9X_SIS3D_BLEND_ONE;
    state.blend_destination = V9X_SIS3D_BLEND_ZERO;

    texture.vram_bytes = SIS3D_VRAM_BYTES;
    texture.format = scene->format != 0ul ? scene->format
                                          : V9X_SIS3D_TEXEL_RGB565;
    texture.log2_width = scene->log2_width;
    texture.log2_height = scene->log2_height;
    texture.levels = scene->levels;
    texture.offset = scene->texture_base != 0ul ? scene->texture_base
                                                : SIS3D_INDEX_BASE;
    texture.pitch_bytes = scene->pitch_bytes;
    texture.mapping = scene->mapping;
    texture.filter = scene->filter;
    texture.colour_mode = scene->colour_mode;
    texture.alpha_mode = scene->alpha_mode;
    texture.clear_cache = 1;
    texture.blend_mask_bit = scene->blend_mask_bit;
    for (index = 0ul; index < V9X_SIS3D_MIP_LEVELS_MAX; ++index) {
        texture.level_offsets[index] = texture.offset +
                                       SIS3D_MIP_LEVEL_OFFSET +
                                       (index + 1ul) * SIS3D_MIP_LEVEL_STRIDE;
    }

    /* Build sis3d-20261005-p3a left D4 (clear texture cache) set while
     * drawing, and no scene went idle. It is pulsed here: the texture
     * words with D4 set, then again with it clear, as SiS's HAL left it. */
    if (v9x_sis3d_build_state(&state, &writes) != V9X_STATUS_OK ||
        v9x_sis3d_build_texture(&texture, &clear_writes) != V9X_STATUS_OK) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "BUILD-REFUSED");
        return 1;
    }
    texture.clear_cache = scene->hold_clear;
    if (v9x_sis3d_build_texture(&texture, &texture_writes) !=
        V9X_STATUS_OK) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "BUILD-REFUSED");
        return 1;
    }
    /* Phase 3a measured the raw field the builder now encodes. */
    if (scene->raw_pitch_field != 0ul) {
        sis3d_set_write(&clear_writes, V9X_SIS3D_TEXTURE_PITCH01,
                        scene->raw_pitch_field << 16);
        sis3d_set_write(&texture_writes, V9X_SIS3D_TEXTURE_PITCH01,
                        scene->raw_pitch_field << 16);
    }

    sis3d_begin();
    sis3d_add(SIS3D_OP_LFB_FILL32, base, 0xa5a5a5a5ul,
              SIS3D_REGION_STRIDE / 4ul);
    sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
              V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    sis3d_add_writes(&writes);
    sis3d_add_writes(&clear_writes);
    sis3d_add_writes(&texture_writes);
    for (index = 0ul; index < scene->triangle_count; ++index) {
        triangle = scene->triangles[index];
        for (vertex = 0ul; vertex < 3ul; ++vertex) {
            long qx = triangle[vertex][0];
            long qy = triangle[vertex][1];

            vertices[vertex].x = v9x_sis3d_float_fixed(
                (v9x_s32)(qx * 16l - 1l), SIS3D_XY_BITS);
            vertices[vertex].y = v9x_sis3d_float_fixed(
                (v9x_s32)(qy * 16l - 1l), SIS3D_XY_BITS);
            vertices[vertex].z = 0ul;
            vertices[vertex].argb = scene->argb != 0ul ? scene->argb
                                                       : SIS3D_WHITE;
            vertices[vertex].u = v9x_sis3d_float_fixed(
                (v9x_s32)(scene->u0 + scene->du * qx / 16l), SIS3D_UV_BITS);
            vertices[vertex].v = v9x_sis3d_float_fixed(
                (v9x_s32)(scene->v0 + scene->dv * qy / 16l), SIS3D_UV_BITS);
            /* RHW is affine in screen x; U and V go unmultiplied, as in
             * a Direct3D TLVERTEX. */
            vertices[vertex].w = v9x_sis3d_float_fixed(
                (v9x_s32)(scene->rhw_left +
                          (scene->rhw_right - scene->rhw_left) * qx / 512l),
                SIS3D_UV_BITS);
            vertices[vertex].fog_specular = 0ul;
        }
        primitive = v9x_sis3d_primitive(vertices, V9X_SIS3D_SHADE_FLAT_TOP,
                                        sis3d_direction(vertices, triangle));
        v9x_sis3d_build_vertices(vertices, &writes);
        sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                  V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
        sis3d_add(SIS3D_OP_MMIO_WRITE32, V9X_SIS3D_PRIMITIVE, primitive, 0ul);
        sis3d_add_writes(&writes);
    }
    wait_index = sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                           V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    status_index = sis3d_add(SIS3D_OP_MMIO_READ32, V9X_SIS3D_STATUS, 0ul,
                             0ul);
    second_wait_index = sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                                  V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    if (!sis3d_run()) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "RUN-FAILED");
        return 0;
    }
    sis3d_key(key, scene->name, "TextureSet");
    sis3d_write_hex(key, texture_writes.values[0]);
    sis3d_key(key, scene->name, "IdleWaitReads");
    sis3d_write_hex(key, sis3d_value(wait_index));
    sis3d_key(key, scene->name, "StatusAfterWait");
    sis3d_write_hex(key, sis3d_value(status_index));
    sis3d_key(key, scene->name, "SecondWaitReads");
    sis3d_write_hex(key, sis3d_value(second_wait_index));
    /* Taken now: the readback reuses the result buffer. Builds p3c-p3e
     * tested it after the readback, against a pixel pair. */
    timed_out = sis3d_value(second_wait_index) == SIS3D_TIMEOUT;
    if (!sis3d_read_region(base, sis3d_pixels)) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "READBACK-FAILED");
        return 0;
    }
    if (scene->kind == SIS3D_KIND_CENTRE) {
        sis3d_key(key, scene->name, "Centre");
        sis3d_write_hex(key, sis3d_pixels[SIS3D_SIDE / 2ul][SIS3D_SIDE / 2ul]);
    } else if (scene->kind == SIS3D_KIND_HISTOGRAM) {
        sis3d_key(key, scene->name, "Colours");
        sis3d_histogram(key, sis3d_pixels);
    } else if (scene->columns_only) {
        for (row = 0ul; row < SIS3D_SIDE; ++row) {
            sis3d_hex(dump + row * 4ul, sis3d_pixels[row][0], 4);
        }
        sis3d_key(key, scene->name, "Column0");
        sis3d_write(key, dump);
        for (row = 0ul; row < SIS3D_SIDE; ++row) {
            sis3d_hex(dump + row * 4ul,
                      sis3d_pixels[row][SIS3D_SIDE - 1ul], 4);
        }
        sis3d_key(key, scene->name, "Column31");
        sis3d_write(key, dump);
    } else {
        for (row = 0ul; row < SIS3D_SIDE; ++row) {
            for (col = 0ul; col < SIS3D_SIDE; ++col) {
                sis3d_hex(dump + col * 4ul, sis3d_pixels[row][col], 4);
            }
            sis3d_key(key, scene->name, "Pixels");
            sis3d_decimal(key + lstrlenA(key), row);
            sis3d_write(key, dump);
        }
        if (scene->kind == SIS3D_KIND_INDEX) {
            sis3d_tex_compare(scene);
        } else if (scene->kind == SIS3D_KIND_FORMAT) {
            sis3d_format_compare(scene);
        }
    }
    /* A scene that never went idle leaves the engine hung: dump it, then
     * stop the run. */
    if (timed_out) {
        sis3d_key(key, scene->name, "Result");
        sis3d_write(key, "NOT-IDLE");
        return 0;
    }
    sis3d_key(key, scene->name, "Result");
    sis3d_write(key, "DUMPED");
    return 1;
}

/*
 * The pitch field is not a plain unit: on build p3c, 280h gave 64-word
 * rows (SiS's value for a 128-byte row), while 40h and 20h gave row terms
 * of 102h and 82h words, OR-ed into the column rather than added. The
 * sweep varies the high bits with the low byte at 80h, the low byte with
 * the high bits at 2, and single low bits, on Norm280's mapping.
 */
static const DWORD sis3d_pitch_sweep[24] = {
    0x080ul, 0x180ul, 0x280ul, 0x380ul, 0x480ul, 0x580ul, 0x680ul, 0x780ul,
    0x200ul, 0x201ul, 0x210ul, 0x220ul, 0x240ul, 0x2c0ul, 0x2fful, 0x100ul,
    0x001ul, 0x002ul, 0x004ul, 0x008ul, 0x010ul, 0x020ul, 0x040ul, 0x400ul
};

static int sis3d_run_pitch_sweep(void)
{
    static char names[24][12];
    struct sis3d_tex_scene scene;
    DWORD index;

    scene = sis3d_addressing_scenes[2];
    scene.columns_only = 1;
    for (index = 0ul; index < 24ul; ++index) {
        lstrcpyA(names[index], "Pitch");
        sis3d_hex(names[index] + 5, sis3d_pitch_sweep[index], 3);
        scene.name = names[index];
        scene.region = 28ul + index;
        scene.raw_pitch_field = sis3d_pitch_sweep[index];
        if (!sis3d_run_tex_scene(&scene)) {
            sis3d_write("StoppedAt", scene.name);
            return 0;
        }
    }
    return 1;
}

static int sis3d_run_tex_scenes(const struct sis3d_tex_scene *scenes,
                                DWORD count)
{
    DWORD index;

    for (index = 0ul; index < count; ++index) {
        if (!sis3d_run_tex_scene(&scenes[index])) {
            sis3d_write("StoppedAt", scenes[index].name);
            return 0;
        }
    }
    return 1;
}

/* Level L of the marker chain: every texel (L + 1) x 0841h; or, for the
 * contrast chain, black on even levels and white on odd ones, so a blend
 * between two levels shows its weight in 31 steps of red. */
static DWORD sis3d_mip_marker(DWORD level, int contrast)
{
    if (contrast) {
        return (level & 1ul) != 0ul ? 0xfffful : 0x0000ul;
    }
    return (level + 1ul) * 0x0841ul;
}

static int sis3d_fill_mip_chain(int contrast)
{
    DWORD level;
    DWORD side;
    DWORD pitch;
    DWORD base;
    DWORD row;
    DWORD col;
    DWORD marker;

    for (level = 0ul; level <= 5ul; ++level) {
        side = SIS3D_SIDE >> level;
        pitch = side * 2ul < 4ul ? 4ul : side * 2ul;
        base = level == 0ul ? SIS3D_MIP_BASE
                            : SIS3D_MIP_BASE + SIS3D_MIP_LEVEL_OFFSET +
                              level * SIS3D_MIP_LEVEL_STRIDE;
        marker = sis3d_mip_marker(level, contrast);
        sis3d_begin();
        for (row = 0ul; row < side; ++row) {
            for (col = 0ul; col < pitch; col += 4ul) {
                sis3d_add(SIS3D_OP_LFB_WRITE32, base + row * pitch + col,
                          marker | (marker << 16), 0ul);
            }
        }
        if (!sis3d_run()) {
            return 0;
        }
    }
    return 1;
}

/*
 * Build p3h: codes 4 and 5 blended between levels (0861h between the
 * level 0 and level 1 markers at 1.5), but the markers differ by a step or
 * two, so most blends truncated away. The contrast chain makes the level
 * weight the red channel. Codes 2-5 at five minifications, then a dump of
 * code 5 at 1.5 and of code 2 at 2.
 */
static void sis3d_phase3_mip_blend(void)
{
    static const long ratios[5] = { 160l, 192l, 224l, 320l, 384l };
    static const char *const ratio_names[5] = {
        "1.25", "1.5", "1.75", "2.5", "3"
    };
    static char names[20][24];
    struct sis3d_tex_scene scene;
    DWORD index;

    if (!sis3d_fill_mip_chain(1)) {
        sis3d_write("ContrastChain", "FILL-FAILED");
        return;
    }
    scene = sis3d_texture_scenes[0];
    scene.texture_base = SIS3D_MIP_BASE;
    scene.levels = 5ul;
    scene.kind = SIS3D_KIND_HISTOGRAM;
    for (index = 0ul; index < 20ul; ++index) {
        lstrcpyA(names[index], "MipBlend");
        sis3d_decimal(names[index] + 8, 2ul + index / 5ul);
        lstrcatA(names[index], "at");
        lstrcatA(names[index], ratio_names[index % 5ul]);
        scene.name = names[index];
        scene.region = 195ul + index;
        scene.filter = 2ul + index / 5ul;
        scene.du = ratios[index % 5ul];
        scene.dv = scene.du;
        if (!sis3d_run_tex_scene(&scene)) {
            sis3d_write("StoppedAt", scene.name);
            return;
        }
    }
    scene.kind = SIS3D_KIND_DUMP;
    scene.name = "MipBlendDump5at1.5";
    scene.region = 215ul;
    scene.filter = 5ul;
    scene.du = 192l;
    scene.dv = 192l;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    scene.name = "MipBlendDump2at2";
    scene.region = 216ul;
    scene.filter = 2ul;
    scene.du = 256l;
    scene.dv = 256l;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
    }
}

/*
 * Build p3g: 1.5 and 2 texels per pixel each returned a level above the
 * rest on part of the target, and nearest-mip-linear returned pure
 * markers. MipDump* show where; MipFine* pin the rounding between 1 and
 * 4; MipFilter* try minification codes 3-5, which name a blend between
 * levels.
 */
static void sis3d_phase3_mip_detail(void)
{
    static const long fine[8] = {
        141l, 160l, 224l, 243l, 269l, 320l, 448l, 499l
    };
    static const char *const fine_names[8] = {
        "MipFine1.1", "MipFine1.25", "MipFine1.75", "MipFine1.9",
        "MipFine2.1", "MipFine2.5", "MipFine3.5", "MipFine3.9"
    };
    static const char *const filter_names[6] = {
        "MipFilter3at1.5", "MipFilter4at1.5", "MipFilter5at1.5",
        "MipFilter3at2.5", "MipFilter4at2.5", "MipFilter5at2.5"
    };
    struct sis3d_tex_scene scene;
    DWORD index;

    scene = sis3d_texture_scenes[0];
    scene.texture_base = SIS3D_MIP_BASE;
    scene.levels = 5ul;
    scene.filter = V9X_SIS3D_MIN_NEAREST_MIP_NEAREST;
    scene.kind = SIS3D_KIND_DUMP;
    scene.name = "MipDump1.5";
    scene.region = 179ul;
    scene.du = 192l;
    scene.dv = 192l;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    scene.name = "MipDump2";
    scene.region = 180ul;
    scene.du = 256l;
    scene.dv = 256l;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    scene.kind = SIS3D_KIND_HISTOGRAM;
    for (index = 0ul; index < 8ul; ++index) {
        scene.name = fine_names[index];
        scene.region = 181ul + index;
        scene.du = fine[index];
        scene.dv = fine[index];
        if (!sis3d_run_tex_scene(&scene)) {
            sis3d_write("StoppedAt", scene.name);
            return;
        }
    }
    for (index = 0ul; index < 6ul; ++index) {
        scene.name = filter_names[index];
        scene.region = 189ul + index;
        scene.filter = 3ul + index % 3ul;
        scene.du = index < 3ul ? 192l : 320l;
        scene.dv = scene.du;
        if (!sis3d_run_tex_scene(&scene)) {
            sis3d_write("StoppedAt", scene.name);
            return;
        }
    }
    sis3d_phase3_mip_blend();
}

/*
 * Which level the engine picks: the histogram of each scene names the
 * markers it returned (level L is (L + 1) x 0841h).
 */
static void sis3d_phase3_mips(void)
{
    static const long ratios[9] = {
        128l, 192l, 256l, 384l, 512l, 768l, 1024l, 2048l, 4096l
    };
    static const char *const names[9] = {
        "Mip1", "Mip1.5", "Mip2", "Mip3", "Mip4", "Mip6", "Mip8", "Mip16",
        "Mip32"
    };
    struct sis3d_tex_scene scene;
    DWORD index;

    if (!sis3d_fill_mip_chain(0)) {
        sis3d_write("MipChain", "FILL-FAILED");
        return;
    }
    for (index = 0ul; index < 9ul; ++index) {
        scene = sis3d_texture_scenes[0];
        scene.name = names[index];
        scene.region = 167ul + index;
        scene.texture_base = SIS3D_MIP_BASE;
        scene.levels = 5ul;
        scene.filter = V9X_SIS3D_MIN_NEAREST_MIP_NEAREST;
        scene.du = ratios[index];
        scene.dv = ratios[index];
        scene.kind = SIS3D_KIND_HISTOGRAM;
        if (!sis3d_run_tex_scene(&scene)) {
            sis3d_write("StoppedAt", scene.name);
            return;
        }
    }
    scene.name = "MipAniso";
    scene.region = 176ul;
    scene.du = 512l;
    scene.dv = 128l;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    scene.name = "MipLinear1.5";
    scene.region = 177ul;
    scene.du = 192l;
    scene.dv = 192l;
    scene.filter = 3ul;          /* nearest within, linear between levels */
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    scene.name = "MipPersp";
    scene.region = 178ul;
    scene.filter = V9X_SIS3D_MIN_NEAREST_MIP_NEAREST;
    scene.perspective = 1;
    scene.rhw_left = 4096l;
    scene.rhw_right = 1024l;
    scene.du = 512l;
    scene.dv = 128l;
    scene.kind = SIS3D_KIND_DUMP;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    sis3d_phase3_mip_detail();
}

/*
 * Formats, bilinear filtering and the texture-blend modes, on Addr's
 * mapping (texel (x, y) at pixel (x, y)) unless a scene says otherwise:
 *
 *   - Format565/1555/4444: the hashed texture through Ctex replace, alpha
 *     test GREATER 80h on the texel alpha;
 *   - LinearU/LinearV: alternating black and white columns (rows), eight
 *     pixels per texel along that axis with no offset, so the grey ramp
 *     shows where the filter puts texel centres;
 *   - Blend0-63: the solid ARGB4444 texel (Atex 88h, FF/88/44) against
 *     vertex colour 40h, 40/FF/80, every colour mode, one pixel reported;
 *   - Format555/8888: as the other formats, ARGB8888 at a 128-byte pitch;
 *   - Mip*: a six-level chain, each level one marker colour, sampled
 *     nearest-mip-nearest at minifications 1 to 32 (texels per pixel),
 *     anisotropic (4 in u, 1 in v), under perspective, and
 *     nearest-mip-linear at 1.5.
 */
static void sis3d_phase3_textures(void)
{
    static const DWORD formats[3] = {
        V9X_SIS3D_TEXEL_RGB565, V9X_SIS3D_TEXEL_ARGB1555,
        V9X_SIS3D_TEXEL_ARGB4444
    };
    static const DWORD bases[3] = {
        SIS3D_HASH565_BASE, SIS3D_HASH1555_BASE, SIS3D_HASH4444_BASE
    };
    static const char *const format_names[3] = {
        "Format565", "Format1555", "Format4444"
    };
    static char blend_names[64][10];
    struct sis3d_tex_scene scene;
    DWORD index;

    if (!sis3d_fill_texture(SIS3D_HASH565_BASE, SIS3D_FILL_HASH) ||
        !sis3d_fill_texture(SIS3D_HASH1555_BASE, SIS3D_FILL_HASH) ||
        !sis3d_fill_texture(SIS3D_HASH4444_BASE, SIS3D_FILL_HASH) ||
        !sis3d_fill_texture(SIS3D_COLUMNS_BASE, SIS3D_FILL_COLUMNS) ||
        !sis3d_fill_texture(SIS3D_ROWS_BASE, SIS3D_FILL_ROWS) ||
        !sis3d_fill_texture(SIS3D_SOLID_BASE, SIS3D_FILL_SOLID)) {
        sis3d_write("Textures", "FILL-FAILED");
        return;
    }

    for (index = 0ul; index < 3ul; ++index) {
        scene = sis3d_texture_scenes[0];
        scene.name = format_names[index];
        scene.region = 80ul + index;
        scene.texture_base = bases[index];
        scene.format = formats[index];
        scene.alpha_test = 1;
        scene.kind = SIS3D_KIND_FORMAT;
        if (!sis3d_run_tex_scene(&scene)) {
            sis3d_write("StoppedAt", scene.name);
            return;
        }
    }

    scene = sis3d_texture_scenes[0];
    scene.name = "LinearU";
    scene.region = 83ul;
    scene.texture_base = SIS3D_COLUMNS_BASE;
    scene.mapping = SIS3D_CLAMP_UV;
    scene.filter = V9X_SIS3D_MAG_LINEAR | V9X_SIS3D_MIN_LINEAR;
    scene.u0 = 0l;
    scene.du = 16l;
    scene.kind = SIS3D_KIND_DUMP;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    scene.name = "LinearV";
    scene.region = 84ul;
    scene.texture_base = SIS3D_ROWS_BASE;
    scene.u0 = 64l;
    scene.du = 128l;
    scene.v0 = 0l;
    scene.dv = 16l;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }

    for (index = 0ul; index < 64ul; ++index) {
        scene = sis3d_texture_scenes[0];
        lstrcpyA(blend_names[index], "Blend");
        sis3d_decimal(blend_names[index] + 5, index);
        scene.name = blend_names[index];
        scene.region = 101ul + index;
        scene.texture_base = SIS3D_SOLID_BASE;
        scene.format = V9X_SIS3D_TEXEL_ARGB4444;
        scene.colour_mode = index;
        scene.argb = SIS3D_BLEND_PIXEL;
        scene.blend_mask_bit = SIS3D_BLEND_MASK_BIT;
        scene.kind = SIS3D_KIND_CENTRE;
        if (!sis3d_run_tex_scene(&scene)) {
            sis3d_write("StoppedAt", scene.name);
            return;
        }
    }

    if (!sis3d_fill_texture(SIS3D_HASH555_BASE, SIS3D_FILL_HASH) ||
        !sis3d_fill_texture32(SIS3D_HASH8888_BASE)) {
        sis3d_write("Textures", "FILL-FAILED");
        return;
    }
    scene = sis3d_texture_scenes[0];
    scene.name = "Format555";
    scene.region = 165ul;
    scene.texture_base = SIS3D_HASH555_BASE;
    scene.format = V9X_SIS3D_TEXEL_RGB555;
    scene.alpha_test = 1;
    scene.kind = SIS3D_KIND_FORMAT;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }
    scene.name = "Format8888";
    scene.region = 166ul;
    scene.texture_base = SIS3D_HASH8888_BASE;
    scene.format = V9X_SIS3D_TEXEL_ARGB8888;
    scene.pitch_bytes = 128ul;
    if (!sis3d_run_tex_scene(&scene)) {
        sis3d_write("StoppedAt", scene.name);
        return;
    }

    sis3d_phase3_mips();
}

/* /phase3a repeats builds p3c and p3d (raw pitch fields); /phase3 runs
 * the scenes on the builder's encoding. */
static void sis3d_phase3(int addressing, int mips_only)
{
    if (mips_only) {
        sis3d_phase3_mips();
        return;
    }
    if (!sis3d_fill_index_texture()) {
        sis3d_write("IndexTexture", "FILL-FAILED");
        return;
    }
    sis3d_write("IndexTexture", "FILLED");
    if (addressing) {
        if (!sis3d_run_pitch_sweep()) {
            return;
        }
        sis3d_run_tex_scenes(sis3d_addressing_scenes,
                             sizeof(sis3d_addressing_scenes) /
                             sizeof(sis3d_addressing_scenes[0]));
        return;
    }
    if (!sis3d_run_tex_scenes(sis3d_texture_scenes,
                              sizeof(sis3d_texture_scenes) /
                              sizeof(sis3d_texture_scenes[0]))) {
        return;
    }
    sis3d_phase3_textures();
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
    int phase3a = sis3d_has_switch("/phase3a");
    int phase3m = sis3d_has_switch("/phase3m");
    int phase3 = phase3a || phase3m || sis3d_has_switch("/phase3");

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
    sis3d_write("Phase", phase3a ? "3a" : phase3m ? "3m" : phase3 ? "3"
                         : phase2 ? "2" : "1");
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

    if (phase3) {
        sis3d_phase3(phase3a, phase3m);
    } else if (phase2) {
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
