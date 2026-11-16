/*
 * V9XSICP.EXE: AMD Oland (GCN 1, Southern Islands) graphics command
 * processor bring-up: the RLC and CP microcode loaded through their own
 * ports, one ring, ME_INITIALIZE, Linux's ring test, then the clear-state
 * preamble.
 *
 * Phase 6 of docs\plans\ati-oland-gfx6.md, first step: a copy of
 * V9XCKCP (cik_cp_win32.c) for SI, following Linux amdgpu gfx_v6_0.c and
 * radeon sid.h (build\external\carrizo\linux\). What changed from CIK:
 *
 * - The RLC's microcode goes through an indexed port (SICP.VXD writes
 *   RLC_UCODE_ADDR before each dword). PFP, CE and ME stream through
 *   their ports as before; after each load the address registers go back
 *   to 0, as gfx_v6_0_cp_gfx_load_microcode leaves them (CIK wrote the
 *   version there).
 * - rlc_resume also clears RLC_RL_BASE/SIZE and RLC_MC_CNTL/UCODE_CNTL and
 *   sets the load-balance counters as Linux does, but leaves load
 *   balancing (LBPW) off, as the Mullins probe left RLC_LB_* alone. Oland
 *   has no power or clock gating flags in amdgpu, so init_pg and init_cg
 *   do nothing and the RLC needs no save/restore or clear-state buffer.
 * - SI has no CP_RB0_BASE_HI, CP_DEVICE_ID, CP_MAX_CONTEXT or
 *   CP_ENDIAN_SWAP: cp_gfx_start's first submission is ME_INITIALIZE
 *   (max_hw_contexts 8, less one; device id 1) and the CE partitions
 *   (C000h, E000h), committed before the CP is unhalted.
 * - cp_gfx_resume's ring 0 writes: the semaphore timers, CP_DEBUG,
 *   SCRATCH_ADDR, SCRATCH_UMSK and CP_RB0_RPTR beside CIK's. Rings 1 and
 *   2 (compute) are not touched.
 * - SCRATCH_REG0 is a config register (8500h): the ring test is
 *   SET_CONFIG_REG, not SET_UCONFIG_REG.
 * - The ring pads to 256 dwords with type-2 NOPs (80000000h), SI's gfx
 *   ring alignment and NOP in gfx_v6_0_ring_funcs_gfx.
 * - The GUI-idle interrupts are turned off as Linux does before the loads,
 *   then left off: nothing here takes an interrupt. Call 1's value goes
 *   back at the end.
 *
 * gfx_v6_0_constants_init (golden registers, tiling, GB_ADDR_CONFIG),
 * which Linux runs before rlc_resume, is not run: the ring test does not
 * draw. It is the next step.
 *
 * SICP.VXD executes the op lists built here and refuses anything outside
 * its allow-lists. Writes C:\V9XDIAG\V9XSICP.INI. The microcode is
 * PFP.BIN, CE.BIN, ME.BIN and RLC.BIN in the working directory,
 * linux-firmware's amdgpu/oland_{pfp,ce,me,rlc}.bin copied there by hand;
 * they are not in this tree.
 *
 * Call 1 is read-only. The run goes ahead only if the CP's three
 * microengines are halted, the RLC stopped and nothing held in reset, as
 * V9XSIMM read them (docs\decisions\2026-11-16-oland-first-survey.md).
 * /readonly stops there.
 *
 * Call 2 checks the CPU reaches the scratch window (BAR0 + 248 MiB,
 * 64 KiB).
 *
 * Call 3: rlc_resume, cp_resume's loads and ring 0, cp_gfx_start's first
 * submission and the unhalt, then two submissions:
 *
 *   1. Linux's ring test: SET_CONFIG_REG writing DEADBEEF to SCRATCH_REG0,
 *      over the CAFEDEAD the CPU wrote;
 *   2. cp_gfx_start's second submission without si_cs_data's context
 *      registers (PREAMBLE_CNTL begin and end, CLEAR_STATE,
 *      VGT_VERTEX_REUSE_BLOCK_CNTL and VGT_OUT_DEALLOC_CNTL), then a
 *      second scratch write, 600DCAFE.
 *
 * Then the CP is halted and the RLC stopped, every register written is
 * put back as call 1 read it, HDP is flushed and invalidated, the
 * writeback is read back and the window restored.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_SICP_PATH        V9X_DIAG_PATH("V9XSICP.INI")
#define V9X_SICP_VXD         "\\\\.\\SICP.VXD"
#define V9X_SICP_MAGIC       0x50434953ul
#define V9X_SICP_RUN         1u
#define V9X_SICP_OP_MAX      512u
#define V9X_SICP_DATA_MAX    16384u
#define V9X_SICP_BLOCK       4096u
#define V9X_SICP_HEADER      16u
#define V9X_SICP_TIMEOUT     0xfffffffful

#define V9X_ST_COMPLETE  0x00000010ul

#define V9X_OP_MMIO_READ    1ul
#define V9X_OP_MMIO_WRITE   2ul
#define V9X_OP_VRAM_WRITE   3ul
#define V9X_OP_VRAM_FILL    5ul
#define V9X_OP_VRAM_BLOCK   6ul
#define V9X_OP_WAIT_MMIO    8ul
#define V9X_OP_SAVE         9ul
#define V9X_OP_RESTORE      10ul
#define V9X_OP_VRAM_PATTERN 11ul
#define V9X_OP_DELAY        12ul
#define V9X_OP_MMIO_RMW     13ul
#define V9X_OP_UCODE        14ul
#define V9X_OP_VRAM_DATA    15ul

/* SICP.VXD's SC_PATTERN_STEP. */
#define V9X_PATTERN_STEP    0x9e3779b9ul

