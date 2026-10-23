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
 *   6. Leave the CCE in NONPM4 with the microengine stopped.
 *
 * Runs at a 16-bpp desktop only. Writes nothing outside the VxD's list.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/r128_2d.h"

#include "../../src/chipsets/ati/rage128/r128_2d.c"
#include "velocity9x/r128_3d.h"
#include "../../src/chipsets/ati/rage128/r128_3d.c"

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
#define RC_OP_LFB_WRITE32 3ul
#define RC_OP_LFB_READ32 4ul
#define RC_OP_LFB_FILL32 5ul
#define RC_OP_LFB_READ_BLOCK 7ul
#define RC_OP_WAIT_IDLE 8ul
#define RC_OP_PM4_WAIT_IDLE 9ul
#define RC_OP_ENGINE_RESET 10ul
#define RC_OP_MMIO_POLL 11ul
#define RC_OP_RING_ALLOC 12ul
#define RC_OP_RING_WRITE32 13ul
#define RC_OP_RING_READ32 14ul
#define RC_OP_GART_ALLOC 15ul

/* /gart: the ring in locked system memory through the Rage 128's own PCI
 * GART, as Linux's is_pci path: PCI_GART_PAGE holds the table's physical
 * address (bit 0, PCI_GART_DIS, clear), the ring is GART page 0, and
 * PM4_BUFFER_OFFSET carries R128_AGP_OFFSET as Linux writes it; bus
 * mastering is on (BUS_CNTL bit 6 clear) only while the test runs. */
#define RC_BUS_CNTL 0x0030ul
#define RC_BUS_MASTER_DIS 0x00000040ul
#define RC_PCI_GART_PAGE 0x017cul
#define RC_AGP_OFFSET 0x02000000ul

/* /agp: no table. The card's address space holds the frame buffer below
 * AGP_APER_OFFSET and an image of system memory from AGP_BASE above it
 * (RRG 2-3, figure 2-1); the PCI GART is for "PCI mode", and this card is
 * AGP. AGP_BASE takes 4 MiB alignment (bits 21:0 hardwired zero), so it is
 * set at or below the locked block and the ring's offset carries the
 * difference. */
#define RC_AGP_BASE 0x0170ul
#define RC_AGP_APER_OFFSET 0x0178ul
#define RC_AGP_BASE_ALIGN 0x00400000ul

/* /ring: the CCE fetching its packets from a ring in the card's own memory
 * (192BM), in the half of VRAM the driver does not use, beside the test
 * surface. PM4_BUFFER_OFFSET without R128_AGP_OFFSET (bit 25), which is how
 * Linux marks a ring in AGP space; no read-pointer write-back (NOUPDATE), so
 * the card writes nothing to system memory; BUS_CNTL is not touched. */
#define RC_PM4_BUFFER_OFFSET 0x0700ul
#define RC_PM4_BUFFER_WM_CNTL 0x0708ul
#define RC_PM4_BUFFER_DL_RPTR 0x0710ul
#define RC_PM4_BUFFER_DL_WPTR 0x0714ul
/* "Informs the engine that the command stream is ending" (Linux's
 * r128_do_cce_flush; the SDK's R128_CCEEnd): without it the CCE holds a
 * fetch shorter than its watermark. */
#define RC_PM4_BUFFER_DL_DONE 0x80000000ul
#define RC_PM4_192BM 0x20000000ul
#define RC_RING_BASE 0x01100000ul
#define RC_RING_BYTES 0x00010000ul
#define RC_RING_L2QW 13ul
/* Linux's watermarks: L 16, M 8, N 8 dwords, K 128 (r128_cce.c). */
#define RC_RING_WM 0x02020204ul

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
/* Type-2 packet, a filler dword; type-3 with n+1 dwords following. */
#define RC_PACKET2 0x80000000ul
#define RC_PACKET3(op, n) ((DWORD)(op) | ((DWORD)(n) << 16))
#define RC_3D_RNDR_GEN_PRIM 0xc0002500ul

/* The 3D state X's Render path sets (r128_exa_render.c COMPOSITE_SETUP and
 * R128CCEPrepareComposite), less texturing: values from r128_reg.h. */
#define RC_SCALE_3D_CNTL 0x1a00ul
#define RC_SETUP_CNTL 0x1bc4ul
#define RC_WINDOW_XY_OFFSET 0x1bccul
#define RC_PM4_VC_FPU_SETUP 0x071cul
#define RC_TEX_CNTL_C 0x1c9cul
#define RC_MISC_3D_STATE_CNTL_REG 0x1ca0ul
#define RC_CONSTANT_COLOR_C 0x1d34ul
#define RC_PLANE_3D_MASK_C 0x1d44ul

/* SCALE_3D_TEXMAP_SHADE (2 << 6) | SCALE_PIX_REPLICATE (1 << 8) |
 * TEX_CACHE_LINE_SIZE_4QW (1 << 31). */
#define RC_SCALE_3D_VALUE 0x80000180ul
/* COLOR_GOURAUD (4 << 3) | PRIM_TYPE_TRI | TEXTURE_ST_MULT_W |
 * STARTING_VERTEX_1 (1 << 14) | ENDING_VERTEX_3 (3 << 16) |
 * SUB_PIX_4BITS (1 << 19). */
#define RC_SETUP_VALUE 0x000b4020ul
/* FRONT_DIR_CCW | BACKFACE_SOLID (3 << 1) | FRONTFACE_SOLID (3 << 3) |
 * FPU_COLOR_GOURAUD (2 << 5) | FPU_SUB_PIX_4BITS (1 << 7) | FPU_MODE_3D
 * (1 << 8) | TRAP_BITS_DISABLE (1 << 9) | FLAT_SHADE_VERTEX_OGL (1 << 14):
 * nothing culled. */
#define RC_FPU_SETUP_VALUE 0x000043dful
/* MISC_SCALE_3D_TEXMAP_SHADE (2 << 8) | MISC_SCALE_PIX_REPLICATE (1 << 10)
 * | ALPHA_COMB_ADD_CLAMP | blend source ONE (1 << 16), destination ZERO. */
#define RC_MISC_3D_VALUE 0x00010600ul
/* Shading only, no texture, Z, alpha or fog; TEX_CACHE_FLUSH (1 << 23). */
#define RC_TEX_CNTL_VALUE 0x00800000ul
/* The 2D master control with GMC_3D_FCN_EN (1 << 27) and
 * GMC_WR_MSK_DIS (1 << 30), as X's Render path sets it. */
#define RC_GMC_3D_EXTRA 0x48000000ul

/* Vertex format DIFFUSE_ARGB: X, Y, Z as floats, then the packed colour.
 * VC_CNTL: TRI_LIST (4) | WALK_RING (30h) | 3 vertices. */
#define RC_VC_FRMT_DIFFUSE_ARGB 0x00000008ul
#define RC_VC_FRMT_RHW 0x00000001ul
#define RC_VC_FRMT_SPEC_FRGB 0x00000040ul
/* The context block the DRM emits from here (r128_state.c
 * r128_emit_context): DST_PITCH_OFFSET_C, DP_GUI_MASTER_CNTL_C,
 * SC_TOP_LEFT_C, SC_BOTTOM_RIGHT_C, ... */
#define RC_DST_PITCH_OFFSET_C 0x1c80ul

/* Triangle variants, each adding to the one before. */
#define TRI_PLAIN 0
#define TRI_CONTEXT 1
#define TRI_RHW 2

/* What follows a Gouraud test's primitive. */
#define TAIL_NONE 0
#define TAIL_STATE 1
#define TAIL_DEGENERATE 2
#define TAIL_DONT_START 3
#define RC_VC_CNTL_TRI_LIST_3 0x00030034ul

/* IEEE single-precision constants, so the probe needs no floating point. */
#define RC_F_0 0x00000000ul
#define RC_F_1 0x3f800000ul
#define RC_F_2 0x40000000ul
#define RC_F_4 0x40800000ul
#define RC_F_30 0x41f00000ul
#define RC_F_60 0x42700000ul

/* The triangle: (4,2), (60,2), (4,30) on the 64x32 surface, opaque blue,
 * which is 001Fh at 5:6:5. */
