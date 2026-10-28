/*
 * V9XCZDMA.EXE: the first writes to an AMD Carrizo (Radeon R7E, GCN 3):
 * SDMA0's microcode loaded through its own port, one ring, two
 * submissions, checked byte for byte.
 *
 * Phase 3 of docs\plans\ati-carrizo-gfx8.md. CZDMA.VXD executes the op
 * lists built here and refuses anything outside its allow-lists. Writes
 * C:\V9XDIAG\V9XCZDMA.INI. The microcode is SDMA.BIN in the working
 * directory, linux-firmware's amdgpu/carrizo_sdma.bin copied there by
 * hand; it is not in this tree.
 *
 * Call 1 is read-only: SDMA0's registers, SRBM_SOFT_RESET and STATUS2,
 * MC_VM_FB_LOCATION, RLC_GPM_STAT and GRBM_STATUS. The run goes ahead only
 * if SDMA0 is halted with its ring off and out of reset, as the register
 * probe read it (docs\decisions\2026-10-28-carrizo-register-probe.md).
 * /readonly stops there.
 *
 * Call 2 checks the CPU can reach the scratch window (BAR0 + 240 MiB,
 * 64 KiB): it saves it, writes a pattern, reads it back and restores it.
 *
 * Call 3 saves the window again, lays out buffers and a ring in it, and
 * does what Linux v4.9 sdma_v3_0.c does with smc_load_fw=0, for SDMA0
 * alone: halt the engine and stop its ring (sdma_v3_0_enable), the
 * Carrizo golden settings (cz_mgcg_cgcg_init, cz_golden_settings_a11),
 * the microcode through SDMA0_UCODE_ADDR/_DATA (load_microcode), the ring
 * registers in gfx_resume's order with the read-pointer writeback in the
 * window, and the unhalt. Then it submits:
 *
 *   1. Linux's ring test (a one-dword WRITE_LINEAR), a dword copy of 256
 *      dwords, a byte copy of 37 bytes between odd offsets, a
 *      forward-overlapping copy of 64 dwords 4 dwords up, a constant fill
 *      of 1024 bytes, and a fence;
 *   2. a constant fill of 10 bytes at byte offset 2, and a second fence.
 *
 * The 1024-byte fill asks for dword elements and the ragged one for
 * 16-bit elements (CONSTANT_FILL's fill size, see V9X_SDMA_FILLSIZE_SHIFT).
 * The ragged fill goes alone in the second submission: if it wedges the
 * engine the rest is already in. Then the engine is halted, every register written is
 * put back as call 1 read it, HDP is flushed and invalidated, the buffers
 * are read back and the window restored.
 *
 * Packets: tonga_sdma_pkt_open.h (build\external\carrizo\linux\), header
 * op 7:0 and sub-op 15:8; sdma_v3_0.c's builders: WRITE_LINEAR header,
 * address low, high, count in dwords, data; COPY_LINEAR header, count in
 * bytes, 0, source low, high, destination low, high; CONSTANT_FILL
 * header, destination low, high, value, count in bytes; FENCE header,
 * address low, high, value; NOP 0.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_CZDMA_PATH       V9X_DIAG_PATH("V9XCZDMA.INI")
#define V9X_CZDMA_VXD        "\\\\.\\CZDMA.VXD"
#define V9X_CZDMA_UCODE      "SDMA.BIN"
#define V9X_CZDMA_MAGIC      0x414d4443ul
#define V9X_CZDMA_RUN        1u
#define V9X_CZDMA_OP_MAX     512u
#define V9X_CZDMA_DATA_MAX   4096u
#define V9X_CZDMA_BLOCK      4096u
#define V9X_CZDMA_HEADER     16u
#define V9X_CZDMA_TIMEOUT    0xfffffffful

#define V9X_ST_FOUND     0x00000001ul
#define V9X_ST_MAPPED    0x00000002ul
#define V9X_ST_IDENTITY  0x00000004ul
#define V9X_ST_REFUSED   0x00000008ul
#define V9X_ST_COMPLETE  0x00000010ul
#define V9X_ST_TIMEOUT   0x00000040ul

#define V9X_OP_MMIO_READ    1ul
#define V9X_OP_MMIO_WRITE   2ul
#define V9X_OP_VRAM_WRITE   3ul
#define V9X_OP_VRAM_FILL    5ul
#define V9X_OP_VRAM_BLOCK   6ul
#define V9X_OP_WAIT_VRAM    7ul
#define V9X_OP_SAVE         9ul
#define V9X_OP_RESTORE      10ul
#define V9X_OP_VRAM_PATTERN 11ul
#define V9X_OP_DELAY        12ul
#define V9X_OP_MMIO_RMW     13ul
#define V9X_OP_UCODE        14ul

/* CZDMA.VXD's CD_PATTERN_STEP. */
#define V9X_PATTERN_STEP    0x9e3779b9ul

