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
 * /gpuinit runs si_init_golden_registers and gfx_v6_0_constants_init for
 * Oland first, as Linux runs them before rlc_resume: v9x_si_gpu_init_ops's
 * list (si_gpu_init.c and its generated tables) as one table op, inside
 * SICP.VXD's graphics windows. After the preamble the probe reads back
 * GB_ADDR_CONFIG, the tile modes, the raster configuration and the other
 * plain writes it can read, and counts those that differ from the list.
 * Those registers are configuration, not state the driver holds, and are
 * not put back. It also reports CGTS_TCC_DISABLE and its user copy, which
 * decide whether setup_tcc (left out of the list) would write anything.
 * Without /gpuinit nothing of this runs.
 *
 * /rect (which implies /gpuinit) is the first draw, V9XCK3D's rectangle
 * for SI: the second submission is the whole of cp_gfx_start's second
 * half with si_cs_data (si_3d.c) instead of the bare preamble, and a third
 * draws a solid rectangle (8,4)-(40,12) into a 64x16 linear-aligned 32 bpp
 * target (COLOR_8_8_8_8, tile index 8) with vi_3d.c's builder in its GFX6
 * mode and vi_shader.c's solid shaders rewritten for SI by
 * v9x_si_shader_from_vi, then SI's fence. The probe waits for the fence,
 * reads the target back and checks every pixel: one value inside the
 * rectangle, the guard everywhere else. Colour R 1.0, G 0.5, B 0.25,
 * A 0.75, four different bytes, so the order shows. No probe markers:
 * those shaders store through FLAT, which SI does not have.
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
#include "velocity9x/si_gpu_init.h"
#include "velocity9x/si_3d.h"
#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_shader.h"
#include "velocity9x/cik_shader.h"
#include "velocity9x/si_shader.h"
#include "velocity9x/ps11.h"
#include "velocity9x/vi_ps11.h"

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
#define V9X_OP_WAIT_VRAM    7ul
#define V9X_OP_MMIO_TABLE   16ul

/* v9x_si_gpu_init_ops's list, and what /gpuinit reads back afterwards. */
#define V9X_GPU_OPS_MAX     256u
#define V9X_SI_TILE_MODE0   0x09910ul
#define V9X_SI_GRBM_CNTL    0x08000ul
#define V9X_SI_HDP_ADDR_CONFIG      0x02f48ul
#define V9X_SI_DMA0_TILING_CONFIG   0x0d0b8ul
#define V9X_SI_PA_SC_RASTER_CONFIG  0x28350ul
#define V9X_SI_PA_SC_FIFO_SIZE      0x08bccul
#define V9X_SI_SPI_THREAD_MGMT_3    0x090e8ul
#define V9X_SI_CGTS_TCC_DISABLE     0x09148ul
#define V9X_SI_CGTS_USER_TCC        0x0914cul

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
 * CNTX_EMPTY, bits 19 and 20. RLC_STAT bits 0-3 as sid.h names them
 * (RLC_BUSY, GFX_POWER, GFX_CLOCK, GFX_LS), which is what
 * enable_gui_idle_interrupt(false) waits on: masked Fh, equal to 6.
 * gfx_6_0_sh_mask.h names bits 1 and 2 RLC_GPM_BUSY and RLC_SPM_BUSY
 * instead; which is right is not settled. Measured on W9QI0DAD: 6 after
 * the RLC's reset, 7 once it runs, and 7 until the next reset, so the
 * wait times out on every run after the first, harmlessly.
 * CP_RB0_CNTL RB_BUFSZ 5:0, RB_BLKSZ 13:8, RB_RPTR_WR_ENA 31. */
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
#define V9X_RING_MAX_DWORDS     4096ul
/* /rect's layout, V9XCK3D's: shaders, fence and target in the window. */
#define V9X_VS                  0x4000ul
#define V9X_PS                  0x4100ul
#define V9X_FENCE               0x8100ul
#define V9X_TARGET              0x9000ul
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
#define V9X_SHADER_MAX          128ul
/* /gouraud, V9XCK3D's: two triangles of TL vertices over the whole
 * target, red 0 at the left edge to FFh at the right, green 80h, blue 40h,
 * alpha FFh; every pixel within +-2 per channel of the ramp at its
 * centre. */
#define V9X_VERTICES            0xb000ul
#define V9X_TL_DWORDS           8ul
#define V9X_GOURAUD_VERTICES    6ul
#define V9X_GOURAUD_LEFT        0xff008040ul
#define V9X_GOURAUD_RIGHT       0xffff8040ul
#define V9X_GOURAUD_TOLERANCE   2ul
#define V9X_FLOAT_ONE           0x3f800000ul
#define V9X_FLOAT_HALF          0x3f000000ul
#define V9X_FLOAT_16            0x41800000ul
#define V9X_FLOAT_64            0x42800000ul
#define V9X_FLOAT_32            0x42000000ul
#define V9X_FLOAT_QUARTER       0x3e800000ul
#define V9X_FLOAT_THREE_QUARTERS 0x3f400000ul
/* /depth, V9XCK3D's: a Z16 buffer the target's size cleared to 1.0, then
 * three rectangles in one draw, LESS with writes: red over the whole
 * target at z 0.5, green over the left half at 0.75 (behind it), blue
 * over the right half at 0.25 (in front). Red left, blue right; the Z
 * buffer 8000h in the left half's tiles, 4000h in the right's. The
 * buffer is GFX6's 1D depth tile mode (4 on Oland): 8x8 tiles of 128
 * bytes in rows, so whole tiles are checked and the order inside one
 * does not matter. */
#define V9X_DEPTH               0xd000ul
#define V9X_DEPTH_DWORDS        (V9X_TARGET_PITCH * V9X_TARGET_HEIGHT / 2ul)
#define V9X_DEPTH_CLEAR         0xfffffffful
#define V9X_DEPTH_VERTICES      18ul
#define V9X_DEPTH_RED           0xffff0000ul
#define V9X_DEPTH_GREEN         0xff00ff00ul
#define V9X_DEPTH_BLUE          0xff0000fful
#define V9X_Z16_HALF            0x8000ul
#define V9X_Z16_QUARTER         0x4000ul
#define V9X_Z16_TILE_DWORDS     32ul
/* /texture and /modulate, V9XCK3D's: a 64x16 A8R8G8B8 texture, linear
 * aligned (tile index 8, linear-aligned on Oland too), the VxD's pattern
 * (texel n = seed + n x 9E3779B9h), mapped 1:1 onto the target with point
 * sampling. /texture exports the texel; /modulate multiplies it by a
 * D3DCOLOR of R FFh, G 80h, B 40h, A FFh. */
#define V9X_TEXTURE             0xe000ul
#define V9X_TEXTURE_DWORDS      (V9X_TARGET_PITCH * V9X_TARGET_HEIGHT)
#define V9X_SEED_TEXTURE        0x7e57ab1eul
#define V9X_WHITE               0xfffffffful
#define V9X_MODULATE_COLOUR     0xffff8040ul
#define V9X_TL_MAX_VERTICES     18ul
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

/* /gpuinit's read-back: the plain writes the probe can read, then the
 * fuse registers setup_tcc would read. The tile modes follow these. */
static const DWORD v9x_check_offsets[] = {
    V9X_SI_GRBM_CNTL, V9X_SI_GB_ADDR_CONFIG, V9X_SI_HDP_ADDR_CONFIG,
    V9X_SI_DMA0_TILING_CONFIG, V9X_SI_PA_SC_RASTER_CONFIG,
    V9X_SI_PA_SC_FIFO_SIZE, V9X_SI_SPI_THREAD_MGMT_3,
    V9X_SI_CGTS_TCC_DISABLE, V9X_SI_CGTS_USER_TCC
};
#define V9X_CHECK_COUNT (sizeof(v9x_check_offsets) / sizeof(v9x_check_offsets[0]))

static const char * const v9x_check_names[V9X_CHECK_COUNT] = {
    "GRBM_CNTL", "GB_ADDR_CONFIG", "HDP_ADDR_CONFIG", "DMA0_TILING_CONFIG",
    "PA_SC_RASTER_CONFIG", "PA_SC_FIFO_SIZE", "SPI_STATIC_THREAD_MGMT_3",
    "CGTS_TCC_DISABLE", "CGTS_USER_TCC_DISABLE"
};

static struct v9x_vi_reg_seq v9x_gpu_ops[V9X_GPU_OPS_MAX];
static DWORD v9x_gpu_count;
static DWORD v9x_at_check;
static DWORD v9x_at_early_addr;
static DWORD v9x_at_reset_stat;
static DWORD v9x_at_rlc_addr;
static DWORD v9x_at_wait_3;
static DWORD v9x_at_rptr_3;
static DWORD v9x_at_grbm_3;
static DWORD v9x_rect_status;
static DWORD v9x_shader_status;
static int v9x_gouraud;
static int v9x_texture;
static int v9x_modulate;
static int v9x_depth;
static DWORD v9x_vertex_count;
static DWORD v9x_vertices[V9X_TL_MAX_VERTICES * V9X_TL_DWORDS];

