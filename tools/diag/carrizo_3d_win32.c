/*
 * V9XCZ3D.EXE: AMD Carrizo (Radeon R7E, GCN 3) first 3D draw: the CP
 * brought up as V9XCZCP does, the full clear state, then a solid rectangle
 * through the shader core into a linear colour buffer, checked pixel by
 * pixel.
 *
 * Phases 2 and 3 of docs\plans\ati-carrizo-gfx8-3d.md. CZCP.VXD executes
 * the op lists built here. Writes C:\V9XDIAG\V9XCZ3D.INI. The microcode is
 * PFP.BIN, CE.BIN, ME.BIN and RLC.BIN in the working directory, as for
 * V9XCZCP. The streams and shaders come from the host-tested vi_3d.c and
 * vi_shader.c; the clear state from vi_clear_state.c, generated from
 * Linux's clearstate_vi.h. Everything that differs from V9XCZCP
 * (carrizo_cp_win32.c) is the third submission and its check.
 *
 * Call 1 is read-only: the CP's and RLC's registers this run writes, and
 * their status. The run goes ahead only if the CP's three microengines are
 * halted, the RLC stopped and nothing held in reset, as the register probe
 * read them (docs\decisions\2026-10-28-carrizo-register-probe.md).
 * /readonly stops there.
 *
 * Call 2 checks the CPU reaches the scratch window (BAR0 + 248 MiB, 64 KiB).
 *
 * Call 3 does what Linux v4.9 gfx_v8_0.c does with smc_load_fw=0, in its
 * order: the golden registers and gpu_init (vi_gpu_init.c, through
 * CZCP.VXD's table op; left as written after the run), rlc_resume (stop with the GUI-idle interrupts off and the SERDES
 * idle, coarse-grain clock gating and light sleep off, power gating off,
 * an RLC soft reset, the RLC microcode, start; init_pg does nothing with
 * power gating off), cp_gfx_load_microcode (halt, PFP, CE, ME),
 * cp_gfx_resume's ring registers (VMID 0, a 16 KiB ring, the read-pointer
 * writeback in the window, no doorbell) and cp_gfx_start's CP registers
 * and unhalt. Then two submissions:
 *
 *   1. Linux's ring test: SET_UCONFIG_REG writing DEADBEEF to SCRATCH_REG0,
 *      over the CAFEDEAD the CPU wrote;
 *   2. cp_gfx_start's whole stream, vi_cs_data included, and a second
 *      scratch write, 600DCAFE, behind it;
 *   3. a solid rectangle (8,4)-(40,12) into a 64x16 32 bpp target
 *      (COLOR_8_8_8_8, tile index 8, linear-aligned) and an end-of-pipe
 *      event that flushes the caches and writes a fence. The shaders are
 *      vi_shader.c's probe variants: each stores a marker beside the
 *      target first, so the report shows whether the VS and PS ran.
 *      With /gouraud instead: two triangles of Direct3D TL vertices over
 *      the whole target from a vertex buffer in the window, a red ramp
 *      checked pixel by pixel (Phase 4 step 1).
 *
 * Each is padded to 16 dwords with PACKET3(NOP, 3FFFh), v4.9's gfx ring
 * NOP and alignment. Then the CP is halted and the RLC stopped, every
 * CP and RLC register written is put back as call 1 read it, HDP is flushed and
 * invalidated, the writeback, the fence and the target are read back and
 * the window restored.
 *
 * Packets: vid.h PACKET3 (type 31:30, count 29:16, opcode 15:8) and its
 * opcodes; register offsets gfx_8_0_d.h and _sh_mask.h
 * (build\external\carrizo\linux\), dword indices times four.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_gpu_init.h"
#include "velocity9x/vi_shader.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_CZCP_PATH        V9X_DIAG_PATH("V9XCZ3D.INI")
#define V9X_CZCP_VXD         "\\\\.\\CZCP.VXD"
#define V9X_CZCP_MAGIC       0x50435a43ul
#define V9X_CZCP_RUN         1u
#define V9X_CZCP_OP_MAX      512u
#define V9X_CZCP_DATA_MAX    16384u
#define V9X_CZCP_BLOCK       4096u
#define V9X_CZCP_HEADER      16u
#define V9X_CZCP_TIMEOUT     0xfffffffful

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
#define V9X_OP_WAIT_VRAM    7ul
#define V9X_OP_VRAM_DATA    15ul
#define V9X_OP_MMIO_TABLE   16ul

#define V9X_CZ_GB_ADDR_CONFIG       0x098f8ul
#define V9X_CZ_GB_TILE_MODE8        0x09930ul

/* CZCP.VXD's CC_PATTERN_STEP. */
#define V9X_PATTERN_STEP    0x9e3779b9ul

/* SRBM_STATUS reads standing in for Linux's udelay(50) after the RLC
 * reset, start and the CP's halt and unhalt: an uncached PCIe read takes
 * on the order of a microsecond. */
#define V9X_DELAY_READS     2000ul

