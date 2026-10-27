/*
 * The AMD Evergreen (Cedar) command processor for V9XHAL.DLL: started for
 * the first Direct3D batch, left running, and drained by whatever needs
 * what it drew.
 *
 * Bring-up repeats V9XEG3D's sequence, which ran on M-BXMFA8's HD 5450
 * (docs\decisions\2026-10-26-cedar-cp-runs-amd-microcode.md and the 3D
 * records after it): evergreen_gpu_init's writes for Cedar (eg_3d.c's
 * table), the CP halted, AMD's PFP and ME microcode loaded
 * (eg_cp_microcode.c; the licence is in NOTICES.TXT), a GRBM reset of the
 * CP blocks, the ring registers as evergreen_cp_resume sets them,
 * ME_INITIALIZE, the clear-state preamble, and a scratch-register ring
 * test. A failed step leaves the CP halted and Direct3D refused for the
 * rest of the HAL's life; DirectDraw on the DMA engine does not depend on
 * it.
 *
 * The ring, its write-back slots and the shaders live in the VRAM window
 * the mini-VDD maps at BAR0 + 240 MiB (eg_3d.h, V9X_EG_WINDOW_*), beside
 * the DMA ring; the vertex arena is its top 2 MiB. The CPU writes all of
 * them through BAR0, so HDP is flushed before the write pointer moves, and
 * again once the CP is idle having drawn, before the CPU reads (the DMA
 * engine's rule, eng_eg.c).
 *
 * The two engines share VRAM and run independently, so each waits for the
 * other: a batch waits for DMA work still queued (v9x_eg_dma_settle), and
 * eng_eg.c drains the CP before its fills and copies and in its wait_idle,
 * which Flip and Lock reach.
 */
#include "ddhal_internal.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/eg_dma.h"
#include "velocity9x/eg_3d.h"
#include "velocity9x/eg_cp_microcode.h"
#include "velocity9x/eg_shader.h"

/* Bounded waits, as eng_eg.c's: the probe's waits landed within a few
 * thousand reads. */
#define V9X_EG_CP_SPINS       0x00200000ul
#define V9X_EG_CP_QUICK_READS 16ul
/* Register reads standing in for udelay: about a microsecond each. The
 * probe's GRBM reset waited 20000 and its ring setup 2000. */
#define V9X_EG_CP_RESET_READS 20000ul
#define V9X_EG_CP_SHORT_READS 2000ul

/* The ring test's values, as V9XEG3D's. */
#define V9X_EG_CP_SCRATCH_SEED 0xcafedeadul
#define V9X_EG_CP_SCRATCH_TEST 0xdeadbeeful

/* BAR0 is 256 MiB aligned: a physical address's offset into it. */
#define V9X_EG_CP_BAR0_MASK 0x0ffffffful

/* Fault-trace tags, "EG3" and where the CP was given up: S at start-up,
 * P waiting for ring space, D draining, R the ring test. The address is
 * the CP's read pointer in the high half and ours in the low. */
#define V9X_EG_CP_FAULT_START 0x45473353ul
#define V9X_EG_CP_FAULT_PUT   0x45473350ul
#define V9X_EG_CP_FAULT_DRAIN 0x45473344ul
#define V9X_EG_CP_FAULT_TEST  0x45473352ul

#define V9X_EG_CP_NOT_TRIED 0
#define V9X_EG_CP_RUNNING   1
#define V9X_EG_CP_FAILED    2

/* One CP, so one set of statics: V9XHAL's statics are shared by every
 * process, which is what these need. The write pointer is in dwords. */
static int v9x_eg_cp_state = V9X_EG_CP_NOT_TRIED;
static DWORD v9x_eg_cp_wptr;
static DWORD v9x_eg_cp_window_gpu;
static DWORD v9x_eg_cp_heap_gpu;
static DWORD v9x_eg_cp_vertex_at;
static int v9x_eg_cp_dirty;

static DWORD v9x_eg_cp_read32(DWORD offset)
{
    return *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

static void v9x_eg_cp_write32(DWORD offset, DWORD value)
{
    *(volatile DWORD *)(v9x_hal->engine.control_linear_base + offset) = value;
}

static volatile DWORD *v9x_eg_cp_window(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.ring_linear_base + offset);
}

