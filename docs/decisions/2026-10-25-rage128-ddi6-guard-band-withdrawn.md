# The Rage 128 is not told of its guard band at DDI 6: told, 3DMark 2001 SE corrupted or hung A8U4I5

Date: 2026-10-25

Status: decided and measured on A8U4I5 (Rage Fury Pro 5046, Win98 SE),
boots 630-638. Supersedes the Rage 128 half of
[2026-10-25-haswell-guard-band.md](2026-10-25-haswell-guard-band.md),
which published the DDI 6 guard band for every engine (Michael's "publish
it") and had not been run on the Rage 128.

Evidence: [`../probe/a8u4i5-ddi6-guard-band-2026-10-25/`](../probe/a8u4i5-ddi6-guard-band-2026-10-25/).

## What changed

`V9X_D3D_ENGINE_LIMITS.guard_band_ddi6`, appended. The core publishes
`dvGuardBand*` (coordinate_limit - 1) at DDI 6 for an engine the core
clips for, as before 1d8d3045, and for an engine with `clip_in_core` 0
only when this field is set. The HD 4600 sets it. The Rage 128 does not,
which puts it back where it was from 09379ddd to 1d8d3045: it draws its
own guard band for DDI 5 programs, and at DDI 6 the runtime clips first.

## Boots

| Boot | RAM | Build | V9XD32 /ddi6, /ddi5 (`/t14nomode`) | 3DMark 2001 SE (DirectX 8, DDI 6) |
|---|---|---|---|---|
| 630 | old stick | 5dcbdb32 | PASS, PASS | 314 |
| 631 | old stick | 1d8d3045+ (band at DDI 6) | first runs FAIL on T15 only (bit 23 set in heap words, seen before any 3D); second runs PASS | "adboard1_l0.kf2 is corrupted", twice |
| 632 | old stick | same | PASS, T15 clean | "car_schoolbus_l0.kf2 is corrupted" |
| 633 | old stick | 5dcbdb32 | (stopped by Michael to swap RAM) | - |
| 635 | new 128 MiB stick | 1d8d3045+ | PASS, PASS | machine hard-hung about 4 min in (no ping); power-cycled |
| 637 | new stick | 5dcbdb32 | PASS | 311, then 311 |
| 638 | new stick | this change (band withheld) | PASS | 311, then 309 |

"1d8d3045+" is this branch at c67abfb9: 1d8d3045, 45bfb395 and main's
tools merged. For the Rage 128 its only difference from 5dcbdb32 that
reaches a Direct3D draw is the published band. The others are the
`v9x_wc_drain` before a plane flip (a few cycles), the Haswell and
Gen3 files, and the WC window rule. The window rule plans the same
16 MiB pair here: `MtrrWrite=ok s=3 b=dc000001 m=ff000800` on every boot.

## Readings

- **With the band, 3DMark 2001 SE failed four runs of four**: three
  corrupt data files while loading, one hard hang. Without it, five of
  five completed, two of them on the new stick after the hang.
- **V9XD32 does not see it.** T10's off-target triangles, now handed to
  the engine at DDI 6, drew within two steps a channel of the runtime's
  clip (A15C01 7B7F04 54A208 against A05C00 797F03 52A207), with T10Ok
  full. Whatever 3DMark 2001 SE sends that breaks it is outside what
  T10 draws.
- **The corrupt-file error is the AGP-ring issue's symptom**
  ([2026-10-25-rage128-32mib-map-corrupts.md](../issues/2026-10-25-rage128-32mib-map-corrupts.md)),
  here with the ring parked and the CCE fed by PIO. In both cases the
  engine took Direct3D's off-target geometry: the ring build drew inside
  its guard band at DDI 5. So that issue's ring hypothesis may share a
  cause with this. Not tested.
- **The RAM fault was present on boot 631** (T15's bit-23 flips on the
  first runs, before any 3D), so 631 alone proves nothing. Boot 632 had a
  clean T15 and still corrupted. Boot 635, on the new stick, hung.

## Open

- Why the Rage 128 corrupts with DDI 6 off-target vertices and not with
  DDI 5's (3DMark 2000 and Half-Life at DDI 5 ran inside the band on
  2026-10-24). Candidates not separated: DrawPrimitives2's own vertex
  conversion, vertices just inside +/- 2048, or triangles larger than
  the setup engine takes.
- The "hit" files differ run to run, as in the ring issue.
