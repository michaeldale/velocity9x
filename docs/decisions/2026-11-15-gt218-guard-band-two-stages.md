# GT218: a measured guard band, and two texture stages in one pass

Date: 2026-11-15

Status: implemented and measured on W9QI0DAD (ASUS GeForce 210, GT218
`10DE:0A65`, FX-6300, Win98 SE), boots 22 and 24. Step 5 of
[nvidia-gt218-ps11.md](../plans/nvidia-gt218-ps11.md), which Michael put
before step 4 so the caps never claim stages that cannot draw. DDI 8 is
still not published (step 4).

Evidence: [`../probe/w9qi0dad-gt218-two-stages-2026-11-15/`](../probe/w9qi0dad-gt218-two-stages-2026-11-15/).

## Why both at once

The core gives a back end two stages only with `clip_in_core` 0 and
`centre_in_engine` set: a clip or a pixel-centre move in the core would
part a vertex from its second coordinate pair (`d3d_internal.h`,
`texture_stages`). So the guard band had to be measured first, as Gen3's
and the HD 4600's were.

## The guard band (V9XN5CH /3d:guard, boot 22)

A new probe stage on /3d:ps11's plumbing: its 28-result vertex program
and the translated `mov r0, v0`, v0 a plane over the whole target, so a
pixel's colour says where the engine put it. For each distance D, a
triangle with every vertex D off the target ("Cover", vertices to 2D +
300) and one with a single vertex D off the left edge ("Sliver"), into a
pattern-filled 128x72 surface, scissor 8 pixels in from every edge.
Every pixel judged: none may change outside the scissor; inside, centres
more than 0.1 pixel inside the triangle must be the plane's colour within
two steps, those more than 0.1 outside the pattern.

| D | 0 | 512 | 1024 | 2048 | 4096 | 8192 | 16384 | 32768 |
|---|---|---|---|---|---|---|---|---|
| Cover: judged ok / bad / leaks | 6272/0/0 | 6272/0/0 | 6272/0/0 | 6272/0/0 | 6272/0/0 | 6272/0/0 | 6272/0/0 | 6272/0/0 |
| Sliver: ok / bad / leaks | 6228/0/0 | 6232/0/0 | 6236/0/0 | 6236/0/0 | 6228/0/0 | 6272/0/0 | 6272/0/0 | 6272/0/0 |
| worst colour step | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |

The largest coordinate drawn was 65,836, with no PGRAPH interrupt or
trap. Nothing failed, so the true limit is beyond what was drawn and
unmeasured. The HAL's band is half the furthest distance tested, 16,384
in absolute coordinates, as Gen3 halved its measured band: a triangle
with every coordinate finite and inside +/- 16,384 is drawn as given,
anything else dropped and counted (shared policy counter 19). DDI 6
programs are told `coordinate_limit` less a pixel (8191), far inside.

Not measured: depth (z) far off its range, and w other than 1; the
probe drew w 1 at z 0.5. Pre-transformed Direct3D vertices bring rhw
from the application, so a triangle with an rhw far from 1 off the target
is drawn on the strength of this measurement only.

/3d:ps11 ran again in the same run after the probe refactor (the stage
draws any triangles now): `Result=PASS` at both depths, 0 bad pixels, as
on boot 21.

## The change

- `d3d_nv50.c` limits: `clip_in_core` 0, `centre_in_engine` 1
  (`v9x_nv50_push_d3d_vertex` already added `draw->centre_offset`),
  `texture_stages` 2, `guard_band_ddi6` 1. Each triangle passes
  `v9x_d3d_nv50_guard` before its vertices go in the push.
- Vertex interface: unit 1's coordinates are attribute 9, results 14 and
  15, `v[0x2c]`; 16 results, 12 interpolants (`FP_INTERPOLANT_CTRL`
  080C040Ch). The 12 one-unit fragment programs are unchanged word for
  word (their host test holds them).
- `nv50_d3d.c`: `v9x_nv50_d3d_fp2` builds a two-unit program for a
  combine (colour REPLACE, MODULATE, DECALALPHA or ADD, saturated as
  Direct3D clamps stages; alpha FRAGMENT, REPLACE or MODULATE; specular
  and fog as the one-unit tail, now one shared function). 576
  combinations, too many to place; the HAL builds them on first use into
  64 slots of 512 bytes at reserve E8000h, in the gap between the
  scratch and the push area (`gt218_reserve.h`), addressed as FP start
  offsets 7000h on from the scratch's code base, with `CODE_CB_FLUSH`
  in the push after a program is written. A slot is written once per
  channel build; when all 64 are taken a new combination is refused and
  counted (policy counter 18).
- `nv50_3d.c`: the setup's TSC limit goes from EXA's 0 to 1, so sampler 1
  exists (`LINKED_TSC`); BIND_TIC2 already bound TIC 1 to unit 1.
- `d3d_nv50_map.c`: a draw with `texcoords1` maps both units' tables and
  translates each unit's combine; colour key, MODULATE2X (not claimed)
  and BLEND are refused.

Host tests: every one of the 576 programs runs in the Tesla model, now
shared as `tests/host/nv50_sh_sim.c`, at three fragments and matches the
combine arithmetic in C within 1e-5 (largest program 60 words); the map's
two-unit path and refusals; the reserve layout by compile-time asserts.

## On the card (boot 24)

V9XD32, the same binary as the step-0 record, at 32 bpp and `/bpp16`,
against boot 21's reports:

| | boot 21 | boot 24 |
|---|---|---|
| T12 two stages (DX6 device) | `SKIPPED-ONE-STAGE` | `PASS`, `T12Ok=FF`, `BlendStages=2` |
| T13 two-unit form | not drawn | `PASS`: 49 DP2 calls on `engine-two-unit` (the core's two-unit counter +263), centres drawn, nothing stray |
| T10 | `Ok=3F` | `Ok=3F` |
| every other `*Ok`, `*Result` | | identical at both depths |
| `Failures` | 1 (T20) | 1 (T20) |

3DMark 2000, default run, same boot after V9XD32 (snapshots before and
after; score recorded, not compared): completed, Overall Score window
4271, no `V9XN50F.TXT` fault record. Between the snapshots the core's
`TwoUnitBatches` rose by 709,222 and `TwoUnitValidateRefused` by 1; the
shared policy counters did not move (no refusal, no guard drop, no full
program cache); `EngineIdleTimeouts` and `EngineFifoTimeouts` stayed 0.
No frame was inspected: this row has no display-start flips, so the
frame grab has nothing to hook. Which stage pair failed validation was
not traced.

## Gates

`build-host.ps1`, `run-checks.ps1` green; the four installed files
hash-checked against the package after `reset`.
