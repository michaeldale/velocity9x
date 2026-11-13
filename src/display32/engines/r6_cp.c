/*
 * The ATI R6xx (Radeon HD 2600 XT, RV630) command processor for
 * V9XHAL.DLL: started for the first Direct3D batch, left running, and
 * drained by whatever needs what it drew. eg_cp.c, Evergreen's, is the
 * model; what differs is R6xx's bring-up, which repeats V9XR63D's
 * (tools\diag\rv630_3d_win32.c) as it ran on M-NZHWBR:
 *
 *   - r600_cp_stop, then r600_cp_load_microcode: the ring in its loading
 *     state, a GRBM reset of the CP alone, the ME image (three times
 *     R600_PM4_UCODE_SIZE) before the PFP's (r6_cp_microcode.c; the
 *     licence is in NOTICES.TXT);
 *   - r600_cp_resume with write-back off (RB_NO_UPDATE, SCRATCH_UMSK 0),
 *     then ME_INITIALIZE and CP_ME_CNTL FFh (r600_cp_start), and
 *     r600_ring_test's scratch-register write
 *     (docs\decisions\2026-11-13-rv630-cp-ring-test.md);
 *   - r600_gpu_init's configuration through the ring as SET_CONFIG_REG,
 *     read-modify-write from MMIO reads, never CP_QUEUE_THRESHOLDS or
 *     CP_MEQ_THRESHOLDS, which stalled the ME when written through the
 *     ring (r6_3d.c, v9x_r6_3d_gpu_init;
 *     docs\decisions\2026-11-13-rv630-first-3d-draw.md).
 *
 * A failed step leaves the CP halted and Direct3D refused for the rest of
 * the HAL's life; DirectDraw on the DMA engine does not depend on it.
 *
 * The ring, its unused write-back slots, the shaders and the vertex arena
 * live in the window the mini-VDD maps at BAR0 + 240 MiB (4 MiB, EG_MAP),
 * after the DMA ring's first 64 KiB: Evergreen's layout (eg_3d.h,
 * V9X_EG_WINDOW_*). The CPU writes all of them through BAR0, so HDP is
 * flushed before the write pointer moves, and again once the CP is idle
 * having drawn, before the CPU reads.
 *
 * The two engines share VRAM and run independently, so each waits for the
 * other: a batch waits for DMA work still queued (v9x_r6_dma_settle), and
 * eng_r6.c drains the CP before its fills and copies and in its wait_idle.
 */
#include "ddhal_internal.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/r6_dma.h"
#include "velocity9x/r6_3d.h"
#include "velocity9x/r6_cp_microcode.h"
#include "velocity9x/r6_shader.h"

/* The CP's registers outside r6_3d.h's SET_CONFIG_REG set (r600d.h), as
 * V9XR63D names them. */
#define R6CP_SCRATCH_UMSK        0x8540ul
#define R6CP_SCRATCH_ADDR        0x8544ul
#define R6CP_SEM_WAIT_TIMER      0x85bcul
#define R6CP_RB_WPTR_DELAY       0x8704ul
#define R6CP_RB_BASE             0xc100ul
#define R6CP_RB_CNTL             0xc104ul
#define R6CP_RB_RPTR_WR          0xc108ul
#define R6CP_RB_RPTR_ADDR        0xc10cul
#define R6CP_RB_RPTR_ADDR_HI     0xc110ul
#define R6CP_RB_WPTR             0xc114ul
#define R6CP_PFP_UCODE_ADDR      0xc150ul
#define R6CP_PFP_UCODE_DATA      0xc154ul
#define R6CP_ME_RAM_RADDR        0xc158ul
#define R6CP_ME_RAM_WADDR        0xc15cul
#define R6CP_ME_RAM_DATA         0xc160ul
#define R6CP_DEBUG               0xc1fcul

/* CP_ME_CNTL: ME_HALT | PFP_HALT to stop, FFh to run (r600_cp_start). */
#define R6CP_HALT                0x14000000ul
#define R6CP_RUN                 0x000000fful
/* CP_RB_CNTL: RB_BUFSZ 11 and RB_BLKSZ 9, a 16 KiB ring; the loading
 * value r600_cp_load_microcode writes; RB_NO_UPDATE and RB_RPTR_WR_ENA. */
