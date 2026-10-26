/*
 * V9XEGDMA.EXE: the first writes to an AMD Evergreen (Radeon HD 5450,
 * Cedar): one async DMA ring, two submissions, checked byte for byte.
 *
 * Phase 3 of docs\plans\ati-evergreen-cedar.md, first step. EGDMA.VXD
 * executes the op lists built here and refuses anything outside its
 * allow-lists. Writes C:\V9XDIAG\V9XEGDMA.INI.
 *
 * Call 1 is read-only: the DMA block's registers, SRBM_STATUS2,
 * SRBM_SOFT_RESET and MC_VM_FB_LOCATION. The run goes ahead only if the ring
 * is disabled, the block neither busy nor in reset and VRAM starts below
 * 4 GiB, as
 * the register probe read them on M-BXMFA8
 * (docs\decisions\2026-10-26-cedar-register-probe.md). /readonly stops
 * there.
 *
 * Call 2 saves the scratch window (BAR0 + 240 MiB, 64 KiB), lays out
 * buffers and a ring in it, starts the ring as Linux r600_dma_resume does
 * (no read-pointer writeback, no interrupts), and submits:
 *
 *   1. a dword copy of 256 dwords, a byte copy of 37 bytes between odd
 *      offsets, a forward-overlapping copy of 64 dwords 4 dwords up, and
 *      a fence;
 *   2. a constant fill of 256 dwords, and a second fence.
 *
 * The fill goes alone in the second submission: it is the one packet only
 * Linux's command checker vouches for on Evergreen, and if it wedges the
 * engine, the copies' results are already in. Then every register written
 * is put back as call 1 read it, HDP is flushed, the buffers are read
 * back and the scratch window is restored.
 *
 * Packet formats: evergreend.h DMA_PACKET (cmd 31:28, sub-command 27:20,
 * count 19:0) and evergreen_cs.c's parser (build\external\evergreen\
 * linux-radeon\): COPY dword-aligned sub 00h header, dst, src, dst high,
 * src high, count in dwords; COPY byte sub 40h the same with the count in
 * bytes; CONSTANT_FILL header, dst, value, dst high in bits 23:16, count
 * in dwords; FENCE header, address, address high, value.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_EGDMA_PATH       V9X_DIAG_PATH("V9XEGDMA.INI")
#define V9X_EGDMA_VXD        "\\\\.\\EGDMA.VXD"
#define V9X_EGDMA_MAGIC      0x414d4445ul
#define V9X_EGDMA_RUN        1u
#define V9X_EGDMA_OP_MAX     512u
#define V9X_EGDMA_BLOCK      4096u
#define V9X_EGDMA_HEADER     16u
#define V9X_EGDMA_TIMEOUT    0xfffffffful

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

/* SRBM_STATUS reads standing in for evergreen_gpu_soft_reset's udelay(50):
 * an uncached PCIe read takes on the order of a microsecond, so this is
 * well over 50 us. */
#define V9X_RESET_DELAY_READS 2000ul

/* EGDMA.VXD's ED_PATTERN_STEP. */
#define V9X_PATTERN_STEP    0x9e3779b9ul

/* Registers (evergreend.h, r600d.h). */
#define V9X_EG_SRBM_STATUS          0x0e50ul
#define V9X_EG_SRBM_SOFT_RESET      0x0e60ul
#define V9X_EG_SRBM_STATUS2         0x0ec4ul
#define V9X_EG_MC_VM_FB_LOCATION    0x2024ul
#define V9X_EG_HDP_MEM_FLUSH        0x5480ul
#define V9X_EG_GRBM_STATUS          0x8010ul
#define V9X_EG_DMA_RB_CNTL          0xd000ul
#define V9X_EG_DMA_RB_BASE          0xd004ul
#define V9X_EG_DMA_RB_RPTR          0xd008ul
#define V9X_EG_DMA_RB_WPTR          0xd00cul
#define V9X_EG_DMA_RB_RPTR_ADDR_HI  0xd01cul
#define V9X_EG_DMA_RB_RPTR_ADDR_LO  0xd020ul
#define V9X_EG_DMA_IB_CNTL          0xd024ul
#define V9X_EG_DMA_CNTL             0xd02cul
#define V9X_EG_DMA_STATUS_REG       0xd034ul
#define V9X_EG_DMA_SEM_INCOMPLETE   0xd044ul
#define V9X_EG_DMA_SEM_WAIT_FAIL    0xd048ul
#define V9X_EG_DMA_MODE             0xd0bcul

