/*
 * V9XEGCP.EXE: AMD Evergreen (Radeon HD 5450 Cedar, HD 7470 Caicos) command
 * processor bring-up: AMD's microcode loaded, a ring started, and one packet
 * run.
 *
 * Phase 1 of docs\plans\ati-evergreen-cedar-3d.md. EGCP.VXD executes the op
 * lists built here and refuses anything outside its allow-lists. Writes
 * C:\V9XDIAG\V9XEGCP.INI.
 *
 * The microcode is read from PFP.BIN and ME.BIN in the current directory:
 * linux-firmware's radeon/CEDAR_pfp.bin (1120 dwords) and CEDAR_me.bin
 * (1376 dwords), or CAICOS_ or JUNIPER_pfp.bin and _me.bin, the same sizes,
 * big-endian as Linux's be32_to_cpup reads them.
 *
 * Call 1 is read-only: the CP's registers. The run goes ahead only if the
 * CP is halted (ME and PFP), as POST leaves it
 * (docs\decisions\2026-10-26-cedar-register-probe.md). /readonly stops
 * there.
 *
 * VRAM's GPU address is 40 bits: 0 on Cedar, F_00000000 on M-D7TFW4's
 * Caicos. CP_RB_BASE and SCRATCH_ADDR take the window's address shifted
 * right by 8, which holds all 40 bits, and CP_RB_RPTR_ADDR_HI its bits
 * 39:32 (evergreen_cp_resume). A start whose window would carry into bit
 * 32 is refused.
 *
 * Call 2 follows Linux:
 *   r700_cp_stop                 - ME and PFP halted, SCRATCH_UMSK 0;
 *   evergreen_cp_load_microcode  - CP_RB_CNTL as it sets it, PFP then ME
 *                                  streamed through their data ports;
 *   evergreen_cp_resume          - GRBM_SOFT_RESET of CP, PA, SH, VGT, SPI
 *                                  and SX, 15 ms, the ring (16 KiB, no
 *                                  read-pointer writeback, RB_NO_UPDATE as
 *                                  Linux sets it with writeback off), base,
 *                                  CP_DEBUG;
 *   evergreen_cp_start           - ME_INITIALIZE, then ME and PFP released;
 *   r600_ring_test               - SCRATCH_REG0 = CAFEDEAD by MMIO, then
 *                                  SET_CONFIG_REG of DEADBEEF through the
 *                                  ring, polled.
 * Clear state is not sent: this proves only that the CP runs packets.
 * Then the CP is halted again, every register written is put back as call
 * 1 read it, and the scratch window (BAR0 + 241 MiB) is restored.
 *
 * On Trinity (1002:9901, the HD 7660D, Linux's CHIP_ARUBA) the CP is
 * Cayman's, and call 2 follows ni.c instead. PFP.BIN and ME.BIN are
 * ARUBA_pfp.bin and ARUBA_me.bin, 2176 dwords each (radeon_ucode.h
 * CAYMAN_PFP/PM4_UCODE_SIZE):
 *   cayman_cp_load_microcode     - no CP_RB_CNTL write before the load;
 *   cayman_cp_resume             - the same GRBM_SOFT_RESET pulse, CP_DEBUG
 *                                  bit 27 alone, and all three rings: each
 *                                  one's size and read-pointer address,
 *                                  then all three bases (writing one resets
 *                                  every ring), then each read pointer
 *                                  reset through CP_RB0/1/2_RPTR with
 *                                  RB_RPTR_WR_ENA. RB1 and RB2 get idle
 *                                  16 KiB rings of their own in the window
 *                                  instead of the values the BIOS left;
 *   cayman_cp_start              - ME_INITIALIZE with 8 contexts
 *                                  (cayman_gpu_init's max_hw_contexts for
 *                                  9901), then CP_ME_CNTL 0
 *                                  (cayman_cp_enable).
 * The RLC microcode Linux loads on every IGP (ARUBA_rlc.bin) is not: this
 * run asks whether the CP runs packets without it. The id comes from a
 * first read-only call, before the microcode is read.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_EGCP_PATH        V9X_DIAG_PATH("V9XEGCP.INI")
#define V9X_EGCP_VXD         "\\\\.\\EGCP.VXD"
#define V9X_EGCP_MAGIC       0x50434745ul
#define V9X_EGCP_RUN         1u
#define V9X_EGCP_OP_MAX      512u
#define V9X_EGCP_BLOCK       4096u
#define V9X_EGCP_DATA        8192u
#define V9X_EGCP_HEADER      16u
#define V9X_EGCP_TIMEOUT     0xfffffffful

#define V9X_ST_COMPLETE  0x00000010ul

#define V9X_OP_MMIO_READ    1ul
#define V9X_OP_MMIO_WRITE   2ul
#define V9X_OP_VRAM_WRITE   3ul
#define V9X_OP_VRAM_FILL    5ul
#define V9X_OP_WAIT_MMIO    8ul
#define V9X_OP_SAVE         9ul
#define V9X_OP_RESTORE      10ul
#define V9X_OP_DELAY        12ul
#define V9X_OP_STREAM       13ul

/* Microcode sizes, radeon_ucode.h. */
#define V9X_EG_PFP_DWORDS   1120ul
#define V9X_EG_ME_DWORDS    1376ul
#define V9X_CM_PFP_DWORDS   2176ul
#define V9X_CM_ME_DWORDS    2176ul