/* Registers: oss_3_0_d.h, bif_5_0_d.h, gmc_8_1_d.h, gfx_8_0_d.h. */
#define V9X_CZ_SRBM_STATUS2         0x0e4cul
#define V9X_CZ_SRBM_STATUS          0x0e50ul
#define V9X_CZ_SRBM_SOFT_RESET      0x0e60ul
#define V9X_CZ_MC_VM_FB_LOCATION    0x2024ul
#define V9X_CZ_HDP_DEBUG0           0x2f30ul
#define V9X_CZ_HDP_MEM_FLUSH        0x5480ul
#define V9X_CZ_GRBM_STATUS          0x8010ul
#define V9X_CZ_RLC_GPM_STAT         0x3b100ul
#define V9X_CZ_SDMA_UCODE_ADDR      0xd000ul
#define V9X_CZ_SDMA_UCODE_DATA      0xd004ul
#define V9X_CZ_SDMA_POWER_CNTL      0xd008ul
#define V9X_CZ_SDMA_CLK_CTRL        0xd00cul
#define V9X_CZ_SDMA_CNTL            0xd010ul
#define V9X_CZ_SDMA_CHICKEN_BITS    0xd014ul
#define V9X_CZ_SDMA_TILING_CONFIG   0xd018ul
#define V9X_CZ_SDMA_SEM_WAIT_FAIL   0xd024ul
#define V9X_CZ_SDMA_STATUS_REG      0xd034ul
#define V9X_CZ_SDMA_STATUS1_REG     0xd038ul
#define V9X_CZ_SDMA_F32_CNTL        0xd048ul
#define V9X_CZ_SDMA_VERSION         0xd074ul
#define V9X_CZ_SDMA_RB_CNTL         0xd200ul
#define V9X_CZ_SDMA_RB_BASE         0xd204ul
#define V9X_CZ_SDMA_RB_BASE_HI      0xd208ul
#define V9X_CZ_SDMA_RB_RPTR         0xd20cul
#define V9X_CZ_SDMA_RB_WPTR         0xd210ul
#define V9X_CZ_SDMA_RPTR_ADDR_HI    0xd220ul
#define V9X_CZ_SDMA_RPTR_ADDR_LO    0xd224ul
#define V9X_CZ_SDMA_IB_CNTL         0xd228ul
#define V9X_CZ_SDMA_IB_RPTR         0xd22cul
#define V9X_CZ_SDMA_IB_OFFSET       0xd230ul
#define V9X_CZ_SDMA_CONTEXT_STATUS  0xd244ul
#define V9X_CZ_SDMA_DOORBELL        0xd248ul
#define V9X_CZ_SDMA_VIRTUAL_ADDR    0xd29cul
#define V9X_CZ_SDMA_APE1_CNTL       0xd2a0ul
#define V9X_CZ_SDMA_RLC0_IB_CNTL    0xd428ul
#define V9X_CZ_SDMA_RLC1_IB_CNTL    0xd628ul

/* Fields: oss_3_0_sh_mask.h. */
#define V9X_CZ_SOFT_RESET_SDMA0     0x00100000ul
#define V9X_CZ_F32_HALT             0x00000001ul
#define V9X_CZ_RB_ENABLE            0x00000001ul
#define V9X_CZ_RB_SIZE_MASK         0x0000003eul
#define V9X_CZ_RB_SIZE_SHIFT        1u
#define V9X_CZ_RPTR_WRITEBACK       0x00001000ul
#define V9X_CZ_IB_ENABLE            0x00000001ul
#define V9X_CZ_AUTO_CTXSW           0x00040000ul
#define V9X_CZ_DOORBELL_ENABLE      0x10000000ul
#define V9X_CZ_FB_START_MASK        0x0000fffful

/* The Carrizo golden settings for SDMA0, v4.9 sdma_v3_0.c, applied as
 * amdgpu_program_register_sequence does (clear the first mask, set the
 * second). cz_mgcg_cgcg_init's CLK_CTRL entry is undone by the golden
 * entry after it, so the two collapse into the golden one. */
#define V9X_CZ_CHICKEN_CLEAR        0xfc910007ul
#define V9X_CZ_CHICKEN_SET          0x00810007ul
#define V9X_CZ_CLK_CLEAR            0xff000ffful
#define V9X_CZ_IB_CNTL_GOLDEN       0x00000100ul
#define V9X_CZ_POWER_CLEAR          0x00000800ul
#define V9X_CZ_POWER_SET            0x0003c800ul

/* SDMA packets, tonga_sdma_pkt_open.h. */
#define V9X_SDMA_HEADER(op, sub) \
    (((DWORD)(op) & 0xfful) | (((DWORD)(sub) & 0xfful) << 8))
#define V9X_SDMA_OP_NOP         0ul
#define V9X_SDMA_OP_COPY        1ul
#define V9X_SDMA_OP_WRITE       2ul
#define V9X_SDMA_OP_FENCE       5ul
#define V9X_SDMA_OP_CONST_FILL  11ul
#define V9X_SDMA_SUB_LINEAR     0ul
/* CONSTANT_FILL header bits 31:30. Run 1 on M-XP7053 sent 0, as Linux's
 * sdma_v3_0_emit_fill_buffer does, and the engine filled with the value's
 * low byte: 0 is one byte. The two others are asked here. */
#define V9X_SDMA_FILLSIZE_SHIFT 30u
#define V9X_FILL_SIZE_DWORD     2ul
#define V9X_FILL_SIZE_WORD      1ul

/* The firmware image: common_firmware_header in amdgpu_ucode.h. */
#define V9X_FW_SIZE_BYTES       0x00ul
#define V9X_FW_VERSION          0x08ul
#define V9X_FW_UCODE_VERSION    0x10ul
#define V9X_FW_UCODE_BYTES      0x14ul
#define V9X_FW_ARRAY_OFFSET     0x18ul
#define V9X_FW_HEADER_BYTES     0x20ul
#define V9X_FW_FILE_MAX         0x4000ul

/* The scratch window: its GPU address is the FB start plus this, and the
 * layout inside it. Submissions are padded to 16 dwords with NOPs, as
 * Evergreen's were; the SDMA ring needs only 8-dword alignment on VI. */
#define V9X_SCRATCH_GPU_OFFSET  0x0f000000ul
#define V9X_SCRATCH_DWORDS      16384ul
#define V9X_RING_LOG2_DWORDS    10ul
#define V9X_RING_ALIGN_DWORDS   16ul
#define V9X_RING                0x0000ul
#define V9X_FENCE_1             0x1000ul
#define V9X_FENCE_2             0x1004ul
#define V9X_WRITEBACK           0x1100ul
#define V9X_RING_TEST           0x1200ul
#define V9X_COPY_SRC            0x2000ul
#define V9X_COPY_DST            0x3000ul
#define V9X_BYTE_SRC            0x4000ul
#define V9X_BYTE_DST            0x4400ul
#define V9X_OVERLAP             0x5000ul
#define V9X_FILL_DST            0x6000ul
#define V9X_RAGGED_DST          0x7000ul
#define V9X_CANARY              0x8000ul

