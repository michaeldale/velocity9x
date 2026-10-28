/*
 * The AMD Carrizo (GFX8) graphics command processor for V9XHAL.DLL:
 * started for the first Direct3D batch, left running, and drained by
 * whatever needs what it drew.
 *
 * Bring-up repeats V9XCZ3D's sequence, which ran on M-XP7053's R7E
 * (docs\decisions\2026-10-28-carrizo-cp-ring-test.md, -gpu-init.md and the
 * 3D records after it), in Linux v4.9 gfx_v8_0_hw_init's order with
 * smc_load_fw=0: the golden registers and gpu_init (vi_gpu_init.c),
 * rlc_resume (the RLC stopped with its GUI-idle interrupts off, clock and
 * power gating off, an RLC soft reset, its microcode, started),
 * cp_gfx_load_microcode (halted, PFP, CE, ME; vi_gfx_microcode.c, the
 * licence in NOTICES.TXT), cp_gfx_resume's ring registers, cp_gfx_start's
 * CP registers and unhalt, Linux's ring test, then cp_gfx_start's clear
 * state (vi_3d.c, v9x_vi_3d_start). A failed step halts the CP and the RLC
 * and refuses Direct3D for the rest of the HAL's life; DirectDraw on SDMA
 * does not depend on it.
 *
 * The ring, its write-back, the fence, the shaders and the vertex arena
 * live in the window the mini-VDD maps at BAR0 + 240 MiB beside SDMA's
 * ring (eng_vi.c). The CPU writes all of them through BAR0, so HDP is
 * flushed before the write pointer moves, and once the CP has drawn, HDP is
 * flushed and its read cache invalidated before the CPU reads (SDMA's
 * rule). Every batch ends with v9x_vi_3d_tl_draw's end-of-pipe event,
 * which flushes the colour and depth caches and writes a sequence number
 * to the fence: idle is that number landing, as for SDMA.
 *
 * The two engines share VRAM and run independently, so each waits for the
 * other: a batch waits for queued SDMA work (v9x_vi_dma_settle), and
 * eng_vi.c drains the CP before its fills and copies and in its wait_idle,
 * which Flip and Lock reach.
 */
#include "ddhal_internal.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_gfx_microcode.h"
#include "velocity9x/vi_gpu_init.h"
#include "velocity9x/vi_sdma.h"
#include "velocity9x/vi_shader.h"

/* The window's layout, from its start (vi_sdma.h: SDMA's ring and slots
 * take the first 33 KiB). The ring is the probe's 16 KiB; the shader slots
 * 256 bytes each; the vertex arena the window's top 2 MiB. */
#define V9X_VI_CP_WINDOW_RING       0x00010000ul
#define V9X_VI_CP_RING_BYTES        0x00004000ul
#define V9X_VI_CP_RING_DWORDS       (V9X_VI_CP_RING_BYTES / 4ul)
#define V9X_VI_CP_RING_LOG2_QWORDS  11ul
#define V9X_VI_CP_WINDOW_WRITEBACK  0x00014000ul
#define V9X_VI_CP_WINDOW_FENCE      0x00014100ul
#define V9X_VI_CP_WINDOW_SHADERS    0x00015000ul
#define V9X_VI_CP_SHADER_BYTES      0x00000100ul
#define V9X_VI_CP_WINDOW_VERTICES   0x00200000ul
#define V9X_VI_CP_VERTEX_BYTES      0x00200000ul

/* Bounded waits, as eng_vi.c's: the probe's fences landed within a few
 * dozen polls. */
#define V9X_VI_CP_SPINS       0x00200000ul
#define V9X_VI_CP_QUICK_READS 16ul
/* SRBM_STATUS reads standing in for Linux's udelay(50): an uncached read
 * takes about a microsecond. The probe used 2000. */
#define V9X_VI_CP_DELAY_READS 2000ul

/* Registers: gfx_8_0_d.h, oss_3_0_d.h, bif_5_0_d.h, dword indices times
 * four, as V9XCZ3D's. */
