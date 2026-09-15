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
its own state block, its own probes, and its own results flushed to disk before
the next scene begins.**

Scenes share the one render target and each fills it before drawing. There is no
room in the reserve for more, and none is needed: see the host-side section.

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

## Blocked: the recorded execution scope forbids this

**Nothing in this plan may be armed until the Phase 5 errata decision is
amended, and that amendment is a risk decision, not an engineering one.**

`docs/decisions/2026-09-15-intel-phase5-errata-gate.md` limits the scope twice,
and the second time in terms that cover exactly what this plan proposes:

> It does not authorise anything beyond one triangle. A sustained or repeated
> 3D workload is a different assessment, and erratum 7's word "extended" is the
> reason.

and, in the 2026-09-15 repeat-mode amendment:

> The scope of the original decision is unchanged: one triangle per boot.
> Nothing here permits sustained or repeated 3D work within a boot, and nothing
> here authorises a second draw in the same boot.

Build 1 as described is five draws and six triangles in one boot. The
host-side scene work does not change that and was never going to: building a
stream is not authorisation to submit it, and the code says so at the top of
`src/chipsets/intel/i9xx_scene.c`.

### What an amendment would have to weigh

Written here so the decision has something to work from, **not** as an argument
that it should be granted:

- **Erratum 7** concerns *extended* 3D operation. Five scenes is more 3D work
  than one, in the direction the erratum names. Nobody has measured where
  "extended" begins on this part, and the word is Intel's, not a threshold.
- **Erratum 12** concerns a CPU/GPU access sequence. This plan multiplies the
  CPU-side aperture reads, which is the side of that pair the driver controls,
  and the bulk-read hang already measured is the closest thing to evidence
  about it. The read budget section below is the response.
- **What has actually been survived:** two armed boots of one triangle each,
  both clean, plus the Phase 4 blit. That is the entire body of evidence about
  sustained work on this part, and it is silent on the question.
- **The cost of being wrong** is unchanged and is what the original gate was
  opened against: a hang on a scratch install, on AC power, recoverable from
  DOS. The hang-interpretation rule from the Phase 4 decision would apply.
- **A smaller step exists.** Two scenes rather than five - scene 0 and one
  other - would test the bundling mechanism itself while roughly doubling the
  3D work rather than quintupling it, and would still close one filed issue.
  If the amendment is granted narrowly, this is the shape to grant it in.

### Until then

Everything host-side proceeds: the scene table, the sequencer, the capture
schema, the validator and the arm tables. The build produces a package whose
Phase 6 arm token **no machine will accept**, because no such phase is
authorised, and that is the correct state for it to be in.

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

## Build 1: five scenes

| # | Scene | Feature | Expected |
|---|---|---|---|
| 0 | Phase 5 triangle, depth `BUF_INFO` removed | none - regression | Byte-identical to `C:\temp\intel42`: seven probes `1C3E`, seven `0842` |
| 1 | Same triangle, colour `0xFF2E03C8` | none - colour | `3038` if rounding holds; `2819` names truncation on green |
| 2 | Upper triangle of a square, alone | none - coverage | Which pixels one triangle claims |
| 3 | Lower triangle, alone, same probe pixels | none - coverage | Which pixels the other claims; the two together give double coverage or gaps |
| 4 | Both under one primitive | none - edge rule | What the hardware produces for a real shared edge |

**Status:** all five scenes are built and host-tested. Scene 0 currently
carries the Phase 5 triangle **with the depth binding still present** - the
scene table landed first, and the removal is its own diff so that the one
change whose expected result is "no change at all" is not mixed into the
refactor that introduced the table.

The texture scene, the parent plan's first real Phase 6 feature, is **not**
in build 1. It was going to be, and the edge experiment growing from one
scene to three is why it is not: five scenes already sit well past what the
recorded execution scope permits, and adding a sixth that needs new hardware
state would mix an unproven capability into a boot whose other four scenes
need none.

**Not one of these five needs a hardware capability the chip has not already
demonstrated.** They are the same packets with different vertices and
colours. Between them they validate the depth-binding removal and close two
filed issues - the green conversion question and the edge rule - for the
cost of capture space.

That is a weaker argument for bundling than the original one, and worth
saying: the first version of this plan justified the extra scenes as free
riders on a boot that had to happen anyway for the texture. With the texture
deferred, these five scenes are the whole reason for the boot. They are
still worth it - three open questions closed in one boot instead of three -
but they are no longer free.

### Scene 1's colour

Green byte **3**: `round(3*63/255) = 1`, `3>>2 = 0`. Red and blue chosen to
separate at values not yet tested, so the boot re-tests all three channels
rather than one. Exact colour and its prediction table to be written **before**
the build, as with the last colour — the prediction is what makes the result
evidence rather than a fit.

## Host-side work, all testable without the machine

- A scene descriptor and a sequencer loop over it, replacing the single
  hard-coded Phase 5 draw. Pure arithmetic and packet assembly.
- ~~Per-scene target regions inside the existing reserve, with a per-scene
  allocator.~~ **Wrong, and corrected by doing the subtraction this plan should
  have done before asserting it.** The reserve is `0x100000`; the ring, status
  page, scratch page, target and upper guard already consume `0xA9000`, leaving
  `0x57000`. A second 640x480x16 target needs `0x96000`. There is no room for
  one, let alone three, and no allocator changes that.

  Scenes therefore **reuse the single target**, each filling it before drawing.
  A scene's pixels are destroyed by the next scene's fill, and that costs
  nothing: the probes are the evidence and they are already on disk, by the
  same flush-after-every-step property that makes a hang localisable. It also
  removes the allocator, its overlap checks and its unit tests from the work
  entirely.
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
