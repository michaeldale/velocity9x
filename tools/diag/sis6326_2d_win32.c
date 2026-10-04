/*
 * SIS2D.EXE: the SiS 6326 2D engine write probe.
 *
 * Publishes C:\V9XDIAG\SIS2D.TXT. Runs beside SIS2D.VXD, which executes the
 * op lists built here and refuses anything outside the sequencer, BAR1 and
 * BAR0. It answers what no source settles before the engine is put behind
 * DirectDraw (docs\plans\sis-6326-family.md, Phase 2):
 *
 *   - whether width and height are programmed as n or n - 1;
 *   - whether a right-to-left copy starts on the last byte of its last
 *     pixel (only visible at 16 bpp);
 *   - whether the busy bit (82ABh D6) clears, and how fast;
 *   - whether a bottom-up copy and a forward copy land where expected.
 *
 * Every engine command comes from the host-tested builder
 * (src\chipsets\sis\sis6326_engine.c), compiled in. Safety contract:
 *
 *   - Run under Velocity9x tier-0, whose display path writes no SiS register,
 *     at 8 or 16 bpp. The engine's depth follows the desktop's.
 *   - SR05, SRB and SR27 are read first and written back last; SRB D[6:5]
 *     is set to 11 (MMIO through BAR1) and SR27 to engine registers on,
 *     Turbo Queue off, keeping its other bits.
 *   - Engine output and CPU test writes stay in VRAM at 2 MiB and above,
 *     beyond any tier-0 desktop (1600x1200x16 is 3.66 MiB, so run at a
 *     smaller desktop; this tool refuses a desktop that reaches 2 MiB).
 *   - Every wait is bounded in the VxD.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/sis6326_engine.h"

#include "../../src/chipsets/sis/sis6326_engine.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define SIS2D_MAGIC 0x44325349ul
#define SIS2D_OP_MAX 512u
#define SIS2D_TIMEOUT 0xfffffffful

#define SIS2D_OP_SR_READ      1ul
#define SIS2D_OP_SR_WRITE     2ul
#define SIS2D_OP_MMIO_WRITE32 3ul
#define SIS2D_OP_MMIO_WRITE16 4ul
#define SIS2D_OP_MMIO_READ32  5ul
#define SIS2D_OP_LFB_WRITE32  6ul
#define SIS2D_OP_LFB_READ32   7ul
#define SIS2D_OP_LFB_FILL32   8ul
#define SIS2D_OP_WAIT_CLEAR   9ul

#define SIS2D_MAPPED  0x00000002ul
#define SIS2D_RAN     0x00000004ul

/* Sequencer registers this tool changes and restores. */
#define SIS_SR05 0x05ul
#define SIS_SRB  0x0bul
#define SIS_SR27 0x27ul
#define SIS_SR05_KEY 0x86ul
#define SIS_SR05_UNLOCKED 0xa1ul
#define SIS_SR05_RELOCK 0x00ul
/* SRB D[6:5] = 11: MMIO through PCI BAR1 (datasheet 7.7.8). */
#define SIS_SRB_MMIO_BAR1 0x60ul
/* SR27: D7 Turbo Queue (off), D6 engine registers (on); D[5:0] kept. */
#define SIS_SR27_KEEP 0x3ful
#define SIS_SR27_ENGINE 0x40ul

#define SIS2D_VRAM_BYTES 0x00400000ul
#define SIS2D_TEST_BASE  0x00200000ul
#define SIS2D_REGION_STRIDE 0x00010000ul
#define SIS2D_PITCH 256ul
#define SIS2D_ROWS 12u
#define SIS2D_COLS 64u
#define SIS2D_GUARD 0xa5u

struct sis2d_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct sis2d_request {
    DWORD count;
    struct sis2d_op ops[SIS2D_OP_MAX];
};

struct sis2d_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar1;
    DWORD executed;
    DWORD refused;
    struct sis2d_op ops[SIS2D_OP_MAX];
};

static struct sis2d_request sis2d_request_buffer;
static struct sis2d_result sis2d_result_buffer;
static HANDLE sis2d_device = INVALID_HANDLE_VALUE;
static HANDLE sis2d_output = INVALID_HANDLE_VALUE;
static DWORD sis2d_failures = 0ul;

/* Byte loops: the tool links no C runtime, and CopyMemory and ZeroMemory
 * expand to memcpy and memset. */
