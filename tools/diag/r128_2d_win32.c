/*
 * R1282D.EXE: the ATI Rage 128 2D engine write probe.
 *
 * Phase 3 of docs\plans\ati-rage128.md, first step: does the 2D engine,
 * programmed with exactly the values src\chipsets\ati\rage128\r128_2d.c
 * builds, draw what a C model of the same operation says, byte for byte?
 * Publishes C:\V9XDIAG\R1282D.TXT. Runs beside R1282D.VXD, which executes the
 * op lists this side builds.
 *
 * It refuses to touch the engine unless PM4_BUFFER_CNTL says NONPM4 (direct
 * register access), which is how POST leaves the card and how tier-0 keeps
 * it (docs\decisions\2026-10-22-rage128-register-probe.md). It writes the
 * builder's init list once (scissors, default pitch, write mask), then
 * draws only on a 4 KiB test surface at VRAM 16 MiB: off the desktop and
 * above the 16 MiB the tier-0 driver maps, so nothing else uses it. Every
 * test prefills the surface, waits for idle (FIFO empty, engine inactive,
 * pixel cache flushed), writes the builder's registers, waits again, reads
 * the surface back and compares bytes with the model.
 *
 * The surface is 64 pixels wide at the desktop's depth (8, 16 or 32 bpp);
 * its height fills one 4 KiB read block. Tests:
 *   Fill             - a 24x10 fill.
 *   FillUnaligned    - the same on a surface 16 bytes past a 32-byte
 *                      boundary, which the builder moves onto x.
 *   CopyForward      - disjoint copy on the surface.
 *   CopyRight        - overlapping copy 4 pixels right: right to left.
 *   CopyDown         - overlapping copy 3 rows down: bottom to top.
 *   CopyDownRight    - both reversed at once.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/r128_2d.h"

#include "../../src/chipsets/ati/rage128/r128_2d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define R2_PATH V9X_DIAG_PATH("R1282D.TXT")
#define R2_MAGIC 0x44323252ul
#define R2_OP_MAX 512u
#define R2_OP_DWORDS 5u
#define R2_BLOCK_DWORDS 1024u
#define R2_TIMEOUT 0xfffffffful

#define R2_OP_MMIO_WRITE32 1ul
#define R2_OP_MMIO_READ32 2ul
#define R2_OP_LFB_FILL32 5ul
#define R2_OP_LFB_GRADIENT 6ul
#define R2_OP_LFB_READ_BLOCK 7ul
#define R2_OP_WAIT_IDLE 8ul

#define R2_MAPPED 0x00000002ul
#define R2_RAN 0x00000004ul

/* CRTC_PITCH (022Ch): the primary's pitch in 8-pixel units, 9:0. */
#define R2_CRTC_PITCH 0x022cul
#define R2_CRTC_PITCH_MASK 0x3fful

/* The test surface: 64 pixels wide, one 4 KiB block in all, at 16 MiB. */
#define TEST_BASE 0x01000000ul
#define TEST_BYTES 4096ul
#define TEST_WIDTH 64ul
#define TEST_DWORDS (TEST_BYTES / 4ul)
#define TEST_VRAM 0x02000000ul
#define TEST_PREFILL 0xa5a5a5a5ul
#define TEST_UNALIGNED 16ul

#define PREFILL_CONSTANT 0
#define PREFILL_GRADIENT 1

struct r2_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct r2_request {
    DWORD count;
    struct r2_op ops[R2_OP_MAX];
};

struct r2_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar2;
    DWORD executed;
    DWORD refused;
    DWORD config_id;
    DWORD copy_id;
    struct r2_op ops[R2_OP_MAX];
    DWORD block[R2_BLOCK_DWORDS];
};

static struct r2_request r2_request_buffer;
static struct r2_result r2_result_buffer;
static unsigned char r2_model[TEST_BYTES];
static unsigned char r2_before[TEST_BYTES];
static HANDLE r2_device;
static HANDLE r2_output;
static DWORD r2_failed;
static DWORD r2_bpp;
static DWORD r2_pitch;
static DWORD r2_height;

static void r2_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void r2_decimal(char *text, DWORD value)
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

static void r2_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(r2_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(r2_output, "=", 1u, &written, 0);
    WriteFile(r2_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(r2_output, "\r\n", 2u, &written, 0);
}

static void r2_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    r2_hex(text + 2, value, 8);
    r2_write(key, text);
}

static void r2_write_decimal(const char *key, DWORD value)
{
    char text[16];

    r2_decimal(text, value);
    r2_write(key, text);
}