#define V9X_EG_SOFT_RESET_DMA       0x00100000ul
#define V9X_EG_DMA_RB_ENABLE        0x00000001ul
#define V9X_EG_DMA_IB_ENABLE        0x00000001ul
#define V9X_EG_CTXEMPTY_INT_ENABLE  0x10000000ul
/* SRBM_STATUS2 bit 5, the busy test Linux's evergreen_gpu_check_soft_reset
 * uses for the DMA block. DMA_STATUS_REG bit 0 (DMA_IDLE in r600d.h) read
 * 1 before the first run on M-BXMFA8 and 0 after it, with the ring stopped
 * and SRBM_STATUS2 clear, so it is reported but not tested. */
#define V9X_EG_DMA_BUSY             0x00000020ul
#define V9X_EG_FB_START_MASK        0x0000fffful

/* DMA packets, evergreend.h. */
#define V9X_DMA_PACKET(cmd, sub, n) \
    ((((DWORD)(cmd) & 0xful) << 28) | (((DWORD)(sub) & 0xfful) << 20) | \
     ((DWORD)(n) & 0xffffful))
#define V9X_DMA_COPY            0x3ul
#define V9X_DMA_FENCE           0x6ul
#define V9X_DMA_CONSTANT_FILL   0xdul
#define V9X_DMA_NOP             0xful
#define V9X_DMA_COPY_DWORD      0x00ul
#define V9X_DMA_COPY_BYTE       0x40ul

/* The scratch window: its GPU address is the FB start plus this, and the
 * layout inside it. Linux pads every DMA submission to 16 dwords with NOPs
 * (the ring's align_mask), so this does too. */
#define V9X_SCRATCH_GPU_OFFSET  0x0f000000ul
#define V9X_SCRATCH_DWORDS      16384ul
#define V9X_RING_LOG2_DWORDS    10ul
#define V9X_RING_ALIGN_DWORDS   16ul
#define V9X_RING                0x0000ul
#define V9X_FENCE_1             0x1000ul
#define V9X_FENCE_2             0x1004ul
#define V9X_COPY_SRC            0x2000ul
#define V9X_COPY_DST            0x3000ul
#define V9X_BYTE_SRC            0x4000ul
#define V9X_BYTE_DST            0x4400ul
#define V9X_OVERLAP             0x5000ul
#define V9X_FILL_DST            0x6000ul

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
#define V9X_FILL_VALUE          0xc0ffee11ul
#define V9X_FENCE_1_VALUE       0x11111111ul
#define V9X_FENCE_2_VALUE       0x22222222ul
#define V9X_FENCE_WAIT_LIMIT    20000000ul

#define V9X_SEED_COPY           0x10000001ul
#define V9X_SEED_BYTE           0x20000002ul
#define V9X_SEED_OVERLAP        0x30000003ul

struct v9x_egdma_op {
    DWORD op;
    DWORD a;
    DWORD b;
    DWORD c;
};

struct v9x_egdma_request {
    DWORD count;
    struct v9x_egdma_op ops[V9X_EGDMA_OP_MAX];
};

struct v9x_egdma_result {
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
    DWORD op_results[V9X_EGDMA_OP_MAX];
    DWORD block[V9X_EGDMA_BLOCK];
};

/* The header must be the VxD's ED_HEADER_DWORDS long. */
typedef char v9x_egdma_header_matches[
    offsetof(struct v9x_egdma_result, op_results) ==
    sizeof(DWORD) * V9X_EGDMA_HEADER ? 1 : -1];

/* The registers call 1 reads and the run puts back, in this order. */
static const DWORD v9x_saved_offsets[] = {
    V9X_EG_DMA_RB_CNTL, V9X_EG_DMA_RB_BASE, V9X_EG_DMA_RB_RPTR,
    V9X_EG_DMA_RB_WPTR, V9X_EG_DMA_RB_RPTR_ADDR_HI,
    V9X_EG_DMA_RB_RPTR_ADDR_LO, V9X_EG_DMA_IB_CNTL, V9X_EG_DMA_CNTL,
    V9X_EG_DMA_SEM_INCOMPLETE, V9X_EG_DMA_SEM_WAIT_FAIL, V9X_EG_DMA_MODE
};
#define V9X_SAVED_COUNT (sizeof(v9x_saved_offsets) / sizeof(v9x_saved_offsets[0]))

static const char * const v9x_saved_names[V9X_SAVED_COUNT] = {
    "DMA_RB_CNTL", "DMA_RB_BASE", "DMA_RB_RPTR", "DMA_RB_WPTR",
    "DMA_RB_RPTR_ADDR_HI", "DMA_RB_RPTR_ADDR_LO", "DMA_IB_CNTL", "DMA_CNTL",
    "DMA_SEM_INCOMPLETE_TIMER_CNTL", "DMA_SEM_WAIT_FAIL_TIMER_CNTL",
    "DMA_MODE"
};