#define V9X_GUARD_VALUE         0x5a5a5a5aul
#define V9X_GUARD_DWORDS        4ul
#define V9X_RUN_DWORDS          256ul
#define V9X_TARGET_DWORDS       (V9X_RUN_DWORDS + 4ul * V9X_GUARD_DWORDS)
#define V9X_BYTE_COUNT          37ul
#define V9X_BYTE_SRC_SKEW       1ul
#define V9X_BYTE_DST_SKEW       3ul
#define V9X_BYTE_BUF_DWORDS     32ul
#define V9X_OVERLAP_DWORDS      128ul
#define V9X_OVERLAP_RUN         64ul
#define V9X_OVERLAP_SHIFT       4ul
#define V9X_FILL_VALUE          0x11223344ul
#define V9X_RAGGED_VALUE        0xa1b2c3d4ul
#define V9X_RAGGED_SKEW         2ul
#define V9X_RAGGED_BYTES        10ul
#define V9X_RAGGED_DWORDS       8ul
#define V9X_RING_TEST_BEFORE    0xcafedeadul
#define V9X_RING_TEST_VALUE     0xdeadbeeful
#define V9X_FENCE_1_VALUE       0x11111111ul
#define V9X_FENCE_2_VALUE       0x22222222ul
#define V9X_FENCE_WAIT_LIMIT    20000000ul
#define V9X_CANARY_DWORDS       16ul

#define V9X_SEED_CANARY         0x40000004ul
#define V9X_SEED_COPY           0x10000001ul
#define V9X_SEED_BYTE           0x20000002ul
#define V9X_SEED_OVERLAP        0x30000003ul

struct v9x_czdma_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

/* The ops, then the data array: CZDMA.VXD reads the data at
 * ops[count], so only count ops go to it. */
struct v9x_czdma_request {
    DWORD count;
    DWORD data_count;
    struct v9x_czdma_op ops[V9X_CZDMA_OP_MAX];
};

struct v9x_czdma_result {
    DWORD magic;
    DWORD status;
    DWORD id;
    DWORD bar0;
    DWORD bar0_high;
    DWORD bar5;
    DWORD reserved0;
    DWORD memsize;
    DWORD executed;
    DWORD refused_index;
    DWORD block_used;
    DWORD config;
    DWORD ucode_written;
    DWORD reserved[3];
    DWORD op_results[V9X_CZDMA_OP_MAX];
    DWORD block[V9X_CZDMA_BLOCK];
};

/* The header must be the VxD's CD_HEADER_DWORDS long. */
typedef char v9x_czdma_header_matches[
    offsetof(struct v9x_czdma_result, op_results) ==
    sizeof(DWORD) * V9X_CZDMA_HEADER ? 1 : -1];

/* The registers call 1 reads and the run puts back, in this order. The
 * engine is left halted, as it was found, so F32_CNTL goes back last. */
static const DWORD v9x_saved_offsets[] = {
    V9X_CZ_SDMA_RB_CNTL, V9X_CZ_SDMA_RB_BASE, V9X_CZ_SDMA_RB_BASE_HI,
    V9X_CZ_SDMA_RB_RPTR, V9X_CZ_SDMA_RB_WPTR, V9X_CZ_SDMA_RPTR_ADDR_HI,
    V9X_CZ_SDMA_RPTR_ADDR_LO, V9X_CZ_SDMA_IB_CNTL, V9X_CZ_SDMA_IB_RPTR,
    V9X_CZ_SDMA_IB_OFFSET, V9X_CZ_SDMA_DOORBELL, V9X_CZ_SDMA_TILING_CONFIG,
    V9X_CZ_SDMA_SEM_WAIT_FAIL, V9X_CZ_SDMA_CHICKEN_BITS,
    V9X_CZ_SDMA_CLK_CTRL, V9X_CZ_SDMA_POWER_CNTL, V9X_CZ_SDMA_RLC0_IB_CNTL,
    V9X_CZ_SDMA_RLC1_IB_CNTL, V9X_CZ_SDMA_CNTL, V9X_CZ_SDMA_F32_CNTL
};
#define V9X_SAVED_COUNT (sizeof(v9x_saved_offsets) / sizeof(v9x_saved_offsets[0]))
#define V9X_SAVED_RB_CNTL   0u
#define V9X_SAVED_F32_CNTL  (V9X_SAVED_COUNT - 1u)

static const char * const v9x_saved_names[V9X_SAVED_COUNT] = {
    "SDMA0_GFX_RB_CNTL", "SDMA0_GFX_RB_BASE", "SDMA0_GFX_RB_BASE_HI",
    "SDMA0_GFX_RB_RPTR", "SDMA0_GFX_RB_WPTR", "SDMA0_GFX_RB_RPTR_ADDR_HI",
    "SDMA0_GFX_RB_RPTR_ADDR_LO", "SDMA0_GFX_IB_CNTL", "SDMA0_GFX_IB_RPTR",
    "SDMA0_GFX_IB_OFFSET", "SDMA0_GFX_DOORBELL", "SDMA0_TILING_CONFIG",
    "SDMA0_SEM_WAIT_FAIL_TIMER_CNTL", "SDMA0_CHICKEN_BITS",
    "SDMA0_CLK_CTRL", "SDMA0_POWER_CNTL", "SDMA0_RLC0_IB_CNTL",
    "SDMA0_RLC1_IB_CNTL", "SDMA0_CNTL", "SDMA0_F32_CNTL"
};