/* SRBM_STATUS reads standing in for Linux's udelay(50) and mdelay(1): an
 * uncached PCIe read takes on the order of a microsecond. */
#define V9X_DELAY_READS     2000ul
#define V9X_DELAY_MS_READS  20000ul

/* Registers, byte offsets: radeon sid.h, checked against amdgpu's
 * gfx_6_0_d.h, oss_1_0_d.h and gmc_6_0_d.h. */
#define V9X_SI_SRBM_STATUS          0x00e50ul
#define V9X_SI_MC_VM_FB_LOCATION    0x02024ul
#define V9X_SI_HDP_DEBUG0           0x02f30ul
#define V9X_SI_HDP_MEM_FLUSH        0x05480ul
#define V9X_SI_GRBM_STATUS2         0x08008ul
#define V9X_SI_GRBM_STATUS          0x08010ul
#define V9X_SI_GRBM_SOFT_RESET      0x08020ul
#define V9X_SI_SCRATCH_REG0         0x08500ul
#define V9X_SI_SCRATCH_UMSK         0x08540ul
#define V9X_SI_SCRATCH_ADDR         0x08544ul
#define V9X_SI_CP_SEM_WAIT_TIMER    0x085bcul
#define V9X_SI_CP_SEM_INCOMPLETE    0x085c8ul
#define V9X_SI_CP_STALLED_STAT1     0x08674ul
#define V9X_SI_CP_BUSY_STAT         0x0867cul
#define V9X_SI_CP_STAT              0x08680ul
#define V9X_SI_CP_ME_CNTL           0x086d8ul
#define V9X_SI_CP_RB0_RPTR          0x08700ul
#define V9X_SI_CP_RB_WPTR_DELAY     0x08704ul
#define V9X_SI_GB_ADDR_CONFIG       0x098f8ul
#define V9X_SI_CP_RB0_BASE          0x0c100ul
#define V9X_SI_CP_RB0_CNTL          0x0c104ul
#define V9X_SI_CP_RB0_RPTR_ADDR     0x0c10cul
#define V9X_SI_CP_RB0_RPTR_ADDR_HI  0x0c110ul
#define V9X_SI_CP_RB0_WPTR          0x0c114ul
#define V9X_SI_CP_PFP_UCODE_ADDR    0x0c150ul
#define V9X_SI_CP_PFP_UCODE_DATA    0x0c154ul
#define V9X_SI_CP_ME_RAM_RADDR      0x0c158ul
#define V9X_SI_CP_ME_RAM_WADDR      0x0c15cul
#define V9X_SI_CP_ME_RAM_DATA       0x0c160ul
#define V9X_SI_CP_CE_UCODE_ADDR     0x0c168ul
#define V9X_SI_CP_CE_UCODE_DATA     0x0c16cul
#define V9X_SI_CP_INT_CNTL_RING0    0x0c1a8ul
#define V9X_SI_CP_DEBUG             0x0c1fcul
#define V9X_SI_RLC_CNTL             0x0c300ul
#define V9X_SI_RLC_RL_BASE          0x0c304ul
#define V9X_SI_RLC_RL_SIZE          0x0c308ul
#define V9X_SI_RLC_LB_CNTL          0x0c30cul
#define V9X_SI_RLC_LB_CNTR_MAX      0x0c314ul
#define V9X_SI_RLC_LB_CNTR_INIT     0x0c318ul
#define V9X_SI_RLC_UCODE_ADDR       0x0c32cul
#define V9X_SI_RLC_UCODE_DATA       0x0c330ul
#define V9X_SI_RLC_MC_CNTL          0x0c344ul
#define V9X_SI_RLC_UCODE_CNTL       0x0c348ul
#define V9X_SI_RLC_STAT             0x0c34cul
#define V9X_SI_RLC_PG_CNTL          0x0c35cul
#define V9X_SI_RLC_CGCG_CGLS_CTRL   0x0c404ul
#define V9X_SI_RLC_LB_INIT_CU_MASK  0x0c41cul
#define V9X_SI_RLC_SERDES_BUSY_0    0x0c464ul
#define V9X_SI_RLC_SERDES_BUSY_1    0x0c468ul

