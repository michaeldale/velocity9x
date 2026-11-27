/*
 * The GT218 register bring-up as an ordered list: see
 * include\velocity9x\nv50_init.h. The values and their order are
 * nouveau's (MIT; fifo\nv50.c nv50_fifo_init, fb\nv50.c nv50_fb_init
 * with fb\gt215.c's or fb\g84.c's trap value, gr\nv50.c nv50_gr_init,
 * fifo\g84.c's
 * bind), as the step 3 probe ran them on M-BXMFA8 in runs 27 to 39.
 *
 * Left out, because the probe ran them and the record does not show they
 * are needed: PMC_ENABLE = FFFFFFFFh (the card kept DFF3F113h, run 19),
 * the BAR channel at 1704h and the G84 BAR flush (run 18 changed
 * nothing). A run of the probe from this list, without them, is their
 * evidence (docs\decisions\2026-11-14-gt218-502d-engine-measurements.md).
 */
#include "velocity9x/nv50_init.h"

/* nv50_fifo_init. */
#define V9X_NV50_PFIFO_250C          0x00250cul
#define V9X_NV50_PFIFO_250C_VALUE    0x6f3cfc34ul
#define V9X_NV50_PFIFO_2044          0x002044ul
#define V9X_NV50_PFIFO_2044_VALUE    0x01003ffful
/* Every PFIFO interrupt source enabled but bit 30. Nothing takes them:
 * PMC_INTR_EN is 0, and the engine polls 2100h. */
#define V9X_NV50_PFIFO_INTR_EN_VALUE 0xbffffffful
#define V9X_NV50_PFIFO_CACHE1_PUSH   0x003200ul
#define V9X_NV50_PFIFO_CACHE1_PULL   0x003250ul

/* nv50_fb_init: without the enable the MMU latches no trap (run 17);
 * bit 31 acknowledges one left latched (nv50_fb_intr). */
#define V9X_NV50_VM_TRAP             0x100c90ul
#define V9X_NV50_VM_TRAP_GT215       0x000d0ffful
/* fb\g84.c's value: G84 to G98 and GT200 (A0h), the G96 among them. */
#define V9X_NV50_VM_TRAP_G84         0x001d07fful
#define V9X_NV50_VM_TRAP_ACK         0x80000000ul

/* nv50_gr_init. */
#define V9X_NV50_PGRAPH_CTXCTL       0x40008cul
#define V9X_NV50_PGRAPH_CTXCTL_HW    0x00000004ul
#define V9X_NV50_PGRAPH_TRAP_RESET   0xc0000000ul
#define V9X_NV50_PGRAPH_TRAP         0x400108ul
#define V9X_NV50_PGRAPH_TRAP_EN      0x400138ul
#define V9X_NV50_PGRAPH_INTR_EN      0x40013cul
#define V9X_NV50_PGRAPH_FIFO         0x400500ul
#define V9X_NV50_PGRAPH_FIFO_ON      0x00010001ul
#define V9X_NV50_PGRAPH_PROG_INDEX   0x400324ul
#define V9X_NV50_PGRAPH_PROG_DATA    0x400328ul
#define V9X_NV50_PGRAPH_ZCULL        0x402ca8ul
#define V9X_NV50_PGRAPH_ZCULL_2CC0   0x402cc0ul
#define V9X_NV50_PGRAPH_ZCULL_FIRST  0x402c20ul
#define V9X_NV50_TP_COUNT            16u
/* The first chipset with the later per-TP trap addresses. */
#define V9X_NV50_CHIPSET_A0          0xa0ul

struct v9x_nv50_init_out {
    struct v9x_nv50_init_op *ops;
    v9x_u32 max;
    v9x_u32 count;
    v9x_u32 overflow;
};

static void v9x_nv50_init_put(struct v9x_nv50_init_out *out, v9x_u32 kind,
                              v9x_u32 reg, v9x_u32 value, v9x_u32 mask)
{
    if (out->count >= out->max) {
        out->overflow = 1ul;
        return;
    }
    out->ops[out->count].kind = kind;
    out->ops[out->count].reg = reg;
    out->ops[out->count].value = value;
    out->ops[out->count].mask = mask;
    ++out->count;
}

static void v9x_nv50_init_write(struct v9x_nv50_init_out *out, v9x_u32 reg,
                                v9x_u32 value)
{
    v9x_nv50_init_put(out, V9X_NV50_INIT_WRITE, reg, value, 0xfffffffful);
}

