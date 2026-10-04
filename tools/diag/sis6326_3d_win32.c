/*
 * SIS3D.EXE: the SiS 6326 3D engine write probe, phase 1 (first triangle).
 *
 * Publishes C:\V9XDIAG\SIS3D.TXT. Drives the 3D registers through
 * SIS2D.VXD, which executes op lists inside the sequencer, BAR1 and BAR0 and
 * refuses anything else (docs\plans\sis-6326-hardware-3d.md, phase 1).
 *
 * Every register value comes from the host-tested builder
 * (src\chipsets\sis\sis6326_3d.c), compiled in. It answers:
 *
 *   - which value of the direction bit (89F8h D7) a triangle needs, for a
 *     middle vertex right of the long edge and for one left of it - each
 *     triangle is fired once with each value;
 *   - whether the engine covers the pixels whose centres are inside, and
 *     whether its edges are inclusive;
 *   - whether 3D runs with the Turbo Queue off and SR39 D2 set.
 *
 * Safety contract, as SIS2D.EXE's: run under Velocity9x at 16 bpp (the
 * targets are RGB565), with the desktop below 2 MiB; every target is
 * off-screen at 2 MiB and above and guarded; every wait is bounded in the
 * VxD; SR39 and SR05 are written back as found. SRB and SR27 are the
 * driver's (it enables the engine window after every mode set) and are only
 * checked.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/sis6326_3d.h"

#include "../../src/chipsets/sis/sis6326_3d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define SIS3D_MAGIC 0x44325349ul
#define SIS3D_OP_MAX 512u
#define SIS3D_TIMEOUT 0xfffffffful

#define SIS3D_OP_SR_READ      1ul
#define SIS3D_OP_SR_WRITE     2ul
#define SIS3D_OP_MMIO_WRITE32 3ul
#define SIS3D_OP_MMIO_READ32  5ul
#define SIS3D_OP_LFB_READ32   7ul
#define SIS3D_OP_LFB_FILL32   8ul
#define SIS3D_OP_WAIT_SET     10ul

#define SIS3D_RAN 0x00000004ul

#define SIS_SR05 0x05ul
#define SIS_SRB  0x0bul
#define SIS_SR27 0x27ul
#define SIS_SR39 0x39ul
#define SIS_SR05_KEY 0x86ul
#define SIS_SR05_UNLOCKED 0xa1ul
#define SIS_SR05_RELOCK 0x00ul
#define SIS_SRB_MMIO_BAR1 0x60ul
#define SIS_SR27_ENGINE 0x40ul
/* SR39 D2: enable the 3D accelerator (datasheet 7.7.60). */
#define SIS_SR39_3D 0x04ul

#define SIS3D_VRAM_BYTES 0x00400000ul
#define SIS3D_TEST_BASE  0x00200000ul
#define SIS3D_REGION_STRIDE 0x00001000ul
#define SIS3D_SIDE 32ul
#define SIS3D_PITCH 64ul
#define SIS3D_GUARD 0xa5a5u
/* Flat green: ARGB FF00FF00 is 07E0h in RGB565. */
#define SIS3D_ARGB 0xff00ff00ul
#define SIS3D_PIXEL 0x07e0u

struct sis3d_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct sis3d_request {
    DWORD count;
    struct sis3d_op ops[SIS3D_OP_MAX];
};

struct sis3d_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar1;
    DWORD executed;
    DWORD refused;
    struct sis3d_op ops[SIS3D_OP_MAX];
};

static struct sis3d_request sis3d_request_buffer;
static struct sis3d_result sis3d_result_buffer;
static HANDLE sis3d_device = INVALID_HANDLE_VALUE;
static HANDLE sis3d_output = INVALID_HANDLE_VALUE;
static unsigned short sis3d_pixels[SIS3D_SIDE][SIS3D_SIDE];

static void sis3d_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void sis3d_decimal(char *text, DWORD value)
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

static void sis3d_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(sis3d_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(sis3d_output, "=", 1u, &written, 0);
    WriteFile(sis3d_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(sis3d_output, "\r\n", 2u, &written, 0);
}

static void sis3d_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    sis3d_hex(text + 2, value, 8);
    sis3d_write(key, text);
}