/* Fields: sid.h and gfx_6_0_sh_mask.h. CP_ME_CNTL CE_HALT bit 24,
 * PFP_HALT 26, ME_HALT 28. RLC_CNTL RLC_ENABLE_F32 bit 0.
 * GRBM_SOFT_RESET SOFT_RESET_RLC bit 2. CP_INT_CNTL_RING0 CNTX_BUSY and
 * CNTX_EMPTY, bits 19 and 20. RLC_STAT RLC_BUSY 0, GFX_POWER 1, GFX_CLOCK
 * 2, GFX_LS 3: enable_gui_idle_interrupt(false) waits for clock and power
 * alone. CP_RB0_CNTL RB_BUFSZ 5:0, RB_BLKSZ 13:8, RB_RPTR_WR_ENA 31. */
#define V9X_SI_CP_HALTS             0x15000000ul
#define V9X_SI_RLC_ENABLE           0x00000001ul
#define V9X_SI_SOFT_RESET_RLC       0x00000004ul
#define V9X_SI_GUI_IDLE_INTS        0x00180000ul
#define V9X_SI_RLC_STAT_MASK        0x0000000ful
#define V9X_SI_RLC_STAT_IDLE        0x00000006ul
#define V9X_SI_RB_RPTR_WR_ENA       0x80000000ul
#define V9X_SI_FB_START_MASK        0x0000fffful

/* gfx_v6_0_constants_init: Oland's max_hw_contexts is 8; ME_INITIALIZE
 * takes one less. */
#define V9X_SI_MAX_CONTEXT          7ul

/* PM4: sid.h. */
#define V9X_PACKET3(op, n) \
    ((3ul << 30) | (((DWORD)(n) & 0x3ffful) << 16) | (((DWORD)(op) & 0xfful) << 8))
#define V9X_P3_SET_BASE             0x11ul
#define V9X_P3_CLEAR_STATE          0x12ul
#define V9X_P3_ME_INITIALIZE        0x44ul
#define V9X_P3_PREAMBLE_CNTL        0x4aul
#define V9X_P3_SET_CONFIG_REG       0x68ul
#define V9X_P3_SET_CONTEXT_REG      0x69ul
#define V9X_PREAMBLE_BEGIN          0x20000000ul
#define V9X_PREAMBLE_END            0x30000000ul
#define V9X_CE_PARTITION_BASE       3ul
#define V9X_ME_INIT_DEVICE_ID_1     0x00010000ul
/* Dword indices: PACKET3_SET_CONFIG_REG_START and mmSCRATCH_REG0. */
#define V9X_CONFIG_REG_START        0x2000ul
#define V9X_SCRATCH_REG0_INDEX      0x2140ul
#define V9X_SI_RING_NOP             0x80000000ul

/* The firmware images: common_firmware_header in amdgpu_ucode.h. */
#define V9X_FW_SIZE_BYTES       0x00ul
#define V9X_FW_VERSION          0x08ul
#define V9X_FW_UCODE_VERSION    0x10ul
#define V9X_FW_UCODE_BYTES      0x14ul
#define V9X_FW_ARRAY_OFFSET     0x18ul
#define V9X_FW_HEADER_BYTES     0x20ul
#define V9X_FW_FILE_MAX         0x8000ul

/* The scratch window and its layout: a 16 KiB ring, the read-pointer
 * writeback behind it. */
#define V9X_SCRATCH_GPU_OFFSET  0x0f800000ul
#define V9X_SCRATCH_DWORDS      16384ul
#define V9X_RING                0x0000ul
#define V9X_RING_LOG2_QWORDS    11ul
#define V9X_RING_ALIGN_DWORDS   256ul
#define V9X_RING_MAX_DWORDS     1024ul
#define V9X_WRITEBACK           0x8000ul
#define V9X_CANARY              0xc000ul
#define V9X_CANARY_DWORDS       16ul
#define V9X_SEED_CANARY         0x50000005ul

#define V9X_SCRATCH_BEFORE      0xcafedeadul
#define V9X_SCRATCH_TEST        0xdeadbeeful
#define V9X_SCRATCH_PREAMBLE    0x600dcafeul

struct v9x_sicp_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

/* The ops, then the data array: SICP.VXD reads the data at ops[count]. */
struct v9x_sicp_request {
    DWORD count;
    DWORD data_count;
    struct v9x_sicp_op ops[V9X_SICP_OP_MAX];
};

struct v9x_sicp_result {
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
    DWORD ucode_written;
    DWORD reserved[3];
    DWORD op_results[V9X_SICP_OP_MAX];
    DWORD block[V9X_SICP_BLOCK];
};

/* The header must be the VxD's SC_HEADER_DWORDS long. */
typedef char v9x_sicp_header_matches[
    offsetof(struct v9x_sicp_result, op_results) ==
    sizeof(DWORD) * V9X_SICP_HEADER ? 1 : -1];

/* The registers call 1 reads and the run puts back, in this order: the
 * ring first, the RLC's controls next, the CP's halts last, so the CP is
 * put back halted after everything it reads. CP_RB0_RPTR is not put back:
 * it takes a write only with RB_RPTR_WR_ENA set. */