/* Registers: gfx_8_0_d.h, oss_3_0_d.h, bif_5_0_d.h, gmc_8_1_d.h. */
#define V9X_CZ_SRBM_STATUS          0x00e50ul
#define V9X_CZ_MC_VM_FB_LOCATION    0x02024ul
#define V9X_CZ_HDP_DEBUG0           0x02f30ul
#define V9X_CZ_HDP_MEM_FLUSH        0x05480ul
#define V9X_CZ_GRBM_STATUS2         0x08008ul
#define V9X_CZ_GRBM_STATUS          0x08010ul
#define V9X_CZ_GRBM_STATUS_SE0      0x08014ul
#define V9X_CZ_CC_SHADER_ARRAY      0x089bcul
#define V9X_CZ_USER_SHADER_ARRAY    0x089c0ul
#define V9X_CZ_CC_RB_DISABLE        0x098f4ul
#define V9X_CZ_USER_RB_DISABLE      0x09b7cul
#define V9X_CZ_GRBM_SOFT_RESET      0x08020ul
#define V9X_CZ_CP_MEC_CNTL          0x08234ul
#define V9X_CZ_CP_STALLED_STAT1     0x08674ul
#define V9X_CZ_CP_BUSY_STAT         0x0867cul
#define V9X_CZ_CP_STAT              0x08680ul
#define V9X_CZ_CP_ME_CNTL           0x086d8ul
#define V9X_CZ_CP_RB0_RPTR          0x08700ul
#define V9X_CZ_CP_RB_WPTR_DELAY     0x08704ul
#define V9X_CZ_CP_RB0_BASE          0x0c100ul
#define V9X_CZ_CP_RB0_CNTL          0x0c104ul
#define V9X_CZ_CP_RB0_RPTR_ADDR     0x0c10cul
#define V9X_CZ_CP_RB0_RPTR_ADDR_HI  0x0c110ul
#define V9X_CZ_CP_RB0_WPTR          0x0c114ul
#define V9X_CZ_CP_DEVICE_ID         0x0c12cul
#define V9X_CZ_CP_ENDIAN_SWAP       0x0c140ul
#define V9X_CZ_CP_RB_VMID           0x0c144ul
#define V9X_CZ_CP_RB_DOORBELL       0x0c164ul
#define V9X_CZ_CP_INT_CNTL_RING0    0x0c1a8ul
#define V9X_CZ_CP_MAX_CONTEXT       0x0c2b8ul
#define V9X_CZ_CP_RB0_BASE_HI       0x0c2c4ul
#define V9X_CZ_SCRATCH_REG0         0x30100ul
#define V9X_CZ_RLC_CNTL             0x3b000ul
#define V9X_CZ_RLC_STAT             0x3b010ul
#define V9X_CZ_RLC_GPM_STAT         0x3b100ul
#define V9X_CZ_RLC_PG_CNTL          0x3b10cul
#define V9X_CZ_RLC_CGCG_CGLS_CTRL   0x3b124ul
#define V9X_CZ_RLC_SERDES_CU_BUSY   0x3b184ul
#define V9X_CZ_RLC_SERDES_NONCU     0x3b188ul
#define V9X_CZ_CP_PFP_UCODE_ADDR    0x3e050ul
#define V9X_CZ_CP_PFP_UCODE_DATA    0x3e054ul
#define V9X_CZ_CP_ME_RAM_WADDR      0x3e058ul
#define V9X_CZ_CP_ME_RAM_DATA       0x3e05cul
#define V9X_CZ_CP_CE_UCODE_ADDR     0x3e060ul
#define V9X_CZ_CP_CE_UCODE_DATA     0x3e064ul
#define V9X_CZ_RLC_GPM_UCODE_ADDR   0x3e0f0ul
#define V9X_CZ_RLC_GPM_UCODE_DATA   0x3e0f4ul

/* Fields: gfx_8_0_sh_mask.h. CP_ME_CNTL CE_HALT bit 24, PFP_HALT 26,
 * ME_HALT 28. RLC_CNTL RLC_ENABLE_F32 bit 0. RLC_CGCG_CGLS_CTRL CGCG_EN
 * bit 0, CGLS_EN bit 1. GRBM_SOFT_RESET SOFT_RESET_RLC bit 2.
 * CP_INT_CNTL_RING0's four GUI-idle interrupt enables, bits 18-21.
 * RLC_SERDES_NONCU_MASTER_BUSY's SE, GC, TC0 and TC1 busy bits.
 * CP_RB0_CNTL RB_BUFSZ 5:0 (log2 of the ring's quadwords), RB_BLKSZ
 * 13:8, MTYPE 16:15, MIN_IB_AVAILSZ 23:22, RB_RPTR_WR_ENA 31.
 * CP_RB_DOORBELL_CONTROL DOORBELL_EN bit 30. */
#define V9X_CZ_CP_HALTS             0x15000000ul
#define V9X_CZ_RLC_ENABLE           0x00000001ul
#define V9X_CZ_CGCG_CGLS            0x00000003ul
#define V9X_CZ_SOFT_RESET_RLC       0x00000004ul
#define V9X_CZ_GUI_IDLE_INTS        0x003c0000ul
#define V9X_CZ_NONCU_BUSY           0x000dfffful
#define V9X_CZ_RB_RPTR_WR_ENA       0x80000000ul
#define V9X_CZ_DOORBELL_EN          0x40000000ul
#define V9X_CZ_FB_START_MASK        0x0000fffful

/* gfx_v8_0_gpu_init: max_hw_contexts 8 for Carrizo; cp_gfx_start writes
 * one less. */
#define V9X_CZ_MAX_CONTEXT          7ul

/* PM4: vid.h. */
#define V9X_PACKET3(op, n) \
    ((3ul << 30) | (((DWORD)(n) & 0x3ffful) << 16) | (((DWORD)(op) & 0xfful) << 8))
#define V9X_P3_NOP                  0x10ul
#define V9X_P3_SET_BASE             0x11ul
#define V9X_P3_CLEAR_STATE          0x12ul
#define V9X_P3_CONTEXT_CONTROL      0x28ul
#define V9X_P3_PREAMBLE_CNTL        0x4aul
#define V9X_P3_SET_CONTEXT_REG      0x69ul
#define V9X_P3_SET_UCONFIG_REG      0x79ul
#define V9X_PREAMBLE_BEGIN          0x20000000ul
#define V9X_PREAMBLE_END            0x30000000ul
#define V9X_CE_PARTITION_BASE       3ul
#define V9X_CONTEXT_REG_START       0xa000ul
#define V9X_UCONFIG_REG_START       0xc000ul
/* Dword indices: mmPA_SC_RASTER_CONFIG and mmSCRATCH_REG0. */
#define V9X_PA_SC_RASTER_CONFIG     0xa0d4ul
#define V9X_SCRATCH_REG0_INDEX      0xc040ul
/* cp_gfx_start's CHIP_CARRIZO raster configuration. */
#define V9X_CZ_RASTER_CONFIG        0x00000002ul
#define V9X_CZ_RASTER_CONFIG_1      0x00000000ul
#define V9X_CZ_RING_NOP             V9X_PACKET3(V9X_P3_NOP, 0x3ffful)

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
#define V9X_RING_BYTES          0x4000ul
#define V9X_RING_LOG2_QWORDS    11ul
#define V9X_RING_ALIGN_DWORDS   16ul
#define V9X_VS                  0x4000ul
#define V9X_PS                  0x4100ul
#define V9X_WRITEBACK           0x8000ul
#define V9X_FENCE               0x8100ul
#define V9X_TARGET              0x9000ul
/* The probe shaders' markers (vi_shader.h): the VS's VertexID at +0, the
 * PS's red at +4, then for each vertex its VertexID at +16, x at +32, y
 * at +48, v2 at +64 and the first compare's VCC at +80, plus 4 x
 * (VertexID & 3). */
