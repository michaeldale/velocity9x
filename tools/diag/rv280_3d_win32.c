/*
 * V9XR23D.EXE: the ATI Radeon 9250 (RV280, R200 class) 3D engine draw
 * probe.
 *
 * Phase 5 of docs\plans\ati-rv280-radeon-9250.md, first step: does the R200
 * 3D engine draw when every register is written directly over BAR2, with
 * the CP off, from the values src\chipsets\ati\rv280\rv280_3d.c builds
 * after xf86-video-ati 6.14.6's MMIO paths? Publishes C:\V9XDIAG\
 * V9XR23D.TXT. Runs beside R23D.VXD, which executes the op lists this side
 * builds. A lean copy of the RS690's V9XRS3D (rs690_3d_win32.c).
 *
 * It refuses unless the CP's queues are off, takes the frame buffer's GPU
 * address from MC_FB_LOCATION, writes the R200 init once, then draws only on
 * a 4 KiB surface at frame buffer offset 16 MiB, prefilled with A5h, 64
 * pixels wide at the desktop's depth (16 or 32 bpp; 8 bpp has no colour
 * buffer format here and is refused). Every test waits for idle, writes the
 * colour-buffer state and the triangles in FIFO-sized batches, ends with
 * WAIT_UNTIL 3D idle-clean, waits for idle (which flushes the destination
 * cache), and reads the surface back.
 *
 * Tests:
 *   FlatOrder0 / FlatOrder1 - one flat triangle whose vertex colour is
 *               FFFF0000h (red as a D3D ARGB colour), with SE_VF_CNTL's
 *               COLOR_ORDER_RGBA clear and then set. Each reports the
 *               colour drawn inside (red, blue, other), the pixels inside
 *               not that colour and the pixels outside changed. These
 *               settle the two hypotheses rv280_3d.h names: that diffuse-
 *               only blending draws the vertex colour, and the packed
 *               colour's byte order.
 *   Gouraud   - red, green and blue corners, in the order the flat tests
 *               found red in; each channel inside within a step of the
 *               interpolation at the pixel centre.
 *   TexDecal  - a 32 x 16 texture (the VxD's gradient, every texel
 *               different) on a quad of the same size, nearest, decal:
 *               every pixel its texel, exactly (RGB at 32 bpp), nothing
 *               outside touched. S and T follow the colour in the vertex.
 *   TexWrap   - the texture twice across 64 pixels, s from 0 to 2,
 *               wrapping: exact the same way.
 *   TexModulate - TexDecal's mapping times a flat vertex colour 80C040h;
 *               each channel within one step of texel * colour / 255.
 *   TexBilinear - texels 0-31 x 0-7 magnified 2x, bilinear, clamped; each
 *               channel within one step of the exact filter plus 1/32 of
 *               the four texels' spread, the RS690 probe's judge.
 *   FillAfter3D - a 2D fill from the RS690 builder after the 3D tests,
 *               byte-exact, so the 3D state left behind does not break the
 *               engine DirectDraw uses.
 *
 * Pixels whose centre lies within about a pixel of an edge are not judged.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/rs690_2d.h"
#include "velocity9x/rv280_3d.h"

#include "../../src/chipsets/ati/rs690/rs690_2d.c"
#include "../../src/chipsets/ati/rv280/rv280_3d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define RS_PATH V9X_DIAG_PATH("V9XR23D.TXT")
#define RS_MAGIC 0x44333252ul
#define RS_OP_MAX 512u
#define RS_BLOCK_DWORDS 1024u

#define RS_OP_MMIO_WRITE32 1ul
#define RS_OP_MMIO_READ32 2ul
#define RS_OP_LFB_FILL32 5ul
#define RS_OP_LFB_GRADIENT 6ul
#define RS_OP_LFB_READ_BLOCK 7ul
#define RS_OP_WAIT_IDLE 8ul
#define RS_OP_WAIT_FIFO 9ul

#define RS_MAPPED 0x00000002ul
#define RS_RAN 0x00000004ul

/* xf86-video-ati waits for a batch's length before writing it; 16 is a
 * quarter of the FIFO. */
#define RS_FIFO_BATCH 16u

#define RV_MC_FB_LOCATION 0x0148ul

#define TEST_BASE 0x01000000ul
#define TEST_BYTES 4096ul
#define TEST_WIDTH 64ul
#define TEST_DWORDS (TEST_BYTES / 4ul)
#define TEST_FB 0x02000000ul
#define TEST_PREFILL 0xa5a5a5a5ul

/* The texture: 32 x 16 texels in the surface's format, rows packed, 64 KiB
 * past the surface. The VxD's gradient op fills it: dword i holds 2i and
 * 2i + 1 in its halves, so at 16 bpp texel n is n and at 32 bpp texel n is
 * 2n | (2n + 1) << 16, every texel different from its neighbours. */
#define TEX_OFFSET (TEST_BASE + 0x10000ul)
#define TEX_WIDTH 32ul
#define TEX_HEIGHT 16ul

/* The modulate test's flat vertex colour, D3D ARGB. */
#define MODULATE_COLOUR 0xff80c040ul