/* Trinity, whose CP is Cayman's (nid.h). CP_RB0_RPTR is V9X_EG_CP_RB_RPTR;
 * Cayman writes it, and its siblings, to reset the read pointers. */
#define V9X_CM_DEVICE_TRINITY       0x9901ul
#define V9X_CM_CP_RB2_RPTR          0x86f8ul
#define V9X_CM_CP_RB1_RPTR          0x86fcul
#define V9X_CM_CP_RB1_BASE          0xc180ul
#define V9X_CM_CP_RB1_CNTL          0xc184ul
#define V9X_CM_CP_RB1_RPTR_ADDR     0xc188ul
#define V9X_CM_CP_RB1_RPTR_ADDR_HI  0xc18cul
#define V9X_CM_CP_RB1_WPTR          0xc190ul
#define V9X_CM_CP_RB2_BASE          0xc194ul
#define V9X_CM_CP_RB2_CNTL          0xc198ul
#define V9X_CM_CP_RB2_RPTR_ADDR     0xc19cul
#define V9X_CM_CP_RB2_RPTR_ADDR_HI  0xc1a0ul
#define V9X_CM_CP_RB2_WPTR          0xc1a4ul
/* cayman_cp_resume's CP_DEBUG, (1 << 27); cayman_cp_enable's run value. */
#define V9X_CM_CP_DEBUG_VALUE       0x08000000ul
#define V9X_CM_CP_RUN               0x00000000ul
/* cayman_gpu_init: max_hw_contexts 8 on 9901. */
#define V9X_CM_HW_CONTEXTS          8ul
/* RB1's and RB2's idle rings in the window, behind RB0's and the
 * write-back words. */
#define V9X_CM_RING1                0x8000ul
#define V9X_CM_RING2                0xc000ul

/* Registers, evergreend.h. */
#define V9X_EG_MC_VM_FB_LOCATION    0x2024ul
#define V9X_EG_HDP_MEM_FLUSH        0x5480ul
#define V9X_EG_GRBM_STATUS          0x8010ul
#define V9X_EG_GRBM_SOFT_RESET      0x8020ul
#define V9X_EG_SCRATCH_REG0         0x8500ul
#define V9X_EG_SCRATCH_UMSK         0x8540ul
#define V9X_EG_SCRATCH_ADDR         0x8544ul
#define V9X_EG_CP_SEM_WAIT_TIMER    0x85bcul
#define V9X_EG_CP_SEM_INCOMPLETE    0x85c8ul
#define V9X_EG_CP_STAT              0x8680ul
#define V9X_EG_CP_ME_CNTL           0x86d8ul
#define V9X_EG_CP_RB_RPTR           0x8700ul
#define V9X_EG_CP_RB_WPTR_DELAY     0x8704ul
#define V9X_EG_CP_RB_BASE           0xc100ul
#define V9X_EG_CP_RB_CNTL           0xc104ul
#define V9X_EG_CP_RB_RPTR_WR        0xc108ul
#define V9X_EG_CP_RB_RPTR_ADDR      0xc10cul
#define V9X_EG_CP_RB_RPTR_ADDR_HI   0xc110ul
#define V9X_EG_CP_RB_WPTR           0xc114ul
#define V9X_EG_CP_PFP_UCODE_ADDR    0xc150ul
#define V9X_EG_CP_PFP_UCODE_DATA    0xc154ul
#define V9X_EG_CP_ME_RAM_RADDR      0xc158ul
#define V9X_EG_CP_ME_RAM_WADDR      0xc15cul
#define V9X_EG_CP_ME_RAM_DATA       0xc160ul
#define V9X_EG_CP_DEBUG             0xc1fcul

/* Fields. CP_ME_CNTL bit 28 ME halt, bit 26 PFP halt; r700_cp_stop
 * writes both, evergreen_cp_start writes FFh. CP_RB_CNTL 5:0 log2 of the
 * ring's 8-byte units, 13:8 log2 of the read-pointer block, 27
 * RB_NO_UPDATE, 31 RB_RPTR_WR_ENA. GRBM_SOFT_RESET's CP 0, PA 5, SPI 8,
 * SH 9, SX 10, VGT 14. */