static void sis2d_bytes(void *to, const void *from, DWORD count)
{
    BYTE *destination = (BYTE *)to;
    const BYTE *source = (const BYTE *)from;

    while (count-- != 0ul) {
        *destination++ = *source++;
    }
}

static void sis2d_zero(void *to, DWORD count)
{
    BYTE *destination = (BYTE *)to;

    while (count-- != 0ul) {
        *destination++ = 0u;
    }
}

static void sis2d_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void sis2d_decimal(char *text, DWORD value)
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

static void sis2d_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(sis2d_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(sis2d_output, "=", 1u, &written, 0);
    WriteFile(sis2d_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(sis2d_output, "\r\n", 2u, &written, 0);
}

static void sis2d_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    sis2d_hex(text + 2, value, 8);
    sis2d_write(key, text);
}

static void sis2d_write_decimal(const char *key, DWORD value)
{
    char text[16];

    sis2d_decimal(text, value);
    sis2d_write(key, text);
}

static void sis2d_key(char *key, const char *prefix, const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
}

static void sis2d_begin(void)
{
    sis2d_request_buffer.count = 0ul;
}

static DWORD sis2d_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct sis2d_op *op;
    DWORD index = sis2d_request_buffer.count;

    if (index >= SIS2D_OP_MAX) {
        return SIS2D_OP_MAX;
    }
    op = &sis2d_request_buffer.ops[index];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0ul;
    ++sis2d_request_buffer.count;
    return index;
}

/* Runs the op list. Non-zero when every op executed. */
static int sis2d_run(void)
{
    DWORD returned = 0ul;

    if (!DeviceIoControl(sis2d_device, 1u, &sis2d_request_buffer,
                         sizeof(sis2d_request_buffer), &sis2d_result_buffer,
                         sizeof(sis2d_result_buffer), &returned, 0) ||
        returned != sizeof(sis2d_result_buffer) ||
        sis2d_result_buffer.magic != SIS2D_MAGIC) {
        return 0;
    }
    return (sis2d_result_buffer.status & SIS2D_RAN) != 0ul &&
           sis2d_result_buffer.executed == sis2d_request_buffer.count;
}

static DWORD sis2d_value(DWORD index)
{
    return index < SIS2D_OP_MAX ? sis2d_result_buffer.ops[index].result : 0ul;
}

/* The builder's dwords, the command word, the posted read xf86-video-sis
 * issues after it, and a bounded wait for the busy bit. Returns the index
 * of the wait op. */
static DWORD sis2d_add_blt(const struct v9x_sis_blt *blt)
{
    DWORD index;

    sis2d_add(SIS2D_OP_WAIT_CLEAR, V9X_SIS_2D_CMD_STATUS,
              V9X_SIS_STATUS_BUSY, 0ul);
    for (index = 0ul; index < blt->count; ++index) {
        sis2d_add(SIS2D_OP_MMIO_WRITE32, blt->offsets[index],
                  blt->values[index], 0ul);
    }
    sis2d_add(SIS2D_OP_MMIO_WRITE16, V9X_SIS_2D_COMMAND, blt->command, 0ul);
    sis2d_add(SIS2D_OP_MMIO_READ32, V9X_SIS_2D_CMD_STATUS, 0ul, 0ul);
    return sis2d_add(SIS2D_OP_WAIT_CLEAR, V9X_SIS_2D_CMD_STATUS,
                     V9X_SIS_STATUS_BUSY, 0ul);
}

/* A region: SIS2D_ROWS x SIS2D_COLS bytes at pitch SIS2D_PITCH. */
typedef BYTE sis2d_image[SIS2D_ROWS][SIS2D_COLS];

static BYTE sis2d_gradient(DWORD row, DWORD col)
{
    return (BYTE)((row * 37ul + col * 5ul + 11ul) & 0xfful);
}

/* Guard the whole region (16 rows, so a size error past row 11 still lands
 * in guarded memory), then optionally write the gradient. */