static void v9x_eg_cp_delay(DWORD reads)
{
    while (reads-- != 0ul) {
        (void)v9x_eg_cp_read32(V9X_EG_GRBM_STATUS);
    }
}

/* Waits for a register to hold value under mask; non-zero when it did. */
static int v9x_eg_cp_wait(DWORD offset, DWORD mask, DWORD value)
{
    DWORD spins;

    for (spins = 0ul; spins < V9X_EG_CP_SPINS; ++spins) {
        if ((v9x_eg_cp_read32(offset) & mask) == value) {
            return 1;
        }
    }
    return 0;
}

/* The window mapped for the 3D layout, on this card's engine. */
static int v9x_eg_cp_mapped(void)
{
    return v9x_hal != 0 &&
        (v9x_hal->fb.flags & V9X_DD_FB_VALID) != 0ul &&
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) != 0ul &&
        v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_EVERGREEN &&
        (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul &&
        v9x_hal->engine.control_linear_base != 0ul &&
        v9x_hal->engine.mapped_aperture_bytes >= V9X_EG_MMIO_BYTES &&
        v9x_hal->engine.ring_linear_base != 0ul &&
        v9x_hal->engine.ring_bytes >= V9X_EG_WINDOW_BYTES;
}

/*
 * The last submissions, for the record written when the CP is given up:
 * where each went, how long it was, and the CP's read pointer as the put
 * found it. A ring that stalls is otherwise one pair of pointers with no
 * history.
 */
#define V9X_EG_CP_LOG_ENTRIES 16ul
#define V9X_EG_CP_LOG_PATH V9X_DIAG_PATH("V9XEGCP.TXT")
#define V9X_EG_CP_LOG_VERTEX_DWORDS 96ul

typedef struct v9x_eg_cp_log_entry {
    DWORD wptr_before;
    DWORD count;
    DWORD rptr_seen;
    DWORD wptr_after;
} V9X_EG_CP_LOG_ENTRY;

static V9X_EG_CP_LOG_ENTRY v9x_eg_cp_log[V9X_EG_CP_LOG_ENTRIES];
static DWORD v9x_eg_cp_log_next;

static void v9x_eg_cp_log_text(HANDLE file, const char *text)
{
    DWORD written;
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    (void)WriteFile(file, text, length, &written, 0);
}

static void v9x_eg_cp_log_hex(HANDLE file, const char *key, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[12];
    DWORD index;

    v9x_eg_cp_log_text(file, key);
    for (index = 0ul; index < 8ul; ++index) {
        text[index] = digits[(value >> ((7ul - index) * 4ul)) & 0xful];
    }
    text[8] = '\r';
    text[9] = '\n';
    text[10] = '\0';
    v9x_eg_cp_log_text(file, text);
}