/* key = prefix + suffix, into a caller buffer of 64 bytes. */
static const char *r2_key(char *key, const char *prefix, const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
    return key;
}

static void r2_begin(void)
{
    r2_request_buffer.count = 0u;
}

/* The index of the op added. */
static DWORD r2_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct r2_op *op;

    if (r2_request_buffer.count >= R2_OP_MAX) {
        return R2_OP_MAX - 1u;
    }
    op = &r2_request_buffer.ops[r2_request_buffer.count];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0u;
    return r2_request_buffer.count++;
}

static void r2_add_blt(const struct v9x_r128_blt *blt)
{
    DWORD index;

    for (index = 0u; index < blt->count; ++index) {
        r2_add(R2_OP_MMIO_WRITE32, blt->offsets[index], blt->values[index],
               0u);
    }
}

/* Non-zero when the VxD ran every op. */
static int r2_run(void)
{
    DWORD returned = 0u;
    DWORD bytes = 4u + r2_request_buffer.count * (DWORD)sizeof(struct r2_op);

    if (!DeviceIoControl(r2_device, 1u, &r2_request_buffer, bytes,
                         &r2_result_buffer, sizeof(r2_result_buffer),
                         &returned, 0) ||
        returned != sizeof(r2_result_buffer) ||
        r2_result_buffer.magic != R2_MAGIC) {
        return 0;
    }
    return (r2_result_buffer.status & R2_RAN) != 0ul &&
           r2_result_buffer.executed == r2_request_buffer.count;
}

/* One register read through the VxD, or 0xffffffff. */
static DWORD r2_read_register(DWORD offset)
{
    r2_begin();
    r2_add(R2_OP_MMIO_READ32, offset, 0u, 0u);
    if (!r2_run()) {
        return 0xfffffffful;
    }
    return r2_result_buffer.ops[0].result;
}

/* The surface as the VxD prefills it, into both model and before-image. */
static void r2_model_prefill(int kind)
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
        r2_model[index] = (unsigned char)dword;
        r2_model[index + 1u] = (unsigned char)(dword >> 8);
        r2_model[index + 2u] = (unsigned char)(dword >> 16);
        r2_model[index + 3u] = (unsigned char)(dword >> 24);
    }
    for (index = 0u; index < TEST_BYTES; ++index) {
        r2_before[index] = r2_model[index];
    }
}

/* base: offset of the surface inside the test block. */
static void r2_model_fill(DWORD base, const struct v9x_r128_fill *fill)
{
    DWORD x;
    DWORD y;
    DWORD byte;

    for (y = fill->top; y < fill->top + fill->height; ++y) {
        for (x = fill->left; x < fill->left + fill->width; ++x) {
            for (byte = 0u; byte < r2_bpp; ++byte) {
                r2_model[base + y * r2_pitch + x * r2_bpp + byte] =
                    (unsigned char)(fill->color >> (byte * 8u));
            }
        }
    }
}

/* Every destination pixel takes the source pixel as it was before the
 * copy began, whatever the overlap. */
static void r2_model_copy(const struct v9x_r128_copy *copy)
{
    DWORD x;
    DWORD y;
    DWORD byte;

    for (y = 0u; y < copy->height; ++y) {
        for (x = 0u; x < copy->width; ++x) {
            for (byte = 0u; byte < r2_bpp; ++byte) {
                r2_model[(copy->destination_top + y) * r2_pitch +
                         (copy->destination_left + x) * r2_bpp + byte] =
                    r2_before[(copy->source_top + y) * r2_pitch +
                              (copy->source_left + x) * r2_bpp + byte];
            }
        }
    }
}

/*
 * One test: prefill, wait idle, the builder's writes, wait idle, read back,
 * compare with the model. Publishes <name>Result, the mismatching bytes,
 * the first mismatch and both waits' read counts.
 */
