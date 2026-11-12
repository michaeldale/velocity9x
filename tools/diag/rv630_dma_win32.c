/*
 * V9XRVDMA.EXE: the first writes to an ATI Radeon HD 2600 XT (RV630,
 * 1002:9588): one async DMA ring, two submissions, checked dword for dword.
 *
 * Phase 3 of docs\plans\ati-rv630-hd2600xt.md, first step. RVDMA.VXD
 * executes the op lists built here and refuses anything outside its
 * allow-lists. Writes C:\V9XDIAG\V9XRVDMA.INI. The Evergreen probe
 * (evergreen_dma_win32.c) is the model; the run differs where R6xx does:
 *
 *   - The packet header is r600d.h's DMA_PACKET: command 31:28, t bit 23,
 *     s bit 22, count 15:0. There is one COPY form, dword-sized
 *     (r600_copy_dma): header, dst, src, then dst bits 39:32 in 23:16 and
 *     src bits 39:32 in 7:0. No byte copy.
 *   - There is no CONSTANT_FILL ("7xx only", r600d.h). A fill is tested the
 *     way a driver would have to build one: one dword by an inline WRITE
 *     packet (header, dst, dst high, data; r600_dma_ring_test), then copies
 *     doubling the run, 1, 2, 4 ... 128 dwords, each reading what the one
 *     before it wrote.
 *   - Linux's R6xx tables route buffer moves through the CP, not this
 *     engine (radeon_asic.c rv6xx_asic: copy_ring_index is the GFX ring;
 *     RV770 on use the DMA ring). Whether the engine runs at all on this
 *     chip is the first thing measured.
 *   - No DMA_MODE (Linux writes it from RV770 on); the DMA soft-reset bit
 *     is SRBM_SOFT_RESET bit 12; no SRBM_STATUS2. The engine's busy test is
 *     DMA_STATUS_REG bit 0 (DMA_IDLE, r600_gpu_check_soft_reset), reported
 *     here but not used as a gate: on Cedar it read 0 after a clean run.
 *
 * Call 1 is read-only: the DMA block's registers, SRBM_SOFT_RESET and
 * MC_VM_FB_LOCATION. The run goes ahead only if the ring is disabled and
 * the block not in reset, as the register probe read them on M-NZHWBR
 * (docs\decisions\2026-11-13-rv630-register-probe.md). /readonly stops
 * there.
 *
 * Call 2 saves the scratch window (BAR0 + 240 MiB, 64 KiB), lays out
 * buffers and a ring in it, pulses the DMA soft reset (Cedar needed one
 * before every ring session), starts the ring as r600_dma_resume does (no
 * read-pointer writeback, no interrupts), and submits:
 *
 *   1. a copy of 256 dwords, an inline write of 8 dwords, a copy of 64
 *      dwords 4 dwords up its own source (overlapping), and a fence;
 *   2. the doubling fill of 256 dwords, and a second fence. /seed8 seeds
 *      eight dwords instead of one; /serial makes each step a submission
 *      of its own, ending in a fence the CPU waits for. On M-NZHWBR only
 *      both together filled every dword
 *      (docs\decisions\2026-11-13-rv630-dma-write-probe.md).
 *
 * Then the ring is stopped and reset, every register written is put back
 * as call 1 read it, HDP is flushed, the buffers are read back and the
 * scratch window is restored.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RVDMA_PATH       V9X_DIAG_PATH("V9XRVDMA.INI")
#define V9X_RVDMA_VXD        "\\\\.\\RVDMA.VXD"
#define V9X_RVDMA_MAGIC      0x414d4452ul
#define V9X_RVDMA_RUN        1u
#define V9X_RVDMA_OP_MAX     512u
#define V9X_RVDMA_BLOCK      4096u
#define V9X_RVDMA_HEADER     16u
#define V9X_RVDMA_TIMEOUT    0xfffffffful

#define V9X_ST_COMPLETE  0x00000010ul

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

/* SRBM_STATUS reads standing in for r600_gpu_soft_reset's udelay(50): an
 * uncached PCIe read takes on the order of a microsecond, so this is well
 * over 50 us. */