#define RC_TRI_ARGB 0xff0000fful
#define RC_TRI_565 0x001fu
#define RC_TRI_AREA 784ul

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

/* The bus-master mode and its CCE FIFO: 192BM (Linux's default) unless
 * /m8 asks for mode 8, 64BM_64VCBM_64INDBM, which ATI's driver runs
 * (PM4_BUFFER_CNTL 80000011, boot 520) and X's r128 defaults to. */
static DWORD rc_bm_mode = RC_PM4_192BM;
static DWORD rc_bm_fifo = RC_PIO_FIFO_ENTRIES;
/* /wb: let the CCE write its read pointer back, as ATI's driver does
 * (PM4_BUFFER_DL_RPTR_ADDR 0D6B6030, NOUPDATE clear), to the start of the
 * probe's own locked block, which /vgart does not otherwise use. */
static DWORD rc_bm_noupdate = RC_PM4_NOUPDATE;
static DWORD rc_rptr_writeback;
/* /ati: ATI's own values around the ring (boot 520): AGP_CNTL 00000038
 * (a 32 MiB aperture, bits 22:20 clear), BUS_CNTL 5033A300 (bus mastering
 * on, bit 29 clear), PM4_BUFFER_WM_CNTL 40070707. */
static int rc_ati_values;
static DWORD rc_agp_cntl_found;
/* /zero: PM4_BUFFER_OFFSET as the ring's distance from AGP_BASE, without
 * AGP_APER_OFFSET, as ATI's own 00000000 reads. */
static int rc_offset_from_agp_base;
#define RC_AGP_CNTL 0x0174ul
#define RC_ATI_AGP_CNTL 0x00000038ul
#define RC_ATI_BUS_CNTL 0x5033a300ul
#define RC_ATI_WM_CNTL 0x40070707ul
#define RC_PM4_BUFFER_DL_RPTR_ADDR 0x070cul
#define RC_PM4_64BM_64VCBM_64INDBM 0x80000000ul
#define RC_PM4_64_FIFO_ENTRIES 64ul

/* The /gart test's table, and the registers it puts back. With /agp,
 * rc_agp_base is non-zero and rc_ring_offset is the ring's place in the
 * card's address space. */
static DWORD rc_gart_table;
static DWORD rc_bus_cntl_found;
static DWORD rc_gart_page_found;
static DWORD rc_agp_base;
static DWORD rc_agp_base_found;
static DWORD rc_ring_offset;

/* One ring dword, at dword index `at`: the VxD's locked block for /gart and
 * /agp, the framebuffer for /ring. */
static void rc_add_ring_dword(DWORD at, DWORD value)
{
    if (rc_gart_table != 0u) {
        rc_add(RC_OP_RING_WRITE32, at * 4u, value, 0u);
    } else {
        rc_add(RC_OP_LFB_WRITE32, RC_RING_BASE + at * 4u, value, 0u);
    }
}

static DWORD rc_add_ring_read(DWORD at)
{
    if (rc_gart_table != 0u) {
        return rc_add(RC_OP_RING_READ32, at * 4u, 0u, 0u);
    }
    return rc_add(RC_OP_LFB_READ32, RC_RING_BASE + at * 4u, 0u, 0u);
}

/* The SDK's R128_CCEInit and CCESetupBusMaster (5.2): the microengine
 * stopped and the engine soft-reset first, then the ring's address, both
 * pointers, the watermarks; for /agp and /gart, the window to the block and
 * bus mastering. Without the reset first, writes of 0 to DL_RPTR did not
 * take: it kept the previous run's 12 (boot 535). */
static void rc_add_ring_init(void)
{
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    if (rc_agp_base != 0u) {
        rc_add_write(RC_AGP_BASE, rc_agp_base);
        rc_add_write(RC_BUS_CNTL, rc_bus_cntl_found & ~RC_BUS_MASTER_DIS);
        rc_add_write(RC_PM4_BUFFER_OFFSET, rc_ring_offset);
    } else if (rc_gart_table != 0u) {
        rc_add_write(RC_PCI_GART_PAGE, rc_gart_table);
        rc_add_write(RC_BUS_CNTL, rc_bus_cntl_found & ~RC_BUS_MASTER_DIS);
        rc_add_write(RC_PM4_BUFFER_OFFSET, RC_AGP_OFFSET);
    } else {
        rc_add_write(RC_PM4_BUFFER_OFFSET, RC_RING_BASE);
    }
    rc_add_write(RC_PM4_BUFFER_DL_WPTR, 0u);
    rc_add_write(RC_PM4_BUFFER_DL_RPTR, 0u);
    rc_add_write(RC_PM4_BUFFER_WM_CNTL,
                 rc_ati_values ? RC_ATI_WM_CNTL : RC_RING_WM);
    if (rc_ati_values) {
        rc_add_write(RC_AGP_CNTL, RC_ATI_AGP_CNTL);
        rc_add_write(RC_BUS_CNTL, RC_ATI_BUS_CNTL);
    }
    if (rc_bm_noupdate == 0u) {
        rc_add_write(RC_PM4_BUFFER_DL_RPTR_ADDR, rc_rptr_writeback);
    }
    rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
}

/*
 * Did the CCE fetch the ring at all? One packet writing `magic` into
 * GUI_SCRATCH_REG0, and a type-2 pad: no drawing, nothing to flush. The
 * register is cleared by MMIO first and read back after RPTR reaches the
 * write pointer. Returns the next write pointer, 0 on a timeout.
 */
#define RC_GUI_SCRATCH_REG0 0x15e0ul

static DWORD rc_ring_scratch(const char *name, DWORD start, DWORD magic)
{
    char key[64];
    DWORD at = start;
    DWORD poll_index;
    DWORD wait_index;
    DWORD scratch_index;
    DWORD rptr_index;
    DWORD stat_index;
    DWORD poll_result;
    DWORD wait_result;
    DWORD scratch;

    rc_begin();
    rc_add_write(RC_GUI_SCRATCH_REG0, 0u);
    rc_add_ring_dword(at++, RC_PACKET0(RC_GUI_SCRATCH_REG0));
    rc_add_ring_dword(at++, magic);
    if (start == 0u) {
        rc_add_ring_init();
    }
    rc_add_write(RC_PM4_BUFFER_CNTL,
                 rc_bm_mode | RC_RING_L2QW | rc_bm_noupdate);
    rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, RC_MICRO_FREERUN);
    rc_add_write(RC_PM4_BUFFER_DL_WPTR, at | RC_PM4_BUFFER_DL_DONE);
    poll_index = rc_add(RC_OP_MMIO_POLL, RC_PM4_BUFFER_DL_RPTR, 0xfffffffful,
                        at);
    wait_index = rc_add(RC_OP_PM4_WAIT_IDLE, 0u, rc_bm_fifo, 0u);
    scratch_index = rc_add(RC_OP_MMIO_READ32, RC_GUI_SCRATCH_REG0, 0u, 0u);
    rptr_index = rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_DL_RPTR, 0u, 0u);
    stat_index = rc_add(RC_OP_MMIO_READ32, RC_PM4_STAT, 0u, 0u);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "NOT-RUN");
        rc_write_decimal(rc_key(key, name, "RefusedOp"),
                         rc_result_buffer.refused);
        rc_failed = 1u;
        return 0u;
    }
    poll_result = rc_result_buffer.ops[poll_index].result;
    wait_result = rc_result_buffer.ops[wait_index].result;
    scratch = rc_result_buffer.ops[scratch_index].result;
    rc_write_hex(rc_key(key, name, "Magic"), magic);
    rc_write_hex(rc_key(key, name, "Scratch"), scratch);
    rc_write_hex(rc_key(key, name, "RptrPoll"), poll_result);
    rc_write_hex(rc_key(key, name, "Pm4Wait"), wait_result);
    rc_write_hex(rc_key(key, name, "Rptr"),
                 rc_result_buffer.ops[rptr_index].result);
    rc_write_hex(rc_key(key, name, "Pm4Stat"),
                 rc_result_buffer.ops[stat_index].result);

    rc_begin();
    if (poll_result == RC_TIMEOUT || wait_result == RC_TIMEOUT) {
        rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    }
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    if (poll_result == RC_TIMEOUT || wait_result == RC_TIMEOUT) {
        rc_add_write(RC_PM4_BUFFER_DL_WPTR, 0u);
        rc_add_write(RC_PM4_BUFFER_DL_RPTR, 0u);
    }
    rc_run();
    if (poll_result == RC_TIMEOUT || wait_result == RC_TIMEOUT) {
        rc_write(rc_key(key, name, "Result"), "TIMEOUT");
        rc_failed = 1u;
        return 0u;
    }
    if (scratch != magic) {
        rc_write(rc_key(key, name, "Result"), "NOT-FETCHED");
        rc_failed = 1u;
        return at;
    }
    rc_write(rc_key(key, name, "Result"), "PASS");
    return at;
}

