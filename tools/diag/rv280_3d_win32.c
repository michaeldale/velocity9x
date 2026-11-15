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
 *   ZUnit / ZBuffer - three overlapping quads, Z16 LESS with writes, at
 *               z 0.5, 0.25 and 0.75 (ZUnit) or those times 65535
 *               (ZBuffer): which range the vertex Z is in is not in the
 *               sources, so both run and ZRange reports which drew the
 *               nearest quad on top. The Z buffer's values at three
 *               columns are published.
 *   Blend     - SRCALPHA / INVSRCALPHA, alpha 80h red over the prefill,
 *               within a step of the model.
 *   AlphaTest - GREATER 128 keeps the alpha-200 quad, drops the alpha-100.
 *   Cull      - a screen-clockwise and a screen-counter-clockwise triangle
 *               under three face settings: which survives each, and so
 *               which winding the hardware calls clockwise.
 *   Perspective - 1/W 1 to 0.25 across a textured quad: perspective-
 *               correct texel columns, not affine ones.
 *   FogFlat / FogGouraud - vertex fog from the specular alpha toward blue,
 *               80h everywhere, then FFh to 0 across the quad.
 *   Specular  - diffuse 404040h plus specular 202020h: 606060h.
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
    target.raster = 0;
    target.layout = 0u;
    for (index = 0; index < 3; ++index) {
        vertices[index].x_bits = rs_float_bits(rs_tri_x[index]);
        vertices[index].y_bits = rs_float_bits(rs_tri_y[index]);
        vertices[index].z_bits = 0u;
        vertices[index].w_bits = 0u;
        vertices[index].specular = 0u;
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
    if (v9x_rv280_build_3d_triangles(vertices, 3u, order_rgba, 0u, &list) !=
        V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rv280_build_3d_finish(0, &list);
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
    target.raster = 0;
    target.layout = 0u;
    texture.fb_gpu_base = rs_fb_gpu_base;
    texture.fb_bytes = TEST_FB;
    texture.offset = TEX_OFFSET;
    texture.pitch_bytes = TEX_WIDTH * rs_bpp;
    texture.bytes_per_pixel = rs_bpp;
    texture.format = V9X_RV280_TEXFMT_DEFAULT;
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
        vertices[index].z_bits = 0u;
        vertices[index].w_bits = 0u;
        vertices[index].specular = 0u;
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
    if (v9x_rv280_build_3d_triangles(vertices, 6u, 0, V9X_RV280_LAYOUT_ST,
                                     &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rv280_build_3d_finish(0, &list);
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

/* No Z, blending off, no alpha test. */
static void rs_zero_raster(struct v9x_rv280_3d_raster *raster)
{
    raster->z_enable = 0ul;
    raster->z_offset = 0ul;
    raster->z_pitch_bytes = 0ul;
    raster->z_func = 0ul;
    raster->z_write = 0ul;
    raster->blend_enable = 0ul;
    raster->blend_src = 0ul;
    raster->blend_dst = 0ul;
    raster->alpha_test_enable = 0ul;
    raster->alpha_func = 0ul;
    raster->alpha_ref = 0ul;
    raster->front_ccw = 0ul;
    raster->cull_front = 0ul;
    raster->cull_back = 0ul;
    raster->fog_enable = 0ul;
    raster->fog_colour = 0ul;
    raster->specular_enable = 0ul;
    raster->keep_x = 0ul;
    raster->masked_channels = 0ul;
    raster->scissor_enable = 0ul;
    raster->scissor_left = 0ul;
    raster->scissor_top = 0ul;
    raster->scissor_right = 0ul;
    raster->scissor_bottom = 0ul;
}

/* The Z buffer: 16-bit, 64 pixels to a row, 128 KiB past the surface. */
#define ZBUF_OFFSET (TEST_BASE + 0x20000ul)
#define ZBUF_PITCH (TEST_WIDTH * 2ul)

/* One flat quad: columns left to left + width - 1, rows 0 to TEX_HEIGHT -
 * 1, at depth z_bits, in colour. */
struct rs_quad {
    DWORD left;
    DWORD width;
    DWORD z_bits;
    DWORD colour;
};

/*
 * Prefill the surface (and with a raster Z buffer, fill Z with FFFFh), wait
 * idle, the state once, each quad as two triangles, the finish (flushing Z
 * when there is a Z buffer), wait idle, read the surface back. Non-zero with
 * the surface in the result block.
 */
static int rs_draw_quads(const char *name,
                         const struct v9x_rv280_3d_raster *raster,
                         const struct rs_quad *quads, DWORD count)
{
    static const int corner_x[6] = { 0, 1, 0, 1, 1, 0 };
    static const int corner_y[6] = { 0, 0, 1, 0, 1, 1 };
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_vertex vertices[6];
    struct v9x_rv280_3d_list list;
    char key[64];
    DWORD after_index;
    DWORD quad;
    DWORD layout;
    int index;

    target.fb_gpu_base = rs_fb_gpu_base;
    target.fb_bytes = TEST_FB;
    target.offset = TEST_BASE;
    target.pitch_bytes = rs_pitch;
    target.bytes_per_pixel = rs_bpp;
    target.width = TEST_WIDTH;
    target.height = rs_height;
    target.raster = raster;
    layout = raster->z_enable ? V9X_RV280_LAYOUT_Z : 0u;
    target.layout = layout;

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    if (raster->z_enable) {
        rs_add(RS_OP_LFB_FILL32, ZBUF_OFFSET, 0xfffffffful,
               ZBUF_PITCH * rs_height / 4u);
    }
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rv280_build_3d_target(&target, 0, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    for (quad = 0u; quad < count; ++quad) {
        for (index = 0; index < 6; ++index) {
            vertices[index].x_bits = rs_float_bits(
                (long)quads[quad].left +
                (corner_x[index] ? (long)quads[quad].width : 0l));
            vertices[index].y_bits =
                rs_float_bits(corner_y[index] ? (long)TEX_HEIGHT : 0l);
            vertices[index].z_bits = quads[quad].z_bits;
            vertices[index].w_bits = 0u;
            vertices[index].specular = 0u;
            vertices[index].colour = quads[quad].colour;
            vertices[index].s_bits = 0u;
            vertices[index].t_bits = 0u;
        }
        if (v9x_rv280_build_3d_triangles(vertices, 6u, 0, layout, &list) !=
            V9X_STATUS_OK) {
            rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
            rs_failed = 1u;
            return 0;
        }
        rs_add_list(&list);
    }
    (void)v9x_rv280_build_3d_finish(raster->z_enable ? 1 : 0, &list);
    rs_add_list(&list);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return 0;
    }
    rs_write_hex(rs_key(key, name, "WaitAfter"),
                 rs_result_buffer.ops[after_index].result);
    return 1;
}

/* A flat colour as the surface stores it. */
static DWORD rs_store(DWORD argb)
{
    if (rs_bpp == 2u) {
        return ((argb >> 8) & 0xf800ul) | ((argb >> 5) & 0x07e0ul) |
               ((argb >> 3) & 0x001ful);
    }
    return argb & 0x00fffffful;
}

/* Pixels in rows 0 to TEX_HEIGHT - 1 compared with the colour model
 * expected(x) (RGB, 0xffffffff meaning the prefill), the rest with the
 * prefill. Publishes and returns the number wrong. */
static DWORD rs_judge_columns(const char *name, const DWORD *expected)
{
    const unsigned char *actual = (const unsigned char *)rs_result_buffer.block;
    char key[64];
    DWORD x;
    DWORD y;
    DWORD wrong = 0u;
    DWORD first = 0xfffffffful;

    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);
            int ok;

            if (y < TEX_HEIGHT && expected[x] != 0xfffffffful) {
                ok = rs_rgb(pixel) == expected[x];
            } else {
                ok = pixel == rs_pixel(rs_model, x, y);
            }
            if (!ok) {
                if (first == 0xfffffffful) {
                    first = y * TEST_WIDTH + x;
                }
                ++wrong;
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "Wrong"), wrong);
    if (first != 0xfffffffful) {
        rs_write_decimal(rs_key(key, name, "FirstWrongX"), first % TEST_WIDTH);
        rs_write_decimal(rs_key(key, name, "FirstWrongY"), first / TEST_WIDTH);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, first % TEST_WIDTH,
                              first / TEST_WIDTH));
    }
    return wrong;
}

