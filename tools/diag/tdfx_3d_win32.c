/*
 * TDFX3D.EXE: the 3dfx VSA-100 3D write probe.
 *
 * Step 2 of docs\plans\3dfx-vsa100-d3d.md: does the 3D engine, fed through
 * the triangle setup unit with exactly the writes src\chipsets\tdfx\
 * tdfx_3d.c builds, draw what a CPU model of the same draw says? Publishes
 * C:\V9XDIAG\TDFX3D.TXT. Runs beside TDFX3D.VXD, which executes the op
 * lists this side builds.
 *
 * It draws only on a 64x32 RGB565 surface at VRAM 24 MiB, its Z16 buffer a
 * page above and a 32x32 texture above that: in DirectDraw's heap, above the
 * desktop (15 MiB up) and far from Glide's memory below it, and in memory
 * nothing allocates while this runs. Do not run it while a Glide title is
 * open: Glide owns the 3D engine then.
 *
 * Every test prefills its surfaces through the framebuffer, writes nopCMD
 * and waits for idle, writes the builder's state and triangles - waiting for
 * PCI FIFO room before each run of 16 writes - writes nopCMD, waits again,
 * reads the surface back and compares with the model.
 *
 * The model is integer: a pixel whose centre lies at least a pixel inside
 * every edge must be drawn, one at least a pixel outside some edge must not,
 * and the band between is counted but not judged, because the chip's fill
 * rule and sub-pixel precision are what the probe measures, not assumes.
 *
 * Tests, cheapest first:
 *   Flat      - one red triangle; inside pixels exactly F800h.
 *   Gouraud   - red, green, blue corners; inside pixels within one step of
 *               the interpolated colour in each channel.
 *   Depth     - three full-surface draws at Z 40000, 20000 (LESS: drawn)
 *               and 60000 (LESS: not drawn); all green, and the Z buffer
 *               read back.
 *   Blend     - a half-alpha red full-surface draw over blue, source alpha /
 *               inverse source alpha; each channel within one step.
 *   Texture   - a 32x32 565 texture whose texel (x, y) is y*32+x, replaced,
 *               point sampled, onto the left 32x32 with S and T in steps of
 *               8 per pixel (the hypothesis that the chip counts S and T in
 *               256ths of the texture's width); the right half untouched.
 * Switch /flat runs only Flat.
 *
 * Register meanings: docs\specifications\vsa100-registers.md section 8.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/tdfx_3d.h"

#include "../../src/chipsets/tdfx/tdfx_3d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define TDFX3D_MAGIC 0x44335854ul
#define TDFX3D_OP_MAX 512u
#define TDFX3D_BLOCK_DWORDS 1024u
#define TDFX3D_TIMEOUT 0xfffffffful

#define TDFX3D_OP_MMIO_WRITE32 1ul
#define TDFX3D_OP_MMIO_READ32 2ul
#define TDFX3D_OP_LFB_FILL32 5ul
#define TDFX3D_OP_LFB_GRADIENT 6ul
#define TDFX3D_OP_LFB_READ_BLOCK 7ul
#define TDFX3D_OP_WAIT_IDLE 8ul
#define TDFX3D_OP_WAIT_FIFO 9ul

#define TDFX3D_RAN 0x00000004ul

/* status bit 9: any unit or FIFO busy (vsa100-registers.md section 3). */
#define V9X_TDFX_STATUS_BUSY_3D 0x00000200ul

/* The surfaces. Colour and depth one page apart, so their bit-12 parity
 * differs (D36). Each surface is exactly one read block. */
#define TEST_BASE 0x01800000ul
#define DEPTH_BASE 0x01801000ul
#define TEXTURE_BASE 0x01804000ul
#define TEST_WIDTH 64
#define TEST_HEIGHT 32
#define TEST_PITCH 128ul
#define TEST_DWORDS (TEST_PITCH * TEST_HEIGHT / 4ul)
#define TEST_VRAM 0x01fff000ul
#define TEXTURE_LOG2 5ul
#define TEXTURE_SIZE 32
#define TEXTURE_DWORDS (TEXTURE_SIZE * TEXTURE_SIZE / 2)

/* The desktop sits at 15 MiB (tdfx_hw16.c); the surfaces must clear it. */
#define DESKTOP_OFFSET 0x00f00000ul

/* PCI FIFO room asked for before each run of writes (32 entries). */
#define FIFO_CHUNK 16u