/* A D3D ARGB colour. */
#define ARGB_RED   0xffff0000ul
#define ARGB_GREEN 0xff00ff00ul
#define ARGB_BLUE  0xff0000fful

#define CLASS_OUTSIDE 0
#define CLASS_INSIDE 1
#define CLASS_BAND 2

struct rs_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct rs_request {
    DWORD count;
    struct rs_op ops[RS_OP_MAX];
};

struct rs_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar2;
    DWORD executed;
    DWORD refused;
    DWORD config_id;
    DWORD memsize;
    struct rs_op ops[RS_OP_MAX];
    DWORD block[RS_BLOCK_DWORDS];
};

static struct rs_request rs_request_buffer;
static struct rs_result rs_result_buffer;
static unsigned char rs_model[TEST_BYTES];
static HANDLE rs_device;
static HANDLE rs_output;
static DWORD rs_failed;
static DWORD rs_bpp;
static DWORD rs_pitch;
static DWORD rs_height;
static DWORD rs_fb_gpu_base;

/* The triangle, in pixels: A top-left, B top-right, C bottom-left. */
static long rs_tri_x[3];
static long rs_tri_y[3];

static void rs_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void rs_decimal(char *text, DWORD value)
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

static void rs_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(rs_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(rs_output, "=", 1u, &written, 0);
    WriteFile(rs_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(rs_output, "\r\n", 2u, &written, 0);
}

static void rs_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    rs_hex(text + 2, value, 8);
    rs_write(key, text);
}

static void rs_write_decimal(const char *key, DWORD value)
{
    char text[16];

    rs_decimal(text, value);
    rs_write(key, text);
}

/* key = prefix + suffix, into a caller buffer of 64 bytes. */
static const char *rs_key(char *key, const char *prefix, const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
    return key;
}

static void rs_begin(void)
{
    rs_request_buffer.count = 0u;
}

/* The index of the op added. */
static DWORD rs_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct rs_op *op;

    if (rs_request_buffer.count >= RS_OP_MAX) {
        return RS_OP_MAX - 1u;
    }
    op = &rs_request_buffer.ops[rs_request_buffer.count];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0u;
    return rs_request_buffer.count++;
}

/* Register writes in batches, each behind a wait for its length in free
 * FIFO entries. */
static void rs_add_writes(const DWORD *offsets, const DWORD *values,
                          DWORD count)
{
    DWORD index;

    for (index = 0u; index < count; ++index) {
        if (index % RS_FIFO_BATCH == 0u) {
            DWORD batch = count - index;

            if (batch > RS_FIFO_BATCH) {
                batch = RS_FIFO_BATCH;
            }
            rs_add(RS_OP_WAIT_FIFO, batch, 0u, 0u);
        }
        rs_add(RS_OP_MMIO_WRITE32, offsets[index], values[index], 0u);
    }
}

static void rs_add_list(const struct v9x_rv280_3d_list *list)
{
    rs_add_writes(list->offsets, list->values, list->count);
}

static void rs_add_blt(const struct v9x_rs690_blt *blt)
{
    rs_add_writes(blt->offsets, blt->values, blt->count);
}

/* Non-zero when the VxD ran every op. */
static int rs_run(void)
{
    DWORD returned = 0u;
    DWORD bytes = 4u + rs_request_buffer.count * (DWORD)sizeof(struct rs_op);

    if (!DeviceIoControl(rs_device, 1u, &rs_request_buffer, bytes,
                         &rs_result_buffer, sizeof(rs_result_buffer),
                         &returned, 0) ||
        returned != sizeof(rs_result_buffer) ||
        rs_result_buffer.magic != RS_MAGIC) {
        return 0;
    }
    return (rs_result_buffer.status & RS_RAN) != 0ul &&
           rs_result_buffer.executed == rs_request_buffer.count;
}

/* One register read through the VxD, or 0xffffffff. */
static DWORD rs_read_register(DWORD offset)
{
    rs_begin();
    rs_add(RS_OP_MMIO_READ32, offset, 0u, 0u);
    if (!rs_run()) {
        return 0xfffffffful;
    }
    return rs_result_buffer.ops[0].result;
}

/* Publishes which op a refused run stopped at, and what it was. */
static void rs_report_refusal(const char *name)
{
    char key[64];
    DWORD index = rs_result_buffer.refused;

    rs_write_decimal(rs_key(key, name, "RefusedOp"), index);
    if (index < rs_request_buffer.count) {
        rs_write_hex(rs_key(key, name, "RefusedCode"),
                     rs_request_buffer.ops[index].code);
        rs_write_hex(rs_key(key, name, "RefusedA"),
                     rs_request_buffer.ops[index].a);
        rs_write_hex(rs_key(key, name, "RefusedResult"),
                     rs_result_buffer.ops[index].result);
    }
    rs_write_hex(rs_key(key, name, "RbbmStatusAfter"),
                 rs_read_register(V9X_RS690_RBBM_STATUS));
}