#define V9X_EG_CP_HALT              0x14000000ul
#define V9X_EG_CP_RUN               0x000000fful
#define V9X_EG_RB_NO_UPDATE         0x08000000ul
#define V9X_EG_RB_RPTR_WR_ENA       0x80000000ul
#define V9X_EG_GRBM_RESET_CP_BLOCKS 0x00004721ul
#define V9X_EG_CP_DEBUG_VALUE       0x18000000ul /* (1 << 27) | (1 << 28) */
#define V9X_EG_FB_START_MASK        0x0000fffful
/* FB_LOCATION's start field: low byte bits 31:24, high byte 39:32. */
#define V9X_EG_FB_LOW_MASK          0x000000fful
#define V9X_EG_FB_HIGH_SHIFT        8u
#define V9X_EG_FB_LOW_MAX           0x000000eful
#define V9X_ADDR_SHIFT_HIGH         24u

/* evergreen_cp_load_microcode's CP_RB_CNTL: RB_NO_UPDATE, RB_BLKSZ(15),
 * RB_BUFSZ(3). */
#define V9X_EG_RB_CNTL_LOADING      0x08000f03ul

/* The ring: 16 KiB, log2(16384 / 8) = 11; the read-pointer block log2
 * of a 4 KiB GPU page in 8-byte units, 9 (evergreen_cp_resume). */
#define V9X_RING_BYTES              0x4000ul
#define V9X_RING_DWORDS             (V9X_RING_BYTES / 4ul)
#define V9X_RB_CNTL                 ((9ul << 8) | 11ul)
#define V9X_RING_ALIGN_DWORDS       16ul
#define V9X_SCRATCH_WB              0x4000ul
#define V9X_RPTR_WB                 0x4100ul

/* PM4 type-3 packets, evergreend.h PACKET3. */
#define V9X_PACKET3(op, n) \
    ((3ul << 30) | (((DWORD)(op) & 0xfful) << 8) | \
     (((DWORD)(n) & 0x3ffful) << 16))
#define V9X_PACKET2                 0x80000000ul
#define V9X_P3_ME_INITIALIZE        0x44ul
#define V9X_P3_SET_CONFIG_REG       0x68ul
#define V9X_SET_CONFIG_REG_START    0x8000ul
/* max_hw_contexts, evergreen_gpu_init: 4 on Cedar and Caicos, 8 on
 * Juniper (1002:68B8, the id the VxD reads). */
#define V9X_EG_MAX_HW_CONTEXTS      4ul
#define V9X_EG_JUNIPER_HW_CONTEXTS  8ul
#define V9X_EG_DEVICE_JUNIPER       0x68b8ul

#define V9X_SCRATCH_SEED            0xcafedeadul
#define V9X_SCRATCH_VALUE           0xdeadbeeful
/* evergreen_cp_resume's mdelay(15) and mdelay(1), as register reads. */
#define V9X_RESET_DELAY_READS       20000ul
#define V9X_SHORT_DELAY_READS       2000ul

#define V9X_SCRATCH_GPU_OFFSET      0x0f100000ul
#define V9X_WINDOW_DWORDS           16384ul

struct v9x_egcp_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

struct v9x_egcp_request {
    DWORD count;
    struct v9x_egcp_op ops[V9X_EGCP_OP_MAX];
    DWORD data_count;
    DWORD data[V9X_EGCP_DATA];
};

struct v9x_egcp_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD bar0_high;
    DWORD bar2;
    DWORD bar2_high;
    DWORD memsize;
    DWORD executed;
    DWORD refused_index;
    DWORD block_used;
    DWORD config;
    DWORD reserved[4];
    DWORD op_results[V9X_EGCP_OP_MAX];
    DWORD block[V9X_EGCP_BLOCK];
};

/* The header must be the VxD's EC_HEADER_DWORDS long. */
typedef char v9x_egcp_header_matches[
    offsetof(struct v9x_egcp_result, op_results) ==
    sizeof(DWORD) * V9X_EGCP_HEADER ? 1 : -1];

/* The registers call 1 reads and the run puts back, in this order. The
 * first is CP_ME_CNTL, which goes back last. */
static const DWORD v9x_saved_offsets[] = {
    V9X_EG_CP_ME_CNTL, V9X_EG_CP_RB_CNTL, V9X_EG_CP_RB_BASE,
    V9X_EG_CP_RB_RPTR_ADDR, V9X_EG_CP_RB_RPTR_ADDR_HI, V9X_EG_CP_RB_WPTR,
    V9X_EG_CP_DEBUG, V9X_EG_SCRATCH_REG0, V9X_EG_SCRATCH_UMSK,
    V9X_EG_SCRATCH_ADDR, V9X_EG_CP_SEM_WAIT_TIMER, V9X_EG_CP_SEM_INCOMPLETE,
    V9X_EG_CP_RB_WPTR_DELAY
};
#define V9X_BASE_SAVED_COUNT (sizeof(v9x_saved_offsets) / sizeof(v9x_saved_offsets[0]))

static const char * const v9x_saved_names[V9X_BASE_SAVED_COUNT] = {
    "CP_ME_CNTL", "CP_RB_CNTL", "CP_RB_BASE", "CP_RB_RPTR_ADDR",
    "CP_RB_RPTR_ADDR_HI", "CP_RB_WPTR", "CP_DEBUG", "SCRATCH_REG0",
    "SCRATCH_UMSK", "SCRATCH_ADDR", "CP_SEM_WAIT_TIMER",
    "CP_SEM_INCOMPLETE_TIMER_CNTL", "CP_RB_WPTR_DELAY"
};