/* R 1.0, G 0.5, B 0.25, A 0.75, as V9XCK3D. */
static const DWORD v9x_colour[4] = {
    0x3f800000ul, 0x3f000000ul, 0x3e800000ul, 0x3f400000ul
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

/* The value the list leaves at offset if its last entry there is a plain
 * write; 0 with *known clear otherwise. */
static DWORD v9x_expected(DWORD offset, int *known)
{
    DWORD index;
    DWORD value = 0ul;

    *known = 0;
    for (index = 0ul; index < v9x_gpu_count; ++index) {
        if (v9x_gpu_ops[index].offset == offset) {
            *known = v9x_gpu_ops[index].clear == 0xfffffffful;
            value = v9x_gpu_ops[index].set;
        }
    }
    return value;
}

/* Call 3's op list. lo/hi are the scratch window's GPU address. */
/* A builder's packets on the end of the ring buffer. */
static void v9x_ring_stream_begin(struct v9x_vi_pm4 *stream)
{
    v9x_vi_pm4_init(stream, v9x_ring + v9x_ring_count,
                    V9X_RING_MAX_DWORDS - v9x_ring_count);
    stream->gfx6 = 1ul;
}

static void v9x_ring_stream_end(const struct v9x_vi_pm4 *stream)
{
    v9x_ring_count += stream->count;
    if (stream->overflow) {
        v9x_ring_count = V9X_RING_MAX_DWORDS + 1ul;
    }
}

/* The solid rectangle's submission: vi_3d.c in its GFX6 mode. */
static void v9x_ring_rect(DWORD lo, DWORD hi)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_solid_rect rect;
    BYTE *bytes = (BYTE *)&rect;
    DWORD index;

    for (index = 0ul; index < sizeof(rect); ++index) {
        bytes[index] = 0u;
    }
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
    v9x_ring_stream_begin(&stream);
    v9x_rect_status = (DWORD)v9x_vi_3d_solid_rect(&stream, &rect);
    v9x_ring_stream_end(&stream);
}

static void v9x_tl_vertex(DWORD *out, DWORD x, DWORD y, DWORD z,
                          DWORD colour, DWORD u, DWORD v)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = V9X_FLOAT_ONE;
    out[4] = colour;
    out[5] = 0ul;
    out[6] = u;
    out[7] = v;
}

/* The target as two triangles, V9XCK3D's order: top-left, top-right,
 * bottom-left, then top-right, bottom-right, bottom-left. */
/* A rectangle as two triangles; the left vertices take left, the right
 * ones right; (u, v) runs (0, 0) to (1, 1) corner to corner. */
static void v9x_tl_rect(DWORD *out, DWORD x0, DWORD y0, DWORD x1, DWORD y1,
                        DWORD z, DWORD left, DWORD right)
{
    v9x_tl_vertex(out + 0, x0, y0, z, left, 0ul, 0ul);
    v9x_tl_vertex(out + 8, x1, y0, z, right, V9X_FLOAT_ONE, 0ul);
    v9x_tl_vertex(out + 16, x0, y1, z, left, 0ul, V9X_FLOAT_ONE);
    v9x_tl_vertex(out + 24, x1, y0, z, right, V9X_FLOAT_ONE, 0ul);
    v9x_tl_vertex(out + 32, x1, y1, z, right, V9X_FLOAT_ONE, V9X_FLOAT_ONE);
    v9x_tl_vertex(out + 40, x0, y1, z, left, 0ul, V9X_FLOAT_ONE);
}

/* The TL draws' vertices, V9XCK3D's; sets v9x_vertex_count. */
static void v9x_tl_vertices(DWORD *out)
{
    DWORD rect = V9X_GOURAUD_VERTICES * V9X_TL_DWORDS;
    DWORD colour;

    if (v9x_depth) {
        v9x_tl_rect(out, 0ul, 0ul, V9X_FLOAT_64, V9X_FLOAT_16,
                    V9X_FLOAT_HALF, V9X_DEPTH_RED, V9X_DEPTH_RED);
        v9x_tl_rect(out + rect, 0ul, 0ul, V9X_FLOAT_32, V9X_FLOAT_16,
                    V9X_FLOAT_THREE_QUARTERS, V9X_DEPTH_GREEN,
                    V9X_DEPTH_GREEN);
        v9x_tl_rect(out + 2ul * rect, V9X_FLOAT_32, 0ul, V9X_FLOAT_64,
                    V9X_FLOAT_16, V9X_FLOAT_QUARTER, V9X_DEPTH_BLUE,
                    V9X_DEPTH_BLUE);
        v9x_vertex_count = V9X_DEPTH_VERTICES;
        return;
    }
    if (v9x_texture) {
        colour = v9x_modulate ? V9X_MODULATE_COLOUR : V9X_WHITE;
        v9x_tl_rect(out, 0ul, 0ul, V9X_FLOAT_64, V9X_FLOAT_16,
                    V9X_FLOAT_HALF, colour, colour);
        v9x_vertex_count = V9X_GOURAUD_VERTICES;
        return;
    }
    v9x_tl_rect(out, 0ul, 0ul, V9X_FLOAT_64, V9X_FLOAT_16, V9X_FLOAT_HALF,
                V9X_GOURAUD_LEFT, V9X_GOURAUD_RIGHT);
    v9x_vertex_count = V9X_GOURAUD_VERTICES;
}

/* The Gouraud submission: vi_3d.c's TL draw in its GFX6 mode, whose VS
 * user SGPRs are a V# over the vertices. */
static void v9x_ring_gouraud(DWORD lo, DWORD hi)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    BYTE *bytes = (BYTE *)&draw;
    DWORD index;

    for (index = 0ul; index < sizeof(draw); ++index) {
        bytes[index] = 0u;
    }
    draw.target_low = lo + V9X_TARGET;
    draw.target_high = hi;
    draw.pitch_pixels = V9X_TARGET_PITCH;
    draw.height = V9X_TARGET_HEIGHT;
    draw.format = V9X_VI_CB_FORMAT_8_8_8_8;
    draw.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw.vs_low = lo + V9X_VS;
    draw.ps_low = lo + V9X_PS;
    draw.shader_high = hi;
    v9x_tl_vertices(v9x_vertices);
    draw.vertex_low = lo + V9X_VERTICES;
    draw.vertex_high = hi;
    draw.vertex_count = v9x_vertex_count;
    draw.fence_low = lo + V9X_FENCE;
    draw.fence_high = hi;
    draw.fence_value = V9X_FENCE_VALUE;
    /* /depth: Z16 in GFX6's 1D depth tile mode, LESS with writes. */
    draw.depth_low = v9x_depth ? lo + V9X_DEPTH : 0ul;
    draw.depth_high = hi;
    draw.depth_func = V9X_VI_ZFUNC_LESS;
    draw.depth_write = 1ul;
    /* /texture, /modulate: the pattern texture 1:1, point-sampled. */
    draw.texture_low = v9x_texture ? lo + V9X_TEXTURE : 0ul;
    draw.texture_high = hi;
    draw.texture_width = V9X_TARGET_PITCH;
    draw.texture_height = V9X_TARGET_HEIGHT;
    draw.texture_pitch = V9X_TARGET_PITCH;
    draw.texture_tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw.texture_bilinear = 0ul;
    if (v9x_texture) {
        draw.ps_vgprs = v9x_modulate ? V9X_VI_MODULATE_PS_VGPRS :
                                       V9X_VI_TEXTURE_PS_VGPRS;
    }
    v9x_ring_stream_begin(&stream);
    v9x_rect_status = (DWORD)v9x_vi_3d_tl_draw(&stream, &draw);
    v9x_ring_stream_end(&stream);
}

/* SI's TL VS (si_shader.c) into the data array. */
static DWORD v9x_tl_vs_data(DWORD *count)
{
    DWORD words[V9X_SI_TL_VS_MAX];
    DWORD first = v9x_data_dwords;
    DWORD index;

    *count = 0ul;
    if ((DWORD)v9x_si_shader_tl_vs(words, count) != 0ul) {
        v9x_shader_status = 0x40000000ul;
        *count = 0ul;
    }
    for (index = 0ul; index < *count &&
                      v9x_data_dwords < V9X_SICP_DATA_MAX; ++index) {
        v9x_data[v9x_data_dwords++] = words[index];
    }
    return first;
}

/* A vi_shader.c program, rewritten for SI, into the data array; returns
 * its first data dword, its length in *count. */
static DWORD v9x_shader_data(DWORD (*build)(DWORD *), DWORD *count)
{
    DWORD words[V9X_SHADER_MAX];
    DWORD first = v9x_data_dwords;
    DWORD failed = 0ul;
    DWORD index;

    *count = build(words);
    if ((DWORD)v9x_si_shader_from_vi(words, *count, &failed) != 0ul) {
        v9x_shader_status = 0x80000000ul | failed;
    }
    for (index = 0ul; index < *count &&
                      v9x_data_dwords < V9X_SICP_DATA_MAX; ++index) {
        v9x_data[v9x_data_dwords++] = words[index];
    }
    return first;
}

