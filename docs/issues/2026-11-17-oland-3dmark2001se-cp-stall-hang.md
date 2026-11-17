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

## 2026-11-17, step 6 build

Boot 60, step 6's build (cubes and signed formats), 32 bpp desktop:
3DMark 2001 SE completed (3101). No hang
([2026-11-17-oland-cubes-signed-formats.md](../decisions/2026-11-17-oland-cubes-signed-formats.md)).
Count so far: one hang in three runs, across two builds that share the
two-unit path.

## 2026-11-17, later: that run stalled too

The paragraph above is wrong. Step 6's run did stall:
- `EngineIdleTimeouts` was 0 in its before-snapshot and 1 in its after.
- `V9XTRACE.INI` and `V9XCZCP.TXT`, found on the guest after the next
  boots, carry its HAL build (`402c2d92-dirty`) and the `VI3P` tag.
- Copies are in
  [`3dmark2001se-boot60/`](../probe/oland-cubes-signed-w9qi0dad-2026-11-17/3dmark2001se-boot60/).

The record matches this issue's first one:
- CP_STAT 84038243h and GRBM_STATUS A0003028h, as before;
- a full ring (read pointer F68h, ours EB0h);
- `Sequence=000E35B4`, `Fence=000E35A0`: 20 behind;
- the ring holds 18 draws in 14 one-stream batches and 4 two-unit ones,
  and no pixel shader batch.

This time the machine stayed up. The HAL quarantined the CP, and 3DMark
ran on to its score window.

Every earlier run's snapshots were checked for the same counter:
- the DDI 6 one-stage run of 2026-11-16 and step 5's second run: 0
  before and after;
- every 3DMark 2000 run: 0 before and after.

So in 3DMark 2001 SE on the DDI 8 builds it is two stalls in three
runs: one hung the machine, one was survived.

Both stalls came after DDI 8 was published (step 5). Step 4's two-stage
build never ran 3DMark 2001 SE. A run with two stages and DDI 6, or one
stage and DDI 8, would separate the two.

Today's freeze just after the desktop on step 7's first boot left no
record of its own: this one was the last, written by the step 6 build.
That freeze is not counted here.

Every later run's after-snapshot is to be checked for
`EngineIdleTimeouts`, not only for a score.

## 2026-11-17, step 7 build: a third stall

Boot 63, step 7's build (DXT published), 32 bpp desktop. `EngineIdleTimeouts` went
from 0 to 1 during 3DMark 2001 SE. The record
([`3dmark2001se-boot63/V9XCZCP.TXT`](../probe/oland-dxt-w9qi0dad-2026-11-17/3dmark2001se-boot63/))
is not the same as the first two:
- a full ring (read pointer 8E0h, ours 8C0h);
- `Sequence=00119F42`, `Fence=00119F1C`: 38 behind;
- GRBM_STATUS A00C10A0h: GUI_ACTIVE, CP_BUSY, **IA_BUSY and
  IA_BUSY_NO_DMA**, ME0PIPE0_CF_RQ_PENDING, SRBM_RQ_PENDING, DB_CLEAN.
  The input assembler is busy here, where the first two had the 3D
  pipeline idle (A0003028h: GUI_ACTIVE, CP_BUSY, SRBM_RQ_PENDING,
  DB_CLEAN, CB_CLEAN);
- CP_STAT 84838243h: as before plus RCIU_BUSY.

Field names are from Mesa's `registers/gfx6.json`. That also corrects
this issue's first entry. GFX6 names CP_STAT 84038243h's set fields
ROQ_RING_BUSY, PFP_BUSY, MEQ_BUSY, ME_BUSY, CE_BUSY and CP_BUSY. It names
no field at bits 0 and 1, so "MIU read and write requests busy" (from
the CIK headers) has no support, and neither has the hypothesis built on
it.

The run was also starved of video memory (a 16 MiB heap, textures
thrashing; see
[the step 7 record](../decisions/2026-11-17-oland-bc-1d-tiled.md)).
Three stalls in four 3DMark 2001 SE runs on DDI 8 builds.

## 2026-11-17, the 128 MiB heap build: a spontaneous reboot

