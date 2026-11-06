/*
 * V9XRS3D.EXE: the ATI Radeon X1200 IGP (RS690) 3D engine draw probe.
 *
 * Phase 5 of docs\plans\ati-rs690-x1200.md, first step: does the R300-class
 * 3D core, programmed by direct register writes with exactly the values
 * src\chipsets\ati\rs690\rs690_3d.c builds, draw colour-interpolated
 * primitives where a model says, with no CP and no microcode? Publishes
 * C:\V9XDIAG\V9XRS3D.TXT and each test's read-back surface as
 * C:\V9XDIAG\R3<x>.BIN. Runs beside RS3D.VXD, which executes the op lists
 * this side builds. The 2D write probe (rs690_2d_win32.c) is the model.
 *
 * It refuses to touch the engine unless CP_CSQ_CNTL says both CP queues
 * are off, which is how VBE leaves the chip, and takes the frame buffer's
 * GPU address from HDP_FB_LOCATION. It writes the 3D init list once, then
 * draws only on a 4 KiB test surface at frame buffer offset 16 MiB, off the
 * desktop and above the 16 MiB the driver uses. Every run of register
 * writes waits for that many free FIFO entries first. Every test prefills
 * the surface, waits for idle, writes the colour-buffer state, the draw and
 * the finish (3D cache flush, WAIT_UNTIL 3D idle-clean), waits for idle,
 * reads the surface back and compares it with an integer model.
 *
 * The surface is 64 pixels wide at the desktop's depth (16 or 32 bpp; 8 is
 * refused, the colour buffer has no palette format); its height fills one
 * 4 KiB read block, 32 or 16 rows. Tests:
 *   Quad      - a quad list rectangle, flat magenta, axis-aligned with
 *               integer corners: the covered pixels are exact under either
 *               pixel-centre convention, so the whole block must match.
 *   Triangle  - a flat green triangle with one slanted edge. Pixels whose
 *               centre lies within one pixel of the slanted edge are not
 *               judged (counted as BandDrawn of BandPixels); every other
 *               pixel must be green inside and untouched outside.
 *   Gouraud   - the same triangle with red, green and blue corners. Inside
 *               pixels are compared per channel with barycentric
 *               interpolation at the pixel centre, within a tolerance
 *               (MaxError is published in channel units).
 *   TexDecal  - a 32 x 16 gradient texture in the surface's format drawn
 *               once at 1:1, nearest, clamped, unmodulated: exact.
 *   TexWrap   - the same texture twice across 64 pixels (s 0 to 2),
 *               wrapping: exact.
 *   TexModulate - TexDecal's mapping times a flat vertex colour; each
 *               channel within one step of texel * colour / 255.
 *   TexBilinear - half the texture magnified 2x, bilinear, clamped; each
 *               channel within one step of the exact filter plus 1/32 of
 *               the four texels' spread (the filter's weights are coarse).
 *   TlGouraud - Gouraud through the TL-vertex state (D3DTLVERTEX dwords,
 *               packed colours), as the HAL will draw.
 *   ZTest     - three overlapping quads at three depths, Z16 LESS with
 *               writes: byte-exact, and the Z values in order.
 *   Blend     - SRCALPHA / INVSRCALPHA over the prefill, within a step.
 *   AlphaTest - GREATER 128 keeps the alpha-200 quad, drops the alpha-100.
 *   Cull      - which winding CULL_FRONT and CULL_BACK each remove.
 *   Perspective - rhw 1 to 0.25 across a textured quad: perspective-
 *               correct texel columns.
 *   QuadAfterTex - Quad again with the colour-only state, so texturing,
 *               Z, blend, alpha test and cull turn off.
 *   FillAfter3D - a 2D engine fill from rs690_2d.c after all of that,
 *               byte-exact, so the 3D state left behind does not break the
 *               engine the driver's DirectDraw uses.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/rs690_2d.h"
#include "velocity9x/rs690_3d.h"

#include "../../src/chipsets/ati/rs690/rs690_2d.c"
#include "../../src/chipsets/ati/rs690/rs690_3d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define RS_PATH V9X_DIAG_PATH("V9XRS3D.TXT")
#define RS_MAGIC 0x44335352ul
#define RS_OP_MAX 512u
#define RS_BLOCK_DWORDS 1024u
#define RS_TIMEOUT 0xfffffffful

#define RS_OP_MMIO_WRITE32 1ul
#define RS_OP_MMIO_READ32 2ul
#define RS_OP_LFB_WRITE32 3ul
#define RS_OP_LFB_FILL32 5ul
#define RS_OP_LFB_GRADIENT 6ul
#define RS_OP_LFB_READ_BLOCK 7ul
#define RS_OP_WAIT_IDLE 8ul
#define RS_OP_WAIT_FIFO 9ul

#define RS_MAPPED 0x00000002ul
#define RS_RAN 0x00000004ul

/* xf86-video-ati 6.14.6 waits for a batch's length before writing it; 16
 * is a quarter of the FIFO. */
#define RS_FIFO_BATCH 16u

#define TEST_BASE 0x01000000ul
#define TEST_BYTES 4096ul
#define TEST_WIDTH 64ul
#define TEST_DWORDS (TEST_BYTES / 4ul)
#define TEST_FB 0x02000000ul
#define TEST_PREFILL 0xa5a5a5a5ul

/* The texture: 32 x 16 texels in the surface's format, rows packed, 64 KiB
 * past the surface. The VxD's gradient op fills it: dword i holds 2i and
 * 2i + 1 in its halves, so at 16 bpp texel n is n (blue = column, green =
 * row, red 0) and at 32 bpp texel n is 2n | (2n + 1) << 16, every texel
 * different from its neighbours. */
#define TEX_OFFSET (TEST_BASE + 0x10000ul)
#define TEX_WIDTH 32ul
#define TEX_HEIGHT 16ul

/* Modulate and bilinear tolerance: one step of the channel. */
#define RS_TEX_TOLERANCE 1l

/* Doubled coordinates make every pixel centre (2x + 1, 2y + 1) an integer. */
#define RS_COORD_SCALE 2l

/* Gouraud tolerance in channel units: 5- and 6-bit channels 1, 8-bit 4.
 * Wide enough for either pixel-centre convention on these gradients. */
#define RS_TOLERANCE_LOW 1l
#define RS_TOLERANCE_8BIT 4l

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

/* memset without the C runtime this tool does not link. */
static void rs_zero(void *memory, DWORD bytes)
{
    unsigned char *at = (unsigned char *)memory;

    while (bytes-- != 0u) {
        *at++ = 0u;
    }
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

static void rs_add_list(const struct v9x_rs690_3d_list *list)
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

    lstrcpyA(path, V9X_DIAG_PATH("R3x.BIN"));
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
    DWORD byte;

    for (byte = 0u; byte < rs_bpp; ++byte) {
        at[byte] = (unsigned char)(value >> (byte * 8u));
    }
}

/* A colour whose channels are 0 or 255, as the buffer stores it. */
static DWORD rs_encode(DWORD red, DWORD green, DWORD blue)
{
    if (rs_bpp == 2u) {
        return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
    }
    return 0xff000000ul | (red << 16) | (green << 8) | blue;
}

static void rs_model_prefill(void)
{
    DWORD index;

    for (index = 0u; index < TEST_BYTES; ++index) {
        rs_model[index] = (unsigned char)(TEST_PREFILL >> ((index & 3u) * 8u));
    }
}

/*
 * Prefill, wait idle, the colour-buffer state, the draw, the finish, wait
 * idle, read back. Returns non-zero with the surface in the result block;
 * otherwise publishes why and marks the probe failed.
 */