static void rs_save_block(char letter)
{
    char path[32];
    HANDLE file;
    DWORD written;

    lstrcpyA(path, V9X_DIAG_PATH("R2x.BIN"));
    path[lstrlenA(path) - 5] = letter;
    file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(file, rs_result_buffer.block, TEST_BYTES, &written, 0);
    CloseHandle(file);
}

static DWORD rs_pixel(const unsigned char *surface, DWORD x, DWORD y)
{
    const unsigned char *at = surface + y * rs_pitch + x * rs_bpp;

    if (rs_bpp == 2u) {
        return (DWORD)at[0] | ((DWORD)at[1] << 8);
    }
    return (DWORD)at[0] | ((DWORD)at[1] << 8) | ((DWORD)at[2] << 16) |
           ((DWORD)at[3] << 24);
}

static void rs_set_pixel(unsigned char *surface, DWORD x, DWORD y,
                         DWORD value)
{
    unsigned char *at = surface + y * rs_pitch + x * rs_bpp;

    at[0] = (unsigned char)value;
    at[1] = (unsigned char)(value >> 8);
    if (rs_bpp == 4u) {
        at[2] = (unsigned char)(value >> 16);
        at[3] = (unsigned char)(value >> 24);
    }
}

static void rs_model_prefill(void)
{
    DWORD index;

    for (index = 0u; index < TEST_BYTES; ++index) {
        rs_model[index] = (unsigned char)(TEST_PREFILL >> ((index & 3u) * 8u));
    }
}

/* A pixel's red, green and blue in the surface's own channel units, and
 * each channel's maximum. */
static void rs_channels(DWORD pixel, long values[3], long maxima[3])
{
    if (rs_bpp == 2u) {
        values[0] = (long)((pixel >> 11) & 31u);
        values[1] = (long)((pixel >> 5) & 63u);
        values[2] = (long)(pixel & 31u);
        maxima[0] = 31l;
        maxima[1] = 63l;
        maxima[2] = 31l;
        return;
    }
    values[0] = (long)((pixel >> 16) & 255u);
    values[1] = (long)((pixel >> 8) & 255u);
    values[2] = (long)(pixel & 255u);
    maxima[0] = 255l;
    maxima[1] = 255l;
    maxima[2] = 255l;
}

static DWORD rs_float_bits(long value)
{
    union {
        float f;
        DWORD d;
    } bits;

    bits.f = (float)value;
    return bits.d;
}

/* Twice the edge function of a->b at p, in doubled coordinates. */
static long rs_edge(int a, int b, long px, long py)
{
    long ax = rs_tri_x[a] * 2l;
    long ay = rs_tri_y[a] * 2l;
    long bx = rs_tri_x[b] * 2l;
    long by = rs_tri_y[b] * 2l;

    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static long rs_abs(long value)
{
    return value < 0l ? -value : value;
}

/* Inside, outside or too near an edge to judge; the three edge weights at
 * the pixel centre, signed so that inside is positive. */
static int rs_classify(DWORD x, DWORD y, long weights[3])
{
    static const int from[3] = { 1, 2, 0 };
    static const int to[3] = { 2, 0, 1 };
    long px = (long)x * 2l + 1l;
    long py = (long)y * 2l + 1l;
    long area = rs_edge(0, 1, rs_tri_x[2] * 2l, rs_tri_y[2] * 2l);
    int sign = area < 0l ? -1 : 1;
    int inside = 1;
    int index;

    for (index = 0; index < 3; ++index) {
        int a = from[index];
        int b = to[index];
        long dx = rs_abs(rs_tri_x[b] - rs_tri_x[a]);
        long dy = rs_abs(rs_tri_y[b] - rs_tri_y[a]);
        /* A pixel is 2 in doubled units, and |dx| + |dy| bounds the
         * edge's length, so this band is at least a pixel wide. */
        long band = 2l * 2l * (dx + dy);

        weights[index] = rs_edge(a, b, px, py) * (long)sign;
        if (rs_abs(weights[index]) < band) {
            return CLASS_BAND;
        }
        if (weights[index] < 0l) {
            inside = 0;
        }
    }
    return inside ? CLASS_INSIDE : CLASS_OUTSIDE;
}

/* Prefill, wait idle, the colour-buffer state, the triangle, the finish,
 * wait idle, read back. Non-zero with the surface in the result block. */
static int rs_draw(const char *name, int gouraud, int order_rgba,
                   const DWORD colours[3])
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_vertex vertices[3];
    struct v9x_rv280_3d_list list;
    char key[64];
    DWORD before_index;
    DWORD after_index;
    int index;

    target.fb_gpu_base = rs_fb_gpu_base;
    target.fb_bytes = TEST_FB;
    target.offset = TEST_BASE;
    target.pitch_bytes = rs_pitch;
    target.bytes_per_pixel = rs_bpp;
    target.width = TEST_WIDTH;
    target.height = rs_height;
    for (index = 0; index < 3; ++index) {
        vertices[index].x_bits = rs_float_bits(rs_tri_x[index]);
        vertices[index].y_bits = rs_float_bits(rs_tri_y[index]);
        vertices[index].colour = colours[index];
        vertices[index].s_bits = 0u;
        vertices[index].t_bits = 0u;
    }

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rv280_build_3d_target(&target, gouraud, &list) !=
        V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TARGET-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    if (v9x_rv280_build_3d_triangles(vertices, 3u, order_rgba, 0, &list) !=
        V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rv280_build_3d_finish(&list);
    rs_add_list(&list);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);

    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return 0;
    }
    rs_write_hex(rs_key(key, name, "WaitBefore"),
                 rs_result_buffer.ops[before_index].result);
    rs_write_hex(rs_key(key, name, "WaitAfter"),
                 rs_result_buffer.ops[after_index].result);
    return 1;
}