/* The Z buffer's value at (x, 0) after the last draw, or FFFFFFFFh. */
static DWORD rs_read_z(DWORD x)
{
    rs_begin();
    rs_add(RS_OP_LFB_READ_BLOCK, ZBUF_OFFSET, 0u, ZBUF_PITCH / 4u);
    if (!rs_run()) {
        return 0xfffffffful;
    }
    return ((const unsigned short *)rs_result_buffer.block)[x];
}

/*
 * ZTest: three quads, Z16 LESS with writes, drawn red at z 0.5, green at
 * 0.25 over its right half, blue at 0.75 over the middle. With depth
 * working, columns 4-19 are red, 20-51 green, blue nowhere. Run with z in
 * [0, 1] and again scaled by 65535, since no source says which range the
 * vertex Z is in without the viewport transform; the Z buffer's values at
 * three columns say how it was stored.
 */
static int rs_z_test(const char *name, long scale)
{
    struct v9x_rv280_3d_raster raster;
    struct rs_quad quads[3];
    DWORD expected[TEST_WIDTH];
    char key[64];
    DWORD x;
    DWORD wrong;

    rs_zero_raster(&raster);
    raster.z_enable = 1ul;
    raster.z_offset = ZBUF_OFFSET;
    raster.z_pitch_bytes = ZBUF_PITCH;
    raster.z_func = 1ul;
    raster.z_write = 1ul;
    quads[0].left = 4u;
    quads[0].width = 32u;
    quads[0].z_bits = rs_ratio_bits(scale, 2l);
    quads[0].colour = ARGB_RED;
    quads[1].left = 20u;
    quads[1].width = 32u;
    quads[1].z_bits = rs_ratio_bits(scale, 4l);
    quads[1].colour = ARGB_GREEN;
    quads[2].left = 12u;
    quads[2].width = 32u;
    quads[2].z_bits = rs_ratio_bits(scale * 3l, 4l);
    quads[2].colour = ARGB_BLUE;
    if (!rs_draw_quads(name, &raster, quads, 3u)) {
        return 0;
    }
    rs_save_block(scale == 1l ? 'Z' : 'U');
    for (x = 0u; x < TEST_WIDTH; ++x) {
        expected[x] = x >= 4u && x < 20u ? rs_store(ARGB_RED) :
                      (x >= 20u && x < 52u ? rs_store(ARGB_GREEN) :
                       0xfffffffful);
    }
    wrong = rs_judge_columns(name, expected);
    rs_write_hex(rs_key(key, name, "ZAt8"), rs_read_z(8u));
    rs_write_hex(rs_key(key, name, "ZAt30"), rs_read_z(30u));
    rs_write_hex(rs_key(key, name, "ZAt60"), rs_read_z(60u));
    rs_write(rs_key(key, name, "Result"), wrong == 0u ? "PASS" : "MISMATCH");
    return wrong == 0u;
}

/*
 * Blend: a red quad with alpha 80h over the prefill, SRCALPHA /
 * INVSRCALPHA: each channel within a step of red * 128/255 + prefill *
 * 127/255. Columns 8-39.
 */