static int rs_draw(const char *name, DWORD primitive,
                   const struct v9x_rs690_3d_vertex *vertices, DWORD count)
{
    struct v9x_rs690_3d_target target;
    struct v9x_rs690_3d_list list;
    char key[64];
    DWORD before_index;
    DWORD after_index;

    target.fb_gpu_base = rs_fb_gpu_base;
    target.fb_bytes = TEST_FB;
    target.offset = TEST_BASE;
    target.pitch_bytes = rs_pitch;
    target.bytes_per_pixel = rs_bpp;
    target.width = TEST_WIDTH;
    target.height = rs_height;

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rs690_build_3d_target(&target, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TARGET-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    if (v9x_rs690_build_3d_draw(primitive, vertices, count, TEST_WIDTH,
                                rs_height, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
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
    rs_write_decimal(rs_key(key, name, "Ops"), rs_request_buffer.count);
    return 1;
}

/* Compares the whole read-back block with rs_model byte for byte and
 * publishes the count and the first difference. Returns the count. */
static DWORD rs_compare_model(const char *name)
{
    const unsigned char *actual = (const unsigned char *)rs_result_buffer.block;
    char key[64];
    DWORD index;
    DWORD mismatches = 0u;
    DWORD first = 0xfffffffful;

    for (index = 0u; index < TEST_BYTES; ++index) {
        if (actual[index] != rs_model[index]) {
            if (first == 0xfffffffful) {
                first = index;
            }
            ++mismatches;
        }
    }
    rs_write_decimal(rs_key(key, name, "MismatchBytes"), mismatches);
    if (first != 0xfffffffful) {
        char text[24];
        char number[12];
        DWORD x = (first % rs_pitch) / rs_bpp;
        DWORD y = first / rs_pitch;

        rs_decimal(text, x);
        lstrcatA(text, ",");
        rs_decimal(number, y);
        lstrcatA(text, number);
        rs_write(rs_key(key, name, "FirstMismatchXY"), text);
        rs_write_hex(rs_key(key, name, "FirstMismatchRead"),
                     rs_pixel(actual, x, y));
        rs_write_hex(rs_key(key, name, "FirstMismatchModel"),
                     rs_pixel(rs_model, x, y));
    }
    return mismatches;
}

static void rs_quad_test(const char *name, char letter)
{
    struct v9x_rs690_3d_vertex vertices[4];
    const unsigned char *actual;
    char key[64];
    DWORD colour;
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD mismatches;

    /* Corners in the order RadeonCompositeTile sends them: top-left,
     * bottom-left, bottom-right, top-right. */
    for (index = 0u; index < 4u; ++index) {
        vertices[index].red = 255u;
        vertices[index].green = 0u;
        vertices[index].blue = 255u;
        vertices[index].alpha = 255u;
    }
    vertices[0].x = 5u;
    vertices[0].y = 3u;
    vertices[1].x = 5u;
    vertices[1].y = 13u;
    vertices[2].x = 29u;
    vertices[2].y = 13u;
    vertices[3].x = 29u;
    vertices[3].y = 3u;
    if (!rs_draw(name, V9X_RS690_PRIM_QUADS, vertices, 4u)) {
        return;
    }
    rs_save_block(letter);

    colour = rs_encode(255u, 0u, 255u);
    rs_model_prefill();
    for (y = 3u; y < 13u; ++y) {
        for (x = 5u; x < 29u; ++x) {
            rs_set_pixel(rs_model, x, y, colour);
        }
    }
    actual = (const unsigned char *)rs_result_buffer.block;
    mismatches = rs_compare_model(name);
    rs_write_hex(rs_key(key, name, "PixelInside"),
                 rs_pixel(actual, 10u, 8u));
    rs_write_hex(rs_key(key, name, "PixelOutside"),
                 rs_pixel(actual, 40u, 8u));
    rs_write(rs_key(key, name, "Result"), mismatches == 0u ? "PASS" :
             "MISMATCH");
    if (mismatches != 0u) {
        rs_failed = 1u;
    }
}

/*
 * The test triangle: (4,2), (52,2), (4,14) - two edges on the axes, one
 * slanted - in doubled coordinates. The slanted edge's band is one pixel
 * either side; the axis edges are exact (no centre lies on them).
 */
static const long rs_tri_x[3] = { 8l, 104l, 8l };
static const long rs_tri_y[3] = { 4l, 4l, 28l };

/* Edge function of the edge from corner a to corner b at doubled point
 * (px, py); positive inside for this winding. */
static long rs_edge(int a, int b, long px, long py)
{
    return (rs_tri_x[b] - rs_tri_x[a]) * (py - rs_tri_y[a]) -
           (rs_tri_y[b] - rs_tri_y[a]) * (px - rs_tri_x[a]);
}

/* CLASS_INSIDE, CLASS_OUTSIDE or CLASS_BAND for pixel (x, y), and the three
 * barycentric weights at its centre. */
static int rs_classify(DWORD x, DWORD y, long weights[3])
{
    long px = RS_COORD_SCALE * (long)x + 1l;
    long py = RS_COORD_SCALE * (long)y + 1l;
    int inside = 1;
    int edge;

    for (edge = 0; edge < 3; ++edge) {
        int a = edge;
        int b = (edge + 1) % 3;
        long value = rs_edge(a, b, px, py);
        long dx = rs_tri_x[b] - rs_tri_x[a];
        long dy = rs_tri_y[b] - rs_tri_y[a];
        long band_squared = 0l;

        /* The weight of the corner opposite this edge. */
        weights[(edge + 2) % 3] = value;
        if (dx != 0l && dy != 0l) {
            /* Distance in pixels is value / (2 * |edge|) in doubled
             * coordinates; within one pixel is the band. */
            band_squared = 4l * (dx * dx + dy * dy);
        }
        if (value < 0l && value * value >= band_squared) {
            return CLASS_OUTSIDE;
        }
        if (value <= 0l || value * value < band_squared) {
            inside = 0;
        }
    }
    return inside ? CLASS_INSIDE : CLASS_BAND;
}

/* Channel value from a pixel: 0 red, 1 green, 2 blue; and its maximum. */
static DWORD rs_channel(DWORD pixel, int channel, DWORD *maximum)
{
    if (rs_bpp == 2u) {
        if (channel == 0) {
            *maximum = 31u;
            return (pixel >> 11) & 31u;
        }
        if (channel == 1) {
            *maximum = 63u;
            return (pixel >> 5) & 63u;
        }
        *maximum = 31u;
        return pixel & 31u;
    }
    *maximum = 255u;
    return (pixel >> (16 - channel * 8)) & 255u;
}

/*
 * Flat (gouraud zero) or Gouraud: corner 0 red, 1 green, 2 blue. Inside
 * pixels: flat must equal green exactly; Gouraud each channel within the
 * tolerance of the interpolated value. Outside pixels must be the prefill.
 * With tl, the same triangle through the TL-vertex state, colours packed
 * as D3DCOLORs.
 */
static int rs_draw_tl(const char *name, struct v9x_rs690_3d_state *state,
                      DWORD primitive, const DWORD *vertices, DWORD count,
                      DWORD prefill);

static void rs_triangle_test(const char *name, int gouraud, char letter,
                             int tl)
{
    struct v9x_rs690_3d_vertex vertices[3];
    struct v9x_rs690_3d_state state;
    DWORD tl_vertices[3 * 8];
    const unsigned char *actual;
    char key[64];
    DWORD x;
    DWORD y;
    DWORD inside_wrong = 0u;
    DWORD outside_wrong = 0u;
    DWORD inside_count = 0u;
    DWORD band_count = 0u;
    DWORD band_drawn = 0u;
    DWORD first_wrong = 0xfffffffful;
    long worst = 0l;
    long total;
    DWORD prefill_pixel;
    DWORD flat;
    int corner;

    for (corner = 0; corner < 3; ++corner) {
        vertices[corner].x = (DWORD)(rs_tri_x[corner] / RS_COORD_SCALE);
        vertices[corner].y = (DWORD)(rs_tri_y[corner] / RS_COORD_SCALE);
        vertices[corner].alpha = 255u;
        if (gouraud) {
            vertices[corner].red = corner == 0 ? 255u : 0u;
            vertices[corner].green = corner == 1 ? 255u : 0u;
            vertices[corner].blue = corner == 2 ? 255u : 0u;
        } else {
            vertices[corner].red = 0u;
            vertices[corner].green = 255u;
            vertices[corner].blue = 0u;
        }
        tl_vertices[corner * 8 + 0] =
            v9x_rs690_float_integer(vertices[corner].x);
        tl_vertices[corner * 8 + 1] =
            v9x_rs690_float_integer(vertices[corner].y);
        tl_vertices[corner * 8 + 2] = 0u;
        tl_vertices[corner * 8 + 3] = 0x3f800000ul;
        tl_vertices[corner * 8 + 4] = 0xff000000ul |
                                      (vertices[corner].red << 16) |
                                      (vertices[corner].green << 8) |
                                      vertices[corner].blue;
        tl_vertices[corner * 8 + 5] = 0u;
        tl_vertices[corner * 8 + 6] = 0u;
        tl_vertices[corner * 8 + 7] = 0u;
    }
    if (tl) {
        rs_zero(&state, sizeof(state));
        if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_TRIANGLES, tl_vertices,
                        3u, TEST_PREFILL)) {
            return;
        }
    } else if (!rs_draw(name, V9X_RS690_PRIM_TRIANGLES, vertices, 3u)) {
        return;
    }
    rs_save_block(letter);

    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    prefill_pixel = rs_pixel(rs_model, 0u, 0u);
    flat = rs_encode(0u, 255u, 0u);
    total = rs_edge(0, 1, rs_tri_x[2], rs_tri_y[2]);

    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            long weights[3];
            int kind = rs_classify(x, y, weights);
            DWORD pixel = rs_pixel(actual, x, y);
            int wrong = 0;

            if (kind == CLASS_BAND) {
                ++band_count;
                if (pixel != prefill_pixel) {
                    ++band_drawn;
                }
                continue;
            }
            if (kind == CLASS_OUTSIDE) {
                if (pixel != prefill_pixel) {
                    ++outside_wrong;
                    wrong = 1;
                }
            } else if (!gouraud) {
                ++inside_count;
                if (pixel != flat) {
                    ++inside_wrong;
                    wrong = 1;
                }
            } else {
                int channel;

                ++inside_count;
                for (channel = 0; channel < 3; ++channel) {
                    DWORD maximum;
                    DWORD value = rs_channel(pixel, channel, &maximum);
                    long tolerance = maximum == 255u ? RS_TOLERANCE_8BIT :
                                     RS_TOLERANCE_LOW;
                    /* Corner `channel` carries that channel at full scale,
                     * so the expected value is maximum * weight / total.
                     * Error in units of 1/total of a channel step. */
                    long error = (long)value * total -
                                 (long)maximum * weights[channel];

                    if (error < 0l) {
                        error = -error;
                    }
                    if (error > worst) {
                        worst = error;
                    }
                    if (error > tolerance * total) {
                        wrong = 1;
                    }
                }
                if (wrong) {
                    ++inside_wrong;
                }
            }
            if (wrong && first_wrong == 0xfffffffful) {
                first_wrong = y * TEST_WIDTH + x;
            }
        }
    }

    rs_write_decimal(rs_key(key, name, "InsidePixels"), inside_count);
    rs_write_decimal(rs_key(key, name, "InsideWrong"), inside_wrong);
    rs_write_decimal(rs_key(key, name, "OutsideWrong"), outside_wrong);
    rs_write_decimal(rs_key(key, name, "BandPixels"), band_count);
    rs_write_decimal(rs_key(key, name, "BandDrawn"), band_drawn);
    if (gouraud) {
        /* Hundredths of a channel step. */
        rs_write_decimal(rs_key(key, name, "MaxErrorCenti"),
                         (DWORD)((worst * 100l + total / 2l) / total));
        rs_write_hex(rs_key(key, name, "PixelNearRed"),
                     rs_pixel(actual, 6u, 3u));
        rs_write_hex(rs_key(key, name, "PixelNearGreen"),
                     rs_pixel(actual, 46u, 3u));
        rs_write_hex(rs_key(key, name, "PixelNearBlue"),
                     rs_pixel(actual, 5u, 12u));
        rs_write_hex(rs_key(key, name, "PixelMiddle"),
                     rs_pixel(actual, 18u, 6u));
    } else {
        rs_write_hex(rs_key(key, name, "PixelInside"),
                     rs_pixel(actual, 10u, 4u));
    }
    if (first_wrong != 0xfffffffful) {
        char text[24];
        char number[12];

        rs_decimal(text, first_wrong % TEST_WIDTH);
        lstrcatA(text, ",");
        rs_decimal(number, first_wrong / TEST_WIDTH);
        lstrcatA(text, number);
        rs_write(rs_key(key, name, "FirstWrongXY"), text);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, first_wrong % TEST_WIDTH,
                              first_wrong / TEST_WIDTH));
    }
    if (inside_wrong != 0u || outside_wrong != 0u || inside_count == 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/* Texel (x, y) of the gradient texture, as the surface stores a pixel. */
static DWORD rs_texel(DWORD x, DWORD y)
{
    DWORD n = y * TEX_WIDTH + x;

    if (rs_bpp == 2u) {
        return n & 0xffffu;
    }
    return ((2u * n) & 0xffffu) | (((2u * n + 1u) & 0xffffu) << 16);
}

/* A pixel's channels - red, green, blue, and alpha at 32 bpp - with each
 * channel's maximum. Returns the channel count. */
static int rs_channels(DWORD pixel, DWORD values[4], DWORD maxima[4])
{
    int channel;

    for (channel = 0; channel < 3; ++channel) {
        values[channel] = rs_channel(pixel, channel, &maxima[channel]);
    }
    if (rs_bpp == 2u) {
        return 3;
    }
    values[3] = pixel >> 24;
    maxima[3] = 255u;
    return 4;
}

static void rs_tex_vertex(struct v9x_rs690_3d_tex_vertex *vertex, DWORD x,
                          DWORD y, DWORD s, DWORD t, DWORD red, DWORD green,
                          DWORD blue)
{
    vertex->x = x;
    vertex->y = y;
    vertex->red = red;
    vertex->green = green;
    vertex->blue = blue;
    vertex->alpha = 255u;
    vertex->s = s;
    vertex->t = t;
}

/* A rectangle as a quad, corners in RadeonCompositeTile's order, s and t
 * running from 0 at the top-left to the given bit patterns. */
static void rs_tex_quad(struct v9x_rs690_3d_tex_vertex vertices[4],
                        DWORD left, DWORD top, DWORD right, DWORD bottom,
                        DWORD s_right, DWORD t_bottom, DWORD red,
                        DWORD green, DWORD blue)
{
    rs_tex_vertex(&vertices[0], left, top, 0u, 0u, red, green, blue);
    rs_tex_vertex(&vertices[1], left, bottom, 0u, t_bottom, red, green, blue);
    rs_tex_vertex(&vertices[2], right, bottom, s_right, t_bottom, red, green,
                  blue);
    rs_tex_vertex(&vertices[3], right, top, s_right, 0u, red, green, blue);
}

/*
 * Prefill the surface, write the gradient texture, wait idle, the
 * colour-buffer state, the texture state, the draw, the finish, wait idle,
 * read back - all in one run. As rs_draw otherwise.
 */
static int rs_draw_textured(const char *name, DWORD filter, DWORD address,
                            DWORD combine,
                            const struct v9x_rs690_3d_tex_vertex *vertices,
                            DWORD count)
{
    struct v9x_rs690_3d_target target;
    struct v9x_rs690_3d_texture texture;
    struct v9x_rs690_3d_list list;
    char key[64];
    DWORD before_index;
    DWORD after_index;

    target.fb_gpu_base = rs_fb_gpu_base;
    target.fb_bytes = TEST_FB;
    target.offset = TEST_BASE;
    target.pitch_bytes = rs_pitch;
    target.bytes_per_pixel = rs_bpp;
    target.width = TEST_WIDTH;
    target.height = rs_height;

    rs_zero(&texture, sizeof(texture));
    texture.offset = TEX_OFFSET;
    texture.pitch_bytes = TEX_WIDTH * rs_bpp;
    texture.format = rs_bpp == 2u ? V9X_RS690_TEXFMT_RGB565 :
                     V9X_RS690_TEXFMT_ARGB8888;
    texture.width = TEX_WIDTH;
    texture.height = TEX_HEIGHT;
    texture.filter = filter;
    texture.address = address;
    texture.combine = combine;

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rs_add(RS_OP_LFB_GRADIENT, TEX_OFFSET, 0u,
           TEX_WIDTH * TEX_HEIGHT * rs_bpp / 4u);
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rs690_build_3d_target(&target, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TARGET-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    if (v9x_rs690_build_3d_texture(rs_fb_gpu_base, TEST_FB, &texture,
                                   &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TEXTURE-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    if (v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_QUADS, vertices,
                                         count, TEST_WIDTH, rs_height,
                                         &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
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
    rs_write_decimal(rs_key(key, name, "Ops"), rs_request_buffer.count);
    return 1;
}

/*
 * Nearest-filtered, unmodulated: a quad from (left, 0) to (right, 16) with
 * s from 0 to s_right and t from 0 to 1. Each pixel centre lands in the
 * middle of a texel, so the result is exact: pixel (x, y) is texel
 * ((x - left) mod 32, y), every other pixel the prefill. TexDecal maps the
 * texture once, clamped; TexWrap twice across 64 pixels, wrapping.
 */
static void rs_tex_exact_test(const char *name, char letter, DWORD address,
                              DWORD left, DWORD right, DWORD s_right)
{
    struct v9x_rs690_3d_tex_vertex vertices[4];
    char key[64];
    DWORD x;
    DWORD y;
    DWORD mismatches;

    rs_tex_quad(vertices, left, 0u, right, TEX_HEIGHT, s_right,
                v9x_rs690_float_integer(1u), 255u, 255u, 255u);
    if (!rs_draw_textured(name, V9X_RS690_TEXFILTER_NEAREST, address,
                          V9X_RS690_TEXCOMBINE_REPLACE, vertices, 4u)) {
        return;
    }
    rs_save_block(letter);

    rs_model_prefill();
    for (y = 0u; y < TEX_HEIGHT; ++y) {
        for (x = left; x < right; ++x) {
            rs_set_pixel(rs_model, x, y, rs_texel((x - left) % TEX_WIDTH, y));
        }
    }
    mismatches = rs_compare_model(name);
    rs_write_hex(rs_key(key, name, "PixelFirstTexel"),
                 rs_pixel((const unsigned char *)rs_result_buffer.block,
                          left, 0u));
    rs_write_hex(rs_key(key, name, "PixelLastTexel"),
                 rs_pixel((const unsigned char *)rs_result_buffer.block,
                          right - 1u, TEX_HEIGHT - 1u));
    rs_write(rs_key(key, name, "Result"), mismatches == 0u ? "PASS" :
             "MISMATCH");
    if (mismatches != 0u) {
        rs_failed = 1u;
    }
}

/*
 * Judges every pixel of a filtered or modulated test: inside the quad
 * (x < right, y < 16) each channel against expected[] in 1/scale of a
 * step, within allowed[] (same units); outside, the prefill. Both are
 * filled per pixel by the caller's model through the callback.
 */
typedef void (*rs_tex_model)(DWORD x, DWORD y, long expected[4],
                             long allowed[4]);

static void rs_tex_judge(const char *name, DWORD left, DWORD right,
                         long scale, rs_tex_model model)
{
    const unsigned char *actual = (const unsigned char *)rs_result_buffer.block;
    char key[64];
    DWORD prefill_pixel;
    DWORD x;
    DWORD y;
    DWORD inside_wrong = 0u;
    DWORD outside_wrong = 0u;
    DWORD first_wrong = 0xfffffffful;
    long worst = 0l;

    rs_model_prefill();
    prefill_pixel = rs_pixel(rs_model, 0u, 0u);
    for (y = 0u; y < rs_height; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);
            int wrong = 0;

            if (x < left || x >= right || y >= TEX_HEIGHT) {
                if (pixel != prefill_pixel) {
                    ++outside_wrong;
                    wrong = 1;
                }
            } else {
                DWORD values[4];
                DWORD maxima[4];
                long expected[4];
                long allowed[4];
                int channels = rs_channels(pixel, values, maxima);
                int channel;

                model(x, y, expected, allowed);
                for (channel = 0; channel < channels; ++channel) {
                    long error = (long)values[channel] * scale -
                                 expected[channel];

                    if (error < 0l) {
                        error = -error;
                    }
                    if (error > worst) {
                        worst = error;
                    }
                    if (error > allowed[channel]) {
                        wrong = 1;
                    }
                }
                if (wrong) {
                    ++inside_wrong;
                }
            }
            if (wrong && first_wrong == 0xfffffffful) {
                first_wrong = y * TEST_WIDTH + x;
            }
        }
    }

    rs_write_decimal(rs_key(key, name, "InsideWrong"), inside_wrong);
    rs_write_decimal(rs_key(key, name, "OutsideWrong"), outside_wrong);
    rs_write_decimal(rs_key(key, name, "MaxErrorCenti"),
                     (DWORD)((worst * 100l + scale / 2l) / scale));
    if (first_wrong != 0xfffffffful) {
        char text[24];
        char number[12];

        rs_decimal(text, first_wrong % TEST_WIDTH);
        lstrcatA(text, ",");
        rs_decimal(number, first_wrong / TEST_WIDTH);
        lstrcatA(text, number);
        rs_write(rs_key(key, name, "FirstWrongXY"), text);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, first_wrong % TEST_WIDTH,
                              first_wrong / TEST_WIDTH));
    }
    if (inside_wrong != 0u || outside_wrong != 0u) {
        rs_write(rs_key(key, name, "Result"), "MISMATCH");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"), "PASS");
}

/* TexModulate's vertex colour; alpha 255. */
#define RS_MOD_RED 160u
#define RS_MOD_GREEN 128u
#define RS_MOD_BLUE 96u
#define RS_MOD_LEFT 4u

/* texel channel * colour / 255, in 1/255 of a step; one step allowed. */
static void rs_modulate_model(DWORD x, DWORD y, long expected[4],
                              long allowed[4])
{
    static const DWORD colour[4] = {
        RS_MOD_RED, RS_MOD_GREEN, RS_MOD_BLUE, 255u
    };
    DWORD values[4];
    DWORD maxima[4];
    int channels = rs_channels(rs_texel(x - RS_MOD_LEFT, y), values, maxima);
    int channel;

    for (channel = 0; channel < channels; ++channel) {
        expected[channel] = (long)(values[channel] * colour[channel]);
        allowed[channel] = RS_TEX_TOLERANCE * 255l;
    }
}

/*
 * TexModulate: TexDecal's mapping with a flat vertex colour multiplied in.
 * Each channel must be within one step of texel * colour / 255.
 */
static void rs_tex_modulate_test(void)
{
    static const char name[] = "TexModulate";
    struct v9x_rs690_3d_tex_vertex vertices[4];

    rs_tex_quad(vertices, RS_MOD_LEFT, 0u, RS_MOD_LEFT + TEX_WIDTH,
                TEX_HEIGHT, v9x_rs690_float_integer(1u),
                v9x_rs690_float_integer(1u), RS_MOD_RED, RS_MOD_GREEN,
                RS_MOD_BLUE);
    if (!rs_draw_textured(name, V9X_RS690_TEXFILTER_NEAREST,
                          V9X_RS690_TEXADDR_CLAMP,
                          V9X_RS690_TEXCOMBINE_MODULATE, vertices, 4u)) {
        return;
    }
    rs_save_block('M');
    rs_tex_judge(name, RS_MOD_LEFT, RS_MOD_LEFT + TEX_WIDTH, 255l,
                 rs_modulate_model);
}

/* Clamp a texel index to 0..limit - 1. */
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

/*
 * Bilinear at twice the size: pixel x samples texel coordinate
 * (x + 0.5) / 2 - 0.5 = x / 2 - 0.25, in quarters 2x - 1; the same for y.
 * The four texels around it are weighted (4 - f)(4 - g), f(4 - g), ... out
 * of 16, clamped at the edges. Expected in 1/16 of a step.
 *
 * Allowed: one step, plus 1/32 of the spread of the four texels. The first
 * run (2026-11-05, 32 bpp) found the filter's weights up to 0.018 short of
 * the quarter on a 192-step edge, 3.5 steps; a wrong texel is off by about
 * half the spread, far outside this.
 */
#define RS_BILINEAR_SPREAD_SHARE 32l

static void rs_bilinear_model(DWORD x, DWORD y, long expected[4],
                              long allowed[4])
{
    DWORD lowest[4] = { 255u, 255u, 255u, 255u };
    DWORD highest[4] = { 0u, 0u, 0u, 0u };
    long u4 = 2l * (long)x - 1l + 4l;
    long v4 = 2l * (long)y - 1l + 4l;
    long fu = u4 % 4l;
    long fv = v4 % 4l;
    long u0 = u4 / 4l - 1l;
    long v0 = v4 / 4l - 1l;
    DWORD texels[4];
    long weights[4];
    int corner;
    int channel;

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

    for (channel = 0; channel < 4; ++channel) {
        expected[channel] = 0l;
    }
    for (corner = 0; corner < 4; ++corner) {
        DWORD values[4];
        DWORD maxima[4];
        int channels = rs_channels(texels[corner], values, maxima);

        for (channel = 0; channel < channels; ++channel) {
            expected[channel] += weights[corner] * (long)values[channel];
            if (values[channel] < lowest[channel]) {
                lowest[channel] = values[channel];
            }
            if (values[channel] > highest[channel]) {
                highest[channel] = values[channel];
            }
        }
    }
    for (channel = 0; channel < 4; ++channel) {
        long spread = highest[channel] > lowest[channel] ?
                      (long)(highest[channel] - lowest[channel]) : 0l;

        allowed[channel] = RS_TEX_TOLERANCE * 16l +
                           spread * 16l / RS_BILINEAR_SPREAD_SHARE;
    }
}

/*
 * TexBilinear: texels 0-31 x 0-7 magnified to 64 x 16 pixels, bilinear,
 * clamped. Each channel within one step of the exact filter.
 */
static void rs_tex_bilinear_test(void)
{
    static const char name[] = "TexBilinear";
    struct v9x_rs690_3d_tex_vertex vertices[4];

    rs_tex_quad(vertices, 0u, 0u, TEST_WIDTH, TEX_HEIGHT,
                v9x_rs690_float_integer(1u), v9x_rs690_float_ratio(1u, 2u),
                255u, 255u, 255u);
    if (!rs_draw_textured(name, V9X_RS690_TEXFILTER_LINEAR,
                          V9X_RS690_TEXADDR_CLAMP,
                          V9X_RS690_TEXCOMBINE_REPLACE, vertices, 4u)) {
        return;
    }
    rs_save_block('B');
    rs_tex_judge(name, 0u, TEST_WIDTH, 16l, rs_bilinear_model);
}

/*
 * The TL-vertex state (v9x_rs690_build_3d_state): vertices are
 * D3DTLVERTEX dwords - sx, sy, sz, rhw, diffuse, specular, tu, tv.
 */
#define DEPTH_OFFSET (TEST_BASE + 0x20000ul)
#define DEPTH_PITCH (TEST_WIDTH * 2ul)
#define RS_FLOAT_ONE 0x3f800000ul
#define RS_FLOAT_QUARTER 0x3e800000ul

static void rs_tl(DWORD *vertex, DWORD x, DWORD y, DWORD z, DWORD rhw,
                  DWORD argb, DWORD tu, DWORD tv)
{
    vertex[0] = v9x_rs690_float_integer(x);
    vertex[1] = v9x_rs690_float_integer(y);
    vertex[2] = z;
    vertex[3] = rhw;
    vertex[4] = argb;
    vertex[5] = 0u;
    vertex[6] = tu;
    vertex[7] = tv;
}

/* A rectangle as four TL vertices, corners in RadeonCompositeTile's order,
 * one depth and colour, rhw 1, texture coordinates 0. */
static void rs_tl_quad(DWORD *vertices, DWORD left, DWORD top, DWORD right,
                       DWORD bottom, DWORD z, DWORD argb)
{
    rs_tl(vertices, left, top, z, RS_FLOAT_ONE, argb, 0u, 0u);
    rs_tl(vertices + 8, left, bottom, z, RS_FLOAT_ONE, argb, 0u, 0u);
    rs_tl(vertices + 16, right, bottom, z, RS_FLOAT_ONE, argb, 0u, 0u);
    rs_tl(vertices + 24, right, top, z, RS_FLOAT_ONE, argb, 0u, 0u);
}

/* The surface's own value for an ARGB colour: 565 drops alpha, 8888
 * keeps it. Exact only for channels of 0 or 255 at 16 bpp. */
static DWORD rs_encode_argb(DWORD argb)
{
    if (rs_bpp == 2u) {
        return rs_encode((argb >> 16) & 255u, (argb >> 8) & 255u,
                         argb & 255u);
    }
    return argb;
}

/*
 * When set, a textured rs_draw_tl writes the texture as rows instead of
 * the gradient: row y red with alpha y / 15, as ARGB8888 at 32 bpp and
 * ARGB4444 at 16 bpp (the test names that format). TexDecalAlpha's.
 */
static DWORD rs_texture_rows;

static DWORD rs_texture_row(DWORD row)
{
    DWORD texel;

    if (rs_bpp == 2u) {
        texel = (row << 12) | 0x0f00u;
        return texel | (texel << 16);
    }
    return ((row * 17u) << 24) | 0x00ff0000ul;
}

/*
 * Prefill the surface with `prefill`, clear the Z buffer to all ones when
 * the state tests Z, write the gradient texture when textured, wait idle,
 * the TL state, the draw, the finish, wait idle, read back. The caller
 * fills the state except the target and the Z buffer's place.
 */
static int rs_draw_tl(const char *name, struct v9x_rs690_3d_state *state,
                      DWORD primitive, const DWORD *vertices, DWORD count,
                      DWORD prefill)
{
    struct v9x_rs690_3d_list list;
    char key[64];
    DWORD before_index;
    DWORD after_index;

    state->target.fb_gpu_base = rs_fb_gpu_base;
    state->target.fb_bytes = TEST_FB;
    state->target.offset = TEST_BASE;
    state->target.pitch_bytes = rs_pitch;
    state->target.bytes_per_pixel = rs_bpp;
    state->target.width = TEST_WIDTH;
    state->target.height = rs_height;
    state->depth_offset = DEPTH_OFFSET;
    state->depth_pitch_bytes = DEPTH_PITCH;
    if (state->textured != 0ul) {
        DWORD texel_bytes = rs_bpp;

        /* A test may name a 16-bit format of its own (rs_tex_format_test);
         * otherwise the surface's. */
        if (state->texture.format == V9X_RS690_TEXFMT_ARGB1555 ||
            state->texture.format == V9X_RS690_TEXFMT_ARGB4444) {
            texel_bytes = 2u;
        } else {
            state->texture.format = rs_bpp == 2u ? V9X_RS690_TEXFMT_RGB565 :
                                    V9X_RS690_TEXFMT_ARGB8888;
        }
        state->texture.offset = TEX_OFFSET;
        state->texture.pitch_bytes = TEX_WIDTH * texel_bytes;
        state->texture.width = TEX_WIDTH;
        state->texture.height = TEX_HEIGHT;
    }

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, prefill, TEST_DWORDS);
    if (state->depth_enable != 0ul) {
        rs_add(RS_OP_LFB_FILL32, DEPTH_OFFSET, 0xfffffffful,
               DEPTH_PITCH * rs_height / 4u);
    }
    if (state->textured != 0ul && rs_texture_rows != 0u) {
        DWORD row;

        for (row = 0u; row < TEX_HEIGHT; ++row) {
            rs_add(RS_OP_LFB_FILL32,
                   TEX_OFFSET + row * state->texture.pitch_bytes,
                   rs_texture_row(row), state->texture.pitch_bytes / 4u);
        }
    } else if (state->textured != 0ul) {
        rs_add(RS_OP_LFB_GRADIENT, TEX_OFFSET, 0u,
               state->texture.pitch_bytes * TEX_HEIGHT / 4u);
    }
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rs690_build_3d_state(state, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    if (v9x_rs690_build_3d_draw_tl(primitive, vertices, count, &list) !=
        V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
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
    rs_write_decimal(rs_key(key, name, "Ops"), rs_request_buffer.count);
    return 1;
}

static void rs_result(const char *name, int pass)
{
    char key[64];

    rs_write(rs_key(key, name, "Result"), pass ? "PASS" : "MISMATCH");
    if (!pass) {
        rs_failed = 1u;
    }
}

/* Fill rs_model's rectangle with one surface value. */
static void rs_model_rect(DWORD left, DWORD top, DWORD right, DWORD bottom,
                          DWORD value)
{
    DWORD x;
    DWORD y;

    for (y = top; y < bottom && y < rs_height; ++y) {
        for (x = left; x < right; ++x) {
            rs_set_pixel(rs_model, x, y, value);
        }
    }
}

/*
 * ZTest: Z cleared to all ones, then in one draw three quads with LESS and
 * writes on - red at z 0.5, green at 0.25 overlapping it, blue at 0.75
 * over the whole of rows 0-15. Green wins where it is, red where it is
 * alone, blue only where nothing nearer was drawn: byte-exact. Then the Z
 * buffer is read and the values at each quad published, which must rise
 * green < red < blue < cleared.
 */
static void rs_z_test(void)
{
    static const char name[] = "ZTest";
    struct v9x_rs690_3d_state state;
    DWORD vertices[12 * 8];
    char key[64];
    DWORD z_green;
    DWORD z_red;
    DWORD z_blue;
    DWORD z_clear;
    DWORD mismatches;
    const unsigned short *depth;

    rs_zero(&state, sizeof(state));
    state.depth_enable = 1u;
    state.depth_func = V9X_RS690_ZFUNC_LESS;
    state.depth_write = 1u;
    rs_tl_quad(vertices, 4u, 2u, 36u, 12u, v9x_rs690_float_ratio(1u, 2u),
               0xffff0000ul);
    rs_tl_quad(vertices + 32, 20u, 6u, 52u, 14u,
               v9x_rs690_float_ratio(1u, 4u), 0xff00ff00ul);
    rs_tl_quad(vertices + 64, 0u, 0u, TEST_WIDTH, TEX_HEIGHT,
               v9x_rs690_float_ratio(3u, 4u), 0xff0000fful);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 12u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block('Z');

    rs_model_prefill();
    rs_model_rect(0u, 0u, TEST_WIDTH, TEX_HEIGHT,
                  rs_encode_argb(0xff0000fful));
    rs_model_rect(4u, 2u, 36u, 12u, rs_encode_argb(0xffff0000ul));
    rs_model_rect(20u, 6u, 52u, 14u, rs_encode_argb(0xff00ff00ul));
    mismatches = rs_compare_model(name);

    /* The Z buffer: 64 Z16 cells a row. */
    rs_begin();
    rs_add(RS_OP_LFB_READ_BLOCK, DEPTH_OFFSET, 0u,
           DEPTH_PITCH * rs_height / 4u);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "DepthRead"), "NOT-RUN");
        rs_failed = 1u;
        return;
    }
    depth = (const unsigned short *)rs_result_buffer.block;
    z_green = depth[10u * TEST_WIDTH + 30u];
    z_red = depth[4u * TEST_WIDTH + 8u];
    z_blue = depth[15u * TEST_WIDTH + 60u];
    z_clear = rs_height > TEX_HEIGHT ? depth[20u * TEST_WIDTH + 30u] :
              0xffffu;
    rs_write_hex(rs_key(key, name, "DepthGreen"), z_green);
    rs_write_hex(rs_key(key, name, "DepthRed"), z_red);
    rs_write_hex(rs_key(key, name, "DepthBlue"), z_blue);
    rs_write_hex(rs_key(key, name, "DepthCleared"), z_clear);
    rs_result(name, mismatches == 0u && z_green < z_red && z_red < z_blue &&
                    z_blue < z_clear);
}