#define V9X_RESET_DELAY_READS 2000ul

/* RVDMA.VXD's RD_PATTERN_STEP. */
#define V9X_PATTERN_STEP    0x9e3779b9ul

/* Registers (r600d.h). */
#define V9X_RV_SRBM_STATUS          0x0e50ul
#define V9X_RV_SRBM_SOFT_RESET      0x0e60ul
#define V9X_RV_MC_VM_FB_LOCATION    0x2180ul
#define V9X_RV_HDP_MEM_FLUSH        0x5480ul
#define V9X_RV_GRBM_STATUS          0x8010ul
#define V9X_RV_DMA_RB_CNTL          0xd000ul
#define V9X_RV_DMA_RB_BASE          0xd004ul
#define V9X_RV_DMA_RB_RPTR          0xd008ul
#define V9X_RV_DMA_RB_WPTR          0xd00cul
#define V9X_RV_DMA_RB_RPTR_ADDR_HI  0xd01cul
#define V9X_RV_DMA_RB_RPTR_ADDR_LO  0xd020ul
#define V9X_RV_DMA_IB_CNTL          0xd024ul
#define V9X_RV_DMA_CNTL             0xd02cul
#define V9X_RV_DMA_STATUS_REG       0xd034ul
#define V9X_RV_DMA_SEM_INCOMPLETE   0xd044ul
#define V9X_RV_DMA_SEM_WAIT_FAIL    0xd048ul

#define V9X_RV_SOFT_RESET_DMA       0x00001000ul
#define V9X_RV_DMA_RB_ENABLE        0x00000001ul
#define V9X_RV_DMA_IB_ENABLE        0x00000001ul
#define V9X_RV_CTXEMPTY_INT_ENABLE  0x10000000ul
#define V9X_RV_FB_START_MASK        0x0000fffful
/* FB_LOCATION's start field is 16 MiB units of the address: its low byte
 * is bits 31:24 and its high byte bits 39:32. The window must not carry
 * out of the low 32 bits, so the low byte stays at EFh or below. */
#define V9X_RV_FB_LOW_MASK          0x000000fful
#define V9X_RV_FB_HIGH_SHIFT        8u
#define V9X_RV_FB_LOW_MAX           0x000000eful
#define V9X_RV_HIGH_MASK            0x000000fful
#define V9X_RB_BASE_HIGH_SHIFT      24u
#define V9X_COPY_DST_HIGH_SHIFT     16u

/* DMA packets, r600d.h. */
#define V9X_DMA_PACKET(cmd, n) \
    ((((DWORD)(cmd) & 0xful) << 28) | ((DWORD)(n) & 0xfffful))
#define V9X_DMA_WRITE           0x2ul
#define V9X_DMA_COPY            0x3ul
#define V9X_DMA_FENCE           0x6ul
#define V9X_DMA_NOP             0xful

/* The scratch window: its GPU address is the FB start plus this, and the
 * layout inside it. Linux pads every DMA submission to 16 dwords with NOPs
 * (the ring's align_mask), so this does too. */
#define V9X_SCRATCH_GPU_OFFSET  0x0f000000ul
#define V9X_SCRATCH_DWORDS      16384ul
#define V9X_RING_LOG2_DWORDS    10ul
#define V9X_RING_ALIGN_DWORDS   16ul
#define V9X_RING_MAX_DWORDS     256ul
#define V9X_RING                0x0000ul
#define V9X_FENCE_1             0x1000ul
#define V9X_FENCE_2             0x1004ul
#define V9X_FENCE_STEP          0x1100ul
#define V9X_COPY_SRC            0x2000ul
#define V9X_COPY_DST            0x3000ul
#define V9X_WRITE_DST           0x4000ul
#define V9X_OVERLAP             0x5000ul
#define V9X_FILL_DST            0x6000ul

