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

#include "../../src/chipsets/ati/rs690/rs690_2d.c"
#include "../../src/chipsets/ati/rs690/rs690_3d.c"

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

/* R500 registers and fields: Mesa 22.3 r300_reg.h, xf86-video-ati 6.14.6
 * radeon_reg.h. */
#define RV_R500_VAP_INDEX_OFFSET    0x208cul
#define RV_R500_RS_IP_0             0x4074ul
#define RV_R500_GA_US_VECTOR_INDEX  0x4250ul
#define RV_R500_GA_US_VECTOR_DATA   0x4254ul
#define RV_R500_RS_INST_0           0x4320ul
#define RV_R500_US_CONFIG           0x4600ul
#define RV_R500_US_PIXSIZE          0x4604ul
#define RV_R500_US_FC_CTRL          0x4624ul
#define RV_R500_US_CODE_ADDR        0x4630ul
#define RV_R500_US_CODE_RANGE       0x4634ul
#define RV_R500_US_CODE_OFFSET      0x4638ul
/* R500_ZERO_TIMES_ANYTHING_EQUALS_ZERO, as Mesa sets US_CONFIG. */
#define RV_R500_US_CONFIG_VALUE     0x00000002ul
/* Highest temporary used, as xf86's R500 composite writes it. */
#define RV_R500_US_PIXSIZE_VALUE    0x00000001ul
/* R500_RS_IP_0: colour pointer 0, format RGBA (both 0), no texture
 * coordinates. R500_RS_INST_0: COL_ID(0) | COL_CN_WRITE | COL_ADDR(0),
 * colour 0 into temporary 0 (Mesa r500_rs_col / r500_rs_col_write). */
#define RV_R500_RS_IP_COLOUR0       0x00000000ul
#define RV_R500_RS_INST_COLOUR0     0x00010000ul
/* GA_US_VECTOR_INDEX: TYPE_INSTR (0) at instruction 0. */
#define RV_R500_VECTOR_INSTR_0      0x00000000ul

/* The R300 guard offset on scan-converter coordinates, and the field
 * layout it is applied to (rs690_3d.h V9X_RS690_SC_OFFSET). */
#define RV_SC_FIELD_MASK            0x00001ffful

/*
 * The one-instruction R500 fragment program: temp0 * 1 + 0 to render
 * target 0, RGB and alpha written and clamped. Field values are Mesa's
 * r300_reg.h R500_INST_* / R500_ALU_* / R500_ALPHA_* defines, built as
 * xf86-video-ati's R500 composite builds its ALU instruction
 * (radeon_exa_render.c:2091-2130), with temp 0 as both sources.
 */
static const DWORD rv_r500_colour_program[6] = {
    /* INST: TYPE_OUT | LAST | RGB_OMASK_R/G/B | ALPHA_OMASK |
     * RGB_CLAMP | ALPHA_CLAMP. */
    0x001f8101ul,
    /* RGB_ADDR: ADDR0/1/2 = temp 0. */
    0x00000000ul,
    /* ALPHA_ADDR: ADDR0/1/2 = temp 0. */
    0x00000000ul,
    /* RGB_INST: SEL_A_SRC0, swizzle A = R G B, SEL_B_SRC0, swizzle B =
     * 1 1 1, TARGET(0). */
    0x00db0220ul,
    /* ALPHA_INST: OP_MAD, ADDRD(0), SEL_A_SRC0 swizzle A, SEL_B_SRC0
     * swizzle 1, TARGET(0). */
    0x00c0c000ul,
    /* RGBA_INST: OP_MAD, ADDRD(0), C swizzles 0 0 0 0. */
    0x20490000ul
};

/* Non-zero for an R300 rasterizer or shader register the R500 must not
 * be written: RS_IP_0-3, RS_INST_0-3, US_CODE_OFFSET to US_TEX_INST_7,
 * and the R300 ALU instruction blocks. */
static int rv_r300_only(DWORD offset)
{
    return (offset >= 0x4310ul && offset <= 0x431cul) ||
           (offset >= 0x4330ul && offset <= 0x433cul) ||
           (offset >= 0x4608ul && offset <= 0x462cul) ||
           (offset >= 0x46c0ul && offset <= 0x49fcul);
}

/* A scan-converter point without the R300's guard offset. */
static DWORD rv_sc_point_r500(DWORD value)
{
    DWORD x = value & RV_SC_FIELD_MASK;
    DWORD y = (value >> V9X_RS690_SC_Y_SHIFT) & RV_SC_FIELD_MASK;
    DWORD rest = value & ~((RV_SC_FIELD_MASK << V9X_RS690_SC_Y_SHIFT) |
                           RV_SC_FIELD_MASK);

    x = x >= V9X_RS690_SC_OFFSET ? x - V9X_RS690_SC_OFFSET : 0ul;
    y = y >= V9X_RS690_SC_OFFSET ? y - V9X_RS690_SC_OFFSET : 0ul;
    return rest | x | (y << V9X_RS690_SC_Y_SHIFT);
}