/*
 * Blend: a quad of (255, 0, 128) at alpha 96 over the prefill with
 * SRCALPHA, INVSRCALPHA. Each channel (and alpha at 32 bpp, by the same
 * factors) within one step of src * a + dst * (1 - a).
 */
#define RS_BLEND_ALPHA 96u
#define RS_BLEND_ARGB 0x60ff0080ul

static void rs_blend_model(DWORD x, DWORD y, long expected[4],
                           long allowed[4])
{
    static const DWORD source[4] = { 255u, 0u, 128u, RS_BLEND_ALPHA };
    DWORD destination[4];
    DWORD maxima[4];
    int channels = rs_channels(rs_pixel(rs_model, x, y), destination,
                               maxima);
    int channel;

    /* In 1/(255 * 255) of a step: source in channel units is
     * s * max / 255. */
    for (channel = 0; channel < channels; ++channel) {
        expected[channel] =
            (long)(source[channel] * maxima[channel] * RS_BLEND_ALPHA) +
            (long)(destination[channel] * 255u * (255u - RS_BLEND_ALPHA));
        allowed[channel] = RS_TEX_TOLERANCE * 255l * 255l;
    }
}

static void rs_blend_test(void)
{
    static const char name[] = "Blend";
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];

    rs_zero(&state, sizeof(state));
    state.blend_enable = 1u;
    state.blend_src = V9X_RS690_BLEND_SRC_ALPHA;
    state.blend_dst = V9X_RS690_BLEND_INV_SRC_ALPHA;
    rs_tl_quad(vertices, 0u, 0u, TEST_WIDTH, TEX_HEIGHT, 0u, RS_BLEND_ARGB);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block('L');
    rs_model_prefill();
    rs_tex_judge(name, 0u, TEST_WIDTH, 255l * 255l, rs_blend_model);
}

/*
 * TlFog: a quad over rows 0-15 in red (diffuse FFFF0000h), specular RGB
 * 004000h (green 64), specular alpha 255 on the left edge and 0 on the
 * right, fog colour 0000FFh. Direct3D's vertex fog: rgb = f * c + (1 - f)
 * * fog, f the interpolated specular alpha, alpha untouched. With
 * specular off, c is the diffuse alone (the specular RGB must not show);
 * TlFogSpecular adds it first. Each channel within 1.5 steps of the model
 * at the pixel centre, f = 1 - (x + 0.5) / width.
 */