#define R6CP_RB_CNTL_VALUE       ((9ul << 8) | 11ul)
#define R6CP_RB_CNTL_LOADING     0x08000f03ul
#define R6CP_RB_NO_UPDATE        0x08000000ul
#define R6CP_RB_RPTR_WR_ENA      0x80000000ul
#define R6CP_DEBUG_VALUE         0x18000000ul
/* GRBM_SOFT_RESET: SOFT_RESET_CP alone around the microcode load, and
 * r600_gpu_soft_reset's CP-side blocks to leave a hung pipe clean. */
#define R6CP_GRBM_RESET_CP       0x00000001ul
#define R6CP_GRBM_RESET_BLOCKS   0x00004721ul
#define R6CP_GUI_ACTIVE          0x80000000ul

/* The window, from its start: the DMA ring (eng_r6.c) in the first 64
 * KiB, then the CP ring (16 KiB), the write-back slots, the shader slots
 * of 256 bytes, and the vertex arena in the top 2 MiB. */
#define R6CP_WINDOW_BYTES        0x00400000ul
#define R6CP_WINDOW_RING         0x00010000ul
#define R6CP_RING_DWORDS         4096ul
#define R6CP_WINDOW_WRITEBACK    0x00014000ul
#define R6CP_WINDOW_SHADERS      0x00015000ul
#define R6CP_SHADER_BYTES        0x00000100ul
#define R6CP_WINDOW_VERTICES     0x00200000ul
#define R6CP_VERTEX_BYTES        0x00200000ul

/* Bounded waits, as eg_cp.c's. Register reads stand in for udelay, about
 * a microsecond each; V9XR63D waited 20000 around its resets and 2000 in
 * the ring setup. */
#define R6CP_SPINS               0x00200000ul
#define R6CP_QUICK_READS         16ul
#define R6CP_RESET_READS         20000ul
#define R6CP_SHORT_READS         2000ul

/* r600_ring_test's values, as V9XR63D's. */
#define R6CP_SCRATCH_SEED        0xcafedeadul
#define R6CP_SCRATCH_TEST        0xdeadbeeful

/* BAR0 is 256 MiB aligned: a physical address's offset into it. */
#define R6CP_BAR0_MASK           0x0ffffffful

/* Fault-trace tags, "R63" and where the CP was given up: S at start-up, P
 * waiting for ring space, D draining, R the ring test. The address is the
 * CP's read pointer in the high half and ours in the low. */
#define R6CP_FAULT_START         0x52363353ul
#define R6CP_FAULT_PUT           0x52363350ul
#define R6CP_FAULT_DRAIN         0x52363344ul
#define R6CP_FAULT_TEST          0x52363352ul

/* The bring-up's stream: ME_INITIALIZE, the ring test's fence, or
 * gpu_init's table, padded. */
#define R6CP_START_DWORDS        256ul

#define R6CP_NOT_TRIED 0
#define R6CP_RUNNING   1
#define R6CP_FAILED    2

/* One CP, so one set of statics: V9XHAL's statics are shared by every
 * process, which is what these need. The write pointer is in dwords. */
static int v9x_r6_cp_state = R6CP_NOT_TRIED;
static DWORD v9x_r6_cp_wptr;
static DWORD v9x_r6_cp_window_gpu;
static DWORD v9x_r6_cp_heap_gpu;
static DWORD v9x_r6_cp_pipe_config;
static DWORD v9x_r6_cp_vertex_at;
static int v9x_r6_cp_dirty;

static DWORD v9x_r6_cp_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_r6_cp_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static volatile DWORD *v9x_r6_cp_window(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.ring_linear_base + offset);
}

static void v9x_r6_cp_delay(DWORD reads)
{
    while (reads-- != 0ul) {
        (void)v9x_r6_cp_read32(V9X_R6_GRBM_STATUS);
    }
}

static int v9x_r6_cp_wait(DWORD offset, DWORD mask, DWORD value)
{
    DWORD spins;

    for (spins = 0ul; spins < R6CP_SPINS; ++spins) {
        if ((v9x_r6_cp_read32(offset) & mask) == value) {
            return 1;
        }
    }
    return 0;
}