#define V9X_PROBE               0xa000ul
#define V9X_PROBE_PS            (V9X_PROBE + 4ul)
#define V9X_PROBE_DWORDS        24ul
#define V9X_PROBE_AT_VS         0u
#define V9X_PROBE_AT_PS         1u
#define V9X_PROBE_AT_VERTEX     4u
#define V9X_PROBE_AT_X          8u
#define V9X_PROBE_AT_Y          12u

/* /gouraud: two triangles of TL vertices over the whole target, red 0 at
 * the left edge to FFh at the right, green 80h, blue 40h, alpha FFh;
 * every pixel within +-2 per channel of the ramp at its centre. */
#define V9X_VERTICES            0xb000ul
#define V9X_TL_DWORDS           8ul
#define V9X_GOURAUD_VERTICES    6ul
#define V9X_GOURAUD_LEFT        0xff008040ul
#define V9X_GOURAUD_RIGHT       0xffff8040ul
#define V9X_GOURAUD_TOLERANCE   2ul
#define V9X_FLOAT_HALF          0x3f000000ul
#define V9X_FLOAT_ONE           0x3f800000ul
#define V9X_FLOAT_16            0x41800000ul
#define V9X_FLOAT_64            0x42800000ul
#define V9X_CANARY              0xc000ul
#define V9X_CANARY_DWORDS       16ul
#define V9X_SEED_CANARY         0x50000005ul

/* The target: 64 pixels a row (a linear-aligned pitch is a multiple of 64
 * elements), 16 rows, 32 bpp, 4 KiB; the rectangle inside it. */
#define V9X_TARGET_PITCH        64ul
#define V9X_TARGET_HEIGHT       16ul
#define V9X_TARGET_DWORDS       (V9X_TARGET_PITCH * V9X_TARGET_HEIGHT)
#define V9X_RECT_LEFT           8ul
#define V9X_RECT_TOP            4ul
#define V9X_RECT_RIGHT          40ul
#define V9X_RECT_BOTTOM         12ul
#define V9X_GUARD_VALUE         0x5a5a5a5aul
#define V9X_FENCE_VALUE         0x3d3d3d3dul
#define V9X_FENCE_WAIT_LIMIT    20000000ul

/* R 1.0, G 0.5, B 0.25, A 0.75: four different bytes, so where each lands
 * shows the channel order. */
static const DWORD v9x_colour[4] = {
    0x3f800000ul, 0x3f000000ul, 0x3e800000ul, 0x3f400000ul
};

#define V9X_SCRATCH_BEFORE      0xcafedeadul
#define V9X_SCRATCH_TEST        0xdeadbeeful
#define V9X_SCRATCH_PREAMBLE    0x600dcafeul

struct v9x_czcp_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

/* The ops, then the data array: CZCP.VXD reads the data at ops[count]. */
struct v9x_czcp_request {
    DWORD count;
    DWORD data_count;
    struct v9x_czcp_op ops[V9X_CZCP_OP_MAX];
};

struct v9x_czcp_result {
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
    DWORD op_results[V9X_CZCP_OP_MAX];
    DWORD block[V9X_CZCP_BLOCK];
};

/* The header must be the VxD's CC_HEADER_DWORDS long. */
typedef char v9x_czcp_header_matches[
    offsetof(struct v9x_czcp_result, op_results) ==
    sizeof(DWORD) * V9X_CZCP_HEADER ? 1 : -1];

/* The registers call 1 reads and the run puts back, in this order: the
 * ring first, the RLC's controls next, the CP's halts last, so the CP is
 * put back halted after everything it reads. */
static const DWORD v9x_saved_offsets[] = {
    V9X_CZ_CP_RB0_CNTL, V9X_CZ_CP_RB0_BASE, V9X_CZ_CP_RB0_BASE_HI,
    V9X_CZ_CP_RB0_RPTR_ADDR, V9X_CZ_CP_RB0_RPTR_ADDR_HI, V9X_CZ_CP_RB0_WPTR,
    V9X_CZ_CP_RB_WPTR_DELAY, V9X_CZ_CP_RB_VMID, V9X_CZ_CP_RB_DOORBELL,
    V9X_CZ_CP_MAX_CONTEXT, V9X_CZ_CP_ENDIAN_SWAP, V9X_CZ_CP_DEVICE_ID,
    V9X_CZ_CP_INT_CNTL_RING0, V9X_CZ_SCRATCH_REG0, V9X_CZ_RLC_PG_CNTL,
    V9X_CZ_RLC_CGCG_CGLS_CTRL, V9X_CZ_RLC_CNTL, V9X_CZ_CP_ME_CNTL
};
#define V9X_SAVED_COUNT (sizeof(v9x_saved_offsets) / sizeof(v9x_saved_offsets[0]))
#define V9X_SAVED_RLC_CNTL  (V9X_SAVED_COUNT - 2u)
#define V9X_SAVED_ME_CNTL   (V9X_SAVED_COUNT - 1u)

static const char * const v9x_saved_names[V9X_SAVED_COUNT] = {
    "CP_RB0_CNTL", "CP_RB0_BASE", "CP_RB0_BASE_HI", "CP_RB0_RPTR_ADDR",
    "CP_RB0_RPTR_ADDR_HI", "CP_RB0_WPTR", "CP_RB_WPTR_DELAY", "CP_RB_VMID",
    "CP_RB_DOORBELL_CONTROL", "CP_MAX_CONTEXT", "CP_ENDIAN_SWAP",
    "CP_DEVICE_ID", "CP_INT_CNTL_RING0", "SCRATCH_REG0", "RLC_PG_CNTL",
    "RLC_CGCG_CGLS_CTRL", "RLC_CNTL", "CP_ME_CNTL"
};

/* Read-only extras for the report, before and after. */
static const DWORD v9x_extra_offsets[] = {
    V9X_CZ_GRBM_STATUS, V9X_CZ_GRBM_STATUS2, V9X_CZ_GRBM_SOFT_RESET,
    V9X_CZ_CP_STAT, V9X_CZ_CP_BUSY_STAT, V9X_CZ_CP_STALLED_STAT1,
    V9X_CZ_CP_RB0_RPTR, V9X_CZ_CP_MEC_CNTL, V9X_CZ_RLC_STAT,
    V9X_CZ_RLC_GPM_STAT, V9X_CZ_RLC_SERDES_CU_BUSY, V9X_CZ_RLC_SERDES_NONCU,
    V9X_CZ_SRBM_STATUS, V9X_CZ_MC_VM_FB_LOCATION, V9X_CZ_GB_TILE_MODE8,
    V9X_CZ_GRBM_STATUS_SE0, V9X_CZ_CC_SHADER_ARRAY, V9X_CZ_USER_SHADER_ARRAY,
    V9X_CZ_CC_RB_DISABLE, V9X_CZ_USER_RB_DISABLE
};
#define V9X_EXTRA_COUNT (sizeof(v9x_extra_offsets) / sizeof(v9x_extra_offsets[0]))
#define V9X_EXTRA_RESET  2u
#define V9X_EXTRA_FB     13u
#define V9X_EXTRA_TILE8  14u