#define V9X_VI_CP_SRBM_STATUS         0x00e50ul
#define V9X_VI_CP_HDP_DEBUG0          0x02f30ul
#define V9X_VI_CP_HDP_MEM_FLUSH       0x05480ul
#define V9X_VI_CP_GRBM_STATUS         0x08010ul
#define V9X_VI_CP_GRBM_SOFT_RESET     0x08020ul
#define V9X_VI_CP_CP_STAT             0x08680ul
#define V9X_VI_CP_ME_CNTL             0x086d8ul
#define V9X_VI_CP_RB0_RPTR            0x08700ul
#define V9X_VI_CP_RB_WPTR_DELAY       0x08704ul
#define V9X_VI_CP_RB0_BASE            0x0c100ul
#define V9X_VI_CP_RB0_CNTL            0x0c104ul
#define V9X_VI_CP_RB0_RPTR_ADDR       0x0c10cul
#define V9X_VI_CP_RB0_RPTR_ADDR_HI    0x0c110ul
#define V9X_VI_CP_RB0_WPTR            0x0c114ul
#define V9X_VI_CP_DEVICE_ID           0x0c12cul
#define V9X_VI_CP_ENDIAN_SWAP         0x0c140ul
#define V9X_VI_CP_RB_VMID             0x0c144ul
#define V9X_VI_CP_RB_DOORBELL         0x0c164ul
#define V9X_VI_CP_INT_CNTL_RING0      0x0c1a8ul
#define V9X_VI_CP_MAX_CONTEXT         0x0c2b8ul
#define V9X_VI_CP_RB0_BASE_HI         0x0c2c4ul
#define V9X_VI_CP_SCRATCH_REG0        0x30100ul
#define V9X_VI_CP_RLC_CNTL            0x3b000ul
#define V9X_VI_CP_RLC_PG_CNTL         0x3b10cul
#define V9X_VI_CP_RLC_CGCG_CGLS_CTRL  0x3b124ul
#define V9X_VI_CP_RLC_SERDES_CU_BUSY  0x3b184ul
#define V9X_VI_CP_RLC_SERDES_NONCU    0x3b188ul
#define V9X_VI_CP_PFP_UCODE_ADDR      0x3e050ul
#define V9X_VI_CP_PFP_UCODE_DATA      0x3e054ul
#define V9X_VI_CP_ME_RAM_WADDR        0x3e058ul
#define V9X_VI_CP_ME_RAM_DATA         0x3e05cul
#define V9X_VI_CP_CE_UCODE_ADDR       0x3e060ul
#define V9X_VI_CP_CE_UCODE_DATA       0x3e064ul
#define V9X_VI_CP_RLC_GPM_UCODE_ADDR  0x3e0f0ul
#define V9X_VI_CP_RLC_GPM_UCODE_DATA  0x3e0f4ul

/* Fields: gfx_8_0_sh_mask.h, as V9XCZ3D's. */
#define V9X_VI_CP_HALTS             0x15000000ul
#define V9X_VI_CP_RLC_ENABLE        0x00000001ul
#define V9X_VI_CP_CGCG_CGLS         0x00000003ul
#define V9X_VI_CP_SOFT_RESET_RLC    0x00000004ul
#define V9X_VI_CP_GUI_IDLE_INTS     0x003c0000ul
#define V9X_VI_CP_NONCU_BUSY        0x000dfffful
#define V9X_VI_CP_RB_RPTR_WR_ENA    0x80000000ul
#define V9X_VI_CP_DOORBELL_EN       0x40000000ul
/* gfx_v8_0_gpu_init: max_hw_contexts 8 for Carrizo; cp_gfx_start writes
 * one less. */
#define V9X_VI_CP_MAX_CONTEXT_VALUE 7ul
/* cp_gfx_resume's CP_RB0_CNTL for the ring: RB_BUFSZ, RB_BLKSZ two less,
 * MTYPE 3, MIN_IB_AVAILSZ 1. */
#define V9X_VI_CP_RB_CNTL_VALUE \
    (V9X_VI_CP_RING_LOG2_QWORDS | ((V9X_VI_CP_RING_LOG2_QWORDS - 2ul) << 8) | \
     (3ul << 15) | (1ul << 22))

/* The ring test's values, as V9XCZ3D's. */
#define V9X_VI_CP_SCRATCH_SEED  0xcafedeadul
#define V9X_VI_CP_SCRATCH_TEST  0xdeadbeeful
#define V9X_VI_CP_SCRATCH_START 0x600dcafeul
/* mmSCRATCH_REG0's dword index, which SET_UCONFIG_REG names. */
#define V9X_VI_CP_SCRATCH_INDEX 0xc040ul