#define V9X_GUARD_VALUE         0x5a5a5a5aul
#define V9X_GUARD_DWORDS        4ul
#define V9X_RUN_DWORDS          256ul
#define V9X_TARGET_DWORDS       (V9X_RUN_DWORDS + 4ul * V9X_GUARD_DWORDS)
#define V9X_WRITE_DWORDS        8ul
#define V9X_WRITE_TARGET        (V9X_WRITE_DWORDS + 2ul * V9X_GUARD_DWORDS)
#define V9X_OVERLAP_DWORDS      128ul
#define V9X_OVERLAP_RUN         64ul
#define V9X_OVERLAP_SHIFT       4ul
#define V9X_FILL_VALUE          0xc0ffee11ul
#define V9X_FENCE_1_VALUE       0x11111111ul
#define V9X_FENCE_2_VALUE       0x22222222ul
#define V9X_FENCE_STEP_VALUE    0x33000000ul
/* The fill's seed: one dword, or with /seed8 one 32-byte run of eight. */
#define V9X_FILL_SEED_SMALL     1ul
#define V9X_FILL_SEED_LINE      8ul
#define V9X_FILL_STEPS_MAX      9ul
#define V9X_FENCE_WAIT_LIMIT    20000000ul

#define V9X_SEED_COPY           0x10000001ul
#define V9X_SEED_WRITE          0x20000002ul
#define V9X_SEED_OVERLAP        0x30000003ul

struct v9x_rvdma_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

struct v9x_rvdma_request {
    DWORD count;
    struct v9x_rvdma_op ops[V9X_RVDMA_OP_MAX];
};

struct v9x_rvdma_result {
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
    DWORD op_results[V9X_RVDMA_OP_MAX];
    DWORD block[V9X_RVDMA_BLOCK];
};

/* The header must be the VxD's RD_HEADER_DWORDS long. */
typedef char v9x_rvdma_header_matches[
    offsetof(struct v9x_rvdma_result, op_results) ==
    sizeof(DWORD) * V9X_RVDMA_HEADER ? 1 : -1];

/* The registers call 1 reads and the run puts back, in this order. */
static const DWORD v9x_saved_offsets[] = {
    V9X_RV_DMA_RB_CNTL, V9X_RV_DMA_RB_BASE, V9X_RV_DMA_RB_RPTR,
    V9X_RV_DMA_RB_WPTR, V9X_RV_DMA_RB_RPTR_ADDR_HI,
    V9X_RV_DMA_RB_RPTR_ADDR_LO, V9X_RV_DMA_IB_CNTL, V9X_RV_DMA_CNTL,
    V9X_RV_DMA_SEM_INCOMPLETE, V9X_RV_DMA_SEM_WAIT_FAIL
};
#define V9X_SAVED_COUNT (sizeof(v9x_saved_offsets) / sizeof(v9x_saved_offsets[0]))
#define V9X_SAVED_DMA_CNTL 7u

static const char * const v9x_saved_names[V9X_SAVED_COUNT] = {
    "DMA_RB_CNTL", "DMA_RB_BASE", "DMA_RB_RPTR", "DMA_RB_WPTR",
    "DMA_RB_RPTR_ADDR_HI", "DMA_RB_RPTR_ADDR_LO", "DMA_IB_CNTL", "DMA_CNTL",
    "DMA_SEM_INCOMPLETE_TIMER_CNTL", "DMA_SEM_WAIT_FAIL_TIMER_CNTL"
};

static struct v9x_rvdma_request v9x_request;
static struct v9x_rvdma_result v9x_result;
static DWORD v9x_saved[V9X_SAVED_COUNT];
static DWORD v9x_ring[V9X_RING_MAX_DWORDS];
static DWORD v9x_ring_count;
/* Bits 39:32 of the scratch window's GPU address, for every packet. */
static DWORD v9x_gpu_high;
/* SRBM_SOFT_RESET as call 1 read it; its DMA bit is clear or the run stops. */
static DWORD v9x_srbm_reset;

