# Intel documents the 945's fill rule: top-left, for D3D and OpenGL alike

Date: 2026-09-17
Status: accepted

## Context

The GMA 950's edge fill rule was measured on 2026-09-16 for one slope, one
diagonal, three probe pixels (`docs\issues\2026-09-15-intel-edge-fill-rule-unmeasured.md`,
record in `2026-09-16-intel-phase6-five-scenes-green-rounds-edges-clean.md`):
exactly one of two triangles sharing an edge claims each pixel whose centre
lies on it, and it is the one for which the edge is a left edge. That matched
the top-left convention, but the record was careful to close only the quad
case. Horizontal and vertical shared edges, other slopes and degenerate
triangles stayed unmeasured, and there was no written source for Gen3 to name
the rule the hardware was following. Mesa's i915 drivers and xf86-video-intel
never state a fill rule; the only written statement was in the 965 PRM, which
describes Gen4.

A desk search on 2026-09-16 and 17 (recorded in
`C:\everything\claude\personal\intel driver research\gen3-d3d-research-notes.md`)
confirmed that no public Gen3 3D programmer's reference exists. It did turn up
the one place Intel wrote the rule down for this silicon.

## Evidence

Intel 945G/945GZ/945GC/945P/945PL Express Chipset Family Datasheet, document
307502-005 (June 2008), section 10.5.3.6 "Pixel Rasterization Rules":

> The GMCH supports both OpenGL and D3D pixel rasterization rules to determine
> whether a pixel is filled by the triangle or line. For both D3D and OpenGL
> modes, a top-left filling convention for filling geometry will be used. Pixel
> rasterization rule on rectangle primitive is also supported using the
> top-left fill convention.

The Mobile Intel 945 Express Chipset Family Datasheet, 309219-006, carries the
same paragraph at section 10.4.1.5.1. That is the netbook's own part (945GSE).
Both PDFs are saved under
`C:\everything\claude\personal\intel driver research\gen3-915-945-datasheets\`;
intel.com no longer serves them and they came from the Wayback Machine.

For comparison, the 965/G35 PRM Vol 2 section 8.3.3 states the Gen4 rule in
hardware terms: a pixel whose sample point intersects an edge is inside if the
edge is a left or top edge, top and bottom edges being exactly horizontal.
Gen3 is a different pipeline, so that text is corroboration, not a source.

What the datasheets do **not** say: how vertex coordinates are snapped before
the rule is applied, what "line" rasterization rule is meant, and anything
about degenerate triangles. The datasheet is prose; there is no bit-level
statement and no diagram.

## Options

**Treat the rule as measured only.** Keep the 2026-09-16 wording, which closes
the quad case and no more. Safe, but it leaves the driver's software reference
rasteriser with no documented rule to match, and every future shared-edge scene
would have to re-derive the convention from probes.

**Treat the rule as documented, hypothesis for the unmeasured cases.**
Accepted. Intel's own datasheet for this silicon names top-left. The one
measured slope agrees. The remaining slopes and the horizontal and vertical
shared-edge cases become tests of a stated rule rather than open questions.

**Treat the rule as established for all cases.** Rejected. Three probe pixels
on one diagonal do not test horizontal or vertical edges, and the datasheet
does not describe snapping, which is where top-left implementations differ in
practice (which sub-pixel grid the sample point sits on, and whether a vertex
exactly on a pixel centre is snapped before or after the edge test).

## Decision

The GMA 950's triangle and rectangle fill rule is recorded as **top-left, as
documented by Intel in datasheets 307502 and 309219**, and confirmed by
measurement for one slope on 2026-09-16.

The software reference rasteriser in the D3D core is to be compared against
this rule at edges, not licensed to differ. The one-pixel band along edges that
the software-versus-hardware comparison has so far excused
(`docs\plans\hardware-d3d-on-intel-gma950.md`, Phase 6 verification) narrows to
the snapping question only: a disagreement at an edge pixel is now a finding
about snapping, or a bug, not an unknown rule.

## Consequences

- The issue `2026-09-15-intel-edge-fill-rule-unmeasured.md` gains a source and
  stays at "measured for one slope"; the open cases are the same three
  (horizontal and vertical shared edges, other slopes, degenerate triangles) but
  they are now tests of a documented rule.
- When those cases are measured, a result that disagrees with top-left is
  evidence against Intel's datasheet and must be recorded as such, with the
  vertex coordinates, so the snapping model can be worked out.
- No code changes. The engine emits vertices; the rule is the hardware's.
- The same datasheet section is silent on the three other questions Phase 6
  carried forward (depth Z scale, destination alpha on a 565 target, the 565
  conversion outside the measured values). It describes dithering only for the
  enabled path and depth only as "16- or 24-bit Z or W". Those stay measurement-
  only; nothing written exists for them, on Gen3, from Intel or anyone else.
