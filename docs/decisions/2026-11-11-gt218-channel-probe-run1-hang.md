# GT218 channel probe, first run: M-BXMFA8 stopped answering in stage fifo

Date: 2026-11-11

Step 3 of [docs/plans/nvidia-gt218-pgraph-2d.md](../plans/nvidia-gt218-pgraph-2d.md).

## What ran

`V9XN5CH.EXE /stop:fifo` (`tools/diag/nv50_channel_probe*.{asm,c}`,
`scripts/build-nv50-channel-probe.ps1`), built from 6737d4f1 plus the
probe (uncommitted at the time), on M-BXMFA8 boot 83 (the BAR1-heap
`nvidia` driver, 1024x768x16 desktop). Its stages, each one DIOC run of
N5CH.VXD:

1. `pre`: reads only.
2. `units`, `region`: read 1540h, build the channel with nv50_chan.c, write
   the B0000h-byte region through BAR1 at video memory 192 MiB, fill the
   2D target, read four words back. INI flushed after.
3. `fifo`: in **one** run: PMC_ENABLE PFIFO off and on, nouveau's
   nv50_fifo_init constants, interrupt status and enables (PMC_INTR_EN not
   touched), the channel table cleared, cache1 and caches on, **a raw VM
   TLB flush for PGRAPH (`100C80h = 1`) and a bounded poll of it**, channel
   1 bound and enabled, runlist committed and polled, IB PUT = 1, IB GET
   polled, status reads.

## What happened

The exec never returned. The agent's last relay check-in was about 166 s
before the transport timed out, and the relay showed the machine offline
from then on: the machine stopped, not just the probe. The owner was asked
by remote-help chat to power-cycle it (the message is only delivered when
the agent next checks in, so it does not reach them while the machine is
hung).

Nothing has been read back yet. V9XN5CH.INI should hold the `pre` and
`region` stages (flushed before `fifo` began); it is to be fetched after
the restart.

## What the evidence allows

The hang is somewhere in the single `fifo` run, which is all the run can
say. Every one of its MMIO waits is bounded by a loop count, so a loop
cannot have spun for ever; a hang that stops the whole machine is a bus
access that never completes (a stalled BAR0 read), or a GPU state that
stalls the bus.

The leading suspect, not confirmed: the raw PGRAPH TLB flush. nouveau
never flushes for PFIFO, and for PGRAPH on the GT215 family it uses
`g84_gr_tlb_flush` (gr/g84.c), which first clears PGRAPH's FIFO access
(400500h bit 0) and waits for its units to idle before writing 100C80h,
with the comment "unfortunate hw bug workaround". This run flushed PGRAPH
raw, before PGRAPH had been initialised. Other candidates in the same run:
the PFIFO reset itself, enabling caches with a stale state, and the USERD
IB GET poll on a channel PFIFO could not load.

## Changed for run 2

- The PGRAPH TLB flush is removed from stage fifo (the VM is new; nothing
  has cached it).
- Stage fifo is three DIOC runs (`fifo-init`, `fifo-bind`, `fifo-kick`),
  stage gr two (`gr-init`, `gr`), and every run first flushes
  `Report/Progress=<run>` to the INI, so the next hang names its run.