static void sis2d_add_prepare(DWORD base, int gradient, sis2d_image image)
{
    DWORD row;
    DWORD col;

    sis2d_add(SIS2D_OP_LFB_FILL32, base, 0xa5a5a5a5ul,
              16ul * SIS2D_PITCH / 4ul);
    for (row = 0ul; row < SIS2D_ROWS; ++row) {
        for (col = 0ul; col < SIS2D_COLS; ++col) {
            image[row][col] = gradient ? sis2d_gradient(row, col)
                                       : (BYTE)SIS2D_GUARD;
        }
        if (!gradient) {
            continue;
        }
        for (col = 0ul; col < SIS2D_COLS; col += 4ul) {
            sis2d_add(SIS2D_OP_LFB_WRITE32, base + row * SIS2D_PITCH + col,
                      (DWORD)image[row][col] |
                      ((DWORD)image[row][col + 1ul] << 8) |
                      ((DWORD)image[row][col + 2ul] << 16) |
                      ((DWORD)image[row][col + 3ul] << 24), 0ul);
        }
    }
}

/* Index of the first readback op. */
static DWORD sis2d_add_readback(DWORD base)
{
    DWORD first = SIS2D_OP_MAX;
    DWORD row;
    DWORD col;
    DWORD index;

    for (row = 0ul; row < SIS2D_ROWS; ++row) {
        for (col = 0ul; col < SIS2D_COLS; col += 4ul) {
            index = sis2d_add(SIS2D_OP_LFB_READ32,
                              base + row * SIS2D_PITCH + col, 0ul, 0ul);
            if (first == SIS2D_OP_MAX) {
                first = index;
            }
        }
    }
    return first;
}

static void sis2d_collect(DWORD first, sis2d_image actual)
{
    DWORD row;
    DWORD col;
    DWORD value;

    for (row = 0ul; row < SIS2D_ROWS; ++row) {
        for (col = 0ul; col < SIS2D_COLS; col += 4ul) {
            value = sis2d_value(first + row * (SIS2D_COLS / 4ul) + col / 4ul);
            actual[row][col] = (BYTE)value;
            actual[row][col + 1ul] = (BYTE)(value >> 8);
            actual[row][col + 2ul] = (BYTE)(value >> 16);
            actual[row][col + 3ul] = (BYTE)(value >> 24);
        }
    }
}

/*
 * Compare and report: the mismatch count, the first mismatch, the extent of
 * bytes that differ from the region as prepared (what the engine actually
 * touched), and every row as hex.
 */
static void sis2d_report(const char *name, sis2d_image before,
                         sis2d_image expected, sis2d_image actual,
                         DWORD wait_reads)
{
    char key[48];
    char text[SIS2D_COLS * 2 + 1];
    DWORD row;
    DWORD col;
    DWORD mismatches = 0ul;
    DWORD first = 0xfffffffful;
    DWORD min_row = 0xfffffffful;
    DWORD max_row = 0ul;
    DWORD min_col = 0xfffffffful;
    DWORD max_col = 0ul;

    for (row = 0ul; row < SIS2D_ROWS; ++row) {
        for (col = 0ul; col < SIS2D_COLS; ++col) {
            if (actual[row][col] != expected[row][col]) {
                if (mismatches == 0ul) {
                    first = (row << 16) | col;
                }
                ++mismatches;
            }
            if (actual[row][col] != before[row][col]) {
                if (row < min_row) min_row = row;
                if (row > max_row) max_row = row;
                if (col < min_col) min_col = col;
                if (col > max_col) max_col = col;
            }
        }
    }

    sis2d_key(key, name, "Result");
    sis2d_write(key, mismatches == 0ul ? "PASS" : "FAIL");
    if (mismatches != 0ul) {
        ++sis2d_failures;
    }
    sis2d_key(key, name, "Mismatches");
    sis2d_write_decimal(key, mismatches);
    sis2d_key(key, name, "FirstMismatchRowCol");
    sis2d_write_hex(key, first);
    sis2d_key(key, name, "WaitReads");
    sis2d_write_hex(key, wait_reads);
    if (min_row != 0xfffffffful) {
        sis2d_key(key, name, "ChangedRows");
        sis2d_decimal(text, min_row);
        lstrcatA(text, "-");
        sis2d_decimal(text + lstrlenA(text), max_row);
        sis2d_write(key, text);
        sis2d_key(key, name, "ChangedColumnBytes");
        sis2d_decimal(text, min_col);
        lstrcatA(text, "-");
        sis2d_decimal(text + lstrlenA(text), max_col);
        sis2d_write(key, text);
    } else {
        sis2d_key(key, name, "ChangedRows");
        sis2d_write(key, "none");
    }
    for (row = 0ul; row < SIS2D_ROWS; ++row) {
        for (col = 0ul; col < SIS2D_COLS; ++col) {
            sis2d_hex(text + col * 2u, actual[row][col], 2);
        }
        sis2d_key(key, name, "Row");
        sis2d_decimal(key + lstrlenA(key), row);
        sis2d_write(key, text);
    }
}