/* What colour a pixel shows: 1 red, 2 blue, 0 anything else. */
static int rs_hue(DWORD pixel)
{
    long values[3];
    long maxima[3];

    rs_channels(pixel, values, maxima);
    if (values[0] == maxima[0] && values[1] == 0l && values[2] == 0l) {
        return 1;
    }
    if (values[0] == 0l && values[1] == 0l && values[2] == maxima[2]) {
        return 2;
    }
    return 0;
}

/* A flat red triangle with the given colour order. Returns the hue every
 * judged inside pixel showed (1 red, 2 blue), or 0 if they disagreed, were
 * another colour, or anything outside changed. */
static int rs_flat_test(const char *name, char letter, int order_rgba)
{
    static const DWORD colours[3] = { ARGB_RED, ARGB_RED, ARGB_RED };
    const unsigned char *actual;
    char key[64];
    long weights[3];
    DWORD x;
    DWORD y;
    DWORD inside = 0u;
    DWORD inside_other = 0u;
    DWORD outside_changed = 0u;
    DWORD first_inside = 0xfffffffful;
    int hue = -1;
    int pixel_hue;

    if (!rs_draw(name, 0, order_rgba, colours)) {
        return 0;
    }
    rs_save_block(letter);
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            int cls = rs_classify(x, y, weights);
            DWORD pixel = rs_pixel(actual, x, y);

            if (cls == CLASS_OUTSIDE) {
                if (pixel != rs_pixel(rs_model, x, y)) {
                    ++outside_changed;
                }
                continue;
            }
            if (cls != CLASS_INSIDE) {
                continue;
            }
            ++inside;
            if (first_inside == 0xfffffffful) {
                first_inside = pixel;
            }
            pixel_hue = rs_hue(pixel);
            if (hue < 0) {
                hue = pixel_hue;
            }
            if (pixel_hue == 0 || pixel_hue != hue) {
                ++inside_other;
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "InsideJudged"), inside);
    rs_write_hex(rs_key(key, name, "InsidePixel"), first_inside);
    rs_write(rs_key(key, name, "InsideHue"),
             hue == 1 ? "red" : (hue == 2 ? "blue" : "other"));
    rs_write_decimal(rs_key(key, name, "InsideOther"), inside_other);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (inside == 0u || inside_other != 0u || outside_changed != 0u ||
        hue <= 0) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        return 0;
    }
    rs_write(rs_key(key, name, "Result"), "DRAWN");
    return hue;
}

/* A D3D ARGB colour as the packed dword this run's order wants: swapped
 * red and blue when the flat tests drew red as blue. */
static DWORD rs_pack(DWORD argb, int swap_red_blue)
{
    if (!swap_red_blue) {
        return argb;
    }
    return (argb & 0xff00ff00ul) | ((argb >> 16) & 0xfful) |
           ((argb & 0xfful) << 16);
}