#define RS_FOG_SPECULAR 0x00004000ul
#define RS_FOG_COLOR 0x000000fful

static DWORD rs_fog_add_specular;

static void rs_fog_model(DWORD x, DWORD y, long expected[4], long allowed[4])
{
    static const DWORD fog[4] = { 0u, 0u, 255u, 255u };
    DWORD source[4] = { 255u, 0u, 0u, 255u };
    DWORD values[4];
    DWORD maxima[4];
    long width2 = (long)TEST_WIDTH * 2l;
    long near_part = width2 - (long)(2u * x + 1u);
    int channels = rs_channels(rs_pixel(rs_model, x, y), values, maxima);
    int channel;

    if (rs_fog_add_specular != 0u) {
        source[1] = (RS_FOG_SPECULAR >> 8) & 255u;
    }
    /* In 1/(255 * 2 * width) of a step. */
    for (channel = 0; channel < channels; ++channel) {
        expected[channel] =
            ((long)source[channel] * near_part +
             (long)fog[channel] * (width2 - near_part)) *
            (long)maxima[channel];
        allowed[channel] = 3l * 255l * width2 / 2l;
    }
}

static void rs_fog_test(const char *name, DWORD specular)
{
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    DWORD corner;

    rs_zero(&state, sizeof(state));
    state.fog = 1u;
    state.fog_color = RS_FOG_COLOR;
    state.specular = specular;
    rs_tl_quad(vertices, 0u, 0u, TEST_WIDTH, TEX_HEIGHT, 0u, 0xffff0000ul);

    /* Corners 0 and 1 are the left edge (rs_tl_quad's order). */
    for (corner = 0u; corner < 4u; ++corner) {
        vertices[corner * 8u + 5u] =
            RS_FOG_SPECULAR | (corner < 2u ? 0xff000000ul : 0ul);
    }
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block(specular != 0u ? 'N' : 'J');
    rs_fog_add_specular = specular;
    rs_model_prefill();
    rs_tex_judge(name, 0u, TEST_WIDTH, 255l * 2l * (long)TEST_WIDTH,
                 rs_fog_model);
}

/*
 * Byte-exact single-colour checks that split colour 1's routing from the
 * fog instruction: a quad over rows 0-15, diffuse FFFF0000h, every
 * vertex's specular dword as given, and the whole quad expected in one
 * colour. TlSpecularOnly: untextured, specular on, specular 0000FF00h -
 * FFFFFF00h. TlFogFull and TexFogFull: fog on, f = 0 (specular alpha 0) -
 * the fog colour 0000FFh with the diffuse alpha, FF0000FFh, whatever the
 * colour or texture underneath. Channels of 0 and 255 only, so exact at
 * 16 bpp too.
 */
static void rs_flat_colour_test(const char *name, char letter,
                                DWORD textured, DWORD specular, DWORD fog,
                                DWORD specular_dword, DWORD expected)
{
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    DWORD corner;

    rs_zero(&state, sizeof(state));
    state.specular = specular;
    state.fog = fog;
    state.fog_color = RS_FOG_COLOR;
    if (textured != 0u) {
        state.textured = 1u;
        state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
        state.texture.address = V9X_RS690_TEXADDR_CLAMP;
        state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
        state.texture_alpha = V9X_RS690_TEXALPHA_DIFFUSE;
    }
    rs_tl_quad(vertices, 0u, 0u, TEST_WIDTH, TEX_HEIGHT, 0u, 0xffff0000ul);
    for (corner = 0u; corner < 4u; ++corner) {
        vertices[corner * 8u + 5u] = specular_dword;
    }
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block(letter);
    rs_model_prefill();
    rs_model_rect(0u, 0u, TEST_WIDTH, TEX_HEIGHT, rs_encode_argb(expected));
    rs_result(name, rs_compare_model(name) == 0u);
}

/*
 * FogInst*: TlFogFull's state (untextured, fog, f = 0), with the output
 * instruction's (2) RGB word replaced after the state, to see what it
 * reads. Const: out = src0 (constant 0, the fog colour) - FF0000FFh if it
 * runs and the constant holds. Factor: out = src2.aaa (f) - FF000000h.
 * Temp: out = src1 (the result temp, red less blue, clamped) -
 * FFFF0000h.
 */
static void rs_fog_inst_test(const char *name, char letter, DWORD rgb_inst,
                             DWORD expected)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;
    DWORD vertices[4 * 8];
    DWORD corner;

    rs_zero(&state, sizeof(state));
    state.fog = 1u;
    state.fog_color = RS_FOG_COLOR;
    state.target.fb_gpu_base = rs_fb_gpu_base;
    state.target.fb_bytes = TEST_FB;
    state.target.offset = TEST_BASE;
    state.target.pitch_bytes = rs_pitch;
    state.target.bytes_per_pixel = rs_bpp;
    state.target.width = TEST_WIDTH;
    state.target.height = rs_height;
    rs_tl_quad(vertices, 0u, 0u, TEST_WIDTH, TEX_HEIGHT, 0u, 0xffff0000ul);
    for (corner = 0u; corner < 4u; ++corner) {
        vertices[corner * 8u + 5u] = 0u;
    }
    if (v9x_rs690_build_3d_state(&state, &list) != V9X_STATUS_OK) {
        rs_write(name, "STATE-REFUSED");
        rs_failed = 1u;
        return;
    }
    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_list(&list);
    rs_add(RS_OP_WAIT_FIFO, 1u, 0u, 0u);
    rs_add(RS_OP_MMIO_WRITE32, V9X_RS690_US_ALU_RGB_INST_2, rgb_inst, 0u);
    (void)v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_QUADS, vertices, 4u,
                                     &list);
    rs_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
    rs_add_list(&list);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rs_run()) {
        rs_write(name, "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return;
    }
    rs_save_block(letter);
    rs_model_prefill();
    rs_model_rect(0u, 0u, TEST_WIDTH, TEX_HEIGHT, rs_encode_argb(expected));
    rs_result(name, rs_compare_model(name) == 0u);
}

/*
 * AlphaTest: white quads at alpha 100 (4-28) and 200 (36-60), GREATER
 * than 128: only the second is drawn, byte-exact.
 */
static void rs_alpha_test(void)
{
    static const char name[] = "AlphaTest";
    struct v9x_rs690_3d_state state;
    DWORD vertices[8 * 8];

    rs_zero(&state, sizeof(state));
    state.alpha_test_enable = 1u;
    state.alpha_func = V9X_RS690_AFUNC_GREATER;
    state.alpha_ref = 128u;
    rs_tl_quad(vertices, 4u, 0u, 28u, TEX_HEIGHT, 0u, 0x64fffffful);
    rs_tl_quad(vertices + 32, 36u, 0u, 60u, TEX_HEIGHT, 0u, 0xc8fffffful);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 8u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block('K');
    rs_model_prefill();
    rs_model_rect(36u, 0u, 60u, TEX_HEIGHT, rs_encode_argb(0xc8fffffful));
    rs_result(name, rs_compare_model(name) == 0u);
}

/*
 * Cull: two triangles in one draw - A (4,2) (28,2) (4,14), clockwise as
 * the screen shows it (y down); B (36,2) (36,14) (60,2), counter-clockwise
 * - once with CULL_FRONT and once with CULL_BACK. Publishes which survive;
 * passes when each mode removes exactly one and they remove different
 * ones. Which is which is the measurement.
 */
static int rs_cull_run(const char *name, DWORD cull, int *a_drawn,
                       int *b_drawn)
{
    struct v9x_rs690_3d_state state;
    DWORD vertices[6 * 8];
    const unsigned char *actual;
    DWORD prefill_pixel;
    char key[64];

    rs_zero(&state, sizeof(state));
    state.cull = cull;
    rs_tl(vertices, 4u, 2u, 0u, RS_FLOAT_ONE, 0xff00ff00ul, 0u, 0u);
    rs_tl(vertices + 8, 28u, 2u, 0u, RS_FLOAT_ONE, 0xff00ff00ul, 0u, 0u);
    rs_tl(vertices + 16, 4u, 14u, 0u, RS_FLOAT_ONE, 0xff00ff00ul, 0u, 0u);
    rs_tl(vertices + 24, 36u, 2u, 0u, RS_FLOAT_ONE, 0xff00ff00ul, 0u, 0u);
    rs_tl(vertices + 32, 36u, 14u, 0u, RS_FLOAT_ONE, 0xff00ff00ul, 0u, 0u);
    rs_tl(vertices + 40, 60u, 2u, 0u, RS_FLOAT_ONE, 0xff00ff00ul, 0u, 0u);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_TRIANGLES, vertices, 6u,
                    TEST_PREFILL)) {
        return 0;
    }
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    prefill_pixel = rs_pixel(rs_model, 0u, 0u);
    *a_drawn = rs_pixel(actual, 8u, 5u) != prefill_pixel;
    *b_drawn = rs_pixel(actual, 40u, 5u) != prefill_pixel;
    rs_write_decimal(rs_key(key, name, "ClockwiseDrawn"), (DWORD)*a_drawn);
    rs_write_decimal(rs_key(key, name, "CounterClockwiseDrawn"),
                     (DWORD)*b_drawn);
    return 1;
}

static void rs_cull_test(void)
{
    int none_a = 0;
    int none_b = 0;
    int front_a = 0;
    int front_b = 0;
    int back_a = 0;
    int back_b = 0;

    if (!rs_cull_run("CullNone", V9X_RS690_CULL_NONE, &none_a, &none_b) ||
        !rs_cull_run("CullFront", V9X_RS690_CULL_FRONT, &front_a,
                     &front_b) ||
        !rs_cull_run("CullBack", V9X_RS690_CULL_BACK, &back_a, &back_b)) {
        return;
    }
    rs_result("Cull", none_a && none_b && front_a != front_b &&
                      back_a != back_b && front_a != back_a);
}

/*
 * Perspective: the gradient texture, nearest, clamped, REPLACE, on a quad
 * across the whole 64 x 16 with tu 0 to 1 and tv 0 throughout, rhw 1 on the
 * left edge and 0.25 on the right. tv is held at 0 because a quad whose
 * rhw varies across it is no projection of a rectangle: its two triangles
 * interpolate a varying tv differently (the first run, 2026-11-05, mixed
 * rows along the diagonal while every column was right). Every pixel then
 * samples texel row 0. Perspective-correct, pixel x's texel column
 * is floor(32 * s) with s = f / 4 / (1 - 3f / 4), f = (2x + 1) / 128:
 * 32(2x + 1) / (512 - 3(2x + 1)). Affine it would be (2x + 1) / 4. Pixels
 * whose perspective s lands within 1/50 of a texel edge are not judged;
 * each other pixel's column is found by matching its value against row
 * 0's texels. Passes when every judged pixel is the perspective column.
 */
static void rs_perspective_test(void)
{
    static const char name[] = "Perspective";
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    const unsigned char *actual;
    char key[64];
    DWORD x;
    DWORD y;
    DWORD judged = 0u;
    DWORD perspective = 0u;
    DWORD affine = 0u;
    DWORD unmatched = 0u;
    DWORD first_wrong = 0xfffffffful;

    rs_zero(&state, sizeof(state));
    state.textured = 1u;
    state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
    state.texture.address = V9X_RS690_TEXADDR_CLAMP;
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    rs_tl(vertices, 0u, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, 0u);
    rs_tl(vertices + 8, 0u, TEX_HEIGHT, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u,
          0u);
    rs_tl(vertices + 16, TEST_WIDTH, TEX_HEIGHT, 0u, RS_FLOAT_QUARTER,
          0xfffffffful, RS_FLOAT_ONE, 0u);
    rs_tl(vertices + 24, TEST_WIDTH, 0u, 0u, RS_FLOAT_QUARTER, 0xfffffffful,
          RS_FLOAT_ONE, 0u);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block('P');
    actual = (const unsigned char *)rs_result_buffer.block;

    for (y = 0u; y < TEX_HEIGHT; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            DWORD numerator = 32u * (2u * x + 1u);
            DWORD denominator = 512u - 3u * (2u * x + 1u);
            DWORD column = numerator / denominator;
            DWORD remainder = numerator % denominator;
            DWORD pixel = rs_pixel(actual, x, y);
            DWORD found = 0xfffffffful;
            DWORD candidate;

            if (remainder * 50u <= denominator ||
                (denominator - remainder) * 50u <= denominator) {
                continue;
            }
            ++judged;
            for (candidate = 0u; candidate < TEX_WIDTH; ++candidate) {
                if (rs_texel(candidate, 0u) == pixel) {
                    found = candidate;
                    break;
                }
            }
            if (found == 0xfffffffful) {
                ++unmatched;
            }
            if (found == column) {
                ++perspective;
            } else if (first_wrong == 0xfffffffful) {
                first_wrong = y * TEST_WIDTH + x;
            }
            if (found == (2u * x + 1u) / 4u) {
                ++affine;
            }
        }
    }
    rs_write_decimal(rs_key(key, name, "Judged"), judged);
    rs_write_decimal(rs_key(key, name, "PerspectiveColumn"), perspective);
    rs_write_decimal(rs_key(key, name, "AffineColumn"), affine);
    rs_write_decimal(rs_key(key, name, "NoTexel"), unmatched);
    if (first_wrong != 0xfffffffful) {
        char text[24];
        char number[12];

        rs_decimal(text, first_wrong % TEST_WIDTH);
        lstrcatA(text, ",");
        rs_decimal(number, first_wrong / TEST_WIDTH);
        lstrcatA(text, number);
        rs_write(rs_key(key, name, "FirstWrongXY"), text);
        rs_write_hex(rs_key(key, name, "FirstWrongRead"),
                     rs_pixel(actual, first_wrong % TEST_WIDTH,
                              first_wrong / TEST_WIDTH));
    }
    rs_result(name, judged != 0u && perspective == judged);
}

/*
 * TexAdd: the gradient 1:1 over x 0-31, rows 0-15, nearest, clamped,
 * Direct3D's ADD through the TL state - texel + diffuse (32, 64, 96),
 * clamped, alpha the diffuse's 255. TexAddSpecular adds a specular blue
 * of 64 on top (ADD's specular is its own instruction). Each channel within
 * one step. TexDecalAlpha: row y of the texture red with alpha y / 15,
 * DECALALPHA over a diffuse of (64, 128, 192): diffuse + (texel - diffuse)
 * * texel alpha, alpha the diffuse's; within 1.5 steps.
 */
#define RS_ADD_DIFFUSE 0xff204060ul
#define RS_DECAL_DIFFUSE 0xff4080c0ul
#define RS_ADD_SPECULAR 0x00000040ul

static DWORD rs_combine_specular;

static void rs_add_model(DWORD x, DWORD y, long expected[4], long allowed[4])
{
    DWORD values[4];
    DWORD maxima[4];
    DWORD addend[4];
    int channels = rs_channels(rs_texel(x, y), values, maxima);
    int channel;

    addend[0] = (RS_ADD_DIFFUSE >> 16) & 255u;
    addend[1] = (RS_ADD_DIFFUSE >> 8) & 255u;
    addend[2] = (RS_ADD_DIFFUSE & 255u) + (rs_combine_specular & 255u);
    addend[3] = 255u;
    if (channels == 4) {
        values[3] = 0u;
    }
    /* In 1/255 of a step: texel * 255 + addend * max, clamped. */
    for (channel = 0; channel < channels; ++channel) {
        long sum = (long)values[channel] * 255l +
                   (long)(addend[channel] * maxima[channel]);
        long top = (long)maxima[channel] * 255l;

        expected[channel] = sum > top ? top : sum;
        allowed[channel] = RS_TEX_TOLERANCE * 255l;
    }
}