/* The fill's shape, from the command line. Run 1 put a one-dword WRITE and
 * all its doubling copies in one submission and only dword 128 came out
 * filled; /seed8 seeds a whole 32-byte run, /serial submits each step
 * alone and waits for its fence before the next. */
static DWORD v9x_fill_seed;
static int v9x_fill_serial;
static DWORD v9x_step_count;
static DWORD v9x_step_wptr[V9X_FILL_STEPS_MAX];
static DWORD v9x_at_step_wait[V9X_FILL_STEPS_MAX];

/* Indices into v9x_result.op_results of the values the report needs. */
static DWORD v9x_at_reset_status;
static DWORD v9x_at_teardown_status;
static DWORD v9x_at_enabled_cntl;
static DWORD v9x_at_enabled_status;
static DWORD v9x_at_wait_1;
static DWORD v9x_at_rptr_1;
static DWORD v9x_at_status_1;
static DWORD v9x_at_wait_2;
static DWORD v9x_at_rptr_2;
static DWORD v9x_at_status_2;
static DWORD v9x_at_after_cntl;
static DWORD v9x_at_after_status;
static DWORD v9x_at_after_reset;
static DWORD v9x_at_after_srbm;
static DWORD v9x_at_after_grbm;

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
    WritePrivateProfileStringA(section, key, value, V9X_RVDMA_PATH);
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

static int v9x_dioc(HANDLE device)
{
    DWORD returned;
    BOOL ok;

    returned = 0ul;
    ok = DeviceIoControl(device, V9X_RVDMA_RUN, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_RVDMA_MAGIC) {
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

/* Ring building: packets, then NOPs to the 16-dword boundary. Returns the
 * write pointer in bytes. */
static void v9x_ring_put(DWORD value)
{
    v9x_ring[v9x_ring_count++] = value;
}

static DWORD v9x_ring_close(void)
{
    while ((v9x_ring_count % V9X_RING_ALIGN_DWORDS) != 0ul) {
        v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_NOP, 0ul));
    }
    return v9x_ring_count * 4ul;
}

/* r600_copy_dma's packet. */
static void v9x_ring_copy(DWORD count, DWORD dst, DWORD src)
{
    v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_COPY, count));
    v9x_ring_put(dst);
    v9x_ring_put(src);
    v9x_ring_put((v9x_gpu_high << V9X_COPY_DST_HIGH_SHIFT) | v9x_gpu_high);
}

/* r600_dma_ring_test's packet, with count data dwords from a pattern. */
static void v9x_ring_write(DWORD count, DWORD dst, DWORD seed)
{
    DWORD index;

    v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_WRITE, count));
    v9x_ring_put(dst);
    v9x_ring_put(v9x_gpu_high & V9X_RV_HIGH_MASK);
    for (index = 0ul; index < count; ++index) {
        v9x_ring_put(seed + index * V9X_PATTERN_STEP);
    }
}

/* r600_dma_fence_ring_emit's packet, without the TRAP that follows it
 * there: no interrupt is wanted. */
static void v9x_ring_fence(DWORD address, DWORD value)
{
    v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_FENCE, 0ul));
    v9x_ring_put(address);
    v9x_ring_put(v9x_gpu_high & V9X_RV_HIGH_MASK);
    v9x_ring_put(value);
}

/* r600_gpu_soft_reset for the DMA block alone: ring enable off, a wait,
 * SRBM_SOFT_RESET's DMA bit set and read back, a wait, cleared and read
 * back, a wait. Linux also stops the memory controller around it; this
 * does not. Returns the op index of the status read that follows. */
static DWORD v9x_dma_reset(void)
{
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_CNTL,
           v9x_saved[0] & ~V9X_RV_DMA_RB_ENABLE, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_SRBM_SOFT_RESET,
           v9x_srbm_reset | V9X_RV_SOFT_RESET_DMA, 0ul);
    v9x_op(V9X_OP_MMIO_READ, V9X_RV_SRBM_SOFT_RESET, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_SRBM_SOFT_RESET, v9x_srbm_reset, 0ul);
    v9x_op(V9X_OP_MMIO_READ, V9X_RV_SRBM_SOFT_RESET, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    return v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_STATUS_REG, 0ul, 0ul);
}