/* Read-only extras for the report, before and after. */
static const DWORD v9x_extra_offsets[] = {
    V9X_CZ_SDMA_STATUS_REG, V9X_CZ_SDMA_STATUS1_REG, V9X_CZ_SDMA_VERSION,
    V9X_CZ_SDMA_CONTEXT_STATUS, V9X_CZ_SDMA_VIRTUAL_ADDR,
    V9X_CZ_SDMA_APE1_CNTL, V9X_CZ_SRBM_SOFT_RESET, V9X_CZ_SRBM_STATUS,
    V9X_CZ_SRBM_STATUS2, V9X_CZ_MC_VM_FB_LOCATION, V9X_CZ_RLC_GPM_STAT,
    V9X_CZ_GRBM_STATUS
};
#define V9X_EXTRA_COUNT (sizeof(v9x_extra_offsets) / sizeof(v9x_extra_offsets[0]))
#define V9X_EXTRA_RESET  6u
#define V9X_EXTRA_FB     9u

static const char * const v9x_extra_names[V9X_EXTRA_COUNT] = {
    "SDMA0_STATUS_REG", "SDMA0_STATUS1_REG", "SDMA0_VERSION",
    "SDMA0_GFX_CONTEXT_STATUS", "SDMA0_GFX_VIRTUAL_ADDR",
    "SDMA0_GFX_APE1_CNTL", "SRBM_SOFT_RESET", "SRBM_STATUS",
    "SRBM_STATUS2", "MC_VM_FB_LOCATION", "RLC_GPM_STAT", "GRBM_STATUS"
};

/* The request, then room for the data array right after count ops. */
static struct {
    struct v9x_czdma_request request;
    DWORD spill[V9X_CZDMA_DATA_MAX];
} v9x_buffer;
#define v9x_request (v9x_buffer.request)

static struct v9x_czdma_result v9x_result;
static DWORD v9x_saved[V9X_SAVED_COUNT];
static DWORD v9x_ring[96];
static DWORD v9x_ring_count;
static BYTE v9x_file[V9X_FW_FILE_MAX];
static DWORD v9x_ucode[V9X_CZDMA_DATA_MAX];
static DWORD v9x_ucode_dwords;
static DWORD v9x_ucode_version;

/* Indices into v9x_result.op_results of the values the report needs. */
static DWORD v9x_at_halted_f32;
static DWORD v9x_at_loaded_version;
static DWORD v9x_at_enabled_cntl;
static DWORD v9x_at_enabled_f32;
static DWORD v9x_at_enabled_status;
static DWORD v9x_at_wait_1;
static DWORD v9x_at_rptr_1;
static DWORD v9x_at_status_1;
static DWORD v9x_at_wait_2;
static DWORD v9x_at_rptr_2;
static DWORD v9x_at_status_2;
static DWORD v9x_at_after_extra;

static void v9x_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_decimal(char *text, DWORD value)
{
    char digits[11];
    int count;
    int at;

    count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    at = 0;
    while (count != 0) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_CZDMA_PATH);
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    v9x_text(section, key, text);
}

static void v9x_number(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_decimal(text, value);
    v9x_text(section, key, text);
}

static void v9x_pass(const char *key, int pass)
{
    v9x_text("Result", key, pass ? "PASS" : "FAIL");
}

static DWORD v9x_le32(const BYTE *at)
{
    return (DWORD)at[0] | ((DWORD)at[1] << 8) | ((DWORD)at[2] << 16) |
           ((DWORD)at[3] << 24);
}

/* Appends an op; returns its index. */
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

static void v9x_begin(void)
{
    v9x_request.count = 0ul;
    v9x_request.data_count = 0ul;
}