static const DWORD v9x_saved_offsets[] = {
    V9X_SI_CP_RB0_CNTL, V9X_SI_CP_RB0_BASE,
    V9X_SI_CP_RB0_RPTR_ADDR, V9X_SI_CP_RB0_RPTR_ADDR_HI, V9X_SI_CP_RB0_WPTR,
    V9X_SI_CP_RB_WPTR_DELAY, V9X_SI_CP_SEM_WAIT_TIMER,
    V9X_SI_CP_SEM_INCOMPLETE, V9X_SI_CP_DEBUG, V9X_SI_SCRATCH_ADDR,
    V9X_SI_SCRATCH_UMSK, V9X_SI_CP_INT_CNTL_RING0, V9X_SI_SCRATCH_REG0,
    V9X_SI_RLC_RL_BASE, V9X_SI_RLC_RL_SIZE, V9X_SI_RLC_LB_CNTL,
    V9X_SI_RLC_LB_CNTR_MAX, V9X_SI_RLC_LB_CNTR_INIT,
    V9X_SI_RLC_LB_INIT_CU_MASK, V9X_SI_RLC_MC_CNTL, V9X_SI_RLC_UCODE_CNTL,
    V9X_SI_RLC_PG_CNTL, V9X_SI_RLC_CGCG_CGLS_CTRL,
    V9X_SI_RLC_CNTL, V9X_SI_CP_ME_CNTL
};
#define V9X_SAVED_COUNT (sizeof(v9x_saved_offsets) / sizeof(v9x_saved_offsets[0]))
#define V9X_SAVED_RLC_CNTL  (V9X_SAVED_COUNT - 2u)
#define V9X_SAVED_ME_CNTL   (V9X_SAVED_COUNT - 1u)

static const char * const v9x_saved_names[V9X_SAVED_COUNT] = {
    "CP_RB0_CNTL", "CP_RB0_BASE", "CP_RB0_RPTR_ADDR", "CP_RB0_RPTR_ADDR_HI",
    "CP_RB0_WPTR", "CP_RB_WPTR_DELAY", "CP_SEM_WAIT_TIMER",
    "CP_SEM_INCOMPLETE_TIMER_CNTL", "CP_DEBUG", "SCRATCH_ADDR",
    "SCRATCH_UMSK", "CP_INT_CNTL_RING0", "SCRATCH_REG0", "RLC_RL_BASE",
    "RLC_RL_SIZE", "RLC_LB_CNTL", "RLC_LB_CNTR_MAX", "RLC_LB_CNTR_INIT",
    "RLC_LB_INIT_CU_MASK", "RLC_MC_CNTL", "RLC_UCODE_CNTL", "RLC_PG_CNTL",
    "RLC_CGCG_CGLS_CTRL", "RLC_CNTL", "CP_ME_CNTL"
};

/* Read-only extras for the report, before and after. */
static const DWORD v9x_extra_offsets[] = {
    V9X_SI_GRBM_STATUS, V9X_SI_GRBM_STATUS2, V9X_SI_GRBM_SOFT_RESET,
    V9X_SI_CP_STAT, V9X_SI_CP_BUSY_STAT, V9X_SI_CP_STALLED_STAT1,
    V9X_SI_CP_RB0_RPTR, V9X_SI_RLC_STAT, V9X_SI_RLC_SERDES_BUSY_0,
    V9X_SI_RLC_SERDES_BUSY_1, V9X_SI_SRBM_STATUS, V9X_SI_MC_VM_FB_LOCATION,
    V9X_SI_GB_ADDR_CONFIG
};
#define V9X_EXTRA_COUNT (sizeof(v9x_extra_offsets) / sizeof(v9x_extra_offsets[0]))
#define V9X_EXTRA_RESET  2u
#define V9X_EXTRA_FB     11u

static const char * const v9x_extra_names[V9X_EXTRA_COUNT] = {
    "GRBM_STATUS", "GRBM_STATUS2", "GRBM_SOFT_RESET", "CP_STAT",
    "CP_BUSY_STAT", "CP_STALLED_STAT1", "CP_RB0_RPTR", "RLC_STAT",
    "RLC_SERDES_MASTER_BUSY_0", "RLC_SERDES_MASTER_BUSY_1", "SRBM_STATUS",
    "MC_VM_FB_LOCATION", "GB_ADDR_CONFIG"
};

/* The four images, in the order the data array holds them. */
struct v9x_sicp_image {
    const char *file;
    const char *name;
    DWORD header_major;
    DWORD first;
    DWORD dwords;
    DWORD version;
};

#define V9X_IMAGE_PFP 0u
#define V9X_IMAGE_CE  1u
#define V9X_IMAGE_ME  2u
#define V9X_IMAGE_RLC 3u
#define V9X_IMAGE_COUNT 4u

static struct v9x_sicp_image v9x_images[V9X_IMAGE_COUNT] = {
    { "PFP.BIN", "Pfp", 1ul, 0ul, 0ul, 0ul },
    { "CE.BIN", "Ce", 1ul, 0ul, 0ul, 0ul },
    { "ME.BIN", "Me", 1ul, 0ul, 0ul, 0ul },
    { "RLC.BIN", "Rlc", 1ul, 0ul, 0ul, 0ul }
};

/* The request, then room for the data array right after count ops. */
static struct {
    struct v9x_sicp_request request;
    DWORD spill[V9X_SICP_DATA_MAX];
} v9x_buffer;
#define v9x_request (v9x_buffer.request)