/*
 * The control for rc_ring_scratch: the same packet through the PIO ports,
 * with or without the engine soft-reset the ring set-up now starts with.
 */
static void rc_pio_scratch(const char *name, DWORD magic, int reset)
{
    char key[64];
    DWORD wait_index;
    DWORD scratch_index;
    DWORD stat_index;
    DWORD wait_result;
    DWORD scratch;

    rc_begin();
    rc_add_write(RC_GUI_SCRATCH_REG0, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    if (reset) {
        rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    }
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_192PIO | RC_PM4_NOUPDATE);
    rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, RC_MICRO_FREERUN);
    rc_add_write(RC_PM4_FIFO_DATA_EVEN, RC_PACKET0(RC_GUI_SCRATCH_REG0));
    rc_add_write(RC_PM4_FIFO_DATA_ODD, magic);
    wait_index = rc_add(RC_OP_PM4_WAIT_IDLE, 0u, RC_PIO_FIFO_ENTRIES, 0u);
    scratch_index = rc_add(RC_OP_MMIO_READ32, RC_GUI_SCRATCH_REG0, 0u, 0u);
    stat_index = rc_add(RC_OP_MMIO_READ32, RC_PM4_STAT, 0u, 0u);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "NOT-RUN");
        rc_failed = 1u;
        return;
    }
    wait_result = rc_result_buffer.ops[wait_index].result;
    scratch = rc_result_buffer.ops[scratch_index].result;
    rc_write_hex(rc_key(key, name, "Scratch"), scratch);
    rc_write_hex(rc_key(key, name, "Pm4Wait"), wait_result);
    rc_write_hex(rc_key(key, name, "Pm4Stat"),
                 rc_result_buffer.ops[stat_index].result);
    rc_begin();
    if (wait_result == RC_TIMEOUT) {
        rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    }
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    rc_run();
    if (wait_result == RC_TIMEOUT || scratch != magic) {
        rc_failed = 1u;
    }
    rc_write(rc_key(key, name, "Result"),
             wait_result == RC_TIMEOUT ? "TIMEOUT" :
             scratch == magic ? "PASS" : "NOT-WRITTEN");
}

/*
 * One fill through a ring. The packets are rc_pio_fill's, written to the
 * ring at `start` dwords; the CCE is set up as r128_cce_init_ring_buffer
 * and r128_do_cce_start do; WPTR moves to the end of the packets, and the
 * test waits for DL_RPTR to reach it, then for PM4 idle. With rc_gart_table
 * zero the ring is in video memory at RC_RING_BASE, with no AGP bit and bus
 * mastering off (the hypothesis 2026-10-23 killed); otherwise it is the
 * VxD's locked block through the PCI GART. Returns the write pointer the
 * next call starts from.
 */
static DWORD rc_ring_fill(const char *name, DWORD start, DWORD color)
{
    struct v9x_r128_fill fill;
    struct v9x_r128_blt blt;
    char key[64];
    DWORD index;
    DWORD x;
    DWORD y;
    DWORD at;
    DWORD mismatches = 0u;
    DWORD poll_index;
    DWORD wait_index;
    DWORD rptr_index;
    DWORD stat_index;
    DWORD addr_index;
    DWORD ring_index;
    DWORD poll_result;
    DWORD wait_result;
    const unsigned char *actual;

    fill.vram_bytes = TEST_VRAM;
    fill.target_offset = TEST_BASE;
    fill.pitch_bytes = TEST_PITCH;
    fill.bytes_per_pixel = 2u;
    fill.left = 7u;
    fill.top = 4u;
    fill.width = 20u;
    fill.height = 9u;
    fill.color = color;
    if (v9x_r128_build_fill(&fill, &blt) != V9X_STATUS_OK) {
        rc_write(rc_key(key, name, "Result"), "BUILDER-REFUSED");
        rc_failed = 1u;
        return start;
    }
    for (index = 0u; index < TEST_BYTES; ++index) {
        rc_model[index] = 0xa5u;
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
    /* The packets into the ring, each register a type-0 header and its
     * value: an even count, so no type-2 pad is needed. */
    at = start;
    for (index = 0u; index < blt.count; ++index) {
        rc_add_ring_dword(at++, RC_PACKET0(blt.offsets[index]));
        rc_add_ring_dword(at++, blt.values[index]);
    }
    ring_index = rc_add_ring_read(start);
    if (start == 0u) {
        rc_add_ring_init();
    }
    /* r128_do_cce_start in bus-master mode. */
    rc_add_write(RC_PM4_BUFFER_CNTL,
                 rc_bm_mode | RC_RING_L2QW | rc_bm_noupdate);
    rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, RC_MICRO_FREERUN);
    rc_add_write(RC_PM4_BUFFER_DL_WPTR, at | RC_PM4_BUFFER_DL_DONE);
    poll_index = rc_add(RC_OP_MMIO_POLL, RC_PM4_BUFFER_DL_RPTR, 0xfffffffful,
                        at);
    wait_index = rc_add(RC_OP_PM4_WAIT_IDLE, 0u, rc_bm_fifo, 0u);
    rptr_index = rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_DL_RPTR, 0u, 0u);
    stat_index = rc_add(RC_OP_MMIO_READ32, RC_PM4_STAT, 0u, 0u);
    addr_index = rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "NOT-RUN");
        rc_write_decimal(rc_key(key, name, "RefusedOp"),
                         rc_result_buffer.refused);
        rc_failed = 1u;
        return start;
    }
    poll_result = rc_result_buffer.ops[poll_index].result;
    wait_result = rc_result_buffer.ops[wait_index].result;
    rc_write_decimal(rc_key(key, name, "Start"), start);
    rc_write_decimal(rc_key(key, name, "Wptr"), at);
    rc_write_hex(rc_key(key, name, "RingFirstDword"),
                 rc_result_buffer.ops[ring_index].result);
    rc_write_hex(rc_key(key, name, "RptrPoll"), poll_result);
    rc_write_hex(rc_key(key, name, "Pm4Wait"), wait_result);
    rc_write_hex(rc_key(key, name, "Rptr"),
                 rc_result_buffer.ops[rptr_index].result);
    rc_write_hex(rc_key(key, name, "Pm4Stat"),
                 rc_result_buffer.ops[stat_index].result);
    rc_write_hex(rc_key(key, name, "Pm4BufferAddr"),
                 rc_result_buffer.ops[addr_index].result);

    /* r128_do_cce_stop, then the surface on an idle engine; a timeout
     * resets the engine first and puts both pointers back to 0. */
    rc_begin();
    if (poll_result == RC_TIMEOUT || wait_result == RC_TIMEOUT) {
        rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    }
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    if (poll_result == RC_TIMEOUT || wait_result == RC_TIMEOUT) {
        rc_add_write(RC_PM4_BUFFER_DL_WPTR, 0u);
        rc_add_write(RC_PM4_BUFFER_DL_RPTR, 0u);
    }
    wait_index = rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    rc_add(RC_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "STOP-NOT-RUN");
        rc_failed = 1u;
        return start;
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
    rc_write_hex(rc_key(key, name, "PixelAt7x4"),
                 rc_result_buffer.block[(4u * TEST_PITCH + 7u * 2u) / 4u]);
    if (poll_result == RC_TIMEOUT) {
        rc_write(rc_key(key, name, "Result"), "RPTR-NOT-REACHED");
        rc_failed = 1u;
        return 0u;
    }
    if (wait_result == RC_TIMEOUT) {
        rc_write(rc_key(key, name, "Result"), "CCE-NOT-IDLE");
        rc_failed = 1u;
        return 0u;
    }
    if (mismatches != 0u) {
        rc_failed = 1u;
    }
    rc_write(rc_key(key, name, "Result"),
             mismatches == 0u ? "PASS" : "MISMATCH");
    return at;
}

