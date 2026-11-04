/*
 * V9XRS2D.EXE: the ATI Radeon X1200 IGP (RS690) 2D engine write probe.
 *
 * Phase 3 of docs\plans\ati-rs690-x1200.md, first step: does the 2D
 * engine, programmed with exactly the values src\chipsets\ati\rs690\
 * rs690_2d.c builds, draw what a C model of the same operation says, byte
 * for byte? Publishes C:\V9XDIAG\V9XRS2D.TXT. Runs beside RS2D.VXD, which
 * executes the op lists this side builds. The Rage 128 probe
 * (r128_2d_win32.c) is the model.
 *
 * It refuses to touch the engine unless CP_CSQ_CNTL says both CP queues
 * are off, which is how VBE leaves the chip
 * (docs\decisions\2026-11-04-rs690-register-probe.md), and takes the frame
 * buffer's GPU address from HDP_FB_LOCATION. It writes the builder's init
 * list once (data type, scissor, write mask), then draws only on a 4 KiB
 * test surface at frame buffer offset 16 MiB: off the desktop and above
 * the 16 MiB the tier-0 driver uses, inside the 128 MiB carve-out, so
 * nothing else uses it. Every test prefills the surface, waits for idle
 * (FIFO empty, engine inactive, destination cache flushed), writes the
 * builder's registers, waits again, reads the surface back and compares
 * bytes with the model.
 *
 * The surface is 64 pixels wide at the desktop's depth (8, 16 or 32 bpp);
 * its height fills one 4 KiB read block. Tests:
 *   Fill             - a 24x10 fill.
 *   FillUnaligned    - the same on a surface 16 bytes past a 1 KiB
 *                      boundary, which the builder moves onto x.
 *   CopyForward      - disjoint copy on the surface.
 *   CopyRight        - overlapping copy 4 pixels right: right to left.
 *   CopyDown         - overlapping copy 3 rows down: bottom to top.
 *   CopyDownRight    - both reversed at once.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/rs690_2d.h"

#include "../../src/chipsets/ati/rs690/rs690_2d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define RS_PATH V9X_DIAG_PATH("V9XRS2D.TXT")
#define RS_MAGIC 0x44325352ul
#define RS_OP_MAX 512u
#define RS_OP_DWORDS 5u
#define RS_BLOCK_DWORDS 1024u
#define RS_TIMEOUT 0xfffffffful

#define RS_OP_MMIO_WRITE32 1ul
#define RS_OP_MMIO_READ32 2ul
#define RS_OP_LFB_FILL32 5ul
#define RS_OP_LFB_GRADIENT 6ul
#define RS_OP_LFB_READ_BLOCK 7ul
#define RS_OP_WAIT_IDLE 8ul

#define RS_MAPPED 0x00000002ul
#define RS_RAN 0x00000004ul

/* The test surface: 64 pixels wide, one 4 KiB block in all, at 16 MiB.
 * TEST_FB is the frame buffer the VxD maps, 32 MiB of the 128 MiB. */
#define TEST_BASE 0x01000000ul
#define TEST_BYTES 4096ul
#define TEST_WIDTH 64ul
#define TEST_DWORDS (TEST_BYTES / 4ul)
#define TEST_FB 0x02000000ul
#define TEST_PREFILL 0xa5a5a5a5ul
#define TEST_UNALIGNED 16ul

#define PREFILL_CONSTANT 0
#define PREFILL_GRADIENT 1

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
static unsigned char rs_before[TEST_BYTES];
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

