/*
 * V9XR53D.EXE: the ATI Radeon X1300 (RV515) 3D engine draw probe.
 *
 * Phase 5 of docs\plans\ati-rv515-x1300.md, first step: does the R500 3D
 * core, programmed by direct register writes, draw colour-interpolated
 * primitives where a model says, with no CP and no microcode? Built from
 * the RS690's V9XRS3D: its machinery, its Quad, Triangle and Gouraud
 * tests, and its builder (src\chipsets\ati\rs690\rs690_3d.c) for the
 * R300-class state the two chips share. Every list the builder makes goes
 * through rv_r500_fixup() before it is written:
 *
 *   - the R300 rasterizer and shader writes are dropped: RS_IP_0-3
 *     (4310h-431Ch), RS_INST_0-3 (4330h-433Ch), US_CODE_OFFSET to
 *     US_TEX_INST (4608h-462Ch) and the ALU blocks (46C0h-49FCh). On the
 *     R500 those addresses are other registers (4624h is US_FC_CTRL);
 *   - SC_CLIP_0_A/B and SC_SCISSOR0/1 lose the R300's 1440-pixel guard
 *     offset, which the R500 does not have (xf86-video-ati 6.14.6
 *     radeon_exa_render.c, the IS_R300_3D / else scissor branch);
 *
 * and the colour state gets the R500 rasterizer and fragment program in
 * their place (rv_add_r500_colour): R500_RS_IP_0 colour 0 RGBA,
 * R500_RS_INST_0 colour 0 into temp 0, then the program Mesa's r500 emit
 * sequence loads (r300_fs.c): US_CONFIG, US_PIXSIZE, US_FC_CTRL,
 * US_CODE_RANGE / OFFSET / ADDR and one six-dword instruction through
 * GA_US_VECTOR_INDEX / DATA: an OUT instruction computing
 * temp0 * 1 + 0 into render target 0 with RGB and alpha written and
 * clamped. The init list gets R500_VAP_INDEX_OFFSET = 0, the R500 line
 * of RADEONInit3DEngine (radeon_commonfuncs.c:97-104); SU_REG_DEST is
 * already in the RS690's list.
 *
 * The vertex stage is the RS690's PVS bypass. The RV515 has a vertex
 * shader, so whether the VAP takes bypass on it is part of what this
 * measures; Mesa runs R500s without TCL the same way.
 *
 * Publishes C:\V9XDIAG\V9XR53D.TXT and each test's read-back surface as
 * C:\V9XDIAG\R5<x>.BIN. Runs beside R53D.VXD. It refuses to touch the
 * engine unless CP_CSQ_CNTL says both CP queues are off, draws only on a
 * 4 KiB surface at frame buffer offset 16 MiB, and waits for free FIFO
 * entries before every run of writes. Tests:
 *   Quad        - a flat magenta axis-aligned rectangle: byte-exact.
 *   Triangle    - a flat green triangle with one slanted edge; pixels
 *                 within a pixel of that edge are not judged.
 *   Gouraud     - the same triangle with red, green and blue corners,
 *                 each channel within a tolerance of the interpolation.
 *   FillAfter3D - a 2D fill from rs690_2d.c afterwards, byte-exact, so
 *                 the 3D state left behind does not break DirectDraw.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/rs690_2d.h"
#include "velocity9x/rs690_3d.h"
#include "velocity9x/r500_fp.h"
#include "velocity9x/rv515_3d.h"

#include "../../src/chipsets/ati/rs690/rs690_2d.c"
#include "../../src/chipsets/ati/rs690/rs690_3d.c"
#include "../../src/chipsets/ati/rv515/r500_fp.c"
#include "../../src/chipsets/ati/rv515/rv515_3d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define RS_PATH V9X_DIAG_PATH("V9XR53D.TXT")
#define RS_MAGIC 0x44333552ul
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

static void rs_add_blt(const struct v9x_rs690_blt *blt)
{
    rs_add_writes(blt->offsets, blt->values, blt->count);
}

/* memset without the C runtime this tool does not link. */
static void rs_zero(void *memory, DWORD bytes)
{
    unsigned char *at = (unsigned char *)memory;

    while (bytes-- != 0u) {
        *at++ = 0u;
    }
}

/* The RV515 builders' output, one at a time. */
static struct v9x_rv515_3d_list rv_list;

/* Writes an RV515 list (rv515_3d.c) as runs behind FIFO waits. */
static void rv_add(const struct v9x_rv515_3d_list *list)
{
    rs_add_writes(list->offsets, list->values, list->count);
}