static void rs_decal_alpha_model(DWORD x, DWORD y, long expected[4],
                                 long allowed[4])
{
    static const DWORD texel[4] = { 255u, 0u, 0u, 255u };
    DWORD diffuse[4];
    DWORD values[4];
    DWORD maxima[4];
    int channels = rs_channels(rs_pixel(rs_model, x, y), values, maxima);
    int channel;

    diffuse[0] = (RS_DECAL_DIFFUSE >> 16) & 255u;
    diffuse[1] = (RS_DECAL_DIFFUSE >> 8) & 255u;
    diffuse[2] = RS_DECAL_DIFFUSE & 255u;
    diffuse[3] = 255u;
    /* In 1/(15 * 255) of a step, alpha y / 15. */
    for (channel = 0; channel < channels; ++channel) {
        DWORD a = channel == 3 ? 0u : y;

        expected[channel] =
            (long)((diffuse[channel] * (15u - a) + texel[channel] * a) *
                   maxima[channel]);
        allowed[channel] = 3l * 15l * 255l / 2l;
    }
}

static void rs_combine_test(const char *name, char letter, DWORD combine,
                            DWORD diffuse, DWORD specular)
{
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    DWORD index;
    int rows = combine == V9X_RS690_TEXCOMBINE_DECALALPHA;

    rs_zero(&state, sizeof(state));
    state.textured = 1u;
    state.specular = specular != 0u ? 1u : 0u;
    state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
    state.texture.address = V9X_RS690_TEXADDR_CLAMP;
    state.texture.combine = combine;
    state.texture_alpha = V9X_RS690_TEXALPHA_DIFFUSE;
    if (rows && rs_bpp == 2u) {
        state.texture.format = V9X_RS690_TEXFMT_ARGB4444;
    }
    rs_tl(vertices, 0u, 0u, 0u, RS_FLOAT_ONE, diffuse, 0u, 0u);
    rs_tl(vertices + 8, 0u, TEX_HEIGHT, 0u, RS_FLOAT_ONE, diffuse, 0u,
          RS_FLOAT_ONE);
    rs_tl(vertices + 16, TEX_WIDTH, TEX_HEIGHT, 0u, RS_FLOAT_ONE, diffuse,
          RS_FLOAT_ONE, RS_FLOAT_ONE);
    rs_tl(vertices + 24, TEX_WIDTH, 0u, 0u, RS_FLOAT_ONE, diffuse,
          RS_FLOAT_ONE, 0u);
    for (index = 0u; index < 4u; ++index) {
        vertices[index * 8u + 5u] = specular;
    }
    rs_texture_rows = rows ? 1u : 0u;
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        rs_texture_rows = 0u;
        return;
    }
    rs_texture_rows = 0u;
    rs_save_block(letter);
    rs_combine_specular = specular;
    rs_model_prefill();
    if (rows) {
        rs_tex_judge(name, 0u, TEX_WIDTH, 15l * 255l, rs_decal_alpha_model);
    } else {
        rs_tex_judge(name, 0u, TEX_WIDTH, 255l, rs_add_model);
    }
}

/*
 * Chroma*: what the texture unit's chroma key does, measured not judged.
 * The gradient 1:1 over x 0-31, rows 0-15, nearest, REPLACE with the
 * texel's alpha, blend and alpha test off, with TX_FILTER1's chroma mode
 * and key set. Every gradient texel is unique, so a key that matches
 * changes exactly texel (5, 3)'s pixel. Published: how many pixels differ
 * from the unkeyed texture, and what pixel (5, 3) and its neighbour read.
 */
static DWORD rs_chroma_scale = 1u;
static DWORD rs_chroma_filter = V9X_RS690_TEXFILTER_NEAREST;

static void rs_chroma_test(const char *name, char letter, DWORD mode,
                           DWORD key)
{
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    const unsigned char *actual;
    char key_name[64];
    DWORD x;
    DWORD y;
    DWORD changed = 0u;
    DWORD killed = 0u;
    DWORD right = TEX_WIDTH * rs_chroma_scale;
    DWORD bottom = TEX_HEIGHT * rs_chroma_scale;
    DWORD prefill;

    if (bottom > rs_height) {
        bottom = rs_height;
    }
    rs_zero(&state, sizeof(state));
    state.textured = 1u;
    state.texture.filter = rs_chroma_filter;
    state.texture.address = V9X_RS690_TEXADDR_CLAMP;
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    state.texture.chroma_mode = mode;
    state.texture.chroma_key = key;
    state.texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
    rs_tl(vertices, 0u, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, 0u);
    rs_tl(vertices + 8, 0u, TEX_HEIGHT * rs_chroma_scale, 0u, RS_FLOAT_ONE,
          0xfffffffful, 0u, RS_FLOAT_ONE);
    rs_tl(vertices + 16, right, TEX_HEIGHT * rs_chroma_scale, 0u,
          RS_FLOAT_ONE, 0xfffffffful, RS_FLOAT_ONE, RS_FLOAT_ONE);
    rs_tl(vertices + 24, right, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful,
          RS_FLOAT_ONE, 0u);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block(letter);
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_model_prefill();
    prefill = rs_pixel(rs_model, 0u, 0u);
    for (y = 0u; y < bottom; ++y) {
        for (x = 0u; x < right; ++x) {
            DWORD pixel = rs_pixel(actual, x, y);

            if (pixel == prefill) {
                ++killed;
            }
            if (rs_chroma_scale == 1u && pixel != rs_texel(x, y)) {
                ++changed;
            }
        }
    }
    rs_write_decimal(rs_key(key_name, name, "KilledPixels"), killed);
    rs_write_hex(rs_key(key_name, name, "Key"), key);
    rs_write_hex(rs_key(key_name, name, "Texel5_3"), rs_texel(5u, 3u));
    rs_write_hex(rs_key(key_name, name, "Pixel5_3"), rs_pixel(actual, 5u, 3u));
    rs_write_hex(rs_key(key_name, name, "Pixel6_3"), rs_pixel(actual, 6u, 3u));
    rs_write_decimal(rs_key(key_name, name, "ChangedPixels"), changed);
    rs_write(rs_key(key_name, name, "Result"), "MEASURED");
}

/* Texel (5, 3) of the gradient as a key. RS_CHROMA_RAW: the texel as
 * stored; RS_CHROMA_RAW_FF: the same with the top byte FF (at 32 bpp an
 * alpha that differs from the texel's 00). Otherwise as A8R8G8B8: itself
 * at 32 bpp; at 16 bpp its 565 channels widened with the top bits repeated
 * (replicate) or zeros below (zero fill), alpha FF. */
#define RS_CHROMA_ZERO_FILL 0
#define RS_CHROMA_REPLICATE 1
#define RS_CHROMA_RAW 2
#define RS_CHROMA_RAW_FF 3

static DWORD rs_chroma_key(int replicate)
{
    DWORD texel = rs_texel(5u, 3u);
    DWORD r;
    DWORD g;
    DWORD b;

    if (rs_bpp == 2u) {
        texel &= 0xffffu;
    }
    if (replicate == RS_CHROMA_RAW) {
        return texel;
    }
    if (replicate == RS_CHROMA_RAW_FF) {
        return texel | 0xff000000ul;
    }
    if (rs_bpp != 2u) {
        return texel;
    }
    r = (texel >> 11) & 31u;
    g = (texel >> 5) & 63u;
    b = texel & 31u;
    if (replicate) {
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
    } else {
        r <<= 3;
        g <<= 2;
        b <<= 3;
    }
    return 0xff000000ul | (r << 16) | (g << 8) | b;
}

/*
 * MipLayout: where the chip finds each mip level, read off the gradient,
 * whose texels say where they are (texel n holds n at 16 bpp; dword i
 * holds 2i in its low half at 32 bpp). A 32x16 texture with six levels,
 * nearest within and between levels, its levels 0-4 drawn 1:1 side by
 * side - level n as a (32 >> n) x (16 >> n) quad at x 0, 32, 48, 56,
 * 60, so the level of detail is exactly n. Each level's texels (0, 0),
 * (1, 0) and (0, 1) give its offset from the base and its row stride in
 * bytes. With pitch_en clear, TXPITCH_EN is cleared after the state.
 * Published, not judged.
 */
#define RS_MIP_LEVELS 6u
#define RS_MIP_DRAWN 5u
#define RS_MIP_GRADIENT_DWORDS 4096ul

static long rs_mip_byte(DWORD pixel)
{
    if (rs_bpp == 2u) {
        return (long)(pixel & 0xffffu) * 2l;
    }
    return (long)((pixel & 0xffffu) / 2u) * 4l;
}

static void rs_mip_layout_test(const char *name, char letter, int pitch_en)
{
    static const DWORD left[RS_MIP_DRAWN] = { 0u, 32u, 48u, 56u, 60u };
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;
    DWORD vertices[12 * 8];
    const unsigned char *actual;
    char key[64];
    char field[32];
    DWORD level;
    DWORD quad;
    DWORD format0 = 0u;
    DWORD index;

    rs_zero(&state, sizeof(state));
    state.target.fb_gpu_base = rs_fb_gpu_base;
    state.target.fb_bytes = TEST_FB;
    state.target.offset = TEST_BASE;
    state.target.pitch_bytes = rs_pitch;
    state.target.bytes_per_pixel = rs_bpp;
    state.target.width = TEST_WIDTH;
    state.target.height = rs_height;
    state.textured = 1u;
    state.texture.format = rs_bpp == 2u ? V9X_RS690_TEXFMT_RGB565 :
                           V9X_RS690_TEXFMT_ARGB8888;
    state.texture.offset = TEX_OFFSET;
    state.texture.pitch_bytes = TEX_WIDTH * rs_bpp;
    state.texture.width = TEX_WIDTH;
    state.texture.height = TEX_HEIGHT;
    state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
    state.texture.address = V9X_RS690_TEXADDR_CLAMP;
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    state.texture.levels = RS_MIP_LEVELS;
    state.texture.mip_filter = V9X_RS690_MIPFILTER_NEAREST;
    state.texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
    if (v9x_rs690_build_3d_state(&state, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return;
    }
    for (index = 0u; index < list.count; ++index) {
        if (list.offsets[index] == V9X_RS690_TX_FORMAT0_0) {
            format0 = list.values[index];
        }
    }
    if (!pitch_en) {
        format0 &= 0x7ffffffful;
    }
    rs_write_hex(rs_key(key, name, "TxFormat0"), format0);

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rs_add(RS_OP_LFB_GRADIENT, TEX_OFFSET, 0u, RS_MIP_GRADIENT_DWORDS);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_list(&list);
    rs_add(RS_OP_WAIT_FIFO, 1u, 0u, 0u);
    rs_add(RS_OP_MMIO_WRITE32, V9X_RS690_TX_FORMAT0_0, format0, 0u);

    /* Two lists: levels 0-2, then 3-4. */
    for (quad = 0u; quad < RS_MIP_DRAWN; quad += 3u) {
        DWORD count = RS_MIP_DRAWN - quad < 3u ? RS_MIP_DRAWN - quad : 3u;
        DWORD q;

        for (q = 0u; q < count; ++q) {
            DWORD n = quad + q;
            DWORD x0 = left[n];
            DWORD x1 = x0 + (TEX_WIDTH >> n);
            DWORD y1 = TEX_HEIGHT >> n;
            DWORD *v = vertices + q * 32u;

            rs_tl(v, x0, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, 0u);
            rs_tl(v + 8, x0, y1, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u,
                  RS_FLOAT_ONE);
            rs_tl(v + 16, x1, y1, 0u, RS_FLOAT_ONE, 0xfffffffful,
                  RS_FLOAT_ONE, RS_FLOAT_ONE);
            rs_tl(v + 24, x1, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful,
                  RS_FLOAT_ONE, 0u);
        }
        (void)v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_QUADS, vertices,
                                         count * 4u, &list);
        rs_add_list(&list);
    }
    (void)v9x_rs690_build_3d_finish(&list);
    rs_add_list(&list);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return;
    }
    rs_save_block(letter);
    actual = (const unsigned char *)rs_result_buffer.block;

    for (level = 0u; level < RS_MIP_DRAWN; ++level) {
        DWORD x0 = left[level];
        long origin = rs_mip_byte(rs_pixel(actual, x0, 0u));

        wsprintfA(field, "Level%luOffset", level);
        rs_write_decimal(rs_key(key, name, field), (DWORD)origin);
        if ((TEX_WIDTH >> level) > 1u) {
            wsprintfA(field, "Level%luStepX", level);
            rs_write_decimal(rs_key(key, name, field),
                             (DWORD)(rs_mip_byte(rs_pixel(actual, x0 + 1u,
                                                          0u)) - origin));
        }
        if ((TEX_HEIGHT >> level) > 1u) {
            wsprintfA(field, "Level%luStride", level);
            rs_write_decimal(rs_key(key, name, field),
                             (DWORD)(rs_mip_byte(rs_pixel(actual, x0,
                                                          1u)) - origin));
        }
        wsprintfA(field, "Level%luPixel", level);
        rs_write_hex(rs_key(key, name, field), rs_pixel(actual, x0, 0u));
    }
    rs_write(rs_key(key, name, "Result"), "MEASURED");
}

/*
 * MipSelect: the builder's layout (v9x_rs690_mip_offset) checked. The
 * 32x16 chain's six levels each filled with a colour of their own -
 * red, green, blue, white, yellow, cyan - at the offsets and pitches the
 * builder computes, the rest of the texture memory the gradient; levels
 * 0-4 drawn as MipLayout draws them, nearest between levels. Each quad
 * must be its level's colour, byte for byte. MipLinear: a 23x11 quad,
 * between level 0's and level 1's sizes, with the linear mip filter -
 * published, and judged only as holding both red and green.
 */
static const DWORD rs_mip_colours[RS_MIP_LEVELS] = {
    0xffff0000ul, 0xff00ff00ul, 0xff0000fful,
    0xfffffffful, 0xffffff00ul, 0xff00fffful
};

static DWORD rs_mip_fill_value(DWORD argb)
{
    DWORD texel;

    if (rs_bpp != 2u) {
        return argb;
    }
    texel = rs_encode((argb >> 16) & 255u, (argb >> 8) & 255u, argb & 255u);
    return texel | (texel << 16);
}