/* Fault-trace tags, "VI3" and where the CP was given up: S at start-up,
 * P waiting for ring space, D draining. The address is the CP's read
 * pointer in the high half and ours in the low. */
#define V9X_VI_CP_FAULT_START 0x56493353ul
#define V9X_VI_CP_FAULT_PUT   0x56493350ul
#define V9X_VI_CP_FAULT_DRAIN 0x56493344ul
#define V9X_VI_CP_LOG_PATH V9X_DIAG_PATH("V9XCZCP.TXT")

#define V9X_VI_CP_NOT_TRIED 0
#define V9X_VI_CP_RUNNING   1
#define V9X_VI_CP_FAILED    2

/* One CP, so one set of statics: V9XHAL's statics are shared by every
 * process, which is what these need. The write pointer is in dwords. */
static int v9x_vi_cp_state = V9X_VI_CP_NOT_TRIED;
static DWORD v9x_vi_cp_wptr;
static DWORD v9x_vi_cp_high;
static DWORD v9x_vi_cp_heap_low;
static DWORD v9x_vi_cp_window_low;
static DWORD v9x_vi_cp_vertex_at;
static DWORD v9x_vi_cp_sequence;
static int v9x_vi_cp_dirty;
static v9x_u32 v9x_vi_cp_stream[V9X_VI_START_DWORDS + 64ul];

static DWORD v9x_vi_cp_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_vi_cp_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static void v9x_vi_cp_modify32(DWORD offset, DWORD clear, DWORD set)
{
    v9x_vi_cp_write32(offset, (v9x_vi_cp_read32(offset) & ~clear) | set);
}

static volatile DWORD *v9x_vi_cp_window(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.ring_linear_base + offset);
}

static void v9x_vi_cp_delay(void)
{
    DWORD reads = V9X_VI_CP_DELAY_READS;

    while (reads-- != 0ul) {
        (void)v9x_vi_cp_read32(V9X_VI_CP_SRBM_STATUS);
    }
}

/* Waits for a register to hold value under mask; non-zero when it did. */
static int v9x_vi_cp_wait(DWORD offset, DWORD mask, DWORD value)
{
    DWORD spins;

    for (spins = 0ul; spins < V9X_VI_CP_SPINS; ++spins) {
        if ((v9x_vi_cp_read32(offset) & mask) == value) {
            return 1;
        }
    }
    return 0;
}

/* The window mapped, on this chip's engine, with the CP claimed. */
static int v9x_vi_cp_mapped(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_VI &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_VI_MMIO_BYTES &&
        v9x_hal->engine.ring_linear_base != 0ul &&
        v9x_hal->engine.ring_bytes >= V9X_VI_RING_WINDOW_BYTES;
}

static void v9x_vi_cp_log_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_vi_cp_log_hex(HANDLE file, const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[12];
    DWORD index;

    v9x_vi_cp_log_text(file, key);
    for (index = 0ul; index < 8ul; ++index) {
        text[index] = digits[(value >> ((7ul - index) * 4ul)) & 0xful];
    }
    text[8] = '\r';
    text[9] = '\n';
    text[10] = '\0';
    v9x_vi_cp_log_text(file, text);
}