static void rs_blend_test(void)
{
    static const char name[] = "Blend";
    struct v9x_rv280_3d_raster raster;
    struct rs_quad quad;
    const unsigned char *actual;
    char key[64];
    long values[3];
    long maxima[3];
    long dst[3];
    long worst = 0l;
    DWORD x;
    DWORD y;
    DWORD wrong = 0u;
    DWORD outside_changed = 0u;
    DWORD prefill_pixel;
    int channel;

    rs_zero_raster(&raster);
    raster.blend_enable = 1ul;
    raster.blend_src = V9X_RV280_BLEND_FACTOR_SRC_ALPHA;
    raster.blend_dst = V9X_RV280_BLEND_FACTOR_INV_SRC_ALPHA;
    quad.left = 8u;
    quad.width = 32u;
    quad.z_bits = 0u;
    quad.colour = 0x80ff0000ul;
    if (!rs_draw_quads(name, &raster, &quad, 1u)) {
        return;
    }
    rs_save_block('A');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    prefill_pixel = rs_pixel(rs_model, 0u, 0u);
    rs_channels(prefill_pixel, dst, maxima);
    rs_write_hex(rs_key(key, name, "PixelAt20"), rs_pixel(actual, 20u, 4u));
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);

            if (y >= TEX_HEIGHT || x < 8u || x >= 40u) {
                if (pixel != rs_pixel(rs_model, x, y)) {
                    ++outside_changed;
                }
                continue;
            }
            rs_channels(pixel, values, maxima);
            for (channel = 0; channel < 3; ++channel) {
                long src = channel == 0 ? maxima[0] : 0l;
                long expected = (src * 128l + dst[channel] * 127l + 127l) /
                                255l;
                long error = rs_abs(values[channel] - expected);

                if (error > worst) {
                    worst = error;
                }
                if (error > 1l) {
                    ++wrong;
                    break;
                }
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "InsideWrong"), wrong);
    rs_write_decimal(rs_key(key, name, "WorstChannelError"), (DWORD)worst);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/*
 * Scissor: a red quad over every column of rows 0-15, scissored to columns
 * 12-39 and rows 2-5 (the builder's corners inclusive, as Mesa's r200
 * writes them). Exactly that rectangle red, every other pixel the prefill.
 */
#define RS_SCISSOR_LEFT   12ul
#define RS_SCISSOR_TOP    2ul
#define RS_SCISSOR_RIGHT  40ul
#define RS_SCISSOR_BOTTOM 6ul