/*
 * /ps11:N: step 3 of docs\plans\ati-gcn-ps11.md. Pixel shader 1.1 program
 * N, translated by v9x_vi_ps11_build (vi_ps11.c) and rewritten for SI,
 * drawn as one rectangle and every pixel judged against the reference
 * interpreter (d3d_ps11_ref.c, linked here) given the same inputs and
 * nearest samples of the same texels: V9XN5CH /3d:ps11's check on the
 * GT218, with its programs, planes and texels.
 *
 * Programs: 0 Arith (every arithmetic instruction and source modifier,
 * shifts, saturation, the [-1, 1] hold, co-issue, cnd and a def), 1
 * FourTex (four textures, descriptors and samplers), 2 Kill (texkill over
 * part of the rectangle), 3 Bem (a dependent read through stage 1's bump
 * matrix), 4 Cnd (a select either way), 5 Fog (two constants in one
 * instruction, which the translator moves off the constant bus, and fog
 * after the shader).
 *
 * The draw: si_shader.h's wide VS over six 112-byte wide vertices (two
 * triangles, (8, 4)-(56, 44), w 1), the six attributes planes over the
 * rectangle; the PS's two user SGPRs the table's address (vi_ps11.h),
 * its T#s for four 8x8 A8R8G8B8 textures, pitch 64, nearest, clamped.
 * /bpp16 draws into R5G6B5, else A8R8G8B8, 64x48, the guard everywhere
 * first. Channels are in their PS 1.1 places, as radeonsi draws a BGRA
 * format: the T#s' DST_SEL Z Y X W puts a texel's red in x, and
 * CB_COLOR0_INFO COMP_SWAP puts x in red, SWAP_ALT for 8888 and
 * SWAP_STD_REV for 565 (r600_translate_colorswap). That convention is
 * measured here too: a wrong swap shows as channels exchanged.
 *
 * A judged pixel must be within two steps a channel at 8 bits (alpha too)
 * and one at 5 or 6; a sample within V9X_PS11_MARGIN of a texel edge, or
 * a kill coordinate within V9X_PS11_KILL_MARGIN of zero, leaves the pixel
 * unjudged; a killed pixel must still be the guard; outside the rectangle,
 * the guard.
 */
#define V9X_PS11_VARIANTS     6ul
#define V9X_PS11_TABLE        0x8200ul
#define V9X_PS11_VERTICES     0x8400ul
#define V9X_PS11_TEXTURES     0x8800ul
#define V9X_PS11_TEX_STEP     0x0800ul
#define V9X_PS11_TARGET       0xb000ul
#define V9X_PS11_PITCH        64ul
#define V9X_PS11_HEIGHT       48ul
#define V9X_PS11_X0           8ul
#define V9X_PS11_Y0           4ul
#define V9X_PS11_X1           56ul
#define V9X_PS11_Y1           44ul
#define V9X_PS11_SIDE         8ul
#define V9X_PS11_TEX_PITCH    64ul
#define V9X_PS11_STAGES       4ul
#define V9X_PS11_ATTRIBUTES   6ul
#define V9X_PS11_VERTEX_COUNT 6ul
#define V9X_PS11_VERTEX_FLOATS 28ul
#define V9X_PS11_CODE_MAX     1024ul
#define V9X_PS11_MARGIN       0.05f
#define V9X_PS11_KILL_MARGIN  0.01f
#define V9X_PS11_GUARD16      0x5a5aul
#define V9X_PS11_FOG_PROGRAM  5ul
/* T# word 3 DST_SEL Z, Y, X, W (6, 5, 4, 7 at 2:0, 5:3, 8:6, 11:9),
 * TILE_INDEX 8 (linear aligned) at 24:20, TYPE 2D (9) at 31:28; word 1
 * DATA_FORMAT 8_8_8_8 (0Ah) at 25:20, NUM_FORMAT UNORM. S# word 0
 * CLAMP_X/Y/Z CLAMP_LAST_TEXEL (2). Mesa's sid.h, as vi_3d.c's. */
#define V9X_PS11_T1_8888      (0x0aul << 20)
#define V9X_PS11_T3_BGRA      (6ul | (5ul << 3) | (4ul << 6) | (7ul << 9) | \
                               (8ul << 20) | (9ul << 28))
#define V9X_PS11_S0_CLAMP     (2ul | (2ul << 3) | (2ul << 6))

/* Shader tokens, as test_d3d_ps11.c builds them. */
#define V9X_PT_VER(minor)    (0xffff0100ul | (minor))
#define V9X_PT_END           0x0000fffful
#define V9X_PT_COISSUE       0x40000000ul
#define V9X_PT_DST(file, reg, mask, mod, shift) \
    (0x80000000ul | ((DWORD)(file) << 28) | ((DWORD)(shift) << 24) | \
     ((DWORD)(mod) << 20) | ((DWORD)(mask) << 16) | (DWORD)(reg))
#define V9X_PT_SRC(file, reg, swizzle, mod) \
    (0x80000000ul | ((DWORD)(file) << 28) | ((DWORD)(mod) << 24) | \
     ((DWORD)(swizzle) << 16) | (DWORD)(reg))
#define V9X_PT_R  0ul
#define V9X_PT_V  1ul
#define V9X_PT_C  2ul
#define V9X_PT_T  3ul
#define V9X_PT_XYZW 0xe4ul
#define V9X_PT_AAAA 0xfful
#define V9X_PT_OP_MOV  1ul
#define V9X_PT_OP_ADD  2ul
#define V9X_PT_OP_SUB  3ul
#define V9X_PT_OP_MAD  4ul
#define V9X_PT_OP_MUL  5ul
#define V9X_PT_OP_DP3  8ul
#define V9X_PT_OP_LRP  18ul
#define V9X_PT_OP_CND  80ul
#define V9X_PT_OP_DEF  81ul
#define V9X_PT_OP_TEXKILL 65ul
#define V9X_PT_OP_TEX     66ul
#define V9X_PT_OP_TEXBEM  67ul
#define V9X_PT_TO_R(n, mask) V9X_PT_DST(V9X_PT_R, n, mask, 0ul, 0ul)
#define V9X_PT_TO_T(n)       V9X_PT_DST(V9X_PT_T, n, 0xful, 0ul, 0ul)
#define V9X_PT_SRC_PLAIN(file, n) V9X_PT_SRC(file, n, V9X_PT_XYZW, 0ul)

struct v9x_ps11_probe_variant {
    const char *name;
    DWORD tokens[48];
};

static const struct v9x_ps11_probe_variant
    v9x_ps11_probe_variants[V9X_PS11_VARIANTS] = {
    { "Arith", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_DEF, V9X_PT_DST(V9X_PT_C, 1ul, 0xful, 0ul, 0ul),
        0x3f400000ul, 0xbf000000ul, 0x3e800000ul, 0x3fc00000ul,
        V9X_PT_OP_MOV, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_V, 0ul),
        V9X_PT_OP_MAD, V9X_PT_DST(V9X_PT_R, 1ul, 0xful, 1ul, 1ul),
        V9X_PT_SRC(V9X_PT_C, 1ul, V9X_PT_XYZW, 4ul),
        V9X_PT_SRC(V9X_PT_V, 1ul, V9X_PT_XYZW, 6ul),
        V9X_PT_SRC(V9X_PT_R, 0ul, V9X_PT_AAAA, 1ul),
        V9X_PT_OP_DP3, V9X_PT_DST(V9X_PT_R, 0ul, 0x7ul, 0ul, 15ul),
        V9X_PT_SRC(V9X_PT_R, 1ul, V9X_PT_XYZW, 2ul),
        V9X_PT_SRC(V9X_PT_C, 2ul, V9X_PT_XYZW, 3ul),
        V9X_PT_OP_LRP | V9X_PT_COISSUE, V9X_PT_TO_R(0ul, 0x8ul),
        V9X_PT_SRC_PLAIN(V9X_PT_V, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_R, 1ul),
        V9X_PT_SRC_PLAIN(V9X_PT_C, 2ul),
        V9X_PT_OP_CND, V9X_PT_TO_R(1ul, 0xful),
        V9X_PT_SRC(V9X_PT_R, 0ul, V9X_PT_AAAA, 0ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul),
        V9X_PT_SRC(V9X_PT_V, 1ul, V9X_PT_XYZW, 5ul),
        V9X_PT_OP_ADD, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 1ul),
        V9X_PT_SRC(V9X_PT_C, 3ul, V9X_PT_XYZW, 1ul),
        V9X_PT_OP_SUB, V9X_PT_TO_R(0ul, 0x7ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_C, 1ul),
        V9X_PT_OP_MUL, V9X_PT_DST(V9X_PT_R, 0ul, 0xful, 0ul, 2ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_V, 0ul),
        V9X_PT_END } },
    /* tex t0-t3; mul r1, t0, t1; mul r0, t2, t3; lrp r0, c0, r1, r0. */
    { "FourTex", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(0ul), V9X_PT_OP_TEX, V9X_PT_TO_T(1ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(2ul), V9X_PT_OP_TEX, V9X_PT_TO_T(3ul),
        V9X_PT_OP_MUL, V9X_PT_TO_R(1ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_T, 1ul),
        V9X_PT_OP_MUL, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 2ul), V9X_PT_SRC_PLAIN(V9X_PT_T, 3ul),
        V9X_PT_OP_LRP, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_C, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_R, 1ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul),
        V9X_PT_END } },
    /* tex t0; texkill t1; mov r0, t0. */
    { "Kill", {
        V9X_PT_VER(0ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(0ul), V9X_PT_OP_TEXKILL, V9X_PT_TO_T(1ul),
        V9X_PT_OP_MOV, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 0ul),
        V9X_PT_END } },
    /* tex t0; texbem t1, t0; mov r0, t1. */
    { "Bem", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(0ul),
        V9X_PT_OP_TEXBEM, V9X_PT_TO_T(1ul), V9X_PT_SRC_PLAIN(V9X_PT_T, 0ul),
        V9X_PT_OP_MOV, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 1ul),
        V9X_PT_END } },
    /* add r0.a, v0.a, v1.a; cnd r0, r0.a, c0, v1. */
    { "Cnd", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_ADD, V9X_PT_TO_R(0ul, 0x8ul),
        V9X_PT_SRC(V9X_PT_V, 0ul, V9X_PT_AAAA, 0ul),
        V9X_PT_SRC(V9X_PT_V, 1ul, V9X_PT_AAAA, 0ul),
        V9X_PT_OP_CND, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC(V9X_PT_R, 0ul, V9X_PT_AAAA, 0ul),
        V9X_PT_SRC_PLAIN(V9X_PT_C, 0ul), V9X_PT_SRC_PLAIN(V9X_PT_V, 1ul),
        V9X_PT_END } },
    /* tex t0; mad r0, c2, c5, t0; add r0.rgb, r0, c5_bx2; fogged. */
    { "Fog", {
        V9X_PT_VER(1ul),
        V9X_PT_OP_TEX, V9X_PT_TO_T(0ul),
        V9X_PT_OP_MAD, V9X_PT_TO_R(0ul, 0xful),
        V9X_PT_SRC_PLAIN(V9X_PT_C, 2ul), V9X_PT_SRC_PLAIN(V9X_PT_C, 5ul),
        V9X_PT_SRC_PLAIN(V9X_PT_T, 0ul),
        V9X_PT_OP_ADD, V9X_PT_TO_R(0ul, 0x7ul),
        V9X_PT_SRC_PLAIN(V9X_PT_R, 0ul),
        V9X_PT_SRC(V9X_PT_C, 5ul, V9X_PT_XYZW, 4ul),
        V9X_PT_END } }
};

