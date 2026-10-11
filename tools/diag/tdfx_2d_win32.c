/*
 * TDFX2D.EXE: the 3dfx VSA-100 2D engine write probe.
 *
 * Phase 3 of docs\plans\3dfx-vsa100-family.md, first step: does the 2D
 * engine, programmed with exactly the values src\chipsets\tdfx\tdfx_2d.c
 * builds, draw what a C model of the same operation says, byte for byte?
 * Publishes C:\V9XDIAG\TDFX2D.TXT. Runs beside TDFX2D.VXD, which executes the
 * op lists this side builds.
 *
 * It draws only on a 64x32, 16-bpp surface at VRAM 16 MiB: off the desktop,
 * inside the linear view the BIOS leaves, and in memory the tier-0 driver
 * has given DirectDraw but nothing has allocated while this runs. Every test
 * prefills the surface, waits for the engine to be idle, writes the
 * builder's registers, waits again, reads the whole surface back, and
 * compares.
 *
 * Tests:
 *   FillCC          - a 24x20 fill with ROP0 CCh, the builder's choice.
 *   FillF0          - the same fill with ROP0 F0h, reported but not
 *                     counted: on A8U4I5 (2026-10-11) CCh painted colorFore
 *                     and F0h did not, which is what settled the builder's
 *                     ROP; it stays as the record of that comparison.
 *   CopyForward     - disjoint copy on the surface.
 *   CopyRight       - overlapping copy 4 pixels right: right to left.
 *   CopyDown        - overlapping copy 4 rows down: bottom to top.
 *   FillSmall       - only with /small: an 8x8 fill, which the builder
 *                     precedes with a NOP for the research notes' small-blit
 *                     erratum. Off by default: the erratum says such an
 *                     operation can hang the chip without the NOP.
 *
 * Register meanings: docs\specifications\vsa100-registers.md section 6.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/tdfx_2d.h"

#include "../../src/chipsets/tdfx/tdfx_2d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define TDFX2D_MAGIC 0x44325854ul
#define TDFX2D_OP_MAX 512u
#define TDFX2D_OP_DWORDS 5u
#define TDFX2D_BLOCK_DWORDS 1024u
#define TDFX2D_TIMEOUT 0xfffffffful

#define TDFX2D_OP_MMIO_WRITE32 1ul
#define TDFX2D_OP_MMIO_READ32 2ul
#define TDFX2D_OP_LFB_FILL32 5ul
#define TDFX2D_OP_LFB_GRADIENT 6ul
#define TDFX2D_OP_LFB_READ_BLOCK 7ul
#define TDFX2D_OP_WAIT_IDLE 8ul

#define TDFX2D_PCI_FOUND 0x00000001ul
#define TDFX2D_MAPPED 0x00000002ul
#define TDFX2D_RAN 0x00000004ul

/* The test surface: 64x32 pixels, 16 bpp, 128-byte pitch, at 16 MiB. One
 * surface is exactly one read block. */
#define TEST_BASE 0x01000000ul
#define TEST_WIDTH 64u
#define TEST_HEIGHT 32u
#define TEST_PITCH 128ul
#define TEST_BPP 2ul
#define TEST_DWORDS (TEST_PITCH * TEST_HEIGHT / 4ul)
#define TEST_VRAM 0x01fff000ul
#define TEST_PREFILL 0xa5a5a5a5ul

#define PREFILL_CONSTANT 0
#define PREFILL_GRADIENT 1

struct tdfx2d_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct tdfx2d_request {
    DWORD count;
    struct tdfx2d_op ops[TDFX2D_OP_MAX];
};

struct tdfx2d_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar1;
    DWORD executed;
    DWORD refused;
    struct tdfx2d_op ops[TDFX2D_OP_MAX];
    DWORD block[TDFX2D_BLOCK_DWORDS];
};

static struct tdfx2d_request tdfx2d_request_buffer;
static struct tdfx2d_result tdfx2d_result_buffer;
static unsigned short tdfx2d_model[TEST_HEIGHT][TEST_WIDTH];
static unsigned short tdfx2d_before[TEST_HEIGHT][TEST_WIDTH];
static HANDLE tdfx2d_device;
static HANDLE tdfx2d_output;
static DWORD tdfx2d_failed;

static void tdfx2d_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void tdfx2d_decimal(char *text, DWORD value)
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

static void tdfx2d_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(tdfx2d_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(tdfx2d_output, "=", 1u, &written, 0);
    WriteFile(tdfx2d_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(tdfx2d_output, "\r\n", 2u, &written, 0);
}

static void tdfx2d_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    tdfx2d_hex(text + 2, value, 8);
    tdfx2d_write(key, text);
}