/* The runlist at layout's address with count entries, and the wait
 * nouveau makes for PFIFO to take it (fifo\nv50.c, 32ECh bit 8). */
static void v9x_nv50_init_runlist(struct v9x_nv50_init_out *out,
                                  const struct v9x_nv50_chan_layout *layout,
                                  v9x_u32 count)
{
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_RUNLIST_ADDR,
                        layout->runlist_reg);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_RUNLIST_COUNT, count);
    v9x_nv50_init_put(out, V9X_NV50_INIT_WAIT,
                      V9X_NV50_PFIFO_RUNLIST_COUNT, 0ul,
                      V9X_NV50_PFIFO_RUNLIST_BUSY);
}

/* The FB subdev's trap enable by chipset, as nouveau's device table
 * assigns gt215_fb (A3h, A5h, A8h, AFh) or g84_fb (84h to A0h). The
 * MCP77/79 IGPs (AAh, ACh) have their own and are not claimed here. */
static v9x_u32 v9x_nv50_vm_trap(v9x_u32 chipset)
{
    if ((chipset > V9X_NV50_CHIPSET_A0 && chipset < 0xaaul) ||
        chipset == 0xaful) {
        return V9X_NV50_VM_TRAP_GT215;
    }
    return V9X_NV50_VM_TRAP_G84;
}

static void v9x_nv50_init_fifo(struct v9x_nv50_init_out *out,
                               const struct v9x_nv50_init_input *input,
                               const struct v9x_nv50_chan_layout *layout)
{
    v9x_u32 chid;

    /* PFIFO off and on through PMC_ENABLE; no other bit changes. */
    v9x_nv50_init_write(out, V9X_NV50_PMC_ENABLE,
                        input->pmc_enable & ~V9X_NV50_PMC_ENABLE_PFIFO);
    v9x_nv50_init_write(out, V9X_NV50_PMC_ENABLE,
                        input->pmc_enable | V9X_NV50_PMC_ENABLE_PFIFO);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_250C,
                        V9X_NV50_PFIFO_250C_VALUE);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_2044,
                        V9X_NV50_PFIFO_2044_VALUE);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_INTR, 0xfffffffful);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_INTR_EN,
                        V9X_NV50_PFIFO_INTR_EN_VALUE);

    /* The channel table as the VBIOS leaves it holds garbage. */
    for (chid = 0ul; chid < V9X_NV50_PFIFO_CHAN_COUNT; ++chid) {
        v9x_nv50_init_write(out, V9X_NV50_PFIFO_CHAN(chid), 0ul);
    }

    /* An empty runlist before CACHE1 is enabled, as nouveau commits. */
    v9x_nv50_init_runlist(out, layout, 0ul);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_CACHE1_PUSH, 1ul);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_CACHE1_PULL, 1ul);
    v9x_nv50_init_write(out, V9X_NV50_PFIFO_CACHES, 1ul);

    v9x_nv50_init_write(out, V9X_NV50_VM_TRAP,
                        v9x_nv50_vm_trap(input->chipset) |
                        V9X_NV50_VM_TRAP_ACK);
    v9x_nv50_init_write(out, V9X_NV50_VM_TRAP,
                        v9x_nv50_vm_trap(input->chipset));
}