static void rs_scissor_test(void)
{
    static const char name[] = "Scissor";
    struct v9x_rv280_3d_raster raster;
    struct rs_quad quad;
    const unsigned char *actual;
    char key[64];
    DWORD x;
    DWORD y;
    DWORD wrong = 0u;
    DWORD inside_wrong = 0u;
    DWORD first = 0xfffffffful;

    rs_zero_raster(&raster);
    raster.keep_x = 1ul;
    raster.scissor_enable = 1ul;
    raster.scissor_left = RS_SCISSOR_LEFT;
    raster.scissor_top = RS_SCISSOR_TOP;
    raster.scissor_right = RS_SCISSOR_RIGHT;
    raster.scissor_bottom = RS_SCISSOR_BOTTOM;
    quad.left = 0u;
    quad.width = TEST_WIDTH;
    quad.z_bits = 0u;
    quad.colour = ARGB_RED;
    if (!rs_draw_quads(name, &raster, &quad, 1u)) {
        return;
    }
    rs_save_block('Q');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);
            int inside = x >= RS_SCISSOR_LEFT && x < RS_SCISSOR_RIGHT &&
                         y >= RS_SCISSOR_TOP && y < RS_SCISSOR_BOTTOM;
            int ok = inside ? rs_rgb(pixel) == rs_store(ARGB_RED)
                            : pixel == rs_pixel(rs_model, x, y);

            if (!ok) {
                if (first == 0xfffffffful) {
                    first = y * TEST_WIDTH + x;
                }
                ++wrong;
                if (inside) {
                    ++inside_wrong;
                }
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "Wrong"), wrong);
    rs_write_decimal(rs_key(key, name, "InsideWrong"), inside_wrong);
    if (first != 0xfffffffful) {
        rs_write_decimal(rs_key(key, name, "FirstWrongX"), first % TEST_WIDTH);
        rs_write_decimal(rs_key(key, name, "FirstWrongY"), first / TEST_WIDTH);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, first % TEST_WIDTH,
                              first / TEST_WIDTH));
    }
    if (wrong != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/*
 * ChannelMask: a white quad over columns 8-39 of rows 0-15 with the given
 * channels masked (and, at 32 bpp, the X byte kept): there each masked
 * channel and X keep the prefill's bits and the rest are white; every other
 * pixel is the prefill.
 */
static void rs_channel_mask_test(const char *name, char letter,
                                 DWORD masked)
{
    struct v9x_rv280_3d_raster raster;
    struct rs_quad quad;
    const unsigned char *actual;
    char key[64];
    DWORD keep;
    DWORD white;
    DWORD x;
    DWORD y;
    DWORD wrong = 0u;
    DWORD first = 0xfffffffful;

    rs_zero_raster(&raster);
    raster.keep_x = 1ul;
    raster.masked_channels = masked;
    quad.left = 8u;
    quad.width = 32u;
    quad.z_bits = 0u;
    quad.colour = 0xfffffffful;
    if (!rs_draw_quads(name, &raster, &quad, 1u)) {
        return;
    }
    rs_save_block(letter);
    actual = (const unsigned char *)rs_result_buffer.block;
    /* The bits a pixel keeps from the prefill. */
    if (rs_bpp == 2u) {
        keep = ((masked & V9X_RV280_CHANNEL_RED) != 0ul ? 0xf800ul : 0ul) |
               ((masked & V9X_RV280_CHANNEL_GREEN) != 0ul ? 0x07e0ul : 0ul) |
               ((masked & V9X_RV280_CHANNEL_BLUE) != 0ul ? 0x001ful : 0ul);
        white = 0xfffful;
    } else {
        keep = 0xff000000ul |
               ((masked & V9X_RV280_CHANNEL_RED) != 0ul ? 0x00ff0000ul : 0ul) |
               ((masked & V9X_RV280_CHANNEL_GREEN) != 0ul ? 0x0000ff00ul
                                                          : 0ul) |
               ((masked & V9X_RV280_CHANNEL_BLUE) != 0ul ? 0x000000fful : 0ul);
        white = 0xfffffffful;
    }
    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);
            DWORD prefill = rs_pixel(rs_model, x, y);
            DWORD expected = prefill;

            if (y < TEX_HEIGHT && x >= 8u && x < 40u) {
                expected = (prefill & keep) | (white & ~keep);
            }
            if (pixel != expected) {
                if (first == 0xfffffffful) {
                    first = y * TEST_WIDTH + x;
                }
                ++wrong;
            }
        }
    }
    rs_write_hex(rs_key(key, name, "PixelAt20"), rs_pixel(actual, 20u, 4u));
    rs_write_decimal(rs_key(key, name, "Wrong"), wrong);
    if (first != 0xfffffffful) {
        rs_write_decimal(rs_key(key, name, "FirstWrongX"), first % TEST_WIDTH);
        rs_write_decimal(rs_key(key, name, "FirstWrongY"), first / TEST_WIDTH);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, first % TEST_WIDTH,
                              first / TEST_WIDTH));
    }
    if (wrong != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/*
 * Pitch: a green quad over columns 4-51 of rows 0-15 into the test surface
 * with its pitch widened by `extra` pixels. COLORPITCH takes any multiple
 * of 8 pixels (radeon_reg.h's 1FF8h mask), but Mesa aligns R200 colour
 * pitches to 64 bytes, and V9XGLP's 360-pixel (720-byte) OpenGL target
 * drew sheared on M-HNBJNV. 8 extra pixels make a pitch that is not a
 * multiple of 64 bytes at either depth; 32 make one that is (the control).
 */
static void rs_pitch_test(const char *name, char letter, DWORD extra)
{
    struct v9x_rv280_3d_raster raster;
    struct rs_quad quad;
    DWORD expected[TEST_WIDTH];
    DWORD saved_pitch = rs_pitch;
    DWORD saved_height = rs_height;
    char key[64];
    DWORD x;
    DWORD wrong;

    rs_pitch = (TEST_WIDTH + extra) * rs_bpp;
    rs_height = TEST_BYTES / rs_pitch;
    rs_write_decimal(rs_key(key, name, "PitchBytes"), rs_pitch);
    rs_zero_raster(&raster);
    raster.keep_x = 1ul;
    quad.left = 4u;
    quad.width = 48u;
    quad.z_bits = 0u;
    quad.colour = ARGB_GREEN;
    if (rs_draw_quads(name, &raster, &quad, 1u)) {
        rs_save_block(letter);
        for (x = 0u; x < TEST_WIDTH; ++x) {
            expected[x] = x >= 4u && x < 52u ? rs_store(ARGB_GREEN)
                                             : 0xfffffffful;
        }
        wrong = rs_judge_columns(name, expected);
        rs_write(rs_key(key, name, "Result"), wrong == 0u ? "PASS"
                                                          : "MISMATCH");
        if (wrong != 0u) {
            rs_failed = 1u;
        }
    }
    rs_pitch = saved_pitch;
    rs_height = saved_height;
}

/*
 * AlphaTest: GREATER 128, no blending. A red quad with alpha 200 at
 * columns 4-27 is drawn; a green one with alpha 100 at 36-59 is not.
 */
static void rs_alpha_test(void)
{
    static const char name[] = "AlphaTest";
    struct v9x_rv280_3d_raster raster;
    struct rs_quad quads[2];
    DWORD expected[TEST_WIDTH];
    char key[64];
    DWORD x;
    DWORD wrong;

    rs_zero_raster(&raster);
    raster.alpha_test_enable = 1ul;
    raster.alpha_func = 5ul;
    raster.alpha_ref = 128ul;
    quads[0].left = 4u;
    quads[0].width = 24u;
    quads[0].z_bits = 0u;
    quads[0].colour = 0xc8ff0000ul;
    quads[1].left = 36u;
    quads[1].width = 24u;
    quads[1].z_bits = 0u;
    quads[1].colour = 0x6400ff00ul;
    if (!rs_draw_quads(name, &raster, quads, 2u)) {
        return;
    }
    rs_save_block('T');
    for (x = 0u; x < TEST_WIDTH; ++x) {
        expected[x] = x >= 4u && x < 28u ? rs_store(ARGB_RED) : 0xfffffffful;
    }
    wrong = rs_judge_columns(name, expected);
    rs_write(rs_key(key, name, "Result"), wrong == 0u ? "PASS" : "MISMATCH");
    if (wrong != 0u) {
        rs_failed = 1u;
    }
}

/* One quad corner: position, 1/W, colours and texture coordinates as the
 * vertex sends them. */
struct rs_corner {
    long x;
    long y;
    DWORD w_bits;
    DWORD colour;
    DWORD specular;
    DWORD s_bits;
    DWORD t_bits;
};

/*
 * Prefill, (the texture,) wait idle, the state, the triangles, the finish,
 * wait idle, read back. vertices are given as corners, three to a triangle.
 * texture may be zero for the diffuse-only environment.
 */
static int rs_draw_corners(const char *name,
                           const struct v9x_rv280_3d_raster *raster,
                           DWORD layout,
                           const struct v9x_rv280_3d_texture *texture,
                           int gouraud, const struct rs_corner *corners,
                           DWORD count)
{
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_vertex vertices[V9X_RV280_3D_MAX_VERTICES];
    struct v9x_rv280_3d_list list;
    char key[64];
    DWORD after_index;
    DWORD index;
    v9x_status status;

    target.fb_gpu_base = rs_fb_gpu_base;
    target.fb_bytes = TEST_FB;
    target.offset = TEST_BASE;
    target.pitch_bytes = rs_pitch;
    target.bytes_per_pixel = rs_bpp;
    target.width = TEST_WIDTH;
    target.height = rs_height;
    target.raster = raster;
    target.layout = layout & ~(DWORD)V9X_RV280_LAYOUT_ST;
    for (index = 0u; index < count; ++index) {
        vertices[index].x_bits = rs_float_bits(corners[index].x);
        vertices[index].y_bits = rs_float_bits(corners[index].y);
        vertices[index].z_bits = 0u;
        vertices[index].w_bits = corners[index].w_bits;
        vertices[index].colour = corners[index].colour;
        vertices[index].specular = corners[index].specular;
        vertices[index].s_bits = corners[index].s_bits;
        vertices[index].t_bits = corners[index].t_bits;
    }

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    if (texture != 0) {
        rs_add(RS_OP_LFB_GRADIENT, TEX_OFFSET, 0u,
               TEX_WIDTH * TEX_HEIGHT * rs_bpp / 4u);
        status = v9x_rv280_build_3d_textured_target(
            &target, gouraud, texture, V9X_RV280_COMBINE_DECAL, &list);
    } else {
        status = v9x_rv280_build_3d_target(&target, gouraud, &list);
    }
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (status != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    if (v9x_rv280_build_3d_triangles(vertices, count, 0, layout, &list) !=
        V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rv280_build_3d_finish(0, &list);
    rs_add_list(&list);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return 0;
    }
    rs_write_hex(rs_key(key, name, "WaitAfter"),
                 rs_result_buffer.ops[after_index].result);
    return 1;
}

/* A quad from (left, 0) to (left + width, TEX_HEIGHT) as two triangles into
 * corners[0..5]: left corners take left, right corners right. */
static void rs_quad_corners(struct rs_corner *corners, DWORD left,
                            DWORD width, const struct rs_corner *left_side,
                            const struct rs_corner *right_side)
{
    static const int corner_x[6] = { 0, 1, 0, 1, 1, 0 };
    static const int corner_y[6] = { 0, 0, 1, 0, 1, 1 };
    int index;

    for (index = 0; index < 6; ++index) {
        corners[index] = corner_x[index] ? *right_side : *left_side;
        corners[index].x = (long)left + (corner_x[index] ? (long)width : 0l);
        corners[index].y = corner_y[index] ? (long)TEX_HEIGHT : 0l;
        corners[index].t_bits = rs_ratio_bits(corner_y[index], 1l);
    }
}

/* Whether a red (1) or green (2) triangle covers the pixel at the given
 * column, row 5: the cull test's verdict per triangle. */
static int rs_drawn_at(DWORD x, DWORD argb)
{
    const unsigned char *actual = (const unsigned char *)rs_result_buffer.block;

    return rs_rgb(rs_pixel(actual, x, 5u)) == rs_store(argb);
}

/*
 * Cull: a red triangle wound clockwise on the screen (y down: top-left,
 * top-right, bottom-left) at columns 4-28 and a green one wound counter-
 * clockwise at 36-60, drawn under three face settings. Reports which
 * survives each; which winding the hardware calls clockwise is what this
 * measures. Passes when each run keeps exactly one and the three runs
 * agree with one reading of the bits.
 */
static void rs_cull_test(void)
{
    static const char name[] = "Cull";
    static const char * const runs[3] = {
        "BackCwFront", "FrontCwFront", "BackCcwFront"
    };
    struct v9x_rv280_3d_raster raster;
    struct rs_corner corners[6];
    char key[64];
    char prefix[32];
    int run;
    int red[3];
    int green[3];

    corners[0].x = 4l;
    corners[0].y = 1l;
    corners[1].x = 28l;
    corners[1].y = 1l;
    corners[2].x = 4l;
    corners[2].y = 14l;
    corners[3].x = 36l;
    corners[3].y = 1l;
    corners[4].x = 36l;
    corners[4].y = 14l;
    corners[5].x = 60l;
    corners[5].y = 1l;
    for (run = 0; run < 6; ++run) {
        corners[run].w_bits = 0u;
        corners[run].colour = run < 3 ? ARGB_RED : ARGB_GREEN;
        corners[run].specular = 0u;
        corners[run].s_bits = 0u;
        corners[run].t_bits = 0u;
    }
    for (run = 0; run < 3; ++run) {
        rs_zero_raster(&raster);
        raster.cull_back = run != 1 ? 1ul : 0ul;
        raster.cull_front = run == 1 ? 1ul : 0ul;
        raster.front_ccw = run == 2 ? 1ul : 0ul;
        lstrcpyA(prefix, name);
        lstrcatA(prefix, runs[run]);
        if (!rs_draw_corners(prefix, &raster, 0u, 0, 0, corners, 6u)) {
            return;
        }
        red[run] = rs_drawn_at(8u, ARGB_RED);
        green[run] = rs_drawn_at(40u, ARGB_GREEN);
        rs_write(rs_key(key, prefix, "Drawn"),
                 red[run] ? (green[run] ? "both" : "screen-cw") :
                 (green[run] ? "screen-ccw" : "neither"));
    }
    if (red[0] != green[0] && red[1] == green[0] && green[1] == red[0] &&
        red[2] == green[0] && green[2] == red[0]) {
        /* Culling back faces with a clockwise front kept the clockwise
         * triangle: the hardware's clockwise is the screen's (y down). */
        rs_write("CullScreenWindingIsHardware", red[0] ? "same" :
                 "mirrored");
        rs_write(rs_key(key, name, "Result"), "PASS");
        return;
    }
    rs_write(rs_key(key, name, "Result"), "MISMATCH");
    rs_failed = 1u;
}

/* Which texel column a pixel shows: the gradient texture's texel n is n at
 * 16 bpp and 2n | (2n + 1) << 16 at 32 bpp, and n's low five bits are the
 * column. */
static DWORD rs_texel_column(DWORD pixel)
{
    if (rs_bpp == 2u) {
        return pixel & (TEX_WIDTH - 1u);
    }
    return ((pixel & 0xfffful) / 2ul) & (TEX_WIDTH - 1u);
}

/*
 * Perspective: the 32 x 16 texture across a 64 x 16 quad, nearest, with
 * 1/W 1 at the left edge and 0.25 at the right. Perspective-correct, pixel
 * x's centre samples s = a * 0.25 / ((1 - a) + a * 0.25), a = (x + 0.5) /
 * 64: texel column floor(32 s). Only the column is judged: a screen
 * rectangle with 1/W changing across it is not the image of a flat
 * rectangle, t/W at its corners (0, 0, 1, 0.25) is not one plane, so each
 * triangle interpolates its own t, while s/W (0, 0.25, 0, 0.25) is one
 * plane. Pixels within 0.08 of a column boundary are not judged; Affine
 * counts how many judged pixels an affine mapping would also get right, so
 * a pass is not one by accident.
 */
static void rs_perspective_test(void)
{
    static const char name[] = "Perspective";
    struct v9x_rv280_3d_texture texture;
    struct rs_corner left_side;
    struct rs_corner right_side;
    struct rs_corner corners[6];
    const unsigned char *actual;
    char key[64];
    DWORD x;
    DWORD y;
    DWORD judged = 0u;
    DWORD wrong = 0u;
    DWORD affine_right = 0u;
    DWORD outside_changed = 0u;

    texture.fb_gpu_base = rs_fb_gpu_base;
    texture.fb_bytes = TEST_FB;
    texture.offset = TEX_OFFSET;
    texture.pitch_bytes = TEX_WIDTH * rs_bpp;
    texture.bytes_per_pixel = rs_bpp;
    texture.format = V9X_RV280_TEXFMT_DEFAULT;
    texture.width = TEX_WIDTH;
    texture.height = TEX_HEIGHT;
    texture.linear = 0ul;
    texture.clamp = 1ul;
    left_side.w_bits = rs_ratio_bits(1l, 1l);
    left_side.colour = ARGB_RED;
    left_side.specular = 0u;
    left_side.s_bits = rs_ratio_bits(0l, 1l);
    right_side = left_side;
    right_side.w_bits = rs_ratio_bits(1l, 4l);
    right_side.s_bits = rs_ratio_bits(1l, 1l);
    rs_quad_corners(corners, 0u, TEST_WIDTH, &left_side, &right_side);
    if (!rs_draw_corners(name, 0, V9X_RV280_LAYOUT_W | V9X_RV280_LAYOUT_ST,
                         &texture, 0, corners, 6u)) {
        return;
    }
    rs_save_block('P');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);
            /* With a = (2x + 1) / 128, u = 32 s = 32 (2x + 1) / (509 -
             * 6x): integers, since a float-to-integer conversion would
             * need the C runtime this tool does not link. */
            long num = 32l * (2l * (long)x + 1l);
            long den = 509l - 6l * (long)x;
            long ix = num / den;
            long rem = num % den;

            if (y >= TEX_HEIGHT) {
                if (pixel != rs_pixel(rs_model, x, y)) {
                    ++outside_changed;
                }
                continue;
            }
            if (rem * 100l < den * 8l || rem * 100l > den * 92l) {
                continue;
            }
            ++judged;
            if (rs_texel_column(pixel) != (DWORD)ix) {
                ++wrong;
            }
            /* Affine: u = 32 a = (2x + 1) / 4. */
            if ((2l * (long)x + 1l) / 4l == ix) {
                ++affine_right;
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "Judged"), judged);
    rs_write_decimal(rs_key(key, name, "Wrong"), wrong);
    rs_write_decimal(rs_key(key, name, "AffineWouldMatch"), affine_right);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (judged == 0u || wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/*
 * Fog: a white quad at columns 8-39, fog toward blue from the specular
 * alpha, specular itself off. Flat: alpha 80h at every corner, so each
 * channel within a step of white * 128/255 + blue * 127/255. Gouraud:
 * alpha FFh on the left edge and 0 on the right, the factor interpolated at
 * the pixel centre; within a step at 16 bpp, 4 at 32.
 */
static void rs_fog_test(const char *name, char letter, int gouraud)
{
    struct v9x_rv280_3d_raster raster;
    struct rs_corner left_side;
    struct rs_corner right_side;
    struct rs_corner corners[6];
    const unsigned char *actual;
    char key[64];
    long values[3];
    long maxima[3];
    long worst = 0l;
    long tolerance = gouraud && rs_bpp == 4u ? 4l : 1l;
    DWORD x;
    DWORD y;
    DWORD wrong = 0u;
    DWORD outside_changed = 0u;
    int channel;

    rs_zero_raster(&raster);
    raster.fog_enable = 1ul;
    raster.fog_colour = 0x000000fful;
    left_side.w_bits = 0u;
    left_side.colour = 0xffffffful;
    left_side.specular = gouraud ? 0xff000000ul : 0x80000000ul;
    left_side.s_bits = 0u;
    right_side = left_side;
    right_side.specular = gouraud ? 0x00000000ul : 0x80000000ul;
    rs_quad_corners(corners, 8u, 32u, &left_side, &right_side);
    if (!rs_draw_corners(name, &raster, V9X_RV280_LAYOUT_SPECULAR, 0,
                         gouraud, corners, 6u)) {
        return;
    }
    rs_save_block(letter);
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    rs_write_hex(rs_key(key, name, "PixelAt20"), rs_pixel(actual, 20u, 4u));
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);
            /* The fog factor, out of 255 * 64, at the pixel centre. */
            long factor = gouraud ?
                255l * (64l - (2l * ((long)x - 8l) + 1l)) :
                128l * 64l;

            if (y >= TEX_HEIGHT || x < 8u || x >= 40u) {
                if (pixel != rs_pixel(rs_model, x, y)) {
                    ++outside_changed;
                }
                continue;
            }
            rs_channels(pixel, values, maxima);
            for (channel = 0; channel < 3; ++channel) {
                long fogged = channel == 2 ? maxima[2] : 0l;
                long expected = (maxima[channel] * factor +
                                 fogged * (255l * 64l - factor) +
                                 255l * 32l) / (255l * 64l);
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
    rs_write_decimal(rs_key(key, name, "InsideWrong"), wrong);
    rs_write_decimal(rs_key(key, name, "WorstChannelError"), (DWORD)worst);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/*
 * Specular: diffuse 404040h plus specular 202020h with SPECULAR_ENABLE,
 * flat, columns 8-39: each channel within a step of 606060h. The specular
 * alpha is FFh, no fog.
 */
static void rs_specular_test(void)
{
    static const char name[] = "Specular";
    struct v9x_rv280_3d_raster raster;
    struct rs_corner side;
    struct rs_corner corners[6];
    const unsigned char *actual;
    char key[64];
    long values[3];
    long maxima[3];
    long worst = 0l;
    DWORD x;
    DWORD y;
    DWORD wrong = 0u;
    DWORD outside_changed = 0u;
    int channel;

    rs_zero_raster(&raster);
    raster.specular_enable = 1ul;
    side.w_bits = 0u;
    side.colour = 0xff404040ul;
    side.specular = 0xff202020ul;
    side.s_bits = 0u;
    rs_quad_corners(corners, 8u, 32u, &side, &side);
    if (!rs_draw_corners(name, &raster, V9X_RV280_LAYOUT_SPECULAR, 0, 0,
                         corners, 6u)) {
        return;
    }
    rs_save_block('S');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    rs_write_hex(rs_key(key, name, "PixelAt20"), rs_pixel(actual, 20u, 4u));
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);

            if (y >= TEX_HEIGHT || x < 8u || x >= 40u) {
                if (pixel != rs_pixel(rs_model, x, y)) {
                    ++outside_changed;
                }
                continue;
            }
            rs_channels(pixel, values, maxima);
            for (channel = 0; channel < 3; ++channel) {
                long expected = (maxima[channel] * 0x60l + 127l) / 255l;
                long error = rs_abs(values[channel] - expected);

                if (error > worst) {
                    worst = error;
                }
                if (error > 1l) {
                    ++wrong;
                    break;
                }
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "InsideWrong"), wrong);
    rs_write_decimal(rs_key(key, name, "WorstChannelError"), (DWORD)worst);
    rs_write_decimal(rs_key(key, name, "OutsideChanged"), outside_changed);
    if (wrong != 0u || outside_changed != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/*
 * WideZ: V9XGLP's GeomScan on M-HNBJNV - a 360-pixel OpenGL target, its Z
 * buffer's pitch 384 pixels, drawn green over the whole window at z 0.5,
 * LESS, after a depth clear - came out black in a band near x 56-71. Here
 * the same shape: a 360 x 32 target at a 384-pixel pitch, a Z buffer of
 * 768-byte rows on a 4 KiB boundary set to FFFFh, one green quad over it
 * all at z 0.5. two_d: the Z buffer set by the 2D engine over the pitch
 * and all 32 rows, as eng_rv280.c's depth fill does; else by the CPU.
 * Rows 0, 8, 16 and 24 read back: every pixel green.
 */
#define RS_WIDE_WIDTH 360ul
#define RS_WIDE_PITCH_PIXELS 384ul
#define RS_WIDE_ROWS 32ul

static void rs_wide_z_test(const char *name, int two_d)
{
    static const int corner_x[6] = { 0, 1, 0, 1, 1, 0 };
    static const int corner_y[6] = { 0, 0, 1, 0, 1, 1 };
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_raster raster;
    struct v9x_rv280_3d_vertex vertices[6];
    struct v9x_rv280_3d_list list;
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt init;
    struct v9x_rs690_blt blt;
    char key[64];
    DWORD pitch = RS_WIDE_PITCH_PIXELS * rs_bpp;
    DWORD z_pitch = RS_WIDE_PITCH_PIXELS * 2ul;
    DWORD green = rs_store(ARGB_GREEN);
    DWORD wrong = 0u;
    DWORD first = 0xfffffffful;
    DWORD row;
    DWORD x;
    int index;

    rs_zero_raster(&raster);
    raster.keep_x = 1ul;
    raster.z_enable = 1ul;
    raster.z_offset = ZBUF_OFFSET;
    raster.z_pitch_bytes = z_pitch;
    raster.z_func = 1ul;
    raster.z_write = 1ul;
    target.fb_gpu_base = rs_fb_gpu_base;
    target.fb_bytes = TEST_FB;
    target.offset = TEST_BASE;
    target.pitch_bytes = pitch;
    target.bytes_per_pixel = rs_bpp;
    target.width = RS_WIDE_WIDTH;
    target.height = RS_WIDE_ROWS;
    target.raster = &raster;
    target.layout = V9X_RV280_LAYOUT_Z;

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, 0u, pitch * RS_WIDE_ROWS / 4u);
    if (two_d) {
        if (v9x_rs690_build_init(rs_read_register(V9X_RS690_DP_DATATYPE),
                                 &init) != V9X_STATUS_OK) {
            rs_write(rs_key(key, name, "Result"), "INIT-REFUSED");
            rs_failed = 1u;
            return;
        }
        fill.fb_gpu_base = rs_fb_gpu_base;
        fill.fb_bytes = TEST_FB;
        fill.target_offset = ZBUF_OFFSET;
        fill.pitch_bytes = z_pitch;
        fill.bytes_per_pixel = 2u;
        fill.left = 0u;
        fill.top = 0u;
        fill.width = RS_WIDE_PITCH_PIXELS;
        fill.height = RS_WIDE_ROWS;
        fill.color = 0xfffful;
        if (v9x_rs690_build_fill(&fill, &blt) != V9X_STATUS_OK) {
            rs_write(rs_key(key, name, "Result"), "FILL-REFUSED");
            rs_failed = 1u;
            return;
        }
        rs_add(RS_OP_LFB_FILL32, ZBUF_OFFSET, 0u, z_pitch * RS_WIDE_ROWS / 4u);
        rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
        rs_add_blt(&init);
        rs_add_blt(&blt);
    } else {
        rs_add(RS_OP_LFB_FILL32, ZBUF_OFFSET, 0xfffffffful,
               z_pitch * RS_WIDE_ROWS / 4u);
    }
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rv280_build_3d_target(&target, 0, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return;
    }
    rs_add_list(&list);
    for (index = 0; index < 6; ++index) {
        vertices[index].x_bits =
            rs_float_bits(corner_x[index] ? (long)RS_WIDE_WIDTH : 0l);
        vertices[index].y_bits =
            rs_float_bits(corner_y[index] ? (long)RS_WIDE_ROWS : 0l);
        vertices[index].z_bits = rs_ratio_bits(1l, 2l);
        vertices[index].w_bits = 0u;
        vertices[index].colour = ARGB_GREEN;
        vertices[index].specular = 0u;
        vertices[index].s_bits = 0u;
        vertices[index].t_bits = 0u;
    }
    if (v9x_rv280_build_3d_triangles(vertices, 6u, 0, V9X_RV280_LAYOUT_Z,
                                     &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return;
    }
    rs_add_list(&list);
    (void)v9x_rv280_build_3d_finish(1, &list);
    rs_add_list(&list);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return;
    }
    for (row = 0u; row < RS_WIDE_ROWS; row += 8u) {
        char row_key[32];
        DWORD row_wrong = 0u;

        rs_begin();
        rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE + row * pitch, 0u,
               RS_WIDE_WIDTH * rs_bpp / 4u);
        if (!rs_run()) {
            rs_write(rs_key(key, name, "Result"), "READ-NOT-RUN");
            rs_failed = 1u;
            return;
        }
        for (x = 0u; x < RS_WIDE_WIDTH; ++x) {
            const unsigned char *at =
                (const unsigned char *)rs_result_buffer.block + x * rs_bpp;
            DWORD pixel = rs_bpp == 2u
                ? ((DWORD)at[0] | ((DWORD)at[1] << 8))
                : ((DWORD)at[0] | ((DWORD)at[1] << 8) |
                   ((DWORD)at[2] << 16));

            if (pixel != green) {
                if (row_wrong == 0u) {
                    wsprintfA(row_key, "Row%luFirstX", row);
                    rs_write_decimal(rs_key(key, name, row_key), x);
                }
                ++row_wrong;
                if (first == 0xfffffffful) {
                    first = row * RS_WIDE_WIDTH + x;
                }
            }
        }
        wsprintfA(row_key, "Row%luWrong", row);
        rs_write_decimal(rs_key(key, name, row_key), row_wrong);
        wrong += row_wrong;
    }
    if (wrong != 0u) {
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
    {
        int unit = rs_z_test("ZUnit", 1l);
        int buffer = rs_z_test("ZBuffer", 65535l);

        rs_write("ZRange", unit ? (buffer ? "both" : "0-1") :
                           (buffer ? "0-65535" : "neither"));
        if (!unit && !buffer) {
            rs_failed = 1u;
        }
    }
    rs_blend_test();
    rs_alpha_test();
    rs_cull_test();
    rs_perspective_test();
    rs_fog_test("FogFlat", 'O', 0);
    rs_fog_test("FogGouraud", 'R', 1);
    rs_specular_test();
    rs_wide_z_test("WideZCpu", 0);
    rs_wide_z_test("WideZ2D", 1);
    rs_pitch_test("Pitch8", 'H', 8u);
    rs_pitch_test("Pitch32", 'J', 32u);
    rs_scissor_test();
    rs_channel_mask_test("MaskGreen", 'K', V9X_RV280_CHANNEL_GREEN);
    rs_channel_mask_test("MaskRedBlue", 'L',
                         V9X_RV280_CHANNEL_RED | V9X_RV280_CHANNEL_BLUE);
    rs_fill_after_test();

    rs_write_hex("RbbmStatusAtEnd", rs_read_register(V9X_RS690_RBBM_STATUS));
    rs_write("Result", rs_failed == 0u ? "PASS" : "FAIL");
    rs_close(rs_failed == 0u ? 0u : 1u);
}