/* rs_add_list for the R500: R300-only writes dropped, scan-converter
 * points moved off the guard offset. Publishes nothing; the dropped
 * count is returned for the report. */
static DWORD rv_add_list(const struct v9x_rs690_3d_list *list)
{
    DWORD offsets[V9X_RS690_3D_WRITES];
    DWORD values[V9X_RS690_3D_WRITES];
    DWORD count = 0u;
    DWORD dropped = 0u;
    DWORD index;

    for (index = 0u; index < list->count; ++index) {
        DWORD offset = list->offsets[index];
        DWORD value = list->values[index];

        if (rv_r300_only(offset)) {
            ++dropped;
            continue;
        }
        if (offset == V9X_RS690_SC_CLIP_0_A ||
            offset == V9X_RS690_SC_CLIP_0_B ||
            offset == V9X_RS690_SC_SCISSOR0 ||
            offset == V9X_RS690_SC_SCISSOR1) {
            value = rv_sc_point_r500(value);
        }
        offsets[count] = offset;
        values[count] = value;
        ++count;
    }
    rs_add_writes(offsets, values, count);
    return dropped;
}

/* The R500 rasterizer and fragment program for the colour-only state. */
static void rv_add_r500_colour(void)
{
    static const DWORD offsets[] = {
        RV_R500_RS_IP_0, RV_R500_RS_INST_0,
        RV_R500_US_CONFIG, RV_R500_US_PIXSIZE, RV_R500_US_FC_CTRL,
        RV_R500_US_CODE_RANGE, RV_R500_US_CODE_OFFSET, RV_R500_US_CODE_ADDR,
        RV_R500_GA_US_VECTOR_INDEX,
        RV_R500_GA_US_VECTOR_DATA, RV_R500_GA_US_VECTOR_DATA,
        RV_R500_GA_US_VECTOR_DATA, RV_R500_GA_US_VECTOR_DATA,
        RV_R500_GA_US_VECTOR_DATA, RV_R500_GA_US_VECTOR_DATA
    };
    DWORD values[sizeof(offsets) / sizeof(offsets[0])];
    DWORD index;

    values[0] = RV_R500_RS_IP_COLOUR0;
    values[1] = RV_R500_RS_INST_COLOUR0;
    values[2] = RV_R500_US_CONFIG_VALUE;
    values[3] = RV_R500_US_PIXSIZE_VALUE;
    values[4] = 0u;
    values[5] = 0u;     /* CODE_RANGE: address 0, size 0 */
    values[6] = 0u;
    values[7] = 0u;     /* CODE_ADDR: start 0, end 0 */
    values[8] = RV_R500_VECTOR_INSTR_0;
    for (index = 0u; index < 6u; ++index) {
        values[9u + index] = rv_r500_colour_program[index];
    }
    rs_add_writes(offsets, values, sizeof(offsets) / sizeof(offsets[0]));
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
    if (v9x_rs690_build_3d_target(&target, &list) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "TARGET-REFUSED");
        rs_failed = 1u;
        return 0;
    }
    rs_write_decimal(rs_key(key, name, "R300WritesDropped"),
                     rv_add_list(&list));
    rv_add_r500_colour();
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
 * The RS690 probe's TL-vertex variant is left out: it needs the textured
 * state, whose fragment program is R300 code.
 */
static void rs_triangle_test(const char *name, int gouraud, char letter)
{
    struct v9x_rs690_3d_vertex vertices[3];
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
    }
    if (!rs_draw(name, V9X_RS690_PRIM_TRIANGLES, vertices, 3u)) {
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

void WINAPI V9xRv5153dProbeEntry(void)
{
    static const char header[] = "[Rv5153dProbe]\r\n";
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
    if (rs_fb_gpu_base == 0ul || pipe_select == 0xfffffffful) {
        rs_write("Result", "REFUSED-FB-BASE");
        rs_close(3u);
    }

    /* The 3D init list once, through the R500 fixup, then the R500 line
     * of RADEONInit3DEngine. */
    (void)v9x_rs690_build_3d_init(
        pipe_select, rs_read_register(V9X_RS690_RB2D_DSTCACHE_MODE), &init);
    rs_begin();
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_write_decimal("InitR300WritesDropped", rv_add_list(&init));
    rs_add(RS_OP_WAIT_FIFO, 1u, 0u, 0u);
    rs_add(RS_OP_MMIO_WRITE32, RV_R500_VAP_INDEX_OFFSET, 0u, 0u);
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
    rs_triangle_test("Triangle", 0, 'T');
    rs_triangle_test("Gouraud", 1, 'G');
    rs_fill_after_test();

    rs_write_hex("RbbmStatusAtEnd", rs_read_register(V9X_RS690_RBBM_STATUS));
    rs_write("Result", rs_failed == 0u ? "PASS" : "FAIL");
    rs_close(rs_failed == 0u ? 0u : 1u);
}