static void tdfx2d_write_decimal(const char *key, DWORD value)
{
    char text[16];

    tdfx2d_decimal(text, value);
    tdfx2d_write(key, text);
}

/* key = prefix + suffix, into a caller buffer of 64 bytes. */
static const char *tdfx2d_key(char *key, const char *prefix,
                              const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
    return key;
}

static int tdfx2d_starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        char left = *text++;
        char right = *prefix++;

        if (left >= 'a' && left <= 'z') {
            left = (char)(left - 32);
        }
        if (right >= 'a' && right <= 'z') {
            right = (char)(right - 32);
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

/* Case-insensitive search for a switch anywhere on the command line. */
static int tdfx2d_has_switch(const char *command_line, const char *name)
{
    if (command_line == 0) {
        return 0;
    }
    while (*command_line != '\0') {
        if (tdfx2d_starts_with_ci(command_line, name)) {
            return 1;
        }
        ++command_line;
    }
    return 0;
}

static void tdfx2d_begin(void)
{
    tdfx2d_request_buffer.count = 0u;
}

static void tdfx2d_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct tdfx2d_op *op;

    if (tdfx2d_request_buffer.count >= TDFX2D_OP_MAX) {
        return;
    }
    op = &tdfx2d_request_buffer.ops[tdfx2d_request_buffer.count++];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0u;
}

/* Non-zero when the VxD ran every op. */
static int tdfx2d_run(void)
{
    DWORD returned = 0u;
    DWORD bytes = 4u + tdfx2d_request_buffer.count *
                       (DWORD)sizeof(struct tdfx2d_op);

    if (!DeviceIoControl(tdfx2d_device, 1u, &tdfx2d_request_buffer, bytes,
                         &tdfx2d_result_buffer, sizeof(tdfx2d_result_buffer),
                         &returned, 0) ||
        returned != sizeof(tdfx2d_result_buffer) ||
        tdfx2d_result_buffer.magic != TDFX2D_MAGIC) {
        return 0;
    }
    return (tdfx2d_result_buffer.status & TDFX2D_RAN) != 0ul &&
           tdfx2d_result_buffer.executed == tdfx2d_request_buffer.count;
}

/* The surface as prefilled, into both model and before-image. */
static void tdfx2d_model_prefill(int kind)
{
    DWORD x;
    DWORD y;

    for (y = 0u; y < TEST_HEIGHT; ++y) {
        for (x = 0u; x < TEST_WIDTH; ++x) {
            unsigned short value = kind == PREFILL_GRADIENT
                ? (unsigned short)((y * TEST_WIDTH + x) & 0xffffu)
                : (unsigned short)(TEST_PREFILL & 0xffffu);

            tdfx2d_model[y][x] = value;
            tdfx2d_before[y][x] = value;
        }
    }
}

static void tdfx2d_model_fill(const struct v9x_tdfx_fill *fill)
{
    DWORD x;
    DWORD y;

    for (y = fill->top; y < fill->top + fill->height; ++y) {
        for (x = fill->left; x < fill->left + fill->width; ++x) {
            tdfx2d_model[y][x] = (unsigned short)fill->color;
        }
    }
}

/* Copy semantics: every destination pixel takes the source pixel as it was
 * before the copy began, whatever the overlap. */
static void tdfx2d_model_copy(const struct v9x_tdfx_copy *copy)
{
    DWORD x;
    DWORD y;

    for (y = 0u; y < copy->height; ++y) {
        for (x = 0u; x < copy->width; ++x) {
            tdfx2d_model[copy->destination_top + y][copy->destination_left + x] =
                tdfx2d_before[copy->source_top + y][copy->source_left + x];
        }
    }
}

/*
 * One test: prefill, wait idle, the builder's writes, wait idle, read back,
 * compare with the model. Publishes <name>Result, Mismatches, the first
 * mismatch, both waits' read counts and the command word.
 */