static const char * const v9x_extra_names[V9X_EXTRA_COUNT] = {
    "GRBM_STATUS", "GRBM_STATUS2", "GRBM_SOFT_RESET", "CP_STAT",
    "CP_BUSY_STAT", "CP_STALLED_STAT1", "CP_RB0_RPTR", "CP_MEC_CNTL",
    "RLC_STAT", "RLC_GPM_STAT", "RLC_SERDES_CU_MASTER_BUSY",
    "RLC_SERDES_NONCU_MASTER_BUSY", "SRBM_STATUS", "MC_VM_FB_LOCATION",
    "GB_TILE_MODE8", "GRBM_STATUS_SE0", "CC_GC_SHADER_ARRAY_CONFIG",
    "GC_USER_SHADER_ARRAY_CONFIG", "CC_RB_BACKEND_DISABLE",
    "GC_USER_RB_BACKEND_DISABLE"
};

/* The four images, in the order the data array holds them. */
struct v9x_czcp_image {
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

static struct v9x_czcp_image v9x_images[V9X_IMAGE_COUNT] = {
    { "PFP.BIN", "Pfp", 1ul, 0ul, 0ul, 0ul },
    { "CE.BIN", "Ce", 1ul, 0ul, 0ul, 0ul },
    { "ME.BIN", "Me", 1ul, 0ul, 0ul, 0ul },
    { "RLC.BIN", "Rlc", 2ul, 0ul, 0ul, 0ul }
};

/* The request, then room for the data array right after count ops. */
static struct {
    struct v9x_czcp_request request;
    DWORD spill[V9X_CZCP_DATA_MAX];
} v9x_buffer;
#define v9x_request (v9x_buffer.request)

static struct v9x_czcp_result v9x_result;
static DWORD v9x_saved[V9X_SAVED_COUNT];
static DWORD v9x_ring[V9X_RING_BYTES / 4ul];
static DWORD v9x_shader[64];
static BYTE v9x_file[V9X_FW_FILE_MAX];
static DWORD v9x_data[V9X_CZCP_DATA_MAX];
static DWORD v9x_data_dwords;
static int v9x_gouraud;
static DWORD v9x_vertices[V9X_GOURAUD_VERTICES * V9X_TL_DWORDS];
static struct v9x_vi_reg_seq v9x_init_ops[V9X_VI_GPU_INIT_OPS_MAX];

/* Indices into v9x_result.op_results of the values the report needs. */
static DWORD v9x_at_cu_busy;
static DWORD v9x_at_noncu_busy;
static DWORD v9x_at_rlc_stat;
static DWORD v9x_at_rlc_gpm;
static DWORD v9x_at_rlc_cntl;
static DWORD v9x_at_me_cntl_halted;
static DWORD v9x_at_me_cntl_running;
static DWORD v9x_at_rb_cntl;
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
static DWORD v9x_at_tile_mode8_written;
static DWORD v9x_at_init;
static DWORD v9x_at_fifo_before;
static DWORD v9x_at_fifo_after;
static DWORD v9x_at_sh_mem_before;
static DWORD v9x_at_sh_mem_after;
static DWORD v9x_at_addr_config_after;
static DWORD v9x_at_wait_3;
static DWORD v9x_at_rptr_3;
static DWORD v9x_at_stat_3;
static DWORD v9x_at_grbm_3;
static DWORD v9x_at_tile_mode8;
static DWORD v9x_at_addr_config;

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
    WritePrivateProfileStringA(section, key, value, V9X_CZCP_PATH);
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