/* The fog colour r g b for program 5. */
static const float v9x_ps11_fog_rgb[3] = { 0.25f, 0.5f, 1.0f };

static DWORD v9x_ps11_variant = 0xfffffffful;
static int v9x_ps11_bpp16;
static V9X_PS11_PROGRAM v9x_ps11_program;
static V9X_PS11_ENV v9x_ps11_env;
static struct v9x_vi_ps11_info v9x_ps11_info;
static DWORD v9x_ps11_code[V9X_PS11_CODE_MAX];
static DWORD v9x_ps11_table[V9X_VI_PS11_TABLE_DWORDS];
static DWORD v9x_ps11_vertices[V9X_PS11_VERTEX_COUNT * V9X_PS11_VERTEX_FLOATS];
static DWORD v9x_ps11_build_status;

/* The target in dwords: 64x48 at two or four bytes a pixel. */
static DWORD v9x_ps11_target_dwords(void)
{
    return V9X_PS11_PITCH * V9X_PS11_HEIGHT / (v9x_ps11_bpp16 ? 2ul : 1ul);
}

#ifdef __WATCOMC__
/* Float to long by fistp, rounding to nearest: a cast would call
 * Watcom's __CHP, from the C runtime the probe does not link (the GT218
 * and HD 4600 probes' helper). */
static long v9x_ps11_long(float value);
#pragma aux v9x_ps11_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_ps11_long(float value)
{
    return (long)(value < 0.0f ? value - 0.5f : value + 0.5f);
}
#endif

static DWORD v9x_ps11_bits(float value)
{
    union {
        float f;
        DWORD u;
    } convert;

    convert.f = value;
    return convert.u;
}