/* The CP's registers and the ring, once, at the first quarantine. */
static void v9x_vi_cp_write_record(DWORD tag)
{
    HANDLE file;
    DWORD index;

    file = CreateFileA(V9X_VI_CP_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_vi_cp_log_text(file, "[CarrizoCp]\r\n");
    v9x_vi_cp_log_hex(file, "Tag=", tag);
    v9x_vi_cp_log_hex(file, "Wptr=", v9x_vi_cp_wptr);
    v9x_vi_cp_log_hex(file, "Sequence=", v9x_vi_cp_sequence);
    v9x_vi_cp_log_hex(file, "Fence=",
                      v9x_vi_cp_window(V9X_VI_CP_WINDOW_FENCE)[0]);
    v9x_vi_cp_log_hex(file, "CP_RB0_RPTR=",
                      v9x_vi_cp_read32(V9X_VI_CP_RB0_RPTR));
    v9x_vi_cp_log_hex(file, "CP_RB0_WPTR=",
                      v9x_vi_cp_read32(V9X_VI_CP_RB0_WPTR));
    v9x_vi_cp_log_hex(file, "CP_ME_CNTL=", v9x_vi_cp_read32(V9X_VI_CP_ME_CNTL));
    v9x_vi_cp_log_hex(file, "CP_STAT=", v9x_vi_cp_read32(V9X_VI_CP_CP_STAT));
    v9x_vi_cp_log_hex(file, "GRBM_STATUS=",
                      v9x_vi_cp_read32(V9X_VI_CP_GRBM_STATUS));
    v9x_vi_cp_log_hex(file, "RLC_CNTL=",
                      v9x_vi_cp_read32(V9X_VI_CP_RLC_CNTL));
    v9x_vi_cp_log_hex(file, "SCRATCH_REG0=",
                      v9x_vi_cp_read32(V9X_VI_CP_SCRATCH_REG0));
    v9x_vi_cp_log_text(file, "[Ring]\r\n");
    for (index = 0ul; index < V9X_VI_CP_RING_DWORDS; ++index) {
        v9x_vi_cp_log_hex(file, "",
                          v9x_vi_cp_window(V9X_VI_CP_WINDOW_RING)[index]);
    }
    CloseHandle(file);
}

/* The CP halted and the RLC stopped: what the BIOS left and what the probe
 * put back after each run. */
static void v9x_vi_cp_halt(void)
{
    v9x_vi_cp_modify32(V9X_VI_CP_ME_CNTL, 0ul, V9X_VI_CP_HALTS);
    v9x_vi_cp_delay();
    v9x_vi_cp_modify32(V9X_VI_CP_RLC_CNTL, V9X_VI_CP_RLC_ENABLE, 0ul);
}

/* Gives the CP up for the HAL's life, the fault traced once with the
 * pointers as they stood. */
static void v9x_vi_cp_quarantine(DWORD tag)
{
    DWORD address = ((v9x_vi_cp_read32(V9X_VI_CP_RB0_RPTR) & 0xfffful) << 16) |
                    (v9x_vi_cp_wptr & 0xfffful);

    if (v9x_hal->engine.idle_timeouts == 0ul) {
        v9x_vi_cp_write_record(tag);
    }
    v9x_vi_cp_halt();
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(tag, address);
    }
    v9x_vi_cp_state = V9X_VI_CP_FAILED;
    v9x_vi_cp_dirty = 0;
}

/* Moves the write pointer after the CPU's writes are out of the
 * write-combining buffers and through HDP. */
static void v9x_vi_cp_kick(void)
{
    v9x_wc_drain();
    v9x_vi_cp_write32(V9X_VI_CP_HDP_MEM_FLUSH, 1ul);
    v9x_vi_cp_write32(V9X_VI_CP_RB0_WPTR, v9x_vi_cp_wptr);
}

/* Dwords the CP has not yet read, between its read pointer and ours. */
static DWORD v9x_vi_cp_pending(void)
{
    DWORD rptr = v9x_vi_cp_read32(V9X_VI_CP_RB0_RPTR) &
                 (V9X_VI_CP_RING_DWORDS - 1ul);

    return (v9x_vi_cp_wptr - rptr) & (V9X_VI_CP_RING_DWORDS - 1ul);
}

/*
 * count dwords into the ring, which the caller has padded to the CP's
 * 16-dword fetch, then the write pointer. A stream that would cross the
 * ring's end is put after NOP padding to it, from the start; the room is
 * waited for, as the read pointer leaves it. Non-zero on success.
 */
static int v9x_vi_cp_put(const v9x_u32 *dwords, DWORD count)
{
    volatile DWORD *ring = v9x_vi_cp_window(V9X_VI_CP_WINDOW_RING);
    DWORD needed = count;
    DWORD spins;
    DWORD index;

    if (count == 0ul || count >= V9X_VI_CP_RING_DWORDS / 2ul ||
        (count % V9X_VI_RING_ALIGN_DWORDS) != 0ul) {
        return 0;
    }
    if (v9x_vi_cp_wptr + count > V9X_VI_CP_RING_DWORDS) {
        needed += V9X_VI_CP_RING_DWORDS - v9x_vi_cp_wptr;
    }
    for (spins = 0ul; spins < V9X_VI_CP_SPINS; ++spins) {
        if (V9X_VI_CP_RING_DWORDS - 1ul - v9x_vi_cp_pending() >= needed) {
            break;
        }
    }
    if (spins == V9X_VI_CP_SPINS) {
        return 0;
    }
    if (v9x_vi_cp_wptr + count > V9X_VI_CP_RING_DWORDS) {
        while (v9x_vi_cp_wptr < V9X_VI_CP_RING_DWORDS) {
            ring[v9x_vi_cp_wptr++] = V9X_VI_RING_NOP;
        }
        v9x_vi_cp_wptr = 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        ring[v9x_vi_cp_wptr++] = dwords[index];
    }
    v9x_vi_cp_wptr &= V9X_VI_CP_RING_DWORDS - 1ul;
    v9x_vi_cp_kick();
    return 1;
}