static struct v9x_sicp_result v9x_result;
static DWORD v9x_saved[V9X_SAVED_COUNT];
static DWORD v9x_ring[V9X_RING_MAX_DWORDS];
static DWORD v9x_ring_count;
static BYTE v9x_file[V9X_FW_FILE_MAX];
static DWORD v9x_data[V9X_SICP_DATA_MAX];
static DWORD v9x_data_dwords;

/* Indices into v9x_result.op_results of the values the report needs. */
static DWORD v9x_at_serdes_0;
static DWORD v9x_at_serdes_1;
static DWORD v9x_at_rlc_idle;
static DWORD v9x_at_rlc_cntl;
static DWORD v9x_at_rlc_stat;
static DWORD v9x_at_me_cntl_halted;
static DWORD v9x_at_me_cntl_running;
static DWORD v9x_at_rb_cntl;
static DWORD v9x_at_rptr_0;
static DWORD v9x_at_wait_1;
static DWORD v9x_at_rptr_1;
static DWORD v9x_at_stat_1;
static DWORD v9x_at_grbm_1;
static DWORD v9x_at_wait_2;
static DWORD v9x_at_rptr_2;
static DWORD v9x_at_stat_2;
static DWORD v9x_at_grbm_2;
static DWORD v9x_at_scratch_2;
static DWORD v9x_at_after_extra;
static DWORD v9x_at_after_me_cntl;
static DWORD v9x_at_after_rlc_cntl;

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
    WritePrivateProfileStringA(section, key, value, V9X_SICP_PATH);
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

/* Sends count ops and, after them, the microcode if with_data. */
static int v9x_dioc(HANDLE device, int with_data)
{
    DWORD returned;
    DWORD bytes;
    DWORD *data;
    DWORD index;
    BOOL ok;

    bytes = 8ul + v9x_request.count * sizeof(struct v9x_sicp_op);
    if (with_data) {
        data = (DWORD *)&v9x_request.ops[v9x_request.count];
        for (index = 0ul; index < v9x_data_dwords; ++index) {
            data[index] = v9x_data[index];
        }
        v9x_request.data_count = v9x_data_dwords;
        bytes += v9x_data_dwords * 4ul;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_SICP_RUN, &v9x_buffer, bytes,
                         &v9x_result, sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_SICP_MAGIC) {
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

/* One image from the working directory into the data array: a
 * common_firmware_header whose sizes agree with the file and whose major
 * version is the one Linux reads it with. */
static int v9x_load_image(struct v9x_sicp_image *image)
{
    HANDLE file;
    DWORD length;
    DWORD read;
    DWORD offset;
    DWORD bytes;
    DWORD index;
    char key[24];

    file = CreateFileA(image->file, GENERIC_READ, FILE_SHARE_READ, 0,
                       OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE) {
        v9x_text("Ucode", image->name, "not-found");
        return 0;
    }
    length = GetFileSize(file, 0);
    read = 0ul;
    if (length < V9X_FW_HEADER_BYTES || length > V9X_FW_FILE_MAX ||
        !ReadFile(file, v9x_file, length, &read, 0) || read != length) {
        CloseHandle(file);
        v9x_text("Ucode", image->name, "unreadable");
        return 0;
    }
    CloseHandle(file);

    offset = v9x_le32(v9x_file + V9X_FW_ARRAY_OFFSET);
    bytes = v9x_le32(v9x_file + V9X_FW_UCODE_BYTES);
    image->version = v9x_le32(v9x_file + V9X_FW_UCODE_VERSION);
    if (v9x_le32(v9x_file + V9X_FW_SIZE_BYTES) != length ||
        (v9x_le32(v9x_file + V9X_FW_VERSION) & 0xfffful) !=
            image->header_major ||
        (bytes % 4ul) != 0ul || bytes == 0ul || offset > length ||
        bytes > length - offset ||
        bytes / 4ul > V9X_SICP_DATA_MAX - v9x_data_dwords) {
        v9x_text("Ucode", image->name, "bad-header");
        return 0;
    }
    image->first = v9x_data_dwords;
    image->dwords = bytes / 4ul;
    for (index = 0ul; index < image->dwords; ++index) {
        v9x_data[v9x_data_dwords++] = v9x_le32(v9x_file + offset + index * 4ul);
    }
    lstrcpyA(key, image->name);
    lstrcatA(key, "Dwords");
    v9x_number("Ucode", key, image->dwords);
    lstrcpyA(key, image->name);
    lstrcatA(key, "Version");
    v9x_key("Ucode", key, image->version);
    return 1;
}

/* A CP image through its port: address 0, the data, address 0 again, as
 * gfx_v6_0_cp_gfx_load_microcode does. */
static void v9x_load(DWORD which, DWORD address, DWORD data)
{
    const struct v9x_sicp_image *image = &v9x_images[which];

    v9x_op(V9X_OP_MMIO_WRITE, address, 0ul, 0ul);
    v9x_op(V9X_OP_UCODE, data, image->first, image->dwords);
    v9x_op(V9X_OP_MMIO_WRITE, address, 0ul, 0ul);
}

static void v9x_ring_put(DWORD value)
{
    if (v9x_ring_count < V9X_RING_MAX_DWORDS) {
        v9x_ring[v9x_ring_count] = value;
    }
    ++v9x_ring_count;
}

/* Pads to SI's 256-dword boundary; returns the write pointer in dwords. */
static DWORD v9x_ring_close(void)
{
    while ((v9x_ring_count % V9X_RING_ALIGN_DWORDS) != 0ul) {
        v9x_ring_put(V9X_SI_RING_NOP);
    }
    return v9x_ring_count;
}

static void v9x_ring_scratch(DWORD value)
{
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONFIG_REG, 1ul));
    v9x_ring_put(V9X_SCRATCH_REG0_INDEX - V9X_CONFIG_REG_START);
    v9x_ring_put(value);
}