/* The fill the builder describes, applied to a model of the region. */
static void sis2d_model_fill(sis2d_image image, const struct v9x_sis_fill *fill)
{
    DWORD row;
    DWORD col;
    DWORD byte_index;
    BYTE pattern[2];

    pattern[0] = (BYTE)fill->color;
    pattern[1] = (BYTE)(fill->color >> 8);
    for (row = fill->top; row < fill->top + fill->height; ++row) {
        for (col = 0ul; col < fill->width * fill->bytes_per_pixel; ++col) {
            byte_index = fill->left * fill->bytes_per_pixel + col;
            image[row][byte_index] = fill->bytes_per_pixel == 1ul
                ? pattern[0] : pattern[col & 1ul];
        }
    }
}

/* A copy with memmove semantics: the destination ends up holding the source
 * as it was before the copy, whatever the overlap. */
static void sis2d_model_copy(sis2d_image destination, sis2d_image source,
                             const struct v9x_sis_copy *copy)
{
    sis2d_image snapshot;
    DWORD row;
    DWORD col;
    DWORD bpp = copy->bytes_per_pixel;

    sis2d_bytes(snapshot, source, sizeof(snapshot));
    for (row = 0ul; row < copy->height; ++row) {
        for (col = 0ul; col < copy->width * bpp; ++col) {
            destination[copy->destination_top + row]
                       [copy->destination_left * bpp + col] =
                snapshot[copy->source_top + row]
                        [copy->source_left * bpp + col];
        }
    }
}

static void sis2d_copy_image(sis2d_image to, sis2d_image from)
{
    sis2d_bytes(to, from, sizeof(sis2d_image));
}

static void sis2d_test_fill(DWORD bpp)
{
    static sis2d_image before;
    static sis2d_image expected;
    static sis2d_image actual;
    struct v9x_sis_fill fill;
    struct v9x_sis_blt blt;
    DWORD base = SIS2D_TEST_BASE;
    DWORD wait;
    DWORD first;

    sis2d_zero(&fill, sizeof(fill));
    fill.vram_bytes = SIS2D_VRAM_BYTES;
    fill.target_offset = base;
    fill.pitch_bytes = SIS2D_PITCH;
    fill.bytes_per_pixel = bpp;
    fill.left = 16ul / bpp;
    fill.top = 2ul;
    fill.width = 32ul / bpp;
    fill.height = 4ul;
    fill.color = bpp == 1ul ? 0x3cul : 0x1234ul;
    if (v9x_sis_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        sis2d_write("FillResult", "BUILD-REFUSED");
        ++sis2d_failures;
        return;
    }

    sis2d_begin();
    sis2d_add_prepare(base, 0, before);
    wait = sis2d_add_blt(&blt);
    first = sis2d_add_readback(base);
    if (!sis2d_run()) {
        sis2d_write("FillResult", "RUN-FAILED");
        ++sis2d_failures;
        return;
    }
    sis2d_copy_image(expected, before);
    sis2d_model_fill(expected, &fill);
    sis2d_collect(first, actual);
    sis2d_report("Fill", before, expected, actual, sis2d_value(wait));
}

