/*
 * The GT218 (NV50 family) register bring-up as an ordered list: PFIFO,
 * PGRAPH with its context program, and one channel bound.
 *
 * The list is what the step 3 probe ran on M-BXMFA8 in runs 27 to 39
 * (docs\decisions\2026-11-13-gt218-nouveau-trace-pd-offset.md,
 * docs\decisions\2026-11-14-gt218-502d-engine-measurements.md), which is
 * nouveau's nv50_fifo_init, nv50_fb_init's trap enable, nv50_gr_init and
 * the g84 channel bind, in that order. The probe and the HAL engine both
 * replay it from here, so the engine's bring-up is the measured one.
 *
 * Pure arithmetic: nothing here touches hardware. The channel's
 * structures (nv50_chan.h) must already be in video memory when the list
 * is replayed, and PMC_INTR_EN stays as found; nothing is taken as an
 * interrupt.
 */
#ifndef VELOCITY9X_NV50_INIT_H
#define VELOCITY9X_NV50_INIT_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"
#include "velocity9x/nv50_chan.h"

/* Write value to the register. */
#define V9X_NV50_INIT_WRITE  1ul
/* Read the register until (read & mask) == value; the caller bounds it
 * and gives the bring-up up if the bound runs out. */
#define V9X_NV50_INIT_WAIT   2ul

struct v9x_nv50_init_op {
    v9x_u32 kind;
    v9x_u32 reg;
    v9x_u32 value;
    v9x_u32 mask;
};

/* Room for the largest list: a 512-word context program and 16 TPs. */
#define V9X_NV50_INIT_OPS_MAX 768ul

struct v9x_nv50_init_input {
    /* PMC_BOOT_0 bits 28:20, A8h for the GT218. */
    v9x_u32 chipset;
    /* PBUS 1540h as read: one trap enable set per TP bit 15:0. */
    v9x_u32 units;
    /* PMC_ENABLE (200h) as read before the bring-up. Only its PFIFO and
     * PGRAPH bits are cycled; every other bit is written back as found. */
    v9x_u32 pmc_enable;
};

struct v9x_nv50_init_list {
    v9x_u32 count;
    /* The three parts, as indices into ops: [0, fifo_end) PFIFO and the
     * VM trap enable, [fifo_end, gr_end) PGRAPH and the context program,
     * [gr_end, count) the channel bind and runlist. */
    v9x_u32 fifo_end;
    v9x_u32 gr_end;
};

/*
 * Build the list for the channel layout describes. ops must hold max
 * entries; V9X_NV50_INIT_OPS_MAX always suffices. Returns
 * V9X_STATUS_INTEGER_OVERFLOW, with nothing usable, if max is too small.
 */
v9x_status v9x_nv50_init_build(const struct v9x_nv50_init_input *input,
                               const struct v9x_nv50_chan_layout *layout,
                               struct v9x_nv50_init_op *ops, v9x_u32 max,
                               struct v9x_nv50_init_list *list);

/* Where the engine reads progress and errors after a kick (fifo\nv04.c
 * nv04_fifo_intr_dma_pusher; measured in runs 28 to 39). USERD is never
 * read: those reads lost the card in runs 3 and 6. */
#define V9X_NV50_PFIFO_IB_GET        0x003334ul
#define V9X_NV50_PFIFO_IB_PUT        0x003330ul
#define V9X_NV50_PFIFO_DMA_GET       0x003244ul
#define V9X_NV50_PFIFO_DMA_PUT       0x003240ul
#define V9X_NV50_PFIFO_DMA_STATE     0x003228ul
#define V9X_NV50_PFIFO_DMA_ERROR_SHIFT 29u
#define V9X_NV50_PGRAPH_INTR         0x400100ul
#define V9X_NV50_PGRAPH_STATUS       0x400700ul
/* PGRAPH's current context: bit 31 | the channel's instance >> 12. */
#define V9X_NV50_PGRAPH_CTX          0x40032cul
#define V9X_NV50_PGRAPH_CTX_VALID    0x80000000ul

/* The IB ring index after index: the ring holds V9X_NV50_IB_ENTRIES and
 * wraps (run 35 carried it past the end). */
v9x_u32 v9x_nv50_ib_next(v9x_u32 index);

#endif