static void rs_gouraud_test(int order_rgba, int swap_red_blue)
{
    static const char name[] = "Gouraud";
    static const DWORD corners[3] = { ARGB_RED, ARGB_GREEN, ARGB_BLUE };
    DWORD colours[3];
    const unsigned char *actual;
    char key[64];
    long weights[3];
    long values[3];
    long maxima[3];
    long area;
    long worst = 0l;
    long tolerance = rs_bpp == 2u ? 1l : 4l;
    DWORD x;
    DWORD y;
    DWORD inside = 0u;
    DWORD wrong = 0u;
    DWORD outside_changed = 0u;
    int corner;
    int channel;

    for (corner = 0; corner < 3; ++corner) {
        colours[corner] = rs_pack(corners[corner], swap_red_blue);
    }
    if (!rs_draw(name, 1, order_rgba, colours)) {
        return;
    }
    rs_save_block('G');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    area = rs_edge(0, 1, rs_tri_x[2] * 2l, rs_tri_y[2] * 2l);
    area = rs_abs(area);
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            int cls = rs_classify(x, y, weights);
            DWORD pixel = rs_pixel(actual, x, y);

            if (cls == CLASS_OUTSIDE) {
                if (pixel != rs_pixel(rs_model, x, y)) {
                    ++outside_changed;
                }
                continue;
            }
            if (cls != CLASS_INSIDE) {
                continue;
            }
            ++inside;
            rs_channels(pixel, values, maxima);
            /* Corner 0 (red) is weighted by the edge opposite it, 1->2,
             * which rs_classify returns first; then green (2->0), blue
             * (0->1). Each corner is pure in its own channel. */
            for (channel = 0; channel < 3; ++channel) {
                long expected = (weights[channel] * maxima[channel] +
                                 area / 2l) / area;
                long error = rs_abs(values[channel] - expected);

                if (error > worst) {
                    worst = error;
                }
                if (error > tolerance) {
                    ++wrong;
                    break;
                }
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "InsideJudged"), inside);
    rs_write_decimal(rs_key(key, name, "InsideWrong"), wrong);
    rs_write_decimal(rs_key(key, name, "WorstChannelError"), (DWORD)worst);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (inside == 0u || wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/* IEEE single bits of num / den. */
static DWORD rs_ratio_bits(long num, long den)
{
    union {
        float f;
        DWORD d;
    } bits;

    bits.f = (float)num / (float)den;
    return bits.d;
}

/* Texel (ix, iy) as the gradient op leaves it. */
static DWORD rs_texel(DWORD ix, DWORD iy)
{
    DWORD n = iy * TEX_WIDTH + ix;

    if (rs_bpp == 2u) {
        return n & 0xfffful;
    }
    return (2ul * n) | ((2ul * n + 1ul) << 16);
}

/* Prefill, the texture, wait idle, the textured state, two triangles
 * making a quad from (left, 0) to (left + width, TEX_HEIGHT) with s from 0
 * to s_right and t from 0 to 1 / t_den, the finish, wait idle, read back.
 * Clamped when linear, wrapping otherwise. */
static int rs_draw_textured(const char *name, DWORD linear, DWORD combine,
                            DWORD colour, DWORD left, DWORD width,
                            long s_right, long t_den)
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_texture texture;
    struct v9x_rv280_3d_vertex vertices[6];
    struct v9x_rv280_3d_list list;
    static const int corner_x[6] = { 0, 1, 0, 1, 1, 0 };
    static const int corner_y[6] = { 0, 0, 1, 0, 1, 1 };
    char key[64];
    DWORD before_index;
    DWORD after_index;
    int index;

    target.fb_gpu_base = rs_fb_gpu_base;
    target.fb_bytes = TEST_FB;
    target.offset = TEST_BASE;
    target.pitch_bytes = rs_pitch;
    target.bytes_per_pixel = rs_bpp;
    target.width = TEST_WIDTH;
    target.height = rs_height;
    texture.fb_gpu_base = rs_fb_gpu_base;
    texture.fb_bytes = TEST_FB;
    texture.offset = TEX_OFFSET;
    texture.pitch_bytes = TEX_WIDTH * rs_bpp;
    texture.bytes_per_pixel = rs_bpp;
    texture.width = TEX_WIDTH;
    texture.height = TEX_HEIGHT;
    texture.linear = linear;
    texture.clamp = linear;
    /* Top-left, top-right, bottom-left; top-right, bottom-right,
     * bottom-left. */
    for (index = 0; index < 6; ++index) {
        vertices[index].x_bits =
            rs_float_bits((long)left + (corner_x[index] ? (long)width : 0l));
        vertices[index].y_bits =
            rs_float_bits(corner_y[index] ? (long)TEX_HEIGHT : 0l);
        vertices[index].colour = colour;
        vertices[index].s_bits =
            rs_ratio_bits(corner_x[index] ? s_right : 0l, 1l);
        vertices[index].t_bits = rs_ratio_bits(corner_y[index], t_den);
    }

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rs_add(RS_OP_LFB_GRADIENT, TEX_OFFSET, 0u,
           TEX_WIDTH * TEX_HEIGHT * rs_bpp / 4u);
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rv280_build_3d_textured_target(&target, 0, &texture, combine,
                                           &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    if (v9x_rv280_build_3d_triangles(vertices, 6u, 0, 1, &list) !=
        V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rv280_build_3d_finish(&list);
    rs_add_list(&list);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);

    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return 0;
    }
    rs_write_hex(rs_key(key, name, "WaitBefore"),
                 rs_result_buffer.ops[before_index].result);
    rs_write_hex(rs_key(key, name, "WaitAfter"),
                 rs_result_buffer.ops[after_index].result);
    return 1;
}

/* The colour bits a pixel compares on: all of a 16-bit pixel, the RGB of a
 * 32-bit one (the alpha byte is not judged: the 3D engine writes it 0,
 * docs\decisions\2026-11-15-rv280-3d-draw-probe.md). */
static DWORD rs_rgb(DWORD pixel)
{
    return rs_bpp == 2u ? pixel : (pixel & 0x00fffffful);
}

/*
 * Nearest-filtered, decal: a quad from (left, 0) of width pixels and 16 rows
 * with s from 0 to s_right. Each pixel centre lands in the middle of a
 * texel, so the result is exact: pixel (x, y) is texel ((x - left) * 32 *
 * s_right / width mod 32, y), every other pixel the prefill. TexDecal maps
 * the texture once; TexWrap twice across 64 pixels, wrapping.
 */
