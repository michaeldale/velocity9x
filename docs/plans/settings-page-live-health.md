# Settings page: live health, temperature and better reporting

Date: 2026-11-17. Status: proposed; nothing coded.

The Velocity9x tab in Display Properties (`tools/diag/settings_propsheet.c`,
and the standalone `V9XSET.EXE` on `settings_status.c`) shows what the
driver published at its last Enable: adapter, video memory, mode switching,
DirectDraw and Direct3D mode. It does no hardware access, and that stays
so. What it cannot show is how the card is doing now. On 2026-11-17 that
was the question every time: whether the 3D engine had been given up
(`EngineIdleTimeouts`), how hot the card was, how much texture memory was
left, and whether a stall record had been written. The answers had to be
dug out of V9XTRACE snapshots, `V9XCZCP.TXT`, and a register probe run by
hand (docs\issues\2026-11-17-oland-3dmark2001se-cp-stall-hang.md).

The table of rows the page cannot fill yet is
[2026-10-09-settings-page-data-gaps.md](../issues/2026-10-09-settings-page-data-gaps.md);
this plan adds to it rather than repeating it.

## What the page should show

A "Health" group, refreshed while the page is open:

| Row | Source | Families first |
|---|---|---|
| GPU temperature, now and peak since boot | the engine backend's sampler (below) | ati Oland (CG_MULT_THERMAL_STATUS 714h, measured: 33-35 C under 3DMark) |
| 3D engine state: running, idle, or given up (quarantined), with the time and tag of the last fault | the HAL's diagnostics (`EngineIdleTimeouts`, the fault tag) | every engine with a ring |
| Fault records present: `V9XCZCP.TXT`, `V9XCPSLW.TXT`, `V9XCPWPT.TXT` | the files' presence and date | ati VI/SI |
| Texture memory: heap size, free, largest free block | the HAL's placement counters | every family that places textures |
| Direct3D features as published: DDI level, pixel shader version, cubes, signed and DXT formats, texture stages | the HAL's published caps, written to an INI | every Direct3D engine (closes the data-gaps issue's Direct3D row) |
| Driver and HAL build, and whether they match | `DriverBuild`, `HalBuild` | all |

The clipboard report gains the same rows, so a field report carries them
without V9XTRACE.

## How the values reach the page

- **Temperature.** The engine backend reads the sensor at a low rate:
  once a second at most, or every few hundred flips, never in a loop. On
  2026-11-17 the HAL's tight polling of one CP register was the likely
  cause of the CP stall, so the rate is the point. The read lives in the
  backend (for the Oland, `eng_vi.c`). The core stores the value and the
  peak in two appended diagnostics fields, as the layering rule asks.
  - On by default, with no INI switch (the no-gated-defaults rule).
  - Carrizo and Mullins keep their sensor behind the SMC index/data pair,
    where a stray read can disturb the SMC. They wait for their own
    measured step.
  - Other families report "not available" until each has a measured
    sensor read.
- **Engine state and texture memory.** These are already in the HAL's
  diagnostics block. The page needs them written where it can read them:
  V9XTRACE's snapshot already does this. Either the HAL writes a small
  `V9XHEALTH.INI` at a low rate, or the page loads the snapshot code.
  Step 1 decides.
- **Direct3D features.** The HAL writes the caps it published at
  DriverInit to `V9XHW.INI` (a `[Direct3D]` section).

## Steps

1. **Decide the channel.** Choose between a HAL-written `V9XHEALTH.INI`
   and the page reading the shared block, and the refresh rate (a timer
   while the page is visible). Record the choice in a decision.
2. **Oland temperature.** Add the backend sampler and the two fields;
   V9XTRACE snapshot and `V9XBEAT.INI` carry `GpuTempC` and
   `GpuTempMaxC`. Gate: a 3DMark 2001 SE run's beat log shows the
   temperature at every poll, matching V9XSIMM's reading within a degree,
   with no stall in the run.
3. **Health rows on the page:** temperature, engine state, fault records,
   texture memory, builds. Gate: on W9QI0DAD the page shows a given-up
   engine after a forced quarantine, and the free texture memory falls
   while 3DMark runs.
4. **Direct3D feature rows** from the published caps. Gate: the rows
   match V9XD32 T17's caps on the Oland and the GeForce 210.
5. **Clipboard report** carries the new rows.
6. **Other families' sensors**, each with its own measurement: GT218,
   Intel Gen, Evergreen, then Carrizo and Mullins through the SMC.

## Risks

- A sampler that reads a register at the wrong time, or too often, is
  exactly the hazard 2026-11-17 found. Keep it rare, read-only, and in
  the backend.
- `settings_propsheet.c` has no C runtime and implements COM by hand; a
  timer on a property page has to stop when the page is hidden or
  destroyed.
- Adding diagnostics fields is an ABI change for V9XTRACE; append only.