static struct v9x_egdma_request v9x_request;
static struct v9x_egdma_result v9x_result;
static DWORD v9x_saved[V9X_SAVED_COUNT];
static DWORD v9x_ring[64];
static DWORD v9x_ring_count;

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
static DWORD v9x_at_after_busy;
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
    WritePrivateProfileStringA(section, key, value, V9X_EGDMA_PATH);
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
    ok = DeviceIoControl(device, V9X_EGDMA_RUN, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_EGDMA_MAGIC) {
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
        v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_NOP, 0ul, 0ul));
    }
    return v9x_ring_count * 4ul;
}

static void v9x_ring_copy(DWORD sub, DWORD count, DWORD dst, DWORD src)
{
    v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_COPY, sub, count));
    v9x_ring_put(dst);
    v9x_ring_put(src);
    v9x_ring_put(0ul);
    v9x_ring_put(0ul);
}

static void v9x_ring_fence(DWORD address, DWORD value)
{
    v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_FENCE, 0ul, 0ul));
    v9x_ring_put(address);
    v9x_ring_put(0ul);
    v9x_ring_put(value);
}

static void v9x_ring_fill(DWORD count, DWORD dst, DWORD value)
{
    v9x_ring_put(V9X_DMA_PACKET(V9X_DMA_CONSTANT_FILL, 0ul, count));
    v9x_ring_put(dst);
    v9x_ring_put(value);
    v9x_ring_put(0ul);
}

/* evergreen_gpu_soft_reset for the DMA block alone: ring enable off, a
 * wait, SRBM_SOFT_RESET's DMA bit set and read back, a wait, cleared and
 * read back, a wait. Linux also stops the memory controller around it; this
 * does not. Returns the op index of the status read that follows. */
static DWORD v9x_dma_reset(void)
{
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_CNTL,
           v9x_saved[0] & ~V9X_EG_DMA_RB_ENABLE, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_SRBM_SOFT_RESET, V9X_EG_SOFT_RESET_DMA,
           0ul);
    v9x_op(V9X_OP_MMIO_READ, V9X_EG_SRBM_SOFT_RESET, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_SRBM_SOFT_RESET, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_READ, V9X_EG_SRBM_SOFT_RESET, 0ul, 0ul);
    v9x_op(V9X_OP_DELAY, V9X_RESET_DELAY_READS, 0ul, 0ul);
    return v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_STATUS_REG, 0ul, 0ul);
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