/* Writes an RS690 list through the R500 fixup (rv515_3d.c) and returns
 * how many R300-only writes it dropped. */
static DWORD rv_add_list(const struct v9x_rs690_3d_list *list)
{
    static struct v9x_rv515_3d_list fixed;

    v9x_rv515_3d_begin(&fixed);
    v9x_rv515_3d_fixup(list, &fixed);
    rv_add(&fixed);
    return fixed.dropped;
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

    lstrcpyA(path, V9X_DIAG_PATH("R5x.BIN"));
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
    if (v9x_rv515_build_3d_target(&target, &rv_list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TARGET-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_write_decimal(rs_key(key, name, "R300WritesDropped"),
                     rv_list.dropped);
    rv_add(&rv_list);
    if (v9x_rs690_build_3d_draw(primitive, vertices, count, TEST_WIDTH,
                                rs_height, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    (void)rv_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
    (void)rv_add_list(&list);
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
    if (v9x_rv515_build_3d_target(&target, &rv_list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TARGET-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rv_add(&rv_list);
    if (v9x_rv515_build_3d_texture(rs_fb_gpu_base, TEST_FB, &texture,
                                   &rv_list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TEXTURE-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_write_decimal(rs_key(key, name, "R300WritesDropped"),
                     rv_list.dropped);
    rv_add(&rv_list);
    if (v9x_rs690_build_3d_draw_textured(V9X_RS690_PRIM_QUADS, vertices,
                                         count, TEST_WIDTH, rs_height,
                                         &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    (void)rv_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
    (void)rv_add_list(&list);
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
    if (v9x_rv515_build_3d_state(state, &rv_list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "STATE-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_write_decimal(rs_key(key, name, "StateWrites"), rv_list.count);
    rv_add(&rv_list);
    if (v9x_rs690_build_3d_draw_tl(primitive, vertices, count, &list) !=
        V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "DRAW-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    (void)rv_add_list(&list);
    (void)v9x_rs690_build_3d_finish(&list);
    (void)rv_add_list(&list);
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
 * ScissorTest: a white quad over x 4-60, rows 0-15, with the state's
 * scissor (10,2)-(30,10) half-open, as an explicit draw (OpenGL's
 * glScissor) states it. Only the scissor's pixels may change: the
 * builder's SC_SCISSOR0/1, inclusive and guard-offset as the target's,
 * byte-exact against the prefill.
 */
static void rs_scissor_test(void)
{
    static const char name[] = "ScissorTest";
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];

    rs_zero(&state, sizeof(state));
    state.scissor_enable = 1u;
    state.scissor_left = 10u;
    state.scissor_top = 2u;
    state.scissor_right = 30u;
    state.scissor_bottom = 10u;
    rs_tl_quad(vertices, 4u, 0u, 60u, TEX_HEIGHT, 0u, 0xfffffffful);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block('S');
    rs_model_prefill();
    rs_model_rect(10u, 2u, 30u, 10u, rs_encode_argb(0xfffffffful));
    rs_result(name, rs_compare_model(name) == 0u);
}

/*
 * ChannelMaskTest: the same white quad, unscissored, with red masked
 * (glColorMask(FALSE, TRUE, TRUE, ...)): every covered pixel keeps the
 * prefill's red and takes white's green and blue - A7FFh at 16 bpp,
 * FFA5FFFFh at 32 (alpha written). Byte-exact.
 */
static void rs_channel_mask_test(void)
{
    static const char name[] = "ChannelMaskTest";
    struct v9x_rs690_3d_state state;
    DWORD vertices[4 * 8];
    DWORD red_field;
    DWORD white;
    DWORD pixel;
    DWORD x;
    DWORD y;

    rs_zero(&state, sizeof(state));
    state.masked_channels = V9X_RS690_CHANNEL_RED;
    rs_tl_quad(vertices, 4u, 0u, 60u, TEX_HEIGHT, 0u, 0xfffffffful);
    if (!rs_draw_tl(name, &state, V9X_RS690_PRIM_QUADS, vertices, 4u,
                    TEST_PREFILL)) {
        return;
    }
    rs_save_block('W');
    rs_model_prefill();
    red_field = rs_bpp == 2u ? 0x0000f800ul : 0x00ff0000ul;
    white = rs_encode_argb(0xfffffffful);
    for (y = 0u; y < TEX_HEIGHT; ++y) {
        for (x = 4u; x < 60u; ++x) {
            pixel = rs_pixel(rs_model, x, y);
            rs_set_pixel(rs_model, x, y,
                         (pixel & red_field) | (white & ~red_field));
        }
    }
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

static void rs_close(DWORD code)
{
    if (rs_device != INVALID_HANDLE_VALUE && rs_device != 0) {
        CloseHandle(rs_device);
    }
    CloseHandle(rs_output);
    ExitProcess(code);
}

/* Nonzero when the command line holds name (si_mmio_probe_win32.c's). */
static int rv_switch(const char *name)
{
    const char *line = GetCommandLineA();
    int length = lstrlenA(name);

    for (; line != 0 && *line != '\0'; ++line) {
        int index = 0;

        while (index < length && line[index] == name[index]) {
            ++index;
        }
        if (index == length) {
            return 1;
        }
    }
    return 0;
}

/* How many of RV_SAMPLE_READS reads of RBBM_STATUS show GUI_ACTIVE. */
#define RV_SAMPLE_READS 16u

static DWORD rv_busy_samples(DWORD *last)
{
    DWORD busy = 0u;
    DWORD index;
    DWORD value = 0u;

    for (index = 0u; index < RV_SAMPLE_READS; ++index) {
        value = rs_read_register(V9X_RS690_RBBM_STATUS);
        if ((value & 0x80000000ul) != 0ul) {
            ++busy;
        }
    }
    *last = value;
    return busy;
}

/*
 * /watch: RV_WATCH_READS reads of RBBM_STATUS with nothing written. How
 * many showed GUI_ACTIVE, how many GA_BUSY (bit 26, Linux r300d.h), the
 * longest busy run, the number of busy/idle changes, and the first busy
 * value. Takes a second or two of DIOC round trips.
 */
#define RV_WATCH_READS 2000u
#define RV_GA_BUSY     0x04000000ul

static void rv_watch(const char *prefix)
{
    char key[64];
    DWORD index;
    DWORD value;
    DWORD busy = 0u;
    DWORD ga = 0u;
    DWORD run = 0u;
    DWORD longest = 0u;
    DWORD changes = 0u;
    DWORD first_busy = 0u;
    DWORD start_tick = GetTickCount();
    int was_busy = 0;

    for (index = 0u; index < RV_WATCH_READS; ++index) {
        int is_busy;

        value = rs_read_register(V9X_RS690_RBBM_STATUS);
        is_busy = (value & 0x80000000ul) != 0ul;
        if (is_busy) {
            ++busy;
            ++run;
            if (run > longest) {
                longest = run;
            }
            if (first_busy == 0u) {
                first_busy = value;
            }
        } else {
            run = 0u;
        }
        if ((value & RV_GA_BUSY) != 0ul) {
            ++ga;
        }
        if (index != 0u && is_busy != was_busy) {
            ++changes;
        }
        was_busy = is_busy;
    }
    rs_write_decimal(rs_key(key, prefix, "Reads"), RV_WATCH_READS);
    rs_write_decimal(rs_key(key, prefix, "Ms"), GetTickCount() - start_tick);
    rs_write_decimal(rs_key(key, prefix, "Busy"), busy);
    rs_write_decimal(rs_key(key, prefix, "GaBusy"), ga);
    rs_write_decimal(rs_key(key, prefix, "LongestBusyRun"), longest);
    rs_write_decimal(rs_key(key, prefix, "Changes"), changes);
    rs_write_hex(rs_key(key, prefix, "FirstBusyValue"), first_busy);
}

/*
 * /bisect: the R500-fixed init list in chunks of RV_BISECT_CHUNK writes,
 * each chunk its own run ending in a wait for idle. After each chunk:
 * the wait's result, how many of 16 RBBM_STATUS reads were busy, the last
 * read, and DSTCACHE_CTLSTAT. Then VAP_INDEX_OFFSET alone. The first chunk
 * after which the engine stays busy names the write that does it.
 */
#define RV_BISECT_CHUNK 8u

static void rv_bisect_init(DWORD pipe_select)
{
    struct v9x_rs690_3d_list init;
    struct v9x_rs690_3d_list chunk;
    char base[24];
    char key[64];
    char number[12];
    DWORD start;
    DWORD index;
    DWORD wait_index;
    DWORD busy;
    DWORD last;

    (void)v9x_rs690_build_3d_init(
        pipe_select, rs_read_register(V9X_RS690_RB2D_DSTCACHE_MODE), &init);
    busy = rv_busy_samples(&last);
    rs_write_decimal("BisectStartBusy", busy);
    rs_write_hex("BisectStartRbbm", last);
    for (start = 0u; start < init.count; start += RV_BISECT_CHUNK) {
        chunk.count = 0u;
        for (index = start;
             index < init.count && index < start + RV_BISECT_CHUNK;
             ++index) {
            chunk.offsets[chunk.count] = init.offsets[index];
            chunk.values[chunk.count] = init.values[index];
            ++chunk.count;
        }
        rs_begin();
        (void)rv_add_list(&chunk);
        wait_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
        lstrcpyA(base, "Bisect");
        rs_decimal(number, start);
        lstrcatA(base, number);
        if (!rs_run()) {
            rs_write_hex(rs_key(key, base, "Wait"), RS_TIMEOUT);
        } else {
            rs_write_hex(rs_key(key, base, "Wait"),
                         rs_result_buffer.ops[wait_index].result);
        }
        rs_write_hex(rs_key(key, base, "FirstOffset"), init.offsets[start]);
        busy = rv_busy_samples(&last);
        rs_write_decimal(rs_key(key, base, "Busy"), busy);
        rs_write_hex(rs_key(key, base, "Rbbm"), last);
        rs_write_hex(rs_key(key, base, "DstCache"),
                     rs_read_register(V9X_RS690_DSTCACHE_CTLSTAT));
    }
    rs_begin();
    rs_add(RS_OP_WAIT_FIFO, 1u, 0u, 0u);
    rs_add(RS_OP_MMIO_WRITE32, V9X_RV515_VAP_INDEX_OFFSET, 0u, 0u);
    wait_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_write_hex("BisectVapIndexWait",
                 rs_run() ? rs_result_buffer.ops[wait_index].result :
                            RS_TIMEOUT);
    busy = rv_busy_samples(&last);
    rs_write_decimal("BisectEndBusy", busy);
    rs_write_hex("BisectEndRbbm", last);
}

/* /readafter=XXXX's register offset (four hex digits), or 0. */
static DWORD rv_readafter_offset(void)
{
    static const char name[] = "/readafter=";
    const char *line = GetCommandLineA();
    int length = lstrlenA(name);
    DWORD value = 0u;
    int digit;

    for (; line != 0 && *line != '\0'; ++line) {
        int index = 0;

        while (index < length && line[index] == name[index]) {
            ++index;
        }
        if (index != length) {
            continue;
        }
        line += length;
        for (digit = 0; digit < 4; ++digit) {
            char c = line[digit];

            if (c >= '0' && c <= '9') {
                value = value * 16u + (DWORD)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value = value * 16u + (DWORD)(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value = value * 16u + (DWORD)(c - 'A' + 10);
            } else {
                return 0u;
            }
        }
        return value;
    }
    return 0u;
}

/*
 * /readafter=XXXX: the R500-fixed init (in one run, ending idle), a watch,
 * one read of register XXXX, and a watch again. Which read, if any,
 * leaves GA_BUSY set.
 */
static void rv_read_after_init(DWORD pipe_select, DWORD offset)
{
    struct v9x_rs690_3d_list init;
    DWORD wait_index;

    (void)v9x_rs690_build_3d_init(
        pipe_select, rs_read_register(V9X_RS690_RB2D_DSTCACHE_MODE), &init);
    rs_begin();
    (void)rv_add_list(&init);
    rs_add(RS_OP_WAIT_FIFO, 1u, 0u, 0u);
    rs_add(RS_OP_MMIO_WRITE32, V9X_RV515_VAP_INDEX_OFFSET, 0u, 0u);
    wait_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_write_hex("ReadAfterInitWait",
                 rs_run() ? rs_result_buffer.ops[wait_index].result :
                            RS_TIMEOUT);
    rv_watch("BeforeRead");
    rs_write_hex("ReadAfterOffset", offset);
    rs_write_hex("ReadAfterValue", rs_read_register(offset));
    rv_watch("AfterRead");
}

void WINAPI V9xRv5153dProbeEntry(void)
{
    static const char header[] = "[Rv5153dProbe]\r\n";
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

    rs_device = CreateFileA("\\\\.\\R53D.VXD", 0, 0, 0, CREATE_NEW,
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
    /* GB_TILE_CONFIG is never read: on this card a read of it after the
     * 3D init leaves GA_BUSY set for good (/readafter=4018 on M-5CAPM3,
     * boot 43), and a second run of this tool on the same boot would be
     * after the init. /readafter can still read it on purpose. */
    rs_write_hex("VapCntlStatusAtStart",
                 rs_read_register(V9X_RS690_VAP_CNTL_STATUS));
    if (v9x_rs690_cp_off(csq) == 0ul) {
        rs_write("Result", "REFUSED-CP-OWNS-ENGINE");
        rs_close(3u);
    }
    rs_fb_gpu_base =
        v9x_rs690_fb_gpu_base(rs_read_register(V9X_RS690_HDP_FB_LOCATION));
    rs_write_hex("FbGpuBase", rs_fb_gpu_base);
    if (rs_fb_gpu_base == 0ul || pipe_select == 0xfffffffful) {
        rs_write("Result", "REFUSED-FB-BASE");
        rs_close(3u);
    }
    if (rv_switch("/watch")) {
        rv_watch("Watch");
        rs_write("Result", "WATCH-ONLY");
        rs_close(0u);
    }
    if (rv_switch("/bisect")) {
        rv_bisect_init(pipe_select);
        rs_write("Result", "BISECT-ONLY");
        rs_close(0u);
    }
    if (rv_readafter_offset() != 0u) {
        rv_read_after_init(pipe_select, rv_readafter_offset());
        rs_write("Result", "READAFTER-ONLY");
        rs_close(0u);
    }

    /* The 3D init list once (rv515_3d.c: the RS690's through the R500
     * fixup, then R500_VAP_INDEX_OFFSET). */
    (void)v9x_rv515_build_3d_init(
        pipe_select, rs_read_register(V9X_RS690_RB2D_DSTCACHE_MODE),
        &rv_list);
    rs_begin();
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_write_decimal("InitR300WritesDropped", rv_list.dropped);
    rv_add(&rv_list);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (!rs_run()) {
        rs_write("Init", "NOT-RUN");
        rs_report_refusal("Init");
        rs_write("Result", "INIT-NOT-IDLE");
        rs_close(3u);
    }
    rs_write_hex("InitWaitBefore", rs_result_buffer.ops[before_index].result);
    rs_write_hex("InitWaitAfter", rs_result_buffer.ops[after_index].result);
    rs_write_hex("VapCntlStatusAfterInit",
                 rs_read_register(V9X_RS690_VAP_CNTL_STATUS));

    rs_quad_test("Quad", 'Q');
    rs_triangle_test("Triangle", 0, 'T', 0);
    rs_triangle_test("Gouraud", 1, 'G', 0);
    /* Texturing through the R500 program from r500_fp.c. */
    rs_tex_exact_test("TexDecal", 'D', V9X_RS690_TEXADDR_CLAMP, 4u,
                      4u + TEX_WIDTH, v9x_rs690_float_integer(1u));
    rs_tex_exact_test("TexWrap", 'W', V9X_RS690_TEXADDR_WRAP, 0u, TEST_WIDTH,
                      v9x_rs690_float_integer(2u));
    rs_tex_modulate_test();
    rs_tex_bilinear_test();
    /* The TL-vertex state (rv515_3d.c v9x_rv515_build_3d_state), as the
     * HAL will use it. */
    rs_triangle_test("TlGouraud", 1, 'H', 1);
    rs_z_test();
    rs_blend_test();
    rs_alpha_test();
    rs_scissor_test();
    rs_channel_mask_test();
    rs_cull_test();
    rs_perspective_test();
    rs_flat_colour_test("TlSpecularOnly", 'E', 0u, 1u, 0u, 0x0000ff00ul,
                        0xffffff00ul);
    rs_flat_colour_test("TlFogFull", 'I', 0u, 0u, 1u, 0x00000000ul,
                        0xff0000fful);
    rs_flat_colour_test("TexFogFull", 'O', 1u, 0u, 1u, 0x00000000ul,
                        0xff0000fful);
    rs_combine_test("TexAdd", '4', V9X_RS690_TEXCOMBINE_ADD, RS_ADD_DIFFUSE,
                    0u);
    rs_combine_test("TexAddSpecular", '5', V9X_RS690_TEXCOMBINE_ADD,
                    RS_ADD_DIFFUSE, RS_ADD_SPECULAR);
    rs_combine_test("TexDecalAlpha", '6', V9X_RS690_TEXCOMBINE_DECALALPHA,
                    RS_DECAL_DIFFUSE, 0u);
    rs_fog_test("TlFog", 0u);
    rs_fog_test("TlFogSpecular", 1u);
    /* The colour-only state again, after the textured one. */
    rs_quad_test("QuadAfterTex", 'A');
    rs_fill_after_test();

    rs_write_hex("RbbmStatusAtEnd", rs_read_register(V9X_RS690_RBBM_STATUS));
    rs_write("Result", rs_failed == 0u ? "PASS" : "FAIL");
    rs_close(rs_failed == 0u ? 0u : 1u);
}