    bytes = 8ul + v9x_request.count * sizeof(struct v9x_czcp_op);
    if (with_data) {
        data = (DWORD *)&v9x_request.ops[v9x_request.count];
        for (index = 0ul; index < v9x_data_dwords; ++index) {
            data[index] = v9x_data[index];
        }
        v9x_request.data_count = v9x_data_dwords;
        bytes += v9x_data_dwords * 4ul;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_CZCP_RUN, &v9x_buffer, bytes,
                         &v9x_result, sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_CZCP_MAGIC) {
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
static int v9x_load_image(struct v9x_czcp_image *image)
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
        bytes / 4ul > V9X_CZCP_DATA_MAX - v9x_data_dwords) {
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

/* An image through its port: address 0, the data, then the version in
 * the address register, as Linux does for each. */
static void v9x_load(DWORD which, DWORD address, DWORD data)
{
    const struct v9x_czcp_image *image = &v9x_images[which];

    v9x_op(V9X_OP_MMIO_WRITE, address, 0ul, 0ul);
    v9x_op(V9X_OP_UCODE, data, image->first, image->dwords);
    v9x_op(V9X_OP_MMIO_WRITE, address, image->version, 0ul);
}

static void v9x_ring_scratch(struct v9x_vi_pm4 *stream, DWORD value)
{
    v9x_vi_pm4_set_uconfig(stream, V9X_SCRATCH_REG0_INDEX, value);
}

/* Appends dwords to the data array; returns where they start. */
static DWORD v9x_data_append(const DWORD *dwords, DWORD count)
{
    DWORD first = v9x_data_dwords;
    DWORD index;

    for (index = 0ul; index < count && v9x_data_dwords < V9X_CZCP_DATA_MAX;
         ++index) {
        v9x_data[v9x_data_dwords++] = dwords[index];
    }
    return first;
}

static DWORD v9x_pattern(DWORD seed, DWORD index)
{
    return seed + index * V9X_PATTERN_STEP;
}

/* One TL vertex: sx, sy, sz, rhw, D3DCOLOR, specular, tu, tv. */
static void v9x_tl_vertex(DWORD *out, DWORD x, DWORD y, DWORD colour)
{
    out[0] = x;
    out[1] = y;
    out[2] = V9X_FLOAT_HALF;
    out[3] = V9X_FLOAT_ONE;
    out[4] = colour;
    out[5] = 0ul;
    out[6] = 0ul;
    out[7] = 0ul;
}

/* The target as two triangles: top-left, top-right, bottom-left, then
 * top-right, bottom-right, bottom-left. */
static void v9x_gouraud_vertices(void)
{
    DWORD *v = v9x_vertices;

    v9x_tl_vertex(v + 0, 0ul, 0ul, V9X_GOURAUD_LEFT);
    v9x_tl_vertex(v + 8, V9X_FLOAT_64, 0ul, V9X_GOURAUD_RIGHT);
    v9x_tl_vertex(v + 16, 0ul, V9X_FLOAT_16, V9X_GOURAUD_LEFT);
    v9x_tl_vertex(v + 24, V9X_FLOAT_64, 0ul, V9X_GOURAUD_RIGHT);
    v9x_tl_vertex(v + 32, V9X_FLOAT_64, V9X_FLOAT_16, V9X_GOURAUD_RIGHT);
    v9x_tl_vertex(v + 40, 0ul, V9X_FLOAT_16, V9X_GOURAUD_LEFT);
}

/* Call 3's op list. lo/hi are the scratch window's GPU address. */
static int v9x_build_run(DWORD lo, DWORD hi)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_solid_rect rect;
    DWORD wptr_1;
    DWORD wptr_2;
    DWORD wptr_3;
    DWORD rb_cntl;
    DWORD index;
    DWORD ring_first;
    DWORD vs_first;
    DWORD vs_count;
    DWORD ps_first;
    DWORD ps_count;
    DWORD init_first;
    DWORD init_count;
    DWORD vertex_first;

    /* The ring: the ring test; the full start stream and a scratch write
     * behind it; the draw and its end-of-pipe fence. */
    v9x_vi_pm4_init(&stream, v9x_ring, V9X_RING_BYTES / 4ul);
    v9x_ring_scratch(&stream, V9X_SCRATCH_TEST);
    v9x_vi_pm4_pad(&stream);
    wptr_1 = stream.count;
    v9x_vi_3d_start(&stream);
    v9x_ring_scratch(&stream, V9X_SCRATCH_PREAMBLE);
    v9x_vi_pm4_pad(&stream);
    wptr_2 = stream.count;

    for (index = 0ul; index < 4ul; ++index) {
        rect.color[index] = v9x_colour[index];
    }
    rect.target_low = lo + V9X_TARGET;
    rect.target_high = hi;
    rect.pitch_pixels = V9X_TARGET_PITCH;
    rect.height = V9X_TARGET_HEIGHT;
    rect.format = V9X_VI_CB_FORMAT_8_8_8_8;
    rect.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    rect.vs_low = lo + V9X_VS;
    rect.ps_low = lo + V9X_PS;
    rect.shader_high = hi;
    rect.left = V9X_RECT_LEFT;
    rect.top = V9X_RECT_TOP;
    rect.right = V9X_RECT_RIGHT;
    rect.bottom = V9X_RECT_BOTTOM;
    rect.fence_low = lo + V9X_FENCE;
    rect.fence_high = hi;
    rect.fence_value = V9X_FENCE_VALUE;
    rect.probe_vs_low = lo + V9X_PROBE;
    rect.probe_ps_low = lo + V9X_PROBE_PS;
    rect.probe_high = hi;
    if (v9x_gouraud) {
        struct v9x_vi_tl_draw draw;

        draw.target_low = rect.target_low;
        draw.target_high = hi;
        draw.pitch_pixels = V9X_TARGET_PITCH;
        draw.height = V9X_TARGET_HEIGHT;
        draw.format = V9X_VI_CB_FORMAT_8_8_8_8;
        draw.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
        draw.vs_low = rect.vs_low;
        draw.ps_low = rect.ps_low;
        draw.shader_high = hi;
        draw.vertex_low = lo + V9X_VERTICES;
        draw.vertex_high = hi;
        draw.vertex_count = V9X_GOURAUD_VERTICES;
        draw.fence_low = rect.fence_low;
        draw.fence_high = hi;
        draw.fence_value = V9X_FENCE_VALUE;
        if (v9x_vi_3d_tl_draw(&stream, &draw) != V9X_STATUS_OK) {
            return 0;
        }
    } else if (v9x_vi_3d_solid_rect(&stream, &rect) != V9X_STATUS_OK) {
        return 0;
    }
    v9x_vi_pm4_pad(&stream);
    wptr_3 = stream.count;
    if (stream.overflow != 0ul) {
        return 0;
    }

    /* The ring and the shaders go behind the microcode in the data array. */
    ring_first = v9x_data_append(v9x_ring, stream.count);
    vs_count = v9x_gouraud ? v9x_vi_shader_tl_vs(v9x_shader) :
                             v9x_vi_shader_probe_vs(v9x_shader);
    vs_first = v9x_data_append(v9x_shader, vs_count);
    ps_count = v9x_gouraud ? v9x_vi_shader_gouraud_ps(v9x_shader) :
                             v9x_vi_shader_probe_ps(v9x_shader);
    ps_first = v9x_data_append(v9x_shader, ps_count);
    vertex_first = v9x_data_append(v9x_vertices,
                                   V9X_GOURAUD_VERTICES * V9X_TL_DWORDS);
    if (v9x_data_dwords !=
        vertex_first + V9X_GOURAUD_VERTICES * V9X_TL_DWORDS) {
        return 0;
    }

    /* gpu_init's list, three dwords an entry, as CZCP.VXD's table op reads
     * it. */
    init_count = v9x_vi_gpu_init_ops(v9x_init_ops, V9X_VI_GPU_INIT_OPS_MAX);
    if (init_count == 0ul) {
        return 0;
    }
    init_first = v9x_data_dwords;
    for (index = 0ul; index < init_count; ++index) {
        v9x_data_append(&v9x_init_ops[index].offset, 1ul);
        v9x_data_append(&v9x_init_ops[index].clear, 1ul);
        v9x_data_append(&v9x_init_ops[index].set, 1ul);
    }
    if (v9x_data_dwords != init_first + init_count * 3ul) {
        return 0;
    }

    /* cp_gfx_resume's CP_RB0_CNTL for a 16 KiB ring. */
    rb_cntl = V9X_RING_LOG2_QWORDS | ((V9X_RING_LOG2_QWORDS - 2ul) << 8) |
              (3ul << 15) | (1ul << 22);

    v9x_begin();
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_SCRATCH_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_DATA, V9X_RING, ring_first, stream.count);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VS, vs_first, vs_count);
    v9x_op(V9X_OP_VRAM_DATA, V9X_PS, ps_first, ps_count);
    v9x_op(V9X_OP_VRAM_DATA, V9X_VERTICES, vertex_first,
           V9X_GOURAUD_VERTICES * V9X_TL_DWORDS);
    v9x_op(V9X_OP_VRAM_FILL, V9X_TARGET, V9X_TARGET_DWORDS, V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_FILL, V9X_PROBE, V9X_PROBE_DWORDS, V9X_GUARD_VALUE);
    v9x_at_tile_mode8 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_GB_TILE_MODE8, 0ul,
                               0ul);
    v9x_at_addr_config = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_GB_ADDR_CONFIG, 0ul,
                                0ul);
    v9x_at_fifo_before = v9x_op(V9X_OP_MMIO_READ, V9X_VI_PA_SC_FIFO_SIZE,
                                0ul, 0ul);
    v9x_at_sh_mem_before = v9x_op(V9X_OP_MMIO_READ, V9X_VI_SH_MEM_CONFIG,
                                  0ul, 0ul);

    /* gfx_v8_0_hw_init's order: the golden registers and gpu_init before
     * rlc_resume and cp_resume. None of it is put back after the run: it
     * is the configuration Linux leaves the engine in, and the BIOS left
     * none (the tile table read unprogrammed). */
    v9x_at_init = v9x_op(V9X_OP_MMIO_TABLE, init_first, init_count, 0ul);
    v9x_at_tile_mode8_written = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_GB_TILE_MODE8,
                                       0ul, 0ul);
    v9x_at_addr_config_after = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_GB_ADDR_CONFIG,
                                      0ul, 0ul);
    v9x_at_fifo_after = v9x_op(V9X_OP_MMIO_READ, V9X_VI_PA_SC_FIFO_SIZE,
                               0ul, 0ul);
    v9x_at_sh_mem_after = v9x_op(V9X_OP_MMIO_READ, V9X_VI_SH_MEM_CONFIG,
                                 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_SCRATCH_REG0, V9X_SCRATCH_BEFORE, 0ul);

    /* rlc_resume: rlc_stop, CG off, PG off, rlc_reset, the microcode,
     * rlc_start. The SERDES waits are wait_for_rlc_serdes's, read through
     * the GRBM_GFX_INDEX the BIOS left (broadcast; one SE, one SH). */
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_RLC_CNTL, V9X_CZ_RLC_ENABLE, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_CP_INT_CNTL_RING0, V9X_CZ_GUI_IDLE_INTS,
           0ul);
    v9x_at_cu_busy = v9x_op(V9X_OP_WAIT_MMIO, V9X_CZ_RLC_SERDES_CU_BUSY,
                            0xfffffffful, 0ul);
    v9x_at_noncu_busy = v9x_op(V9X_OP_WAIT_MMIO, V9X_CZ_RLC_SERDES_NONCU,
                               V9X_CZ_NONCU_BUSY, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_RLC_CGCG_CGLS_CTRL, V9X_CZ_CGCG_CGLS, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_RLC_PG_CNTL, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_GRBM_SOFT_RESET, V9X_CZ_SOFT_RESET_RLC,
           0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_GRBM_SOFT_RESET, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_load(V9X_IMAGE_RLC, V9X_CZ_RLC_GPM_UCODE_ADDR,
             V9X_CZ_RLC_GPM_UCODE_DATA);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_RLC_CNTL, 0ul, V9X_CZ_RLC_ENABLE);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_at_rlc_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_RLC_CNTL, 0ul, 0ul);
    v9x_at_rlc_stat = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_RLC_STAT, 0ul, 0ul);
    v9x_at_rlc_gpm = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_RLC_GPM_STAT, 0ul, 0ul);

    /* cp_gfx_load_microcode: halt, then PFP, CE, ME. */
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_CP_ME_CNTL, 0ul, V9X_CZ_CP_HALTS);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_at_me_cntl_halted = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_ME_CNTL,
                                   0ul, 0ul);
    v9x_load(V9X_IMAGE_PFP, V9X_CZ_CP_PFP_UCODE_ADDR,
             V9X_CZ_CP_PFP_UCODE_DATA);
    v9x_load(V9X_IMAGE_CE, V9X_CZ_CP_CE_UCODE_ADDR, V9X_CZ_CP_CE_UCODE_DATA);
    v9x_load(V9X_IMAGE_ME, V9X_CZ_CP_ME_RAM_WADDR, V9X_CZ_CP_ME_RAM_DATA);

    /* cp_gfx_resume's ring registers. The ring's 40-bit address >> 8: bits
     * 39:32 land in RB0_BASE 31:24, and RB0_BASE_HI takes what is above,
     * nothing. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB_WPTR_DELAY, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB_VMID, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_CNTL, rb_cntl, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_CNTL,
           rb_cntl | V9X_CZ_RB_RPTR_WR_ENA, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_WPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_RPTR_ADDR, lo + V9X_WRITEBACK,
           0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_RPTR_ADDR_HI, hi & 0xfful, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_CNTL, rb_cntl, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_BASE,
           ((lo + V9X_RING) >> 8) | (hi << 24), 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_BASE_HI, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_CP_RB_DOORBELL, V9X_CZ_DOORBELL_EN, 0ul);
    v9x_at_rb_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_RB0_CNTL, 0ul, 0ul);

    /* cp_gfx_start's registers and the unhalt; the ring was written
     * through BAR0, so HDP is flushed before the CP reads it. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_MAX_CONTEXT, V9X_CZ_MAX_CONTEXT, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_ENDIAN_SWAP, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_DEVICE_ID, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_CP_ME_CNTL, V9X_CZ_CP_HALTS, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_at_me_cntl_running = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_ME_CNTL,
                                    0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_WPTR, wptr_1, 0ul);
    v9x_at_wait_1 = v9x_op(V9X_OP_WAIT_MMIO, V9X_CZ_SCRATCH_REG0,
                           0xfffffffful, V9X_SCRATCH_TEST);
    v9x_at_rptr_1 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_RB0_RPTR, 0ul, 0ul);
    v9x_at_stat_1 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_STAT, 0ul, 0ul);
    v9x_at_grbm_1 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_GRBM_STATUS, 0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_WPTR, wptr_2, 0ul);
    v9x_at_wait_2 = v9x_op(V9X_OP_WAIT_MMIO, V9X_CZ_SCRATCH_REG0,
                           0xfffffffful, V9X_SCRATCH_PREAMBLE);
    v9x_at_rptr_2 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_RB0_RPTR, 0ul, 0ul);
    v9x_at_stat_2 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_STAT, 0ul, 0ul);
    v9x_at_grbm_2 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_GRBM_STATUS, 0ul, 0ul);
    v9x_at_scratch_2 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_SCRATCH_REG0, 0ul, 0ul);

    /* The draw: done when the end-of-pipe event has written the fence. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_CP_RB0_WPTR, wptr_3, 0ul);
    v9x_at_wait_3 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE, V9X_FENCE_VALUE,
                           V9X_FENCE_WAIT_LIMIT);
    v9x_at_rptr_3 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_RB0_RPTR, 0ul, 0ul);
    v9x_at_stat_3 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_STAT, 0ul, 0ul);
    v9x_at_grbm_3 = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_GRBM_STATUS, 0ul, 0ul);

    /* Halt the CP and stop the RLC, then put back what call 1 read,
     * CP_ME_CNTL (halted) last. */
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_CP_ME_CNTL, 0ul, V9X_CZ_CP_HALTS);
    v9x_op(V9X_OP_DELAY, V9X_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_RMW, V9X_CZ_RLC_CNTL, V9X_CZ_RLC_ENABLE, 0ul);
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_WRITE, v9x_saved_offsets[index], v9x_saved[index],
               0ul);
    }
    v9x_at_after_me_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_CP_ME_CNTL,
                                  0ul, 0ul);
    v9x_at_after_rlc_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_CZ_RLC_CNTL,
                                   0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_CZ_HDP_DEBUG0, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_WRITEBACK, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_FENCE, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_TARGET, V9X_TARGET_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_PROBE, V9X_PROBE_DWORDS, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_at_after_extra = v9x_request.count;
    for (index = 0ul; index < V9X_EXTRA_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_READ, v9x_extra_offsets[index], 0ul, 0ul);
    }

    v9x_number("Run", "Ops", v9x_request.count);
    v9x_number("Run", "RingDwords", stream.count);
    v9x_number("Run", "DataDwords", v9x_data_dwords);
    v9x_key("Run", "Wptr1", wptr_1);
    v9x_key("Run", "Wptr2", wptr_2);
    v9x_key("Run", "Wptr3", wptr_3);
    v9x_key("Run", "RbCntl", rb_cntl);
    return 1;
}