static void sis3d_write_decimal(const char *key, DWORD value)
{
    char text[16];

    sis3d_decimal(text, value);
    sis3d_write(key, text);
}

static void sis3d_key(char *key, const char *prefix, const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
}

static void sis3d_begin(void)
{
    sis3d_request_buffer.count = 0ul;
}

static DWORD sis3d_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct sis3d_op *op;
    DWORD index = sis3d_request_buffer.count;

    if (index >= SIS3D_OP_MAX) {
        return SIS3D_OP_MAX;
    }
    op = &sis3d_request_buffer.ops[index];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0ul;
    ++sis3d_request_buffer.count;
    return index;
}

static int sis3d_run(void)
{
    DWORD returned = 0ul;

    if (!DeviceIoControl(sis3d_device, 1u, &sis3d_request_buffer,
                         sizeof(sis3d_request_buffer), &sis3d_result_buffer,
                         sizeof(sis3d_result_buffer), &returned, 0) ||
        returned != sizeof(sis3d_result_buffer) ||
        sis3d_result_buffer.magic != SIS3D_MAGIC) {
        return 0;
    }
    return (sis3d_result_buffer.status & SIS3D_RAN) != 0ul &&
           sis3d_result_buffer.executed == sis3d_request_buffer.count;
}

static DWORD sis3d_value(DWORD index)
{
    return index < SIS3D_OP_MAX ? sis3d_result_buffer.ops[index].result : 0ul;
}

static void sis3d_add_writes(const struct v9x_sis3d_writes *writes)
{
    DWORD index;

    for (index = 0ul; index < writes->count; ++index) {
        sis3d_add(SIS3D_OP_MMIO_WRITE32, writes->offsets[index],
                  writes->values[index], 0ul);
    }
}

/*
 * The reference coverage: pixel (x, y) is inside when the point (x, y) -
 * its integer corner, Direct3D's pixel centre - is strictly inside all three
 * edges. Measured on 2026-10-05: sampling at (x + 0.5, y + 0.5) disagreed
 * with the engine on 20-24 rows per triangle, at (x, y) on none. Samples
 * exactly on an edge (the Tie shots) count as outside here; the report's
 * spans are what the tie rule is read from.
 */
