# The GMA 950's triangle fill rule at edges is unmeasured

**Status:** MEASURED 2026-09-16 for one slope; see the limit below.
**Chip:** 945GSE A3, `8086:27AE` rev 03, MICHAEL-NETBOOK.

## What is unknown

Which pixels along a triangle's edges the hardware considers covered: the
top-left rule or some other convention, the subpixel snapping that precedes it,
and whether a pixel exactly on a shared edge is drawn once, twice or not at
all.

## Evidence, and why there is none

Two successful draws, `C:\temp\intel41` and `C:\temp\intel42`. Fourteen probes
each: seven interior, seven exterior, **all deliberately placed away from the
edges**. The interior probes sit at the centroid, near each vertex, and at
three midpoints; the exterior probes sit at the corners and outside each edge.

The probe set was designed to answer "did a triangle appear, in the right
place, in the right colour". It answers that. It cannot answer anything about
edges, and no reading of these captures should be offered as evidence about
them. The software reference comparison has a one-pixel band along the edges
explicitly licensed to differ for this reason.

## Why it is deferred rather than scheduled

Nothing currently drawn depends on it. A single triangle on a flat background
looks correct whichever rule holds; the disagreement is confined to a
one-pixel boundary that no probe samples and no consumer reads.

It stops being deferrable when any of these arrive:

- **Shared edges.** Two triangles meeting along an edge — the quad case, and
  therefore almost every real mesh. A wrong rule shows as seams (dropped
  pixels) or as double-drawn pixels, which blending makes visible.
- **Adjacent triangles** in a strip or fan, same reason.
- **Pixel-exact coverage** being asserted anywhere: a promoted golden capture,
  a full-target hash, or a row CRC compared against the software reference.

The first Phase 6 step that draws more than one triangle makes this a
prerequisite, not a curiosity.

## Proposed experiment

Not the current probe set with more points — the geometry has to be built for
the question.

1. Two triangles sharing an edge, forming a rectangle, in **different colours**
   so a doubly-drawn or undrawn pixel is unambiguous rather than hidden by a
   flat fill.
2. Vertices on exact integer coordinates and, in a second scene, on exact
   half-pixel coordinates, so snapping is exercised rather than avoided.
3. Probes placed **on** the shared edge and one pixel either side of it, along
   its length — a handful of points, not a grid, for the bounded-read reason in
   `docs/decisions/2026-09-15-bulk-aperture-reads-hang-the-945gse.md`.
4. A degenerate case: a zero-area triangle, which must draw nothing.

Read against the software rasteriser's rule, which is known, rather than
against a guess — and report the disagreement rather than asserting either is
correct until the pattern is understood.

## Not to be confused with

The colour conversion difference
(`docs/issues/2026-09-15-intel-565-conversion-outside-measured-values.md`).
That affects every interior pixel by a known amount; this affects only boundary
pixels by an unknown rule. A capture can show both at once.

## Measured 2026-09-16

Three scenes, `C:	emp\intel44`: the upper triangle alone, the lower alone at
identical probe pixels, and both under one primitive.

| Probe | Upper alone | Lower alone | Both |
|---|---|---|---|
| EdgeA | `1C3E` | `0842` | `1C3E` |
| EdgeB | `1C3E` | `0842` | `1C3E` |
| EdgeC | `1C3E` | `0842` | `1C3E` |

**Exactly one triangle claims each shared-edge pixel.** No double coverage, no
gap. The diagonal is a left edge for the upper triangle and a right edge for
the lower, and the upper claims it - the top-left fill rule.

The probes sample pixels whose centres lie exactly ON the diagonal, which is
the property the geometry was rebuilt for after the first version used a slope
no sample centre touched.

**What remains unmeasured:** one slope, one diagonal, three points. Horizontal
and vertical shared edges, other slopes, and degenerate triangles are all
untested. This closes the question for the case a quad presents and nothing
wider.

Record: `docs/decisions/2026-09-16-intel-phase6-five-scenes-green-rounds-edges-clean.md`.