/* The target, against the rectangle: every pixel inside one value, every
 * pixel outside still the guard. The value inside is reported, not
 * judged: which byte each channel lands in is what the run shows. */
static void v9x_verify(const DWORD *target)
{
    DWORD x;
    DWORD y;
    DWORD inside_value;
    DWORD inside_other;
    DWORD outside_changed;
    DWORD pixel;

    inside_value = target[V9X_RECT_TOP * V9X_TARGET_PITCH + V9X_RECT_LEFT];
    inside_other = 0ul;
    outside_changed = 0ul;
    for (y = 0ul; y < V9X_TARGET_HEIGHT; ++y) {
        for (x = 0ul; x < V9X_TARGET_PITCH; ++x) {
            pixel = target[y * V9X_TARGET_PITCH + x];
            if (x >= V9X_RECT_LEFT && x < V9X_RECT_RIGHT &&
                y >= V9X_RECT_TOP && y < V9X_RECT_BOTTOM) {
                if (pixel != inside_value) {
                    ++inside_other;
                }
            } else if (pixel != V9X_GUARD_VALUE) {
                ++outside_changed;
            }
        }
    }
    v9x_key("Result", "InsidePixel", inside_value);
    v9x_number("Result", "InsideOther", inside_other);
    v9x_number("Result", "OutsideChanged", outside_changed);
    v9x_pass("Drew", inside_value != V9X_GUARD_VALUE && inside_other == 0ul &&
                     outside_changed == 0ul);
    for (y = 0ul; y < V9X_TARGET_HEIGHT; ++y) {
        char key[16];
        char text[V9X_TARGET_PITCH + 1u];

        lstrcpyA(key, "Row");
        v9x_decimal(key + 3, y);
        for (x = 0ul; x < V9X_TARGET_PITCH; ++x) {
            pixel = target[y * V9X_TARGET_PITCH + x];
            text[x] = pixel == V9X_GUARD_VALUE ? '.' :
                      (pixel == inside_value ? '#' : '?');
        }
        text[V9X_TARGET_PITCH] = '\0';
        v9x_text("Map", key, text);
    }
}

