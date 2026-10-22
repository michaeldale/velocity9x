/*
 * R128CCE.EXE: the ATI Rage 128 CCE (Concurrent Command Engine) probe.
 *
 * Decision 1 of docs\plans\ati-rage128.md: does the CCE run packets fed
 * through its PIO FIFO ports (PM4_FIFO_DATA_EVEN/ODD, 1000h/1004h), with no
 * ring and no bus mastering? No open driver still has that path: the Linux
 * DRM refuses every PIO mode, and X's CCEPIOMode only hands the mode to the
 * DRM. ATI's own driver runs a bus-master ring
 * (docs\decisions\2026-10-22-rage128-register-probe.md). Publishes
 * C:\V9XDIAG\R128CCE.TXT; runs beside R128CCE.VXD.
 *
 * Needs C:\V9XDIAG\R128CCE.BIN, linux-firmware's r128/r128_cce.bin (MIT,
 * "Copyright 2000 Advanced Micro Devices"): 256 pairs of big-endian dwords,
 * high half first, as r128_cce.c's r128_cce_load_microcode writes them.
 *
 * Steps, each published:
 *   1. Refuse unless PM4_BUFFER_CNTL is NONPM4 (our driver's state).
 *   2. Stop the microengine and read back what its RAM holds (RADDR, then
 *      DATAH and DATAL per word): what POST left, against the file.
 *   3. Load the file (ADDR 0, then DATAH/DATAL pairs) and read it back.
 *   4. Put the 2D engine's init list in by direct writes, as R1282D does.
 *   5. For each FIFO order - EVEN and ODD alternating, then all to EVEN -
 *      prefill a 64x32 16-bpp surface at VRAM 16 MiB, start the CCE in
 *      192PIO with NOUPDATE and the microengine free-running, write the
 *      r128_2d.c fill as type-0 packets, wait on PM4_STAT (192 free, not
 *      busy, GUI not active), stop the CCE back to NONPM4, read the
 *      surface and compare. A timeout resets the engine (R128EngineReset)
 *      and moves on.
 *   6. Leave the CCE in NONPM4 with PM4_MICRO_CNTL as found.
 *
 * Runs at a 16-bpp desktop only. Writes nothing outside the VxD's list.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/r128_2d.h"

#include "../../src/chipsets/ati/rage128/r128_2d.c"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define RC_PATH V9X_DIAG_PATH("R128CCE.TXT")
#define RC_MICROCODE_PATH V9X_DIAG_PATH("R128CCE.BIN")
#define RC_MAGIC 0x45434352ul
#define RC_OP_MAX 512u
#define RC_BLOCK_DWORDS 1024u
#define RC_TIMEOUT 0xfffffffful

#define RC_OP_MMIO_WRITE32 1ul
#define RC_OP_MMIO_READ32 2ul
#define RC_OP_LFB_FILL32 5ul
#define RC_OP_LFB_READ_BLOCK 7ul
#define RC_OP_WAIT_IDLE 8ul
#define RC_OP_PM4_WAIT_IDLE 9ul
#define RC_OP_ENGINE_RESET 10ul

#define RC_MAPPED 0x00000002ul
#define RC_RAN 0x00000004ul

/* r128_reg.h: the CCE's registers. */
#define RC_PM4_BUFFER_CNTL 0x0704ul
#define RC_PM4_STAT 0x07b8ul
#define RC_PM4_MICROCODE_ADDR 0x07d4ul
#define RC_PM4_MICROCODE_RADDR 0x07d8ul
#define RC_PM4_MICROCODE_DATAH 0x07dcul
#define RC_PM4_MICROCODE_DATAL 0x07e0ul
#define RC_PM4_BUFFER_ADDR 0x07f0ul
#define RC_PM4_MICRO_CNTL 0x07fcul
#define RC_PM4_FIFO_DATA_EVEN 0x1000ul
#define RC_PM4_FIFO_DATA_ODD 0x1004ul
#define RC_CRTC_PITCH 0x022cul
#define RC_CRTC_PITCH_MASK 0x3fful

#define RC_PM4_192PIO 0x10000000ul
#define RC_PM4_NONPM4 0x00000000ul
#define RC_PM4_NOUPDATE 0x08000000ul
#define RC_MICRO_FREERUN 0x40000000ul
#define RC_PIO_FIFO_ENTRIES 192ul