static void sis2d_test_copy(const char *name, DWORD region, int same_surface,
                            DWORD bpp, DWORD source_left, DWORD source_top,
                            DWORD destination_left, DWORD destination_top,
                            DWORD width, DWORD height)
{
    static sis2d_image source;
    static sis2d_image before;
    static sis2d_image expected;
    static sis2d_image actual;
    struct v9x_sis_copy copy;
    struct v9x_sis_blt blt;
    char key[48];
    DWORD source_base = SIS2D_TEST_BASE + region * SIS2D_REGION_STRIDE;
    DWORD destination_base = same_surface ? source_base
                                          : source_base + SIS2D_REGION_STRIDE;
    DWORD wait;
    DWORD first;

    sis2d_zero(&copy, sizeof(copy));
    copy.vram_bytes = SIS2D_VRAM_BYTES;
    copy.source_offset = source_base;
    copy.source_pitch_bytes = SIS2D_PITCH;
    copy.destination_offset = destination_base;
    copy.destination_pitch_bytes = SIS2D_PITCH;
    copy.bytes_per_pixel = bpp;
    copy.source_left = source_left;
    copy.source_top = source_top;
    copy.destination_left = destination_left;
    copy.destination_top = destination_top;
    copy.width = width;
    copy.height = height;
    if (v9x_sis_build_copy(&copy, &blt) != V9X_STATUS_OK) {
        sis2d_key(key, name, "Result");
        sis2d_write(key, "BUILD-REFUSED");
        ++sis2d_failures;
        return;
    }
    sis2d_key(key, name, "Command");
    sis2d_write_hex(key, blt.command);

    sis2d_begin();
    sis2d_add_prepare(source_base, 1, source);
    if (same_surface) {
        sis2d_copy_image(before, source);
    } else {
        sis2d_add_prepare(destination_base, 0, before);
    }
    wait = sis2d_add_blt(&blt);
    first = sis2d_add_readback(destination_base);
    if (!sis2d_run()) {
        sis2d_key(key, name, "Result");
        sis2d_write(key, "RUN-FAILED");
        ++sis2d_failures;
        return;
    }
    sis2d_copy_image(expected, before);
    sis2d_model_copy(expected, source, &copy);
    sis2d_collect(first, actual);
    sis2d_report(name, before, expected, actual, sis2d_value(wait));
}