/* A packet stream to the PIO ports in EVEN/ODD pairs, padded with a type-2
 * filler to a whole pair. */
static void rc_add_stream(const DWORD *dwords, DWORD count)
{
    DWORD index;

    for (index = 0u; index < count; ++index) {
        rc_add_write((index & 1u) != 0u ? RC_PM4_FIFO_DATA_ODD :
                                          RC_PM4_FIFO_DATA_EVEN,
                     dwords[index]);
    }
    if ((count & 1u) != 0u) {
        rc_add_write(RC_PM4_FIFO_DATA_ODD, RC_PACKET2);
    }
}

static DWORD rc_surface_pixel(const unsigned char *bytes, DWORD x, DWORD y)
{
    return (DWORD)bytes[y * TEST_PITCH + x * 2u] |
           ((DWORD)bytes[y * TEST_PITCH + x * 2u + 1u] << 8);
}

/*
 * One untextured triangle through 3D_RNDR_GEN_PRIM, on the 2D destination
 * with GMC_3D_FCN_EN, as X's Render path draws its quads. The rasterizer's
 * edge rules are not known yet, so this does not demand a byte model: it
 * publishes how many pixels changed against the triangle's area, the
 * colour inside, a pixel just outside the hypotenuse, and how many changed
 * pixels lie outside the triangle's bounding box (which must be none).
 */
static void rc_pio_triangle(const char *name, int variant,
                            DWORD dst_pitch_offset)
{
    DWORD stream[80];
    DWORD count = 0u;
    DWORD wait_index;
    DWORD wait_result;
    DWORD x;
    DWORD y;
    DWORD vertex;
    DWORD changed = 0u;
    DWORD outside = 0u;
    DWORD pixel;
    DWORD gmc;
    char key[64];
    const unsigned char *actual;
    static const DWORD positions[3][2] = {
        { RC_F_4, RC_F_2 }, { RC_F_60, RC_F_2 }, { RC_F_4, RC_F_30 }
    };

    gmc = V9X_R128_GMC_DST_PITCH_OFFSET_CNTL |
          V9X_R128_GMC_BRUSH_SOLID_COLOR |
          (V9X_R128_DATATYPE_16BPP << V9X_R128_GMC_DST_DATATYPE_SHIFT) |
          V9X_R128_GMC_SRC_DATATYPE_COLOR |
          (V9X_R128_ROP3_S << V9X_R128_GMC_ROP3_SHIFT) |
          V9X_R128_DP_SRC_SOURCE_MEMORY |
          V9X_R128_GMC_CLR_CMP_CNTL_DIS |
          V9X_R128_GMC_AUX_CLIP_DIS | RC_GMC_3D_EXTRA;
    stream[count++] = RC_PACKET0(V9X_R128_DST_PITCH_OFFSET);
    stream[count++] = dst_pitch_offset;
    stream[count++] = RC_PACKET0(V9X_R128_DP_GUI_MASTER_CNTL);
    stream[count++] = gmc;
    if (variant >= TRI_CONTEXT) {
        /* The DRM's context block, first four: destination, master
         * control and the 3D scissor, which POST leaves at zero. */
        stream[count++] = RC_PACKET0(RC_DST_PITCH_OFFSET_C) |
                          (3ul << 16);
        stream[count++] = dst_pitch_offset;
        stream[count++] = gmc;
        stream[count++] = 0ul;
        stream[count++] = V9X_R128_SC_MAX;
    }
    stream[count++] = RC_PACKET0(V9X_R128_DP_CNTL);
    stream[count++] = V9X_R128_DST_X_LEFT_TO_RIGHT |
                      V9X_R128_DST_Y_TOP_TO_BOTTOM;
    stream[count++] = RC_PACKET0(RC_SCALE_3D_CNTL);
    stream[count++] = RC_SCALE_3D_VALUE;
    stream[count++] = RC_PACKET0(RC_SETUP_CNTL);
    stream[count++] = RC_SETUP_VALUE;
    stream[count++] = RC_PACKET0(RC_PM4_VC_FPU_SETUP);
    stream[count++] = RC_FPU_SETUP_VALUE;
    stream[count++] = RC_PACKET0(RC_PLANE_3D_MASK_C);
    stream[count++] = 0xfffffffful;
    stream[count++] = RC_PACKET0(RC_CONSTANT_COLOR_C);
    stream[count++] = 0xff000000ul;
    stream[count++] = RC_PACKET0(RC_WINDOW_XY_OFFSET);
    stream[count++] = 0ul;
    stream[count++] = RC_PACKET0(RC_MISC_3D_STATE_CNTL_REG);
    stream[count++] = RC_MISC_3D_VALUE;
    stream[count++] = RC_PACKET0(RC_TEX_CNTL_C);
    stream[count++] = RC_TEX_CNTL_VALUE;
    /* Format, control and three vertices of four dwords (five with RHW):
     * n counts the dwords after the header, less one. */
    stream[count++] = RC_PACKET3(RC_3D_RNDR_GEN_PRIM,
                                 variant >= TRI_RHW ? 16u : 13u);
    stream[count++] = RC_VC_FRMT_DIFFUSE_ARGB |
                      (variant >= TRI_RHW ? RC_VC_FRMT_RHW : 0ul);
    stream[count++] = RC_VC_CNTL_TRI_LIST_3;
    for (vertex = 0u; vertex < 3u; ++vertex) {
        stream[count++] = positions[vertex][0];
        stream[count++] = positions[vertex][1];
        stream[count++] = RC_F_0;
        if (variant >= TRI_RHW) {
            stream[count++] = RC_F_1;
        }
        stream[count++] = RC_TRI_ARGB;
    }

    rc_begin();
    rc_add(RC_OP_LFB_FILL32, TEST_BASE, TEST_PREFILL, TEST_DWORDS);
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_192PIO | RC_PM4_NOUPDATE);
    rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, RC_MICRO_FREERUN);
    rc_add_stream(stream, count);
    wait_index = rc_add(RC_OP_PM4_WAIT_IDLE, 0u, RC_PIO_FIFO_ENTRIES, 0u);
    rc_add(RC_OP_MMIO_READ32, RC_PM4_STAT, 0u, 0u);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "NOT-RUN");
        rc_write_decimal(rc_key(key, name, "RefusedOp"), rc_result_buffer.refused);
        rc_failed = 1u;
        return;
    }
    wait_result = rc_result_buffer.ops[wait_index].result;
    rc_write_hex(rc_key(key, name, "Pm4Wait"), wait_result);
    rc_write_hex(rc_key(key, name, "Pm4Stat"),
                 rc_result_buffer.ops[wait_index + 1u].result);

    /* Stop the microengine first, then reset the engine whatever the wait
     * said: a primitive that drew nothing may have left the setup engine
     * part-way through a packet, and A8U4I5 hung idle about a minute after
     * the first triangle run left it so (boot 522). */
    rc_begin();
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    /* Put back what the 2D tests and the driver expect of these. */
    rc_add_write(RC_SCALE_3D_CNTL, 0u);
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    rc_add(RC_OP_LFB_READ_BLOCK, TEST_BASE, 0u, TEST_DWORDS);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "STOP-NOT-RUN");
        rc_failed = 1u;
        return;
    }
    actual = (const unsigned char *)rc_result_buffer.block;
    for (y = 0u; y < TEST_BYTES / TEST_PITCH; ++y) {
        for (x = 0u; x < TEST_PITCH / 2u; ++x) {
            if (rc_surface_pixel(actual, x, y) == (TEST_PREFILL & 0xffffu)) {
                continue;
            }
            ++changed;
            if (x < 3u || x > 61u || y < 1u || y > 31u) {
                ++outside;
            }
        }
    }
    pixel = rc_surface_pixel(actual, 10u, 6u);
    rc_write_decimal(rc_key(key, name, "ChangedPixels"), changed);
    rc_write_decimal(rc_key(key, name, "ExpectedAbout"), RC_TRI_AREA);
    rc_write_decimal(rc_key(key, name, "ChangedOutsideBox"), outside);
    rc_write_hex(rc_key(key, name, "PixelAt10x6"), pixel);
    rc_write_hex(rc_key(key, name, "PixelAt58x28"),
                 rc_surface_pixel(actual, 58u, 28u));
    if (wait_result == RC_TIMEOUT) {
        rc_write(rc_key(key, name, "Result"), "CCE-NOT-IDLE");
        return;
    }
    if (pixel == RC_TRI_565 && outside == 0u &&
        changed > RC_TRI_AREA - RC_TRI_AREA / 8u &&
        changed < RC_TRI_AREA + RC_TRI_AREA / 8u) {
        rc_write(rc_key(key, name, "Result"), "DRAWN");
    } else if (changed == 0u) {
        rc_write(rc_key(key, name, "Result"), "NOTHING-DRAWN");
    } else {
        rc_write(rc_key(key, name, "Result"), "DRAWN-DIFFERENTLY");
    }
}