static void rs_tex_exact_test(const char *name, char letter, DWORD left,
                              DWORD width, long s_right)
{
    const unsigned char *actual;
    char key[64];
    DWORD x;
    DWORD y;
    DWORD inside_wrong = 0u;
    DWORD outside_changed = 0u;
    DWORD first_wrong = 0xfffffffful;

    if (!rs_draw_textured(name, 0ul, V9X_RV280_COMBINE_DECAL, ARGB_RED,
                          left, width, s_right, 1l)) {
        return;
    }
    rs_save_block(letter);
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);

            if (x >= left && x < left + width && y < TEX_HEIGHT) {
                DWORD ix = ((x - left) * TEX_WIDTH * (DWORD)s_right / width) %
                           TEX_WIDTH;

                if (rs_rgb(pixel) != rs_rgb(rs_texel(ix, y))) {
                    if (first_wrong == 0xfffffffful) {
                        first_wrong = y * TEST_WIDTH + x;
                    }
                    ++inside_wrong;
                }
            } else if (pixel != rs_pixel(rs_model, x, y)) {
                ++outside_changed;
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "InsideWrong"), inside_wrong);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (first_wrong != 0xfffffffful) {
        DWORD fx = first_wrong % TEST_WIDTH;
        DWORD fy = first_wrong / TEST_WIDTH;

        rs_write_decimal(rs_key(key, name, "FirstWrongX"), fx);
        rs_write_decimal(rs_key(key, name, "FirstWrongY"), fy);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, fx, fy));
        rs_write_hex(rs_key(key, name, "FirstWrongTexel"),
                     rs_texel(((fx - left) * TEX_WIDTH * (DWORD)s_right /
                               width) % TEX_WIDTH, fy));
    }
    if (inside_wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/* TexDecal's mapping times a flat vertex colour: each channel within one
 * step of texel * colour / 255. */
static void rs_tex_modulate_test(void)
{
    static const char name[] = "TexModulate";
    static const long colour8[3] = { 0x80l, 0xc0l, 0x40l };
    const unsigned char *actual;
    char key[64];
    long values[3];
    long texel[3];
    long maxima[3];
    long worst = 0l;
    DWORD x;
    DWORD y;
    DWORD inside_wrong = 0u;
    DWORD outside_changed = 0u;
    int channel;

    if (!rs_draw_textured(name, 0ul, V9X_RV280_COMBINE_MODULATE,
                          MODULATE_COLOUR, 16u, TEX_WIDTH, 1l, 1l)) {
        return;
    }
    rs_save_block('M');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);

            if (x >= 16u && x < 16u + TEX_WIDTH && y < TEX_HEIGHT) {
                rs_channels(pixel, values, maxima);
                rs_channels(rs_texel(x - 16u, y), texel, maxima);
                for (channel = 0; channel < 3; ++channel) {
                    long expected = (texel[channel] * colour8[channel] +
                                     127l) / 255l;
                    long error = rs_abs(values[channel] - expected);

                    if (error > worst) {
                        worst = error;
                    }
                    if (error > 1l) {
                        ++inside_wrong;
                        break;
                    }
                }
            } else if (pixel != rs_pixel(rs_model, x, y)) {
                ++outside_changed;
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "InsideWrong"), inside_wrong);
    rs_write_decimal(rs_key(key, name, "WorstChannelError"), (DWORD)worst);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (inside_wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/* A texel index clamped to the texture, as CLAMP_LAST samples. */
static DWORD rs_clamp_index(long index, DWORD limit)
{
    if (index < 0l) {
        return 0u;
    }
    if (index >= (long)limit) {
        return limit - 1u;
    }
    return (DWORD)index;
}

/* The bilinear filter's tolerance: one step plus this share of the four
 * texels' spread, as on the RS690 (its weights were coarse). */
#define RS_BILINEAR_SPREAD_SHARE 32l

/*
 * TexBilinear: texels 0-31 x 0-7 magnified 2x to 64 x 16 pixels, bilinear,
 * clamped; the RS690 probe's model. Pixel x's centre samples texel space at
 * u = (x + 0.5) / 2 - 0.5 = (2x - 1) / 4, so in quarter texels: weights out
 * of 16, each channel compared at 16 times its value.
 */
static void rs_tex_bilinear_test(void)
{
    static const char name[] = "TexBilinear";
    const unsigned char *actual;
    char key[64];
    DWORD x;
    DWORD y;
    DWORD inside_wrong = 0u;
    DWORD outside_changed = 0u;
    DWORD first_wrong = 0xfffffffful;
    long worst = 0l;

    if (!rs_draw_textured(name, 1ul, V9X_RV280_COMBINE_DECAL, ARGB_RED, 0u,
                          TEST_WIDTH, 1l, 2l)) {
        return;
    }
    rs_save_block('B');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);
            long u4 = 2l * (long)x - 1l + 4l;
            long v4 = 2l * (long)y - 1l + 4l;
            long fu = u4 % 4l;
            long fv = v4 % 4l;
            long u0 = u4 / 4l - 1l;
            long v0 = v4 / 4l - 1l;
            DWORD texels[4];
            long weights[4];
            long values[3];
            long maxima[3];
            long expected[3] = { 0l, 0l, 0l };
            long lowest[3] = { 255l, 255l, 255l };
            long highest[3] = { 0l, 0l, 0l };
            int corner;
            int channel;
            int wrong = 0;

            if (y >= TEX_HEIGHT) {
                if (pixel != rs_pixel(rs_model, x, y)) {
                    ++outside_changed;
                }
                continue;
            }
            texels[0] = rs_texel(rs_clamp_index(u0, TEX_WIDTH),
                                 rs_clamp_index(v0, TEX_HEIGHT));
            texels[1] = rs_texel(rs_clamp_index(u0 + 1l, TEX_WIDTH),
                                 rs_clamp_index(v0, TEX_HEIGHT));
            texels[2] = rs_texel(rs_clamp_index(u0, TEX_WIDTH),
                                 rs_clamp_index(v0 + 1l, TEX_HEIGHT));
            texels[3] = rs_texel(rs_clamp_index(u0 + 1l, TEX_WIDTH),
                                 rs_clamp_index(v0 + 1l, TEX_HEIGHT));
            weights[0] = (4l - fu) * (4l - fv);
            weights[1] = fu * (4l - fv);
            weights[2] = (4l - fu) * fv;
            weights[3] = fu * fv;
            for (corner = 0; corner < 4; ++corner) {
                rs_channels(texels[corner], values, maxima);
                for (channel = 0; channel < 3; ++channel) {
                    expected[channel] += weights[corner] * values[channel];
                    if (values[channel] < lowest[channel]) {
                        lowest[channel] = values[channel];
                    }
                    if (values[channel] > highest[channel]) {
                        highest[channel] = values[channel];
                    }
                }
            }
            rs_channels(pixel, values, maxima);
            for (channel = 0; channel < 3; ++channel) {
                long allowed = 16l + (highest[channel] - lowest[channel]) *
                               16l / RS_BILINEAR_SPREAD_SHARE;
                long error = rs_abs(values[channel] * 16l -
                                    expected[channel]);

                if (error > worst) {
                    worst = error;
                }
                if (error > allowed) {
                    wrong = 1;
                }
            }
            if (wrong) {
                if (first_wrong == 0xfffffffful) {
                    first_wrong = y * TEST_WIDTH + x;
                }
                ++inside_wrong;
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "InsideWrong"), inside_wrong);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    rs_write_decimal(rs_key(key, name, "MaxErrorCenti"),
                     (DWORD)((worst * 100l + 8l) / 16l));
    if (first_wrong != 0xfffffffful) {
        rs_write_decimal(rs_key(key, name, "FirstWrongX"),
                         first_wrong % TEST_WIDTH);
        rs_write_decimal(rs_key(key, name, "FirstWrongY"),
                         first_wrong / TEST_WIDTH);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, first_wrong % TEST_WIDTH,
                              first_wrong / TEST_WIDTH));
    }
    if (inside_wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

static void rs_fill_after_test(void)
{
    static const char name[] = "FillAfter3D";
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt init;
    struct v9x_rs690_blt blt;
    const unsigned char *actual;
    char key[64];
    DWORD datatype;
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD mismatches = 0u;
    DWORD colour;

    datatype = rs_read_register(V9X_RS690_DP_DATATYPE);
    if (v9x_rs690_build_init(datatype, &init) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "INIT-REFUSED");
        rs_failed = 1u;
        return;
    }
    colour = rs_bpp == 2u ? 0x1234ul : 0x00123456ul;
    fill.fb_gpu_base = rs_fb_gpu_base;
    fill.fb_bytes = TEST_FB;
    fill.target_offset = TEST_BASE;
    fill.pitch_bytes = rs_pitch;
    fill.bytes_per_pixel = rs_bpp;
    fill.left = 5u;
    fill.top = 3u;
    fill.width = 24u;
    fill.height = 10u;
    fill.color = colour;
    if (v9x_rs690_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "BUILDER-REFUSED");
        rs_failed = 1u;
        return;
    }

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_blt(&init);
    rs_add_blt(&blt);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return;
    }
    rs_save_block('F');

    rs_model_prefill();
    for (y = 3u; y < 13u; ++y) {
        for (x = 5u; x < 29u; ++x) {
            rs_set_pixel(rs_model, x, y, colour);
        }
    }
    actual = (const unsigned char *)rs_result_buffer.block;
    for (index = 0u; index < TEST_BYTES; ++index) {
        if (actual[index] != rs_model[index]) {
            ++mismatches;
        }
    }
    rs_write_decimal(rs_key(key, name, "MismatchBytes"), mismatches);
    rs_write(rs_key(key, name, "Result"), mismatches == 0u ? "PASS" :
             "MISMATCH");
    if (mismatches != 0u) {
        rs_failed = 1u;
    }
}

