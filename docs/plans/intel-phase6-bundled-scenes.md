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

## Execution scope: authorised 2026-09-16, bounded at five draws

This plan was blocked when it was written. The Phase 5 errata decision limited
a boot to one triangle and said in terms that nothing in it "authorises a
second draw in the same boot", and five draws is what build 1 needs.

**Amended 2026-09-16 by Michael Dale:** up to five independent draws per armed
boot. Asked first as a second draw and widened to five when the consequence of
the narrow form was put - the shared-edge experiment needs three scenes and
would not have fitted beside the regression scene. The weighing, and what
remains unauthorised, are in
[the errata gate's amendment](../decisions/2026-09-15-intel-phase5-errata-gate.md).

**The authorisation rests on a condition this plan already required:** each
draw re-emits complete state, fills its own target, and flushes its own probes
before the next submits. A build in which one draw inherited another's state
would be outside the amendment even at a count of two. That is why the
independence rule above is a rule and not a preference.

Five is a bound, not a target. Raising it needs another decision recorded in
that file, which is what the authorised-draw constant in the code exists to
make visible.

## The read budget, and the figure this plan had wrong by ten times

Bulk aperture reads hang this part:
[record](../decisions/2026-09-15-bulk-aperture-reads-hang-the-945gse.md).
Three single reads succeeded and 153,600 hung. The Phase 5 capture that
completed performed about 45.

**This section previously said "four scenes, ~60 reads total". That was the
probe count mistaken for the read count.** The probes are the smallest part of
what a boot reads, and the budget rule below was being applied to the wrong
quantity — which is worse than having no rule, because a tenth of the real
figure looks like headroom.

What a Phase 6 boot actually reads through the aperture:

| Source | Reads | Why |
|---|---|---|
| Staging read-back | 330 | The mini-VDD reads back every dword it stages, one per dword across all five scenes |
| Scene verify | 330 | Each scene's verify step compares its whole staged stream against the generated table |
| Pixel probes | 52 | 14 + 14 + 8 + 8 + 8 |
| Guards and heap | 14 | Both guards before the run and after each scene, heap probe either side |
| **Total** | **~726** | |

Against 45 on the last boot that completed. That is a factor of sixteen, not
the factor of one-and-a-third the old figure implied.

**This does not mean the boot is unsafe, and it does not mean it is safe.**
What it means is that the number nobody had computed is large, and the
comparison that matters — 726 against a hang measured at 153,600 and a success
measured at 45 — sits in between with no evidence either side of it. The
decision to spend the boot is a risk decision like the draw count was, and it
is recorded as one rather than buried in a table.

Every capture publishes the figures rather than assuming them:

- `ExpectedApertureReads` before any read happens, so a hang is measured
  against a number already on disk.
- `DriverApertureReads`, incremented and flushed **before** each read the
  driver makes, so a lock names the read rather than being inferred from the
  gap where it stopped.
- `MiniApertureReads`, computed from the scene table, for the reads the
  mini-VDD makes on the driver's behalf. These are not observable from the
  driver as they happen, which is exactly why they are computed and published
  rather than counted.

The rule stands, now applied to the right quantity: no boot raises the total
aperture reads by more than roughly double the last boot that completed. This
one raises it by sixteen times, so **the first Phase 6 boot is itself the
experiment** — if it hangs, the count at the stopping point is the
measurement, and it goes in a decision record.

### The cheaper shape, if that is judged too large a step

The verify step is 330 of the 726 and is a defence against staged memory
changing between staging and submission. Running fewer scenes per boot scales
everything linearly: two scenes is about 290 reads, which is six times the last
success rather than sixteen. The scene count is one constant and the
authorised bound is another, so this is a decision to take, not a rewrite.

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
