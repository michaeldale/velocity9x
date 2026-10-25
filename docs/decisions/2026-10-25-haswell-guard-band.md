# The HD 4600 draws inside its guard band; the core publishes the band for engines that draw it

Date: 2026-10-25

Status: implemented, measured on the 4570 (W9QI5A83, HD 4600) at boot 81
against boots 77 (core clipping) and 80 (indexed draws, core clipping).
The core change also changes what the Rage 128 publishes at DDI 6; that
is not measured here (A8U4I5 belongs to another session).

Evidence: [`../probe/haswell-guard-band-2026-10-25/`](../probe/haswell-guard-band-2026-10-25/);
boots 77 and 80 in [`../probe/haswell-indexed-cull-2026-10-25/`](../probe/haswell-indexed-cull-2026-10-25/).
Before it: [2026-10-25-intel-indexed-draws-and-hardware-cull.md](2026-10-25-intel-indexed-draws-and-hardware-cull.md).
The Rage 128's: 09379ddd, [d3d-core-throughput.md](../plans/d3d-core-throughput.md), "Guard band, measured".

## What the PRM licenses

HSW PRM Vol 7, "Vertex XY Clamping and Quantization": screen-space x and
y are clamped to [-32K, 32K-1] after the drawing-rectangle origin, and an
object whose bounding box exceeds 8K in x or y "will not match the
intended result"; software must clip so neither happens. "Drawing
Rectangle Clipping": no pixel outside the clipped drawing rectangle is
drawn. This driver sets that rectangle to the target, or to an explicit
draw's scissor (`v9x_d3d_hsw_draw_desc`), and the CLIP stage stays off.

## What changed

- **`d3d_hsw.c`:** `clip_in_core` 0, `coordinate_limit` 8192 as before.
  Each triangle the core hands over unclipped is classified by
  `v9x_d3d_hsw_guard`:
  - inside +/- 8192 with a span under 8K in both axes: drawn as given;
  - span reaching 8K: cut to the target by the core's own clipper
    (`v9x_r3d_clip_triangle`) and drawn as its fan, which is exactly
    what the core did before;
  - a vertex past the band or not finite: dropped, as the core's clipper
    refuses it. Finiteness is tested on the bits, since Open Watcom's
    compares do not see NaN (2026-09-25).
  A two-unit triangle that would need the cut is dropped. The render
  interface clips those to the drawable first, so it is not expected.
- **The indexed path** now takes ranges whose vertices lie within
  +/- 4096 after the pixel-centre move: no triangle of such a call can
  span 8K. Anything outside is declined as `IndexedDeclineGuard`.
- **`d3d_core.c`, `v9x_d3d_extended_caps7`:** the DDI 6 guard band,
  `coordinate_limit - 1`, is now published for every engine. Before, only
  engines the core clips for got it. The Rage 128 has drawn inside its
  own band since 2026-10-24 and so published none, and the runtime
  clipped every DDI 6 triangle on the CPU (Michael chose to publish it).
  The only other engine with `clip_in_core` 0 is now the HD 4600. Every
  other engine is clipped in the core, so for them nothing changes.

## Measured (Haswell, 800x600x16 desktop)

| | Boot 77 core clip | Boot 80 indexed | Boot 81 guard band |
|---|---|---|---|
| V9XD32 /ddi5 | PASS | PASS | PASS, every value equal to boot 80 |
| V9XD32 /ddi6 | PASS | PASS | PASS; all equal but T10's Gouraud samples |
| T10 Gouraud samples, /ddi5 | A05C01 798004 52A208 | same | same |
| T10 Gouraud samples, /ddi6 | A15D01 7B8005 54A309 | same | A25C01 7B8004 54A308 |
| DB3D probes | 174 pass | 174 pass | died after its last test, then 174 pass on the rerun |
| Half-Life mwd5 D3D / GL, windowed | 65.7 / 79.1 | 65.3 / 78.9 | 65.4 / 77.5 |
| 3DMark 2000 default | 1959 | 2032 | 2009 |

| Trace, over each boot's runs | Boot 77 | Boot 80 | Boot 81 |
|---|---:|---:|---:|
| Triangles clipped in the core | 1,055,314 | 1,078,965 | 0 |
| Indexed calls | 528,719 | 545,019 | 542,646 |
| drawn whole by the engine | 0 | 392,456 | 472,191 |
| declined: vertex off target / past half the band | | 81,967 | 0 |
| declined: range over a slot | | 61,107 | 60,696 |
| declined: state | | 9,552 | 9,822 |
| Timeouts of any kind | 0 | 0 | 0 |

Readings:

- **The engine draws what the core drew.** T10's band, its edges and
  the no-wrap row pass on every boot. At DDI 5 the Gouraud samples are
  equal to the core-clipped reference's.
- **At DDI 6 the three Gouraud samples moved by at most two steps in a
  channel.** At DDI 6 the triangles now reach the engine whole, where
  boots 77 and 80 had them cut by the core first. The cut adds vertices
  whose colours are stored back as 8-bit D3DCOLORs, so the reference
  interpolated rounded corners and boot 81 interpolates the
  application's own. Why DDI 5's samples did not move as well is not
  established: they were equal on all three boots.
- **No core clipping at all**, and every off-target indexed call that was
  declined now goes whole: 87% of indexed calls on the engine.
- **The scores do not separate the builds.** 2032 and 2009 are two
  single runs, no change in the expected direction; not chased.

## What the evidence disputes or leaves open

- **DB3D died after its last test** on boot 81's first run (every test,
  Cube included, verified; no `[Result]`), and the rerun completed. This
  is the third sighting of exactly this death: the Rage 128 under WC
  (boot 576), the netbook uncached (boot 253, before any Haswell change),
  and now here. It is not write-combining and not this change; cause not
  known.
- **The Rage 128 at DDI 6** now publishes a 2047 guard band. Its engine
  accepts vertices inside +/- 2048 and drops anything past them (V9XD32
  T10 at DDI 5, A8U4I5 boot 549). DDI 6 programs will now send such
  vertices instead of having the runtime clip them. Not run on A8U4I5.
- **Frames not watched**, as before: 3DMark full screen and no OBS on
  this machine.