/* The window mapped for the 3D layout, on this card's engine. */
static int v9x_r6_cp_mapped(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_R600 &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_R6_MMIO_BYTES &&
        v9x_hal->engine.ring_linear_base != 0ul &&
        v9x_hal->engine.ring_bytes >= R6CP_WINDOW_BYTES;
}

/*
 * The registers and the ring, written once at the first quarantine to
 * C:\V9XDIAG\V9XR6CP.TXT: a ring that stalls is otherwise one pair of
 * pointers with no history.
 */
#define R6CP_LOG_PATH V9X_DIAG_PATH("V9XR6CP.TXT")

static void v9x_r6_cp_log_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_r6_cp_log_hex(HANDLE file, const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[12];
    DWORD index;

    v9x_r6_cp_log_text(file, key);
    for (index = 0ul; index < 8ul; ++index) {
        text[index] = digits[(value >> ((7ul - index) * 4ul)) & 0xful];
    }
    text[8] = '\r';
    text[9] = '\n';
    text[10] = '\0';
    v9x_r6_cp_log_text(file, text);
}

static void v9x_r6_cp_write_record(DWORD tag)
{
    HANDLE file;
    DWORD index;

    file = CreateFileA(R6CP_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_r6_cp_log_text(file, "[R600Cp]\r\n");
    v9x_r6_cp_log_hex(file, "Tag=", tag);
    v9x_r6_cp_log_hex(file, "Wptr=", v9x_r6_cp_wptr);
    v9x_r6_cp_log_hex(file, "CP_RB_RPTR=",
                      v9x_r6_cp_read32(V9X_R6_CP_RB_RPTR));
    v9x_r6_cp_log_hex(file, "CP_RB_WPTR=", v9x_r6_cp_read32(R6CP_RB_WPTR));
    v9x_r6_cp_log_hex(file, "CP_RB_BASE=", v9x_r6_cp_read32(R6CP_RB_BASE));
    v9x_r6_cp_log_hex(file, "CP_RB_CNTL=", v9x_r6_cp_read32(R6CP_RB_CNTL));
    v9x_r6_cp_log_hex(file, "CP_ME_CNTL=",
                      v9x_r6_cp_read32(V9X_R6_CP_ME_CNTL));
    v9x_r6_cp_log_hex(file, "CP_STAT=", v9x_r6_cp_read32(V9X_R6_CP_STAT));
    v9x_r6_cp_log_hex(file, "GRBM_STATUS=",
                      v9x_r6_cp_read32(V9X_R6_GRBM_STATUS));
    v9x_r6_cp_log_hex(file, "SCRATCH_REG0=",
                      v9x_r6_cp_read32(V9X_R6_SCRATCH_REG0));
    v9x_r6_cp_log_hex(file, "WindowGpu=", v9x_r6_cp_window_gpu);
    v9x_r6_cp_log_hex(file, "HeapGpu=", v9x_r6_cp_heap_gpu);
    v9x_r6_cp_log_text(file, "[Ring]\r\n");
    for (index = 0ul; index < R6CP_RING_DWORDS; ++index) {
        v9x_r6_cp_log_hex(file, "", v9x_r6_cp_window(R6CP_WINDOW_RING)[index]);
    }
    CloseHandle(file);
}

static void v9x_r6_cp_halt(void)
{
    v9x_r6_cp_write32(V9X_R6_CP_ME_CNTL, R6CP_HALT);
    v9x_r6_cp_delay(R6CP_SHORT_READS);
}

static void v9x_r6_cp_reset(DWORD blocks)
{
    v9x_r6_cp_write32(V9X_R6_GRBM_SOFT_RESET, blocks);
    (void)v9x_r6_cp_read32(V9X_R6_GRBM_SOFT_RESET);
    v9x_r6_cp_delay(R6CP_RESET_READS);
    v9x_r6_cp_write32(V9X_R6_GRBM_SOFT_RESET, 0ul);
    (void)v9x_r6_cp_read32(V9X_R6_GRBM_SOFT_RESET);
}

/* Gives the CP up for the HAL's life: halted, its blocks reset so a hung
 * pipe is left clean (V9XR63D's end), the fault traced once. */
static void v9x_r6_cp_quarantine(DWORD tag)
{
    DWORD address = ((v9x_r6_cp_read32(V9X_R6_CP_RB_RPTR) & 0xfffful) << 16) |
                    (v9x_r6_cp_wptr & 0xfffful);

    if (v9x_hal->engine.idle_timeouts == 0ul) {
        v9x_r6_cp_write_record(tag);
    }
    v9x_r6_cp_halt();
    v9x_r6_cp_reset(R6CP_GRBM_RESET_BLOCKS);
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(tag, address);
    }
    v9x_r6_cp_state = R6CP_FAILED;
    v9x_r6_cp_dirty = 0;
}