static DWORD v9x_pattern(DWORD seed, DWORD index)
{
    return seed + index * V9X_PATTERN_STEP;
}

/* Call 2's op list. g is the scratch window's GPU address. */
static void v9x_build_run(DWORD g)
{
    DWORD wptr_1;
    DWORD wptr_2;
    DWORD index;
    DWORD run;
    DWORD fill;

    v9x_ring_count = 0ul;
    v9x_ring_copy(V9X_RUN_DWORDS, g + V9X_COPY_DST + V9X_GUARD_DWORDS * 4ul,
                  g + V9X_COPY_SRC);
    v9x_ring_write(V9X_WRITE_DWORDS,
                   g + V9X_WRITE_DST + V9X_GUARD_DWORDS * 4ul,
                   V9X_SEED_WRITE);
    v9x_ring_copy(V9X_OVERLAP_RUN, g + V9X_OVERLAP + V9X_OVERLAP_SHIFT * 4ul,
                  g + V9X_OVERLAP);
    v9x_ring_fence(g + V9X_FENCE_1, V9X_FENCE_1_VALUE);
    wptr_1 = v9x_ring_close();

    /* The fill: the seed written inline, then each copy doubles the run
     * from what the packets before it wrote. With /serial every step is
     * its own submission ending in its own fence. */
    fill = g + V9X_FILL_DST + V9X_GUARD_DWORDS * 4ul;
    v9x_step_count = 0ul;
    v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_WRITE, v9x_fill_seed));
    v9x_ring_put(fill);
    v9x_ring_put(v9x_gpu_high & V9X_RV_HIGH_MASK);
    for (index = 0ul; index < v9x_fill_seed; ++index) {
        v9x_ring_put(V9X_FILL_VALUE);
    }
    for (run = v9x_fill_seed; run <= V9X_RUN_DWORDS; run <<= 1) {
        if (v9x_fill_serial) {
            v9x_ring_fence(g + V9X_FENCE_STEP + v9x_step_count * 4ul,
                           V9X_FENCE_STEP_VALUE + v9x_step_count);
            v9x_step_wptr[v9x_step_count++] = v9x_ring_close();
        }
        if (run == V9X_RUN_DWORDS) {
            break;
        }
        v9x_ring_copy(run, fill + run * 4ul, fill);
    }
    v9x_ring_fence(g + V9X_FENCE_2, V9X_FENCE_2_VALUE);
    wptr_2 = v9x_ring_close();

    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_SCRATCH_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_COPY_SRC, V9X_RUN_DWORDS, V9X_SEED_COPY);
    v9x_op(V9X_OP_VRAM_FILL, V9X_COPY_DST, V9X_TARGET_DWORDS,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_FILL, V9X_WRITE_DST, V9X_WRITE_TARGET,
           V9X_GUARD_VALUE);
    v9x_op(V9X_OP_VRAM_PATTERN, V9X_OVERLAP, V9X_OVERLAP_DWORDS,
           V9X_SEED_OVERLAP);
    v9x_op(V9X_OP_VRAM_FILL, V9X_FILL_DST, V9X_TARGET_DWORDS,
           V9X_GUARD_VALUE);
    for (index = 0ul; index < v9x_ring_count; ++index) {
        v9x_op(V9X_OP_VRAM_WRITE, V9X_RING + index * 4ul, v9x_ring[index],
               0ul);
    }

    /* A DMA soft reset first, as Cedar needed before every ring session,
     * then r600_dma_resume's order without writeback or interrupts. */
    v9x_at_reset_status = v9x_dma_reset();
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_SEM_INCOMPLETE, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_SEM_WAIT_FAIL, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_CNTL,
           V9X_RING_LOG2_DWORDS << 1, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_RPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_WPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_RPTR_ADDR_HI, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_RPTR_ADDR_LO, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_BASE,
           (v9x_gpu_high << V9X_RB_BASE_HIGH_SHIFT) | ((g + V9X_RING) >> 8),
           0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_IB_CNTL, V9X_RV_DMA_IB_ENABLE, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_CNTL,
           v9x_saved[V9X_SAVED_DMA_CNTL] & ~V9X_RV_CTXEMPTY_INT_ENABLE, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_WPTR, 0ul, 0ul);
    /* The CPU wrote the ring and buffers through BAR0: flush HDP before the
     * engine reads them. */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_CNTL,
           (V9X_RING_LOG2_DWORDS << 1) | V9X_RV_DMA_RB_ENABLE, 0ul);
    v9x_at_enabled_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_RB_CNTL, 0ul, 0ul);
    v9x_at_enabled_status = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_STATUS_REG,
                                   0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_WPTR, wptr_1, 0ul);
    v9x_at_wait_1 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE_1, V9X_FENCE_1_VALUE,
                           V9X_FENCE_WAIT_LIMIT);
    v9x_at_rptr_1 = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_RB_RPTR, 0ul, 0ul);
    v9x_at_status_1 = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_STATUS_REG,
                             0ul, 0ul);

    for (index = 0ul; index < v9x_step_count; ++index) {
        v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_WPTR, v9x_step_wptr[index],
               0ul);
        v9x_at_step_wait[index] = v9x_op(V9X_OP_WAIT_VRAM,
                                         V9X_FENCE_STEP + index * 4ul,
                                         V9X_FENCE_STEP_VALUE + index,
                                         V9X_FENCE_WAIT_LIMIT);
    }
    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_DMA_RB_WPTR, wptr_2, 0ul);
    v9x_at_wait_2 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE_2, V9X_FENCE_2_VALUE,
                           V9X_FENCE_WAIT_LIMIT);
    v9x_at_rptr_2 = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_RB_RPTR, 0ul, 0ul);
    v9x_at_status_2 = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_STATUS_REG,
                             0ul, 0ul);

    /* Stop the ring and reset the block, so the next user starts from what
     * a reset leaves; then put back what call 1 read. */
    v9x_at_teardown_status = v9x_dma_reset();
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_WRITE, v9x_saved_offsets[index], v9x_saved[index],
               0ul);
    }
    v9x_at_after_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_RB_CNTL, 0ul, 0ul);
    v9x_at_after_status = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_STATUS_REG,
                                 0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_RV_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_FENCE_1, 2ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_COPY_DST, V9X_TARGET_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_WRITE_DST, V9X_WRITE_TARGET, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_OVERLAP, V9X_OVERLAP_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_FILL_DST, V9X_TARGET_DWORDS, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_at_after_reset = v9x_op(V9X_OP_MMIO_READ, V9X_RV_SRBM_SOFT_RESET,
                                0ul, 0ul);
    v9x_at_after_srbm = v9x_op(V9X_OP_MMIO_READ, V9X_RV_SRBM_STATUS, 0ul, 0ul);
    v9x_at_after_grbm = v9x_op(V9X_OP_MMIO_READ, V9X_RV_GRBM_STATUS, 0ul, 0ul);

    v9x_number("Run", "FillSeedDwords", v9x_fill_seed);
    v9x_text("Run", "FillSerial", v9x_fill_serial ? "yes" : "no");
    v9x_number("Run", "FillSteps", v9x_step_count);
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
    const DWORD *written;
    const DWORD *overlap;
    const DWORD *fill;
    DWORD original[V9X_OVERLAP_DWORDS];
    DWORD index;
    int memmove_ok;
    int repeat_ok;

    block = v9x_result.block;
    v9x_key("Result", "Fence1", block[0]);
    v9x_key("Result", "Fence2", block[1]);
    copy = block + 2;
    written = copy + V9X_TARGET_DWORDS;
    overlap = written + V9X_WRITE_TARGET;
    fill = overlap + V9X_OVERLAP_DWORDS;

    /* Copy: guards, the pattern, guards. */
    for (index = 0ul; index < V9X_TARGET_DWORDS; ++index) {
        v9x_expected[index] = V9X_GUARD_VALUE;
    }
    for (index = 0ul; index < V9X_RUN_DWORDS; ++index) {
        v9x_expected[V9X_GUARD_DWORDS + index] =
            v9x_pattern(V9X_SEED_COPY, index);
    }
    v9x_pass("Copy", v9x_check_run("Copy", copy, v9x_expected,
                                   V9X_TARGET_DWORDS));

    /* Inline write: guards, eight pattern dwords, guards. */
    for (index = 0ul; index < V9X_WRITE_TARGET; ++index) {
        v9x_expected[index] = V9X_GUARD_VALUE;
    }
    for (index = 0ul; index < V9X_WRITE_DWORDS; ++index) {
        v9x_expected[V9X_GUARD_DWORDS + index] =
            v9x_pattern(V9X_SEED_WRITE, index);
    }
    v9x_pass("Write", v9x_check_run("Write", written, v9x_expected,
                                    V9X_WRITE_TARGET));

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
    for (index = 0ul; index < 8ul; ++index) {
        char key[16];

        lstrcpyA(key, "OverlapAt");
        v9x_decimal(key + 9, V9X_OVERLAP_SHIFT + index);
        v9x_key("Result", key, overlap[V9X_OVERLAP_SHIFT + index]);
    }

    /* The doubling fill. */
    for (index = 0ul; index < V9X_TARGET_DWORDS; ++index) {
        v9x_expected[index] = V9X_GUARD_VALUE;
    }
    for (index = 0ul; index < V9X_RUN_DWORDS; ++index) {
        v9x_expected[V9X_GUARD_DWORDS + index] = V9X_FILL_VALUE;
    }
    v9x_pass("Fill", v9x_check_run("Fill", fill, v9x_expected,
                                   V9X_TARGET_DWORDS));

    /* The whole fill target, eight dwords a line: a failed fill's shape
     * says which packet went wrong. */
    for (index = 0ul; index < V9X_TARGET_DWORDS; index += 8ul) {
        char key[16];
        char line[80];
        DWORD column;

        lstrcpyA(key, "Fill");
        v9x_decimal(key + 4, index);
        for (column = 0ul; column < 8ul; ++column) {
            v9x_hex(line + column * 9ul, fill[index + column], 8);
            line[column * 9ul + 8ul] = ' ';
        }
        line[71] = '\0';
        v9x_text("FillDump", key, line);
    }
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
    WritePrivateProfileStringA(0, 0, 0, V9X_RVDMA_PATH);
    ExitProcess(code);
}