/* Sends count ops and, after them, the microcode if with_ucode. */
static int v9x_dioc(HANDLE device, int with_ucode)
{
    DWORD returned;
    DWORD bytes;
    DWORD *data;
    DWORD index;
    BOOL ok;

    bytes = 8ul + v9x_request.count * sizeof(struct v9x_czdma_op);
    if (with_ucode) {
        data = (DWORD *)&v9x_request.ops[v9x_request.count];
        for (index = 0ul; index < v9x_ucode_dwords; ++index) {
            data[index] = v9x_ucode[index];
        }
        v9x_request.data_count = v9x_ucode_dwords;
        bytes += v9x_ucode_dwords * 4ul;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_CZDMA_RUN, &v9x_buffer, bytes,
                         &v9x_result, sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_CZDMA_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    v9x_key("Report", "Status", v9x_result.status);
    if ((v9x_result.status & V9X_ST_COMPLETE) == 0ul) {
        v9x_text("Report", "Result", "REFUSED");
        v9x_number("Report", "RefusedIndex", v9x_result.refused_index);
        return 0;
    }
    return 1;
}

/* SDMA.BIN from the working directory: a common_firmware_header whose
 * sizes agree with the file, and a payload that fits the data array.
 * Returns 0 with a reason in the report otherwise. */
static int v9x_load_ucode(void)
{
    HANDLE file;
    DWORD length;
    DWORD read;
    DWORD size;
    DWORD offset;
    DWORD bytes;
    DWORD index;

    file = CreateFileA(V9X_CZDMA_UCODE, GENERIC_READ, FILE_SHARE_READ, 0,
                       OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE) {
        v9x_text("Ucode", "Error", "not-found");
        return 0;
    }
    length = GetFileSize(file, 0);
    read = 0ul;
    if (length < V9X_FW_HEADER_BYTES || length > V9X_FW_FILE_MAX ||
        !ReadFile(file, v9x_file, length, &read, 0) || read != length) {
        CloseHandle(file);
        v9x_text("Ucode", "Error", "unreadable");
        return 0;
    }
    CloseHandle(file);

    size = v9x_le32(v9x_file + V9X_FW_SIZE_BYTES);
    offset = v9x_le32(v9x_file + V9X_FW_ARRAY_OFFSET);
    bytes = v9x_le32(v9x_file + V9X_FW_UCODE_BYTES);
    v9x_ucode_version = v9x_le32(v9x_file + V9X_FW_UCODE_VERSION);
    v9x_number("Ucode", "FileBytes", length);
    v9x_key("Ucode", "HeaderVersion", v9x_le32(v9x_file + V9X_FW_VERSION));
    v9x_key("Ucode", "UcodeVersion", v9x_ucode_version);
    v9x_number("Ucode", "UcodeBytes", bytes);
    v9x_key("Ucode", "ArrayOffset", offset);
    if (size != length || (v9x_le32(v9x_file + V9X_FW_VERSION) & 0xfffful) != 1ul ||
        (bytes % 4ul) != 0ul || bytes == 0ul ||
        bytes / 4ul > V9X_CZDMA_DATA_MAX || offset > length ||
        bytes > length - offset) {
        v9x_text("Ucode", "Error", "bad-header");
        return 0;
    }
    v9x_ucode_dwords = bytes / 4ul;
    for (index = 0ul; index < v9x_ucode_dwords; ++index) {
        v9x_ucode[index] = v9x_le32(v9x_file + offset + index * 4ul);
    }
    return 1;
}

/* Ring building: packets, then NOPs to the 16-dword boundary. Returns the
 * write pointer in bytes. */
static void v9x_ring_put(DWORD value)
{
    v9x_ring[v9x_ring_count++] = value;
}

static DWORD v9x_ring_close(void)
{
    while ((v9x_ring_count % V9X_RING_ALIGN_DWORDS) != 0ul) {
        v9x_ring_put(V9X_SDMA_HEADER(V9X_SDMA_OP_NOP, 0ul));
    }
    return v9x_ring_count * 4ul;
}

static void v9x_ring_write(DWORD lo, DWORD hi, DWORD value)
{
    v9x_ring_put(V9X_SDMA_HEADER(V9X_SDMA_OP_WRITE, V9X_SDMA_SUB_LINEAR));
    v9x_ring_put(lo);
    v9x_ring_put(hi);
    v9x_ring_put(1ul);
    v9x_ring_put(value);
}

static void v9x_ring_copy(DWORD bytes, DWORD dst, DWORD src, DWORD hi)
{
    v9x_ring_put(V9X_SDMA_HEADER(V9X_SDMA_OP_COPY, V9X_SDMA_SUB_LINEAR));
    v9x_ring_put(bytes);
    v9x_ring_put(0ul);
    v9x_ring_put(src);
    v9x_ring_put(hi);
    v9x_ring_put(dst);
    v9x_ring_put(hi);
}

static void v9x_ring_fill(DWORD size, DWORD bytes, DWORD dst, DWORD hi,
                          DWORD value)
{
    v9x_ring_put(V9X_SDMA_HEADER(V9X_SDMA_OP_CONST_FILL, 0ul) |
                 (size << V9X_SDMA_FILLSIZE_SHIFT));
    v9x_ring_put(dst);
    v9x_ring_put(hi);
    v9x_ring_put(value);
    v9x_ring_put(bytes);
}

static void v9x_ring_fence(DWORD lo, DWORD hi, DWORD value)
{
    v9x_ring_put(V9X_SDMA_HEADER(V9X_SDMA_OP_FENCE, 0ul));
    v9x_ring_put(lo);
    v9x_ring_put(hi);
    v9x_ring_put(value);
}

static DWORD v9x_pattern(DWORD seed, DWORD index)
{
    return seed + index * V9X_PATTERN_STEP;
}

static BYTE v9x_pattern_byte(DWORD seed, DWORD byte_index)
{
    return (BYTE)(v9x_pattern(seed, byte_index / 4ul) >>
                  ((byte_index % 4ul) * 8ul));
}

/* Halt SDMA0 and stop its ring and IBs, sdma_v3_0_enable(false) and
 * gfx_stop for one engine. */
static DWORD v9x_halt(void)
{
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_RB_CNTL, V9X_CZ_RB_ENABLE, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_IB_CNTL, V9X_CZ_IB_ENABLE, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_F32_CNTL, 0ul, V9X_CZ_F32_HALT);
    return v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_F32_CNTL, 0ul, 0ul);
}