/* The last batch's fence landed, within the given reads. */
static int v9x_vi_cp_poll_fence(DWORD reads)
{
    volatile DWORD *fence = v9x_vi_cp_window(V9X_VI_CP_WINDOW_FENCE);

    while (reads-- != 0ul) {
        if (fence[0] == v9x_vi_cp_sequence) {
            return 1;
        }
    }
    return 0;
}

/* A stream padded and through the ring, then SCRATCH_REG0 waited for. */
static int v9x_vi_cp_run_scratch(struct v9x_vi_pm4 *stream, DWORD value)
{
    v9x_vi_pm4_set_uconfig(stream, V9X_VI_CP_SCRATCH_INDEX, value);
    v9x_vi_pm4_pad(stream);
    return stream->overflow == 0ul &&
           v9x_vi_cp_put(stream->dwords, stream->count) &&
           v9x_vi_cp_wait(V9X_VI_CP_SCRATCH_REG0, 0xfffffffful, value);
}

/* The golden registers and gpu_init's writes (vi_gpu_init.c), as
 * amdgpu_program_register_sequence applies them. The list is 3 KiB, past
 * the HAL's stack-frame limit, so it is a static, built when the CP
 * starts. */
static struct v9x_vi_reg_seq v9x_vi_cp_init_ops[V9X_VI_GPU_INIT_OPS_MAX];

static void v9x_vi_cp_gpu_init(void)
{
    struct v9x_vi_reg_seq *ops = v9x_vi_cp_init_ops;
    DWORD count;
    DWORD index;

    count = v9x_vi_gpu_init_ops(ops, V9X_VI_GPU_INIT_OPS_MAX);
    for (index = 0ul; index < count; ++index) {
        if (ops[index].clear == 0xfffffffful) {
            v9x_vi_cp_write32(ops[index].offset, ops[index].set);
        } else {
            v9x_vi_cp_modify32(ops[index].offset, ops[index].clear,
                               ops[index].set);
        }
        (void)v9x_vi_cp_read32(V9X_VI_CP_SRBM_STATUS);
    }
}

/* An image through its port: address 0, the data, then the version in the
 * address register, as Linux does for each. */
static void v9x_vi_cp_load(DWORD address, DWORD data, const v9x_u32 *image,
                           DWORD dwords, DWORD version)
{
    DWORD index;

    v9x_vi_cp_write32(address, 0ul);
    for (index = 0ul; index < dwords; ++index) {
        v9x_vi_cp_write32(data, image[index]);
    }
    v9x_vi_cp_write32(address, version);
}

/* rlc_resume: rlc_stop, CG off, PG off, rlc_reset, the microcode,
 * rlc_start. The SERDES waits are wait_for_rlc_serdes's. */