/*
 * The builder's tests (src\chipsets\ati\rage128\r128_3d.c): Gouraud colour,
 * Z, and a texture, each a state stream and full TL vertices as the HAL
 * will send them. Surfaces beside the 4 KiB target at 16 MiB: the Z buffer
 * 64 KiB on, the texture 128 KiB on, both 64-pixel/16-texel rows of 16 bpp.
 */
#define RC_Z_BASE (TEST_BASE + 0x00010000ul)
#define RC_TEX_BASE (TEST_BASE + 0x00020000ul)
#define RC_TEX_SIZE 16ul
#define RC_TEX_PITCH (RC_TEX_SIZE * 2ul)

struct rc_vertex {
    float x;
    float y;
    float z;
    float rhw;
    DWORD color;
    DWORD specular;
    float s;
    float t;
};

typedef char rc_vertex_is_8_dwords[sizeof(struct rc_vertex) == 32u ? 1 : -1];

static DWORD rc_stream[640];
/* Pixel (6,4) of the target, read after the prefill and the idle wait,
 * just before the CCE starts. */
static DWORD rc_pre_stream_pixel;
/* /noflush: leave out the CPU-side GUI cache flush, to show it is the
 * flush that makes each draw visible. */
static int rc_no_cpu_flush;
/* /ring: run the ring tests in place of the triangle tests. */
static int rc_ring;
/* /gart: the same, with the ring in system memory through the PCI GART.
 * /gpn: the same, the table holding page numbers rather than addresses. */
static int rc_gart;
static int rc_gart_page_numbers;
/* /agp: the locked block through AGP_BASE, no table. */
static int rc_agp;
/* /vgart: pages of the chipset's AGP aperture from the VMM's GART
 * services, behind AGP_BASE as ATI's driver keeps its ring. */
static int rc_vgart;
static DWORD rc_stream_count;

static void rc_stream_state(const struct v9x_r128_3d_stream *state)
{
    DWORD index;

    for (index = 0u; index < state->count; ++index) {
        rc_stream[rc_stream_count++] = state->dwords[index];
    }
}

/* One 3D_RNDR_GEN_PRIM of `count` triangles, vertices as they are. */
static int rc_stream_prim(const struct rc_vertex *vertices, DWORD count)
{
    v9x_u32 header[V9X_R128_PRIM_HEADER_DWORDS];
    const DWORD *words = (const DWORD *)vertices;
    DWORD index;

    if (v9x_r128_3d_build_prim(count, header) != V9X_STATUS_OK) {
        return 0;
    }
    for (index = 0u; index < V9X_R128_PRIM_HEADER_DWORDS; ++index) {
        rc_stream[rc_stream_count++] = header[index];
    }
    for (index = 0u; index < count * 3u * V9X_R128_VERTEX_DWORDS; ++index) {
        rc_stream[rc_stream_count++] = words[index];
    }
    return 1;
}

/* A screen-aligned rectangle as two triangles. */
static void rc_quad(struct rc_vertex *out, float left, float top,
                    float right, float bottom, float z, DWORD color)
{
    static const float corners[6][2] = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
        { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
    };
    DWORD index;

    for (index = 0u; index < 6u; ++index) {
        out[index].x = corners[index][0] != 0.0f ? right : left;
        out[index].y = corners[index][1] != 0.0f ? bottom : top;
        out[index].z = z;
        out[index].rhw = 1.0f;
        out[index].color = color;
        out[index].specular = 0u;
        out[index].s = corners[index][0];
        out[index].t = corners[index][1];
    }
}

static void rc_base_state(struct v9x_r128_3d_state *state)
{
    DWORD index;
    BYTE *bytes = (BYTE *)state;

    for (index = 0u; index < sizeof(*state); ++index) {
        bytes[index] = 0u;
    }
    state->vram_bytes = TEST_VRAM;
    state->target_offset = TEST_BASE;
    state->target_pitch_bytes = TEST_PITCH;
    state->target_bytes_per_pixel = 2u;
    state->src_blend = V9X_R128_BLEND_ONE;
    state->dst_blend = V9X_R128_BLEND_ZERO;
    state->alpha_func = V9X_R128_CMP_ALWAYS;
}

/*
 * Run rc_stream after the ops the caller has already added (prefills), then
 * stop the microengine, reset the engine, return to NONPM4 and read
 * read_base's 4 KiB. Publishes <name>Pm4Wait. Non-zero when it all ran and
 * the CCE went idle.
 */
static int rc_run_stream(const char *name, DWORD read_base)
{
    char key[64];
    DWORD pre_index;
    DWORD gui_flush_index;
    DWORD wait_index;
    DWORD wait_result;

    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    pre_index = rc_add(RC_OP_LFB_READ32,
                       read_base + 4u * TEST_PITCH + 6u * 2u, 0u, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_192PIO | RC_PM4_NOUPDATE);
    rc_add(RC_OP_MMIO_READ32, RC_PM4_BUFFER_ADDR, 0u, 0u);
    rc_add_write(RC_PM4_MICRO_CNTL, RC_MICRO_FREERUN);
    /* The batch ends by flushing the GUI pixel cache, as the HAL will. */
    v9x_r128_3d_build_flush(&rc_stream[rc_stream_count]);
    rc_stream_count += V9X_R128_FLUSH_DWORDS;
    rc_add_stream(rc_stream, rc_stream_count);
    wait_index = rc_add(RC_OP_PM4_WAIT_IDLE, 0u, RC_PIO_FIFO_ENTRIES, 0u);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "NOT-RUN");
        rc_write_decimal(rc_key(key, name, "RefusedOp"),
                         rc_result_buffer.refused);
        rc_failed = 1u;
        return 0;
    }
    wait_result = rc_result_buffer.ops[wait_index].result;
    rc_pre_stream_pixel = rc_result_buffer.ops[pre_index].result & 0xfffful;
    rc_write_hex(rc_key(key, name, "Pm4Wait"), wait_result);

    rc_begin();
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    /* The GUI pixel cache flushed from the CPU side too, before the reset
     * and the read: the stream's own flush packet did not change what the
     * read saw (boot 524). */
    if (!rc_no_cpu_flush) {
        rc_add_write(V9X_R128_PC_GUI_CTLSTAT, V9X_R128_PC_FLUSH_GUI);
    }
    gui_flush_index = rc_add(RC_OP_MMIO_READ32, V9X_R128_PC_GUI_CTLSTAT,
                             0u, 0u);
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    rc_add(RC_OP_MMIO_READ32, V9X_R128_PC_GUI_CTLSTAT, 0u, 0u);
    rc_add(RC_OP_ENGINE_RESET, 0u, 0u, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    rc_add_write(RC_SCALE_3D_CNTL, 0u);
    rc_add(RC_OP_WAIT_IDLE, 0u, 0u, 0u);
    rc_add(RC_OP_LFB_READ_BLOCK, read_base, 0u, TEST_DWORDS);
    if (!rc_run()) {
        rc_write(rc_key(key, name, "Result"), "STOP-NOT-RUN");
        rc_failed = 1u;
        return 0;
    }
    rc_write_hex(rc_key(key, name, "PcGuiAfterFlush"),
                 rc_result_buffer.ops[gui_flush_index].result);
    rc_write_hex(rc_key(key, name, "PcGuiAfterIdle"),
                 rc_result_buffer.ops[gui_flush_index + 2u].result);
    if (wait_result == RC_TIMEOUT) {
        rc_write(rc_key(key, name, "Result"), "CCE-NOT-IDLE");
        rc_failed = 1u;
        return 0;
    }
    return 1;
}