/* Call 3's op list. lo/hi are the scratch window's GPU address. */
static void v9x_build_run(DWORD lo, DWORD hi)
{
    DWORD wptr_1;
    DWORD wptr_2;
    DWORD index;

    v9x_ring_count = 0ul;
    v9x_ring_write(lo + V9X_RING_TEST, hi, V9X_RING_TEST_VALUE);
    v9x_ring_copy(V9X_RUN_DWORDS * 4ul,
                  lo + V9X_COPY_DST + V9X_GUARD_DWORDS * 4ul,
                  lo + V9X_COPY_SRC, hi);
    v9x_ring_copy(V9X_BYTE_COUNT, lo + V9X_BYTE_DST + V9X_BYTE_DST_SKEW,
                  lo + V9X_BYTE_SRC + V9X_BYTE_SRC_SKEW, hi);
    v9x_ring_copy(V9X_OVERLAP_RUN * 4ul,
                  lo + V9X_OVERLAP + V9X_OVERLAP_SHIFT * 4ul,
                  lo + V9X_OVERLAP, hi);
    v9x_ring_fill(V9X_FILL_SIZE_DWORD, V9X_RUN_DWORDS * 4ul,
                  lo + V9X_FILL_DST + V9X_GUARD_DWORDS * 4ul, hi,
                  V9X_FILL_VALUE);
    v9x_ring_fence(lo + V9X_FENCE_1, hi, V9X_FENCE_1_VALUE);
    wptr_1 = v9x_ring_close();
    v9x_ring_fill(V9X_FILL_SIZE_WORD, V9X_RAGGED_BYTES,
                  lo + V9X_RAGGED_DST + V9X_GUARD_DWORDS * 4ul +
                  V9X_RAGGED_SKEW, hi, V9X_RAGGED_VALUE);
    v9x_ring_fence(lo + V9X_FENCE_2, hi, V9X_FENCE_2_VALUE);
    wptr_2 = v9x_ring_close();

    v9x_begin();
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_SCRATCH_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_WRITE, V9X_RING_TEST, V9X_RING_TEST_BEFORE, 0ul);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_COPY_SRC, V9X_RUN_DWORDS, V9X_SEED_COPY);
    v9x_op(V9X_OP_VRAM_FILL, V9X_COPY_DST, V9X_TARGET_DWORDS,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_BYTE_SRC, V9X_BYTE_BUF_DWORDS,
           V9X_SEED_BYTE);
    v9x_op(V9X_OP_VRAM_FILL, V9X_BYTE_DST, V9X_BYTE_BUF_DWORDS,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_OVERLAP, V9X_OVERLAP_DWORDS,
           V9X_SEED_OVERLAP);
    v9x_op(V9X_OP_VRAM_FILL, V9X_FILL_DST, V9X_TARGET_DWORDS,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_FILL, V9X_RAGGED_DST, V9X_RAGGED_DWORDS +
           2ul * V9X_GUARD_DWORDS, V9X_GUARD_VALUE);
    for (index = 0ul; index < v9x_ring_count; ++index) {
        v9x_op(V9X_OP_VRAM_WRITE, V9X_RING + index * 4ul, v9x_ring[index],
               0ul);
    }

    /* sdma_v3_0_start: halt, then golden settings and microcode. */
    v9x_at_halted_f32 = v9x_halt();
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_CNTL, V9X_CZ_AUTO_CTXSW, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_CHICKEN_BITS, V9X_CZ_CHICKEN_CLEAR,
           V9X_CZ_CHICKEN_SET);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_CLK_CTRL, V9X_CZ_CLK_CLEAR, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_IB_CNTL, 0ul, V9X_CZ_IB_CNTL_GOLDEN);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_POWER_CNTL, V9X_CZ_POWER_CLEAR,
           V9X_CZ_POWER_SET);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_RLC0_IB_CNTL, 0ul,
           V9X_CZ_IB_CNTL_GOLDEN);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_RLC1_IB_CNTL, 0ul,
           V9X_CZ_IB_CNTL_GOLDEN);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_UCODE_ADDR, 0ul, 0ul);
    v9x_op(V9X_OP_UCODE, V9X_CZ_SDMA_UCODE_DATA, 0ul, v9x_ucode_dwords);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_UCODE_ADDR, v9x_ucode_version, 0ul);
    v9x_at_loaded_version = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_VERSION,
                                   0ul, 0ul);

    /* gfx_resume's order. TILING_CONFIG is GB_ADDR_CONFIG & 70h, and
     * GB_ADDR_CONFIG read 22010001h (the register probe), so 0. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_TILING_CONFIG, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_SEM_WAIT_FAIL, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_RB_CNTL,
           V9X_CZ_RB_SIZE_MASK | V9X_CZ_RB_ENABLE,
           V9X_RING_LOG2_DWORDS << V9X_CZ_RB_SIZE_SHIFT);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RB_RPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RB_WPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_IB_RPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_IB_OFFSET, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RPTR_ADDR_HI, hi, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RPTR_ADDR_LO,
           (lo + V9X_WRITEBACK) & ~3ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_RB_CNTL, 0ul, V9X_CZ_RPTR_WRITEBACK);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RB_BASE,
           ((lo + V9X_RING) >> 8) | (hi << 24), 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RB_BASE_HI, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RB_WPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_DOORBELL, V9X_CZ_DOORBELL_ENABLE,
           0ul);
    /* The CPU wrote the ring and buffers through BAR0: flush HDP before the
     * engine reads them. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_RB_CNTL, 0ul, V9X_CZ_RB_ENABLE);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_IB_CNTL, 0ul, V9X_CZ_IB_ENABLE);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_SDMA_F32_CNTL, V9X_CZ_F32_HALT, 0ul);
    v9x_at_enabled_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_RB_CNTL,
                                 0ul, 0ul);
    v9x_at_enabled_f32 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_F32_CNTL,
                                0ul, 0ul);
    v9x_at_enabled_status = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_STATUS_REG,
                                   0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RB_WPTR, wptr_1, 0ul);
    v9x_at_wait_1 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE_1, V9X_FENCE_1_VALUE,
                           V9X_FENCE_WAIT_LIMIT);
    v9x_at_rptr_1 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_RB_RPTR, 0ul, 0ul);
    v9x_at_status_1 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_STATUS_REG,
                             0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SDMA_RB_WPTR, wptr_2, 0ul);
    v9x_at_wait_2 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE_2, V9X_FENCE_2_VALUE,
                           V9X_FENCE_WAIT_LIMIT);
    v9x_at_rptr_2 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_RB_RPTR, 0ul, 0ul);
    v9x_at_status_2 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SDMA_STATUS_REG,
                             0ul, 0ul);

    /* Halt it again and put back what call 1 read, F32_CNTL (halted) last. */
    v9x_halt();
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_WRITE, v9x_saved_offsets[index], v9x_saved[index],
               0ul);
    }

    /* The engine wrote through the memory controller; HDP may hold stale
     * lines from the CPU's own writes, so flush and invalidate before the
     * reads back. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_HDP_DEBUG0, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_FENCE_1, 2ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_WRITEBACK, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_RING_TEST, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_COPY_DST, V9X_TARGET_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_BYTE_DST, V9X_BYTE_BUF_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_OVERLAP, V9X_OVERLAP_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_FILL_DST, V9X_TARGET_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_RAGGED_DST,
           V9X_RAGGED_DWORDS + 2ul * V9X_GUARD_DWORDS, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_at_after_extra = v9x_request.count;
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_READ, v9x_extra_offsets[index], 0ul, 0ul);
    }

    v9x_number("Run", "Ops", v9x_request.count);
    v9x_number("Run", "RingDwords", v9x_ring_count);
    v9x_key("Run", "Wptr1", wptr_1);
    v9x_key("Run", "Wptr2", wptr_2);
}

/* Reports up to eight mismatches of a block run against expected values. */
static int v9x_check_run(const char *name, const DWORD *got,
                         const DWORD *expected, DWORD count)
{
    char key[40];
    char text[24];
    DWORD index;
    DWORD bad;

    bad = 0ul;
    for (index = 0ul; index < count; ++index) {
        if (got[index] == expected[index]) {
            continue;
        }
        if (bad < 8ul) {
            lstrcpyA(key, name);
            lstrcatA(key, ".Bad");
            v9x_decimal(key + lstrlenA(key), index);
            v9x_hex(text, got[index], 8);
            text[8] = '/';
            v9x_hex(text + 9, expected[index], 8);
            v9x_text("Mismatch", key, text);
        }
        ++bad;
    }
    lstrcpyA(key, name);
    lstrcatA(key, "Mismatches");
    v9x_number("Result", key, bad);
    return bad == 0ul;
}