/* Trinity's other two rings, read and put back after the list above. */
static const DWORD v9x_trinity_saved_offsets[] = {
    V9X_CM_CP_RB1_BASE, V9X_CM_CP_RB1_CNTL, V9X_CM_CP_RB1_RPTR_ADDR,
    V9X_CM_CP_RB1_RPTR_ADDR_HI, V9X_CM_CP_RB1_WPTR,
    V9X_CM_CP_RB2_BASE, V9X_CM_CP_RB2_CNTL, V9X_CM_CP_RB2_RPTR_ADDR,
    V9X_CM_CP_RB2_RPTR_ADDR_HI, V9X_CM_CP_RB2_WPTR
};
#define V9X_TRINITY_SAVED_COUNT \
    (sizeof(v9x_trinity_saved_offsets) / sizeof(v9x_trinity_saved_offsets[0]))

static const char * const v9x_trinity_saved_names[V9X_TRINITY_SAVED_COUNT] = {
    "CP_RB1_BASE", "CP_RB1_CNTL", "CP_RB1_RPTR_ADDR", "CP_RB1_RPTR_ADDR_HI",
    "CP_RB1_WPTR", "CP_RB2_BASE", "CP_RB2_CNTL", "CP_RB2_RPTR_ADDR",
    "CP_RB2_RPTR_ADDR_HI", "CP_RB2_WPTR"
};

#define V9X_SAVED_MAX (V9X_BASE_SAVED_COUNT + V9X_TRINITY_SAVED_COUNT)

static struct v9x_egcp_request v9x_request;
static struct v9x_egcp_result v9x_result;
static DWORD v9x_saved[V9X_SAVED_MAX];
/* The run's saved list: the base list, then Trinity's rings on 9901. */
static DWORD v9x_saved_list[V9X_SAVED_MAX];
static const char *v9x_saved_list_names[V9X_SAVED_MAX];
static DWORD v9x_saved_count;
/* Nonzero on 1002:9901, whose CP is Cayman's. */
static int v9x_trinity;
static BYTE v9x_microcode_bytes[V9X_CM_ME_DWORDS * 4ul];
static DWORD v9x_ring[64];
static DWORD v9x_ring_count;
/* Bits 39:32 of the window's GPU address. */
static DWORD v9x_gpu_high;

static DWORD v9x_at_wait_init;
static DWORD v9x_at_wait_scratch;
static DWORD v9x_at_scratch;
static DWORD v9x_at_rptr;
static DWORD v9x_at_cp_stat;
static DWORD v9x_at_grbm;
static DWORD v9x_at_me_cntl;
static DWORD v9x_at_after_me_cntl;
static DWORD v9x_at_after_grbm;
static DWORD v9x_at_reset_read;

static void v9x_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_EGCP_PATH);
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    v9x_text(section, key, text);
}

static DWORD v9x_op(DWORD op, DWORD a, DWORD b, DWORD c)
{
    DWORD index;

    index = v9x_request.count++;
    v9x_request.ops[index].op = op;
    v9x_request.ops[index].a = a;
    v9x_request.ops[index].b = b;
    v9x_request.ops[index].c = c;
    return index;
}

static DWORD v9x_write(DWORD offset, DWORD value)
{
    return v9x_op(V9X_OP_MMIO_WRITE, offset, value, 0ul);
}

static DWORD v9x_read(DWORD offset)
{
    return v9x_op(V9X_OP_MMIO_READ, offset, 0ul, 0ul);
}