/* Moves the write pointer after the CPU's writes are out of the
 * write-combining buffers and through HDP. */
static void v9x_r6_cp_kick(void)
{
    v9x_wc_drain();
    v9x_r6_cp_write32(V9X_R6_HDP_MEM_COHERENCY_FLUSH, 1ul);
    v9x_r6_cp_write32(R6CP_RB_WPTR, v9x_r6_cp_wptr);
}

/* Dwords the CP has not yet read, between its read pointer and ours. */
static DWORD v9x_r6_cp_pending(void)
{
    DWORD rptr = v9x_r6_cp_read32(V9X_R6_CP_RB_RPTR) &
                 (R6CP_RING_DWORDS - 1ul);

    return (v9x_r6_cp_wptr - rptr) & (R6CP_RING_DWORDS - 1ul);
}

/*
 * count dwords into the ring, which the caller has padded to the CP's
 * 16-dword fetch, then the write pointer. A stream that would cross the
 * ring's end is put after type-2 padding to it, from the start; the room
 * is waited for. Non-zero on success.
 */
static int v9x_r6_cp_put(const v9x_u32 *dwords, DWORD count)
{
    volatile DWORD *ring = v9x_r6_cp_window(R6CP_WINDOW_RING);
    DWORD needed = count;
    DWORD spins;
    DWORD index;

    if (count == 0ul || count >= R6CP_RING_DWORDS / 2ul ||
        (count % V9X_R6_CP_ALIGN_DWORDS) != 0ul) {
        return 0;
    }
    if (v9x_r6_cp_wptr + count > R6CP_RING_DWORDS) {
        needed += R6CP_RING_DWORDS - v9x_r6_cp_wptr;
    }
    for (spins = 0ul; spins < R6CP_SPINS; ++spins) {
        if (R6CP_RING_DWORDS - 1ul - v9x_r6_cp_pending() >= needed) {
            break;
        }
    }
    if (spins == R6CP_SPINS) {
        return 0;
    }
    if (v9x_r6_cp_wptr + count > R6CP_RING_DWORDS) {
        while (v9x_r6_cp_wptr < R6CP_RING_DWORDS) {
            ring[v9x_r6_cp_wptr++] = V9X_R6_PACKET2;
        }
        v9x_r6_cp_wptr = 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        ring[v9x_r6_cp_wptr++] = dwords[index];
    }
    v9x_r6_cp_wptr &= R6CP_RING_DWORDS - 1ul;
    v9x_r6_cp_kick();
    return 1;
}

/* All of it read and the GUI idle. */
static int v9x_r6_cp_poll_idle(DWORD reads)
{
    while (reads-- != 0ul) {
        if (v9x_r6_cp_pending() == 0ul &&
            (v9x_r6_cp_read32(V9X_R6_GRBM_STATUS) & R6CP_GUI_ACTIVE) == 0ul) {
            return 1;
        }
    }
    return 0;
}

/* One stream through the ring, padded, and waited for. */
static int v9x_r6_cp_run(struct v9x_r6_pm4 *stream)
{
    v9x_r6_pm4_pad(stream);
    if (stream->overflow != 0ul ||
        !v9x_r6_cp_put(stream->dwords, stream->count)) {
        return 0;
    }
    return v9x_r6_cp_poll_idle(R6CP_SPINS);
}

/* r600_cp_stop, r600_cp_load_microcode and r600_cp_resume, in V9XR63D's
 * order. */