/* gfx_v6_0_cp_gfx_start's first submission: ME_INITIALIZE, then the CE
 * partitions. */
static void v9x_ring_start(void)
{
    v9x_ring_put(V9X_PACKET3(V9X_P3_ME_INITIALIZE, 5ul));
    v9x_ring_put(0x1ul);
    v9x_ring_put(0x0ul);
    v9x_ring_put(V9X_SI_MAX_CONTEXT);
    v9x_ring_put(V9X_ME_INIT_DEVICE_ID_1);
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_BASE, 2ul));
    v9x_ring_put(V9X_CE_PARTITION_BASE);
    v9x_ring_put(0xc000ul);
    v9x_ring_put(0xe000ul);
}

/* Its second, without si_cs_data's context registers between the
 * preamble's begin and end. */
static void v9x_ring_preamble(void)
{
    v9x_ring_put(V9X_PACKET3(V9X_P3_PREAMBLE_CNTL, 0ul));
    v9x_ring_put(V9X_PREAMBLE_BEGIN);
    v9x_ring_put(V9X_PACKET3(V9X_P3_PREAMBLE_CNTL, 0ul));
    v9x_ring_put(V9X_PREAMBLE_END);
    v9x_ring_put(V9X_PACKET3(V9X_P3_CLEAR_STATE, 0ul));
    v9x_ring_put(0ul);
    v9x_ring_put(V9X_PACKET3(V9X_P3_SET_CONTEXT_REG, 2ul));
    v9x_ring_put(0x00000316ul);
    v9x_ring_put(0x0000000eul);
    v9x_ring_put(0x00000010ul);
}

static DWORD v9x_pattern(DWORD seed, DWORD index)
{
    return seed + index * V9X_PATTERN_STEP;
}

