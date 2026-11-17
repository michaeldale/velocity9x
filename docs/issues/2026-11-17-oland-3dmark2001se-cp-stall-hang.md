# Oland: 3DMark 2001 SE hung the machine once, in Car Chase High, with the CP stalled and fences 38 behind

Date: 2026-11-17

Machine: W9QI0DAD (Oland XT `1002:6610`, FX-6300, Win98 SE), boot 58,
the step 5 build of [ati-gcn-ps11.md](../plans/ati-gcn-ps11.md)
(`V9XHAL build=1c631c63-dirty`: DDI 8, `draw_shader`, two texture
stages). Status: open, seen once in two runs; cause not known.

Evidence: [`../probe/oland-ddi8-w9qi0dad-2026-11-17/3dmark2001se-1/`](../probe/oland-ddi8-w9qi0dad-2026-11-17/3dmark2001se-1/).

## What happened

3DMark 2001 SE's default benchmark (17 tests, DDI 8 now published) ran
Car Chase Low Detail to the end and started High Detail. The flip
count stopped at 2,971, about 2.4 minutes in (09:53:47). The agent then
refused connections and the machine answered only ping. Michael
power-cycled it.

The rerun on the same build after the power-cycle (boot 59) completed
with a score of 1915
([`3dmark2001se-2-boot59/`](../probe/oland-ddi8-w9qi0dad-2026-11-17/3dmark2001se-2-boot59/)).
So it does not happen on every run.

## What the HAL recorded

`V9XTRACE.INI` was flushed at the fault (`FaultFlush=1`), and
`vi_cp.c`'s quarantine wrote its record (`V9XCZCP.TXT`, kept in the
evidence folder):

- `FaultCode=0x56493350`, `V9X_VI_CP_FAULT_PUT`: the HAL waited for
  ring space for its whole spin count and gave the CP up.
  `FaultAddress=0x0C400B60`: the CP's read pointer 0C40h, ours 0B60h.
  The ring was full.
- `Sequence=000E4AFD`, `Fence=000E4AD7`: the last fence written to
  memory is 38 behind the last one submitted.
- `GRBM_STATUS=A0003028`: GUI_ACTIVE and CP_BUSY. VGT, IA, SPI, SC, PA,
  DB and CB are not busy, and DB_CLEAN and CB_CLEAN are set. The 3D
  pipeline was idle.
- `CP_STAT=84038243`: CP_BUSY, CE_BUSY, ME_BUSY, MEQ_BUSY, and bits 0
  and 1, which the CIK register headers name MIU read and write
  requests busy. That bit naming is from the CIK headers. It has not
  been checked for SI.
- `CP_ME_CNTL=0`: the ME was running until the quarantine halted it.

The ring holds 4,096 dwords, and a batch here takes 0E0h. The fences
from E4AD8 to E4AEC (21 batches) were therefore fetched and then
overwritten before the stall. The CP went on fetching and running
batches for about 20 submits after the fence writes stopped landing.
So whichever draw went wrong is no longer in the ring. The batches that
are still there look normal:

- one-unit draws and two-unit draws (the TL2 VS's second V#, stride
  16);
- each batch ends in SURFACE_SYNC and then an EOP fence write;
- the next batch starts with SURFACE_SYNC, then the colour target.

The read pointer stopped inside the CB_COLOR0 SET_CONTEXT_REG at
0C38h, just after a SURFACE_SYNC.

## The mode set that never finished

`V9XTRAIL.TXT`, the 16-bit driver's trail, has one `enable-start`
with no `enable-ok` after it, at 09:52:40. That is while Car Chase High
was loading, 67 s before the flips stopped. In the whole file it is the
only Enable that did not finish. Every earlier test switch in this run
logged two Enables, each finished. Flips went on rising after it, from
1,132 to 2,971.

How a mode set can stay unfinished while the same program goes on
flipping is not understood. Neither is whether it caused the stall. The
trail's own writes could have been lost at the power-cycle, but other
lines written after it at boot were kept.

## Hypotheses, none tested

- The CP's memory writes (EOP fences, perhaps CB writeback) stopped
  completing while it went on fetching, until its write queue filled.
  This fits CP_STAT's request-busy bits and the fences stopping 20
  batches early. Why the writes stopped is not known.
- The mode set at 09:52:40 touched something the GFX engine depends on
  (the frame buffer location, HDP, the memory controller) while batches
  were in flight. Only the timing points at it.
- Step 4's two-unit draws are new in 3DMark 2001 SE with this run; boot
  51's one-stage build completed 3DMark 2001 SE once. One completion
  does not clear a fault that shows up one run in two or fewer.

## Next

Run 3DMark 2001 SE again on this build until it hangs or completes
three times. Log the trail's Enable lines next to the flip count. If it
hangs again, compare the ring and the trail with this record.