static void v9x_r6_cp_load(void)
{
    DWORD ring_gpu = v9x_r6_cp_window_gpu + R6CP_WINDOW_RING;
    DWORD writeback = v9x_r6_cp_window_gpu + R6CP_WINDOW_WRITEBACK;
    DWORD index;

    v9x_r6_cp_halt();
    v9x_r6_cp_write32(R6CP_SCRATCH_UMSK, 0ul);

    v9x_r6_cp_write32(R6CP_RB_CNTL, R6CP_RB_CNTL_LOADING);
    v9x_r6_cp_reset(R6CP_GRBM_RESET_CP);
    v9x_r6_cp_write32(R6CP_ME_RAM_WADDR, 0ul);
    for (index = 0ul; index < V9X_R6_ME_MICROCODE_DWORDS; ++index) {
        v9x_r6_cp_write32(R6CP_ME_RAM_DATA, v9x_r6_rv630_me_microcode[index]);
    }
    v9x_r6_cp_write32(R6CP_PFP_UCODE_ADDR, 0ul);
    for (index = 0ul; index < V9X_R6_PFP_MICROCODE_DWORDS; ++index) {
        v9x_r6_cp_write32(R6CP_PFP_UCODE_DATA,
                          v9x_r6_rv630_pfp_microcode[index]);
    }
    v9x_r6_cp_write32(R6CP_PFP_UCODE_ADDR, 0ul);
    v9x_r6_cp_write32(R6CP_ME_RAM_WADDR, 0ul);
    v9x_r6_cp_write32(R6CP_ME_RAM_RADDR, 0ul);

    v9x_r6_cp_reset(R6CP_GRBM_RESET_CP);
    v9x_r6_cp_write32(R6CP_RB_CNTL, R6CP_RB_CNTL_VALUE);
    v9x_r6_cp_write32(R6CP_SEM_WAIT_TIMER, 0ul);
    v9x_r6_cp_write32(R6CP_RB_WPTR_DELAY, 0ul);
    v9x_r6_cp_write32(R6CP_RB_CNTL, R6CP_RB_CNTL_VALUE | R6CP_RB_RPTR_WR_ENA);
    v9x_r6_cp_write32(R6CP_RB_RPTR_WR, 0ul);
    v9x_r6_cp_write32(R6CP_RB_WPTR, 0ul);
    v9x_r6_cp_write32(R6CP_RB_RPTR_ADDR, writeback & ~3ul);
    v9x_r6_cp_write32(R6CP_RB_RPTR_ADDR_HI, 0ul);
    v9x_r6_cp_write32(R6CP_SCRATCH_ADDR, (writeback + 0x100ul) >> 8);
    v9x_r6_cp_write32(R6CP_SCRATCH_UMSK, 0ul);
    v9x_r6_cp_delay(R6CP_SHORT_READS);
    v9x_r6_cp_write32(R6CP_RB_CNTL, R6CP_RB_CNTL_VALUE | R6CP_RB_NO_UPDATE);
    v9x_r6_cp_write32(R6CP_RB_BASE, ring_gpu >> 8);
    v9x_r6_cp_write32(R6CP_DEBUG, R6CP_DEBUG_VALUE);
    v9x_r6_cp_wptr = 0ul;
}

/* The Direct3D shaders into their slots (r6_shader.h, V9X_R6_SLOT_*).
 * Zero if one could not be made, which leaves Direct3D refused. */
static int v9x_r6_cp_upload_shaders(void)
{
    v9x_u32 words[V9X_R6_SHADER_DWORDS_MAX];
    volatile DWORD *slot;
    DWORD count;
    DWORD which;
    DWORD index;

    for (which = 0ul; which < V9X_R6_SLOT_COUNT; ++which) {
        if (which == V9X_R6_SLOT_VS) {
            count = v9x_r6_shader_d3d_vs(0ul, words);
        } else if (which == V9X_R6_SLOT_VS_TEXTURED) {
            count = v9x_r6_shader_d3d_vs(1ul, words);
        } else if (which == V9X_R6_SLOT_VS_LIT) {
            count = v9x_r6_shader_d3d_vs_lit(0ul, words);
        } else if (which == V9X_R6_SLOT_VS_LIT_TEXTURED) {
            count = v9x_r6_shader_d3d_vs_lit(1ul, words);
        } else {
            count = v9x_r6_shader_d3d_ps(which - V9X_R6_SLOT_PS_FIRST, words);
        }
        if (count == 0ul || count > R6CP_SHADER_BYTES / 4ul) {
            return 0;
        }
        slot = v9x_r6_cp_window(R6CP_WINDOW_SHADERS +
                                which * R6CP_SHADER_BYTES);
        for (index = 0ul; index < count; ++index) {
            slot[index] = words[index];
        }
    }
    return 1;
}