/* Call 2's op list. g is the scratch window's GPU address. */
static void v9x_build_run(DWORD g)
{
    DWORD wptr_1;
    DWORD wptr_2;
    DWORD index;

    v9x_ring_count = 0ul;
    v9x_ring_copy(V9X_DMA_COPY_DWORD, V9X_RUN_DWORDS,
                  g + V9X_COPY_DST + V9X_GUARD_DWORDS * 4ul,
                  g + V9X_COPY_SRC);
    v9x_ring_copy(V9X_DMA_COPY_BYTE, V9X_BYTE_COUNT,
                  g + V9X_BYTE_DST + V9X_BYTE_DST_SKEW,
                  g + V9X_BYTE_SRC + V9X_BYTE_SRC_SKEW);
    v9x_ring_copy(V9X_DMA_COPY_DWORD, V9X_OVERLAP_RUN,
                  g + V9X_OVERLAP + V9X_OVERLAP_SHIFT * 4ul,
                  g + V9X_OVERLAP);
    v9x_ring_fence(g + V9X_FENCE_1, V9X_FENCE_1_VALUE);
    wptr_1 = v9x_ring_close();
    v9x_ring_fill(V9X_RUN_DWORDS, g + V9X_FILL_DST + V9X_GUARD_DWORDS * 4ul,
                  V9X_FILL_VALUE);
    v9x_ring_fence(g + V9X_FENCE_2, V9X_FENCE_2_VALUE);
    wptr_2 = v9x_ring_close();

    v9x_request.count = 0ul;
    v9x_op(V9X_OP_SAVE, 0ul, 0ul, 0ul);
    v9x_op(V9X_OP_VRAM_FILL, 0ul, V9X_SCRATCH_DWORDS, 0ul);
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
    for (index = 0ul; index < v9x_ring_count; ++index) {
        v9x_op(V9X_OP_VRAM_WRITE, V9X_RING + index * 4ul, v9x_ring[index],
               0ul);
    }

    /* A DMA soft reset first: a ring stopped as r600_dma_stop stops it left
     * the engine unable to run a second ring on M-BXMFA8 (runs 2 and 3).
     * Then r600_dma_resume's order, without writeback or interrupts. */
    v9x_at_reset_status = v9x_dma_reset();
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_SEM_INCOMPLETE, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_SEM_WAIT_FAIL, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_CNTL,
           V9X_RING_LOG2_DWORDS << 1, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_RPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_WPTR, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_RPTR_ADDR_HI, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_RPTR_ADDR_LO, 0ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_BASE, (g + V9X_RING) >> 8, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_IB_CNTL, V9X_EG_DMA_IB_ENABLE, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_CNTL,
           v9x_saved[7] & ~V9X_EG_CTXEMPTY_INT_ENABLE, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_MODE, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_WPTR, 0ul, 0ul);
    /* The CPU wrote the ring and buffers through BAR0: flush HDP before the
     * engine reads them (Linux flushes it after every CPU write it hands to
     * the GPU). */
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_CNTL,
           (V9X_RING_LOG2_DWORDS << 1) | V9X_EG_DMA_RB_ENABLE, 0ul);
    v9x_at_enabled_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_RB_CNTL, 0ul, 0ul);
    v9x_at_enabled_status = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_STATUS_REG,
                                   0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_WPTR, wptr_1, 0ul);
    v9x_at_wait_1 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE_1, V9X_FENCE_1_VALUE,
                           V9X_FENCE_WAIT_LIMIT);
    v9x_at_rptr_1 = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_RB_RPTR, 0ul, 0ul);
    v9x_at_status_1 = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_STATUS_REG,
                             0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_DMA_RB_WPTR, wptr_2, 0ul);
    v9x_at_wait_2 = v9x_op(V9X_OP_WAIT_VRAM, V9X_FENCE_2, V9X_FENCE_2_VALUE,
                           V9X_FENCE_WAIT_LIMIT);
    v9x_at_rptr_2 = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_RB_RPTR, 0ul, 0ul);
    v9x_at_status_2 = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_STATUS_REG,
                             0ul, 0ul);

    /* Stop the ring and reset the block, so the next user starts from what
     * a reset leaves; then put back what call 1 read. */
    v9x_at_teardown_status = v9x_dma_reset();
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_op(V9X_OP_MMIO_WRITE, v9x_saved_offsets[index], v9x_saved[index],
               0ul);
    }
    v9x_at_after_cntl = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_RB_CNTL, 0ul, 0ul);
    v9x_at_after_status = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_STATUS_REG,
                                 0ul, 0ul);

    v9x_op(V9X_OP_MMIO_WRITE, V9X_EG_HDP_MEM_FLUSH, 1ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_FENCE_1, 2ul, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_COPY_DST, V9X_TARGET_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_BYTE_DST, V9X_BYTE_BUF_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_OVERLAP, V9X_OVERLAP_DWORDS, 0ul);
    v9x_op(V9X_OP_VRAM_BLOCK, V9X_FILL_DST, V9X_TARGET_DWORDS, 0ul);
    v9x_op(V9X_OP_RESTORE, 0ul, 0ul, 0ul);

    v9x_at_after_reset = v9x_op(V9X_OP_MMIO_READ, V9X_EG_SRBM_SOFT_RESET,
                                0ul, 0ul);
    v9x_at_after_srbm = v9x_op(V9X_OP_MMIO_READ, V9X_EG_SRBM_STATUS, 0ul, 0ul);
    v9x_at_after_busy = v9x_op(V9X_OP_MMIO_READ, V9X_EG_SRBM_STATUS2, 0ul,
                               0ul);
    v9x_at_after_grbm = v9x_op(V9X_OP_MMIO_READ, V9X_EG_GRBM_STATUS, 0ul, 0ul);

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
    BYTE expected_bytes[V9X_BYTE_BUF_DWORDS * 4ul];
    DWORD original[V9X_OVERLAP_DWORDS];
    DWORD index;
    int memmove_ok;
    int repeat_ok;

    block = v9x_result.block;
    v9x_key("Result", "Fence1", block[0]);
    v9x_key("Result", "Fence2", block[1]);
    copy = block + 2;
    bytes = copy + V9X_TARGET_DWORDS;
    overlap = bytes + V9X_BYTE_BUF_DWORDS;
    fill = overlap + V9X_OVERLAP_DWORDS;

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
    for (index = 0ul; index < 8ul; ++index) {
        char key[16];

        lstrcpyA(key, "OverlapAt");
        v9x_decimal(key + 9, V9X_OVERLAP_SHIFT + index);
        v9x_key("Result", key, overlap[V9X_OVERLAP_SHIFT + index]);
    }

    /* Constant fill. */
    for (index = 0ul; index < V9X_TARGET_DWORDS; ++index) {
        v9x_expected[index] = V9X_GUARD_VALUE;
    }
    for (index = 0ul; index < V9X_RUN_DWORDS; ++index) {
        v9x_expected[V9X_GUARD_DWORDS + index] = V9X_FILL_VALUE;
    }
    v9x_pass("Fill", v9x_check_run("Fill", fill, v9x_expected,
                                   V9X_TARGET_DWORDS));
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
    WritePrivateProfileStringA(0, 0, 0, V9X_EGDMA_PATH);
    ExitProcess(code);
}