/* Call 3's op list. lo/hi are the scratch window's GPU address. */
static void v9x_build_run(DWORD lo, DWORD hi)
{
    DWORD wptr_0;
    DWORD wptr_1;
    DWORD wptr_2;
    DWORD rb_cntl;
    DWORD index;

    v9x_ring_count = 0ul;
    v9x_ring_start();
    wptr_0 = v9x_ring_close();
    v9x_ring_scratch(V9X_SCRATCH_TEST);
    wptr_1 = v9x_ring_close();
    v9x_ring_preamble();
    v9x_ring_scratch(V9X_SCRATCH_PREAMBLE);
    wptr_2 = v9x_ring_close();

    /* gfx_v6_0_cp_gfx_resume's CP_RB0_CNTL for a 16 KiB ring: RB_BUFSZ, and
     * RB_BLKSZ log2 of a 4 KiB GPU page in quadwords (9). */
    rb_cntl = V9X_RING_LOG2_QWORDS | (9ul << 8);

    v9x_begin();
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_SCRATCH_DWORDS, 0ul);
    /* The ring goes in the data array behind the microcode: 256-dword
     * submissions are too many ops a dword. */
    if (v9x_ring_count <= V9X_RING_MAX_DWORDS &&
        v9x_ring_count <= V9X_SICP_DATA_MAX - v9x_data_dwords) {
        v9x_op(V9X_OP_VRAM_DATA, V9X_RING, v9x_data_dwords, v9x_ring_count);
        for (index = 0ul; index < v9x_ring_count; ++index) {
            v9x_data[v9x_data_dwords++] = v9x_ring[index];
        }
    }
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_SCRATCH_REG0, V9X_SCRATCH_BEFORE, 0ul);

    /* rlc_resume: rlc_stop (RLC_CNTL 0, the GUI-idle interrupts off and
     * RLC_STAT down to clock and power, the SERDES idle), rlc_reset,
     * Linux's RL/LB/MC/UCODE_CNTL values, the microcode, rlc_start. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_CNTL, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_SI_CP_INT_CNTL_RING0, V9X_SI_GUI_IDLE_INTS,
           0ul);
    v9x_at_rlc_idle = v9x_op(V9X_OP_WAIT_MMIO, V9X_SI_RLC_STAT,
                             V9X_SI_RLC_STAT_MASK, V9X_SI_RLC_STAT_IDLE);
    v9x_at_serdes_0 = v9x_op(V9X_OP_WAIT_MMIO, V9X_SI_RLC_SERDES_BUSY_0,
                             0xfffffffful, 0ul);
    v9x_at_serdes_1 = v9x_op(V9X_OP_WAIT_MMIO, V9X_SI_RLC_SERDES_BUSY_1,
                             0xfffffffful, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_GRBM_SOFT_RESET, V9X_SI_SOFT_RESET_RLC,
           0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_GRBM_SOFT_RESET, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_RL_BASE, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_RL_SIZE, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_LB_CNTL, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_LB_CNTR_MAX, 0xfffffffful, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_LB_CNTR_INIT, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_LB_INIT_CU_MASK, 0xfffffffful, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_MC_CNTL, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_UCODE_CNTL, 0ul, 0ul);
    v9x_op(V9X_OP_UCODE, V9X_SI_RLC_UCODE_DATA, v9x_images[V9X_IMAGE_RLC].first,
           v9x_images[V9X_IMAGE_RLC].dwords);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_UCODE_ADDR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_CNTL, V9X_SI_RLC_ENABLE, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_at_rlc_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_SI_RLC_CNTL, 0ul, 0ul);
    v9x_at_rlc_stat = v9x_op(V9X_OP_MMIO_READ, V9X_SI_RLC_STAT, 0ul, 0ul);

    /* cp_gfx_load_microcode: halt and SCRATCH_UMSK 0, then PFP, CE, ME,
     * and every address register back to 0. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_ME_CNTL, V9X_SI_CP_HALTS, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_SCRATCH_UMSK, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_at_me_cntl_halted = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_ME_CNTL,
                                   0ul, 0ul);
    v9x_load(V9X_IMAGE_PFP, V9X_SI_CP_PFP_UCODE_ADDR,
             V9X_SI_CP_PFP_UCODE_DATA);
    v9x_load(V9X_IMAGE_CE, V9X_SI_CP_CE_UCODE_ADDR, V9X_SI_CP_CE_UCODE_DATA);
    v9x_load(V9X_IMAGE_ME, V9X_SI_CP_ME_RAM_WADDR, V9X_SI_CP_ME_RAM_DATA);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_PFP_UCODE_ADDR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_CE_UCODE_ADDR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_ME_RAM_WADDR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_ME_RAM_RADDR, 0ul, 0ul);

    /* cp_gfx_resume, ring 0. The ring's 40-bit address >> 8 fits
     * CP_RB0_BASE: bits 39:32 land in its top byte. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_SEM_WAIT_TIMER, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_SEM_INCOMPLETE, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB_WPTR_DELAY, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_DEBUG, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_SCRATCH_ADDR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_CNTL, rb_cntl, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_CNTL,
           rb_cntl | V9X_SI_RB_RPTR_WR_ENA, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_WPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_RPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_RPTR_ADDR, lo + V9X_WRITEBACK,
           0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_RPTR_ADDR_HI, hi & 0xfful, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_SCRATCH_UMSK, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_MS_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_CNTL, rb_cntl, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_BASE,
           ((lo + V9X_RING) >> 8) | (hi << 24), 0ul);
    v9x_at_rb_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_RB0_CNTL, 0ul, 0ul);

    /* cp_gfx_start: the first submission committed, then the unhalt. The
     * ring was written through BAR0, so HDP is flushed before the CP
     * reads it. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_WPTR, wptr_0, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_ME_CNTL, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_at_me_cntl_running = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_ME_CNTL,
                                    0ul, 0ul);
    v9x_at_rptr_0 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_RB0_RPTR, 0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_WPTR, wptr_1, 0ul);
    v9x_at_wait_1 = v9x_op(V9X_OP_WAIT_MMIO, V9X_SI_SCRATCH_REG0,
                           0xfffffffful, V9X_SCRATCH_TEST);
    v9x_at_rptr_1 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_RB0_RPTR, 0ul, 0ul);
    v9x_at_stat_1 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_STAT, 0ul, 0ul);
    v9x_at_grbm_1 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_GRBM_STATUS, 0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_WPTR, wptr_2, 0ul);
    v9x_at_wait_2 = v9x_op(V9X_OP_WAIT_MMIO, V9X_SI_SCRATCH_REG0,
                           0xfffffffful, V9X_SCRATCH_PREAMBLE);
    v9x_at_rptr_2 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_RB0_RPTR, 0ul, 0ul);
    v9x_at_stat_2 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_STAT, 0ul, 0ul);
    v9x_at_grbm_2 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_GRBM_STATUS, 0ul, 0ul);
    v9x_at_scratch_2 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_SCRATCH_REG0, 0ul, 0ul);

    /* Halt the CP and stop the RLC, then put back what call 1 read,
     * CP_ME_CNTL (halted) last. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_ME_CNTL, V9X_SI_CP_HALTS, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_RLC_CNTL, 0ul, 0ul);
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_WRITE, v9x_saved_offsets[index], v9x_saved[index],
               0ul);
    }
    v9x_at_after_me_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_ME_CNTL,
                                  0ul, 0ul);
    v9x_at_after_rlc_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_SI_RLC_CNTL,
                                   0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_HDP_DEBUG0, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_WRITEBACK, 1ul, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_at_after_extra = v9x_request.count;
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_READ, v9x_extra_offsets[index], 0ul, 0ul);
    }

    v9x_number("Run", "Ops", v9x_request.count);
    v9x_number("Run", "RingDwords", v9x_ring_count);
    v9x_key("Run", "Wptr0", wptr_0);
    v9x_key("Run", "Wptr1", wptr_1);
    v9x_key("Run", "Wptr2", wptr_2);
    v9x_key("Run", "RbCntl", rb_cntl);
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
    WritePrivateProfileStringA(0, 0, 0, V9X_SICP_PATH);
    ExitProcess(code);
}

void __stdcall V9xOlandCpEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD first_extra;
    DWORD fb;
    DWORD lo;
    DWORD hi;
    const DWORD *r;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_SICP_PATH);
    v9x_text("Report", "Tool", "V9XSICP");
    v9x_text("Report", "Build", V9X_BUILD_ID);

    v9x_data_dwords = 0ul;
    for (index = 0ul; index < V9X_IMAGE_COUNT; ++index) {
        if (!v9x_load_image(&v9x_images[index])) {
            v9x_finish("NO-UCODE", 6u);
        }
    }

    device = CreateFileA(V9X_SICP_VXD, 0, 0, 0, CREATE_NEW,
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
    v9x_key("Report", "Bar2", v9x_result.bar2);
    v9x_key("Report", "Bar2High", v9x_result.bar2_high);
    v9x_number("Report", "MemoryMiB", v9x_result.memsize);
    r = v9x_result.op_results;
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_saved[index] = r[index];
        v9x_key("Before", v9x_saved_names[index], r[index]);
    }
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_key("Before", v9x_extra_names[index], r[first_extra + index]);
    }

    if ((v9x_saved[V9X_SAVED_ME_CNTL] & V9X_SI_CP_HALTS) != V9X_SI_CP_HALTS ||
        (v9x_saved[V9X_SAVED_RLC_CNTL] & V9X_SI_RLC_ENABLE) != 0ul ||
        r[first_extra + V9X_EXTRA_RESET] != 0ul) {
        CloseHandle(device);
        v9x_finish("ENGINE-NOT-AS-READ", 3u);
    }
    /* FB_LOCATION's start: bits 39:32 its high byte, 31:24 its low byte;
     * a low byte the window offset would carry out of is refused. */
    fb = r[first_extra + V9X_EXTRA_FB] & V9X_SI_FB_START_MASK;
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
    if (v9x_ring_count > V9X_RING_MAX_DWORDS ||
        v9x_request.count > V9X_SICP_OP_MAX) {
        CloseHandle(device);
        v9x_finish("RUN-TOO-LONG", 8u);
    }
    if (!v9x_dioc(device, 1)) {
        CloseHandle(device);
        v9x_number("Report", "Executed", v9x_result.executed);
        v9x_finish("RUN-REFUSED", 5u);
    }
    CloseHandle(device);
    r = v9x_result.op_results;
    v9x_number("During", "UcodeDwordsWritten", v9x_result.ucode_written);
    v9x_key("During", "RlcIdleWait", r[v9x_at_rlc_idle]);
    v9x_key("During", "Serdes0Wait", r[v9x_at_serdes_0]);
    v9x_key("During", "Serdes1Wait", r[v9x_at_serdes_1]);
    v9x_key("During", "RlcCntlStarted", r[v9x_at_rlc_cntl]);
    v9x_key("During", "RlcStat", r[v9x_at_rlc_stat]);
    v9x_key("During", "MeCntlHalted", r[v9x_at_me_cntl_halted]);
    v9x_key("During", "RbCntl", r[v9x_at_rb_cntl]);
    v9x_key("During", "MeCntlRunning", r[v9x_at_me_cntl_running]);
    v9x_key("During", "Rptr0", r[v9x_at_rptr_0]);
    v9x_key("During", "Wait1", r[v9x_at_wait_1]);
    v9x_key("During", "Rptr1", r[v9x_at_rptr_1]);
    v9x_key("During", "CpStat1", r[v9x_at_stat_1]);
    v9x_key("During", "GrbmStatus1", r[v9x_at_grbm_1]);
    v9x_key("During", "Wait2", r[v9x_at_wait_2]);
    v9x_key("During", "Rptr2", r[v9x_at_rptr_2]);
    v9x_key("During", "CpStat2", r[v9x_at_stat_2]);
    v9x_key("During", "GrbmStatus2", r[v9x_at_grbm_2]);
    v9x_key("During", "Scratch2", r[v9x_at_scratch_2]);
    v9x_key("After", "CP_ME_CNTL", r[v9x_at_after_me_cntl]);
    v9x_key("After", "RLC_CNTL", r[v9x_at_after_rlc_cntl]);
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_key("After", v9x_extra_names[index],
                r[v9x_at_after_extra + index]);
    }
    v9x_key("Result", "WritebackRptr", v9x_result.block[0]);
    v9x_pass("RingTest", r[v9x_at_wait_1] != V9X_SICP_TIMEOUT);
    v9x_pass("Preamble", r[v9x_at_wait_2] != V9X_SICP_TIMEOUT);
    v9x_finish("COMPLETE", 0u);
}