static int v9x_dioc(HANDLE device)
{
    DWORD returned;
    BOOL ok;

    returned = 0ul;
    ok = DeviceIoControl(device, V9X_EGCP_RUN, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_EGCP_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    v9x_key("Report", "Status", v9x_result.status);
    if ((v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
        v9x_key("Report", "RefusedIndex", v9x_result.refused_index);
        return 0;
    }
    return 1;
}

/* Reads count big-endian dwords from a file into the data area at
 * first. Non-zero when the file is exactly that long. */
static int v9x_load_microcode(const char *path, DWORD first, DWORD count)
{
    HANDLE file;
    BYTE *bytes = v9x_microcode_bytes;
    DWORD read;
    DWORD size;
    DWORD index;

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    size = GetFileSize(file, 0);
    read = 0ul;
    if (size != count * 4ul || size > sizeof(v9x_microcode_bytes) ||
        !ReadFile(file, bytes, size, &read, 0) || read != size) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    for (index = 0ul; index < count; ++index) {
        v9x_request.data[first + index] =
            ((DWORD)bytes[index * 4ul] << 24) |
            ((DWORD)bytes[index * 4ul + 1ul] << 16) |
            ((DWORD)bytes[index * 4ul + 2ul] << 8) |
            (DWORD)bytes[index * 4ul + 3ul];
    }
    return 1;
}

static void v9x_ring_put(DWORD value)
{
    v9x_ring[v9x_ring_count++] = value;
}

/* Pads with type-2 NOPs to the 16-dword boundary Linux's CP ring keeps
 * and returns the write pointer, in dwords. */
static DWORD v9x_ring_close(void)
{
    while ((v9x_ring_count % V9X_RING_ALIGN_DWORDS) != 0ul) {
        v9x_ring_put(V9X_PACKET2);
    }
    return v9x_ring_count;
}

/* Halt again, then put back what call 1 read: CP_ME_CNTL is first in the
 * list and written last, the rest before it. */
static void v9x_build_teardown(void)
{
    DWORD index;

    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
    for (index = 1ul; index < v9x_saved_count; ++index) {
        v9x_write(v9x_saved_list[index], v9x_saved[index]);
    }
    v9x_write(V9X_EG_CP_ME_CNTL, v9x_saved[0]);
    v9x_at_after_me_cntl = v9x_read(V9X_EG_CP_ME_CNTL);
    v9x_at_after_grbm = v9x_read(V9X_EG_GRBM_STATUS);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);
}

static void v9x_build_run(DWORD g)
{
    DWORD wptr_init;
    DWORD wptr_test;
    DWORD index;

    v9x_ring_count = 0ul;
    v9x_ring_put(V9X_PACKET3(V9X_P3_ME_INITIALIZE, 5ul));
    v9x_ring_put(0x1ul);
    v9x_ring_put(0x0ul);
    v9x_ring_put(((v9x_result.id >> 16) == V9X_EG_DEVICE_JUNIPER ?
                  V9X_EG_JUNIPER_HW_CONTEXTS : V9X_EG_MAX_HW_CONTEXTS) - 1ul);
    v9x_ring_put(1ul << 16); /* PACKET3_ME_INITIALIZE_DEVICE_ID(1) */
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    wptr_init = v9x_ring_close();
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONFIG_REG, 1ul));
    v9x_ring_put((V9X_EG_SCRATCH_REG0 - V9X_SET_CONFIG_REG_START) >> 2);
    v9x_ring_put(V9X_SCRATCH_VALUE);
    wptr_test = v9x_ring_close();

    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_WINDOW_DWORDS, 0ul);
    for (index = 0ul; index < v9x_ring_count; ++index) {
        v9x_op(V9X_OP_VRAM_WRITE, index * 4ul, v9x_ring[index], 0ul);
    }

    /* r700_cp_stop. */
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);

    /* evergreen_cp_load_microcode. */
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_EG_RB_CNTL_LOADING);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_PFP_UCODE_DATA, 0ul, V9X_EG_PFP_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_ME_RAM_DATA, V9X_EG_PFP_DWORDS,
           V9X_EG_ME_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_RADDR, 0ul);

    /* evergreen_cp_resume. */
    v9x_write(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    v9x_at_reset_read = v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_RB_CNTL);
    v9x_write(V9X_EG_CP_SEM_WAIT_TIMER, 0ul);
    v9x_write(V9X_EG_CP_SEM_INCOMPLETE, 0ul);
    v9x_write(V9X_EG_CP_RB_WPTR_DELAY, 0ul);
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_RB_CNTL | V9X_EG_RB_RPTR_WR_ENA);
    v9x_write(V9X_EG_CP_RB_RPTR_WR, 0ul);
    v9x_write(V9X_EG_CP_RB_WPTR, 0ul);
    v9x_write(V9X_EG_CP_RB_RPTR_ADDR, (g + V9X_RPTR_WB) & ~3ul);
    v9x_write(V9X_EG_CP_RB_RPTR_ADDR_HI, v9x_gpu_high);
    v9x_write(V9X_EG_SCRATCH_ADDR,
              (v9x_gpu_high << V9X_ADDR_SHIFT_HIGH) |
              ((g + V9X_SCRATCH_WB) >> 8));
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_CP_RB_CNTL, V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE);
    v9x_write(V9X_EG_CP_RB_BASE, (v9x_gpu_high << V9X_ADDR_SHIFT_HIGH) |
                                 (g >> 8));
    v9x_write(V9X_EG_CP_DEBUG, V9X_EG_CP_DEBUG_VALUE);

    /* evergreen_cp_start: ME_INITIALIZE, then release the ME and PFP. */
    v9x_write(V9X_EG_HDP_MEM_FLUSH, 1ul);
    v9x_write(V9X_EG_CP_RB_WPTR, wptr_init);
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_RUN);
    v9x_at_wait_init = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_CP_RB_RPTR,
                              0xfffffffful, wptr_init);

    /* r600_ring_test. */
    v9x_write(V9X_EG_SCRATCH_REG0, V9X_SCRATCH_SEED);
    v9x_write(V9X_EG_CP_RB_WPTR, wptr_test);
    v9x_at_wait_scratch = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_SCRATCH_REG0,
                                 0xfffffffful, V9X_SCRATCH_VALUE);
    v9x_at_scratch = v9x_read(V9X_EG_SCRATCH_REG0);
    v9x_at_rptr = v9x_read(V9X_EG_CP_RB_RPTR);
    v9x_at_cp_stat = v9x_read(V9X_EG_CP_STAT);
    v9x_at_grbm = v9x_read(V9X_EG_GRBM_STATUS);
    v9x_at_me_cntl = v9x_read(V9X_EG_CP_ME_CNTL);

    v9x_build_teardown();

    v9x_key("Run", "WptrInit", wptr_init);
    v9x_key("Run", "WptrTest", wptr_test);
}