static void r2_test(const char *name, int prefill,
                    const struct v9x_r128_blt *blt)
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

    r2_begin();
    if (prefill == PREFILL_GRADIENT) {
        r2_add(R2_OP_LFB_GRADIENT, TEST_BASE, 0u, TEST_DWORDS);
    } else {
        r2_add(R2_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    }
    before_index = r2_add(R2_OP_WAIT_IDLE, 0u, 0u, 0u);
    r2_add_blt(blt);
    after_index = r2_add(R2_OP_WAIT_IDLE, 0u, 0u, 0u);
    r2_add(R2_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);

    if (!r2_run()) {
        r2_write(r2_key(key, name, "Result"), "NOT-RUN");
        r2_write_decimal(r2_key(key, name, "RefusedOp"),
                         r2_result_buffer.refused);
        r2_failed = 1u;
        return;
    }
    wait_before = r2_result_buffer.ops[before_index].result;
    wait_after = r2_result_buffer.ops[after_index].result;

    actual = (const unsigned char *)r2_result_buffer.block;
    for (index = 0u; index < TEST_BYTES; ++index) {
        if (actual[index] != r2_model[index]) {
            if (first == 0xfffffffful) {
                first = index;
            }
            ++mismatches;
        }
    }

    r2_write_hex(r2_key(key, name, "WaitBefore"), wait_before);
    r2_write_hex(r2_key(key, name, "WaitAfter"), wait_after);
    r2_write_decimal(r2_key(key, name, "MismatchBytes"), mismatches);
    if (first != 0xfffffffful) {
        char text[24];
        char number[12];

        r2_decimal(text, (first % r2_pitch) / r2_bpp);
        lstrcatA(text, ",");
        r2_decimal(number, first / r2_pitch);
        lstrcatA(text, number);
        r2_write(r2_key(key, name, "FirstMismatchXY"), text);
        r2_write_hex(r2_key(key, name, "FirstMismatchRead"),
                     r2_result_buffer.block[first / 4u]);
    }
    if (wait_before == R2_TIMEOUT || wait_after == R2_TIMEOUT) {
        r2_write(r2_key(key, name, "Result"), "ENGINE-NOT-IDLE");
        r2_failed = 1u;
        return;
    }
    r2_write(r2_key(key, name, "Result"),
             mismatches == 0u ? "PASS" : "MISMATCH");
    if (mismatches != 0u) {
        r2_failed = 1u;
    }
}

/* A colour that uses every byte of the pixel and differs from the
 * prefill. */
static DWORD r2_colour(void)
{
    if (r2_bpp == 1u) {
        return 0x3cul;
    }
    if (r2_bpp == 2u) {
        return 0x1234ul;
    }
    return 0x00123456ul;
}

static void r2_fill_test(const char *name, DWORD bias)
{
    struct v9x_r128_fill fill;
    struct v9x_r128_blt blt;
    char key[64];
    v9x_status status;

    fill.vram_bytes = TEST_VRAM;
    fill.target_offset = TEST_BASE + bias;
    fill.pitch_bytes = r2_pitch;
    fill.bytes_per_pixel = r2_bpp;
    fill.left = 5u;
    fill.top = 3u;
    fill.width = 24u;
    fill.height = 10u;
    fill.color = r2_colour();
    status = v9x_r128_build_fill(&fill, &blt);
    if (status != V9X_STATUS_OK) {
        r2_write(r2_key(key, name, "Result"), "BUILDER-REFUSED");
        r2_failed = 1u;
        return;
    }
    r2_model_prefill(PREFILL_CONSTANT);
    r2_model_fill(bias, &fill);
    r2_test(name, PREFILL_CONSTANT, &blt);
}

static void r2_copy_test(const char *name, DWORD source_left,
                         DWORD source_top, DWORD destination_left,
                         DWORD destination_top)
{
    struct v9x_r128_copy copy;
    struct v9x_r128_blt blt;
    char key[64];

    copy.vram_bytes = TEST_VRAM;
    copy.source_offset = TEST_BASE;
    copy.source_pitch_bytes = r2_pitch;
    copy.destination_offset = TEST_BASE;
    copy.destination_pitch_bytes = r2_pitch;
    copy.bytes_per_pixel = r2_bpp;
    copy.source_left = source_left;
    copy.source_top = source_top;
    copy.destination_left = destination_left;
    copy.destination_top = destination_top;
    copy.width = 24u;
    copy.height = 8u;
    if (v9x_r128_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        r2_write(r2_key(key, name, "Result"), "BUILDER-REFUSED");
        r2_failed = 1u;
        return;
    }
    r2_model_prefill(PREFILL_GRADIENT);
    r2_model_copy(&copy);
    r2_test(name, PREFILL_GRADIENT, &blt);
}

static void r2_close(DWORD code)
{
    if (r2_device != INVALID_HANDLE_VALUE && r2_device != 0) {
        CloseHandle(r2_device);
    }
    CloseHandle(r2_output);
    ExitProcess(code);
}