Boot 64: step 7's build with the Oland's heap mapped at 128 MiB (as the
Evergreen rows').
- Michael's OBS recording of it (`Y:\MWD\videos\obs\2026-11-17
  16-49-13.mkv`, on the host):
  - Car Chase Low with the game clock holding for about 1.5 s at 08:57
    and about 2 s at 11:50, at a few fps between;
  - then the capture card's colour bars, the signal gone, at about 13:07
    game time. The machine reset in the first test.
- Correction: this entry first said its frame grabs showed Dragothic
  textured at 20-38 fps on this build. The grab files persist on the
  guest across boots. The run never left Car Chase, so the Dragothic
  grabs (5-8 of `3dmark2001se-heap128-boot64/grabs-sheet.jpg`) are boot
  63's. Which of grabs 0-4 are boot 64's is not known. What the 128 MiB
  heap does to 3DMark's textures is not measured.
- No new fault record: `V9XTRACE.INI` and `V9XCZCP.TXT` on the guest
  are still boot 63's (the same sequence, fence and status words). So
  the HAL did not reach its quarantine; this stop was harder than the
  stalls above.
- The run script lost the agent and then met the next boot's logon
  dialog, so its log's timeline is not the run's.

Whether the reboot is this issue's stall, the larger heap, or neither
was not known then; the A/B runs below say it is not the heap.

## 2026-11-17, evening: A/B runs, and a control that fails too

Evidence: [`../probe/oland-cp-stall-w9qi0dad-2026-11-17/`](../probe/oland-cp-stall-w9qi0dad-2026-11-17/).

Each build below was installed by WININIT rename and hash-checked:
- **1, the instrumented build.** `vi_cp.c` keeps the last 64 batches
  and dumps the ones around the last fence at a quarantine, or at the
  first ring-space wait past 1/16 of its budget (`V9XCPSLW.TXT`,
  flushed). With the 128 MiB heap, the machine reset about a minute into
  3DMark. Neither record was written.
- **2, the same with the 16 MiB heap.** Froze at FlipHandled 949, about
  2 minutes in; the machine stopped answering ping. Michael
  power-cycled it. No record.
- **3, the same, 16 MiB, with DDI 8 off** (EngineCaps 40000017, step 4's
  state: two stages, no pixel shaders, no DXT). Rebooted by itself about
  3 minutes in (a new driver load at 17:19:13). Michael's recording
  (`2026-11-17 17-17-27.mkv`) shows Car Chase High's loading screen held
  a long time, then 1-13 fps with the clock held at 06:40.
- **4, control: boot 59's step 5 package**, which completed 3DMark
  2001 SE cleanly at 15:19 today. Crashing again by Michael's eye at
  17:27, about 3 minutes in.

So neither the heap nor DDI 8 makes the difference, and a package that
passed this afternoon fails now. Through the day the failures came
sooner whatever the build:
- 09:52 hang after 2.4 min;
- 15:19 clean;
- 15:47 and 16:36 stalls survived;
- then a reset or hang every run from 16:47, two or three minutes in.

The card is not hot to the touch and its fan turns; Michael added a case
fan before the control.

What this points at, none of it measured:
- the card, or the machine (power, slot, board), degrading or failing
  under load;
- a state that survives a reset and a power-cycle (the card's own, or
  the BIOS's);
- if the driver's, then in what every build here shares: steps 1-4,
  the CP bring-up and the ring.

The last three failures wrote no record. A CPU read of a GPU register
that never completes would do that: the ring wait would never count a
spin. That is a hypothesis.

Next:
- run what this card did before today, a build from before the
  pixel-shader work, or another driver's 3D, to see whether the card
  still runs;
- if it fails too, swap the card or move it to another machine.

## 2026-11-17, evening: not heat; the failures sit at the first test change

- The control run ended with the machine wedged after Michael closed
  3DMark (ping answered, the agent did not), at 5.6 min and 5,846 flips.
  It needed a power-cycle.
- V9XSIMM now reads the die temperature as Linux's `si_get_temp` does
  (CG_MULT_THERMAL_STATUS 714h, CTF_TEMP bits 17:9). Its VxD gained the
  4-byte window for that one register.
  - Idle after the power-cycle: 33 C (R00714=00004221h).
- Run 5 (`3dmark2001se-5-temperature`, the step 5 control package) read
  the temperature at every poll: 33, 33, 34, 34, 34 C. The machine then
  rebooted itself about two minutes in (a new driver load at 17:42:53),
  at 1,423 flips.
- So it is not heat. The card is cool to the touch, its fan turns, and
  a case fan was added.

Where the runs die:
- 1,423 flips is the end of Car Chase Low, and several runs died near
  there: boot 58 after 1,721 (Car Chase High), runs 2 and 3 at 949 and
  2,558-3,680.
- This issue's first record had a 16-bit Enable (a mode set) that
  started at Car Chase High's load and never logged its end.
- Michael's 17:17 recording held on that loading screen a long time
  before High Detail drew at 1-13 fps.
- The trail of run 5 has no Enable between the boot and the reboot. If
  the trail's writes reached the disk, it died before the first test
  change's mode set had even logged its start.

Not known: whether the mode set at a test change is involved, whether
the card is failing, or whether this was always latent and the
afternoon's clean runs were luck.

## 2026-11-17, 18:10: the GeForce 210 in the same machine completes

Michael pulled 8 GB of the machine's RAM (it had also crashed on a boot)
and put the GeForce 210 (10DE:0A65) back in W9QI0DAD.
- The nvidia package was built from origin/main e5dbf0ba in its own
  worktree, put in place by WININIT rename, hash-checked.
- On the first boot Windows stayed on vga.drv: the ati driver's failure
  at 17:50 (`fail-hardware-present`, no Oland) had left the profile at
  640x480x4, and the INF hands 4 bpp to vga.drv. Setting
  `HKLM\Config\0001\Display\Settings` back to 32 bpp, 1024,768 and a
  reset loaded the driver: GeForce 210, hardware Direct3D, DDI 8.
- 3DMark 2001 SE (`3dmark2001se-6-gt210-control`) completed:
  - score 1996 in 7.5 minutes;
  - `EngineIdleTimeouts` 0 before and after;
  - 120,700 RenderPrimitive calls, one boot throughout.

Two things changed before this run, the card and the RAM, so it does not
separate them. Next: the Oland back in with the RAM still out. A clean
run then points at the RAM; failures at the Oland card.

## 2026-11-17, 18:21: the Oland with the RAM still out completes

The setup:
- the Oland refitted, the 8 GB still out;
- boot 59's step 5 package (the control that crashed at 17:27 and
  17:41), WININIT rename, hash-checked;
- the profile's 640x480x4 fallback undone as above.

3DMark 2001 SE (`3dmark2001se-7-oland-ram-out`) completed:
- score 1913 in 7.5 minutes (boot 59 scored 1915 on this package);
- `EngineIdleTimeouts` 0 before and after;
- 155,168 RenderPrimitive calls on one boot;
- 33-35 C throughout.

So far this points at the RAM that was pulled, not at the card or the
driver. It is one run; more follow. Nothing idles the CP or the DMA engine
before the 16-bit side sets a mode through the VBE. Nothing measured
says that matters. Four of five 3DMark 2001 SE runs on DDI 8 builds have now
ended in, or survived, a stall or a stop.

## 2026-11-17, 18:37: the RAM-out machine stalls on the second run

The same setup, run again (`3dmark2001se-8-oland-ram-out-2`): the run
reached a score (3025) but `EngineIdleTimeouts` went 0 to 1 and
`V9XCZCP.TXT` was written. A quarantined run still scores, so the score
is not evidence of health. The RAM is not the whole story.

## 2026-11-17, 18:51: the evidence build; the ring read pointer lies

`vi_cp.c` gained, for the record only: CP_STALLED_STAT1-3 (8674h, 8678h,
8670h), CP_BUSY_STAT (867Ch), CP_COHER_STATUS (85FCh), the read pointer
the CP writes back to memory, four more reads of CP_RB0_RPTR, and a check
of the hardware WPTR against the HAL's copy before every put
(`V9XCPWPT.TXT` on a mismatch). Run `3dmark2001se-9-evidence` stalled
(`EngineIdleTimeouts` 0 to 1; it still scored 6010). The record:

| Field | Value |
|---|---|
| HAL wptr | 410h |
| CP_RB0_RPTR, five reads | 480h each |
| read pointer written back to memory | 400h |
| CP_STALLED_STAT1 | 04008000h |
| CP_STALLED_STAT2 / STAT3 | 00010000h / 00000400h |
| CP_COHER_STATUS | 0 |
| `V9XCPWPT.TXT` | not written |

- The register read pointer (480h) is past the write pointer (410h). The
  HAL computes free space from it, finds the ring "full", and spins on
  that MMIO register.
- The writeback copy (400h) is consistent: 10h dwords pending, the CP
  behind the HAL as expected.
- CP_STALLED_STAT1 bits 15 and 26 are, by the SI register headers,
  RCIU_STALLED_ON_ME_READ and ME_WAITING_ON_REG_READ_DATA: the ME waits
  on a register read that does not return. The HAL's tight polling of
  CP registers through the same register interface is the likely
  reason. Not proven; the fix below is consistent with it.
- No WPTR mismatch: the HAL and the hardware agree on what was
  submitted, so this is not a lost write.
- The HAL never overwrites unconsumed ring space, so the stalled batch
  was still intact in the ring (an earlier note here said otherwise).

## 2026-11-17, 18:52-19:20: read the pointer from writeback; three clean runs

`v9x_vi_cp_pending()` now reads the read pointer from the writeback
dword (window WRITEBACK) instead of CP_RB0_RPTR, as Linux radeon does on
SI. RB_NO_UPDATE is already clear on every chip this engine drives, so
the CP updates that dword. Waiting for space now polls memory, not an
MMIO register.

Three 3DMark 2001 SE runs back to back on one boot, RAM still out:

| Run | Score | EngineIdleTimeouts | Stall records |
|---|---|---|---|
| `3dmark2001se-10-rptr-wb-1` | 2601 | 0 to 0 | none |
| `3dmark2001se-10-rptr-wb-2` | 2603 | 0 to 0 | none |
| `3dmark2001se-10-rptr-wb-3` | 2616 | 0 to 0 | none |

GPU temperature 33-35 C throughout (V9XSIMM, 714h). Three runs are not
proof, but every earlier DDI 8 build on this card stalled within one or
two runs. Still open:
- whether the pulled RAM also caused the spontaneous resets: put it back
  and repeat on this build;
- why CP_RB0_RPTR reads past wptr at all (not explained, only avoided).