static int rs_mip_draw(const char *name, DWORD mip_filter,
                       const DWORD *vertices, DWORD quads)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;
    char key[64];
    DWORD level;
    DWORD quad;

    rs_zero(&state, sizeof(state));
    state.target.fb_gpu_base = rs_fb_gpu_base;
    state.target.fb_bytes = TEST_FB;
    state.target.offset = TEST_BASE;
    state.target.pitch_bytes = rs_pitch;
    state.target.bytes_per_pixel = rs_bpp;
    state.target.width = TEST_WIDTH;
    state.target.height = rs_height;
    state.textured = 1u;
    state.texture.format = rs_bpp == 2u ? V9X_RS690_TEXFMT_RGB565 :
                           V9X_RS690_TEXFMT_ARGB8888;
    state.texture.offset = TEX_OFFSET;
    state.texture.pitch_bytes = v9x_rs690_mip_pitch(TEX_WIDTH, rs_bpp, 0u);
    state.texture.width = TEX_WIDTH;
    state.texture.height = TEX_HEIGHT;
    state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
    state.texture.address = V9X_RS690_TEXADDR_CLAMP;
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    state.texture.levels = RS_MIP_LEVELS;
    state.texture.mip_filter = mip_filter;
    state.texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
    if (v9x_rs690_build_3d_state(&state, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return 0;
    }

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rs_add(RS_OP_LFB_GRADIENT, TEX_OFFSET, 0u, RS_MIP_GRADIENT_DWORDS);
    for (level = 0u; level < RS_MIP_LEVELS; ++level) {
        DWORD start = v9x_rs690_mip_offset(TEX_WIDTH, TEX_HEIGHT, rs_bpp,
                                           level);
        DWORD end = v9x_rs690_mip_offset(TEX_WIDTH, TEX_HEIGHT, rs_bpp,
                                         level + 1u);

        rs_add(RS_OP_LFB_FILL32, TEX_OFFSET + start,
               rs_mip_fill_value(rs_mip_colours[level]), (end - start) / 4u);
    }
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_list(&list);
    for (quad = 0u; quad < quads; quad += 3u) {
        DWORD count = quads - quad < 3u ? quads - quad : 3u;

        (void)v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_QUADS,
                                         vertices + quad * 32u, count * 4u,
                                         &list);
        rs_add_list(&list);
    }
    (void)v9x_rs690_build_3d_finish(&list);
    rs_add_list(&list);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return 0;
    }
    return 1;
}

static void rs_mip_quad(DWORD *v, DWORD x0, DWORD x1, DWORD y1)
{
    rs_tl(v, x0, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, 0u);
    rs_tl(v + 8, x0, y1, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, RS_FLOAT_ONE);
    rs_tl(v + 16, x1, y1, 0u, RS_FLOAT_ONE, 0xfffffffful, RS_FLOAT_ONE,
          RS_FLOAT_ONE);
    rs_tl(v + 24, x1, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, RS_FLOAT_ONE, 0u);
}

static void rs_mip_select_test(void)
{
    static const char name[] = "MipSelect";
    static const DWORD left[RS_MIP_DRAWN] = { 0u, 32u, 48u, 56u, 60u };
    DWORD vertices[RS_MIP_DRAWN * 32];
    char key[64];
    char field[32];
    DWORD level;

    for (level = 0u; level < RS_MIP_DRAWN; ++level) {
        rs_mip_quad(vertices + level * 32u, left[level],
                    left[level] + (TEX_WIDTH >> level), TEX_HEIGHT >> level);
    }
    if (!rs_mip_draw(name, V9X_RS690_MIPFILTER_NEAREST, vertices,
                     RS_MIP_DRAWN)) {
        return;
    }
    rs_save_block('~');
    rs_model_prefill();
    for (level = 0u; level < RS_MIP_DRAWN; ++level) {
        rs_model_rect(left[level], 0u, left[level] + (TEX_WIDTH >> level),
                      TEX_HEIGHT >> level,
                      rs_encode_argb(rs_mip_colours[level]));
        wsprintfA(field, "Level%luPixel", level);
        rs_write_hex(rs_key(key, name, field),
                     rs_pixel((const unsigned char *)rs_result_buffer.block,
                              left[level], 0u));
    }
    rs_result(name, rs_compare_model(name) == 0u);
}

static void rs_mip_linear_test(void)
{
    static const char name[] = "MipLinear";
    DWORD vertices[32];
    DWORD pixel;
    DWORD values[4];
    DWORD maxima[4];
    char key[64];

    rs_mip_quad(vertices, 0u, 23u, 11u);
    if (!rs_mip_draw(name, V9X_RS690_MIPFILTER_LINEAR, vertices, 1u)) {
        return;
    }
    rs_save_block('#');
    pixel = rs_pixel((const unsigned char *)rs_result_buffer.block, 11u, 5u);
    (void)rs_channels(pixel, values, maxima);
    rs_write_hex(rs_key(key, name, "Pixel11_5"), pixel);
    rs_result(name, values[0] * 8u > maxima[0] && values[1] * 8u > maxima[1] &&
                    values[2] * 8u < maxima[2]);
}

/*
 * TexFormat1555 / TexFormat4444: the gradient written as 16-bit texels
 * (texel n is n), read as ARGB1555 or ARGB4444, drawn 1:1 over x 0-31,
 * rows 0-15, nearest, clamped, REPLACE with the texel's alpha, through the
 * TL state. Each channel within one step of the texel's channel scaled to
 * the target's; alpha too at 32 bpp. The raw pixels at the four corners
 * are published, so a channel or row mapping other than the model's can
 * be read from the result alone.
 */
static DWORD rs_format_under_test;

/* Texel n's channel c (0 red, 1 green, 2 blue, 3 alpha) and its maximum. */
static DWORD rs_texel16_channel(DWORD n, int channel, DWORD *maximum)
{
    if (rs_format_under_test == V9X_RS690_TEXFMT_ARGB1555) {
        static const DWORD shifts[4] = { 10u, 5u, 0u, 15u };

        *maximum = channel == 3 ? 1u : 31u;
        return (n >> shifts[channel]) & *maximum;
    }
    {
        static const DWORD shifts[4] = { 8u, 4u, 0u, 12u };

        *maximum = 15u;
        return (n >> shifts[channel]) & 15u;
    }
}

/* In 1/(31 * 15) of a target step: v / source max * target max. */
#define RS_FORMAT_SCALE 465l

static void rs_format_model(DWORD x, DWORD y, long expected[4],
                            long allowed[4])
{
    DWORD n = (y * TEX_WIDTH + x) & 0xffffu;
    DWORD values[4];
    DWORD maxima[4];
    int channels = rs_channels(rs_pixel(rs_model, 0u, 0u), values, maxima);
    int channel;

    for (channel = 0; channel < channels; ++channel) {
        DWORD source_max;
        DWORD value = rs_texel16_channel(n, channel, &source_max);

        expected[channel] = (long)(value * maxima[channel]) *
                            (RS_FORMAT_SCALE / (long)source_max);
        allowed[channel] = RS_TEX_TOLERANCE * RS_FORMAT_SCALE;
    }
}

static void rs_tex_format_test(const char *name, char letter, DWORD format)
{
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    const unsigned char *actual;
    char key[64];

    rs_zero(&state, sizeof(state));
    state.textured = 1u;
    state.texture.format = format;
    state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
    state.texture.address = V9X_RS690_TEXADDR_CLAMP;
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    state.texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
    rs_tl(vertices, 0u, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, 0u);
    rs_tl(vertices + 8, 0u, TEX_HEIGHT, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u,
          RS_FLOAT_ONE);
    rs_tl(vertices + 16, TEX_WIDTH, TEX_HEIGHT, 0u, RS_FLOAT_ONE,
          0xfffffffful, RS_FLOAT_ONE, RS_FLOAT_ONE);
    rs_tl(vertices + 24, TEX_WIDTH, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful,
          RS_FLOAT_ONE, 0u);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block(letter);
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_write_hex(rs_key(key, name, "Pixel0_0"), rs_pixel(actual, 0u, 0u));
    rs_write_hex(rs_key(key, name, "Pixel31_0"), rs_pixel(actual, 31u, 0u));
    rs_write_hex(rs_key(key, name, "Pixel0_15"), rs_pixel(actual, 0u, 15u));
    rs_write_hex(rs_key(key, name, "Pixel31_15"),
                 rs_pixel(actual, 31u, 15u));
    rs_format_under_test = format;
    rs_model_prefill();
    rs_tex_judge(name, 0u, TEX_WIDTH, RS_FORMAT_SCALE, rs_format_model);
}

/*
 * TexSpecular: the gradient texture in the surface's format drawn 1:1 over
 * x 0-31, rows 0-15, REPLACE with the texel's alpha, through the TL state
 * with specular on and every specular colour zero: the result must be the
 * texture, byte for byte, as TexDecal's. Then TexSpecularAdd: the same
 * with a specular of (0, 0, 64) on every vertex, published, not judged.
 */
static int rs_tex_specular_draw(const char *name, DWORD specular)
{
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    DWORD index;

    rs_zero(&state, sizeof(state));
    state.textured = 1u;
    state.specular = 1u;
    state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
    state.texture.address = V9X_RS690_TEXADDR_CLAMP;
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    state.texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
    rs_tl(vertices, 0u, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, 0u);
    rs_tl(vertices + 8, 0u, TEX_HEIGHT, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u,
          RS_FLOAT_ONE);
    rs_tl(vertices + 16, TEX_WIDTH, TEX_HEIGHT, 0u, RS_FLOAT_ONE,
          0xfffffffful, RS_FLOAT_ONE, RS_FLOAT_ONE);
    rs_tl(vertices + 24, TEX_WIDTH, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful,
          RS_FLOAT_ONE, 0u);
    for (index = 0u; index < 4u; ++index) {
        vertices[index * 8u + 5u] = specular;
    }
    return rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                      TEST_PREFILL);
}

static void rs_tex_specular_test(void)
{
    static const char name[] = "TexSpecular";
    static const char added[] = "TexSpecularAdd";
    const unsigned char *actual;
    char key[64];
    DWORD x;
    DWORD y;

    if (!rs_tex_specular_draw(name, 0u)) {
        return;
    }
    rs_save_block('U');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_write_hex(rs_key(key, name, "Pixel0_0"), rs_pixel(actual, 0u, 0u));
    rs_write_hex(rs_key(key, name, "Pixel31_0"), rs_pixel(actual, 31u, 0u));
    rs_write_hex(rs_key(key, name, "Pixel0_15"), rs_pixel(actual, 0u, 15u));
    rs_model_prefill();
    for (y = 0u; y < TEX_HEIGHT; ++y) {
        for (x = 0u; x < TEX_WIDTH; ++x) {
            rs_set_pixel(rs_model, x, y, rs_texel(x, y));
        }
    }
    rs_result(name, rs_compare_model(name) == 0u);

    if (!rs_tex_specular_draw(added, 0x00000040ul)) {
        return;
    }
    rs_save_block('V');
    actual = (const unsigned char *)rs_result_buffer.block;
    rs_write_hex(rs_key(key, added, "Pixel0_0"), rs_pixel(actual, 0u, 0u));
    rs_write_hex(rs_key(key, added, "Pixel31_0"), rs_pixel(actual, 31u, 0u));
    rs_write_hex(rs_key(key, added, "Pixel0_15"), rs_pixel(actual, 0u, 15u));
}

/*
 * TexQuadrants: V9XD32's T6 texture - 8 x 8 ARGB1555, red, green, blue and
 * white quadrants, alpha set - drawn over a square of the surface's
 * height, nearest, REPLACE with the texel's alpha, through the TL state;
 * at an offset aligned as every other probe texture is (64 KiB) and at
 * one aligned only to 32 bytes, as Direct3D's placement put T6 (offset
 * ...500h, M-GB8KRE 2026-11-05). Every pixel exact. Its first run put the
 * rows 32 bytes apart and read them 64 apart at both offsets; the rows
 * are now 64 bytes apart, the builder's rule.
 */
static const DWORD rs_quadrant_texels[4] = {
    0xfc00u, 0x83e0u, 0x801fu, 0xffffu
};

static void rs_tex_quadrants_test(const char *name, char letter,
                                  DWORD offset)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;
    DWORD vertices[4 * 8];
    char key[64];
    DWORD side = rs_height;
    DWORD x;
    DWORD y;
    DWORD before_index;
    DWORD after_index;

    rs_zero(&state, sizeof(state));
    state.target.fb_gpu_base = rs_fb_gpu_base;
    state.target.fb_bytes = TEST_FB;
    state.target.offset = TEST_BASE;
    state.target.pitch_bytes = rs_pitch;
    state.target.bytes_per_pixel = rs_bpp;
    state.target.width = TEST_WIDTH;
    state.target.height = rs_height;
    state.textured = 1u;
    state.texture.offset = offset;
    state.texture.pitch_bytes = V9X_RS690_TEX_PITCH_ALIGN;
    state.texture.format = V9X_RS690_TEXFMT_ARGB1555;
    state.texture.width = 8u;
    state.texture.height = 8u;
    state.texture.filter = V9X_RS690_TEXFILTER_NEAREST;
    state.texture.address = V9X_RS690_TEXADDR_WRAP;
    state.texture.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    state.texture_alpha = V9X_RS690_TEXALPHA_TEXEL;
    rs_tl(vertices, 0u, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u, 0u);
    rs_tl(vertices + 8, 0u, side, 0u, RS_FLOAT_ONE, 0xfffffffful, 0u,
          RS_FLOAT_ONE);
    rs_tl(vertices + 16, side, side, 0u, RS_FLOAT_ONE, 0xfffffffful,
          RS_FLOAT_ONE, RS_FLOAT_ONE);
    rs_tl(vertices + 24, side, 0u, 0u, RS_FLOAT_ONE, 0xfffffffful,
          RS_FLOAT_ONE, 0u);

    rs_begin();
    rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    /* Each 64-byte row: four dwords of two texels, then padding. */
    for (y = 0u; y < 8u; ++y) {
        for (x = 0u; x < 8u; ++x) {
            DWORD value = 0u;

            if (x < 4u) {
                DWORD quadrant = (y >= 4u ? 2u : 0u) + (x >= 2u ? 1u : 0u);

                value = rs_quadrant_texels[quadrant] |
                        (rs_quadrant_texels[quadrant] << 16);
            }
            rs_add(RS_OP_LFB_WRITE32,
                   offset + y * V9X_RS690_TEX_PITCH_ALIGN + x * 4u, value,
                   0u);
        }
    }
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (v9x_rs690_build_3d_state(&state, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return;
    }
    rs_add_list(&list);
    (void)v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_QUADS, vertices, 4u,
                                     &list);
    rs_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
    rs_add_list(&list);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_report_refusal(name);
        rs_failed = 1u;
        return;
    }
    rs_write_hex(rs_key(key, name, "WaitBefore"),
                 rs_result_buffer.ops[before_index].result);
    rs_write_hex(rs_key(key, name, "WaitAfter"),
                 rs_result_buffer.ops[after_index].result);
    rs_save_block(letter);

    rs_model_prefill();
    for (y = 0u; y < side; ++y) {
        for (x = 0u; x < side; ++x) {
            DWORD quadrant = (y * 2u >= side ? 2u : 0u) +
                             (x * 2u >= side ? 1u : 0u);
            DWORD argb = quadrant == 0u ? 0xffff0000ul :
                         quadrant == 1u ? 0xff00ff00ul :
                         quadrant == 2u ? 0xff0000fful : 0xfffffffful;

            rs_set_pixel(rs_model, x, y, rs_encode_argb(argb));
        }
    }
    {
        const unsigned char *actual =
            (const unsigned char *)rs_result_buffer.block;
        DWORD quarter = side / 4u;

        rs_write_hex(rs_key(key, name, "PixelRed"),
                     rs_pixel(actual, quarter, quarter));
        rs_write_hex(rs_key(key, name, "PixelGreen"),
                     rs_pixel(actual, 3u * quarter, quarter));
        rs_write_hex(rs_key(key, name, "PixelBlue"),
                     rs_pixel(actual, quarter, 3u * quarter));
        rs_write_hex(rs_key(key, name, "PixelWhite"),
                     rs_pixel(actual, 3u * quarter, 3u * quarter));
    }
    rs_result(name, rs_compare_model(name) == 0u);
}