/* Prefill colours. */
#define PREFILL_GREY 0x18e3u
#define PREFILL_BLUE 0x001fu

struct tdfx3d_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct tdfx3d_request {
    DWORD count;
    struct tdfx3d_op ops[TDFX3D_OP_MAX];
};

struct tdfx3d_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar1;
    DWORD executed;
    DWORD refused;
    struct tdfx3d_op ops[TDFX3D_OP_MAX];
    DWORD block[TDFX3D_BLOCK_DWORDS];
};

/* An integer vertex: pixels, colour 0-255, Z in depth counts, S/T. */
struct tdfx3d_ivertex {
    long x;
    long y;
    long r;
    long g;
    long b;
    long a;
    long z;
    long s;
    long t;
};

static struct tdfx3d_request tdfx3d_request_buffer;
static struct tdfx3d_result tdfx3d_result_buffer;
static HANDLE tdfx3d_device;
static HANDLE tdfx3d_output;
static DWORD tdfx3d_failed;
static DWORD tdfx3d_fifo_waits;

static void tdfx3d_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void tdfx3d_decimal(char *text, DWORD value)
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

static void tdfx3d_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(tdfx3d_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(tdfx3d_output, "=", 1u, &written, 0);
    WriteFile(tdfx3d_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(tdfx3d_output, "\r\n", 2u, &written, 0);
    FlushFileBuffers(tdfx3d_output);
}

static void tdfx3d_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    tdfx3d_hex(text + 2, value, 8);
    tdfx3d_write(key, text);
}

static void tdfx3d_write_decimal(const char *key, DWORD value)
{
    char text[16];

    tdfx3d_decimal(text, value);
    tdfx3d_write(key, text);
}

static const char *tdfx3d_key(char *key, const char *prefix,
                              const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
    return key;
}

static int tdfx3d_has_switch(const char *command_line, const char *name)
{
    if (command_line == 0) {
        return 0;
    }
    while (*command_line != '\0') {
        const char *a = command_line;
        const char *b = name;

        while (*b != '\0') {
            char left = *a;
            char right = *b;

            if (left >= 'a' && left <= 'z') {
                left = (char)(left - 32);
            }
            if (right >= 'a' && right <= 'z') {
                right = (char)(right - 32);
            }
            if (left != right) {
                break;
            }
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return 1;
        }
        ++command_line;
    }
    return 0;
}

static void tdfx3d_begin(void)
{
    tdfx3d_request_buffer.count = 0u;
}

/* The index of the op added, for reading its result. */
static DWORD tdfx3d_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct tdfx3d_op *op;

    if (tdfx3d_request_buffer.count >= TDFX3D_OP_MAX) {
        return TDFX3D_OP_MAX;
    }
    op = &tdfx3d_request_buffer.ops[tdfx3d_request_buffer.count];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0u;
    return tdfx3d_request_buffer.count++;
}

/* The builder's writes, with a FIFO-room wait before every chunk. */
static void tdfx3d_add_words(const struct v9x_tdfx_3d_words *words)
{
    v9x_u32 index;

    for (index = 0u; index < words->count; ++index) {
        if ((index % FIFO_CHUNK) == 0u) {
            tdfx3d_add(TDFX3D_OP_WAIT_FIFO, FIFO_CHUNK, 0u, 0u);
            ++tdfx3d_fifo_waits;
        }
        tdfx3d_add(TDFX3D_OP_MMIO_WRITE32,
                   V9X_TDFX_3D_BASE + words->offsets[index],
                   words->values[index], 0u);
    }
}

static DWORD tdfx3d_add_flush(void)
{
    tdfx3d_add(TDFX3D_OP_WAIT_FIFO, 1u, 0u, 0u);
    tdfx3d_add(TDFX3D_OP_MMIO_WRITE32, V9X_TDFX_3D_BASE + V9X_TDFX_3D_NOP_CMD,
               0u, 0u);
    return tdfx3d_add(TDFX3D_OP_WAIT_IDLE, 0u, 0u, 0u);
}