static DWORD rc_block_pixel(DWORD x, DWORD y)
{
    return rc_surface_pixel((const unsigned char *)rc_result_buffer.block,
                            x, y);
}

/*
 * One triangle, (4,2), (60,2), (4,30), with the three vertex colours
 * given; near each corner the corner's channel must lead the other two
 * (only meaningful for distinct colours). vertex_words picks the format:
 * 8 is the builder's TL vertex; 6 is X, Y, Z, RHW, diffuse, specular; 5
 * drops the specular, as the earlier triangle tests drew.
 */
static void rc_test_gouraud(const char *name, DWORD c0, DWORD c1, DWORD c2,
                            DWORD vertex_words, float z, int use_depth,
                            int tail)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;
    struct rc_vertex triangle[3];
    char key[64];
    DWORD colours[3];
    DWORD vertex;
    DWORD red;
    DWORD green;
    DWORD blue;
    int ok;

    colours[0] = c0;
    colours[1] = c1;
    colours[2] = c2;
    rc_base_state(&state);
    if (use_depth) {
        state.depth_enable = 1u;
        state.depth_write = 1u;
        state.depth_func = V9X_R128_CMP_ALWAYS;
        state.depth_offset = RC_Z_BASE;
        state.depth_pitch_bytes = TEST_PITCH;
        state.depth_bytes_per_pixel = 2u;
    }
    if (v9x_r128_3d_build_state(&state, &stream) != V9X_STATUS_OK) {
        rc_write(rc_key(key, name, "Result"), "BUILDER-REFUSED");
        rc_failed = 1u;
        return;
    }
    for (vertex = 0u; vertex < 3u; ++vertex) {
        triangle[vertex].x = vertex == 1u ? 60.0f : 4.0f;
        triangle[vertex].y = vertex == 2u ? 30.0f : 2.0f;
        triangle[vertex].z = z;
        triangle[vertex].rhw = 1.0f;
        triangle[vertex].color = colours[vertex];
        triangle[vertex].specular = 0u;
        triangle[vertex].s = 0.0f;
        triangle[vertex].t = 0.0f;
    }
    rc_stream_count = 0u;
    rc_stream_state(&stream);
    if (tail == TAIL_DONT_START) {
        /* SETUP_CNTL is the third burst's value: find it and set
         * DONT_START_TRIANGLE (bit 0). */
        DWORD at;

        for (at = 0u; at + 1u < rc_stream_count; ++at) {
            if (rc_stream[at] == RC_PACKET0(RC_SETUP_CNTL)) {
                rc_stream[at + 1u] |= 1ul;
            }
        }
    }
    if (vertex_words == V9X_R128_VERTEX_DWORDS) {
        rc_stream_prim(triangle, 1u);
    } else {
        /* RHW | DIFFUSE_ARGB, with SPEC_FRGB for six dwords: n is the
         * dwords after the header, less one. */
        rc_stream[rc_stream_count++] =
            RC_PACKET3(RC_3D_RNDR_GEN_PRIM, 2u + 3u * vertex_words - 1u);
        rc_stream[rc_stream_count++] = RC_VC_FRMT_DIFFUSE_ARGB |
                                       RC_VC_FRMT_RHW |
                                       (vertex_words == 6u ?
                                        RC_VC_FRMT_SPEC_FRGB : 0ul);
        rc_stream[rc_stream_count++] = RC_VC_CNTL_TRI_LIST_3;
        for (vertex = 0u; vertex < 3u; ++vertex) {
            const DWORD *words = (const DWORD *)&triangle[vertex];
            DWORD word;

            for (word = 0u; word < vertex_words; ++word) {
                rc_stream[rc_stream_count++] = words[word];
            }
        }
    }

    if (tail == TAIL_STATE) {
        rc_stream_state(&stream);
    } else if (tail == TAIL_DEGENERATE) {
        struct rc_vertex dot[3];

        for (vertex = 0u; vertex < 3u; ++vertex) {
            dot[vertex] = triangle[0];
            dot[vertex].x = 0.0f;
            dot[vertex].y = 0.0f;
        }
        rc_stream_prim(dot, 1u);
    }

    rc_begin();
    rc_add(RC_OP_LFB_FILL32, TEST_BASE, 0u, TEST_DWORDS);
    if (!rc_run_stream(name, TEST_BASE)) {
        return;
    }
    red = rc_block_pixel(6u, 4u);
    green = rc_block_pixel(52u, 4u);
    blue = rc_block_pixel(6u, 26u);
    rc_write_hex(rc_key(key, name, "PixelBeforeStream"), rc_pre_stream_pixel);
    rc_write_hex(rc_key(key, name, "NearV0"), red);
    rc_write_hex(rc_key(key, name, "NearV1"), green);
    rc_write_hex(rc_key(key, name, "NearV2"), blue);
    rc_write_hex(rc_key(key, name, "Centre"), rc_block_pixel(20u, 10u));
    /* 5:6:5 channels scaled to five bits for the comparison. */
    ok = (red >> 11) > ((red >> 6) & 31u) && (red >> 11) > (red & 31u) &&
         ((green >> 6) & 31u) > (green >> 11) &&
         ((green >> 6) & 31u) > (green & 31u) &&
         (blue & 31u) > (blue >> 11) && (blue & 31u) > ((blue >> 6) & 31u);
    rc_write(rc_key(key, name, "Result"), ok ? "PASS" : "MISMATCH");
}

/* Z cleared to all ones; a near blue rectangle, then a far red one over the
 * whole surface: red must not cover the blue. Then the Z buffer itself,
 * read linearly: the blue's Z inside it, the red's outside. */
static void rc_test_depth(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;
    struct rc_vertex quads[12];
    DWORD inside;
    DWORD outside;
    DWORD z_inside;
    DWORD z_outside;
    int ok;

    rc_base_state(&state);
    state.depth_enable = 1u;
    state.depth_write = 1u;
    state.depth_func = V9X_R128_CMP_LESS;
    state.depth_offset = RC_Z_BASE;
    state.depth_pitch_bytes = TEST_PITCH;
    state.depth_bytes_per_pixel = 2u;
    if (v9x_r128_3d_build_state(&state, &stream) != V9X_STATUS_OK) {
        rc_write("DepthResult", "BUILDER-REFUSED");
        rc_failed = 1u;
        return;
    }
    rc_quad(&quads[0], 8.0f, 4.0f, 40.0f, 20.0f, 0.25f, 0xff0000fful);
    rc_quad(&quads[6], 0.0f, 0.0f, 64.0f, 32.0f, 0.5f, 0xffff0000ul);
    rc_stream_count = 0u;
    rc_stream_state(&stream);
    rc_stream_prim(&quads[0], 2u);
    rc_stream_prim(&quads[6], 2u);

    rc_begin();
    rc_add(RC_OP_LFB_FILL32, TEST_BASE, 0u, TEST_DWORDS);
    rc_add(RC_OP_LFB_FILL32, RC_Z_BASE, 0xfffffffful, TEST_DWORDS);
    if (!rc_run_stream("Depth", TEST_BASE)) {
        return;
    }
    inside = rc_block_pixel(20u, 10u);
    outside = rc_block_pixel(50u, 25u);
    rc_write_hex("DepthColourInside", inside);
    rc_write_hex("DepthColourOutside", outside);

    rc_begin();
    rc_add(RC_OP_LFB_READ_BLOCK, RC_Z_BASE, 0u, TEST_DWORDS);
    if (!rc_run()) {
        rc_write("DepthResult", "Z-READ-NOT-RUN");
        rc_failed = 1u;
        return;
    }
    z_inside = rc_block_pixel(20u, 10u);
    z_outside = rc_block_pixel(50u, 25u);
    rc_write_hex("DepthZInside", z_inside);
    rc_write_hex("DepthZOutside", z_outside);
    rc_write_hex("DepthZCorner63x31", rc_block_pixel(63u, 31u));
    ok = inside == 0x001fu && outside == 0xf800u;
    rc_write("DepthResult", ok ? "PASS" : "MISMATCH");
    /* Linear Z holds about 4000h and 8000h where the two rectangles are. */
    rc_write("DepthZLayout",
             z_inside >= 0x3f00u && z_inside <= 0x4100u &&
             z_outside >= 0x7f00u && z_outside <= 0x8100u ?
             "linear" : "not-linear-as-read");
    if (!ok) {
        rc_failed = 1u;
    }
}