/* Type-0 packet: one register, CCE_PACKET0(reg, 0) in r128_drv.h. */
#define RC_PACKET0(reg) ((DWORD)(reg) >> 2)

#define RC_MICROCODE_WORDS 256u

/* The test surface: 64x32 at 16 bpp, one 4 KiB block, at 16 MiB. */
#define TEST_BASE 0x01000000ul
#define TEST_BYTES 4096ul
#define TEST_PITCH 128ul
#define TEST_DWORDS (TEST_BYTES / 4ul)
#define TEST_VRAM 0x02000000ul
#define TEST_PREFILL 0xa5a5a5a5ul

#define ORDER_ALTERNATE 0
#define ORDER_EVEN_ONLY 1

struct rc_op {
    DWORD code;
    DWORD a;
    DWORD b;
    DWORD c;
    DWORD result;
};

struct rc_request {
    DWORD count;
    struct rc_op ops[RC_OP_MAX];
};

struct rc_result {
    DWORD magic;
    DWORD status;
    DWORD bar0;
    DWORD bar2;
    DWORD executed;
    DWORD refused;
    DWORD config_id;
    DWORD copy_id;
    struct rc_op ops[RC_OP_MAX];
    DWORD block[RC_BLOCK_DWORDS];
};

static struct rc_request rc_request_buffer;
static struct rc_result rc_result_buffer;
static DWORD rc_microcode[RC_MICROCODE_WORDS * 2u];
static DWORD rc_readback[RC_MICROCODE_WORDS * 2u];
static unsigned char rc_model[TEST_BYTES];
static HANDLE rc_device;
static HANDLE rc_output;
static DWORD rc_failed;

static void rc_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15u];
    }
    text[digits] = '\0';
}

static void rc_decimal(char *text, DWORD value)
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