/* Non-zero when the VxD ran every op. */
static int tdfx3d_run(void)
{
    DWORD returned = 0u;
    DWORD bytes = 4u + tdfx3d_request_buffer.count *
                       (DWORD)sizeof(struct tdfx3d_op);

    if (tdfx3d_request_buffer.count >= TDFX3D_OP_MAX) {
        return 0;
    }
    if (!DeviceIoControl(tdfx3d_device, 1u, &tdfx3d_request_buffer, bytes,
                         &tdfx3d_result_buffer, sizeof(tdfx3d_result_buffer),
                         &returned, 0) ||
        returned != sizeof(tdfx3d_result_buffer) ||
        tdfx3d_result_buffer.magic != TDFX3D_MAGIC) {
        return 0;
    }
    return (tdfx3d_result_buffer.status & TDFX3D_RAN) != 0ul &&
           tdfx3d_result_buffer.executed == tdfx3d_request_buffer.count;
}

static void tdfx3d_base_state(struct v9x_tdfx_3d_state *state)
{
    BYTE *bytes = (BYTE *)state;
    DWORD index;

    for (index = 0u; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    state->vram_bytes = TEST_VRAM;
    state->target.offset = TEST_BASE;
    state->target.pitch_bytes = TEST_PITCH;
    state->width = (v9x_u32)TEST_WIDTH;
    state->height = (v9x_u32)TEST_HEIGHT;
}

static void tdfx3d_to_vertex(const struct tdfx3d_ivertex *in,
                             struct v9x_tdfx_3d_vertex *out)
{
    out->x = (float)in->x;
    out->y = (float)in->y;
    out->z = (float)in->z;
    out->oow = 1.0f;
    out->r = (float)in->r;
    out->g = (float)in->g;
    out->b = (float)in->b;
    out->a = (float)in->a;
    out->sow = (float)in->s;
    out->tow = (float)in->t;
}

/*
 * Twice the signed edge function of point (px, py) against edge a->b, with
 * the point in half-pixel units: positive on the inside of a clockwise (in
 * screen space, y down) triangle.
 */
static long tdfx3d_edge2(const struct tdfx3d_ivertex *a,
                         const struct tdfx3d_ivertex *b, long px2, long py2)
{
    return (b->x * 2l - a->x * 2l) * (py2 - a->y * 2l) -
           (b->y * 2l - a->y * 2l) * (px2 - a->x * 2l);
}

static long tdfx3d_abs(long value)
{
    return value < 0l ? -value : value;
}

/* The edge's length, rounded up, in half-pixel units, for the margin. */
static long tdfx3d_edge_len2(const struct tdfx3d_ivertex *a,
                             const struct tdfx3d_ivertex *b)
{
    return (tdfx3d_abs(b->x - a->x) + tdfx3d_abs(b->y - a->y)) * 2l;
}

#define COVER_OUT 0
#define COVER_IN 1
#define COVER_EDGE 2

/*
 * Where pixel (x, y)'s centre lies: at least one pixel inside every edge,
 * at least one pixel outside some edge, or in between. The edge function
 * divided by the edge's length is the distance; the Manhattan length
 * over-estimates it, so "inside by one pixel" is judged conservatively.
 */
static int tdfx3d_cover(const struct tdfx3d_ivertex *v, int x, int y,
                        long *w)
{
    long px2 = (long)x * 2l + 1l;
    long py2 = (long)y * 2l + 1l;
    long area;
    int edge;
    int inside = 1;
    static const int from[3] = { 1, 2, 0 };
    static const int to[3] = { 2, 0, 1 };

    area = tdfx3d_edge2(&v[0], &v[1], v[2].x * 2l, v[2].y * 2l);
    for (edge = 0; edge < 3; ++edge) {
        const struct tdfx3d_ivertex *a = &v[from[edge]];
        const struct tdfx3d_ivertex *b = &v[to[edge]];
        long e = tdfx3d_edge2(a, b, px2, py2);
        long margin = tdfx3d_edge_len2(a, b) * 2l;

        if (area < 0l) {
            e = -e;
        }
        w[edge] = e;
        if (e <= -margin) {
            return COVER_OUT;
        }
        if (e < margin) {
            inside = 0;
        }
    }
    return inside ? COVER_IN : COVER_EDGE;
}

static unsigned short tdfx3d_pack565(long r, long g, long b)
{
    return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static unsigned short tdfx3d_pixel(int x, int y)
{
    DWORD index = ((DWORD)y * TEST_PITCH + (DWORD)x * 2u) / 4u;
    DWORD dword = tdfx3d_result_buffer.block[index];

    return (x & 1) != 0 ? (unsigned short)(dword >> 16)
                        : (unsigned short)(dword & 0xffffu);
}

static long tdfx3d_channel_error(unsigned short actual, unsigned short expected,
                                 int shift, unsigned short mask)
{
    return tdfx3d_abs((long)((actual >> shift) & mask) -
                      (long)((expected >> shift) & mask));
}

static long tdfx3d_565_error(unsigned short actual, unsigned short expected)
{
    long r = tdfx3d_channel_error(actual, expected, 11, 0x1fu);
    long g = tdfx3d_channel_error(actual, expected, 5, 0x3fu);
    long b = tdfx3d_channel_error(actual, expected, 0, 0x1fu);
    long worst = r;

    if (g > worst) {
        worst = g;
    }
    if (b > worst) {
        worst = b;
    }
    return worst;
}

/* A row of the read-back surface as hex, for the record. */
static void tdfx3d_write_row(const char *name, int y)
{
    char key[64];
    char text[8 * 5 + 1];
    char number[8];
    int x;

    text[0] = '\0';
    for (x = 0; x < 8; ++x) {
        tdfx3d_hex(number, tdfx3d_pixel(x * 4, y), 4);
        lstrcatA(text, number);
        if (x != 7) {
            lstrcatA(text, " ");
        }
    }
    tdfx3d_decimal(number, (DWORD)y);
    lstrcpyA(key, name);
    lstrcatA(key, "Row");
    lstrcatA(key, number);
    tdfx3d_write(key, text);
}

/*
 * One draw: prefill colour (and depth, and the texture), flush, state,
 * every triangle, flush, read the colour surface back. Returns non-zero
 * when it ran and both waits completed; publishes the waits either way.
 */
static int tdfx3d_draw(const char *name, const struct v9x_tdfx_3d_state *state,
                       const struct tdfx3d_ivertex *triangles, int count,
                       DWORD prefill, int fill_depth, int upload_texture)
{
    struct v9x_tdfx_3d_words words;
    struct v9x_tdfx_3d_vertex v[3];
    char key[64];
    DWORD before;
    DWORD after;
    int index;
    int corner;

    tdfx3d_begin();
    tdfx3d_add(TDFX3D_OP_LFB_FILL32, TEST_BASE, prefill | (prefill << 16),
               TEST_DWORDS);
    if (fill_depth) {
        tdfx3d_add(TDFX3D_OP_LFB_FILL32, DEPTH_BASE, 0xfffffffful,
                   TEST_DWORDS);
    }
    if (upload_texture) {
        tdfx3d_add(TDFX3D_OP_LFB_GRADIENT, TEXTURE_BASE, 0u,
                   (DWORD)TEXTURE_DWORDS);
    }
    before = tdfx3d_add_flush();

    if (v9x_tdfx_3d_build_state(state, &words) != V9X_STATUS_OK) {
        tdfx3d_write(tdfx3d_key(key, name, "Result"), "BUILDER-REFUSED-STATE");
        tdfx3d_failed = 1u;
        return 0;
    }
    tdfx3d_add_words(&words);
    for (index = 0; index < count; ++index) {
        for (corner = 0; corner < 3; ++corner) {
            tdfx3d_to_vertex(&triangles[index * 3 + corner], &v[corner]);
        }
        words.count = 0u;
        if (v9x_tdfx_3d_build_triangle(state, v, &words) != V9X_STATUS_OK) {
            tdfx3d_write(tdfx3d_key(key, name, "Result"),
                         "BUILDER-REFUSED-TRIANGLE");
            tdfx3d_failed = 1u;
            return 0;
        }
        tdfx3d_add_words(&words);
    }
    after = tdfx3d_add_flush();
    tdfx3d_add(TDFX3D_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);

    if (!tdfx3d_run()) {
        tdfx3d_write_hex(tdfx3d_key(key, name, "VxdStatus"),
                         tdfx3d_result_buffer.status);
        tdfx3d_write_decimal(tdfx3d_key(key, name, "VxdExecuted"),
                             tdfx3d_result_buffer.executed);
        tdfx3d_write(tdfx3d_key(key, name, "Result"), "NOT-RUN");
        tdfx3d_failed = 1u;
        return 0;
    }
    tdfx3d_write_hex(tdfx3d_key(key, name, "WaitBefore"),
                     tdfx3d_result_buffer.ops[before].result);
    tdfx3d_write_hex(tdfx3d_key(key, name, "WaitAfter"),
                     tdfx3d_result_buffer.ops[after].result);
    tdfx3d_write_decimal(tdfx3d_key(key, name, "Ops"),
                         tdfx3d_request_buffer.count);
    if (tdfx3d_result_buffer.ops[before].result == TDFX3D_TIMEOUT ||
        tdfx3d_result_buffer.ops[after].result == TDFX3D_TIMEOUT) {
        tdfx3d_write(tdfx3d_key(key, name, "Result"), "ENGINE-NOT-IDLE");
        tdfx3d_failed = 1u;
        return 0;
    }
    for (index = 0; index < (int)tdfx3d_request_buffer.count; ++index) {
        if (tdfx3d_request_buffer.ops[index].code == TDFX3D_OP_WAIT_FIFO &&
            tdfx3d_result_buffer.ops[index].result == TDFX3D_TIMEOUT) {
            tdfx3d_write(tdfx3d_key(key, name, "Result"), "FIFO-NO-ROOM");
            tdfx3d_failed = 1u;
            return 0;
        }
    }
    return 1;
}

/* Judge every pixel against expect(): IN pixels within tolerance, OUT
 * pixels the prefill. Publishes the counts and the result. */
static void tdfx3d_judge(const char *name, const struct tdfx3d_ivertex *tri,
                         unsigned short prefill, long tolerance,
                         unsigned short (*expect)(const struct tdfx3d_ivertex *,
                                                  const long *))
{
    char key[64];
    DWORD in_bad = 0u;
    DWORD out_bad = 0u;
    DWORD in_count = 0u;
    DWORD edge_drawn = 0u;
    DWORD edge_count = 0u;
    long worst = 0l;
    int x;
    int y;
    long w[3];

    for (y = 0; y < TEST_HEIGHT; ++y) {
        for (x = 0; x < TEST_WIDTH; ++x) {
            int cover = tdfx3d_cover(tri, x, y, w);
            unsigned short actual = tdfx3d_pixel(x, y);

            if (cover == COVER_IN) {
                long error = tdfx3d_565_error(actual, expect(tri, w));

                ++in_count;
                if (error > worst) {
                    worst = error;
                }
                if (error > tolerance) {
                    ++in_bad;
                }
            } else if (cover == COVER_OUT) {
                if (actual != prefill) {
                    ++out_bad;
                }
            } else {
                ++edge_count;
                if (actual != prefill) {
                    ++edge_drawn;
                }
            }
        }
    }
    tdfx3d_write_decimal(tdfx3d_key(key, name, "InsidePixels"), in_count);
    tdfx3d_write_decimal(tdfx3d_key(key, name, "InsideWrong"), in_bad);
    tdfx3d_write_decimal(tdfx3d_key(key, name, "InsideWorstStep"),
                         (DWORD)worst);
    tdfx3d_write_decimal(tdfx3d_key(key, name, "OutsideDrawn"), out_bad);
    tdfx3d_write_decimal(tdfx3d_key(key, name, "EdgePixels"), edge_count);
    tdfx3d_write_decimal(tdfx3d_key(key, name, "EdgeDrawn"), edge_drawn);
    tdfx3d_write_row(name, 4);
    tdfx3d_write_row(name, 12);
    tdfx3d_write(tdfx3d_key(key, name, "Result"),
                 in_bad == 0u && out_bad == 0u && in_count != 0u ? "PASS"
                                                                 : "MISMATCH");
    if (in_bad != 0u || out_bad != 0u || in_count == 0u) {
        tdfx3d_failed = 1u;
    }
}

static unsigned short tdfx3d_expect_corner0(const struct tdfx3d_ivertex *t,
                                            const long *w)
{
    (void)w;
    return tdfx3d_pack565(t[0].r, t[0].g, t[0].b);
}

/* Barycentric interpolation of the corner colours: w[i] weights corner i
 * through the edge opposite it. */
static unsigned short tdfx3d_expect_gouraud(const struct tdfx3d_ivertex *t,
                                            const long *w)
{
    long sum = w[0] + w[1] + w[2];
    long r = (w[0] * t[0].r + w[1] * t[1].r + w[2] * t[2].r) / sum;
    long g = (w[0] * t[0].g + w[1] * t[1].g + w[2] * t[2].g) / sum;
    long b = (w[0] * t[0].b + w[1] * t[1].b + w[2] * t[2].b) / sum;

    return tdfx3d_pack565(r, g, b);
}

static void tdfx3d_corner(struct tdfx3d_ivertex *v, long x, long y, long r,
                          long g, long b)
{
    BYTE *bytes = (BYTE *)v;
    DWORD index;

    for (index = 0u; index < sizeof(*v); ++index) {
        bytes[index] = 0u;
    }
    v->x = x;
    v->y = y;
    v->r = r;
    v->g = g;
    v->b = b;
    v->a = 255l;
}

static void tdfx3d_test_flat(void)
{
    struct v9x_tdfx_3d_state state;
    struct tdfx3d_ivertex tri[3];

    tdfx3d_base_state(&state);
    tdfx3d_corner(&tri[0], 8l, 4l, 255l, 0l, 0l);
    tdfx3d_corner(&tri[1], 56l, 6l, 255l, 0l, 0l);
    tdfx3d_corner(&tri[2], 20l, 28l, 255l, 0l, 0l);
    if (tdfx3d_draw("Flat", &state, tri, 1, PREFILL_GREY, 0, 0)) {
        tdfx3d_judge("Flat", tri, PREFILL_GREY, 0l, tdfx3d_expect_corner0);
    }
}

static void tdfx3d_test_gouraud(void)
{
    struct v9x_tdfx_3d_state state;
    struct tdfx3d_ivertex tri[3];

    tdfx3d_base_state(&state);
    tdfx3d_corner(&tri[0], 8l, 4l, 255l, 0l, 0l);
    tdfx3d_corner(&tri[1], 56l, 6l, 0l, 255l, 0l);
    tdfx3d_corner(&tri[2], 20l, 28l, 0l, 0l, 255l);
    if (tdfx3d_draw("Gouraud", &state, tri, 1, PREFILL_GREY, 0, 0)) {
        tdfx3d_judge("Gouraud", tri, PREFILL_GREY, 1l, tdfx3d_expect_gouraud);
    }
}

/* Two triangles covering the whole surface, one colour, one Z. */
static void tdfx3d_full(struct tdfx3d_ivertex *six, long r, long g, long b,
                        long a, long z)
{
    int index;

    tdfx3d_corner(&six[0], 0l, 0l, r, g, b);
    tdfx3d_corner(&six[1], (long)TEST_WIDTH, 0l, r, g, b);
    tdfx3d_corner(&six[2], 0l, (long)TEST_HEIGHT, r, g, b);
    tdfx3d_corner(&six[3], (long)TEST_WIDTH, 0l, r, g, b);
    tdfx3d_corner(&six[4], (long)TEST_WIDTH, (long)TEST_HEIGHT, r, g, b);
    tdfx3d_corner(&six[5], 0l, (long)TEST_HEIGHT, r, g, b);
    for (index = 0; index < 6; ++index) {
        six[index].a = a;
        six[index].z = z;
    }
}

/* Every pixel against one value; publishes the count and the first. */
static DWORD tdfx3d_count_not(const char *name, const char *what,
                              unsigned short expected, long tolerance)
{
    char key[64];
    DWORD wrong = 0u;
    long worst = 0l;
    int x;
    int y;

    for (y = 0; y < TEST_HEIGHT; ++y) {
        for (x = 0; x < TEST_WIDTH; ++x) {
            long error = tdfx3d_565_error(tdfx3d_pixel(x, y), expected);

            if (error > worst) {
                worst = error;
            }
            if (error > tolerance) {
                ++wrong;
            }
        }
    }
    lstrcpyA(key, name);
    lstrcatA(key, what);
    lstrcatA(key, "Wrong");
    tdfx3d_write_decimal(key, wrong);
    lstrcpyA(key, name);
    lstrcatA(key, what);
    lstrcatA(key, "WorstStep");
    tdfx3d_write_decimal(key, (DWORD)worst);
    return wrong;
}

static void tdfx3d_test_depth(void)
{
    struct v9x_tdfx_3d_state state;
    struct tdfx3d_ivertex draws[18];
    DWORD wrong;

    tdfx3d_base_state(&state);
    state.depth_enable = 1ul;
    state.depth.offset = DEPTH_BASE;
    state.depth.pitch_bytes = TEST_PITCH;
    state.depth_func = V9X_TDFX_CMP_LESS;
    state.depth_write = 1ul;
    tdfx3d_full(&draws[0], 255l, 0l, 0l, 255l, 40000l);
    tdfx3d_full(&draws[6], 0l, 255l, 0l, 255l, 20000l);
    tdfx3d_full(&draws[12], 0l, 0l, 255l, 255l, 60000l);
    if (!tdfx3d_draw("Depth", &state, draws, 6, PREFILL_GREY, 1, 0)) {
        return;
    }
    tdfx3d_write_row("Depth", 12);
    wrong = tdfx3d_count_not("Depth", "Colour", 0x07e0u, 0l);

    /* The Z buffer as written: the probe's hypothesis is Z in depth
     * counts, so 20000 (4E20h) everywhere; recorded either way. */
    tdfx3d_begin();
    tdfx3d_add(TDFX3D_OP_LFB_READ_BLOCK, DEPTH_BASE, 0u, TEST_DWORDS);
    if (tdfx3d_run()) {
        tdfx3d_write_row("DepthZ", 12);
        tdfx3d_count_not("Depth", "Z", 0x4e20u, 0l);
    }
    tdfx3d_write("DepthResult", wrong == 0u ? "PASS" : "MISMATCH");
    if (wrong != 0u) {
        tdfx3d_failed = 1u;
    }
}

static void tdfx3d_test_blend(void)
{
    struct v9x_tdfx_3d_state state;
    struct tdfx3d_ivertex draws[6];
    DWORD wrong;

    tdfx3d_base_state(&state);
    state.blend_enable = 1ul;
    state.blend_src = V9X_TDFX_BLEND_SRC_ALPHA;
    state.blend_dst = V9X_TDFX_BLEND_INV_SRC_ALPHA;
    tdfx3d_full(draws, 255l, 0l, 0l, 128l, 0l);
    if (!tdfx3d_draw("Blend", &state, draws, 2, PREFILL_BLUE, 0, 0)) {
        return;
    }
    tdfx3d_write_row("Blend", 12);
    /* Red 255 x 128/255 = 128, 16 in five bits; blue 31 (248 or 255) x
     * 127/255, 15 in five bits. */
    wrong = tdfx3d_count_not("Blend", "Colour",
                             (unsigned short)((16u << 11) | 15u), 1l);
    tdfx3d_write("BlendResult", wrong == 0u ? "PASS" : "MISMATCH");
    if (wrong != 0u) {
        tdfx3d_failed = 1u;
    }
}

static void tdfx3d_test_texture(void)
{
    struct v9x_tdfx_3d_state state;
    struct tdfx3d_ivertex draws[6];
    char key[64];
    DWORD wrong = 0u;
    DWORD untouched_bad = 0u;
    int index;
    int x;
    int y;

    tdfx3d_base_state(&state);
    state.texture_enable = 1ul;
    state.texture.offset = TEXTURE_BASE;
    state.texture.format = V9X_TDFX_TEX_RGB565;
    state.texture.log2_size = TEXTURE_LOG2;
    state.texture.combine = V9X_TDFX_COMBINE_REPLACE;

    /* The left 32x32, S and T at 8 per pixel. */
    tdfx3d_corner(&draws[0], 0l, 0l, 0l, 0l, 0l);
    tdfx3d_corner(&draws[1], 32l, 0l, 0l, 0l, 0l);
    tdfx3d_corner(&draws[2], 0l, 32l, 0l, 0l, 0l);
    tdfx3d_corner(&draws[3], 32l, 0l, 0l, 0l, 0l);
    tdfx3d_corner(&draws[4], 32l, 32l, 0l, 0l, 0l);
    tdfx3d_corner(&draws[5], 0l, 32l, 0l, 0l, 0l);
    for (index = 0; index < 6; ++index) {
        draws[index].s = draws[index].x * 8l;
        draws[index].t = draws[index].y * 8l;
    }
    if (!tdfx3d_draw("Texture", &state, draws, 2, PREFILL_GREY, 0, 1)) {
        return;
    }
    for (y = 0; y < TEST_HEIGHT; ++y) {
        for (x = 0; x < TEST_WIDTH; ++x) {
            unsigned short actual = tdfx3d_pixel(x, y);

            if (x < TEXTURE_SIZE) {
                if (actual != (unsigned short)(y * TEXTURE_SIZE + x)) {
                    ++wrong;
                }
            } else if (actual != PREFILL_GREY) {
                ++untouched_bad;
            }
        }
    }
    tdfx3d_write_row("Texture", 0);
    tdfx3d_write_row("Texture", 5);
    tdfx3d_write_decimal(tdfx3d_key(key, "Texture", "TexelsWrong"), wrong);
    tdfx3d_write_decimal(tdfx3d_key(key, "Texture", "OutsideDrawn"),
                         untouched_bad);
    tdfx3d_write("TextureResult",
                 wrong == 0u && untouched_bad == 0u ? "PASS" : "MISMATCH");
    if (wrong != 0u || untouched_bad != 0u) {
        tdfx3d_failed = 1u;
    }
}

void WINAPI V9xTdfx3dProbeEntry(void)
{
    static const char header[] = "[Tdfx3dProbe]\r\n";
    HDC display;
    DWORD returned;
    DWORD desktop_bytes = 0u;
    DWORD status;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    tdfx3d_output = CreateFileA(V9X_DIAG_TDFX3D_TXT, GENERIC_WRITE,
                                FILE_SHARE_READ, 0, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, 0);
    if (tdfx3d_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(tdfx3d_output, header, (DWORD)lstrlenA(header), &returned, 0);
    tdfx3d_write("Build", V9X_BUILD_ID);

    display = GetDC(0);
    if (display != 0) {
        DWORD width = (DWORD)GetDeviceCaps(display, HORZRES);
        DWORD height = (DWORD)GetDeviceCaps(display, VERTRES);
        DWORD bpp = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                            GetDeviceCaps(display, PLANES));

        tdfx3d_write_decimal("DesktopWidth", width);
        tdfx3d_write_decimal("DesktopHeight", height);
        tdfx3d_write_decimal("DesktopBpp", bpp);
        desktop_bytes = width * height * ((bpp + 7u) / 8u);
        ReleaseDC(0, display);
    }
    /* The desktop, from 15 MiB, must end below the test surfaces. */
    if (desktop_bytes == 0u || DESKTOP_OFFSET + desktop_bytes >= TEST_BASE) {
        tdfx3d_write("Result", "REFUSED-DESKTOP");
        CloseHandle(tdfx3d_output);
        ExitProcess(1u);
    }

    tdfx3d_device = CreateFileA("\\\\.\\TDFX3D.VXD", 0, 0, 0, CREATE_NEW,
                                FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (tdfx3d_device == INVALID_HANDLE_VALUE) {
        tdfx3d_write("Result", "VXD-NOT-LOADED");
        CloseHandle(tdfx3d_output);
        ExitProcess(2u);
    }

    /* First contact: status, read-only. All ones is decode off; busy means
     * someone else - Glide - is using the engine, and nothing runs. */
    tdfx3d_begin();
    tdfx3d_add(TDFX3D_OP_MMIO_READ32, 0u, 0u, 0u);
    if (!tdfx3d_run()) {
        tdfx3d_write_hex("VxdStatus", tdfx3d_result_buffer.status);
        tdfx3d_write("Result", "NOT-MAPPED");
        CloseHandle(tdfx3d_device);
        CloseHandle(tdfx3d_output);
        ExitProcess(3u);
    }
    status = tdfx3d_result_buffer.ops[0].result;
    tdfx3d_write_hex("Bar0", tdfx3d_result_buffer.bar0);
    tdfx3d_write_hex("Bar1", tdfx3d_result_buffer.bar1);
    tdfx3d_write_hex("StatusAtStart", status);
    if (status == 0xfffffffful || (status & V9X_TDFX_STATUS_BUSY_3D) != 0ul) {
        tdfx3d_write("Result", status == 0xfffffffful ? "DECODE-OFF"
                                                     : "BUSY-AT-START");
        CloseHandle(tdfx3d_device);
        CloseHandle(tdfx3d_output);
        ExitProcess(3u);
    }

    tdfx3d_test_flat();
    if (!tdfx3d_has_switch(GetCommandLineA(), "/flat")) {
        tdfx3d_test_gouraud();
        tdfx3d_test_depth();
        tdfx3d_test_blend();
        tdfx3d_test_texture();
    }
    tdfx3d_write_decimal("FifoWaits", tdfx3d_fifo_waits);

    tdfx3d_begin();
    tdfx3d_add(TDFX3D_OP_MMIO_READ32, 0u, 0u, 0u);
    if (tdfx3d_run()) {
        tdfx3d_write_hex("StatusAtEnd", tdfx3d_result_buffer.ops[0].result);
    }

    tdfx3d_write("Result", tdfx3d_failed == 0u ? "PASS" : "FAIL");
    CloseHandle(tdfx3d_device);
    CloseHandle(tdfx3d_output);
    ExitProcess(tdfx3d_failed == 0u ? 0u : 1u);
}