static int v9x_vi_cp_rlc_resume(void)
{
    v9x_vi_cp_modify32(V9X_VI_CP_RLC_CNTL, V9X_VI_CP_RLC_ENABLE, 0ul);
    v9x_vi_cp_modify32(V9X_VI_CP_INT_CNTL_RING0, V9X_VI_CP_GUI_IDLE_INTS, 0ul);
    if (!v9x_vi_cp_wait(V9X_VI_CP_RLC_SERDES_CU_BUSY, 0xfffffffful, 0ul) ||
        !v9x_vi_cp_wait(V9X_VI_CP_RLC_SERDES_NONCU, V9X_VI_CP_NONCU_BUSY,
                        0ul)) {
        return 0;
    }
    v9x_vi_cp_modify32(V9X_VI_CP_RLC_CGCG_CGLS_CTRL, V9X_VI_CP_CGCG_CGLS, 0ul);
    v9x_vi_cp_write32(V9X_VI_CP_RLC_PG_CNTL, 0ul);
    v9x_vi_cp_write32(V9X_VI_CP_GRBM_SOFT_RESET, V9X_VI_CP_SOFT_RESET_RLC);
    v9x_vi_cp_delay();
    v9x_vi_cp_write32(V9X_VI_CP_GRBM_SOFT_RESET, 0ul);
    v9x_vi_cp_delay();
    v9x_vi_cp_load(V9X_VI_CP_RLC_GPM_UCODE_ADDR, V9X_VI_CP_RLC_GPM_UCODE_DATA,
                   v9x_vi_rlc_microcode, V9X_VI_RLC_MICROCODE_DWORDS,
                   v9x_vi_rlc_microcode_version);
    v9x_vi_cp_modify32(V9X_VI_CP_RLC_CNTL, 0ul, V9X_VI_CP_RLC_ENABLE);
    v9x_vi_cp_delay();
    return 1;
}

/* cp_gfx_load_microcode, cp_gfx_resume's ring registers and cp_gfx_start's
 * CP registers and unhalt. The ring's 40-bit address >> 8: bits 39:32 land
 * in RB0_BASE 31:24, and RB0_BASE_HI takes what is above, nothing. */
static void v9x_vi_cp_ring_resume(void)
{
    DWORD ring_low = v9x_vi_cp_window_low + V9X_VI_CP_WINDOW_RING;

    v9x_vi_cp_modify32(V9X_VI_CP_ME_CNTL, 0ul, V9X_VI_CP_HALTS);
    v9x_vi_cp_delay();
    v9x_vi_cp_load(V9X_VI_CP_PFP_UCODE_ADDR, V9X_VI_CP_PFP_UCODE_DATA,
                   v9x_vi_pfp_microcode, V9X_VI_PFP_MICROCODE_DWORDS,
                   v9x_vi_pfp_microcode_version);
    v9x_vi_cp_load(V9X_VI_CP_CE_UCODE_ADDR, V9X_VI_CP_CE_UCODE_DATA,
                   v9x_vi_ce_microcode, V9X_VI_CE_MICROCODE_DWORDS,
                   v9x_vi_ce_microcode_version);
    v9x_vi_cp_load(V9X_VI_CP_ME_RAM_WADDR, V9X_VI_CP_ME_RAM_DATA,
                   v9x_vi_me_microcode, V9X_VI_ME_MICROCODE_DWORDS,
                   v9x_vi_me_microcode_version);

    v9x_vi_cp_write32(V9X_VI_CP_RB_WPTR_DELAY, 0ul);
    v9x_vi_cp_write32(V9X_VI_CP_RB_VMID, 0ul);
    v9x_vi_cp_write32(V9X_VI_CP_RB0_CNTL, V9X_VI_CP_RB_CNTL_VALUE);
    v9x_vi_cp_write32(V9X_VI_CP_RB0_CNTL,
                      V9X_VI_CP_RB_CNTL_VALUE | V9X_VI_CP_RB_RPTR_WR_ENA);
    v9x_vi_cp_write32(V9X_VI_CP_RB0_WPTR, 0ul);
    v9x_vi_cp_write32(V9X_VI_CP_RB0_RPTR_ADDR,
                      v9x_vi_cp_window_low + V9X_VI_CP_WINDOW_WRITEBACK);
    v9x_vi_cp_write32(V9X_VI_CP_RB0_RPTR_ADDR_HI, v9x_vi_cp_high & 0xfful);
    v9x_vi_cp_delay();
    v9x_vi_cp_write32(V9X_VI_CP_RB0_CNTL, V9X_VI_CP_RB_CNTL_VALUE);
    v9x_vi_cp_write32(V9X_VI_CP_RB0_BASE,
                      (ring_low >> 8) | ((v9x_vi_cp_high & 0xfful) << 24));
    v9x_vi_cp_write32(V9X_VI_CP_RB0_BASE_HI, 0ul);
    v9x_vi_cp_modify32(V9X_VI_CP_RB_DOORBELL, V9X_VI_CP_DOORBELL_EN, 0ul);

    v9x_vi_cp_write32(V9X_VI_CP_MAX_CONTEXT, V9X_VI_CP_MAX_CONTEXT_VALUE);
    v9x_vi_cp_write32(V9X_VI_CP_ENDIAN_SWAP, 0ul);
    v9x_vi_cp_write32(V9X_VI_CP_DEVICE_ID, 1ul);
    v9x_vi_cp_write32(V9X_VI_CP_HDP_MEM_FLUSH, 1ul);
    v9x_vi_cp_modify32(V9X_VI_CP_ME_CNTL, V9X_VI_CP_HALTS, 0ul);
    v9x_vi_cp_delay();
    v9x_vi_cp_wptr = 0ul;
}