static void rc_write(const char *key, const char *value)
{
    DWORD written;

    WriteFile(rc_output, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(rc_output, "=", 1u, &written, 0);
    WriteFile(rc_output, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(rc_output, "\r\n", 2u, &written, 0);
}

static void rc_write_hex(const char *key, DWORD value)
{
    char text[12];

    text[0] = '0';
    text[1] = 'x';
    rc_hex(text + 2, value, 8);
    rc_write(key, text);
}

static void rc_write_decimal(const char *key, DWORD value)
{
    char text[16];

    rc_decimal(text, value);
    rc_write(key, text);
}

static const char *rc_key(char *key, const char *prefix, const char *suffix)
{
    lstrcpyA(key, prefix);
    lstrcatA(key, suffix);
    return key;
}

static void rc_begin(void)
{
    rc_request_buffer.count = 0u;
}

static DWORD rc_add(DWORD code, DWORD a, DWORD b, DWORD c)
{
    struct rc_op *op;

    if (rc_request_buffer.count >= RC_OP_MAX) {
        return RC_OP_MAX - 1u;
    }
    op = &rc_request_buffer.ops[rc_request_buffer.count];
    op->code = code;
    op->a = a;
    op->b = b;
    op->c = c;
    op->result = 0u;
    return rc_request_buffer.count++;
}

static void rc_add_write(DWORD offset, DWORD value)
{
    rc_add(RC_OP_MMIO_WRITE32, offset, value, 0u);
}

static int rc_run(void)
{
    DWORD returned = 0u;
    DWORD bytes = 4u + rc_request_buffer.count * (DWORD)sizeof(struct rc_op);

    if (!DeviceIoControl(rc_device, 1u, &rc_request_buffer, bytes,
                         &rc_result_buffer, sizeof(rc_result_buffer),
                         &returned, 0) ||
        returned != sizeof(rc_result_buffer) ||
        rc_result_buffer.magic != RC_MAGIC) {
        return 0;
    }
    return (rc_result_buffer.status & RC_RAN) != 0ul &&
           rc_result_buffer.executed == rc_request_buffer.count;
}

static DWORD rc_read_register(DWORD offset)
{
    rc_begin();
    rc_add(RC_OP_MMIO_READ32, offset, 0u, 0u);
    if (!rc_run()) {
        return 0xfffffffful;
    }
    return rc_result_buffer.ops[0].result;
}

static void rc_close(DWORD code)
{
    if (rc_device != INVALID_HANDLE_VALUE && rc_device != 0) {
        CloseHandle(rc_device);
    }
    CloseHandle(rc_output);
    ExitProcess(code);
}

static int rc_load_file(void)
{
    HANDLE file;
    DWORD read = 0u;
    unsigned char bytes[RC_MICROCODE_WORDS * 8u];
    DWORD index;

    file = CreateFileA(RC_MICROCODE_PATH, GENERIC_READ, FILE_SHARE_READ, 0,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!ReadFile(file, bytes, sizeof(bytes), &read, 0) ||
        read != sizeof(bytes)) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    for (index = 0u; index < RC_MICROCODE_WORDS * 2u; ++index) {
        rc_microcode[index] = ((DWORD)bytes[index * 4u] << 24) |
                              ((DWORD)bytes[index * 4u + 1u] << 16) |
                              ((DWORD)bytes[index * 4u + 2u] << 8) |
                              (DWORD)bytes[index * 4u + 3u];
    }
    return 1;
}

/* Reads the microcode RAM into rc_readback, 64 words a run (RADDR, DATAH,
 * DATAL per word). Returns how many of the 256 words equal the file. */
static DWORD rc_read_microcode(const char *name)
{
    char key[64];
    DWORD word;
    DWORD first;
    DWORD index;
    DWORD matches = 0u;
    DWORD zero = 0u;

    for (first = 0u; first < RC_MICROCODE_WORDS; first += 64u) {
        rc_begin();
        for (word = first; word < first + 64u; ++word) {
            rc_add_write(RC_PM4_MICROCODE_RADDR, word);
            rc_add(RC_OP_MMIO_READ32, RC_PM4_MICROCODE_DATAH, 0u, 0u);
            rc_add(RC_OP_MMIO_READ32, RC_PM4_MICROCODE_DATAL, 0u, 0u);
        }
        if (!rc_run()) {
            rc_write(rc_key(key, name, "Result"), "NOT-RUN");
            rc_failed = 1u;
            return 0u;
        }
        for (word = 0u; word < 64u; ++word) {
            rc_readback[(first + word) * 2u] =
                rc_result_buffer.ops[word * 3u + 1u].result;
            rc_readback[(first + word) * 2u + 1u] =
                rc_result_buffer.ops[word * 3u + 2u].result;
        }
    }
    for (index = 0u; index < RC_MICROCODE_WORDS; ++index) {
        if (rc_readback[index * 2u] == rc_microcode[index * 2u] &&
            rc_readback[index * 2u + 1u] == rc_microcode[index * 2u + 1u]) {
            ++matches;
        }
        if (rc_readback[index * 2u] == 0u &&
            rc_readback[index * 2u + 1u] == 0u) {
            ++zero;
        }
    }
    rc_write_decimal(rc_key(key, name, "WordsMatchingFile"), matches);
    rc_write_decimal(rc_key(key, name, "WordsZero"), zero);
    rc_write_hex(rc_key(key, name, "Word0High"), rc_readback[0]);
    rc_write_hex(rc_key(key, name, "Word0Low"), rc_readback[1]);
    rc_write_hex(rc_key(key, name, "Word1Low"), rc_readback[3]);
    rc_write_hex(rc_key(key, name, "Word255Low"), rc_readback[511]);
    return matches;
}

/* ADDR 0, then the pairs, two runs of 128 words. */
static int rc_load_microcode(void)
{
    DWORD word;
    DWORD first;

    for (first = 0u; first < RC_MICROCODE_WORDS; first += 128u) {
        rc_begin();
        if (first == 0u) {
            rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
            rc_add_write(RC_PM4_MICROCODE_ADDR, 0u);
        }
        for (word = first; word < first + 128u; ++word) {
            rc_add_write(RC_PM4_MICROCODE_DATAH, rc_microcode[word * 2u]);
            rc_add_write(RC_PM4_MICROCODE_DATAL,
                         rc_microcode[word * 2u + 1u]);
        }
        if (!rc_run()) {
            return 0;
        }
    }
    return 1;
}

static void rc_add_packets(const struct v9x_r128_blt *blt, int order)
{
    DWORD index;
    DWORD slot = 0u;
    DWORD port;

    for (index = 0u; index < blt->count; ++index) {
        port = (order == ORDER_ALTERNATE && (slot & 1u) != 0u) ?
               RC_PM4_FIFO_DATA_ODD : RC_PM4_FIFO_DATA_EVEN;
        rc_add_write(port, RC_PACKET0(blt->offsets[index]));
        ++slot;
        port = (order == ORDER_ALTERNATE && (slot & 1u) != 0u) ?
               RC_PM4_FIFO_DATA_ODD : RC_PM4_FIFO_DATA_EVEN;
        rc_add_write(port, blt->values[index]);
        ++slot;
    }
}

/* One PIO fill in the given FIFO order. */
static void rc_pio_fill(const char *name, int order, DWORD micro_found)
{
    struct v9x_r128_fill fill;
    struct v9x_r128_blt blt;
    char key[64];
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD mismatches = 0u;
    DWORD wait_index;
    DWORD stat_index;
    DWORD gui_index;
    DWORD wait_result;
    const unsigned char *actual;

    fill.vram_bytes = TEST_VRAM;
    fill.target_offset = TEST_BASE;
    fill.pitch_bytes = TEST_PITCH;
    fill.bytes_per_pixel = 2u;
    fill.left = 5u;
    fill.top = 3u;
    fill.width = 24u;
    fill.height = 10u;
    fill.color = order == ORDER_ALTERNATE ? 0x1234u : 0x5678u;
    if (v9x_r128_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        rc_write(rc_key(key, name, "Result"), "BUILDER-REFUSED");
        rc_failed = 1u;
        return;
    }
    for (index = 0u; index < TEST_BYTES; index += 4u) {
        rc_model[index] = 0xa5u;
        rc_model[index + 1u] = 0xa5u;
        rc_model[index + 2u] = 0xa5u;
        rc_model[index + 3u] = 0xa5u;
    }
    for (y = fill.top; y < fill.top + fill.height; ++y) {
        for (x = fill.left; x < fill.left + fill.width; ++x) {
            rc_model[y * TEST_PITCH + x * 2u] = (unsigned char)fill.color;
            rc_model[y * TEST_PITCH + x * 2u + 1u] =
                (unsigned char)(fill.color >> 8);
        }
    }

    rc_begin();
    rc_add(RC_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    /* r128_do_cce_start: mode, the read "as per the sample code", then
     * the microengine. */
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_192PIO | RC_PM4_NOUPDATE);
    rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, RC_MICRO_FREERUN);
    rc_add_packets(&blt, order);
    wait_index = rc_add(RC_OP_PM4_WAIT_IDLE, 0u, RC_PIO_FIFO_ENTRIES, 0u);
    stat_index = rc_add(RC_OP_MMIO_READ32, RC_PM4_STAT, 0u, 0u);
    gui_index = rc_add(RC_OP_MMIO_READ32, V9X_R128_GUI_STAT, 0u, 0u);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "NOT-RUN");
        rc_write_decimal(rc_key(key, name, "RefusedOp"),
                         rc_result_buffer.refused);
        rc_failed = 1u;
        return;
    }
    wait_result = rc_result_buffer.ops[wait_index].result;
    rc_write_hex(rc_key(key, name, "Pm4Wait"), wait_result);
    rc_write_hex(rc_key(key, name, "Pm4Stat"),
                 rc_result_buffer.ops[stat_index].result);
    rc_write_hex(rc_key(key, name, "GuiStat"),
                 rc_result_buffer.ops[gui_index].result);

    /* r128_do_cce_stop, then the surface on an idle engine. A timeout
     * resets the engine first. */
    rc_begin();
    if (wait_result == RC_TIMEOUT) {
        rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    }
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    wait_index = rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    rc_add(RC_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "STOP-NOT-RUN");
        rc_failed = 1u;
        return;
    }
    rc_write_hex(rc_key(key, name, "WaitAfterStop"),
                 rc_result_buffer.ops[wait_index].result);
    actual = (const unsigned char *)rc_result_buffer.block;
    for (index = 0u; index < TEST_BYTES; ++index) {
        if (actual[index] != rc_model[index]) {
            ++mismatches;
        }
    }
    rc_write_decimal(rc_key(key, name, "MismatchBytes"), mismatches);
    rc_write_hex(rc_key(key, name, "PixelAt5x3"),
                 rc_result_buffer.block[(3u * TEST_PITCH + 5u * 2u) / 4u]);
    (void)micro_found;
    if (wait_result == RC_TIMEOUT) {
        rc_write(rc_key(key, name, "Result"), "CCE-NOT-IDLE");
        return;
    }
    rc_write(rc_key(key, name, "Result"),
             mismatches == 0u ? "PASS" : "MISMATCH");
}