static int v9x_near(DWORD got, DWORD want)
{
    return (got > want ? got - want : want - got) <= V9X_GOURAUD_TOLERANCE;
}

/* The ramp: red round(255 (x + 0.5) / 64) at each pixel centre, green 80h,
 * blue 40h, alpha FFh, the CB's bytes R, G, B, A from the low one (the
 * first rectangle's order). Within +-2 per channel. */
static void v9x_verify_gouraud(const DWORD *target)
{
    DWORD x;
    DWORD y;
    DWORD pixel;
    DWORD red;
    DWORD bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    char text[V9X_TARGET_PITCH + 1u];
    char key[16];

    for (y = 0ul; y < V9X_TARGET_HEIGHT; ++y) {
        for (x = 0ul; x < V9X_TARGET_PITCH; ++x) {
            pixel = target[y * V9X_TARGET_PITCH + x];
            red = ((2ul * x + 1ul) * 255ul + V9X_TARGET_PITCH) /
                  (2ul * V9X_TARGET_PITCH);
            if (v9x_near(pixel & 0xfful, red) &&
                v9x_near((pixel >> 8) & 0xfful, 0x80ul) &&
                v9x_near((pixel >> 16) & 0xfful, 0x40ul) &&
                v9x_near(pixel >> 24, 0xfful)) {
                text[x] = '#';
            } else {
                text[x] = pixel == V9X_GUARD_VALUE ? '.' : '?';
                if (bad++ == 0ul) {
                    first_bad = y * V9X_TARGET_PITCH + x;
                }
            }
        }
        text[V9X_TARGET_PITCH] = '\0';
        lstrcpyA(key, "Row");
        v9x_decimal(key + 3, y);
        v9x_text("Map", key, text);
    }
    for (x = 0ul; x < V9X_TARGET_PITCH; x += 8ul) {
        lstrcpyA(key, "Row0X");
        v9x_decimal(key + 5, x);
        v9x_key("Gouraud", key, target[x]);
    }
    v9x_key("Gouraud", "Row0X63", target[V9X_TARGET_PITCH - 1ul]);
    v9x_number("Result", "GouraudBad", bad);
    if (bad != 0ul) {
        v9x_number("Result", "GouraudFirstBadIndex", first_bad);
        v9x_key("Result", "GouraudFirstBad", target[first_bad]);
    }
    v9x_pass("Gouraud", bad == 0ul);
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
    WritePrivateProfileStringA(0, 0, 0, V9X_CZCP_PATH);
    ExitProcess(code);
}