/* Stage N's texel (x, y), A8R8G8B8: the GT218 probe's. */
static DWORD v9x_ps11_texel(DWORD stage, DWORD x, DWORD y)
{
    DWORD a;
    DWORD r;
    DWORD g;
    DWORD b;

    if (stage == 0ul) {
        a = 0xfful - x * 8ul;
        r = 0x58ul + x * 12ul;
        g = 0x58ul + y * 12ul;
        b = 0xe0ul - (x + y) * 4ul;
    } else if (stage == 1ul) {
        a = 0x90ul + y * 8ul;
        r = x * 32ul + 16ul;
        g = 0xfful - y * 32ul;
        b = 0x40ul + ((x + y) & 1ul) * 0x80ul;
    } else if (stage == 2ul) {
        a = 0x70ul + x * 16ul;
        r = 0x20ul + y * 28ul;
        g = 0x30ul + x * 20ul;
        b = 0xc0ul - y * 16ul;
    } else {
        a = 0xd0ul - (x + y) * 4ul;
        r = 0xf0ul - x * 16ul;
        g = 0x10ul + x * y * 3ul;
        b = y * 32ul + 8ul;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Attribute a's component c as a plane over the rectangle (the GT218
 * probe's): colours in [0, 1], coordinate x and y a little past [0, 1],
 * set 1's x from below zero so Kill kills part of it, z positive, w 1. */
static float v9x_ps11_plane(DWORD a, DWORD c, float fx, float fy)
{
    float p;
    float q;
    float r;

    if (a < 2ul) {
        p = 0.05f + 0.1f * (float)(long)c + 0.05f * (float)(long)a;
        q = 0.4f - 0.05f * (float)(long)c;
        r = 0.2f + 0.1f * (float)(long)a;
    } else if (c == 0ul) {
        p = a == 3ul ? -0.3f : -0.05f + 0.02f * (float)(long)a;
        q = 1.15f;
        r = 0.1f;
    } else if (c == 1ul) {
        p = -0.04f;
        q = 0.08f;
        r = 1.1f - 0.03f * (float)(long)a;
    } else if (c == 2ul) {
        p = 0.3f;
        q = -0.2f;
        r = 0.25f;
    } else {
        return 1.0f;
    }
    return p + q * fx + r * fy;
}

static void v9x_ps11_make_env(DWORD variant, V9X_PS11_ENV *env)
{
    unsigned char *bytes = (unsigned char *)env;
    DWORD k;

    for (k = 0ul; k < sizeof(*env); ++k) {
        bytes[k] = 0u;
    }
    for (k = 0ul; k < 8ul; ++k) {
        env->c[k][0] = 0.1f * (float)(long)k - 0.3f;
        env->c[k][1] = 1.5f - 0.2f * (float)(long)k;
        env->c[k][2] = -1.25f + 0.25f * (float)(long)k;
        env->c[k][3] = 0.45f;
    }
    if (variant == 1ul) {
        env->c[0][0] = 0.25f;
        env->c[0][1] = 0.5f;
        env->c[0][2] = 0.75f;
        env->c[0][3] = 0.6f;
    }
    env->bump[1][0] = 0.25f;
    env->bump[1][1] = -0.1f;
    env->bump[1][2] = 0.05f;
    env->bump[1][3] = 0.3f;
}

/* The program, its table and its vertices; v9x_ps11_build_status 0 when
 * all is built, else what stopped it (parse 1xxh, translation 2xxh, the
 * SI rewrite 3xxh). */
static void v9x_ps11_prepare(DWORD lo, DWORD hi)
{
    const struct v9x_ps11_probe_variant *v =
        &v9x_ps11_probe_variants[v9x_ps11_variant];
    struct v9x_vi_ps11_desc desc;
    DWORD dwords = 0ul;
    DWORD where = 0ul;
    DWORD stage;
    DWORD index;
    DWORD a;
    DWORD c;
    v9x_status status;

    while (v->tokens[dwords] != V9X_PT_END) {
        ++dwords;
    }
    ++dwords;
    if (v9x_ps11_parse(v->tokens, dwords, &v9x_ps11_program, &where) !=
        V9X_PS11_OK) {
        v9x_ps11_build_status = 0x100ul | where;
        return;
    }
    v9x_ps11_make_env(v9x_ps11_variant, &v9x_ps11_env);
    desc.cube = 0ul;
    desc.projected = 0ul;
    desc.unbound = 0ul;
    desc.fog = v9x_ps11_variant == V9X_PS11_FOG_PROGRAM ? 1ul : 0ul;
    desc.alpha_test = 0ul;
    desc.alpha_func = 0ul;
    status = v9x_vi_ps11_build(v9x_ps11_code, V9X_PS11_CODE_MAX,
                               &v9x_ps11_program, &desc, &v9x_ps11_info);
    if (status != V9X_STATUS_OK) {
        v9x_ps11_build_status = 0x200ul | (DWORD)status;
        return;
    }
    if (v9x_si_shader_from_vi(v9x_ps11_code, v9x_ps11_info.words, &where) !=
        V9X_STATUS_OK) {
        v9x_ps11_build_status = 0x300ul | where;
        return;
    }

    /* The table: four T#s and S#s, the constants, the fog colour. */
    for (index = 0ul; index < V9X_VI_PS11_TABLE_DWORDS; ++index) {
        v9x_ps11_table[index] = 0ul;
    }
    for (stage = 0ul; stage < V9X_PS11_STAGES; ++stage) {
        DWORD *t = v9x_ps11_table + V9X_VI_PS11_T_SHARP + 8ul * stage;
        DWORD address = lo + V9X_PS11_TEXTURES + V9X_PS11_TEX_STEP * stage;

        t[0] = (address >> 8) | (hi << 24);
        t[1] = ((hi >> 8) & 0xfful) | V9X_PS11_T1_8888;
        t[2] = (V9X_PS11_SIDE - 1ul) | ((V9X_PS11_SIDE - 1ul) << 14);
        t[3] = V9X_PS11_T3_BGRA;
        t[4] = (V9X_PS11_TEX_PITCH - 1ul) << 13;
        v9x_ps11_table[V9X_VI_PS11_S_SHARP + 4ul * stage] =
            V9X_PS11_S0_CLAMP;
    }
    v9x_vi_ps11_constants(&v9x_ps11_program, &v9x_ps11_env, v9x_ps11_table);
    for (c = 0ul; c < 3ul; ++c) {
        v9x_ps11_table[V9X_VI_PS11_FOG + c] = v9x_ps11_bits(v9x_ps11_fog_rgb[c]);
    }

    /* Two triangles: top-left, top-right, bottom-left; top-right,
     * bottom-right, bottom-left. Each wide vertex its position, then the
     * six planes at its corner. */
    for (index = 0ul; index < V9X_PS11_VERTEX_COUNT; ++index) {
        static const DWORD right[V9X_PS11_VERTEX_COUNT] = { 0, 1, 0, 1, 1, 0 };
        static const DWORD bottom[V9X_PS11_VERTEX_COUNT] = { 0, 0, 1, 0, 1, 1 };
        DWORD *out = v9x_ps11_vertices + index * V9X_PS11_VERTEX_FLOATS;
        float fx = right[index] != 0ul ? 1.0f : 0.0f;
        float fy = bottom[index] != 0ul ? 1.0f : 0.0f;

        out[0] = v9x_ps11_bits((float)(long)(right[index] != 0ul ?
                                             V9X_PS11_X1 : V9X_PS11_X0));
        out[1] = v9x_ps11_bits((float)(long)(bottom[index] != 0ul ?
                                             V9X_PS11_Y1 : V9X_PS11_Y0));
        out[2] = V9X_FLOAT_HALF;
        out[3] = V9X_FLOAT_ONE;
        for (a = 0ul; a < V9X_PS11_ATTRIBUTES; ++a) {
            for (c = 0ul; c < 4ul; ++c) {
                out[4ul + 4ul * a + c] =
                    v9x_ps11_bits(v9x_ps11_plane(a, c, fx, fy));
            }
        }
    }
    v9x_ps11_build_status = 0ul;
}

/* The draw's submission: vi_3d.c's TL draw in GFX6 mode, wide. */
static void v9x_ring_ps11(DWORD lo, DWORD hi)
{
    struct v9x_vi_pm4 stream;
    struct v9x_vi_tl_draw draw;
    BYTE *bytes = (BYTE *)&draw;
    DWORD index;

    for (index = 0ul; index < sizeof(draw); ++index) {
        bytes[index] = 0u;
    }
    draw.target_low = lo + V9X_PS11_TARGET;
    draw.target_high = hi;
    draw.pitch_pixels = V9X_PS11_PITCH;
    draw.height = V9X_PS11_HEIGHT;
    draw.format = v9x_ps11_bpp16 ? V9X_VI_CB_FORMAT_5_6_5
                                 : V9X_VI_CB_FORMAT_8_8_8_8;
    draw.target_swap = v9x_ps11_bpp16 ? V9X_VI_CB_SWAP_STD_REV
                                      : V9X_VI_CB_SWAP_ALT;
    draw.tile_mode_index = V9X_VI_TILE_LINEAR_ALIGNED;
    draw.vs_low = lo + V9X_VS;
    draw.ps_low = lo + V9X_PS;
    draw.shader_high = hi;
    draw.vertex_low = lo + V9X_PS11_VERTICES;
    draw.vertex_high = hi;
    draw.vertex_count = V9X_PS11_VERTEX_COUNT;
    draw.vertex_stride = V9X_SI_WIDE_VERTEX_BYTES;
    draw.vs_vgprs = V9X_SI_WIDE_VS_VGPRS;
    draw.params = V9X_SI_WIDE_PARAMS;
    draw.ps_vgprs = v9x_ps11_info.vgprs;
    draw.ps_sgprs = v9x_ps11_info.sgprs;
    draw.ps_kill = v9x_ps11_info.kills;
    draw.ps_table_low = lo + V9X_PS11_TABLE;
    draw.ps_table_high = hi;
    draw.fence_low = lo + V9X_FENCE;
    draw.fence_high = hi;
    draw.fence_value = V9X_FENCE_VALUE;
    v9x_ring_stream_begin(&stream);
    v9x_rect_status = (DWORD)v9x_vi_3d_tl_draw(&stream, &draw);
    v9x_ring_stream_end(&stream);
}

/* data[] gets count dwords from words; the first index returned. */
static DWORD v9x_put_data(const DWORD *words, DWORD count)
{
    DWORD first = v9x_data_dwords;
    DWORD index;

    for (index = 0ul; index < count && v9x_data_dwords < V9X_SICP_DATA_MAX;
         ++index) {
        v9x_data[v9x_data_dwords++] = words[index];
    }
    return first;
}

/* The ops that put the vertices, table and textures in the window, the
 * target filled with the guard; the VS's first data dword and count. */
static void v9x_ps11_data_ops(DWORD *vs_first, DWORD *vs_count,
                              DWORD *ps_first, DWORD *ps_count)
{
    DWORD vs[V9X_SI_TL_VS_MAX];
    DWORD row[V9X_PS11_SIDE];
    DWORD stage;
    DWORD x;
    DWORD y;
    DWORD first;

    *vs_count = 0ul;
    if (v9x_si_shader_wide_vs(vs, vs_count) != V9X_STATUS_OK) {
        v9x_shader_status = 0x40000000ul;
        *vs_count = 0ul;
    }
    *vs_first = v9x_put_data(vs, *vs_count);
    *ps_count = v9x_ps11_info.words;
    *ps_first = v9x_put_data(v9x_ps11_code, *ps_count);
    first = v9x_put_data(v9x_ps11_vertices,
                         V9X_PS11_VERTEX_COUNT * V9X_PS11_VERTEX_FLOATS);
    v9x_op(V9X_OP_VRAM_DATA, V9X_PS11_VERTICES, first,
           V9X_PS11_VERTEX_COUNT * V9X_PS11_VERTEX_FLOATS);
    first = v9x_put_data(v9x_ps11_table, V9X_VI_PS11_TABLE_DWORDS);
    v9x_op(V9X_OP_VRAM_DATA, V9X_PS11_TABLE, first, V9X_VI_PS11_TABLE_DWORDS);
    for (stage = 0ul; stage < V9X_PS11_STAGES; ++stage) {
        for (y = 0ul; y < V9X_PS11_SIDE; ++y) {
            for (x = 0ul; x < V9X_PS11_SIDE; ++x) {
                row[x] = v9x_ps11_texel(stage, x, y);
            }
            first = v9x_put_data(row, V9X_PS11_SIDE);
            v9x_op(V9X_OP_VRAM_DATA,
                   V9X_PS11_TEXTURES + V9X_PS11_TEX_STEP * stage +
                       y * V9X_PS11_TEX_PITCH * 4ul,
                   first, V9X_PS11_SIDE);
        }
    }
    v9x_op(V9X_OP_VRAM_FILL, V9X_PS11_TARGET, v9x_ps11_target_dwords(),
           V9X_GUARD_VALUE);
}

static void v9x_build_run(DWORD lo, DWORD hi, int with_gpu_init,
                          int with_rect)
{
    struct v9x_vi_pm4 stream;
    DWORD wptr_0;
    DWORD wptr_1;
    DWORD wptr_2;
    DWORD wptr_3 = 0ul;
    DWORD rb_cntl;
    DWORD index;
    DWORD vs_first = 0ul;
    DWORD vs_count = 0ul;
    DWORD ps_first = 0ul;
    DWORD ps_count = 0ul;

    v9x_ring_count = 0ul;
    v9x_ring_start();
    wptr_0 = v9x_ring_close();
    v9x_ring_scratch(V9X_SCRATCH_TEST);
    wptr_1 = v9x_ring_close();
    if (with_rect) {
        v9x_ring_stream_begin(&stream);
        v9x_si_3d_start(&stream);
        v9x_ring_stream_end(&stream);
    } else {
        v9x_ring_preamble();
    }
    v9x_ring_scratch(V9X_SCRATCH_PREAMBLE);
    wptr_2 = v9x_ring_close();
    if (with_rect) {
        if (v9x_ps11_variant < V9X_PS11_VARIANTS) {
            v9x_ring_ps11(lo, hi);
        } else if (v9x_gouraud) {
            v9x_ring_gouraud(lo, hi);
        } else {
            v9x_ring_rect(lo, hi);
        }
        wptr_3 = v9x_ring_close();
    }

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
    /* The shaders, and the target filled with the guard; the fence is
     * the window's 0. */
    if (with_rect && v9x_ps11_variant < V9X_PS11_VARIANTS) {
        v9x_ps11_data_ops(&vs_first, &vs_count, &ps_first, &ps_count);
    } else if (with_rect && v9x_gouraud) {
        DWORD first;
        DWORD vertex_dwords = v9x_vertex_count * V9X_TL_DWORDS;

        vs_first = v9x_tl_vs_data(&vs_count);
        if (v9x_modulate) {
            ps_first = v9x_shader_data(v9x_vi_shader_modulate_ps, &ps_count);
        } else if (v9x_texture) {
            ps_first = v9x_shader_data(v9x_vi_shader_texture_ps, &ps_count);
        } else {
            ps_first = v9x_shader_data(v9x_vi_shader_gouraud_ps, &ps_count);
        }
        /* The vertices (built with the ring) start after both shaders in
         * the data array. */
        first = v9x_data_dwords;
        for (index = 0ul; index < vertex_dwords &&
                          v9x_data_dwords < V9X_SICP_DATA_MAX; ++index) {
            v9x_data[v9x_data_dwords++] = v9x_vertices[index];
        }
        v9x_op(V9X_OP_VRAM_DATA, V9X_VERTICES, first, vertex_dwords);
        if (v9x_depth) {
            v9x_op(V9X_OP_VRAM_FILL, V9X_DEPTH, V9X_DEPTH_DWORDS,
                   V9X_DEPTH_CLEAR);
        }
        if (v9x_texture) {
            v9x_op(V9X_OP_VRAM_PATTERN, V9X_TEXTURE, V9X_TEXTURE_DWORDS,
                   V9X_SEED_TEXTURE);
        }
    } else if (with_rect) {
        vs_first = v9x_shader_data(v9x_vi_shader_solid_vs, &vs_count);
        ps_first = v9x_shader_data(v9x_vi_shader_solid_ps, &ps_count);
    }
    if (with_rect) {
        v9x_op(V9X_OP_VRAM_DATA, V9X_VS, vs_first, vs_count);
        v9x_op(V9X_OP_VRAM_DATA, V9X_PS, ps_first, ps_count);
        /* /ps11's target is its own; this one would cover its textures. */
        if (v9x_ps11_variant >= V9X_PS11_VARIANTS) {
            v9x_op(V9X_OP_VRAM_FILL, V9X_TARGET, V9X_TARGET_DWORDS,
                   V9X_GUARD_VALUE);
        }
    }
    v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_SCRATCH_REG0, V9X_SCRATCH_BEFORE, 0ul);

    /* si_init_golden_registers and constants_init, before rlc_resume as
     * in Linux's hw_init order: one table op, refused whole by SICP.VXD if
     * any entry falls outside its windows. */
    if (with_gpu_init && v9x_gpu_count != 0ul &&
        v9x_gpu_count * 3ul <= V9X_SICP_DATA_MAX - v9x_data_dwords) {
        v9x_op(V9X_OP_MMIO_TABLE, v9x_data_dwords, v9x_gpu_count, 0ul);
        for (index = 0ul; index < v9x_gpu_count; ++index) {
            v9x_data[v9x_data_dwords++] = v9x_gpu_ops[index].offset;
            v9x_data[v9x_data_dwords++] = v9x_gpu_ops[index].clear;
            v9x_data[v9x_data_dwords++] = v9x_gpu_ops[index].set;
        }
        /* Read at once, before the RLC runs, so a later change is told
         * apart from a write that never took. */
        v9x_at_early_addr = v9x_op(V9X_OP_MMIO_READ, V9X_SI_GB_ADDR_CONFIG,
                                   0ul, 0ul);
        v9x_op(V9X_OP_MMIO_READ, V9X_SI_DMA0_TILING_CONFIG, 0ul, 0ul);
        v9x_op(V9X_OP_MMIO_READ, V9X_SI_HDP_ADDR_CONFIG, 0ul, 0ul);
    }

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
    v9x_at_reset_stat = v9x_op(V9X_OP_MMIO_READ, V9X_SI_RLC_STAT, 0ul, 0ul);
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
    v9x_at_rlc_addr = v9x_op(V9X_OP_MMIO_READ, V9X_SI_GB_ADDR_CONFIG, 0ul,
                             0ul);

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

    /* The rectangle, then its fence; HDP's read cache is invalidated
     * before the CPU polls what the GPU writes. */
    if (with_rect) {
        v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_CP_RB0_WPTR, wptr_3, 0ul);
        v9x_op(V9X_OP_MMIO_WRITE, V9X_SI_HDP_DEBUG0, 1ul, 0ul);
        v9x_at_wait_3 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE, V9X_FENCE_VALUE,
                               V9X_FENCE_WAIT_LIMIT);
        v9x_at_rptr_3 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_CP_RB0_RPTR, 0ul,
                               0ul);
        v9x_at_grbm_3 = v9x_op(V9X_OP_MMIO_READ, V9X_SI_GRBM_STATUS, 0ul,
                               0ul);
    }

    /* What gpu_init left, read while the CP still runs. */
    v9x_at_check = v9x_request.count;
    if (with_gpu_init) {
        for (index = 0ul; index < V9X_CHECK_COUNT; ++index) {
            v9x_op(V9X_OP_MMIO_READ, v9x_check_offsets[index], 0ul, 0ul);
        }
        for (index = 0ul; index < V9X_SI_TILE_MODES; ++index) {
            v9x_op(V9X_OP_MMIO_READ, V9X_SI_TILE_MODE0 + index * 4ul, 0ul,
                   0ul);
        }
    }

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
    if (with_rect) {
        v9x_op(V9X_OP_VRAM_BLOCK, V9X_FENCE, 1ul, 0ul);
        if (v9x_ps11_variant < V9X_PS11_VARIANTS) {
            v9x_op(V9X_OP_VRAM_BLOCK, V9X_PS11_TARGET,
                   v9x_ps11_target_dwords(), 0ul);
        } else {
            v9x_op(V9X_OP_VRAM_BLOCK, V9X_TARGET, V9X_TARGET_DWORDS, 0ul);
        }
        if (v9x_depth) {
            v9x_op(V9X_OP_VRAM_BLOCK, V9X_DEPTH, V9X_DEPTH_DWORDS, 0ul);
        }
    }
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
    if (with_rect) {
        v9x_key("Run", "Wptr3", wptr_3);
        v9x_key("Run", "RectStatus", v9x_rect_status);
        v9x_key("Run", "ShaderStatus", v9x_shader_status);
        v9x_number("Run", "VsDwords", vs_count);
        v9x_number("Run", "PsDwords", ps_count);
    }
    v9x_key("Run", "RbCntl", rb_cntl);
}