void WINAPI V9xRage1282dProbeEntry(void)
{
    static const char header[] = "[Rage1282dProbe]\r\n";
    struct v9x_r128_blt init;
    HDC display;
    DWORD returned;
    DWORD desktop_bytes = 0u;
    DWORD desktop_bpp = 0u;
    DWORD pm4;
    DWORD primary_pitch;
    DWORD datatype;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    r2_output = CreateFileA(R2_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (r2_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(r2_output, header, (DWORD)lstrlenA(header), &returned, 0);
    r2_write("Build", V9X_BUILD_ID);

    display = GetDC(0);
    if (display != 0) {
        DWORD width = (DWORD)GetDeviceCaps(display, HORZRES);
        DWORD height = (DWORD)GetDeviceCaps(display, VERTRES);

        desktop_bpp = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                              GetDeviceCaps(display, PLANES));
        r2_write_decimal("DesktopWidth", width);
        r2_write_decimal("DesktopHeight", height);
        r2_write_decimal("DesktopBpp", desktop_bpp);
        desktop_bytes = width * height * ((desktop_bpp + 7u) / 8u);
        ReleaseDC(0, display);
    }
    r2_bpp = (desktop_bpp + 7u) / 8u;
    if (r2_bpp != 1u && r2_bpp != 2u && r2_bpp != 4u) {
        r2_write("Result", "REFUSED-DEPTH");
        r2_close(1u);
    }
    r2_pitch = TEST_WIDTH * r2_bpp;
    r2_height = TEST_BYTES / r2_pitch;
    r2_write_decimal("SurfacePitch", r2_pitch);
    r2_write_decimal("SurfaceHeight", r2_height);
    /* The desktop must end well below the test surface. */
    if (desktop_bytes == 0u || desktop_bytes >= TEST_BASE) {
        r2_write("Result", "REFUSED-DESKTOP");
        r2_close(1u);
    }

    r2_device = CreateFileA("\\\\.\\R1282D.VXD", 0, 0, 0, CREATE_NEW,
                            FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (r2_device == INVALID_HANDLE_VALUE) {
        r2_write("Result", "VXD-NOT-LOADED");
        r2_close(2u);
    }

    /* First contact, read-only: the CCE's mode and the engine status. */
    pm4 = r2_read_register(V9X_R128_PM4_BUFFER_CNTL);
    r2_write_hex("VxdStatus", r2_result_buffer.status);
    r2_write_hex("ConfigId", r2_result_buffer.config_id);
    r2_write_hex("CopyId", r2_result_buffer.copy_id);
    r2_write_hex("Bar0", r2_result_buffer.bar0);
    r2_write_hex("Bar2", r2_result_buffer.bar2);
    if ((r2_result_buffer.status & R2_MAPPED) == 0ul) {
        r2_write("Result", "NOT-MAPPED");
        r2_close(3u);
    }
    r2_write_hex("Pm4BufferCntl", pm4);
    r2_write_hex("GuiStatAtStart", r2_read_register(V9X_R128_GUI_STAT));
    if (v9x_r128_pm4_direct(pm4) == 0ul) {
        r2_write("Result", "REFUSED-CCE-OWNS-ENGINE");
        r2_close(3u);
    }

    /* The init list, against the primary's own pitch. */
    primary_pitch = (r2_read_register(R2_CRTC_PITCH) & R2_CRTC_PITCH_MASK) *
                    V9X_R128_PITCH_PIXEL_UNIT * r2_bpp;
    datatype = r2_read_register(V9X_R128_DP_DATATYPE);
    r2_write_decimal("PrimaryPitch", primary_pitch);
    r2_write_hex("DpDatatypeAtStart", datatype);
    if (v9x_r128_build_init(r2_bpp, primary_pitch, datatype, &init) !=
        V9X_STATUS_OK) {
        r2_write("Result", "INIT-REFUSED");
        r2_close(3u);
    }
    r2_begin();
    r2_add(R2_OP_WAIT_IDLE, 0u, 0u, 0u);
    r2_add_blt(&init);
    r2_add(R2_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (!r2_run() || r2_result_buffer.ops[0].result == R2_TIMEOUT ||
        r2_result_buffer.ops[r2_request_buffer.count - 1u].result ==
            R2_TIMEOUT) {
        r2_write("Result", "INIT-NOT-IDLE");
        r2_close(3u);
    }

    r2_fill_test("Fill", 0u);
    r2_fill_test("FillUnaligned", TEST_UNALIGNED);
    r2_copy_test("CopyForward", 2u, 2u, 34u, 4u);
    r2_copy_test("CopyRight", 5u, 3u, 9u, 3u);
    r2_copy_test("CopyDown", 5u, 3u, 5u, 6u);
    r2_copy_test("CopyDownRight", 5u, 3u, 9u, 6u);

    r2_write_hex("GuiStatAtEnd", r2_read_register(V9X_R128_GUI_STAT));
    r2_write("Result", r2_failed == 0u ? "PASS" : "FAIL");
    r2_close(r2_failed == 0u ? 0u : 1u);
}