/* Cayman's bring-up (ni.c), for Trinity: see the header. Both builders
 * lay out the same ring, ME_INITIALIZE then SET_CONFIG_REG, apart from the
 * context count. */
static void v9x_build_run_trinity(DWORD g)
{
    static const DWORD cntl[3] = {
        V9X_EG_CP_RB_CNTL, V9X_CM_CP_RB1_CNTL, V9X_CM_CP_RB2_CNTL
    };
    static const DWORD rptr_addr[3] = {
        V9X_EG_CP_RB_RPTR_ADDR, V9X_CM_CP_RB1_RPTR_ADDR,
        V9X_CM_CP_RB2_RPTR_ADDR
    };
    static const DWORD rptr_addr_hi[3] = {
        V9X_EG_CP_RB_RPTR_ADDR_HI, V9X_CM_CP_RB1_RPTR_ADDR_HI,
        V9X_CM_CP_RB2_RPTR_ADDR_HI
    };
    static const DWORD base[3] = {
        V9X_EG_CP_RB_BASE, V9X_CM_CP_RB1_BASE, V9X_CM_CP_RB2_BASE
    };
    static const DWORD rptr[3] = {
        V9X_EG_CP_RB_RPTR, V9X_CM_CP_RB1_RPTR, V9X_CM_CP_RB2_RPTR
    };
    static const DWORD wptr[3] = {
        V9X_EG_CP_RB_WPTR, V9X_CM_CP_RB1_WPTR, V9X_CM_CP_RB2_WPTR
    };
    static const DWORD ring_offset[3] = {
        0ul, V9X_CM_RING1, V9X_CM_RING2
    };
    DWORD wptr_init;
    DWORD wptr_test;
    DWORD index;
    DWORD ring;

    v9x_ring_count = 0ul;
    v9x_ring_put(V9X_PACKET3(V9X_P3_ME_INITIALIZE, 5ul));
    v9x_ring_put(0x1ul);
    v9x_ring_put(0x0ul);
    v9x_ring_put(V9X_CM_HW_CONTEXTS - 1ul);
    v9x_ring_put(1ul << 16); /* PACKET3_ME_INITIALIZE_DEVICE_ID(1) */
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    wptr_init = v9x_ring_close();
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONFIG_REG, 1ul));
    v9x_ring_put((V9X_EG_SCRATCH_REG0 - V9X_SET_CONFIG_REG_START) >> 2);
    v9x_ring_put(V9X_SCRATCH_VALUE);
    wptr_test = v9x_ring_close();

    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_WINDOW_DWORDS, 0ul);
    for (index = 0ul; index < v9x_ring_count; ++index) {
        v9x_op(V9X_OP_VRAM_WRITE, index * 4ul, v9x_ring[index], 0ul);
    }

    /* cayman_cp_enable(false). */
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);

    /* cayman_cp_load_microcode. */
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_PFP_UCODE_DATA, 0ul, V9X_CM_PFP_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_op(V9X_OP_STREAM, V9X_EG_CP_ME_RAM_DATA, V9X_CM_PFP_DWORDS,
           V9X_CM_ME_DWORDS);
    v9x_write(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_write(V9X_EG_CP_ME_RAM_RADDR, 0ul);

    /* cayman_cp_resume: the reset pulse, timers and CP_DEBUG. */
    v9x_write(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    v9x_at_reset_read = v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_write(V9X_EG_GRBM_SOFT_RESET, 0ul);
    v9x_read(V9X_EG_GRBM_SOFT_RESET);
    v9x_write(V9X_EG_CP_SEM_WAIT_TIMER, 0ul);
    v9x_write(V9X_EG_CP_SEM_INCOMPLETE, 0ul);
    v9x_write(V9X_EG_CP_RB_WPTR_DELAY, 0ul);
    v9x_write(V9X_EG_CP_DEBUG, V9X_CM_CP_DEBUG_VALUE);
    v9x_write(V9X_EG_SCRATCH_ADDR,
              (v9x_gpu_high << V9X_ADDR_SHIFT_HIGH) |
              ((g + V9X_SCRATCH_WB) >> 8));
    v9x_write(V9X_EG_SCRATCH_UMSK, 0ul);

    /* Each ring's size and read-pointer address. With writeback off, as
     * on Evergreen here, RB_NO_UPDATE keeps the CP from writing it. */
    for (ring = 0ul; ring < 3ul; ++ring) {
        v9x_write(cntl[ring], V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE);
        v9x_write(rptr_addr[ring], (g + V9X_RPTR_WB) & ~3ul);
        v9x_write(rptr_addr_hi[ring], v9x_gpu_high);
    }
    /* Every base: writing one resets all the rings. */
    for (ring = 0ul; ring < 3ul; ++ring) {
        v9x_write(base[ring], (v9x_gpu_high << V9X_ADDR_SHIFT_HIGH) |
                              ((g + ring_offset[ring]) >> 8));
    }
    /* Read and write pointers to 0 under RB_RPTR_WR_ENA. */
    for (ring = 0ul; ring < 3ul; ++ring) {
        v9x_write(cntl[ring], V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE |
                              V9X_EG_RB_RPTR_WR_ENA);
        v9x_write(rptr[ring], 0ul);
        v9x_write(wptr[ring], 0ul);
        v9x_op(V9X_OP_DELAY, V9X_SHORT_DELAY_READS, 0ul, 0ul);
        v9x_write(cntl[ring], V9X_RB_CNTL | V9X_EG_RB_NO_UPDATE);
    }

    /* cayman_cp_start: ME_INITIALIZE, then cayman_cp_enable(true). */
    v9x_write(V9X_EG_HDP_MEM_FLUSH, 1ul);
    v9x_write(V9X_EG_CP_RB_WPTR, wptr_init);
    v9x_write(V9X_EG_CP_ME_CNTL, V9X_CM_CP_RUN);
    v9x_at_wait_init = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_CP_RB_RPTR,
                              0xfffffffful, wptr_init);

    /* r600_ring_test, on ring 0. */
    v9x_write(V9X_EG_SCRATCH_REG0, V9X_SCRATCH_SEED);
    v9x_write(V9X_EG_CP_RB_WPTR, wptr_test);
    v9x_at_wait_scratch = v9x_op(V9X_OP_WAIT_MMIO, V9X_EG_SCRATCH_REG0,
                                 0xfffffffful, V9X_SCRATCH_VALUE);
    v9x_at_scratch = v9x_read(V9X_EG_SCRATCH_REG0);
    v9x_at_rptr = v9x_read(V9X_EG_CP_RB_RPTR);
    v9x_at_cp_stat = v9x_read(V9X_EG_CP_STAT);
    v9x_at_grbm = v9x_read(V9X_EG_GRBM_STATUS);
    v9x_at_me_cntl = v9x_read(V9X_EG_CP_ME_CNTL);

    v9x_build_teardown();

    v9x_key("Run", "WptrInit", wptr_init);
    v9x_key("Run", "WptrTest", wptr_test);
}