static DWORD v9x_expected[V9X_TARGET_DWORDS];

static void v9x_verify(void)
{
    const DWORD *block;
    const DWORD *copy;
    const DWORD *bytes;
    const DWORD *overlap;
    const DWORD *fill;
    const DWORD *ragged;
    BYTE expected_bytes[V9X_BYTE_BUF_DWORDS * 4ul];
    const BYTE *ragged_bytes;
    DWORD original[V9X_OVERLAP_DWORDS];
    DWORD index;
    DWORD outside;
    int memmove_ok;
    int repeat_ok;

    block = v9x_result.block;
    v9x_key("Result", "Fence1", block[0]);
    v9x_key("Result", "Fence2", block[1]);
    v9x_key("Result", "WritebackRptr", block[2]);
    v9x_key("Result", "RingTest", block[3]);
    v9x_pass("RingTestWrite", block[3] == V9X_RING_TEST_VALUE);
    copy = block + 4;
    bytes = copy + V9X_TARGET_DWORDS;
    overlap = bytes + V9X_BYTE_BUF_DWORDS;
    fill = overlap + V9X_OVERLAP_DWORDS;
    ragged = fill + V9X_TARGET_DWORDS;

    /* Dword copy: guards, the pattern, guards. */
    for (index = 0ul; index < V9X_TARGET_DWORDS; ++index) {
        v9x_expected[index] = V9X_GUARD_VALUE;
    }
    for (index = 0ul; index < V9X_RUN_DWORDS; ++index) {
        v9x_expected[V9X_GUARD_DWORDS + index] =
            v9x_pattern(V9X_SEED_COPY, index);
    }
    v9x_pass("CopyDword", v9x_check_run("CopyDword", copy, v9x_expected,
                                        V9X_TARGET_DWORDS));

    /* Byte copy: 37 bytes from source byte 1 to destination byte 3. */
    for (index = 0ul; index < sizeof(expected_bytes); ++index) {
        expected_bytes[index] = (BYTE)V9X_GUARD_VALUE;
    }
    for (index = 0ul; index < V9X_BYTE_COUNT; ++index) {
        expected_bytes[V9X_BYTE_DST_SKEW + index] =
            v9x_pattern_byte(V9X_SEED_BYTE, V9X_BYTE_SRC_SKEW + index);
    }
    v9x_pass("CopyByte", v9x_check_run("CopyByte", bytes,
                                       (const DWORD *)expected_bytes,
                                       V9X_BYTE_BUF_DWORDS));

    /* Overlap: memmove semantics give the original run 4 dwords up; a
     * naive ascending copy repeats the first four dwords. */
    for (index = 0ul; index < V9X_OVERLAP_DWORDS; ++index) {
        original[index] = v9x_pattern(V9X_SEED_OVERLAP, index);
    }
    memmove_ok = 1;
    repeat_ok = 1;
    for (index = 0ul; index < V9X_OVERLAP_RUN; ++index) {
        if (overlap[V9X_OVERLAP_SHIFT + index] != original[index]) {
            memmove_ok = 0;
        }
        if (overlap[V9X_OVERLAP_SHIFT + index] !=
            original[index % V9X_OVERLAP_SHIFT]) {
            repeat_ok = 0;
        }
    }
    v9x_text("Result", "OverlapUp",
             memmove_ok ? "memmove" : (repeat_ok ? "ascending-repeat" :
                                       "other"));

    /* Constant fill, 1024 bytes at a dword boundary. */
    for (index = 0ul; index < V9X_TARGET_DWORDS; ++index) {
        v9x_expected[index] = V9X_GUARD_VALUE;
    }
    for (index = 0ul; index < V9X_RUN_DWORDS; ++index) {
        v9x_expected[V9X_GUARD_DWORDS + index] = V9X_FILL_VALUE;
    }
    v9x_pass("Fill", v9x_check_run("Fill", fill, v9x_expected,
                                   V9X_TARGET_DWORDS));

    /* The ragged fill: what landed, dword by dword, and whether anything
     * outside bytes 2..11 of the run moved. What the bytes inside should
     * be is the question, so they are reported, not judged. */
    for (index = 0ul; index < V9X_RAGGED_DWORDS + 2ul * V9X_GUARD_DWORDS;
         ++index) {
        char key[16];

        lstrcpyA(key, "RaggedAt");
        v9x_decimal(key + 8, index);
        v9x_key("Ragged", key, ragged[index]);
    }
    ragged_bytes = (const BYTE *)(ragged + V9X_GUARD_DWORDS);
    outside = 0ul;
    for (index = 0ul; index < V9X_GUARD_DWORDS * 4ul; ++index) {
        if (((const BYTE *)ragged)[index] != (BYTE)V9X_GUARD_VALUE) {
            ++outside;
        }
    }
    for (index = 0ul; index < V9X_RAGGED_DWORDS * 4ul; ++index) {
        if ((index < V9X_RAGGED_SKEW ||
             index >= V9X_RAGGED_SKEW + V9X_RAGGED_BYTES) &&
            ragged_bytes[index] != (BYTE)V9X_GUARD_VALUE) {
            ++outside;
        }
    }
    for (index = 0ul; index < V9X_GUARD_DWORDS * 4ul; ++index) {
        if (((const BYTE *)(ragged + V9X_GUARD_DWORDS +
                            V9X_RAGGED_DWORDS))[index] !=
            (BYTE)V9X_GUARD_VALUE) {
            ++outside;
        }
    }
    v9x_number("Ragged", "BytesTouchedOutside", outside);
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
    WritePrivateProfileStringA(0, 0, 0, V9X_CZDMA_PATH);
    ExitProcess(code);
}