static void tdfx2d_test(const char *name, int prefill,
                        const struct v9x_tdfx_blt *blt, int counted)
{
    char key[64];
    DWORD index;
    DWORD mismatches = 0u;
    DWORD first = 0xfffffffful;
    DWORD wait_before;
    DWORD wait_after;
    DWORD before_index;
    DWORD after_index;

    tdfx2d_begin();
    if (prefill == PREFILL_GRADIENT) {
        tdfx2d_add(TDFX2D_OP_LFB_GRADIENT, TEST_BASE, 0u, TEST_DWORDS);
    } else {
        tdfx2d_add(TDFX2D_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    }
    before_index = tdfx2d_request_buffer.count;
    tdfx2d_add(TDFX2D_OP_WAIT_IDLE, 0u, 0u, 0u);
    for (index = 0u; index < blt->count; ++index) {
        tdfx2d_add(TDFX2D_OP_MMIO_WRITE32,
                   V9X_TDFX_2D_BASE + blt->offsets[index],
                   blt->values[index], 0u);
    }
    after_index = tdfx2d_request_buffer.count;
    tdfx2d_add(TDFX2D_OP_WAIT_IDLE, 0u, 0u, 0u);
    tdfx2d_add(TDFX2D_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);

    if (!tdfx2d_run()) {
        tdfx2d_write(tdfx2d_key(key, name, "Result"), "NOT-RUN");
        tdfx2d_failed = 1u;
        return;
    }
    wait_before = tdfx2d_result_buffer.ops[before_index].result;
    wait_after = tdfx2d_result_buffer.ops[after_index].result;

    for (index = 0u; index < TEST_DWORDS; ++index) {
        DWORD actual = tdfx2d_result_buffer.block[index];
        DWORD pixel = index * 2u;
        DWORD expected = (DWORD)tdfx2d_model[pixel / TEST_WIDTH][pixel % TEST_WIDTH] |
            ((DWORD)tdfx2d_model[(pixel + 1u) / TEST_WIDTH][(pixel + 1u) % TEST_WIDTH] << 16);

        if (actual != expected) {
            if (first == 0xfffffffful) {
                first = index;
            }
            ++mismatches;
        }
    }

    tdfx2d_write_hex(tdfx2d_key(key, name, "Command"),
                     blt->values[blt->count - 1u]);
    tdfx2d_write_hex(tdfx2d_key(key, name, "WaitBefore"), wait_before);
    tdfx2d_write_hex(tdfx2d_key(key, name, "WaitAfter"), wait_after);
    tdfx2d_write_decimal(tdfx2d_key(key, name, "MismatchDwords"), mismatches);
    if (first != 0xfffffffful) {
        DWORD pixel = first * 2u;
        char text[24];
        char number[12];

        tdfx2d_decimal(text, pixel % TEST_WIDTH);
        lstrcatA(text, ",");
        tdfx2d_decimal(number, pixel / TEST_WIDTH);
        lstrcatA(text, number);
        tdfx2d_write(tdfx2d_key(key, name, "FirstMismatchXY"), text);
        tdfx2d_write_hex(tdfx2d_key(key, name, "FirstMismatchRead"),
                         tdfx2d_result_buffer.block[first]);
    }
    if (wait_before == TDFX2D_TIMEOUT || wait_after == TDFX2D_TIMEOUT) {
        tdfx2d_write(tdfx2d_key(key, name, "Result"), "ENGINE-NOT-IDLE");
        tdfx2d_failed = 1u;
        return;
    }
    tdfx2d_write(tdfx2d_key(key, name, "Result"),
                 mismatches == 0u ? "PASS" : "MISMATCH");
    if (mismatches != 0u && counted) {
        tdfx2d_failed = 1u;
    }
}

static void tdfx2d_fill_test(const char *name, DWORD left, DWORD top,
                             DWORD width, DWORD height, DWORD color,
                             DWORD rop, int counted)
{
    struct v9x_tdfx_fill fill;
    struct v9x_tdfx_blt blt;
    char key[64];

    fill.vram_bytes = TEST_VRAM;
    fill.target_offset = TEST_BASE;
    fill.pitch_bytes = TEST_PITCH;
    fill.bytes_per_pixel = TEST_BPP;
    fill.left = left;
    fill.top = top;
    fill.width = width;
    fill.height = height;
    fill.color = color;
    fill.rop = rop;
    if (v9x_tdfx_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        tdfx2d_write(tdfx2d_key(key, name, "Result"), "BUILDER-REFUSED");
        tdfx2d_failed = 1u;
        return;
    }
    tdfx2d_model_prefill(PREFILL_CONSTANT);
    tdfx2d_model_fill(&fill);
    tdfx2d_test(name, PREFILL_CONSTANT, &blt, counted);
}

static void tdfx2d_copy_test(const char *name, DWORD source_left,
                             DWORD source_top, DWORD destination_left,
                             DWORD destination_top)
{
    struct v9x_tdfx_copy copy;
    struct v9x_tdfx_blt blt;
    char key[64];

    copy.vram_bytes = TEST_VRAM;
    copy.source_offset = TEST_BASE;
    copy.source_pitch_bytes = TEST_PITCH;
    copy.destination_offset = TEST_BASE;
    copy.destination_pitch_bytes = TEST_PITCH;
    copy.bytes_per_pixel = TEST_BPP;
    copy.source_left = source_left;
    copy.source_top = source_top;
    copy.destination_left = destination_left;
    copy.destination_top = destination_top;
    copy.width = 24u;
    copy.height = 20u;
    if (v9x_tdfx_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        tdfx2d_write(tdfx2d_key(key, name, "Result"), "BUILDER-REFUSED");
        tdfx2d_failed = 1u;
        return;
    }
    tdfx2d_model_prefill(PREFILL_GRADIENT);
    tdfx2d_model_copy(&copy);
    tdfx2d_test(name, PREFILL_GRADIENT, &blt, 1);
}

void WINAPI V9xTdfx2dProbeEntry(void)
{
    static const char header[] = "[Tdfx2dProbe]\r\n";
    HDC display;
    DWORD returned;
    DWORD desktop_bytes = 0u;
    DWORD status;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    tdfx2d_output = CreateFileA(V9X_DIAG_TDFX2D_TXT, GENERIC_WRITE,
                                FILE_SHARE_READ, 0, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, 0);
    if (tdfx2d_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(tdfx2d_output, header, (DWORD)lstrlenA(header), &returned, 0);
    tdfx2d_write("Build", V9X_BUILD_ID);

    display = GetDC(0);
    if (display != 0) {
        DWORD width = (DWORD)GetDeviceCaps(display, HORZRES);
        DWORD height = (DWORD)GetDeviceCaps(display, VERTRES);
        DWORD bpp = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                            GetDeviceCaps(display, PLANES));

        tdfx2d_write_decimal("DesktopWidth", width);
        tdfx2d_write_decimal("DesktopHeight", height);
        tdfx2d_write_decimal("DesktopBpp", bpp);
        desktop_bytes = width * height * ((bpp + 7u) / 8u);
        ReleaseDC(0, display);
    }
    /* The desktop must end well below the test surface. */
    if (desktop_bytes == 0u || desktop_bytes >= TEST_BASE) {
        tdfx2d_write("Result", "REFUSED-DESKTOP");
        CloseHandle(tdfx2d_output);
        ExitProcess(1u);
    }

    tdfx2d_device = CreateFileA("\\\\.\\TDFX2D.VXD", 0, 0, 0, CREATE_NEW,
                                FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (tdfx2d_device == INVALID_HANDLE_VALUE) {
        tdfx2d_write("Result", "VXD-NOT-LOADED");
        CloseHandle(tdfx2d_output);
        ExitProcess(2u);
    }

    /* First contact: the status register, read-only. All ones is a card
     * whose decode is off; the tests do not run against it. */
    tdfx2d_begin();
    tdfx2d_add(TDFX2D_OP_MMIO_READ32, 0u, 0u, 0u);
    if (!tdfx2d_run()) {
        tdfx2d_write_hex("VxdStatus", tdfx2d_result_buffer.status);
        tdfx2d_write("Result", "NOT-MAPPED");
        CloseHandle(tdfx2d_device);
        CloseHandle(tdfx2d_output);
        ExitProcess(3u);
    }
    status = tdfx2d_result_buffer.ops[0].result;
    tdfx2d_write_hex("Bar0", tdfx2d_result_buffer.bar0);
    tdfx2d_write_hex("Bar1", tdfx2d_result_buffer.bar1);
    tdfx2d_write_hex("StatusAtStart", status);
    if (status == 0xfffffffful) {
        tdfx2d_write("Result", "DECODE-OFF");
        CloseHandle(tdfx2d_device);
        CloseHandle(tdfx2d_output);
        ExitProcess(3u);
    }

    tdfx2d_fill_test("FillCC", 5u, 4u, 24u, 20u, 0x1234u, 0xccu, 1);
    tdfx2d_fill_test("FillF0", 5u, 4u, 24u, 20u, 0x5678u, 0xf0u, 0);
    tdfx2d_copy_test("CopyForward", 2u, 2u, 34u, 8u);
    tdfx2d_copy_test("CopyRight", 5u, 4u, 9u, 4u);
    tdfx2d_copy_test("CopyDown", 5u, 4u, 5u, 8u);
    if (tdfx2d_has_switch(GetCommandLineA(), "/small")) {
        tdfx2d_fill_test("FillSmall", 5u, 4u, 8u, 8u, 0x4321u, 0xccu, 1);
    }

    /* The status register after everything, as at the start. */
    tdfx2d_begin();
    tdfx2d_add(TDFX2D_OP_MMIO_READ32, 0u, 0u, 0u);
    if (tdfx2d_run()) {
        tdfx2d_write_hex("StatusAtEnd", tdfx2d_result_buffer.ops[0].result);
    }

    tdfx2d_write("Result", tdfx2d_failed == 0u ? "PASS" : "FAIL");
    CloseHandle(tdfx2d_device);
    CloseHandle(tdfx2d_output);
    ExitProcess(tdfx2d_failed == 0u ? 0u : 1u);
}