/* The registers and the submission log, once, at the first quarantine. */
static void v9x_eg_cp_write_record(DWORD tag)
{
    HANDLE file;
    DWORD index;
    DWORD slot;

    file = CreateFileA(V9X_EG_CP_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    v9x_eg_cp_log_text(file, "[EvergreenCp]\r\n");
    v9x_eg_cp_log_hex(file, "Tag=", tag);
    v9x_eg_cp_log_hex(file, "Wptr=", v9x_eg_cp_wptr);
    v9x_eg_cp_log_hex(file, "CP_RB_RPTR=", v9x_eg_cp_read32(V9X_EG_CP_RB_RPTR));
    v9x_eg_cp_log_hex(file, "CP_RB_WPTR=", v9x_eg_cp_read32(V9X_EG_CP_RB_WPTR));
    v9x_eg_cp_log_hex(file, "CP_RB_BASE=", v9x_eg_cp_read32(V9X_EG_CP_RB_BASE));
    v9x_eg_cp_log_hex(file, "CP_RB_CNTL=", v9x_eg_cp_read32(V9X_EG_CP_RB_CNTL));
    v9x_eg_cp_log_hex(file, "CP_ME_CNTL=", v9x_eg_cp_read32(V9X_EG_CP_ME_CNTL));
    v9x_eg_cp_log_hex(file, "CP_STAT=", v9x_eg_cp_read32(V9X_EG_CP_STAT));
    v9x_eg_cp_log_hex(file, "GRBM_STATUS=",
                      v9x_eg_cp_read32(V9X_EG_GRBM_STATUS));
    v9x_eg_cp_log_hex(file, "SCRATCH_REG0=",
                      v9x_eg_cp_read32(V9X_EG_SCRATCH_REG0));
    v9x_eg_cp_log_hex(file, "Submissions=", v9x_eg_cp_log_next);
    for (index = 0ul; index < V9X_EG_CP_LOG_ENTRIES; ++index) {
        if (v9x_eg_cp_log_next < V9X_EG_CP_LOG_ENTRIES - index) {
            continue;
        }
        slot = (v9x_eg_cp_log_next - V9X_EG_CP_LOG_ENTRIES + index) %
               V9X_EG_CP_LOG_ENTRIES;
        v9x_eg_cp_log_hex(file, "PutWptrBefore=",
                          v9x_eg_cp_log[slot].wptr_before);
        v9x_eg_cp_log_hex(file, "PutCount=", v9x_eg_cp_log[slot].count);
        v9x_eg_cp_log_hex(file, "PutRptrSeen=",
                          v9x_eg_cp_log[slot].rptr_seen);
        v9x_eg_cp_log_hex(file, "PutWptrAfter=",
                          v9x_eg_cp_log[slot].wptr_after);
    }
    /* The ring as the CP has it, and the arena's first vertices: enough
     * to replay the batch that stalled. */
    v9x_eg_cp_log_text(file, "[Ring]\r\n");
    for (index = 0ul; index < V9X_EG_CP_RING_DWORDS; ++index) {
        v9x_eg_cp_log_hex(file, "",
                          v9x_eg_cp_window(V9X_EG_WINDOW_CP_RING)[index]);
    }
    v9x_eg_cp_log_text(file, "[Vertices]\r\n");
    for (index = 0ul; index < V9X_EG_CP_LOG_VERTEX_DWORDS; ++index) {
        v9x_eg_cp_log_hex(file, "",
                          v9x_eg_cp_window(V9X_EG_WINDOW_VERTICES)[index]);
    }
    CloseHandle(file);
}

static void v9x_eg_cp_halt(void)
{
    v9x_eg_cp_write32(V9X_EG_CP_ME_CNTL, V9X_EG_CP_HALT);
    v9x_eg_cp_delay(V9X_EG_CP_SHORT_READS);
}

/* Gives the CP up for the HAL's life: halted, its blocks reset so a hung
 * pipe is left clean, the fault traced once with the pointers as they
 * stood. */
static void v9x_eg_cp_quarantine(DWORD tag)
{
    DWORD address = ((v9x_eg_cp_read32(V9X_EG_CP_RB_RPTR) & 0xfffful) << 16) |
                    (v9x_eg_cp_wptr & 0xfffful);

    if (v9x_hal->engine.idle_timeouts == 0ul) {
        v9x_eg_cp_write_record(tag);
    }
    v9x_eg_cp_halt();
    v9x_eg_cp_write32(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    (void)v9x_eg_cp_read32(V9X_EG_GRBM_SOFT_RESET);
    v9x_eg_cp_delay(V9X_EG_CP_RESET_READS);
    v9x_eg_cp_write32(V9X_EG_GRBM_SOFT_RESET, 0ul);
    (void)v9x_eg_cp_read32(V9X_EG_GRBM_SOFT_RESET);
    if (v9x_hal->engine.idle_timeouts++ == 0ul) {
        v9x_trace_flush_fault(tag, address);
    }
    v9x_eg_cp_state = V9X_EG_CP_FAILED;
    v9x_eg_cp_dirty = 0;
}

/* Moves the write pointer after the CPU's writes are out of the
 * write-combining buffers and through HDP. */
static void v9x_eg_cp_kick(void)
{
    v9x_wc_drain();
    v9x_eg_cp_write32(V9X_EG_HDP_MEM_COHERENCY_FLUSH, 1ul);
    v9x_eg_cp_write32(V9X_EG_CP_RB_WPTR, v9x_eg_cp_wptr);
}

/* Dwords the CP has not yet read, between its read pointer and ours. */
static DWORD v9x_eg_cp_pending(void)
{
    DWORD rptr = v9x_eg_cp_read32(V9X_EG_CP_RB_RPTR) &
                 (V9X_EG_CP_RING_DWORDS - 1ul);

    return (v9x_eg_cp_wptr - rptr) & (V9X_EG_CP_RING_DWORDS - 1ul);
}

/*
 * count dwords into the ring, which the caller has padded to the CP's
 * 16-dword fetch, then the write pointer. A stream that would cross the
 * ring's end is put after type-2 padding to it, from the start; the room
 * is waited for, as the read pointer leaves it. Non-zero on success.
 */
static int v9x_eg_cp_put(const v9x_u32 *dwords, DWORD count)
{
    volatile DWORD *ring = v9x_eg_cp_window(V9X_EG_WINDOW_CP_RING);
    V9X_EG_CP_LOG_ENTRY *entry;
    DWORD needed = count;
    DWORD spins;
    DWORD index;

    if (count == 0ul || count >= V9X_EG_CP_RING_DWORDS / 2ul ||
        (count % V9X_EG_CP_ALIGN_DWORDS) != 0ul) {
        return 0;
    }
    if (v9x_eg_cp_wptr + count > V9X_EG_CP_RING_DWORDS) {
        needed += V9X_EG_CP_RING_DWORDS - v9x_eg_cp_wptr;
    }
    for (spins = 0ul; spins < V9X_EG_CP_SPINS; ++spins) {
        if (V9X_EG_CP_RING_DWORDS - 1ul - v9x_eg_cp_pending() >= needed) {
            break;
        }
    }
    entry = &v9x_eg_cp_log[v9x_eg_cp_log_next % V9X_EG_CP_LOG_ENTRIES];
    ++v9x_eg_cp_log_next;
    entry->wptr_before = v9x_eg_cp_wptr;
    entry->count = count;
    entry->rptr_seen = v9x_eg_cp_read32(V9X_EG_CP_RB_RPTR);
    entry->wptr_after = 0xfffffffful;
    if (spins == V9X_EG_CP_SPINS) {
        return 0;
    }
    if (v9x_eg_cp_wptr + count > V9X_EG_CP_RING_DWORDS) {
        while (v9x_eg_cp_wptr < V9X_EG_CP_RING_DWORDS) {
            ring[v9x_eg_cp_wptr++] = V9X_EG_PACKET2;
        }
        v9x_eg_cp_wptr = 0ul;
    }
    for (index = 0ul; index < count; ++index) {
        ring[v9x_eg_cp_wptr++] = dwords[index];
    }
    v9x_eg_cp_wptr &= V9X_EG_CP_RING_DWORDS - 1ul;
    entry->wptr_after = v9x_eg_cp_wptr;
    v9x_eg_cp_kick();
    return 1;
}

/* All of it read and the GUI idle. */
static int v9x_eg_cp_poll_idle(DWORD reads)
{
    while (reads-- != 0ul) {
        if (v9x_eg_cp_pending() == 0ul &&
            (v9x_eg_cp_read32(V9X_EG_GRBM_STATUS) & V9X_EG_GUI_ACTIVE) ==
                0ul) {
            return 1;
        }
    }
    return 0;
}

/* evergreen_gpu_init for Cedar, read-modify-write where Linux reads. */
static void v9x_eg_cp_gpu_init(void)
{
    const struct v9x_eg_reg_update *update;
    DWORD index;
    DWORD value;

    for (index = 0ul; index < V9X_EG_CEDAR_GPU_INIT_COUNT; ++index) {
        update = &v9x_eg_cedar_gpu_init[index];
        value = update->set;
        if (update->keep != 0ul) {
            value |= v9x_eg_cp_read32(update->offset) & update->keep;
        }
        v9x_eg_cp_write32(update->offset, value);
    }
}

/* r700_cp_stop, evergreen_cp_load_microcode and evergreen_cp_resume. */
static void v9x_eg_cp_load(DWORD ring_gpu)
{
    DWORD index;

    v9x_eg_cp_halt();
    v9x_eg_cp_write32(V9X_EG_SCRATCH_UMSK, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_RB_CNTL, V9X_EG_RB_CNTL_LOADING);
    v9x_eg_cp_write32(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    for (index = 0ul; index < V9X_EG_PFP_MICROCODE_DWORDS; ++index) {
        v9x_eg_cp_write32(V9X_EG_CP_PFP_UCODE_DATA,
                          v9x_eg_pfp_microcode[index]);
    }
    v9x_eg_cp_write32(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    for (index = 0ul; index < V9X_EG_ME_MICROCODE_DWORDS; ++index) {
        v9x_eg_cp_write32(V9X_EG_CP_ME_RAM_DATA, v9x_eg_me_microcode[index]);
    }
    v9x_eg_cp_write32(V9X_EG_CP_PFP_UCODE_ADDR, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_ME_RAM_WADDR, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_ME_RAM_RADDR, 0ul);

    v9x_eg_cp_write32(V9X_EG_GRBM_SOFT_RESET, V9X_EG_GRBM_RESET_CP_BLOCKS);
    (void)v9x_eg_cp_read32(V9X_EG_GRBM_SOFT_RESET);
    v9x_eg_cp_delay(V9X_EG_CP_RESET_READS);
    v9x_eg_cp_write32(V9X_EG_GRBM_SOFT_RESET, 0ul);
    (void)v9x_eg_cp_read32(V9X_EG_GRBM_SOFT_RESET);

    v9x_eg_cp_write32(V9X_EG_CP_RB_CNTL, V9X_EG_CP_RB_CNTL_VALUE);
    v9x_eg_cp_write32(V9X_EG_CP_SEM_WAIT_TIMER, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_SEM_INCOMPLETE, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_RB_WPTR_DELAY, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_RB_CNTL,
                      V9X_EG_CP_RB_CNTL_VALUE | V9X_EG_RB_RPTR_WR_ENA);
    v9x_eg_cp_write32(V9X_EG_CP_RB_RPTR_WR, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_RB_WPTR, 0ul);
    v9x_eg_cp_write32(V9X_EG_CP_RB_RPTR_ADDR,
                      (v9x_eg_cp_window_gpu + V9X_EG_WINDOW_CP_WRITEBACK) &
                      ~3ul);
    v9x_eg_cp_write32(V9X_EG_CP_RB_RPTR_ADDR_HI, 0ul);
    v9x_eg_cp_write32(V9X_EG_SCRATCH_ADDR,
                      (v9x_eg_cp_window_gpu + V9X_EG_WINDOW_CP_WRITEBACK +
                       0x100ul) >> 8);
    v9x_eg_cp_write32(V9X_EG_SCRATCH_UMSK, 0ul);
    v9x_eg_cp_delay(V9X_EG_CP_SHORT_READS);
    v9x_eg_cp_write32(V9X_EG_CP_RB_CNTL,
                      V9X_EG_CP_RB_CNTL_VALUE | V9X_EG_RB_NO_UPDATE);
    v9x_eg_cp_write32(V9X_EG_CP_RB_BASE, ring_gpu >> 8);
    v9x_eg_cp_write32(V9X_EG_CP_DEBUG, V9X_EG_CP_DEBUG_VALUE);
    v9x_eg_cp_wptr = 0ul;
}

/* The Direct3D shaders into their slots (eg_shader.h, V9X_EG_SLOT_*). */
static void v9x_eg_cp_upload_shaders(void)
{
    v9x_u32 words[V9X_EG_SHADER_DWORDS_MAX];
    volatile DWORD *slot;
    DWORD count;
    DWORD which;
    DWORD index;

    for (which = 0ul; which < V9X_EG_SLOT_COUNT; ++which) {
        if (which == V9X_EG_SLOT_VS) {
            count = v9x_eg_shader_d3d_vs(0ul, words);
        } else if (which == V9X_EG_SLOT_VS_TEXTURED) {
            count = v9x_eg_shader_d3d_vs(1ul, words);
        } else if (which == V9X_EG_SLOT_VS_LIT) {
            count = v9x_eg_shader_d3d_vs_lit(0ul, words);
        } else if (which == V9X_EG_SLOT_VS_LIT_TEXTURED) {
            count = v9x_eg_shader_d3d_vs_lit(1ul, words);
        } else {
            count = v9x_eg_shader_d3d_ps(which - V9X_EG_SLOT_PS_FIRST, words);
        }
        slot = v9x_eg_cp_window(V9X_EG_WINDOW_SHADERS +
                                which * V9X_EG_WINDOW_SHADER_BYTES);
        for (index = 0ul; index < count; ++index) {
            slot[index] = words[index];
        }
    }
}

/* One stream through the ring, padded, and waited for. */
static int v9x_eg_cp_run(struct v9x_eg_pm4 *stream)
{
    v9x_eg_pm4_pad(stream);
    if (stream->overflow != 0ul ||
        !v9x_eg_cp_put(stream->dwords, stream->count)) {
        return 0;
    }
    return v9x_eg_cp_poll_idle(V9X_EG_CP_SPINS);
}

/*
 * Where the window and the heap are in the GPU's address space, from
 * MC_VM_FB_LOCATION: a register read, so the draw's mapping and accepts()
 * have them before the CP is started. The first draw used to be mapped
 * before the start that set them, and sent the shader addresses as offsets
 * from zero: the GPU ran desktop pixels as its shaders and hung the
 * pipeline (M-BXMFA8 boots 8-12, docs\decisions\2026-10-27-cedar-d3d-
 * hal-first-light.md). Non-zero once located.
 */
static int v9x_eg_cp_locate(void)
{
    DWORD fb_start;
    DWORD ok;

    if (v9x_eg_cp_window_gpu != 0ul) {
        return 1;
    }
    if (!v9x_eg_cp_mapped()) {
        return 0;
    }
    fb_start = v9x_eg_fb_start(v9x_eg_cp_read32(V9X_EG_MC_VM_FB_LOCATION),
                               &ok);
    if (ok == 0ul || fb_start > 0xfffffffful - V9X_EG_RING_WINDOW_OFFSET -
                                V9X_EG_WINDOW_BYTES) {
        return 0;
    }
    v9x_eg_cp_heap_gpu = fb_start +
        (v9x_hal->fb.physical_base & V9X_EG_CP_BAR0_MASK);
    v9x_eg_cp_window_gpu = fb_start + V9X_EG_RING_WINDOW_OFFSET;
    return 1;
}

/* The bring-up, once. */
static int v9x_eg_cp_start(void)
{
    v9x_u32 buffer[V9X_EG_CP_ALIGN_DWORDS * 20ul];
    struct v9x_eg_pm4 stream;

    if (v9x_eg_cp_state == V9X_EG_CP_RUNNING) {
        return 1;
    }
    if (v9x_eg_cp_state == V9X_EG_CP_FAILED || !v9x_eg_cp_mapped()) {
        return 0;
    }
    v9x_eg_cp_state = V9X_EG_CP_FAILED;
    if (!v9x_eg_cp_locate()) {
        return 0;
    }

    v9x_eg_cp_gpu_init();
    v9x_eg_cp_load(v9x_eg_cp_window_gpu + V9X_EG_WINDOW_CP_RING);
    v9x_eg_cp_upload_shaders();

    /* evergreen_cp_start: ME_INITIALIZE with the CP running, then the
     * preamble, each waited for. */
    v9x_eg_pm4_begin(&stream, buffer, sizeof(buffer) / sizeof(buffer[0]));
    v9x_eg_3d_me_initialize(&stream);
    v9x_eg_pm4_pad(&stream);
    if (!v9x_eg_cp_put(stream.dwords, stream.count)) {
        v9x_eg_cp_quarantine(V9X_EG_CP_FAULT_START);
        return 0;
    }
    v9x_eg_cp_write32(V9X_EG_CP_ME_CNTL, V9X_EG_CP_RUN);
    if (!v9x_eg_cp_poll_idle(V9X_EG_CP_SPINS)) {
        v9x_eg_cp_quarantine(V9X_EG_CP_FAULT_START);
        return 0;
    }
    v9x_eg_pm4_begin(&stream, buffer, sizeof(buffer) / sizeof(buffer[0]));
    v9x_eg_3d_preamble(&stream);
    if (!v9x_eg_cp_run(&stream)) {
        v9x_eg_cp_quarantine(V9X_EG_CP_FAULT_START);
        return 0;
    }

    /* r600_ring_test. */
    v9x_eg_cp_write32(V9X_EG_SCRATCH_REG0, V9X_EG_CP_SCRATCH_SEED);
    v9x_eg_pm4_begin(&stream, buffer, sizeof(buffer) / sizeof(buffer[0]));
    v9x_eg_3d_fence(&stream, V9X_EG_CP_SCRATCH_TEST);
    v9x_eg_pm4_pad(&stream);
    if (!v9x_eg_cp_put(stream.dwords, stream.count) ||
        !v9x_eg_cp_wait(V9X_EG_SCRATCH_REG0, 0xfffffffful,
                        V9X_EG_CP_SCRATCH_TEST)) {
        v9x_eg_cp_quarantine(V9X_EG_CP_FAULT_TEST);
        return 0;
    }
    v9x_eg_cp_vertex_at = 0ul;
    v9x_eg_cp_dirty = 0;
    v9x_eg_cp_state = V9X_EG_CP_RUNNING;
    return 1;
}

int v9x_eg_cp_ready(void)
{
    return v9x_eg_cp_mapped() && v9x_eg_cp_state != V9X_EG_CP_FAILED;
}

int v9x_eg_cp_submit(const v9x_u32 *dwords, DWORD count)
{
    if (dwords == 0 || !v9x_eg_cp_start()) {
        return 0;
    }
    if (!v9x_eg_dma_settle()) {
        return 0;
    }
    if (!v9x_eg_cp_put(dwords, count)) {
        v9x_eg_cp_quarantine(V9X_EG_CP_FAULT_PUT);
        return 0;
    }
    v9x_eg_cp_dirty = 1;
    return 1;
}

int v9x_eg_cp_drain(int wait)
{
    if (v9x_eg_cp_state != V9X_EG_CP_RUNNING || !v9x_eg_cp_dirty ||
        !v9x_eg_cp_mapped()) {
        return 1;
    }
    if (!v9x_eg_cp_poll_idle(wait ? V9X_EG_CP_SPINS
                                  : V9X_EG_CP_QUICK_READS)) {
        if (wait) {
            v9x_eg_cp_quarantine(V9X_EG_CP_FAULT_DRAIN);
        }
        return 0;
    }
    v9x_eg_cp_write32(V9X_EG_HDP_MEM_COHERENCY_FLUSH, 1ul);
    v9x_eg_cp_dirty = 0;
    return 1;
}

int v9x_eg_cp_vertex_space(DWORD bytes, volatile DWORD **linear,
                           DWORD *gpu)
{
    DWORD rounded = (bytes + 0xfful) & ~0xfful;

    if (bytes == 0ul || rounded > V9X_EG_WINDOW_VERTEX_BYTES ||
        !v9x_eg_cp_start()) {
        return 0;
    }
    /* At the arena's end, everything queued is drained before its start
     * is written again. */
    if (v9x_eg_cp_vertex_at + rounded > V9X_EG_WINDOW_VERTEX_BYTES) {
        if (!v9x_eg_cp_drain(1)) {
            return 0;
        }
        v9x_eg_cp_vertex_at = 0ul;
    }
    *linear = v9x_eg_cp_window(V9X_EG_WINDOW_VERTICES + v9x_eg_cp_vertex_at);
    *gpu = v9x_eg_cp_window_gpu + V9X_EG_WINDOW_VERTICES +
           v9x_eg_cp_vertex_at;
    v9x_eg_cp_vertex_at += rounded;
    return 1;
}

int v9x_eg_cp_addresses(DWORD *heap_gpu)
{
    if (!v9x_eg_cp_locate()) {
        return 0;
    }
    *heap_gpu = v9x_eg_cp_heap_gpu;
    return 1;
}

DWORD v9x_eg_cp_shader_gpu(DWORD which)
{
    return v9x_eg_cp_window_gpu + V9X_EG_WINDOW_SHADERS +
           which * V9X_EG_WINDOW_SHADER_BYTES;
}