static int v9x_switch_given(const char *switch_text)
{
    const char *line;
    int length;

    line = GetCommandLineA();
    length = lstrlenA(switch_text);
    while (*line != '\0') {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           length, switch_text, length) == CSTR_EQUAL) {
            return 1;
        }
        ++line;
    }
    return 0;
}

static void v9x_finish(const char *result, UINT code)
{
    v9x_text("Report", "Result", result);
    WritePrivateProfileStringA(0, 0, 0, V9X_EGCP_PATH);
    ExitProcess(code);
}

void __stdcall V9xEvergreenCpEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD at_fb;
    DWORD at_grbm;
    DWORD at_cp_stat;
    DWORD at_rptr;
    DWORD fb;
    DWORD g;
    const DWORD *r;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_EGCP_PATH);
    v9x_text("Report", "Tool", "V9XEGCP");
    v9x_text("Report", "Build", V9X_BUILD_ID);

    device = CreateFileA(V9X_EGCP_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_key("Report", "LoadError", GetLastError());
        v9x_finish("VXD-LOAD-FAILED", 1u);
    }

    /* Call 0: one status read, for the chip's id, which sets the microcode
     * sizes and the registers saved. */
    v9x_request.count = 0ul;
    v9x_request.data_count = 0ul;
    v9x_read(V9X_EG_GRBM_STATUS);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    /* The VxD reports the card only on the call that maps it. */
    v9x_key("Report", "Id", v9x_result.id);
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar2", v9x_result.bar2);
    v9x_trinity = (v9x_result.id >> 16) == V9X_CM_DEVICE_TRINITY;
    v9x_text("Report", "Trinity", v9x_trinity ? "yes" : "no");
    v9x_saved_count = 0ul;
    for (index = 0ul; index < V9X_BASE_SAVED_COUNT; ++index) {
        v9x_saved_list[v9x_saved_count] = v9x_saved_offsets[index];
        v9x_saved_list_names[v9x_saved_count++] = v9x_saved_names[index];
    }
    if (v9x_trinity) {
        for (index = 0ul; index < V9X_TRINITY_SAVED_COUNT; ++index) {
            v9x_saved_list[v9x_saved_count] = v9x_trinity_saved_offsets[index];
            v9x_saved_list_names[v9x_saved_count++] =
                v9x_trinity_saved_names[index];
        }
    }

    if (v9x_trinity) {
        if (!v9x_load_microcode("PFP.BIN", 0ul, V9X_CM_PFP_DWORDS) ||
            !v9x_load_microcode("ME.BIN", V9X_CM_PFP_DWORDS,
                                V9X_CM_ME_DWORDS)) {
            CloseHandle(device);
            v9x_finish("MICROCODE-MISSING", 1u);
        }
        v9x_request.data_count = V9X_CM_PFP_DWORDS + V9X_CM_ME_DWORDS;
    } else {
        if (!v9x_load_microcode("PFP.BIN", 0ul, V9X_EG_PFP_DWORDS) ||
            !v9x_load_microcode("ME.BIN", V9X_EG_PFP_DWORDS,
                                V9X_EG_ME_DWORDS)) {
            CloseHandle(device);
            v9x_finish("MICROCODE-MISSING", 1u);
        }
        v9x_request.data_count = V9X_EG_PFP_DWORDS + V9X_EG_ME_DWORDS;
    }
    v9x_key("Report", "PfpFirst", v9x_request.data[0]);
    v9x_key("Report", "MeFirst", v9x_request.data[v9x_trinity ?
                                                  V9X_CM_PFP_DWORDS :
                                                  V9X_EG_PFP_DWORDS]);

    /* Call 1: read-only. */
    v9x_request.count = 0ul;
    for (index = 0ul; index < v9x_saved_count; ++index) {
        v9x_read(v9x_saved_list[index]);
    }
    at_grbm = v9x_read(V9X_EG_GRBM_STATUS);
    at_cp_stat = v9x_read(V9X_EG_CP_STAT);
    at_rptr = v9x_read(V9X_EG_CP_RB_RPTR);
    at_fb = v9x_read(V9X_EG_MC_VM_FB_LOCATION);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    r = v9x_result.op_results;
    for (index = 0ul; index < v9x_saved_count; ++index) {
        v9x_saved[index] = r[index];
        v9x_key("Before", v9x_saved_list_names[index], r[index]);
    }
    v9x_key("Before", "GRBM_STATUS", r[at_grbm]);
    v9x_key("Before", "CP_STAT", r[at_cp_stat]);
    v9x_key("Before", "CP_RB_RPTR", r[at_rptr]);
    v9x_key("Before", "MC_VM_FB_LOCATION", r[at_fb]);

    /* The CP must be halted, as POST leaves it: nothing else is using it. */
    if ((v9x_saved[0] & V9X_EG_CP_HALT) != V9X_EG_CP_HALT) {
        CloseHandle(device);
        v9x_finish("CP-NOT-HALTED", 3u);
    }
    fb = r[at_fb] & V9X_EG_FB_START_MASK;
    if ((fb & V9X_EG_FB_LOW_MASK) > V9X_EG_FB_LOW_MAX) {
        CloseHandle(device);
        v9x_finish("FB-START-UNSUPPORTED", 4u);
    }
    v9x_gpu_high = fb >> V9X_EG_FB_HIGH_SHIFT;
    g = ((fb & V9X_EG_FB_LOW_MASK) << 24) + V9X_SCRATCH_GPU_OFFSET;
    v9x_key("Run", "WindowGpuHigh", v9x_gpu_high);
    v9x_key("Run", "WindowGpu", g);
    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }

    /* Call 2: the run. */
    if (v9x_trinity) {
        v9x_build_run_trinity(g);
    } else {
        v9x_build_run(g);
    }
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_key("Report", "Executed", v9x_result.executed);
        v9x_finish("RUN-REFUSED", 5u);
    }
    CloseHandle(device);
    r = v9x_result.op_results;
    v9x_key("During", "GRBM_SOFT_RESET_Pulse", r[v9x_at_reset_read]);
    v9x_key("During", "WaitInit", r[v9x_at_wait_init]);
    v9x_key("During", "WaitScratch", r[v9x_at_wait_scratch]);
    v9x_key("During", "SCRATCH_REG0", r[v9x_at_scratch]);
    v9x_key("During", "CP_RB_RPTR", r[v9x_at_rptr]);
    v9x_key("During", "CP_STAT", r[v9x_at_cp_stat]);
    v9x_key("During", "GRBM_STATUS", r[v9x_at_grbm]);
    v9x_key("During", "CP_ME_CNTL", r[v9x_at_me_cntl]);
    v9x_key("After", "CP_ME_CNTL", r[v9x_at_after_me_cntl]);
    v9x_key("After", "GRBM_STATUS", r[v9x_at_after_grbm]);
    v9x_text("Result", "MeInitialize",
             r[v9x_at_wait_init] != V9X_EGCP_TIMEOUT ? "PASS" : "FAIL");
    v9x_text("Result", "RingTest",
             r[v9x_at_scratch] == V9X_SCRATCH_VALUE ? "PASS" : "FAIL");
    v9x_finish("COMPLETE", 0u);
}