/* The target, against the rectangle: every pixel inside one value, every
 * pixel outside still the guard. The value inside is reported, not
 * judged: which byte each channel lands in is what the run shows.
 * V9XCK3D's check. */
static void v9x_verify(const DWORD *target)
{
    DWORD x;
    DWORD y;
    DWORD inside_value;
    DWORD inside_other = 0ul;
    DWORD outside_changed = 0ul;
    DWORD pixel;

    inside_value = target[V9X_RECT_TOP * V9X_TARGET_PITCH + V9X_RECT_LEFT];
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

/* The ramp, V9XCK3D's check: red round(255 (x + 0.5) / 64) at each pixel
 * centre, green 80h, blue 40h, alpha FFh, in the D3DCOLOR's own bytes
 * (blue the low one: the colour path keeps memory order). Within +-2 per
 * channel. */
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
            if (v9x_near((pixel >> 16) & 0xfful, red) &&
                v9x_near((pixel >> 8) & 0xfful, 0x80ul) &&
                v9x_near(pixel & 0xfful, 0x40ul) &&
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

/* /depth's two halves, V9XCK3D's check: red left of x 32, blue right of
 * it, within +-2; then the Z16 buffer a tile at a time, every sample 8000h
 * in the left half's tiles and 4000h in the right's. */
static void v9x_verify_depth(const DWORD *target, const DWORD *depth)
{
    DWORD x;
    DWORD y;
    DWORD pixel;
    DWORD want;
    DWORD tile;
    DWORD index;
    DWORD sample;
    DWORD colour_bad = 0ul;
    DWORD depth_bad = 0ul;
    DWORD tiles_across = V9X_TARGET_PITCH / 8ul;
    DWORD tiles = V9X_DEPTH_DWORDS / V9X_Z16_TILE_DWORDS;
    char text[V9X_TARGET_PITCH + 1u];
    char key[16];

    for (y = 0ul; y < V9X_TARGET_HEIGHT; ++y) {
        for (x = 0ul; x < V9X_TARGET_PITCH; ++x) {
            pixel = target[y * V9X_TARGET_PITCH + x];
            want = x < V9X_TARGET_PITCH / 2ul ? V9X_DEPTH_RED : V9X_DEPTH_BLUE;
            if (v9x_near(pixel & 0xfful, want & 0xfful) &&
                v9x_near((pixel >> 8) & 0xfful, (want >> 8) & 0xfful) &&
                v9x_near((pixel >> 16) & 0xfful, (want >> 16) & 0xfful) &&
                v9x_near(pixel >> 24, want >> 24)) {
                text[x] = x < V9X_TARGET_PITCH / 2ul ? 'R' : 'B';
            } else {
                text[x] = pixel == V9X_GUARD_VALUE ? '.' :
                          (pixel == V9X_DEPTH_GREEN ? 'G' : '?');
                ++colour_bad;
            }
        }
        text[V9X_TARGET_PITCH] = '\0';
        lstrcpyA(key, "Row");
        v9x_decimal(key + 3, y);
        v9x_text("Map", key, text);
    }
    for (tile = 0ul; tile < tiles; ++tile) {
        want = (tile % tiles_across) < tiles_across / 2ul ? V9X_Z16_HALF :
                                                            V9X_Z16_QUARTER;
        for (index = 0ul; index < V9X_Z16_TILE_DWORDS; ++index) {
            sample = depth[tile * V9X_Z16_TILE_DWORDS + index];
            if ((sample & 0xfffful) != want || (sample >> 16) != want) {
                ++depth_bad;
            }
        }
        lstrcpyA(key, "Tile");
        v9x_decimal(key + 4, tile);
        v9x_key("Depth", key, depth[tile * V9X_Z16_TILE_DWORDS]);
    }
    v9x_number("Result", "ColourBad", colour_bad);
    v9x_number("Result", "DepthBadDwords", depth_bad);
    v9x_pass("DepthColour", colour_bad == 0ul);
    v9x_pass("DepthBuffer", depth_bad == 0ul);
}

/* round(a x b / 255) for bytes, the modulate the CB should store. */
static DWORD v9x_scale(DWORD a, DWORD b)
{
    return (a * b + 127ul) / 255ul;
}

/* /texture: every pixel the texel under it, byte for byte (the colour path
 * keeps memory order), exact. /modulate: each byte times the diffuse's
 * byte in the same place, within +-2. V9XCK3D's check. */
static void v9x_verify_texture(const DWORD *target)
{
    DWORD x;
    DWORD y;
    DWORD index;
    DWORD texel;
    DWORD pixel;
    DWORD want[4];
    DWORD got[4];
    DWORD chan;
    DWORD bad = 0ul;
    DWORD exact = 0ul;
    DWORD first_bad = 0xfffffffful;
    int ok;
    char text[V9X_TARGET_PITCH + 1u];
    char key[16];

    for (y = 0ul; y < V9X_TARGET_HEIGHT; ++y) {
        for (x = 0ul; x < V9X_TARGET_PITCH; ++x) {
            index = y * V9X_TARGET_PITCH + x;
            texel = v9x_pattern(V9X_SEED_TEXTURE, index);
            pixel = target[index];
            for (chan = 0ul; chan < 4ul; ++chan) {
                want[chan] = (texel >> (chan * 8ul)) & 0xfful;
                if (v9x_modulate) {
                    want[chan] = v9x_scale(want[chan],
                                           (V9X_MODULATE_COLOUR >>
                                            (chan * 8ul)) & 0xfful);
                }
            }
            ok = 1;
            for (chan = 0ul; chan < 4ul; ++chan) {
                got[chan] = (pixel >> (chan * 8ul)) & 0xfful;
                if (v9x_modulate ? !v9x_near(got[chan], want[chan]) :
                                   got[chan] != want[chan]) {
                    ok = 0;
                }
            }
            if (got[0] == want[0] && got[1] == want[1] &&
                got[2] == want[2] && got[3] == want[3]) {
                ++exact;
            }
            text[x] = ok ? '#' : (pixel == V9X_GUARD_VALUE ? '.' : '?');
            if (!ok && bad++ == 0ul) {
                first_bad = index;
            }
        }
        text[V9X_TARGET_PITCH] = '\0';
        lstrcpyA(key, "Row");
        v9x_decimal(key + 3, y);
        v9x_text("Map", key, text);
    }
    for (index = 0ul; index < 4ul; ++index) {
        lstrcpyA(key, "Texel");
        v9x_decimal(key + 5, index);
        v9x_key("Texture", key, v9x_pattern(V9X_SEED_TEXTURE, index));
        lstrcpyA(key, "Pixel");
        v9x_decimal(key + 5, index);
        v9x_key("Texture", key, target[index]);
    }
    v9x_number("Result", "TextureBad", bad);
    v9x_number("Result", "TextureExact", exact);
    if (bad != 0ul) {
        v9x_number("Result", "TextureFirstBadIndex", first_bad);
        v9x_key("Result", "TextureFirstBad", target[first_bad]);
    }
    v9x_pass(v9x_modulate ? "Modulate" : "Texture", bad == 0ul);
}

/* ---- /ps11's judge --------------------------------------------------- */

struct v9x_ps11_lookup {
    int unjudged;
};

/* A nearest texel index under CLAMP; near a texel edge, unjudged. */
static DWORD v9x_ps11_index(float coord, int *unjudged)
{
    float scaled = coord * (float)(long)V9X_PS11_SIDE;
    long index;
    float fraction;

    if (scaled <= 0.0f) {
        if (scaled > -V9X_PS11_MARGIN) {
            *unjudged = 1;
        }
        return 0ul;
    }
    if (scaled >= (float)(long)V9X_PS11_SIDE) {
        if (scaled < (float)(long)V9X_PS11_SIDE + V9X_PS11_MARGIN) {
            *unjudged = 1;
        }
        return V9X_PS11_SIDE - 1ul;
    }
    index = v9x_ps11_long(scaled - 0.5f);
    fraction = scaled - (float)index;
    if (fraction < V9X_PS11_MARGIN || fraction > 1.0f - V9X_PS11_MARGIN) {
        *unjudged = 1;
    }
    if (index < 0l) {
        index = 0l;
    }
    if (index >= (long)V9X_PS11_SIDE) {
        index = (long)V9X_PS11_SIDE - 1l;
    }
    return (DWORD)index;
}

static void v9x_ps11_sample(void *user, v9x_u32 stage, v9x_u32 count,
                            const float *coords, float rgba[4])
{
    struct v9x_ps11_lookup *lookup = (struct v9x_ps11_lookup *)user;
    DWORD texel;

    (void)count;
    texel = v9x_ps11_texel(stage,
                           v9x_ps11_index(coords[0], &lookup->unjudged),
                           v9x_ps11_index(coords[1], &lookup->unjudged));
    rgba[0] = (float)(long)((texel >> 16) & 0xfful) / 255.0f;
    rgba[1] = (float)(long)((texel >> 8) & 0xfful) / 255.0f;
    rgba[2] = (float)(long)(texel & 0xfful) / 255.0f;
    rgba[3] = (float)(long)(texel >> 24) / 255.0f;
}

/* value on [0, 1] as an integer of steps steps, rounded. */
static long v9x_ps11_unorm(float value, DWORD steps)
{
    if (!(value > 0.0f)) {
        return 0l;
    }
    if (value >= 1.0f) {
        return (long)steps;
    }
    return v9x_ps11_long(value * (float)(long)steps);
}

/* Every pixel of the read-back target against the interpreter. */
static void v9x_verify_ps11(const DWORD *target)
{
    const WORD *target16 = (const WORD *)target;
    DWORD kills = 0ul;
    DWORD ok = 0ul;
    DWORD bad = 0ul;
    DWORD unjudged = 0ul;
    DWORD killed = 0ul;
    DWORD outside_bad = 0ul;
    DWORD first_bad = 0xfffffffful;
    DWORD first_bad_pixel = 0ul;
    DWORD first_bad_expected = 0ul;
    DWORD channels = v9x_ps11_bpp16 ? 3ul : 4ul;
    DWORD guard = v9x_ps11_bpp16 ? V9X_PS11_GUARD16 : V9X_GUARD_VALUE;
    long slack = v9x_ps11_bpp16 ? 1l : 2l;
    long max_diff = 0l;
    DWORD x;
    DWORD y;
    DWORD i;

    for (i = 0ul; i < v9x_ps11_program.count; ++i) {
        if (v9x_ps11_program.inst[i].op == V9X_PS11_TEXKILL) {
            kills |= 1ul << v9x_ps11_program.inst[i].dst_reg;
        }
    }
    for (y = 0ul; y < V9X_PS11_HEIGHT; ++y) {
        for (x = 0ul; x < V9X_PS11_PITCH; ++x) {
            DWORD index = y * V9X_PS11_PITCH + x;
            DWORD pixel = v9x_ps11_bpp16 ? (DWORD)target16[index]
                                         : target[index];
            struct v9x_ps11_lookup lookup;
            V9X_PS11_PIXEL inputs;
            float out[4];
            float fx;
            float fy;
            long want[4];
            long got[4];
            long diff;
            DWORD expected = 0ul;
            DWORD a;
            DWORD c;
            int kept;
            int good = 1;

            if (x < V9X_PS11_X0 || x >= V9X_PS11_X1 || y < V9X_PS11_Y0 ||
                y >= V9X_PS11_Y1) {
                if (pixel != guard) {
                    ++outside_bad;
                }
                continue;
            }
            fx = ((float)(long)(x - V9X_PS11_X0) + 0.5f) /
                 (float)(long)(V9X_PS11_X1 - V9X_PS11_X0);
            fy = ((float)(long)(y - V9X_PS11_Y0) + 0.5f) /
                 (float)(long)(V9X_PS11_Y1 - V9X_PS11_Y0);
            lookup.unjudged = 0;
            for (a = 0ul; a < V9X_PS11_ATTRIBUTES; ++a) {
                for (c = 0ul; c < 4ul; ++c) {
                    float value = v9x_ps11_plane(a, c, fx, fy);

                    if (a < 2ul) {
                        inputs.v[a][c] = value;
                    } else {
                        inputs.tc[a - 2ul][c] = value;
                        if ((kills & (1ul << (a - 2ul))) != 0ul && c < 3ul &&
                            value < V9X_PS11_KILL_MARGIN &&
                            value > -V9X_PS11_KILL_MARGIN) {
                            lookup.unjudged = 1;
                        }
                    }
                }
            }
            kept = v9x_ps11_run(&v9x_ps11_program, &v9x_ps11_env, &inputs,
                                v9x_ps11_sample, &lookup, out);
            if (lookup.unjudged) {
                ++unjudged;
                continue;
            }
            if (!kept) {
                if (pixel == guard) {
                    ++killed;
                    continue;
                }
                good = 0;
                expected = guard;
            } else {
                /* Fog as vi_ps11.h applies it: r0 clamped, then
                 * (c - fog) f + fog, f the specular alpha. */
                if (v9x_ps11_variant == V9X_PS11_FOG_PROGRAM) {
                    float f = inputs.v[1][3];

                    for (c = 0ul; c < 4ul; ++c) {
                        out[c] = out[c] < 0.0f ? 0.0f :
                                 out[c] > 1.0f ? 1.0f : out[c];
                    }
                    for (c = 0ul; c < 3ul; ++c) {
                        out[c] = (out[c] - v9x_ps11_fog_rgb[c]) * f +
                                 v9x_ps11_fog_rgb[c];
                    }
                }
                if (v9x_ps11_bpp16) {
                    want[0] = v9x_ps11_unorm(out[0], 31ul);
                    want[1] = v9x_ps11_unorm(out[1], 63ul);
                    want[2] = v9x_ps11_unorm(out[2], 31ul);
                    got[0] = (long)((pixel >> 11) & 0x1ful);
                    got[1] = (long)((pixel >> 5) & 0x3ful);
                    got[2] = (long)(pixel & 0x1ful);
                    expected = ((DWORD)want[0] << 11) |
                               ((DWORD)want[1] << 5) | (DWORD)want[2];
                } else {
                    for (c = 0ul; c < 4ul; ++c) {
                        want[c] = v9x_ps11_unorm(out[c], 255ul);
                    }
                    got[0] = (long)((pixel >> 16) & 0xfful);
                    got[1] = (long)((pixel >> 8) & 0xfful);
                    got[2] = (long)(pixel & 0xfful);
                    got[3] = (long)(pixel >> 24);
                    expected = ((DWORD)want[3] << 24) |
                               ((DWORD)want[0] << 16) |
                               ((DWORD)want[1] << 8) | (DWORD)want[2];
                }
                for (c = 0ul; c < channels; ++c) {
                    diff = got[c] - want[c];
                    if (diff < 0l) {
                        diff = -diff;
                    }
                    if (diff > max_diff) {
                        max_diff = diff;
                    }
                    if (diff > slack) {
                        good = 0;
                    }
                }
            }
            if (good) {
                ++ok;
                continue;
            }
            if (bad++ == 0ul) {
                first_bad = index;
                first_bad_pixel = pixel;
                first_bad_expected = expected;
            }
        }
    }
    v9x_text("Ps11", "Program", v9x_ps11_probe_variants[v9x_ps11_variant].name);
    v9x_number("Ps11", "Bpp", v9x_ps11_bpp16 ? 16ul : 32ul);
    v9x_number("Ps11", "Words", v9x_ps11_info.words);
    v9x_number("Ps11", "Sgprs", v9x_ps11_info.sgprs);
    v9x_number("Ps11", "Kills", v9x_ps11_info.kills);
    v9x_number("Ps11", "Judged", ok + bad);
    v9x_number("Ps11", "Bad", bad);
    v9x_number("Ps11", "Unjudged", unjudged);
    v9x_number("Ps11", "Killed", killed);
    v9x_number("Ps11", "OutsideBad", outside_bad);
    v9x_number("Ps11", "WorstStep", (DWORD)max_diff);
    v9x_key("Ps11", "Corner", v9x_ps11_bpp16 ?
            (DWORD)target16[V9X_PS11_Y0 * V9X_PS11_PITCH + V9X_PS11_X0] :
            target[V9X_PS11_Y0 * V9X_PS11_PITCH + V9X_PS11_X0]);
    if (bad != 0ul) {
        v9x_number("Ps11", "FirstBadX", first_bad % V9X_PS11_PITCH);
        v9x_number("Ps11", "FirstBadY", first_bad / V9X_PS11_PITCH);
        v9x_key("Ps11", "FirstBadPixel", first_bad_pixel);
        v9x_key("Ps11", "FirstBadExpected", first_bad_expected);
    }
    v9x_pass("Ps11", bad == 0ul && outside_bad == 0ul && ok != 0ul);
}

/* /ps11:N's N, 0xFFFFFFFF if the switch is absent or N is not a
 * program. */
static DWORD v9x_ps11_switch(void)
{
    const char *line = GetCommandLineA();
    static const char key[] = "/ps11:";
    int length = lstrlenA(key);

    while (*line != '\0') {
        if (CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, line,
                           length, key, length) == CSTR_EQUAL) {
            char digit = line[length];

            if (digit >= '0' && digit < (char)('0' + V9X_PS11_VARIANTS)) {
                return (DWORD)(digit - '0');
            }
            return 0xfffffffful;
        }
        ++line;
    }
    return 0xfffffffful;
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
    int with_gpu_init;
    int with_rect;
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
    v9x_modulate = v9x_switch_given("/modulate");
    v9x_texture = v9x_modulate || v9x_switch_given("/texture");
    v9x_depth = v9x_switch_given("/depth");
    v9x_gouraud = v9x_texture || v9x_depth || v9x_switch_given("/gouraud");
    v9x_ps11_variant = v9x_ps11_switch();
    v9x_ps11_bpp16 = v9x_switch_given("/bpp16");
    with_rect = v9x_gouraud || v9x_switch_given("/rect") ||
                v9x_ps11_variant < V9X_PS11_VARIANTS;
    if (v9x_ps11_variant < V9X_PS11_VARIANTS) {
        v9x_ps11_prepare(lo, hi);
        v9x_key("Ps11", "BuildStatus", v9x_ps11_build_status);
        if (v9x_ps11_build_status != 0ul) {
            CloseHandle(device);
            v9x_finish("PS11-NOT-BUILT", 9u);
        }
    }
    with_gpu_init = with_rect || v9x_switch_given("/gpuinit");
    v9x_gpu_count = 0ul;
    if (with_gpu_init) {
        v9x_gpu_count = v9x_si_gpu_init_ops(v9x_gpu_ops, V9X_GPU_OPS_MAX);
        v9x_number("GpuInit", "Entries", v9x_gpu_count);
        if (v9x_gpu_count == 0ul) {
            CloseHandle(device);
            v9x_finish("GPU-INIT-TOO-LONG", 8u);
        }
    }
    v9x_build_run(lo, hi, with_gpu_init, with_rect);
    if (with_rect && (v9x_rect_status != 0ul || v9x_shader_status != 0ul)) {
        CloseHandle(device);
        v9x_finish("RECT-NOT-BUILT", 9u);
    }
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
    v9x_key("During", "RlcStatAfterReset", r[v9x_at_reset_stat]);
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
    if (with_gpu_init) {
        DWORD mismatches = 0ul;
        DWORD expected;
        int known;
        char key[24];

        for (index = 0ul; index < V9X_CHECK_COUNT; ++index) {
            DWORD got = r[v9x_at_check + index];

            v9x_key("GpuInit", v9x_check_names[index], got);
            expected = v9x_expected(v9x_check_offsets[index], &known);
            if (known && got != expected) {
                ++mismatches;
                lstrcpyA(key, v9x_check_names[index]);
                lstrcatA(key, "Want");
                v9x_key("GpuInit", key, expected);
            }
        }
        for (index = 0ul; index < V9X_SI_TILE_MODES; ++index) {
            DWORD got = r[v9x_at_check + V9X_CHECK_COUNT + index];

            lstrcpyA(key, "TileMode");
            v9x_decimal(key + 8, index);
            v9x_key("GpuInit", key, got);
            expected = v9x_expected(V9X_SI_TILE_MODE0 + index * 4ul, &known);
            if (known && got != expected) {
                ++mismatches;
                lstrcatA(key, "Want");
                v9x_key("GpuInit", key, expected);
            }
        }
        v9x_key("GpuInit", "EarlyGbAddrConfig", r[v9x_at_early_addr]);
        v9x_key("GpuInit", "EarlyDma0Tiling", r[v9x_at_early_addr + 1ul]);
        v9x_key("GpuInit", "EarlyHdpAddrConfig", r[v9x_at_early_addr + 2ul]);
        v9x_key("GpuInit", "RlcStartedGbAddrConfig", r[v9x_at_rlc_addr]);
        v9x_number("GpuInit", "Mismatches", mismatches);
        v9x_pass("GpuInit", mismatches == 0ul);
    }
    if (with_rect) {
        v9x_key("During", "Wait3", r[v9x_at_wait_3]);
        v9x_key("During", "Rptr3", r[v9x_at_rptr_3]);
        v9x_key("During", "GrbmStatus3", r[v9x_at_grbm_3]);
        v9x_key("Result", "Fence", v9x_result.block[1]);
        v9x_pass("FenceLanded", r[v9x_at_wait_3] != V9X_SICP_TIMEOUT);
        if (v9x_ps11_variant < V9X_PS11_VARIANTS) {
            v9x_verify_ps11(v9x_result.block + 2);
        } else if (v9x_gouraud) {
            if (v9x_depth) {
                v9x_verify_depth(v9x_result.block + 2,
                                 v9x_result.block + 2 + V9X_TARGET_DWORDS);
            } else if (v9x_texture) {
                v9x_verify_texture(v9x_result.block + 2);
            } else {
                v9x_verify_gouraud(v9x_result.block + 2);
            }
        } else {
            v9x_verify(v9x_result.block + 2);
        }
    }
    v9x_key("Result", "WritebackRptr", v9x_result.block[0]);
    v9x_pass("RingTest", r[v9x_at_wait_1] != V9X_SICP_TIMEOUT);
    v9x_pass("Preamble", r[v9x_at_wait_2] != V9X_SICP_TIMEOUT);
    v9x_finish("COMPLETE", 0u);
}