void __stdcall V9xCarrizo3dEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD first_extra;
    DWORD fb;
    DWORD lo;
    DWORD hi;
    const DWORD *r;
    const DWORD *probe;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_CZCP_PATH);
    v9x_text("Report", "Tool", "V9XCZ3D");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_gouraud = v9x_switch_given("/gouraud");
    v9x_text("Report", "Draw", v9x_gouraud ? "gouraud" : "rectangle");
    v9x_gouraud_vertices();

    v9x_data_dwords = 0ul;
    for (index = 0ul; index < V9X_IMAGE_COUNT; ++index) {
        if (!v9x_load_image(&v9x_images[index])) {
            v9x_finish("NO-UCODE", 6u);
        }
    }

    device = CreateFileA(V9X_CZCP_VXD, 0, 0, 0, CREATE_NEW,
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

    if ((v9x_saved[V9X_SAVED_ME_CNTL] & V9X_CZ_CP_HALTS) != V9X_CZ_CP_HALTS ||
        (v9x_saved[V9X_SAVED_RLC_CNTL] & V9X_CZ_RLC_ENABLE) != 0ul ||
        r[first_extra + V9X_EXTRA_RESET] != 0ul) {
        CloseHandle(device);
        v9x_finish("ENGINE-NOT-AS-READ", 3u);
    }
    /* FB_LOCATION's start: bits 39:32 its high byte, 31:24 its low byte;
     * a low byte the window offset would carry out of is refused. */
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
    if (!v9x_build_run(lo, hi)) {
        CloseHandle(device);
        v9x_finish("STREAM-BUILD-FAILED", 8u);
    }
    if (!v9x_dioc(device, 1)) {
        CloseHandle(device);
        v9x_number("Report", "Executed", v9x_result.executed);
        v9x_finish("RUN-REFUSED", 5u);
    }
    CloseHandle(device);
    r = v9x_result.op_results;
    v9x_number("During", "UcodeDwordsWritten", v9x_result.ucode_written);
    v9x_key("During", "SerdesCuWait", r[v9x_at_cu_busy]);
    v9x_key("During", "SerdesNonCuWait", r[v9x_at_noncu_busy]);
    v9x_key("During", "RlcCntlStarted", r[v9x_at_rlc_cntl]);
    v9x_key("During", "RlcStat", r[v9x_at_rlc_stat]);
    v9x_key("During", "RlcGpmStat", r[v9x_at_rlc_gpm]);
    v9x_key("During", "MeCntlHalted", r[v9x_at_me_cntl_halted]);
    v9x_key("During", "RbCntl", r[v9x_at_rb_cntl]);
    v9x_key("During", "MeCntlRunning", r[v9x_at_me_cntl_running]);
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
    v9x_pass("RingTest", r[v9x_at_wait_1] != V9X_CZCP_TIMEOUT);
    v9x_pass("Preamble", r[v9x_at_wait_2] != V9X_CZCP_TIMEOUT);
    v9x_key("During", "GbTileMode8", r[v9x_at_tile_mode8]);
    v9x_key("During", "GbTileMode8Written", r[v9x_at_tile_mode8_written]);
    v9x_key("During", "GbAddrConfig", r[v9x_at_addr_config]);
    v9x_number("During", "InitEntries", r[v9x_at_init]);
    v9x_key("During", "GbAddrConfigWritten", r[v9x_at_addr_config_after]);
    v9x_key("During", "PaScFifoSize", r[v9x_at_fifo_before]);
    v9x_key("During", "PaScFifoSizeWritten", r[v9x_at_fifo_after]);
    v9x_key("During", "ShMemConfig", r[v9x_at_sh_mem_before]);
    v9x_key("During", "ShMemConfigWritten", r[v9x_at_sh_mem_after]);
    v9x_key("During", "Wait3", r[v9x_at_wait_3]);
    v9x_key("During", "Rptr3", r[v9x_at_rptr_3]);
    v9x_key("During", "CpStat3", r[v9x_at_stat_3]);
    v9x_key("During", "GrbmStatus3", r[v9x_at_grbm_3]);
    v9x_key("Result", "Fence", v9x_result.block[1]);
    v9x_pass("DrawFence", r[v9x_at_wait_3] != V9X_CZCP_TIMEOUT);
    /* The markers: the guard where a stage never stored. Vertex n's dword
     * reads n if a VS invocation had VertexID n. */
    probe = v9x_result.block + 2ul + V9X_TARGET_DWORDS;
    for (index = 0ul; index < V9X_PROBE_DWORDS; ++index) {
        char key[16];

        lstrcpyA(key, "Probe");
        v9x_decimal(key + 5, index);
        v9x_key("Result", key, probe[index]);
    }
    v9x_pass("VsRan", probe[V9X_PROBE_AT_VS] != V9X_GUARD_VALUE);
    v9x_pass("VertexIds", probe[V9X_PROBE_AT_VERTEX] == 0ul &&
                          probe[V9X_PROBE_AT_VERTEX + 1u] == 1ul &&
                          probe[V9X_PROBE_AT_VERTEX + 2u] == 2ul);
    /* The RECTLIST's corners: (x1, y1), (x2, y1), (x1, y2). */
    v9x_pass("Positions",
             probe[V9X_PROBE_AT_X] == 0x41000000ul &&
             probe[V9X_PROBE_AT_X + 1u] == 0x42200000ul &&
             probe[V9X_PROBE_AT_X + 2u] == 0x41000000ul &&
             probe[V9X_PROBE_AT_Y] == 0x40800000ul &&
             probe[V9X_PROBE_AT_Y + 1u] == 0x40800000ul &&
             probe[V9X_PROBE_AT_Y + 2u] == 0x41400000ul);
    v9x_pass("PsRan", probe[V9X_PROBE_AT_PS] != V9X_GUARD_VALUE);
    if (v9x_gouraud) {
        v9x_verify_gouraud(v9x_result.block + 2);
    } else {
        v9x_verify(v9x_result.block + 2);
    }
    v9x_finish("COMPLETE", 0u);
}