static DWORD rc_texel(DWORD x, DWORD y)
{
    return ((y & 15u) << 11) | ((x & 15u) << 5) | ((x ^ y) & 31u);
}

/* A 16x16 RGB565 texture, every texel distinct, on a 16x16 quad at
 * (8,8), nearest, replace: pixel (8+i, 8+j) against texel (i, j), and
 * against the eight neighbours, so an off-by-one shows as a shift. */
static void rc_test_texture(void)
{
    struct v9x_r128_3d_state state;
    struct v9x_r128_3d_stream stream;
    struct rc_vertex quad[6];
    DWORD x;
    DWORD y;
    DWORD dx;
    DWORD dy;
    DWORD matches[3][3];
    DWORD best = 0u;
    DWORD best_dx = 1u;
    DWORD best_dy = 1u;
    char key[64];
    char text[8];

    rc_base_state(&state);
    state.texture.enable = 1u;
    state.texture.datatype = V9X_R128_TEX_RGB565;
    state.texture.width_log2 = 4u;
    state.texture.height_log2 = 4u;
    state.texture.levels = 1u;
    state.texture.offsets[0] = RC_TEX_BASE;
    state.texture.min_filter = V9X_R128_FILTER_NEAREST;
    state.texture.mag_filter = V9X_R128_FILTER_NEAREST;
    state.texture.clamp_s = 1u;
    state.texture.clamp_t = 1u;
    state.texture.combine = V9X_R128_COMBINE_REPLACE;
    if (v9x_r128_3d_build_state(&state, &stream) != V9X_STATUS_OK) {
        rc_write("TextureResult", "BUILDER-REFUSED");
        rc_failed = 1u;
        return;
    }
    rc_quad(quad, 8.0f, 8.0f, 24.0f, 24.0f, 0.0f, 0xfffffffful);
    rc_stream_count = 0u;
    rc_stream_state(&stream);
    rc_stream_prim(quad, 2u);

    rc_begin();
    rc_add(RC_OP_LFB_FILL32, TEST_BASE, 0u, TEST_DWORDS);
    for (y = 0u; y < RC_TEX_SIZE; ++y) {
        for (x = 0u; x < RC_TEX_SIZE; x += 2u) {
            rc_add(RC_OP_LFB_WRITE32, RC_TEX_BASE + y * RC_TEX_PITCH + x * 2u,
                   rc_texel(x, y) | (rc_texel(x + 1u, y) << 16), 0u);
        }
    }
    if (!rc_run_stream("Texture", TEST_BASE)) {
        return;
    }
    for (dy = 0u; dy < 3u; ++dy) {
        for (dx = 0u; dx < 3u; ++dx) {
            matches[dy][dx] = 0u;
        }
    }
    for (y = 0u; y < RC_TEX_SIZE; ++y) {
        for (x = 0u; x < RC_TEX_SIZE; ++x) {
            DWORD pixel = rc_block_pixel(8u + x, 8u + y);

            for (dy = 0u; dy < 3u; ++dy) {
                for (dx = 0u; dx < 3u; ++dx) {
                    if (x + dx >= 1u && y + dy >= 1u &&
                        x + dx - 1u < RC_TEX_SIZE &&
                        y + dy - 1u < RC_TEX_SIZE &&
                        pixel == rc_texel(x + dx - 1u, y + dy - 1u)) {
                        ++matches[dy][dx];
                    }
                }
            }
        }
    }
    for (dy = 0u; dy < 3u; ++dy) {
        for (dx = 0u; dx < 3u; ++dx) {
            if (matches[dy][dx] > best) {
                best = matches[dy][dx];
                best_dx = dx;
                best_dy = dy;
            }
        }
    }
    rc_write_decimal("TextureExactMatches", matches[1][1]);
    rc_write_decimal("TextureBestMatches", best);
    text[0] = (char)('0' + best_dx);
    text[1] = ',';
    text[2] = (char)('0' + best_dy);
    text[3] = '\0';
    rc_write("TextureBestShiftPlusOne", text);
    rc_write_hex("TexturePixelAt8x8", rc_block_pixel(8u, 8u));
    rc_write_hex("TexturePixelAt23x23", rc_block_pixel(23u, 23u));
    rc_write_hex("TexturePixelAt7x7", rc_block_pixel(7u, 7u));
    rc_write(rc_key(key, "Texture", "Result"),
             matches[1][1] == RC_TEX_SIZE * RC_TEX_SIZE ? "PASS" :
             best == 0u ? "NOT-SAMPLED" : "SHIFTED-OR-PARTIAL");
    if (matches[1][1] != RC_TEX_SIZE * RC_TEX_SIZE) {
        rc_failed = 1u;
    }
}