static long sis3d_edge(const long *a, const long *b, long px, long py)
{
    return (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
}

static int sis3d_inside(const long v[3][2], DWORD x, DWORD y)
{
    long px = (long)x * 16l;
    long py = (long)y * 16l;
    long e0 = sis3d_edge(v[0], v[1], px, py);
    long e1 = sis3d_edge(v[1], v[2], px, py);
    long e2 = sis3d_edge(v[2], v[0], px, py);

    return (e0 > 0l && e1 > 0l && e2 > 0l) ||
           (e0 < 0l && e1 < 0l && e2 < 0l);
}

static void sis3d_span_text(char *text, DWORD left, DWORD right,
                            DWORD count)
{
    if (count == 0ul) {
        lstrcpyA(text, "-");
        return;
    }
    sis3d_decimal(text, left);
    lstrcatA(text, "-");
    sis3d_decimal(text + lstrlenA(text), right);
    lstrcatA(text, "/");
    sis3d_decimal(text + lstrlenA(text), count);
}

/* One shot: guard, fire, wait, read back, compare row by row. */
static void sis3d_shot(const char *name, DWORD region, const long q[3][2],
                       int direction)
{
    struct v9x_sis3d_vertex vertices[3];
    struct v9x_sis3d_target target;
    struct v9x_sis3d_writes state;
    struct v9x_sis3d_writes vertex_writes;
    char key[48];
    char text[96];
    char part[32];
    DWORD base = SIS3D_TEST_BASE + region * SIS3D_REGION_STRIDE;
    DWORD index;
    DWORD row;
    DWORD col;
    DWORD first;
    DWORD wait_index;
    DWORD status_index;
    DWORD painted = 0ul;
    DWORD expected_total = 0ul;
    DWORD wrong_colour = 0ul;
    DWORD differing_rows = 0ul;

    for (index = 0ul; index < 3ul; ++index) {
        vertices[index].x = v9x_sis3d_float_q4((v9x_s32)q[index][0]);
        vertices[index].y = v9x_sis3d_float_q4((v9x_s32)q[index][1]);
        vertices[index].z = 0ul;
        vertices[index].argb = SIS3D_ARGB;
        vertices[index].u = 0ul;
        vertices[index].v = 0ul;
        vertices[index].w = v9x_sis3d_float_q4(16);
        vertices[index].fog_specular = 0ul;
    }
    target.vram_bytes = SIS3D_VRAM_BYTES;
    target.offset = base;
    target.pitch_bytes = SIS3D_PITCH;
    target.width = SIS3D_SIDE;
    target.height = SIS3D_SIDE;
    if (v9x_sis3d_build_flat_state(&target, &state) != V9X_STATUS_OK) {
        sis3d_key(key, name, "Result");
        sis3d_write(key, "BUILD-REFUSED");
        return;
    }
    v9x_sis3d_build_vertices(vertices, &vertex_writes);
    sis3d_key(key, name, "Primitive");
    sis3d_write_hex(key, v9x_sis3d_primitive(vertices,
                                             V9X_SIS3D_SHADE_FLAT_TOP,
                                             direction));

    sis3d_begin();
    /* Guard the target and the 2 KiB after it. */
    sis3d_add(SIS3D_OP_LFB_FILL32, base, 0xa5a5a5a5ul,
              SIS3D_REGION_STRIDE / 4ul);
    sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
              V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    sis3d_add_writes(&state);
    sis3d_add(SIS3D_OP_MMIO_WRITE32, V9X_SIS3D_PRIMITIVE,
              v9x_sis3d_primitive(vertices, V9X_SIS3D_SHADE_FLAT_TOP,
                                  direction), 0ul);
    sis3d_add_writes(&vertex_writes);
    status_index = sis3d_add(SIS3D_OP_MMIO_READ32, V9X_SIS3D_STATUS, 0ul,
                             0ul);
    wait_index = sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                           V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    if (!sis3d_run()) {
        sis3d_key(key, name, "Result");
        sis3d_write(key, "RUN-FAILED");
        return;
    }
    sis3d_key(key, name, "StatusAfterFire");
    sis3d_write_hex(key, sis3d_value(status_index));
    sis3d_key(key, name, "IdleWaitReads");
    sis3d_write_hex(key, sis3d_value(wait_index));

    /* The 32x32 target is exactly 512 dwords: one op list. */
    sis3d_begin();
    first = SIS3D_OP_MAX;
    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; col += 2ul) {
            index = sis3d_add(SIS3D_OP_LFB_READ32,
                              base + row * SIS3D_PITCH + col * 2ul, 0ul, 0ul);
            if (first == SIS3D_OP_MAX) {
                first = index;
            }
        }
    }
    if (!sis3d_run()) {
        sis3d_key(key, name, "Result");
        sis3d_write(key, "READBACK-FAILED");
        return;
    }
    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        for (col = 0ul; col < SIS3D_SIDE; col += 2ul) {
            DWORD value = sis3d_value(first + row * (SIS3D_SIDE / 2ul) +
                                      col / 2ul);

            sis3d_pixels[row][col] = (unsigned short)value;
            sis3d_pixels[row][col + 1ul] = (unsigned short)(value >> 16);
        }
    }

    for (row = 0ul; row < SIS3D_SIDE; ++row) {
        DWORD actual_left = 0ul;
        DWORD actual_right = 0ul;
        DWORD actual_count = 0ul;
        DWORD expected_left = 0ul;
        DWORD expected_right = 0ul;
        DWORD expected_count = 0ul;
        int same = 1;

        for (col = 0ul; col < SIS3D_SIDE; ++col) {
            int hit = sis3d_pixels[row][col] != SIS3D_GUARD;
            int want = sis3d_inside(q, col, row);

            if (hit) {
                if (actual_count == 0ul) {
                    actual_left = col;
                }
                actual_right = col;
                ++actual_count;
                if (sis3d_pixels[row][col] != SIS3D_PIXEL) {
                    ++wrong_colour;
                }
            }
            if (want) {
                if (expected_count == 0ul) {
                    expected_left = col;
                }
                expected_right = col;
                ++expected_count;
            }
            if (hit != want) {
                same = 0;
            }
        }
        painted += actual_count;
        expected_total += expected_count;
        if (!same) {
            ++differing_rows;
        }
        if (actual_count == 0ul && expected_count == 0ul) {
            continue;
        }
        sis3d_span_text(text, expected_left, expected_right, expected_count);
        lstrcatA(text, " actual ");
        sis3d_span_text(part, actual_left, actual_right, actual_count);
        lstrcatA(text, part);
        if (!same) {
            lstrcatA(text, " DIFF");
        }
        sis3d_key(key, name, "Row");
        sis3d_decimal(key + lstrlenA(key), row);
        sis3d_write(key, text);
    }
    sis3d_key(key, name, "Painted");
    sis3d_write_decimal(key, painted);
    sis3d_key(key, name, "Expected");
    sis3d_write_decimal(key, expected_total);
    sis3d_key(key, name, "WrongColour");
    sis3d_write_decimal(key, wrong_colour);
    sis3d_key(key, name, "DifferingRows");
    sis3d_write_decimal(key, differing_rows);
    sis3d_key(key, name, "Result");
    sis3d_write(key, painted == 0ul ? "NOTHING-DRAWN"
                     : (differing_rows == 0ul && wrong_colour == 0ul)
                       ? "MATCH" : "DIFF");
}