/* The Direct3D shaders into their slots (V9X_VI_CP_SLOT_*); each fits its
 * 64 dwords. */
static void v9x_vi_cp_upload_shaders(void)
{
    v9x_u32 words[V9X_VI_CP_SHADER_BYTES / 4ul];
    volatile DWORD *slot;
    DWORD count;
    DWORD which;
    DWORD index;

    for (which = 0ul; which < V9X_VI_CP_SLOT_COUNT; ++which) {
        if (which == V9X_VI_CP_SLOT_VS) {
            count = v9x_vi_shader_tl_vs(words);
        } else if (which < V9X_VI_CP_SLOT_PS_SPECULAR_FIRST) {
            count = v9x_vi_shader_d3d_ps(which - V9X_VI_CP_SLOT_PS_FIRST, 0,
                                         words);
        } else {
            count = v9x_vi_shader_d3d_ps(
                which - V9X_VI_CP_SLOT_PS_SPECULAR_FIRST, 1, words);
        }
        slot = v9x_vi_cp_window(V9X_VI_CP_WINDOW_SHADERS +
                                which * V9X_VI_CP_SHADER_BYTES);
        for (index = 0ul; index < count; ++index) {
            slot[index] = words[index];
        }
    }
}

/* The bring-up, once. */
static int v9x_vi_cp_start(void)
{
    struct v9x_vi_pm4 stream;

    if (v9x_vi_cp_state == V9X_VI_CP_RUNNING) {
        return 1;
    }
    if (v9x_vi_cp_state == V9X_VI_CP_FAILED || !v9x_vi_cp_mapped() ||
        !v9x_vi_window_address(&v9x_vi_cp_high, &v9x_vi_cp_window_low) ||
        !v9x_vi_heap_address(&v9x_vi_cp_high, &v9x_vi_cp_heap_low)) {
        return 0;
    }
    v9x_vi_cp_state = V9X_VI_CP_FAILED;

    /* The fence starts at a value no batch writes, so a fence the CP never
     * writes cannot read as landed. */
    v9x_vi_cp_sequence = 0ul;
    v9x_vi_cp_window(V9X_VI_CP_WINDOW_FENCE)[0] = 0xfffffffful;
    v9x_vi_cp_upload_shaders();
    v9x_vi_cp_write32(V9X_VI_CP_SCRATCH_REG0, V9X_VI_CP_SCRATCH_SEED);

    v9x_vi_cp_gpu_init();
    if (!v9x_vi_cp_rlc_resume()) {
        v9x_vi_cp_quarantine(V9X_VI_CP_FAULT_START);
        return 0;
    }
    v9x_vi_cp_ring_resume();

    /* Linux's ring test, then cp_gfx_start's whole stream with a second
     * scratch write behind it. */
    v9x_vi_pm4_init(&stream, v9x_vi_cp_stream,
                    sizeof(v9x_vi_cp_stream) / sizeof(v9x_vi_cp_stream[0]));
    if (!v9x_vi_cp_run_scratch(&stream, V9X_VI_CP_SCRATCH_TEST)) {
        v9x_vi_cp_quarantine(V9X_VI_CP_FAULT_START);
        return 0;
    }
    v9x_vi_pm4_init(&stream, v9x_vi_cp_stream,
                    sizeof(v9x_vi_cp_stream) / sizeof(v9x_vi_cp_stream[0]));
    v9x_vi_3d_start(&stream);
    if (!v9x_vi_cp_run_scratch(&stream, V9X_VI_CP_SCRATCH_START)) {
        v9x_vi_cp_quarantine(V9X_VI_CP_FAULT_START);
        return 0;
    }
    v9x_vi_cp_vertex_at = 0ul;
    v9x_vi_cp_dirty = 0;
    v9x_vi_cp_state = V9X_VI_CP_RUNNING;
    return 1;
}

