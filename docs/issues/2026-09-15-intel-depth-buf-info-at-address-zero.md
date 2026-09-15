# Phase 5 emits a depth BUF_INFO for a depth buffer that does not exist

**Status:** removed host-side; **awaiting the regression scene on hardware.**
**Blocks:** any Phase 6 step that touches depth, until that scene runs.
**Chip:** 945GSE A3, `8086:27AE` rev 03, MICHAEL-NETBOOK.

## What the stream does

`v9x_i9xx_build_3d_state()` emits a `_3DSTATE_BUF_INFO` with
`BUF_3D_ID_DEPTH`, describing a depth buffer at graphics address **zero**.
Phase 5 has no depth buffer: depth test and depth write are both off in S6, and
nothing allocates depth storage.

The binding is therefore unused, and it names address zero, which is inside the
aperture and is not ours.

## Evidence

Two successful armed draws with the binding present — `C:\temp\intel41` and
`C:\temp\intel42`, both `Result=PASS`, guards `A5A5A5A5` / `00000000` intact
before and after, `EIR`/`ESR` zero.

**That is not a justification, and this issue exists because it was briefly
treated as one.** The draw succeeding with depth testing disabled says only
that nothing read through the binding. It does not license the binding.

Mesa emits no depth `BUF_INFO` when there is no depth buffer. The packet audit
(`docs/decisions/2026-09-14-intel-gen3-3d-packet-audit.md`, section 8) records
depth as *declared, never referenced* — which is exactly the state that should
have prompted omitting it.

## Why it matters before Phase 6 and not after

Phase 6 turns depth testing **on** (16-bit depth test without writes, then
depth writes and clear). At that point a stale binding at address zero stops
being inert: the first step that enables depth either uses this binding or
replaces it, and if the replacement is wrong the failure will be read against a
baseline that already contained a bad binding. The unused binding must go
before anything depends on the depth path, or the two changes cannot be told
apart.

## Proposed experiment

1. Remove the depth `BUF_INFO` from the Phase 5 state block, following the
   reference path's behaviour when no depth buffer exists.
2. Host side: the stream shortens, the Phase 5 CRC changes, the generated
   artefact and both arm tables follow it. The packet decoder must still accept
   the stream and the published offsets must still locate every region.
3. **A controlled regression scene, on its own diff, running first.** The
   expected result is *byte-identical pixel output* — the same seven `1C3E`
   interior probes and seven `0842` exterior probes — so any difference is
   attributable to this change alone, and that property is the entire value of
   the measurement.

   This originally said "on its own boot, not bundled". That was protecting
   attribution, and attribution survives bundling as long as **nothing precedes
   it**: as scene 0 of a multi-scene boot
   ([plan](../plans/intel-phase6-bundled-scenes.md)), before anything else
   touches the GPU, the comparison against `C:\temp\intel42` is still exact.
   Corrected rather than left contradicting that plan. What would destroy the
   property is bundling it *into* another scene, or running it after one.

Expected: identical probes, `Result=PASS`, guards intact. Any pixel difference
means the binding was load-bearing in a way nothing predicted, and that is a
finding worth the boot on its own.

## Kill / escalation

If removing it changes the output, restore it and record what changed — that
would mean the Gen3 pipeline requires the declaration even unused, which
contradicts the reference path and belongs in the audit.

## Removed 2026-09-15

The packet is gone from `v9x_i9xx_build_3d_state`, and the **decoder now
refuses any depth `BUF_INFO`**, address zero included - it previously accepted
one at zero. An allowlist that permits the thing just removed would let it
return unnoticed.

The state block is 31 dwords rather than 34, the Phase 5 stream 63 rather than
66, its CRC `6B1C2CEF` rather than `32597220`, and the combined arm CRC
`3CE2FB35`. The published vertex offset moves from 51 to 48.

The golden stream was edited by **deleting exactly the three depth dwords**,
their identity asserted rather than their position, not regenerated from the
builder's output - a golden refreshed from the code it checks stops being a
golden. The new CRC was computed by an independent implementation that was
first validated against the old pinned value `32597220`, which hardware has
confirmed twice.

**What is still owed:** the hardware boot. The expected result is
byte-identical pixel output at all fourteen probes, and nothing else in this
build has established that - the host tests prove the stream changed exactly
as intended, not that the GPU agrees.