void __stdcall V9xRv630DmaEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD at_fb;
    DWORD at_reset;
    DWORD at_status;
    DWORD at_grbm;
    DWORD fb;
    DWORD g;
    const DWORD *r;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_RVDMA_PATH);
    v9x_text("Report", "Tool", "V9XRVDMA");
    v9x_text("Report", "Build", V9X_BUILD_ID);

    device = CreateFileA(V9X_RVDMA_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_key("Report", "LoadError", GetLastError());
        v9x_finish("VXD-LOAD-FAILED", 1u);
    }

    /* Call 1: read-only. */
    v9x_request.count = 0ul;
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_READ, v9x_saved_offsets[index], 0ul, 0ul);
    }
    at_status = v9x_op(V9X_OP_MMIO_READ, V9X_RV_DMA_STATUS_REG, 0ul, 0ul);
    at_reset = v9x_op(V9X_OP_MMIO_READ, V9X_RV_SRBM_SOFT_RESET, 0ul, 0ul);
    at_fb = v9x_op(V9X_OP_MMIO_READ, V9X_RV_MC_VM_FB_LOCATION, 0ul, 0ul);
    at_grbm = v9x_op(V9X_OP_MMIO_READ, V9X_RV_GRBM_STATUS, 0ul, 0ul);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    v9x_key("Report", "Id", v9x_result.id);
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar0High", v9x_result.bar0_high);
    v9x_key("Report", "Bar2", v9x_result.bar2);
    v9x_key("Report", "Bar2High", v9x_result.bar2_high);
    v9x_key("Report", "MemSize", v9x_result.memsize);
    r = v9x_result.op_results;
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_saved[index] = r[index];
        v9x_key("Before", v9x_saved_names[index], r[index]);
    }
    v9x_key("Before", "DMA_STATUS_REG", r[at_status]);
    v9x_key("Before", "SRBM_SOFT_RESET", r[at_reset]);
    v9x_key("Before", "MC_VM_FB_LOCATION", r[at_fb]);
    v9x_key("Before", "GRBM_STATUS", r[at_grbm]);

    if ((v9x_saved[0] & V9X_RV_DMA_RB_ENABLE) != 0ul ||
        (r[at_reset] & V9X_RV_SOFT_RESET_DMA) != 0ul) {
        CloseHandle(device);
        v9x_finish("ENGINE-NOT-AS-READ", 3u);
    }
    v9x_srbm_reset = r[at_reset];
    /* A start whose window would carry into bit 32 is refused rather than
     * computed. */
    fb = (r[at_fb] & V9X_RV_FB_START_MASK);
    if ((fb & V9X_RV_FB_LOW_MASK) > V9X_RV_FB_LOW_MAX) {
        CloseHandle(device);
        v9x_finish("FB-START-UNSUPPORTED", 4u);
    }
    v9x_gpu_high = fb >> V9X_RV_FB_HIGH_SHIFT;
    g = ((fb & V9X_RV_FB_LOW_MASK) << 24) + V9X_SCRATCH_GPU_OFFSET;
    v9x_key("Run", "ScratchGpuHigh", v9x_gpu_high);
    v9x_key("Run", "ScratchGpu", g);

    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }

    /* Call 2: the run. */
    v9x_fill_seed = v9x_switch_given("/seed8") ? V9X_FILL_SEED_LINE :
                                                 V9X_FILL_SEED_SMALL;
    v9x_fill_serial = v9x_switch_given("/serial");
    v9x_build_run(g);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_number("Report", "Executed", v9x_result.executed);
        v9x_finish("RUN-REFUSED", 5u);
    }
    CloseHandle(device);
    r = v9x_result.op_results;
    v9x_key("During", "StatusAfterReset", r[v9x_at_reset_status]);
    v9x_key("During", "StatusAfterTeardownReset", r[v9x_at_teardown_status]);
    v9x_key("During", "DMA_RB_CNTL", r[v9x_at_enabled_cntl]);
    v9x_key("During", "DMA_STATUS_REG", r[v9x_at_enabled_status]);
    v9x_key("During", "Wait1", r[v9x_at_wait_1]);
    v9x_key("During", "Rptr1", r[v9x_at_rptr_1]);
    v9x_key("During", "Status1", r[v9x_at_status_1]);
    v9x_key("During", "Wait2", r[v9x_at_wait_2]);
    v9x_key("During", "Rptr2", r[v9x_at_rptr_2]);
    v9x_key("During", "Status2", r[v9x_at_status_2]);
    v9x_key("After", "DMA_RB_CNTL", r[v9x_at_after_cntl]);
    v9x_key("After", "DMA_STATUS_REG", r[v9x_at_after_status]);
    v9x_key("After", "SRBM_SOFT_RESET", r[v9x_at_after_reset]);
    v9x_key("After", "SRBM_STATUS", r[v9x_at_after_srbm]);
    v9x_key("After", "GRBM_STATUS", r[v9x_at_after_grbm]);
    for (index = 0ul; index < v9x_step_count; ++index) {
        char key[16];

        lstrcpyA(key, "StepWait");
        v9x_decimal(key + 8, index);
        v9x_key("During", key, r[v9x_at_step_wait[index]]);
    }
    v9x_pass("Fence1", r[v9x_at_wait_1] != V9X_RVDMA_TIMEOUT);
    v9x_pass("Fence2", r[v9x_at_wait_2] != V9X_RVDMA_TIMEOUT);
    v9x_verify();
    v9x_finish("COMPLETE", 0u);
}