void WINAPI V9xSis2dProbeEntry(void)
{
    HDC display;
    DWORD width = 0ul;
    DWORD height = 0ul;
    DWORD bits = 0ul;
    DWORD bpp;
    DWORD sr05;
    DWORD sr05_after;
    DWORD srb;
    DWORD sr27;
    DWORD index[8];
    static const char header[] = "[Sis2dProbe]\r\n";
    DWORD written;

    display = GetDC(0);
    if (display != 0) {
        width = (DWORD)GetDeviceCaps(display, HORZRES);
        height = (DWORD)GetDeviceCaps(display, VERTRES);
        bits = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                       GetDeviceCaps(display, PLANES));
        ReleaseDC(0, display);
    }

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    sis2d_output = CreateFileA(V9X_DIAG_SIS2D_TXT, GENERIC_WRITE,
                               FILE_SHARE_READ, 0, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, 0);
    if (sis2d_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(sis2d_output, header, (DWORD)lstrlenA(header), &written, 0);
    sis2d_write("Build", V9X_BUILD_ID);
    sis2d_write_decimal("DesktopWidth", width);
    sis2d_write_decimal("DesktopHeight", height);
    sis2d_write_decimal("DesktopBpp", bits);

    /* The engine works at the desktop's depth, and the tests must not reach
     * the visible framebuffer. */
    if (bits != 8ul && bits != 16ul) {
        sis2d_write("Result", "REFUSED-DEPTH");
        ExitProcess(5u);
    }
    bpp = bits / 8ul;
    if (width * height * bpp >= SIS2D_TEST_BASE) {
        sis2d_write("Result", "REFUSED-DESKTOP-TOO-LARGE");
        ExitProcess(5u);
    }

    sis2d_device = CreateFileA("\\\\.\\SIS2D.VXD", 0, 0, 0, CREATE_NEW,
                               FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (sis2d_device == INVALID_HANDLE_VALUE) {
        sis2d_write("Result", "NO-VXD");
        ExitProcess(2u);
    }

    /* Read the state this tool will change, unlocking to read it. */
    sis2d_begin();
    index[0] = sis2d_add(SIS2D_OP_SR_READ, SIS_SR05, 0ul, 0ul);
    sis2d_add(SIS2D_OP_SR_WRITE, SIS_SR05, SIS_SR05_KEY, 0ul);
    index[1] = sis2d_add(SIS2D_OP_SR_READ, SIS_SR05, 0ul, 0ul);
    index[2] = sis2d_add(SIS2D_OP_SR_READ, SIS_SRB, 0ul, 0ul);
    index[3] = sis2d_add(SIS2D_OP_SR_READ, SIS_SR27, 0ul, 0ul);
    if (!sis2d_run()) {
        sis2d_write_hex("VxdStatus", sis2d_result_buffer.status);
        sis2d_write("Result", "STATE-READ-FAILED");
        CloseHandle(sis2d_device);
        ExitProcess(3u);
    }
    sis2d_write_hex("Bar0", sis2d_result_buffer.bar0);
    sis2d_write_hex("Bar1", sis2d_result_buffer.bar1);
    sr05 = sis2d_value(index[0]);
    sr05_after = sis2d_value(index[1]);
    srb = sis2d_value(index[2]);
    sr27 = sis2d_value(index[3]);
    sis2d_write_hex("SR05Found", sr05);
    sis2d_write_hex("SR05Unlocked", sr05_after);
    sis2d_write_hex("SRBFound", srb);
    sis2d_write_hex("SR27Found", sr27);

    if (sr05_after != SIS_SR05_UNLOCKED) {
        sis2d_write("Result", "UNLOCK-FAILED");
        goto restore;
    }

    /* MMIO on, engine registers on, Turbo Queue off. */
    sis2d_begin();
    sis2d_add(SIS2D_OP_SR_WRITE, SIS_SRB, srb | SIS_SRB_MMIO_BAR1, 0ul);
    sis2d_add(SIS2D_OP_SR_WRITE, SIS_SR27,
              (sr27 & SIS_SR27_KEEP) | SIS_SR27_ENGINE, 0ul);
    index[4] = sis2d_add(SIS2D_OP_SR_READ, SIS_SRB, 0ul, 0ul);
    index[5] = sis2d_add(SIS2D_OP_SR_READ, SIS_SR27, 0ul, 0ul);
    index[6] = sis2d_add(SIS2D_OP_MMIO_READ32, V9X_SIS_2D_CMD_STATUS, 0ul,
                         0ul);
    index[7] = sis2d_add(SIS2D_OP_WAIT_CLEAR, V9X_SIS_2D_CMD_STATUS,
                         V9X_SIS_STATUS_BUSY, 0ul);
    if (!sis2d_run()) {
        sis2d_write("Result", "ENABLE-FAILED");
        goto restore;
    }
    sis2d_write_hex("SRBEngine", sis2d_value(index[4]));
    sis2d_write_hex("SR27Engine", sis2d_value(index[5]));
    sis2d_write_hex("StatusAtEnable", sis2d_value(index[6]));
    sis2d_write_hex("IdleWaitAtEnable", sis2d_value(index[7]));
    if (sis2d_value(index[6]) == 0xfffffffful ||
        sis2d_value(index[7]) == SIS2D_TIMEOUT) {
        sis2d_write("Result", "ENGINE-NOT-RESPONDING");
        goto restore;
    }

    sis2d_test_fill(bpp);
    /* Separate surfaces: forward in both axes. */
    sis2d_test_copy("CopyForward", 1ul, 0, bpp, 4ul / bpp, 1ul,
                    8ul / bpp, 3ul, 24ul / bpp, 4ul);
    /* One surface, destination to the right on the same rows:
     * right-to-left, starting on the last byte of the last pixel. */
    sis2d_test_copy("CopyRight", 3ul, 1, bpp, 4ul / bpp, 2ul,
                    8ul / bpp, 2ul, 24ul / bpp, 3ul);
    /* One surface, destination lower: bottom-up. */
    sis2d_test_copy("CopyDown", 4ul, 1, bpp, 4ul / bpp, 2ul,
                    6ul / bpp, 4ul, 24ul / bpp, 4ul);

    sis2d_write("Result", sis2d_failures == 0ul ? "PASS" : "FAIL");

restore:
    /* SR27 and SRB as found, then the lock as found. */
    sis2d_begin();
    sis2d_add(SIS2D_OP_SR_WRITE, SIS_SR27, sr27, 0ul);
    sis2d_add(SIS2D_OP_SR_WRITE, SIS_SRB, srb, 0ul);
    if (sr05 != SIS_SR05_UNLOCKED) {
        sis2d_add(SIS2D_OP_SR_WRITE, SIS_SR05, SIS_SR05_RELOCK, 0ul);
    }
    index[0] = sis2d_add(SIS2D_OP_SR_READ, SIS_SR05, 0ul, 0ul);
    if (sis2d_run()) {
        sis2d_write("Restored", "1");
        sis2d_write_hex("SR05Final", sis2d_value(index[0]));
    } else {
        sis2d_write("Restored", "0");
    }
    CloseHandle(sis2d_device);
    CloseHandle(sis2d_output);
    ExitProcess(sis2d_failures == 0ul ? 0u : 1u);
}
