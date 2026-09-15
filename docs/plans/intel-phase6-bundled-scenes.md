# Phase 6 on the GMA 950: many scenes per boot, one feature per scene

## The problem this solves

The parent plan says "one feature per build" and lists six features: a texture,
a texture-stage operation, depth test without writes, depth writes and clear,
alpha test, source-alpha blend. Taken literally that is six armed cold boots,
and Phase 5 needed seventeen captures to land one triangle. At that rate Phase
6 is a month of boots.

Boots are the scarce resource. Code is not.

## The shape that bundles without losing attribution

**One boot carries many scenes. Each scene is a complete, independent draw with
its own state block, its own target region, its own probes, and its own results
flushed to disk before the next scene begins.**

That is not the same as stacking features into one scene. The rule that makes
bundling safe:

> Scenes may share a boot. A scene may not depend on the state left by another
> scene.

Every scene re-emits complete state, which Phase 5 already does — "complete
state re-emitted every draw" was a Phase 5 constraint and it is what makes this
possible now. A scene that inherited state from its predecessor would make a
failure unattributable, which is the property the one-feature-per-build rule
was protecting. Independence preserves it at a fraction of the cost.

### Why a hang does not cost the boot's evidence

The sequencer flushes after every step (`v9x_p5_flush`, added when
`IntelArmPhase` never reached disk). Scene *n*'s probes are on disk before
scene *n+1* submits anything. A hang in scene 4 leaves scenes 1–3 complete and
names scene 4 as the one that wedged — which is *better* localisation than a
one-feature boot gives, because the three preceding scenes are a working
baseline captured minutes earlier on the same boot.

This is the same argument as the intent markers, and it is why those exist.

### What still cannot be bundled

**A scene whose expected result is "identical to last time" must run first.**
The depth `BUF_INFO` removal
([issue](../issues/2026-09-15-intel-depth-buf-info-at-address-zero.md)) is
exactly this: its whole value is that the output is byte-identical to
`C:\temp\intel42`. As **scene 0**, running before anything else touches the
GPU, that comparison is still exact and still attributable to the removal
alone.

This refines what that issue says. It asked for the removal on its own boot;
that was written to protect attribution, and scene 0 of a bundle protects it
equally as long as nothing precedes it. The issue is updated to say so rather
than left contradicting this plan.

**A feature that changes the shared state block** — as opposed to adding to one
scene's — still needs its own scene, and that scene's probes must be read
against the scene that lacks the change.

## The read budget, stated because it is the known hazard

Bulk aperture reads hang this part:
[record](../decisions/2026-09-15-bulk-aperture-reads-hang-the-945gse.md).
Three single reads succeeded and 153,600 hung. The current capture performs
roughly 45 and completes.

Scenes multiply reads. The budget is therefore explicit, published in the
capture, and raised one step at a time:

- Every capture publishes a running **total aperture read count**, not just a
  per-scene one. A hang then names both the scene and the cumulative read at
  which it stopped, which is the number this hazard is actually about.
- The first bundled boot targets **four scenes, ~60 reads total**. That is
  within a third of a factor of what already works.
- No boot raises the total read count by more than roughly double the last
  boot that completed.

If a boot hangs, the read count at the stopping point is the measurement, and
it goes in a decision record. That is the `-zc`-style bounded experiment the
parent plan wanted and never got, obtained for free from work being done
anyway.

## Build 1: four scenes

| # | Scene | Feature | Expected |
|---|---|---|---|
| 0 | Phase 5 triangle, depth `BUF_INFO` removed | none — regression | Byte-identical to `C:\temp\intel42`: seven probes `1C3E`, seven `0842` |
| 1 | Same triangle, colour with a **separating green byte** | none — colour | Closes the green `round`-vs-`trunc` question ([issue](../issues/2026-09-15-intel-565-conversion-outside-measured-values.md)) |
| 2 | Two triangles sharing an edge, different colours | none — edge rule | First evidence on the fill rule ([issue](../issues/2026-09-15-intel-edge-fill-rule-unmeasured.md)) |
| 3 | One opaque RGB565 texture, nearest-clamp | **texture** | The parent plan's first real Phase 6 feature |

Scenes 0–2 need **no new hardware capability**: they are the same packets with
different vertices and colours. They close two filed issues and validate one
removal, and they cost nothing but capture space on a boot that was going to
happen for scene 3 regardless. That is the whole argument for bundling.

Scene 3 is the only one that needs new state — texture buffer, map and sampler
state, a texture-stage program — and if it hangs, scenes 0–2 are already on
disk.

### Scene 1's colour

Green byte **3**: `round(3*63/255) = 1`, `3>>2 = 0`. Red and blue chosen to
separate at values not yet tested, so the boot re-tests all three channels
rather than one. Exact colour and its prediction table to be written **before**
the build, as with the last colour — the prediction is what makes the result
evidence rather than a fit.

## Host-side work, all testable without the machine

- A scene descriptor and a sequencer loop over it, replacing the single
  hard-coded Phase 5 draw. Pure arithmetic and packet assembly.
- Per-scene target regions inside the existing reserve, with the same guard
  dwords either side of each. The allocator is leaf arithmetic with a unit
  test; overlapping regions must be a build-time refusal, not a runtime one.
- Per-scene CRC, and a combined CRC over all scenes in execution order —
  the Phase 4/5 chain already establishes this pattern.
- Capture schema 3: per-scene sections, per-scene probes, the running read
  count, and a per-scene intent marker.
- The capture validator extended to require every declared scene's section and
  probe set, and to fail on a missing one rather than report on what is present
   — the partial-probe-set defect, which has now recurred three times.
- Texture format, pitch and mip-offset maths; map and sampler state; the small
  texture-stage compiler. All leaf units.

## Exit criterion for Phase 6

Unchanged from the parent plan: the six features working, each with an isolated
scene, a software reference and a capture. What changes is that they arrive in
perhaps three boots rather than six, and that three filed issues close as a
side effect of boots taken for other reasons.

## Kill criteria

- **A scene count that hangs.** If four scenes hang where one does not, and the
  read count is not the cause, scenes are not independent in the way this plan
  assumes and the bundling is wrong. Record it and fall back to one feature per
  boot.
- Unchanged from the parent plan: undocumented packet experimentation to avoid
  hangs, or a general shader compiler becoming necessary.