static void rs_close(DWORD code)
{
    if (rs_device != INVALID_HANDLE_VALUE && rs_device != 0) {
        CloseHandle(rs_device);
    }
    CloseHandle(rs_output);
    ExitProcess(code);
}

void WINAPI V9xRv2803dProbeEntry(void)
{
    static const char header[] = "[Rv2803dProbe]\r\n";
    struct v9x_rv280_3d_list init;
    HDC display;
    DWORD returned;
    DWORD desktop_bpp = 0u;
    DWORD desktop_bytes = 0u;
    DWORD csq;
    DWORD location;
    int hue0;
    int hue1;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    rs_output = CreateFileA(RS_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (rs_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(rs_output, header, (DWORD)lstrlenA(header), &returned, 0);
    rs_write("Build", V9X_BUILD_ID);

    display = GetDC(0);
    if (display != 0) {
        DWORD width = (DWORD)GetDeviceCaps(display, HORZRES);
        DWORD height = (DWORD)GetDeviceCaps(display, VERTRES);

        desktop_bpp = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                              GetDeviceCaps(display, PLANES));
        rs_write_decimal("DesktopWidth", width);
        rs_write_decimal("DesktopHeight", height);
        rs_write_decimal("DesktopBpp", desktop_bpp);
        desktop_bytes = width * height * ((desktop_bpp + 7u) / 8u);
        ReleaseDC(0, display);
    }
    rs_bpp = (desktop_bpp + 7u) / 8u;
    if (rs_bpp != 2u && rs_bpp != 4u) {
        rs_write("Result", "REFUSED-DEPTH");
        rs_close(1u);
    }
    rs_pitch = TEST_WIDTH * rs_bpp;
    rs_height = TEST_BYTES / rs_pitch;
    rs_write_decimal("SurfaceHeight", rs_height);
    if (desktop_bytes == 0u || desktop_bytes >= TEST_BASE) {
        rs_write("Result", "REFUSED-DESKTOP");
        rs_close(1u);
    }
    rs_tri_x[0] = 2l;
    rs_tri_y[0] = 1l;
    rs_tri_x[1] = 54l;
    rs_tri_y[1] = 1l;
    rs_tri_x[2] = 2l;
    rs_tri_y[2] = (long)rs_height - 2l;

    rs_device = CreateFileA("\\\\.\\R23D.VXD", 0, 0, 0, CREATE_NEW,
                            FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (rs_device == INVALID_HANDLE_VALUE) {
        rs_write("Result", "VXD-NOT-LOADED");
        rs_close(2u);
    }

    csq = rs_read_register(V9X_RS690_CP_CSQ_CNTL);
    rs_write_hex("VxdStatus", rs_result_buffer.status);
    rs_write_hex("ConfigId", rs_result_buffer.config_id);
    rs_write_hex("MemSize", rs_result_buffer.memsize);
    if ((rs_result_buffer.status & RS_MAPPED) == 0ul) {
        rs_write("Result", "NOT-MAPPED");
        rs_close(3u);
    }
    rs_write_hex("CpCsqCntl", csq);
    rs_write_hex("RbbmStatusAtStart",
                 rs_read_register(V9X_RS690_RBBM_STATUS));
    if (v9x_rs690_cp_off(csq) == 0ul) {
        rs_write("Result", "REFUSED-CP-OWNS-ENGINE");
        rs_close(3u);
    }
    location = rs_read_register(RV_MC_FB_LOCATION);
    rs_write_hex("McFbLocation", location);
    rs_fb_gpu_base = v9x_rs690_fb_gpu_base(location);
    rs_write_hex("FbGpuBase", rs_fb_gpu_base);
    rs_write_hex("SeCntlAtStart", rs_read_register(V9X_RV280_SE_CNTL));
    rs_write_hex("Rb3dCntlAtStart", rs_read_register(V9X_RV280_RB3D_CNTL));

    /* The R200 init, once, on an idle engine. */
    (void)v9x_rv280_build_3d_init(&init);
    rs_begin();
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_list(&init);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (!rs_run()) {
        rs_write("Result", "INIT-NOT-RUN");
        rs_report_refusal("Init");
        rs_close(3u);
    }
    rs_write_hex("RbbmStatusAfterInit",
                 rs_read_register(V9X_RS690_RBBM_STATUS));

    hue0 = rs_flat_test("FlatOrder0", '0', 0);
    hue1 = rs_flat_test("FlatOrder1", '1', 1);
    if (hue0 == 1) {
        rs_write("ColourOrder", "argb-order0");
        rs_gouraud_test(0, 0);
    } else if (hue1 == 1) {
        rs_write("ColourOrder", "argb-order1");
        rs_gouraud_test(1, 0);
    } else if (hue0 == 2) {
        rs_write("ColourOrder", "abgr-order0");
        rs_gouraud_test(0, 1);
    } else {
        rs_write("ColourOrder", "none-drew-red-or-blue");
        rs_failed = 1u;
    }
    rs_tex_exact_test("TexDecal", 'D', 8u, TEX_WIDTH, 1l);
    rs_tex_exact_test("TexWrap", 'W', 0u, TEST_WIDTH, 2l);
    rs_tex_modulate_test();
    rs_tex_bilinear_test();
    rs_fill_after_test();

    rs_write_hex("RbbmStatusAtEnd", rs_read_register(V9X_RS690_RBBM_STATUS));
    rs_write("Result", rs_failed == 0u ? "PASS" : "FAIL");
    rs_close(rs_failed == 0u ? 0u : 1u);
}