int v9x_vi_cp_ready(void)
{
    return v9x_vi_cp_mapped() && v9x_vi_cp_state != V9X_VI_CP_FAILED;
}

int v9x_vi_cp_submit(const v9x_u32 *dwords, DWORD count, DWORD fence)
{
    if (dwords == 0 || !v9x_vi_cp_start()) {
        return 0;
    }
    if (!v9x_vi_dma_settle()) {
        return 0;
    }
    if (!v9x_vi_cp_put(dwords, count)) {
        v9x_vi_cp_quarantine(V9X_VI_CP_FAULT_PUT);
        return 0;
    }
    v9x_vi_cp_sequence = fence;
    v9x_vi_cp_dirty = 1;
    return 1;
}

int v9x_vi_cp_drain(int wait)
{
    if (v9x_vi_cp_state != V9X_VI_CP_RUNNING || !v9x_vi_cp_dirty ||
        !v9x_vi_cp_mapped()) {
        return 1;
    }
    if (!v9x_vi_cp_poll_fence(wait ? V9X_VI_CP_SPINS
                                   : V9X_VI_CP_QUICK_READS)) {
        if (wait) {
            v9x_vi_cp_quarantine(V9X_VI_CP_FAULT_DRAIN);
        }
        return 0;
    }
    v9x_vi_cp_write32(V9X_VI_CP_HDP_MEM_FLUSH, 1ul);
    v9x_vi_cp_write32(V9X_VI_CP_HDP_DEBUG0, 1ul);
    v9x_vi_cp_dirty = 0;
    return 1;
}

int v9x_vi_cp_vertex_space(DWORD bytes, volatile DWORD **linear,
                           DWORD *gpu_low)
{
    DWORD rounded = (bytes + 0xfful) & ~0xfful;

    if (bytes == 0ul || rounded > V9X_VI_CP_VERTEX_BYTES ||
        !v9x_vi_cp_start()) {
        return 0;
    }
    /* At the arena's end, everything queued is drained before its start
     * is written again. */
    if (v9x_vi_cp_vertex_at + rounded > V9X_VI_CP_VERTEX_BYTES) {
        if (!v9x_vi_cp_drain(1)) {
            return 0;
        }
        v9x_vi_cp_vertex_at = 0ul;
    }
    *linear = v9x_vi_cp_window(V9X_VI_CP_WINDOW_VERTICES + v9x_vi_cp_vertex_at);
    *gpu_low = v9x_vi_cp_window_low + V9X_VI_CP_WINDOW_VERTICES +
               v9x_vi_cp_vertex_at;
    v9x_vi_cp_vertex_at += rounded;
    return 1;
}

/* The heap's and the window's GPU address, from the SDMA engine's
 * validation (eng_vi.c), which reads MC_VM_FB_LOCATION: located before any
 * batch is mapped, which Cedar's first draw needed (docs\decisions\
 * 2026-10-27-cedar-d3d-hal-first-light.md). */
int v9x_vi_cp_addresses(DWORD *high, DWORD *heap_low)
{
    if (!v9x_vi_cp_mapped() ||
        !v9x_vi_window_address(&v9x_vi_cp_high, &v9x_vi_cp_window_low) ||
        !v9x_vi_heap_address(&v9x_vi_cp_high, &v9x_vi_cp_heap_low)) {
        return 0;
    }
    *high = v9x_vi_cp_high;
    *heap_low = v9x_vi_cp_heap_low;
    return 1;
}

DWORD v9x_vi_cp_shader_low(DWORD which)
{
    return v9x_vi_cp_window_low + V9X_VI_CP_WINDOW_SHADERS +
           which * V9X_VI_CP_SHADER_BYTES;
}

DWORD v9x_vi_cp_fence_low(void)
{
    return v9x_vi_cp_window_low + V9X_VI_CP_WINDOW_FENCE;
}

/* The sequence the next batch's fence carries; never the 0xFFFFFFFF the
 * fence starts at. */
DWORD v9x_vi_cp_fence_next(void)
{
    DWORD next = v9x_vi_cp_sequence + 1ul;

    return next == 0xfffffffful ? 1ul : next;
}
