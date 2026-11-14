# The HD 4600 renders at 400 MHz under load: the BIOS's request stands because nothing programs RPS, and RP0 is 1150

2026-11-14, i5-4570 (`8086:0412` rev 06, W9QI5A83), boot 262 (the
77495fa5-dirty set of `haswell-4570-test-machine`), desktop 800x600.
`V9XHSWMM.EXE /rps` built `5b0ae9ab-dirty` (this commit's tree), run as
`C:\V9XRPS.EXE`, with the matching `HSWMM.VXD` in `C:\WINDOWS\SYSTEM`
(the boot's previous copy kept in the session scratchpad). Evidence:
`docs\probe\hsw-4570-rps-2026-11-14\`, the `[Report]` and `[Rps]`
sections of each run.

## The question

The driver holds forcewake for as long as it is open, so the GT never
sleeps. No module in the tree writes `RPNSWREQ` (`A008h`) or any other RPS
register; the reviewed Haswell path has no render-clock management at all.
A GT that is awake is not necessarily a GT at its intended clock, and this
had never been measured.

## The probe

`/rps` is read only: DIOC 1 without the forcewake flag, 120 captures 250 ms
apart (about 43 s, each capture also copies the GTT head). The VxD's
allowlist gained two narrow windows for it, `138100h-138110h` and
`145940h-1459A0h`. Every address and field is i915's (`i915_reg.h`), not
the public PRM's:

- `RPNSWREQ` `A008h`, request in 31:24 (`HSW_FREQUENCY`);
- `RPSTAT1` `A01Ch`, actual clock in 13:7 (`HSW_CAGF`);
- `RP_STATE_CAP` at MCHBAR mirror `145998h`: RP0 7:0, RP1 15:8, RPn 23:16;
- the RP evaluation counters `A050h`-`A060h`, PM interrupts `44024h`-`4402Ch`,
  RC6 residency `138108h`;
- 50 MHz per unit.

## Readings

| | idle desktop | 3DMark 2001 SE, ~10 s in | same run, ~2 min in |
|---|---|---|---|
| requested (`RPNSWREQ`) | 400 MHz, all 120 | 400 MHz, all 120 | 400 MHz, all 120 |
| actual (`RPSTAT1` CAGF) | 400 MHz, all 120 | 400 MHz, all 120 | 400 MHz, all 120 |
| `CUR_UP`/`CUR_UP_EI` mean, max | 0 %, 0 % | 30 %, 100 % | 58 %, 100 % |
| `PMIIR` non-zero | 0 of 120 | 0 of 120 | 0 of 120 |

Fixed across all three:

| Register | Value | Reading |
|---|---|---|
| `RP_STATE_CAP` `145998h` | `00040717` | RP0 1150 MHz, RP1 350, RPn 200 |
| `RPNSWREQ` `A008h` | `08000000` | 8 x 50 = 400 MHz |
| `RPSTAT1` `A01Ch` | `00040400` | CAGF 8 = 400 MHz |
| `RP_INTERRUPT_LIMITS` `A014h` | `12060000` | 18 / 6 units, as i915 lays it out |
| `RP_CONTROL` `A024h` | `00000B92` | non-zero: the BIOS set RPS up; not decoded further |
| `PMIMR` `44024h` | `FFFFFFFF` | every PM interrupt masked |
| `RC_CONTROL` `A090h` | `88040000` | as Phase A |
| RC6 residency `138108h` | `019A379F` | unchanged across every sample: no RC6, as forcewake predicts |
| `GT_PERF_STATUS` `145948h` | `0000085C` | not decoded |

The 3DMark run was the full Pro selection at 1024x768x32, compressed
textures, D3D software T&L, started by its Benchmark button. The two
loaded samples began about 10 s and 2 min after the click; which test was
on screen during each was not captured. The run completed: 2357 3D marks,
with the two probe runs (each about 43 s of reads every 250 ms) inside it.

## What it means

The render clock is 400 MHz whether the GT is idle or busy, and RP0 is
1150 MHz: the GT runs at about 35 % of its maximum clock while 3DMark keeps
it busy up to 100 % of an evaluation interval. The request is the BIOS's
(nothing else writes `A008h`), the PCU grants it exactly, and nothing asks
for more: the PM interrupts are all masked, so the up-threshold events
the evaluation counters show never reach anyone, and no driver would act
on them anyway.

This is the "render engine is now the limit" of
`2026-10-28-haswell-pat-write-combining.md` seen from the clock side, and
it applies to every Direct3D, OpenGL and BCS number taken on this machine
so far. Those numbers stand as recorded; they were taken at 400 MHz.

The Ivy Bridge record (`2026-10-21-ivybridge-gt-forcewake.md`) read
`RPSTAT1` `00040700` on W9QI4E14 without decoding it. With Gen7's CAGF at
14:8 (i915) that is 7 x 50 = 350 MHz, an idle reading with nothing
programming RPS there either. Whether it stays there under load was not
measured: this record covers only the 4570.

## Hypotheses the evidence kills

- "Forcewake keeps the GPU at full performance": it keeps the GT out of
  RC6 (residency flat) and leaves the clock wherever the BIOS left it.
- "The PCU raises the clock on its own under load": with the request fixed
  at 400 MHz, CAGF stayed at 400 MHz through 240 loaded samples.

## Not checked

- Whether a write of RP0 (or RP1) to `RPNSWREQ` is honoured, and what the
  PCU does with it under the CPU's package power limit. That is a write to
  the GPU, a design change for the HAL or mini-VDD, and wants agreement
  first. The obvious first step is one static request at boot with
  `RPSTAT1` read back, before any interrupt-driven turbo.
- Frame rates at a higher clock; the BCS and display paths at a higher
  clock.
- Which 3DMark test each loaded sample covered.
- The decoding of `RP_CONTROL`, `RP_INTERRUPT_LIMITS` and
  `GT_PERF_STATUS`, beyond recording them raw.
- Other Gen7/7.5 machines (Ivy Bridge, the HD 4000 mobile).