/*
 * Where the window and the heap are in the GPU's address space, from
 * MC_VM_FB_LOCATION, and the inactive pipes the default state needs: read
 * by MMIO, so the draw's mapping and accepts() have them before the CP is
 * started (Evergreen's first draws were mapped before the start and sent
 * the shaders' addresses as offsets from zero, eg_cp.c). R6xx's addresses
 * are 32 bits: a start above 4 GiB is refused. Non-zero once located.
 */
static int v9x_r6_cp_locate(void)
{
    v9x_u32 fb_high;
    v9x_u32 fb_low;

    if (v9x_r6_cp_window_gpu != 0ul) {
        return 1;
    }
    if (!v9x_r6_cp_mapped()) {
        return 0;
    }
    if (v9x_r6_fb_start(v9x_r6_cp_read32(V9X_R6_MC_VM_FB_LOCATION), &fb_high,
                        &fb_low) != V9X_STATUS_OK || fb_high != 0ul) {
        return 0;
    }
    v9x_r6_cp_pipe_config =
        v9x_r6_cp_read32(V9X_R6_CC_GC_SHADER_PIPE_CONFIG);
    v9x_r6_cp_heap_gpu = fb_low +
        (v9x_hal->fb.physical_base & R6CP_BAR0_MASK);
    v9x_r6_cp_window_gpu = fb_low + V9X_R6_RING_WINDOW_OFFSET;
    return 1;
}

/* r600_gpu_init's configuration through the ring, each entry read first
 * (V9XR63D's form). */
static int v9x_r6_cp_gpu_init(v9x_u32 *buffer, DWORD capacity)
{
    v9x_u32 reads[V9X_R6_RV630_GPU_INIT_COUNT];
    struct v9x_r6_pm4 stream;
    DWORD index;

    for (index = 0ul; index < V9X_R6_RV630_GPU_INIT_COUNT; ++index) {
        reads[index] = v9x_r6_cp_read32(v9x_r6_rv630_gpu_init[index].offset);
    }
    v9x_r6_pm4_begin(&stream, buffer, capacity);
    v9x_r6_3d_gpu_init(&stream, reads,
                       v9x_r6_rv630_tiling_config(
                           v9x_r6_cp_read32(V9X_R6_RAMCFG)));
    return v9x_r6_cp_run(&stream);
}