void WINAPI V9xRage128CceProbeEntry(void)
{
    static const char header[] = "[Rage128CceProbe]\r\n";
    struct v9x_r128_blt init;
    HDC display;
    DWORD returned;
    DWORD desktop_bpp = 0u;
    DWORD pm4;
    DWORD micro_found;
    DWORD primary_pitch;
    DWORD index;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    rc_output = CreateFileA(RC_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (rc_output == INVALID_HANDLE_VALUE) {
        ExitProcess(4u);
    }
    WriteFile(rc_output, header, (DWORD)lstrlenA(header), &returned, 0);
    rc_write("Build", V9X_BUILD_ID);

    display = GetDC(0);
    if (display != 0) {
        desktop_bpp = (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                              GetDeviceCaps(display, PLANES));
        ReleaseDC(0, display);
    }
    rc_write_decimal("DesktopBpp", desktop_bpp);
    if (desktop_bpp != 16u) {
        rc_write("Result", "REFUSED-DEPTH");
        rc_close(1u);
    }
    if (!rc_load_file()) {
        rc_write("Result", "NO-MICROCODE-FILE");
        rc_close(1u);
    }

    rc_device = CreateFileA("\\\\.\\R128CCE.VXD", 0, 0, 0, CREATE_NEW,
                            FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (rc_device == INVALID_HANDLE_VALUE) {
        rc_write("Result", "VXD-NOT-LOADED");
        rc_close(2u);
    }

    pm4 = rc_read_register(RC_PM4_BUFFER_CNTL);
    rc_write_hex("VxdStatus", rc_result_buffer.status);
    rc_write_hex("CopyId", rc_result_buffer.copy_id);
    if ((rc_result_buffer.status & RC_MAPPED) == 0ul) {
        rc_write("Result", "NOT-MAPPED");
        rc_close(3u);
    }
    micro_found = rc_read_register(RC_PM4_MICRO_CNTL);
    rc_write_hex("Pm4BufferCntlAtStart", pm4);
    rc_write_hex("Pm4MicroCntlAtStart", micro_found);
    rc_write_hex("Pm4StatAtStart", rc_read_register(RC_PM4_STAT));
    if (v9x_r128_pm4_direct(pm4) == 0ul) {
        rc_write("Result", "REFUSED-CCE-RUNNING");
        rc_close(3u);
    }

    /* Stop the microengine, then see what POST left in its RAM. */
    rc_begin();
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    if (!rc_run() || rc_result_buffer.ops[0].result == RC_TIMEOUT) {
        rc_write("Result", "NOT-IDLE-AT-START");
        rc_close(3u);
    }
    rc_read_microcode("Post");

    if (!rc_load_microcode()) {
        rc_write("Result", "LOAD-NOT-RUN");
        rc_close(3u);
    }
    if (rc_read_microcode("Loaded") != RC_MICROCODE_WORDS) {
        rc_write("Result", "LOAD-READBACK-MISMATCH");
        rc_failed = 1u;
    }

    primary_pitch = (rc_read_register(RC_CRTC_PITCH) & RC_CRTC_PITCH_MASK) *
                    V9X_R128_PITCH_PIXEL_UNIT * 2u;
    if (v9x_r128_build_init(2u, primary_pitch,
                            rc_read_register(V9X_R128_DP_DATATYPE),
                            &init) != V9X_STATUS_OK) {
        rc_write("Result", "INIT-REFUSED");
        rc_close(3u);
    }
    rc_begin();
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    for (index = 0u; index < init.count; ++index) {
        rc_add_write(init.offsets[index], init.values[index]);
    }
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    if (!rc_run()) {
        rc_write("Result", "INIT-NOT-RUN");
        rc_close(3u);
    }

    rc_pio_fill("PioAlternate", ORDER_ALTERNATE, micro_found);
    rc_pio_fill("PioEvenOnly", ORDER_EVEN_ONLY, micro_found);

    /* As found: NONPM4, and PM4_MICRO_CNTL's POST value. */
    rc_begin();
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    rc_add_write(RC_PM4_MICRO_CNTL, micro_found);
    rc_run();
    rc_write_hex("Pm4BufferCntlAtEnd", rc_read_register(RC_PM4_BUFFER_CNTL));
    rc_write_hex("Pm4MicroCntlAtEnd", rc_read_register(RC_PM4_MICRO_CNTL));
    rc_write_hex("GuiStatAtEnd", rc_read_register(V9X_R128_GUI_STAT));
    rc_write("Result", rc_failed == 0u ? "COMPLETE" : "FAIL");
    rc_close(rc_failed == 0u ? 0u : 1u);
}
