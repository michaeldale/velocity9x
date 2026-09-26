# Phase 2 on the netbook: the three-state drain draws the same, Gen3 passes the mixed-engine rung, and the new drain paths were never taken under load

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 0.5 and Phase 2 (shared drain)
Machine: MICHAEL-NETBOOK (10.0.1.254:9869, agent `dev-inv2`), 945GSE /
GMA 950, Windows 98 SE, 1024x576x16. Boot 38: the Phase 1 driver set
(HAL 111,616 bytes, 4808fb8). Boots 39-40: the intel-gma package built at
d7a07ce (driver 104,932, HAL 120,320, mini-VDD 22,588), deployed by the
WININIT rename; all three sizes checked on the guest.
Evidence: `2026-09-26-phase2-gen3-V9XDD-{before,after}.ini` and
`...-V9XDD2-{before,after}.ini` (both report parts of `V9XDDP.EXE /mixed`,
probe built at c491857), `2026-09-26-phase2-gen3-V9XSNA7-run1.ini`
(V9XTRACE after 3DMark on boot 39), `...-3dmark99-settings.png`,
`...-3dmark99-score.png`, `...-3dmark99-score-run2.png`

## Measured

- **V9XDDP, before and after, both parts.** Outside per-process keys - the
  runtime's pointers (`Gbl*`, `GblRaw*`), the HAL callback addresses in
  `CbRaw*`, texture handles and timings - nothing differs. `GblRaw0180`'s
  first word also moves (`1CFF` both runs on boot 38, `0CDF` after); it
  moved between this morning's before and after as well (`0CD7`/`0CC7`) and
  is a runtime-internal word, not a result key.
- **Phase 0.5 on Gen3: passes, on both drivers.** `MixedOrderingOk=1`,
  `MixedColorEncodingOk=1`, `MixedDepthEncodingOk=1`, `MixedOk=1`: first
  hardware colour `0xF800` (the RGB565 target's red), depth `0x8000`, CPU
  store `0x07E0`/`0x4000`, rejected pixel unchanged, neighbour `0x001F` at
  depth `0x6000`. Unlike the ViRGE, Gen3 writes the target's declared 565,
  so its colour encoding does not block fallback.
- **The drain under 3DMark 99.** After a full run on boot 39:
  `RenderDrainWaits=0`, `RenderDrainStalls=0`, `BreadcrumbTimeouts=0`,
  `BreadcrumbAbandoned=0`, over 326,930 breadcrumb submits and 2,981 flips.
  No drain found work outstanding, so the BUSY and ABANDONED branches, and
  the DestroySurface loop added in c491857, were never taken. The run
  completed; nothing hung.
- 3DMark 99 read 636 and 634 on two boots. It is recorded, not compared:
  the Phase 1 gate this afternoon's work owes is correctness, and the
  morning's 669 on the Phase 1 driver is not investigated further.

## Also found

`V9XDDP` writes its report in two files (`ResultFiles=2`); the texture
matrix, the Z section and every `Mixed*` key are in `V9XDD2.INI`. The
Phase 1d Gen3 record's "every result key the same" compared part 1 only.
Part 2 was not captured then, so that half of the Phase 1 comparison
cannot be recovered; this record's before/after covers both.

## Standing

The three-state drain draws the same on Gen3 and Gen3 passes the Phase 0.5
ordering, depth and colour cells. Not exercised: a drain that actually
waits, times out or abandons - which needs fault injection, not a
benchmark - and the texture-update and blended-overlap cells. The netbook
stays on the d7a07ce set.