/* A 2D fill after the 3D tests, byte-exact, as the 2D probe's Fill. */
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

/* Non-zero when the command line holds the word (a plain substring
 * search; the probe's switches do not prefix one another). */
static int rs_has_switch(const char *word)
{
    const char *line = GetCommandLineA();
    int length = lstrlenA(word);

    for (; *line != '\0'; ++line) {
        int index = 0;

        while (index < length && line[index] == word[index]) {
            ++index;
        }
        if (index == length) {
            return 1;
        }
    }
    return 0;
}

/*
 * /mc: where the memory controller puts the frame buffer, read through
 * MC_INDEX / MC_DATA as Linux rs690_mc_rreg does, the index left at 7Fh
 * after. MCCFG_FB_LOCATION (100h) is the GPU-side window; K8_FB_LOCATION
 * (1Eh) with MC_MISC_UMA_CNTL's K8_ADDR_EXT (5Fh, 7:0) is the system
 * physical address the UMA frame buffer occupies (rs690.c:164-182). The
 * VxD refuses an index write with MC_IND_WR_EN, so nothing is written to
 * the memory controller.
 */
#define RS_MC_INDEX 0x0078ul
#define RS_MC_DATA 0x007cul
#define RS_MC_INDEX_IDLE 0x7ful

static void rs_mc_report(void)
{
    static const DWORD registers[3] = { 0x100ul, 0x01eul, 0x05ful };
    static const char *const names[3] = {
        "McCfgFbLocation", "K8FbLocation", "McMiscUmaCntl"
    };
    DWORD reads[3];
    DWORD index;

    rs_begin();
    for (index = 0u; index < 3u; ++index) {
        rs_add(RS_OP_MMIO_WRITE32, RS_MC_INDEX, registers[index], 0u);
        reads[index] = rs_add(RS_OP_MMIO_READ32, RS_MC_DATA, 0u, 0u);
    }
    rs_add(RS_OP_MMIO_WRITE32, RS_MC_INDEX, RS_MC_INDEX_IDLE, 0u);
    if (!rs_run()) {
        rs_write("Mc", "NOT-RUN");
        rs_report_refusal("Mc");
        return;
    }
    for (index = 0u; index < 3u; ++index) {
        rs_write_hex(names[index], rs_result_buffer.ops[reads[index]].result);
    }
    /* MCCFG_FB_LOCATION: start in 15:0, top in 31:16, both >> 16. */
    rs_write_hex("McFbStart",
                 (rs_result_buffer.ops[reads[0]].result & 0xfffful) << 16);
    rs_write_hex("McFbTop",
                 (rs_result_buffer.ops[reads[0]].result >> 16) << 16);
    rs_write_hex("K8PhysicalHigh",
                 rs_result_buffer.ops[reads[2]].result & 0xfful);
    rs_write_hex("K8PhysicalLow", rs_result_buffer.ops[reads[1]].result);
}

/*
 * /physscan: 8 MiB of system RAM at physical 05C00000h, around the
 * 06000000h-0609FFFFh where the T15 hunt found the second Direct3D
 * process's pixels, read (never written)
 * a page at a time through the VxD. Per page: a checksum, the words whose
 * low three bytes are 00FF00h (that process's green written RGB-only over
 * a kept top byte) and the zero words. A scan before and one after a
 * Direct3D program show whether it wrote here without owning the frames.
 */
#define RS_OP_PHYS_READ_BLOCK 10ul
#define RS_PHYS_WINDOW_BASE 0x05c00000ul
#define RS_PHYS_PAGES 2048ul
#define RS_PAGE_DWORDS 1024ul

static void rs_phys_scan(void)
{
    char key[16];
    char text[48];
    DWORD page;
    DWORD green_total = 0u;
    DWORD zero_total = 0u;
    DWORD green_pages = 0u;

    for (page = 0u; page < RS_PHYS_PAGES; ++page) {
        DWORD checksum = 0u;
        DWORD green = 0u;
        DWORD zeros = 0u;
        DWORD index;

        rs_begin();
        rs_add(RS_OP_PHYS_READ_BLOCK, page * 4096u, 0u, RS_PAGE_DWORDS);
        if (!rs_run()) {
            rs_write("PhysScan", "NOT-RUN");
            rs_report_refusal("Phys");
            return;
        }
        for (index = 0u; index < RS_PAGE_DWORDS; ++index) {
            DWORD word = rs_result_buffer.block[index];

            checksum = ((checksum << 5) | (checksum >> 27)) ^ word;
            if ((word & 0x00fffffful) == 0x0000ff00ul) {
                ++green;
            }
            if (word == 0u) {
                ++zeros;
            }
        }
        green_total += green;
        zero_total += zeros;
        if (green != 0u) {
            ++green_pages;
        }
        /* /physdump: a page's non-zero words, offset and value, up to 48. */
        if (rs_has_switch("/physdump") && zeros != RS_PAGE_DWORDS) {
            DWORD shown = 0u;

            for (index = 0u; index < RS_PAGE_DWORDS && shown < 48u;
                 ++index) {
                DWORD word = rs_result_buffer.block[index];

                if (word == 0u) {
                    continue;
                }
                rs_hex(key + 1, page * 4096u + index * 4u, 8);
                key[0] = 'W';
                rs_write_hex(key, word);
                ++shown;
            }
        }
        rs_hex(key + 1, RS_PHYS_WINDOW_BASE + page * 4096u, 8);
        key[0] = 'P';
        rs_hex(text, checksum, 8);
        text[8] = ' ';
        rs_decimal(text + 9, green);
        lstrcatA(text, " ");
        rs_decimal(text + lstrlenA(text), zeros);
        rs_write(key, text);
    }
    rs_write_decimal("PhysGreenWords", green_total);
    rs_write_decimal("PhysGreenPages", green_pages);
    rs_write_decimal("PhysZeroWords", zero_total);
}

/* The hex number after word on the command line, or `fallback`. */
static DWORD rs_switch_hex(const char *word, DWORD fallback)
{
    const char *line = GetCommandLineA();
    int length = lstrlenA(word);

    for (; *line != '\0'; ++line) {
        int index = 0;
        DWORD value = 0u;
        int digits = 0;

        while (index < length && line[index] == word[index]) {
            ++index;
        }
        if (index != length) {
            continue;
        }
        line += length;
        for (;; ++line, ++digits) {
            char ch = *line;

            if (ch >= '0' && ch <= '9') {
                value = (value << 4) | (DWORD)(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value = (value << 4) | (DWORD)(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value = (value << 4) | (DWORD)(ch - 'A' + 10);
            } else {
                break;
            }
        }
        return digits != 0 ? value : fallback;
    }
    return fallback;
}

/*
 * /strayat=<hex>: the T15 child's drawing without Direct3D - 40 batches of
 * three green 12x12 quads, TL vertices, the XRGB channel mask on, at
 * 32 bpp, into a 640-pixel-wide, 2560-byte-pitch colour buffer at that
 * frame buffer offset, each batch ended by the finish - and nothing read
 * back. A /physscan after it shows whether lines went to system RAM
 * (docs\issues\2026-11-05-rs690-t15-heap-words-zeroed-once.md). At offset 0
 * it draws over the desktop.
 */
#define RS_STRAY_PITCH 2560ul
#define RS_STRAY_WIDTH 640ul
#define RS_STRAY_HEIGHT 96ul
#define RS_STRAY_BATCHES 40ul

static void rs_stray_draw(DWORD offset)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt fill_init;
    struct v9x_rs690_blt fill_blt;
    DWORD vertices[12 * 8];
    DWORD batch;
    DWORD quad;
    DWORD with_fill;

    /*
     * /strayfill: a 2D engine fill of the whole target before each batch,
     * the child's DDBLT_COLORFILL-then-draw order.
     */
    with_fill = rs_has_switch("/strayfill") ? 1u : 0u;
    if (with_fill != 0u) {
        rs_zero(&fill, sizeof(fill));
        fill.fb_gpu_base = rs_fb_gpu_base;
        fill.fb_bytes = TEST_FB;
        fill.target_offset = offset;
        fill.pitch_bytes = RS_STRAY_PITCH;
        fill.bytes_per_pixel = 4u;
        fill.width = RS_STRAY_WIDTH;
        fill.height = RS_STRAY_HEIGHT;
        fill.color = 0ul;
        if (v9x_rs690_build_init(rs_read_register(V9X_RS690_DP_DATATYPE),
                                 &fill_init) != V9X_STATUS_OK ||
            v9x_rs690_build_fill(&fill, &fill_blt) != V9X_STATUS_OK) {
            rs_write("Stray", "FILL-REFUSED");
            return;
        }
    }
    rs_write_decimal("StrayFill", with_fill);

    rs_zero(&state, sizeof(state));
    state.target.fb_gpu_base = rs_fb_gpu_base;
    state.target.fb_bytes = TEST_FB;
    state.target.offset = offset;
    state.target.pitch_bytes = RS_STRAY_PITCH;
    state.target.bytes_per_pixel = 4u;
    state.target.width = RS_STRAY_WIDTH;
    state.target.height = RS_STRAY_HEIGHT;
    state.x_channel = 1u;
    if (v9x_rs690_build_3d_state(&state, &list) != V9X_STATUS_OK) {
        rs_write("Stray", "STATE-REFUSED");
        return;
    }
    for (batch = 0u; batch < RS_STRAY_BATCHES; ++batch) {
        rs_begin();
        rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
        if (with_fill != 0u) {
            rs_add_blt(&fill_init);
            rs_add_blt(&fill_blt);
            rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
        }
        (void)v9x_rs690_build_3d_state(&state, &list);
        rs_add_list(&list);
        for (quad = 0u; quad < 3u; ++quad) {
            DWORD index = (batch * 3u + quad) % 170u;
            DWORD x = (index % 34u) * 16u + 2u;
            DWORD y = (index / 34u) * 16u + 2u;

            rs_tl_quad(vertices + quad * 32u, x, y, x + 12u, y + 12u,
                       0x3f000000ul, 0xff00ff00ul);
        }
        (void)v9x_rs690_build_3d_draw_tl(V9X_RS690_PRIM_QUADS, vertices, 12u,
                                         &list);
        rs_add_list(&list);
        (void)v9x_rs690_build_3d_finish(&list);
        rs_add_list(&list);
        rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
        if (!rs_run()) {
            rs_write("Stray", "NOT-RUN");
            rs_report_refusal("Stray");
            return;
        }
    }
    rs_write_hex("StrayOffset", offset);
    rs_write_decimal("StrayBatches", RS_STRAY_BATCHES);
}

/*
 * /strayhal: the T15 child's frames in the HAL's own order rather than the
 * probe's. Four frames, alternating targets at 12C000h and 0 (the child's
 * back buffer, then the buffer a flip hands it), each: wait idle and the
 * 2D destination cache flush eng_rs690.c's wait_idle writes, the 2D fill
 * of the 640x480 target, then eight draws of the 170-quad volume list as
 * d3d_rs690.c sends them - WAIT_UNTIL 2D idle, the state, triangles in
 * runs of 64 behind VF_CNTL, the finish - with no CPU idle wait between
 * draws. The flip itself is left out: the CRTC is not touched.
 */
#define RS_HAL_FRAMES 4ul
#define RS_HAL_DRAWS 8ul
#define RS_HAL_QUADS 170ul
#define RS_HAL_RUN 64ul
#define RS_HAL_HEIGHT 480ul

static DWORD rs_hal_failed;

/* rs_add, sending the request when it is full so long streams fit. */
static void rs_hal_add(DWORD code, DWORD a, DWORD b)
{
    if (rs_request_buffer.count >= RS_OP_MAX - 1u) {
        if (!rs_run()) {
            rs_hal_failed = 1u;
        }
        rs_begin();
    }
    rs_add(code, a, b, 0u);
}

static void rs_hal_writes(const DWORD *offsets, const DWORD *values,
                          DWORD count)
{
    DWORD index;

    for (index = 0u; index < count; ++index) {
        if (index % RS_FIFO_BATCH == 0u) {
            DWORD batch = count - index;

            if (batch > RS_FIFO_BATCH) {
                batch = RS_FIFO_BATCH;
            }
            rs_hal_add(RS_OP_WAIT_FIFO, batch, 0u);
        }
        rs_hal_add(RS_OP_MMIO_WRITE32, offsets[index], values[index]);
    }
}

/* Corner `corner` (0-5) of quad `index`'s two triangles. */
static void rs_hal_corner(DWORD *vertex, DWORD index, DWORD corner)
{
    static const unsigned char right[6] = { 0u, 0u, 1u, 0u, 1u, 1u };
    static const unsigned char bottom[6] = { 0u, 1u, 1u, 0u, 1u, 0u };
    DWORD x = (index % 34u) * 16u + 2u;
    DWORD y = (index / 34u) * 16u + 2u;

    rs_tl(vertex, x + 12u * right[corner], y + 12u * bottom[corner],
          0x3f000000ul, RS_FLOAT_ONE, 0xff00ff00ul, 0u, 0u);
}

static void rs_stray_hal(void)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_list list;
    struct v9x_rs690_3d_list finish;
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt fill_init;
    struct v9x_rs690_blt fill_blt;
    DWORD vertex[8];
    DWORD dc_ctlstat;
    DWORD frame;
    DWORD draw;
    DWORD triangle;
    DWORD word;
    DWORD idle_between;
    DWORD no_flush;
    DWORD no_fill;
    DWORD one_target;
    DWORD fill_idle;
    DWORD fill_flush;
    DWORD wait_bits;

    /* Bisection switches: a CPU idle wait after every draw, no 2D cache
     * flush, no 2D fill, every frame at offset 0. */
    idle_between = rs_has_switch("/hidle") ? 1u : 0u;
    no_flush = rs_has_switch("/hnoflush") ? 1u : 0u;
    no_fill = rs_has_switch("/hnofill") ? 1u : 0u;
    one_target = rs_has_switch("/hzero") ? 1u : 0u;

    /* Separations after the fill: a CPU idle wait, the 2D destination
     * cache flush (no wait), or WAIT_UNTIL with 2D, 3D and host idle-clean
     * (bits 16-18, Linux RADEON_WAIT_*_IDLECLEAN) for the draws' wait. */
    fill_idle = rs_has_switch("/hfillidle") ? 1u : 0u;
    fill_flush = rs_has_switch("/hfillflush") ? 1u : 0u;
    wait_bits = rs_has_switch("/hwaitall") ? 0x00070000ul :
                V9X_RS690_WAIT_2D_IDLECLEAN;
    rs_write_decimal("StrayHalFillIdle", fill_idle);
    rs_write_decimal("StrayHalFillFlush", fill_flush);
    rs_write_hex("StrayHalWaitUntil", wait_bits);
    rs_write_decimal("StrayHalIdle", idle_between);
    rs_write_decimal("StrayHalNoFlush", no_flush);
    rs_write_decimal("StrayHalNoFill", no_fill);
    rs_write_decimal("StrayHalZero", one_target);

    dc_ctlstat = rs_read_register(V9X_RS690_DSTCACHE_CTLSTAT);
    if (v9x_rs690_build_init(rs_read_register(V9X_RS690_DP_DATATYPE),
                             &fill_init) != V9X_STATUS_OK) {
        rs_write("StrayHal", "INIT-REFUSED");
        return;
    }
    (void)v9x_rs690_build_3d_finish(&finish);
    rs_hal_failed = 0u;
    rs_begin();
    rs_hal_add(RS_OP_WAIT_IDLE, 0u, 0u);
    rs_hal_writes(fill_init.offsets, fill_init.values, fill_init.count);

    for (frame = 0u; frame < RS_HAL_FRAMES; ++frame) {
        DWORD offset =
            ((frame & 1u) == 0u && one_target == 0u) ? 0x12c000ul : 0ul;

        /* eng_rs690.c's wait_idle with the engine dirty: idle, then the
         * 2D destination cache flush, then the fill. */
        rs_hal_add(RS_OP_WAIT_IDLE, 0u, 0u);
        if (no_flush == 0u) {
            rs_hal_add(RS_OP_MMIO_WRITE32, V9X_RS690_DSTCACHE_CTLSTAT,
                       dc_ctlstat | V9X_RS690_DC_FLUSH_ALL);
            rs_hal_add(RS_OP_WAIT_IDLE, 0u, 0u);
        }
        rs_zero(&fill, sizeof(fill));
        fill.fb_gpu_base = rs_fb_gpu_base;
        fill.fb_bytes = TEST_FB;
        fill.target_offset = offset;
        fill.pitch_bytes = RS_STRAY_PITCH;
        fill.bytes_per_pixel = 4u;
        fill.width = RS_STRAY_WIDTH;
        fill.height = RS_HAL_HEIGHT;
        fill.color = 0ul;
        if (v9x_rs690_build_fill(&fill, &fill_blt) != V9X_STATUS_OK) {
            rs_write("StrayHal", "FILL-REFUSED");
            return;
        }
        if (no_fill == 0u) {
            rs_hal_writes(fill_blt.offsets, fill_blt.values, fill_blt.count);
        }
        if (fill_idle != 0u) {
            rs_hal_add(RS_OP_WAIT_IDLE, 0u, 0u);
        }
        if (fill_flush != 0u) {
            rs_hal_add(RS_OP_WAIT_FIFO, 1u, 0u);
            rs_hal_add(RS_OP_MMIO_WRITE32, V9X_RS690_DSTCACHE_CTLSTAT,
                       dc_ctlstat | V9X_RS690_DC_FLUSH_ALL);
        }

        rs_zero(&state, sizeof(state));
        state.target.fb_gpu_base = rs_fb_gpu_base;
        state.target.fb_bytes = TEST_FB;
        state.target.offset = offset;
        state.target.pitch_bytes = RS_STRAY_PITCH;
        state.target.bytes_per_pixel = 4u;
        state.target.width = RS_STRAY_WIDTH;
        state.target.height = RS_HAL_HEIGHT;
        state.x_channel = 1u;
        if (v9x_rs690_build_3d_state(&state, &list) != V9X_STATUS_OK) {
            rs_write("StrayHal", "STATE-REFUSED");
            return;
        }

        for (draw = 0u; draw < RS_HAL_DRAWS; ++draw) {
            rs_hal_add(RS_OP_WAIT_FIFO, 1u, 0u);
            rs_hal_add(RS_OP_MMIO_WRITE32, V9X_RS690_WAIT_UNTIL,
                       wait_bits);
            rs_hal_writes(list.offsets, list.values, list.count);
            for (triangle = 0u; triangle < RS_HAL_QUADS * 2u; ++triangle) {
                DWORD corner;

                if (triangle % RS_HAL_RUN == 0u) {
                    DWORD run = RS_HAL_QUADS * 2u - triangle;

                    if (run > RS_HAL_RUN) {
                        run = RS_HAL_RUN;
                    }
                    rs_hal_add(RS_OP_WAIT_FIFO, 1u, 0u);
                    rs_hal_add(RS_OP_MMIO_WRITE32, V9X_RS690_VAP_VF_CNTL,
                               v9x_rs690_3d_vf_cntl(V9X_RS690_PRIM_TRIANGLES,
                                                    run * 3u));
                }
                for (corner = 0u; corner < 3u; ++corner) {
                    rs_hal_corner(vertex, triangle / 2u,
                                  (triangle & 1u) * 3u + corner);
                    rs_hal_add(RS_OP_WAIT_FIFO, 8u, 0u);
                    for (word = 0u; word < 8u; ++word) {
                        rs_hal_add(RS_OP_MMIO_WRITE32,
                                   V9X_RS690_SE_PORT_DATA0, vertex[word]);
                    }
                }
            }
            rs_hal_writes(finish.offsets, finish.values, finish.count);
            if (idle_between != 0u) {
                rs_hal_add(RS_OP_WAIT_IDLE, 0u, 0u);
            }
        }
    }

    /* The flip's wait_idle and flush, without the flip. */
    rs_hal_add(RS_OP_WAIT_IDLE, 0u, 0u);
    rs_hal_add(RS_OP_MMIO_WRITE32, V9X_RS690_DSTCACHE_CTLSTAT,
               dc_ctlstat | V9X_RS690_DC_FLUSH_ALL);
    rs_hal_add(RS_OP_WAIT_IDLE, 0u, 0u);
    if (!rs_run()) {
        rs_hal_failed = 1u;
    }
    if (rs_hal_failed != 0u) {
        rs_write("StrayHal", "NOT-RUN");
        rs_report_refusal("StrayHal");
        return;
    }
    rs_write_decimal("StrayHalFrames", RS_HAL_FRAMES);
}