void __stdcall V9xCarrizoDmaEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD first_extra;
    DWORD fb;
    DWORD lo;
    DWORD hi;
    const DWORD *r;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_CZDMA_PATH);
    v9x_text("Report", "Tool", "V9XCZDMA");
    v9x_text("Report", "Build", V9X_BUILD_ID);

    if (!v9x_load_ucode()) {
        v9x_finish("NO-UCODE", 6u);
    }

    device = CreateFileA(V9X_CZDMA_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_key("Report", "LoadError", GetLastError());
        v9x_finish("VXD-LOAD-FAILED", 1u);
    }

    /* Call 1: read-only. */
    v9x_begin();
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_READ, v9x_saved_offsets[index], 0ul, 0ul);
    }
    first_extra = v9x_request.count;
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_READ, v9x_extra_offsets[index], 0ul, 0ul);
    }
    if (!v9x_dioc(device, 0)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    v9x_key("Report", "Id", v9x_result.id);
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar0High", v9x_result.bar0_high);
    v9x_key("Report", "Bar5", v9x_result.bar5);
    v9x_number("Report", "MemoryMiB", v9x_result.memsize);
    r = v9x_result.op_results;
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_saved[index] = r[index];
        v9x_key("Before", v9x_saved_names[index], r[index]);
    }
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_key("Before", v9x_extra_names[index], r[first_extra + index]);
    }

    if ((v9x_saved[V9X_SAVED_F32_CNTL] & V9X_CZ_F32_HALT) == 0ul ||
        (v9x_saved[V9X_SAVED_RB_CNTL] & V9X_CZ_RB_ENABLE) != 0ul ||
        (r[first_extra + V9X_EXTRA_RESET] & V9X_CZ_SOFT_RESET_SDMA0) != 0ul) {
        CloseHandle(device);
        v9x_finish("ENGINE-NOT-AS-READ", 3u);
    }
    /* FB_LOCATION's start field is 16 MiB units of a 40-bit address:
     * bits 39:32 are its high byte, bits 31:24 its low byte. A low byte
     * that would carry when the window offset is added is refused rather
     * than computed. */
    fb = r[first_extra + V9X_EXTRA_FB] & V9X_CZ_FB_START_MASK;
    if ((fb & 0xfful) > 0xf0ul) {
        CloseHandle(device);
        v9x_finish("FB-START-UNSUPPORTED", 4u);
    }
    lo = ((fb & 0xfful) << 24) + V9X_SCRATCH_GPU_OFFSET;
    hi = fb >> 8;
    v9x_key("Run", "ScratchGpuHigh", hi);
    v9x_key("Run", "ScratchGpuLow", lo);

    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }

    /* Call 2: the CPU reaches the window through BAR0. */
    v9x_begin();
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_CANARY, V9X_CANARY_DWORDS,
           V9X_SEED_CANARY);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_CANARY, V9X_CANARY_DWORDS, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);
    if (!v9x_dioc(device, 0)) {
        CloseHandle(device);
        v9x_finish("CANARY-REFUSED", 5u);
    }
    for (index = 0ul; index < V9X_CANARY_DWORDS; ++index) {
        if (v9x_result.block[index] != v9x_pattern(V9X_SEED_CANARY, index)) {
            v9x_key("Report", "CanaryGot", v9x_result.block[index]);
            CloseHandle(device);
            v9x_finish("SCRATCH-NOT-REACHED", 7u);
        }
    }

    /* Call 3: the run. */
    v9x_build_run(lo, hi);
    if (!v9x_dioc(device, 1)) {
        CloseHandle(device);
        v9x_number("Report", "Executed", v9x_result.executed);
        v9x_finish("RUN-REFUSED", 5u);
    }
    CloseHandle(device);
    r = v9x_result.op_results;
    v9x_number("During", "UcodeDwordsWritten", v9x_result.ucode_written);
    v9x_key("During", "F32AfterHalt", r[v9x_at_halted_f32]);
    v9x_key("During", "VersionAfterLoad", r[v9x_at_loaded_version]);
    v9x_key("During", "RbCntlEnabled", r[v9x_at_enabled_cntl]);
    v9x_key("During", "F32Enabled", r[v9x_at_enabled_f32]);
    v9x_key("During", "StatusEnabled", r[v9x_at_enabled_status]);
    v9x_key("During", "Wait1", r[v9x_at_wait_1]);
    v9x_key("During", "Rptr1", r[v9x_at_rptr_1]);
    v9x_key("During", "Status1", r[v9x_at_status_1]);
    v9x_key("During", "Wait2", r[v9x_at_wait_2]);
    v9x_key("During", "Rptr2", r[v9x_at_rptr_2]);
    v9x_key("During", "Status2", r[v9x_at_status_2]);
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_key("After", v9x_extra_names[index],
                r[v9x_at_after_extra + index]);
    }
    v9x_pass("Fence1", r[v9x_at_wait_1] != V9X_CZDMA_TIMEOUT);
    v9x_pass("Fence2", r[v9x_at_wait_2] != V9X_CZDMA_TIMEOUT);
    v9x_verify();
    v9x_finish("COMPLETE", 0u);
}