void WINAPI V9xSis3dProbeEntry(void)
{
    /* Sixteenths of a pixel, on quarter-pixel positions. Right: the middle
     * vertex B lies right of the long edge A-C. Left: the mirror image. */
    static const long right[3][2] = {
        { 68l, 52l }, { 420l, 196l }, { 164l, 452l }
    };
    static const long left[3][2] = {
        { 444l, 52l }, { 92l, 196l }, { 348l, 452l }
    };
    /* Integer vertices, so edges run through sample points: the tie rule.
     * TieTopLeft has its axis-aligned edges on top and left, TieBottomRight
     * on the bottom and right; both diagonals pass through samples too.
     * Middle vertex right of the long edge in both: direction 0. */
    static const long tie_top_left[3][2] = {
        { 64l, 64l }, { 320l, 64l }, { 64l, 320l }
    };
    static const long tie_bottom_right[3][2] = {
        { 320l, 64l }, { 320l, 320l }, { 64l, 320l }
    };
    /* The same two, moved up and left by 1/16 pixel. The engine owns
     * samples on bottom and right edges (measured with the two above);
     * the shift should turn that into Direct3D's top-left ownership. */
    static const long shifted_top_left[3][2] = {
        { 63l, 63l }, { 319l, 63l }, { 63l, 319l }
    };
    static const long shifted_bottom_right[3][2] = {
        { 319l, 63l }, { 319l, 319l }, { 63l, 319l }
    };
    static const char header[] = "[Sis3dProbe]\r\n";
    HDC display;
    DWORD width = 0ul;
    DWORD height = 0ul;
    DWORD bits = 0ul;
    DWORD sr05;
    DWORD srb;
    DWORD sr27;
    DWORD sr39;
    DWORD index[8];
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
    sis3d_output = CreateFileA(V9X_DIAG_SIS3D_TXT, GENERIC_WRITE,
                               FILE_SHARE_READ, 0, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, 0);
    if (sis3d_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(sis3d_output, header, (DWORD)lstrlenA(header), &written, 0);
    sis3d_write("Build", V9X_BUILD_ID);
    sis3d_write_decimal("DesktopWidth", width);
    sis3d_write_decimal("DesktopHeight", height);
    sis3d_write_decimal("DesktopBpp", bits);
    if (bits != 16ul) {
        sis3d_write("Result", "REFUSED-DEPTH");
        ExitProcess(5u);
    }
    if (width * height * 2ul >= SIS3D_TEST_BASE) {
        sis3d_write("Result", "REFUSED-DESKTOP-TOO-LARGE");
        ExitProcess(5u);
    }

    sis3d_device = CreateFileA("\\\\.\\SIS2D.VXD", 0, 0, 0, CREATE_NEW,
                               FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (sis3d_device == INVALID_HANDLE_VALUE) {
        sis3d_write("Result", "NO-VXD");
        ExitProcess(2u);
    }

    sis3d_begin();
    index[0] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR05, 0ul, 0ul);
    sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR05, SIS_SR05_KEY, 0ul);
    index[1] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR05, 0ul, 0ul);
    index[2] = sis3d_add(SIS3D_OP_SR_READ, SIS_SRB, 0ul, 0ul);
    index[3] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR27, 0ul, 0ul);
    index[4] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR39, 0ul, 0ul);
    if (!sis3d_run()) {
        sis3d_write("Result", "STATE-READ-FAILED");
        CloseHandle(sis3d_device);
        ExitProcess(3u);
    }
    sr05 = sis3d_value(index[0]);
    srb = sis3d_value(index[2]);
    sr27 = sis3d_value(index[3]);
    sr39 = sis3d_value(index[4]);
    sis3d_write_hex("SR05Found", sr05);
    sis3d_write_hex("SR05Unlocked", sis3d_value(index[1]));
    sis3d_write_hex("SRB", srb);
    sis3d_write_hex("SR27", sr27);
    sis3d_write_hex("SR39Found", sr39);
    if (sis3d_value(index[1]) != SIS_SR05_UNLOCKED ||
        (srb & SIS_SRB_MMIO_BAR1) != SIS_SRB_MMIO_BAR1 ||
        (sr27 & SIS_SR27_ENGINE) == 0ul) {
        /* The driver enables the engine window after every mode set; if it
         * is not on, this is not the driver this probe was written for. */
        sis3d_write("Result", "ENGINE-WINDOW-NOT-ENABLED");
        goto restore;
    }

    sis3d_begin();
    sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR39, sr39 | SIS_SR39_3D, 0ul);
    index[5] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR39, 0ul, 0ul);
    index[6] = sis3d_add(SIS3D_OP_MMIO_READ32, V9X_SIS3D_STATUS, 0ul, 0ul);
    index[7] = sis3d_add(SIS3D_OP_WAIT_SET, V9X_SIS3D_STATUS,
                         V9X_SIS3D_STATUS_IDLE_EMPTY, 0ul);
    if (!sis3d_run()) {
        sis3d_write("Result", "ENABLE-FAILED");
        goto restore;
    }
    sis3d_write_hex("SR39Enabled", sis3d_value(index[5]));
    sis3d_write_hex("StatusAtEnable", sis3d_value(index[6]));
    sis3d_write_hex("IdleWaitAtEnable", sis3d_value(index[7]));
    if (sis3d_value(index[7]) == SIS3D_TIMEOUT) {
        sis3d_write("Result", "3D-NOT-IDLE");
        goto restore;
    }

    sis3d_shot("RightDir0", 0ul, right, 0);
    sis3d_shot("RightDir1", 1ul, right, 1);
    sis3d_shot("LeftDir0", 2ul, left, 0);
    sis3d_shot("LeftDir1", 3ul, left, 1);
    sis3d_shot("TieTopLeft", 4ul, tie_top_left, 0);
    sis3d_shot("TieBottomRight", 5ul, tie_bottom_right, 0);
    sis3d_shot("ShiftTopLeft", 6ul, shifted_top_left, 0);
    sis3d_shot("ShiftBottomRight", 7ul, shifted_bottom_right, 0);
    sis3d_write("Result", "RAN");

restore:
    sis3d_begin();
    sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR39, sr39, 0ul);
    if (sr05 != SIS_SR05_UNLOCKED) {
        sis3d_add(SIS3D_OP_SR_WRITE, SIS_SR05, SIS_SR05_RELOCK, 0ul);
    }
    index[0] = sis3d_add(SIS3D_OP_SR_READ, SIS_SR39, 0ul, 0ul);
    if (sis3d_run()) {
        sis3d_write("Restored", "1");
        sis3d_write_hex("SR39Final", sis3d_value(index[0]));
    } else {
        sis3d_write("Restored", "0");
    }
    CloseHandle(sis3d_device);
    CloseHandle(sis3d_output);
    ExitProcess(0u);
}