/* The PIO tests that follow the first fill when /ring is not given. */
static void rc_pio_tests(DWORD micro_found)
{
    /* The surface's pitch/offset word: 8 units of 8 pixels, 16 MiB / 32. */
    DWORD surface = ((TEST_PITCH / 2u / V9X_R128_PITCH_PIXEL_UNIT) <<
                     V9X_R128_PITCH_SHIFT) |
                    (TEST_BASE / V9X_R128_OFFSET_ALIGN);

    rc_pio_fill("PioEvenOnly", ORDER_EVEN_ONLY, micro_found);
    rc_pio_triangle("TriPlain", TRI_PLAIN, surface);
    rc_pio_triangle("TriContext", TRI_CONTEXT, surface);
    rc_pio_triangle("TriContextRhw", TRI_RHW, surface);
    rc_test_gouraud("RedPlain", 0xffff0000ul, 0xffff0000ul, 0xffff0000ul,
                    8u, 0.5f, 0, TAIL_NONE);
    rc_test_gouraud("GreenTailState", 0xff00ff00ul, 0xff00ff00ul,
                    0xff00ff00ul, 8u, 0.5f, 0, TAIL_STATE);
    rc_test_gouraud("BlueTailDegenerate", 0xff0000fful, 0xff0000fful,
                    0xff0000fful, 8u, 0.5f, 0, TAIL_DEGENERATE);
    /* TAIL_DONT_START jammed the CCE on boot 524: not run again. */
    rc_test_gouraud("RedPlainAgain", 0xffff0000ul, 0xffff0000ul,
                    0xffff0000ul, 8u, 0.5f, 0, TAIL_NONE);
    rc_test_gouraud("GouraudTailState", 0xffff0000ul, 0xff00ff00ul,
                    0xff0000fful, 8u, 0.5f, 0, TAIL_STATE);
    rc_test_depth();
    rc_test_texture();
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
    {
        const char *line = GetCommandLineA();

        while (*line != '\0') {
            if (line[0] == '/' && (line[1] == 'n' || line[1] == 'N') &&
                (line[2] == 'o' || line[2] == 'O') &&
                (line[3] == 'f' || line[3] == 'F')) {
                rc_no_cpu_flush = 1;
            }
            if (line[0] == '/' && (line[1] == 'r' || line[1] == 'R') &&
                (line[2] == 'i' || line[2] == 'I') &&
                (line[3] == 'n' || line[3] == 'N')) {
                rc_ring = 1;
            }
            if (line[0] == '/' && (line[1] == 'g' || line[1] == 'G') &&
                (line[2] == 'a' || line[2] == 'A') &&
                (line[3] == 'r' || line[3] == 'R')) {
                rc_gart = 1;
            }
            if (line[0] == '/' && (line[1] == 'g' || line[1] == 'G') &&
                (line[2] == 'p' || line[2] == 'P') &&
                (line[3] == 'n' || line[3] == 'N')) {
                rc_gart = 1;
                rc_gart_page_numbers = 1;
            }
            if (line[0] == '/' && (line[1] == 'a' || line[1] == 'A') &&
                (line[2] == 'g' || line[2] == 'G') &&
                (line[3] == 'p' || line[3] == 'P')) {
                rc_gart = 1;
                rc_agp = 1;
            }
            if (line[0] == '/' && (line[1] == 'w' || line[1] == 'W') &&
                (line[2] == 'b' || line[2] == 'B')) {
                rc_bm_noupdate = 0u;
            }
            if (line[0] == '/' && (line[1] == 'a' || line[1] == 'A') &&
                (line[2] == 't' || line[2] == 'T') &&
                (line[3] == 'i' || line[3] == 'I')) {
                rc_ati_values = 1;
            }
            if (line[0] == '/' && (line[1] == 'z' || line[1] == 'Z') &&
                (line[2] == 'e' || line[2] == 'E') &&
                (line[3] == 'r' || line[3] == 'R')) {
                rc_offset_from_agp_base = 1;
            }
            if (line[0] == '/' && (line[1] == 'm' || line[1] == 'M') &&
                line[2] == '8') {
                rc_bm_mode = RC_PM4_64BM_64VCBM_64INDBM;
                rc_bm_fifo = RC_PM4_64_FIFO_ENTRIES;
            }
            if (line[0] == '/' && (line[1] == 'v' || line[1] == 'V') &&
                (line[2] == 'g' || line[2] == 'G') &&
                (line[3] == 'a' || line[3] == 'A')) {
                rc_gart = 1;
                rc_agp = 1;
                rc_vgart = 1;
            }
            ++line;
        }
    }
    rc_write("CpuSideGuiFlush", rc_no_cpu_flush ? "off" : "on");

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
    if (rc_gart || rc_ring) {
        rc_pio_scratch("PioScratch", 0x11223344ul, 0);
        rc_pio_scratch("PioScratchAfterReset", 0x55667788ul, 1);
    }
    if (rc_gart) {
        DWORD wptr;
        DWORD alloc_index;
        DWORD wb_index = 0u;

        rc_bus_cntl_found = rc_read_register(RC_BUS_CNTL);
        rc_gart_page_found = rc_read_register(RC_PCI_GART_PAGE);
        rc_agp_cntl_found = rc_read_register(RC_AGP_CNTL);
        /* AGP_STATUS and AGP_COMMAND through BAR2's image of configuration
         * space: AGP_EN (command bit 8) clear is "PCI mode", where the PCI
         * GART applies (RRG 3-185, 3-207). */
        rc_write_hex("AgpStatus", rc_read_register(0x0f54ul));
        rc_write_hex("AgpCommand", rc_read_register(0x0f58ul));
        rc_write_hex("AgpCntl", rc_read_register(0x0174ul));
        rc_write_hex("AgpAperOffsetAtStart",
                     rc_read_register(RC_AGP_APER_OFFSET));
        rc_write_hex("BusCntlAtStart", rc_bus_cntl_found);
        rc_write_hex("PciGartPageAtStart", rc_gart_page_found);
        rc_begin();
        rc_write("GartEntries", rc_gart_page_numbers ? "page-numbers" :
                                                      "addresses");
        if (rc_vgart) {
            if (rc_bm_noupdate == 0u) {
                /* The write-back target: the locked block, allocated
                 * first so the aperture stays the ring. */
                wb_index = rc_add(RC_OP_RING_ALLOC, 0u, 0u, 0u);
            }
            alloc_index = rc_add(RC_OP_GART_ALLOC, 0u, 0u, 0u);
        } else {
            alloc_index = rc_add(RC_OP_RING_ALLOC,
                                 rc_gart_page_numbers ? 1u : 0u, 0u, 0u);
        }
        if (!rc_run() || rc_result_buffer.ops[alloc_index].result == 0u ||
            (rc_bm_noupdate == 0u &&
             rc_result_buffer.ops[wb_index].result == 0u)) {
            rc_write("GartResult", "ALLOC-REFUSED");
            rc_failed = 1u;
        } else {
            rc_gart_table = rc_result_buffer.ops[alloc_index].result;
            if (rc_bm_noupdate == 0u) {
                rc_rptr_writeback = rc_result_buffer.ops[wb_index].result;
                rc_write_hex("RptrWriteback", rc_rptr_writeback);
            }
            rc_write_hex("GartTablePhys", rc_gart_table);
            if (rc_agp) {
                /* /vgart's result is the ring's own device address; the
                 * locked block's ring follows its table. */
                DWORD ring_phys = rc_vgart ? rc_gart_table :
                                             rc_gart_table + 0x8000ul;
                DWORD aper = rc_read_register(RC_AGP_APER_OFFSET);

                rc_agp_base_found = rc_read_register(RC_AGP_BASE);
                rc_agp_base = ring_phys & ~(RC_AGP_BASE_ALIGN - 1ul);
                rc_ring_offset = (rc_offset_from_agp_base ? 0ul : aper) +
                                 (ring_phys - rc_agp_base);
                rc_write_hex("AgpBaseAtStart", rc_agp_base_found);
                rc_write_hex("AgpAperOffset", aper);
                rc_write_hex("AgpBase", rc_agp_base);
                rc_write_hex("RingCardOffset", rc_ring_offset);
            }
            wptr = rc_ring_scratch("ScratchFirst", 0u, 0x5eed1234ul);
            if (wptr != 0u) {
                wptr = rc_ring_scratch("ScratchSecond", wptr, 0xc0ffee42ul);
            }
            if (wptr != 0u) {
                (void)rc_ring_fill("GartFill", wptr, 0x9abcu);
            }
            /* Bus mastering off and the GART as found, before the VxD
             * frees the block on close. */
            rc_begin();
            rc_add_write(RC_PM4_MICRO_CNTL, 0u);
            rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
            rc_add_write(RC_BUS_CNTL, rc_bus_cntl_found);
            rc_add_write(RC_PCI_GART_PAGE, rc_gart_page_found);
            if (rc_agp) {
                rc_add_write(RC_AGP_BASE, rc_agp_base_found);
            }
            rc_add_write(RC_AGP_CNTL, rc_agp_cntl_found);
            rc_run();
            rc_write_hex("BusCntlAtEnd", rc_read_register(RC_BUS_CNTL));
            rc_write_hex("PciGartPageAtEnd",
                         rc_read_register(RC_PCI_GART_PAGE));
        }
    } else if (rc_ring) {
        DWORD wptr;

        /* The PIO fill above is the control; then two fills through the
         * ring, the second continuing from the first's pointers. */
        rc_write("Ring", "on");
        wptr = rc_ring_scratch("ScratchFirst", 0u, 0x5eed1234ul);
        if (wptr != 0u) {
            (void)rc_ring_fill("RingFill", wptr, 0x9abcu);
        }
    } else {
        rc_pio_tests(micro_found);
    }

    /* NONPM4 with the microengine stopped, as r128_do_cce_stop leaves it.
     * Not POST's free-run value: that let A8U4I5 hang idle after the first
     * triangle run (boot 522), and NONPM4 needs no microengine. */
    rc_begin();
    rc_add_write(RC_PM4_MICRO_CNTL, 0u);
    rc_add_write(RC_PM4_BUFFER_CNTL, RC_PM4_NONPM4 | RC_PM4_NOUPDATE);
    rc_run();
    rc_write_hex("Pm4BufferCntlAtEnd", rc_read_register(RC_PM4_BUFFER_CNTL));
    rc_write_hex("Pm4MicroCntlAtEnd", rc_read_register(RC_PM4_MICRO_CNTL));
    rc_write_hex("GuiStatAtEnd", rc_read_register(V9X_R128_GUI_STAT));
    rc_write("Result", rc_failed == 0u ? "COMPLETE" : "FAIL");
    rc_close(rc_failed == 0u ? 0u : 1u);
}