static void v9x_nv50_init_gr(struct v9x_nv50_init_out *out,
                             const struct v9x_nv50_init_input *input,
                             const struct v9x_nv50_chan_layout *layout)
{
    static const v9x_u32 traps[6] = {
        0x400804ul, 0x406800ul, 0x400c04ul,
        0x401800ul, 0x405018ul, 0x402000ul
    };
    v9x_u32 pmc = input->pmc_enable | V9X_NV50_PMC_ENABLE_PFIFO;
    v9x_u32 index;
    v9x_u32 word;

    /* PGRAPH off and on (mc\gt215.c's reset mask). */
    v9x_nv50_init_write(out, V9X_NV50_PMC_ENABLE,
                        pmc & ~V9X_NV50_PMC_ENABLE_PGRAPH);
    v9x_nv50_init_write(out, V9X_NV50_PMC_ENABLE,
                        pmc | V9X_NV50_PMC_ENABLE_PGRAPH);

    /* Hardware context switching, then the trap resets. */
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_CTXCTL,
                        V9X_NV50_PGRAPH_CTXCTL_HW);
    for (index = 0ul; index < 6ul; ++index) {
        v9x_nv50_init_write(out, traps[index], V9X_NV50_PGRAPH_TRAP_RESET);
    }
    /* One set per TP present; the addresses moved at chipset A0h. */
    for (index = 0ul; index < V9X_NV50_TP_COUNT; ++index) {
        if ((input->units & (1ul << index)) == 0ul) {
            continue;
        }
        if (input->chipset < V9X_NV50_CHIPSET_A0) {
            v9x_nv50_init_write(out, 0x408900ul + (index << 12),
                                V9X_NV50_PGRAPH_TRAP_RESET);
            v9x_nv50_init_write(out, 0x408e08ul + (index << 12),
                                V9X_NV50_PGRAPH_TRAP_RESET);
            v9x_nv50_init_write(out, 0x408314ul + (index << 12),
                                V9X_NV50_PGRAPH_TRAP_RESET);
        } else {
            v9x_nv50_init_write(out, 0x408600ul + (index << 11),
                                V9X_NV50_PGRAPH_TRAP_RESET);
            v9x_nv50_init_write(out, 0x408708ul + (index << 11),
                                V9X_NV50_PGRAPH_TRAP_RESET);
            v9x_nv50_init_write(out, 0x40831cul + (index << 11),
                                V9X_NV50_PGRAPH_TRAP_RESET);
        }
    }
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_TRAP, 0xfffffffful);
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_TRAP_EN, 0xfffffffful);
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_INTR, 0xfffffffful);
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_INTR_EN, 0xfffffffful);
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_FIFO,
                        V9X_NV50_PGRAPH_FIFO_ON);

    /* nv50_grctx_init: the context program, from index 0. */
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_PROG_INDEX, 0ul);
    for (word = 0ul; word < layout->program_words; ++word) {
        v9x_nv50_init_write(out, V9X_NV50_PGRAPH_PROG_DATA,
                            layout->program[word]);
    }
    v9x_nv50_init_write(out, 0x400824ul, 0ul);
    v9x_nv50_init_write(out, 0x400828ul, 0ul);
    v9x_nv50_init_write(out, 0x40082cul, 0ul);
    v9x_nv50_init_write(out, 0x400830ul, 0ul);
    v9x_nv50_init_write(out, V9X_NV50_PGRAPH_CTX, 0ul);
    v9x_nv50_init_write(out, 0x400330ul, 0ul);

    /* Zcull, by chipset as nv50_gr_init has it. */
    if (input->chipset < V9X_NV50_CHIPSET_A0) {
        v9x_nv50_init_write(out, V9X_NV50_PGRAPH_ZCULL, 0x00000800ul);
    } else if (input->chipset == 0xa0ul || input->chipset == 0xaaul ||
               input->chipset == 0xacul) {
        v9x_nv50_init_write(out, V9X_NV50_PGRAPH_ZCULL, 0x00000802ul);
    } else {
        v9x_nv50_init_write(out, V9X_NV50_PGRAPH_ZCULL_2CC0, 0ul);
        v9x_nv50_init_write(out, V9X_NV50_PGRAPH_ZCULL, 0x00000002ul);
    }
    for (index = 0ul; index < 8ul; ++index) {
        for (word = 0ul; word < 4ul; ++word) {
            v9x_nv50_init_write(out, V9X_NV50_PGRAPH_ZCULL_FIRST +
                                index * 0x10ul + word * 4ul, 0ul);
        }
    }
}

v9x_status v9x_nv50_init_build(const struct v9x_nv50_init_input *input,
                               const struct v9x_nv50_chan_layout *layout,
                               struct v9x_nv50_init_op *ops, v9x_u32 max,
                               struct v9x_nv50_init_list *list)
{
    struct v9x_nv50_init_out out;

    if (input == 0 || layout == 0 || ops == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    out.ops = ops;
    out.max = max;
    out.count = 0ul;
    out.overflow = 0ul;

    v9x_nv50_init_fifo(&out, input, layout);
    list->fifo_end = out.count;
    v9x_nv50_init_gr(&out, input, layout);
    list->gr_end = out.count;

    /* The channel: its table entry with the enable bit, then the runlist
     * naming it. */
    v9x_nv50_init_write(&out, V9X_NV50_PFIFO_CHAN(layout->chid),
                        layout->chan_entry);
    v9x_nv50_init_runlist(&out, layout, layout->runlist_count);
    list->count = out.count;

    if (out.overflow != 0ul) {
        list->count = 0ul;
        list->fifo_end = 0ul;
        list->gr_end = 0ul;
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    return V9X_STATUS_OK;
}