void __stdcall V9xEvergreenDmaEntry(void)
{
    HANDLE device;
    DWORD index;
    DWORD at_fb;
    DWORD at_busy;
    DWORD at_reset;
    DWORD at_status;
    DWORD fb;
    DWORD g;
    const DWORD *r;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_EGDMA_PATH);
    v9x_text("Report", "Tool", "V9XEGDMA");
    v9x_text("Report", "Build", V9X_BUILD_ID);

    device = CreateFileA(V9X_EGDMA_VXD, 0, 0, 0, CREATE_NEW,
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
    at_status = v9x_op(V9X_OP_MMIO_READ, V9X_EG_DMA_STATUS_REG, 0ul, 0ul);
    at_busy = v9x_op(V9X_OP_MMIO_READ, V9X_EG_SRBM_STATUS2, 0ul, 0ul);
    at_reset = v9x_op(V9X_OP_MMIO_READ, V9X_EG_SRBM_SOFT_RESET, 0ul, 0ul);
    at_fb = v9x_op(V9X_OP_MMIO_READ, V9X_EG_MC_VM_FB_LOCATION, 0ul, 0ul);
    if (!v9x_dioc(device)) {
        CloseHandle(device);
        v9x_finish("REFUSED", 2u);
    }
    v9x_key("Report", "Id", v9x_result.id);
    v9x_key("Report", "Bar0", v9x_result.bar0);
    v9x_key("Report", "Bar0High", v9x_result.bar0_high);
    v9x_key("Report", "Bar2", v9x_result.bar2);
    v9x_key("Report", "Bar2High", v9x_result.bar2_high);
    v9x_number("Report", "MemoryMiB", v9x_result.memsize);
    r = v9x_result.op_results;
    for (index = 0ul; index < V9X_SAVED_COUNT; ++index) {
        v9x_saved[index] = r[index];
        v9x_key("Before", v9x_saved_names[index], r[index]);
    }
    v9x_key("Before", "DMA_STATUS_REG", r[at_status]);
    v9x_key("Before", "SRBM_STATUS2", r[at_busy]);
    v9x_key("Before", "SRBM_SOFT_RESET", r[at_reset]);
    v9x_key("Before", "MC_VM_FB_LOCATION", r[at_fb]);

    if ((v9x_saved[0] & V9X_EG_DMA_RB_ENABLE) != 0ul ||
        (r[at_busy] & V9X_EG_DMA_BUSY) != 0ul ||
        (r[at_reset] & V9X_EG_SOFT_RESET_DMA) != 0ul) {
        CloseHandle(device);
        v9x_finish("ENGINE-NOT-AS-READ", 3u);
    }
    /* The start field is 16 MiB units of a 40-bit address; anything that
     * puts the window above 4 GiB is refused rather than computed. */
    fb = (r[at_fb] & V9X_EG_FB_START_MASK);
    if (fb > 0xeful) {
        CloseHandle(device);
        v9x_finish("FB-START-UNSUPPORTED", 4u);
    }
    g = (fb << 24) + V9X_SCRATCH_GPU_OFFSET;
    v9x_key("Run", "ScratchGpu", g);

    if (v9x_switch_given("/readonly")) {
        CloseHandle(device);
        v9x_finish("READONLY", 0u);
    }

    /* Call 2: the run. */
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
    v9x_key("After", "SRBM_STATUS2", r[v9x_at_after_busy]);
    v9x_key("After", "GRBM_STATUS", r[v9x_at_after_grbm]);
    v9x_pass("Fence1", r[v9x_at_wait_1] != V9X_EGDMA_TIMEOUT);
    v9x_pass("Fence2", r[v9x_at_wait_2] != V9X_EGDMA_TIMEOUT);
    v9x_verify();
    v9x_finish("COMPLETE", 0u);
}