static void rs_close(DWORD code)
{
    if (rs_device != INVALID_HANDLE_VALUE && rs_device != 0) {
        CloseHandle(rs_device);
    }
    CloseHandle(rs_output);
    ExitProcess(code);
}

void WINAPI V9xRs6903dProbeEntry(void)
{
    static const char header[] = "[Rs6903dProbe]\r\n";
    struct v9x_rs690_3d_list init;
    HDC display;
    DWORD returned;
    DWORD desktop_bytes = 0u;
    DWORD desktop_bpp = 0u;
    DWORD csq;
    DWORD pipe_select;
    DWORD before_index;
    DWORD after_index;

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
    rs_write_decimal("SurfacePitch", rs_pitch);
    rs_write_decimal("SurfaceHeight", rs_height);
    if (desktop_bytes == 0u || desktop_bytes >= TEST_BASE) {
        rs_write("Result", "REFUSED-DESKTOP");
        rs_close(1u);
    }

    rs_device = CreateFileA("\\\\.\\RS3D.VXD", 0, 0, 0, CREATE_NEW,
                            FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (rs_device == INVALID_HANDLE_VALUE) {
        rs_write("Result", "VXD-NOT-LOADED");
        rs_close(2u);
    }

    /* First contact, read-only. */
    csq = rs_read_register(V9X_RS690_CP_CSQ_CNTL);
    rs_write_hex("VxdStatus", rs_result_buffer.status);
    rs_write_hex("ConfigId", rs_result_buffer.config_id);
    rs_write_hex("MemSize", rs_result_buffer.memsize);
    rs_write_hex("Bar0", rs_result_buffer.bar0);
    rs_write_hex("Bar2", rs_result_buffer.bar2);
    if ((rs_result_buffer.status & RS_MAPPED) == 0ul) {
        rs_write("Result", "NOT-MAPPED");
        rs_close(3u);
    }
    rs_write_hex("CpCsqCntl", csq);
    rs_write_hex("RbbmStatusAtStart",
                 rs_read_register(V9X_RS690_RBBM_STATUS));
    pipe_select = rs_read_register(V9X_RS690_GB_PIPE_SELECT);
    rs_write_hex("GbPipeSelect", pipe_select);
    rs_write_hex("GbTileConfigAtStart",
                 rs_read_register(V9X_RS690_GB_TILE_CONFIG));
    rs_write_hex("VapCntlStatusAtStart",
                 rs_read_register(V9X_RS690_VAP_CNTL_STATUS));
    if (v9x_rs690_cp_off(csq) == 0ul) {
        rs_write("Result", "REFUSED-CP-OWNS-ENGINE");
        rs_close(3u);
    }
    rs_fb_gpu_base =
        v9x_rs690_fb_gpu_base(rs_read_register(V9X_RS690_HDP_FB_LOCATION));
    rs_write_hex("FbGpuBase", rs_fb_gpu_base);
    if (rs_has_switch("/mc")) {
        rs_mc_report();
        rs_write("Result", "MC-ONLY");
        rs_close(0u);
    }
    if (rs_has_switch("/physscan")) {
        rs_phys_scan();
        rs_write("Result", "PHYS-SCAN-ONLY");
        rs_close(0u);
    }
    if (rs_has_switch("/strayat=")) {
        struct v9x_rs690_3d_list stray_init;

        (void)v9x_rs690_build_3d_init(
            pipe_select, rs_read_register(V9X_RS690_RB2D_DSTCACHE_MODE),
            &stray_init);
        rs_begin();
        rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
        rs_add_list(&stray_init);
        rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
        if (!rs_run()) {
            rs_write("Result", "INIT-NOT-RUN");
            rs_close(3u);
        }
        if (rs_has_switch("/strayhal")) {
            rs_stray_hal();
        } else {
            rs_stray_draw(rs_switch_hex("/strayat=", 0xfffffffful));
        }
        rs_write("Result", "STRAY-ONLY");
        rs_close(0u);
    }
    if (rs_fb_gpu_base == 0ul || pipe_select == 0xfffffffful) {
        rs_write("Result", "REFUSED-FB-BASE");
        rs_close(3u);
    }

    /* The 3D init list, once. */
    (void)v9x_rs690_build_3d_init(
        pipe_select, rs_read_register(V9X_RS690_RB2D_DSTCACHE_MODE), &init);
    rs_begin();
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_list(&init);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (!rs_run()) {
        rs_write("Init", "NOT-RUN");
        rs_report_refusal("Init");
        rs_write("Result", "INIT-NOT-IDLE");
        rs_close(3u);
    }
    rs_write_hex("InitWaitBefore", rs_result_buffer.ops[before_index].result);
    rs_write_hex("InitWaitAfter", rs_result_buffer.ops[after_index].result);
    rs_write_hex("GbTileConfigAfterInit",
                 rs_read_register(V9X_RS690_GB_TILE_CONFIG));
    rs_write_hex("VapCntlStatusAfterInit",
                 rs_read_register(V9X_RS690_VAP_CNTL_STATUS));

    rs_quad_test("Quad", 'Q');
    rs_triangle_test("Triangle", 0, 'T', 0);
    rs_triangle_test("Gouraud", 1, 'G', 0);
    rs_tex_exact_test("TexDecal", 'D', V9X_RS690_TEXADDR_CLAMP, 4u,
                      4u + TEX_WIDTH, v9x_rs690_float_integer(1u));
    rs_tex_exact_test("TexWrap", 'W', V9X_RS690_TEXADDR_WRAP, 0u, TEST_WIDTH,
                      v9x_rs690_float_integer(2u));
    rs_tex_modulate_test();
    rs_tex_bilinear_test();
    /* The TL-vertex state, as the HAL will use it. */
    rs_triangle_test("TlGouraud", 1, 'H', 1);
    rs_z_test();
    rs_blend_test();
    rs_alpha_test();
    rs_cull_test();
    rs_perspective_test();
    rs_tex_format_test("TexFormat1555", 'R', V9X_RS690_TEXFMT_ARGB1555);
    rs_tex_format_test("TexFormat4444", 'S', V9X_RS690_TEXFMT_ARGB4444);
    rs_tex_specular_test();
    rs_tex_quadrants_test("TexQuadrants", 'X', TEX_OFFSET);
    rs_tex_quadrants_test("TexQuadrants500", 'Y', TEX_OFFSET + 0x500u);
    rs_flat_colour_test("TlSpecularOnly", 'E', 0u, 1u, 0u, 0x0000ff00ul,
                        0xffffff00ul);
    rs_flat_colour_test("TlFogFull", 'I', 0u, 0u, 1u, 0x00000000ul,
                        0xff0000fful);
    rs_flat_colour_test("TexFogFull", 'O', 1u, 0u, 1u, 0x00000000ul,
                        0xff0000fful);
    rs_fog_inst_test("FogInstConst", '1', 0x40050a80ul, 0xff0000fful);
    rs_fog_inst_test("FogInstFactor", '2', 0x40050a8eul, 0xff000000ul);
    rs_fog_inst_test("FogInstTemp", '3', 0x40050a84ul, 0xffff0000ul);
    rs_combine_test("TexAdd", '4', V9X_RS690_TEXCOMBINE_ADD, RS_ADD_DIFFUSE,
                    0u);
    rs_combine_test("TexAddSpecular", '5', V9X_RS690_TEXCOMBINE_ADD,
                    RS_ADD_DIFFUSE, RS_ADD_SPECULAR);
    rs_combine_test("TexDecalAlpha", '6', V9X_RS690_TEXCOMBINE_DECALALPHA,
                    RS_DECAL_DIFFUSE, 0u);
    rs_chroma_test("ChromaForce", '7', V9X_RS690_CHROMA_FORCE,
                   rs_chroma_key(RS_CHROMA_REPLICATE));
    rs_chroma_test("ChromaForceZeroFill", '8', V9X_RS690_CHROMA_FORCE,
                   rs_chroma_key(RS_CHROMA_ZERO_FILL));
    rs_chroma_test("ChromaForceRaw", '9', V9X_RS690_CHROMA_FORCE,
                   rs_chroma_key(RS_CHROMA_RAW));
    rs_chroma_test("ChromaForceRawFF", 'C', V9X_RS690_CHROMA_FORCE,
                   rs_chroma_key(RS_CHROMA_RAW_FF));
    rs_chroma_test("ChromaBlend", '0', V9X_RS690_CHROMA_BLEND,
                   rs_chroma_key(RS_CHROMA_REPLICATE));
    /* The same key magnified 2x: nearest should kill the 2x2 block of
     * texel (5, 3), four pixels; bilinear shows what the filter does with
     * a keyed texel among its four. */
    rs_chroma_scale = 2u;
    rs_chroma_test("ChromaNearest2x", '_', V9X_RS690_CHROMA_FORCE,
                   rs_chroma_key(RS_CHROMA_RAW));
    rs_chroma_filter = V9X_RS690_TEXFILTER_LINEAR;
    rs_chroma_test("ChromaLinear2x", '$', V9X_RS690_CHROMA_FORCE,
                   rs_chroma_key(RS_CHROMA_RAW));
    rs_chroma_scale = 1u;
    rs_chroma_filter = V9X_RS690_TEXFILTER_NEAREST;
    rs_mip_layout_test("MipLayoutPitch", '-', 1);
    rs_mip_layout_test("MipLayoutNoPitch", '!', 0);
    rs_mip_select_test();
    rs_mip_linear_test();
    rs_fog_test("TlFog", 0u);
    rs_fog_test("TlFogSpecular", 1u);
    /* The colour-only state again: texturing, Z, blend, alpha test and
     * cull must be off once more. */
    rs_quad_test("QuadAfterTex", 'A');
    rs_fill_after_test();

    rs_write_hex("RbbmStatusAtEnd", rs_read_register(V9X_RS690_RBBM_STATUS));
    rs_write("Result", rs_failed == 0u ? "PASS" : "FAIL");
    rs_close(rs_failed == 0u ? 0u : 1u);
}