/* The bring-up, once. */
static int v9x_r6_cp_start(void)
{
    v9x_u32 buffer[R6CP_START_DWORDS];
    struct v9x_r6_pm4 stream;

    if (v9x_r6_cp_state == R6CP_RUNNING) {
        return 1;
    }
    if (v9x_r6_cp_state == R6CP_FAILED || !v9x_r6_cp_mapped()) {
        return 0;
    }
    v9x_r6_cp_state = R6CP_FAILED;
    if (!v9x_r6_cp_locate() || !v9x_r6_cp_upload_shaders()) {
        return 0;
    }
    v9x_r6_cp_load();

    /* r600_cp_start: ME_INITIALIZE queued, then the CP run, and waited for. */
    v9x_r6_pm4_begin(&stream, buffer, R6CP_START_DWORDS);
    v9x_r6_3d_me_initialize(&stream);
    v9x_r6_pm4_pad(&stream);
    if (!v9x_r6_cp_put(stream.dwords, stream.count)) {
        v9x_r6_cp_quarantine(R6CP_FAULT_START);
        return 0;
    }
    v9x_r6_cp_write32(V9X_R6_CP_ME_CNTL, R6CP_RUN);
    if (!v9x_r6_cp_poll_idle(R6CP_SPINS)) {
        v9x_r6_cp_quarantine(R6CP_FAULT_START);
        return 0;
    }

    /* r600_ring_test. */
    v9x_r6_cp_write32(V9X_R6_SCRATCH_REG0, R6CP_SCRATCH_SEED);
    v9x_r6_pm4_begin(&stream, buffer, R6CP_START_DWORDS);
    v9x_r6_3d_fence(&stream, R6CP_SCRATCH_TEST);
    v9x_r6_pm4_pad(&stream);
    if (!v9x_r6_cp_put(stream.dwords, stream.count) ||
        !v9x_r6_cp_wait(V9X_R6_SCRATCH_REG0, 0xfffffffful,
                        R6CP_SCRATCH_TEST)) {
        v9x_r6_cp_quarantine(R6CP_FAULT_TEST);
        return 0;
    }

    if (!v9x_r6_cp_gpu_init(buffer, R6CP_START_DWORDS)) {
        v9x_r6_cp_quarantine(R6CP_FAULT_START);
        return 0;
    }
    v9x_r6_cp_vertex_at = 0ul;
    v9x_r6_cp_dirty = 0;
    v9x_r6_cp_state = R6CP_RUNNING;
    return 1;
}

int v9x_r6_cp_ready(void)
{
    return v9x_r6_cp_mapped() && v9x_r6_cp_state != R6CP_FAILED;
}

int v9x_r6_cp_submit(const v9x_u32 *dwords, DWORD count)
{
    if (dwords == 0 || !v9x_r6_cp_start()) {
        return 0;
    }
    if (!v9x_r6_dma_settle()) {
        return 0;
    }
    if (!v9x_r6_cp_put(dwords, count)) {
        v9x_r6_cp_quarantine(R6CP_FAULT_PUT);
        return 0;
    }
    v9x_r6_cp_dirty = 1;
    return 1;
}

int v9x_r6_cp_drain(int wait)
{
    if (v9x_r6_cp_state != R6CP_RUNNING || !v9x_r6_cp_dirty ||
        !v9x_r6_cp_mapped()) {
        return 1;
    }
    if (!v9x_r6_cp_poll_idle(wait ? R6CP_SPINS : R6CP_QUICK_READS)) {
        if (wait) {
            v9x_r6_cp_quarantine(R6CP_FAULT_DRAIN);
        }
        return 0;
    }
    v9x_r6_cp_write32(V9X_R6_HDP_MEM_COHERENCY_FLUSH, 1ul);
    v9x_r6_cp_dirty = 0;
    return 1;
}

int v9x_r6_cp_vertex_space(DWORD bytes, volatile DWORD **linear, DWORD *gpu)
{
    DWORD rounded = (bytes + 0xfful) & ~0xfful;

    if (bytes == 0ul || rounded > R6CP_VERTEX_BYTES || !v9x_r6_cp_start()) {
        return 0;
    }
    /* At the arena's end, everything queued is drained before its start
     * is written again. */
    if (v9x_r6_cp_vertex_at + rounded > R6CP_VERTEX_BYTES) {
        if (!v9x_r6_cp_drain(1)) {
            return 0;
        }
        v9x_r6_cp_vertex_at = 0ul;
    }
    *linear = v9x_r6_cp_window(R6CP_WINDOW_VERTICES + v9x_r6_cp_vertex_at);
    *gpu = v9x_r6_cp_window_gpu + R6CP_WINDOW_VERTICES + v9x_r6_cp_vertex_at;
    v9x_r6_cp_vertex_at += rounded;
    return 1;
}

int v9x_r6_cp_addresses(DWORD *heap_gpu, DWORD *pipe_config)
{
    if (!v9x_r6_cp_locate()) {
        return 0;
    }
    *heap_gpu = v9x_r6_cp_heap_gpu;
    *pipe_config = v9x_r6_cp_pipe_config;
    return 1;
}

DWORD v9x_r6_cp_shader_gpu(DWORD which)
{
    return v9x_r6_cp_window_gpu + R6CP_WINDOW_SHADERS +
           which * R6CP_SHADER_BYTES;
}