static void rs_add_blt(const struct v9x_rs690_blt *blt)
{
    DWORD index;

    for (index = 0u; index < blt->count; ++index) {
        rs_add(RS_OP_MMIO_WRITE32, blt->offsets[index], blt->values[index],
               0u);
    }
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

/* The surface as the VxD prefills it, into both model and before-image. */
static void rs_model_prefill(int kind)
{
    DWORD index;
    DWORD dword;

    for (index = 0u; index < TEST_BYTES; index += 4u) {
        if (kind == PREFILL_GRADIENT) {
            DWORD i = index / 4u;

            dword = ((i * 2u) & 0xffffu) | ((((i * 2u) + 1u) & 0xffffu) << 16);
        } else {
            dword = TEST_PREFILL;
        }
        rs_model[index] = (unsigned char)dword;
        rs_model[index + 1u] = (unsigned char)(dword >> 8);
        rs_model[index + 2u] = (unsigned char)(dword >> 16);
        rs_model[index + 3u] = (unsigned char)(dword >> 24);
    }
    for (index = 0u; index < TEST_BYTES; ++index) {
        rs_before[index] = rs_model[index];
    }
}

/* base: offset of the surface inside the test block. */
static void rs_model_fill(DWORD base, const struct v9x_rs690_fill *fill)
{
    DWORD x;
    DWORD y;
    DWORD byte;

    for (y = fill->top; y < fill->top + fill->height; ++y) {
        for (x = fill->left; x < fill->left + fill->width; ++x) {
            for (byte = 0u; byte < rs_bpp; ++byte) {
                rs_model[base + y * rs_pitch + x * rs_bpp + byte] =
                    (unsigned char)(fill->color >> (byte * 8u));
            }
        }
    }
}

/* Every destination pixel takes the source pixel as it was before the
 * copy began, whatever the overlap. */
static void rs_model_copy(const struct v9x_rs690_copy *copy)
{
    DWORD x;
    DWORD y;
    DWORD byte;

    for (y = 0u; y < copy->height; ++y) {
        for (x = 0u; x < copy->width; ++x) {
            for (byte = 0u; byte < rs_bpp; ++byte) {
                rs_model[(copy->destination_top + y) * rs_pitch +
                         (copy->destination_left + x) * rs_bpp + byte] =
                    rs_before[(copy->source_top + y) * rs_pitch +
                              (copy->source_left + x) * rs_bpp + byte];
            }
        }
    }
}

/*
 * One test: prefill, wait idle, the builder's writes, wait idle, read back,
 * compare with the model. Publishes <name>Result, the mismatching bytes,
 * the first mismatch and both waits' read counts.
 */
static void rs_test(const char *name, int prefill,
                    const struct v9x_rs690_blt *blt)
{
    char key[64];
    DWORD index;
    DWORD mismatches = 0u;
    DWORD first = 0xfffffffful;
    DWORD wait_before;
    DWORD wait_after;
    DWORD before_index;
    DWORD after_index;
    const unsigned char *actual;

    rs_begin();
    if (prefill == PREFILL_GRADIENT) {
        rs_add(RS_OP_LFB_GRADIENT, TEST_BASE, 0u, TEST_DWORDS);
    } else {
        rs_add(RS_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    }
    before_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_blt(blt);
    after_index = rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add(RS_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);

    if (!rs_run()) {
        rs_write(rs_key(key, name, "Result"), "NOT-RUN");
        rs_write_decimal(rs_key(key, name, "RefusedOp"),
                         rs_result_buffer.refused);
        rs_failed = 1u;
        return;
    }
    wait_before = rs_result_buffer.ops[before_index].result;
    wait_after = rs_result_buffer.ops[after_index].result;

    actual = (const unsigned char *)rs_result_buffer.block;
    for (index = 0u; index < TEST_BYTES; ++index) {
        if (actual[index] != rs_model[index]) {
            if (first == 0xfffffffful) {
                first = index;
            }
            ++mismatches;
        }
    }

    rs_write_hex(rs_key(key, name, "WaitBefore"), wait_before);
    rs_write_hex(rs_key(key, name, "WaitAfter"), wait_after);
    rs_write_decimal(rs_key(key, name, "MismatchBytes"), mismatches);
    if (first != 0xfffffffful) {
        char text[24];
        char number[12];

        rs_decimal(text, (first % rs_pitch) / rs_bpp);
        lstrcatA(text, ",");
        rs_decimal(number, first / rs_pitch);
        lstrcatA(text, number);
        rs_write(rs_key(key, name, "FirstMismatchXY"), text);
        rs_write_hex(rs_key(key, name, "FirstMismatchRead"),
                     rs_result_buffer.block[first / 4u]);
    }
    if (wait_before == RS_TIMEOUT || wait_after == RS_TIMEOUT) {
        rs_write(rs_key(key, name, "Result"), "ENGINE-NOT-IDLE");
        rs_failed = 1u;
        return;
    }
    rs_write(rs_key(key, name, "Result"),
             mismatches == 0u ? "PASS" : "MISMATCH");
    if (mismatches != 0u) {
        rs_failed = 1u;
    }
}

/* A colour that uses every byte of the pixel and differs from the
 * prefill. */
static DWORD rs_colour(void)
{
    if (rs_bpp == 1u) {
        return 0x3cul;
    }
    if (rs_bpp == 2u) {
        return 0x1234ul;
    }
    return 0x00123456ul;
}

static void rs_fill_test(const char *name, DWORD bias)
{
    struct v9x_rs690_fill fill;
    struct v9x_rs690_blt blt;
    char key[64];
    v9x_status status;

    fill.fb_gpu_base = rs_fb_gpu_base;
    fill.fb_bytes = TEST_FB;
    fill.target_offset = TEST_BASE + bias;
    fill.pitch_bytes = rs_pitch;
    fill.bytes_per_pixel = rs_bpp;
    fill.left = 5u;
    fill.top = 3u;
    fill.width = 24u;
    fill.height = 10u;
    fill.color = rs_colour();
    status = v9x_rs690_build_fill(&fill, &blt);
    if (status != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "BUILDER-REFUSED");
        rs_failed = 1u;
        return;
    }
    rs_model_prefill(PREFILL_CONSTANT);
    rs_model_fill(bias, &fill);
    rs_test(name, PREFILL_CONSTANT, &blt);
}

static void rs_copy_test(const char *name, DWORD source_left,
                         DWORD source_top, DWORD destination_left,
                         DWORD destination_top)
{
    struct v9x_rs690_copy copy;
    struct v9x_rs690_blt blt;
    char key[64];

    copy.fb_gpu_base = rs_fb_gpu_base;
    copy.fb_bytes = TEST_FB;
    copy.source_offset = TEST_BASE;
    copy.source_pitch_bytes = rs_pitch;
    copy.destination_offset = TEST_BASE;
    copy.destination_pitch_bytes = rs_pitch;
    copy.bytes_per_pixel = rs_bpp;
    copy.source_left = source_left;
    copy.source_top = source_top;
    copy.destination_left = destination_left;
    copy.destination_top = destination_top;
    copy.width = 24u;
    copy.height = 8u;
    if (v9x_rs690_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        rs_write(rs_key(key, name, "Result"), "BUILDER-REFUSED");
        rs_failed = 1u;
        return;
    }
    rs_model_prefill(PREFILL_GRADIENT);
    rs_model_copy(&copy);
    rs_test(name, PREFILL_GRADIENT, &blt);
}

static void rs_close(DWORD code)
{
    if (rs_device != INVALID_HANDLE_VALUE && rs_device != 0) {
        CloseHandle(rs_device);
    }
    CloseHandle(rs_output);
    ExitProcess(code);
}

void WINAPI V9xRs6902dProbeEntry(void)
{
    static const char header[] = "[Rs6902dProbe]\r\n";
    struct v9x_rs690_blt init;
    HDC display;
    DWORD returned;
    DWORD desktop_bytes = 0u;
    DWORD desktop_bpp = 0u;
    DWORD csq;
    DWORD datatype;

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
    if (rs_bpp != 1u && rs_bpp != 2u && rs_bpp != 4u) {
        rs_write("Result", "REFUSED-DEPTH");
        rs_close(1u);
    }
    rs_pitch = TEST_WIDTH * rs_bpp;
    rs_height = TEST_BYTES / rs_pitch;
    rs_write_decimal("SurfacePitch", rs_pitch);
    rs_write_decimal("SurfaceHeight", rs_height);
    /* The desktop must end well below the test surface. */
    if (desktop_bytes == 0u || desktop_bytes >= TEST_BASE) {
        rs_write("Result", "REFUSED-DESKTOP");
        rs_close(1u);
    }

    rs_device = CreateFileA("\\\\.\\RS2D.VXD", 0, 0, 0, CREATE_NEW,
                            FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (rs_device == INVALID_HANDLE_VALUE) {
        rs_write("Result", "VXD-NOT-LOADED");
        rs_close(2u);
    }

    /* First contact, read-only: the CP's mode, the frame buffer's GPU
     * address and the engine status. */
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
    if (v9x_rs690_cp_off(csq) == 0ul) {
        rs_write("Result", "REFUSED-CP-OWNS-ENGINE");
        rs_close(3u);
    }
    rs_fb_gpu_base =
        v9x_rs690_fb_gpu_base(rs_read_register(V9X_RS690_HDP_FB_LOCATION));
    rs_write_hex("FbGpuBase", rs_fb_gpu_base);
    if (rs_fb_gpu_base == 0ul) {
        rs_write("Result", "REFUSED-FB-BASE");
        rs_close(3u);
    }

    /* The init list. */
    datatype = rs_read_register(V9X_RS690_DP_DATATYPE);
    rs_write_hex("DpDatatypeAtStart", datatype);
    if (v9x_rs690_build_init(datatype, &init) != V9X_STATUS_OK) {
        rs_write("Result", "INIT-REFUSED");
        rs_close(3u);
    }
    rs_begin();
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    rs_add_blt(&init);
    rs_add(RS_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (!rs_run() || rs_result_buffer.ops[0].result == RS_TIMEOUT ||
        rs_result_buffer.ops[rs_request_buffer.count - 1u].result ==
            RS_TIMEOUT) {
        rs_write("Result", "INIT-NOT-IDLE");
        rs_close(3u);
    }

    rs_fill_test("Fill", 0u);
    rs_fill_test("FillUnaligned", TEST_UNALIGNED);
    rs_copy_test("CopyForward", 2u, 2u, 34u, 4u);
    rs_copy_test("CopyRight", 5u, 3u, 9u, 3u);
    rs_copy_test("CopyDown", 5u, 3u, 5u, 6u);
    rs_copy_test("CopyDownRight", 5u, 3u, 9u, 6u);

    rs_write_hex("RbbmStatusAtEnd", rs_read_register(V9X_RS690_RBBM_STATUS));
    rs_write("Result", rs_failed == 0u ? "PASS" : "FAIL");
    rs_close(rs_failed == 0u ? 0u : 1u);
}
