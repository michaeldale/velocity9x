# GDI acceleration: what comes after the merge

Date: 2026-08-27
Status: **item 1 recommended and uncontroversial; item 2 is an open decision
that re-sequences the plan and is not mine to settle.** Items 3 onward are
recorded so they are not rediscovered.

Written immediately after `6f86e94` merged fill, copy and overlap to `main` with
those three primitives **on by default**. The point of this document is to say
what the measurements now argue for, including where they argue against
[`docs/plans/gdi-acceleration.md`](../plans/gdi-acceleration.md)'s own next
build.

## State at the merge

| | |
|---|---|
| Merged | `6f86e94`, 22 commits, `main`, not pushed |
| Shipping on | fill, copy, overlap (`GdiAccel` master default 1) |
| Shipping off | monochrome upload (`GdiAccelUpload=0`), ViRGE only |
| Emulated | ViRGE/DX 11/11 modes, Trio64 11/11, mach64-vt2 6/6 |
| Physical | BARRY (Trio32/64) verified at 640x480x16 and 800x600x16 |
| Build 005 | not started |

## 1. Close the ADVFUNC regression net (recommended, small)

**Do this before any new build.** It is the only open item that protects
something already shipped.

Fill, copy and overlap are default-on, and the single thing that makes them safe
on real hardware - the per-operation re-assertion of ADVFUNC_CNTL bit 0 - has no
automated test behind it. This was verified rather than assumed: 86Box
*does* implement reads of `4AE8H` (`s3_accel_in` is registered for it in
`vid_s3.c`), so bit 0 reads back set, the restore branch never executes in
emulation, and `AdvFuncRestores` stays 0 on every emulated guest. The harness
only **reports** `LastAdvFunc` and `AdvFuncRestores`; nothing asserts on them.

The consequence is concrete: delete `v9x_trio_ensure_enhanced()` and the
ADVFUNC block in `v9x_gdi_trio_prepare()`, and `run-checks` stays green and all
three emulated guests still pass. The regression would ship.

That is the same standard this branch already applied to itself when it deleted
a cursor check for passing 40 of 40 on a driver built to fail it. An assertion
never observed failing is not known to work.

**Proposed shape**, reusing machinery that exists: extend `V9X_GDIFAULTINJECT`
with an arm that clears `4AE8H` bit 0, then assert that a subsequent fill still
lands. On physical silicon that is a repeatable regression test for the actual
defect. In emulation it is still not vacuous, because the register is modelled
even though the gate is not - the restore branch executes, `AdvFuncRestores`
advances, and the check can fail if the guard is removed.

## 2. Open decision: build 005 as planned, or text acceleration

The rollout table lists `gdi-accel-005` as **extra ROPs** - DSTINVERT,
PATINVERT, DPx/DPa, behind per-ROP INI keys. The accelerated CrystalMark run
([record](2026-08-27-crystalmark-barry-accelerated.md)) argues that is the wrong
target next, and the argument is short:

| 2D test | Score | Accelerated by |
|---|---|---|
| Text | **3** | nothing |
| Image | 98 | builds 002/003 (screen-to-screen only) |
| Circle | 134 | nothing (`Output`) |
| Square | 275 | build 001 |

**Text is roughly ninety times lower than Square.** All the remaining headroom in
this driver's 2D behaviour is there. Extra ROPs would move none of those four
numbers: they are rare in desktop workloads, and CrystalMark's 2D group does not
exercise them at all. Build 005 as specified would be correct work with no
measurable result.

**The groundwork for text already exists and is verified.** Build 004 implemented
monochrome expansion (`MONOSRCBLT`) on the ViRGE and proved it - glyph blitting
*is* monochrome expansion. 004's
[design record](2026-08-27-gdi-accel-004-design.md) set text aside for one
reason only: glyphs arrive through `ExtTextOut` and `StrBlt`, different ordinals
that this driver still forwards to the DIB Engine unconditionally. The engine
path is built and tested; what is missing is ordinal plumbing.

**The catch, stated before anyone commits to it.** The Trio64 declines upload -
this repository has no first-party source for its 8514/A CPU-data registers - so
text acceleration would be **ViRGE-only** at first. BARRY, the only physical
machine here, is a Trio64. So the highest-value work is also the work that
cannot currently be validated on hardware, which is exactly the situation that
produced the ADVFUNC defect. That is an argument for care, not for avoidance,
but it should be weighed rather than discovered.

This decision changes what gets built and is left open deliberately.

## 3. Opportunistic while BARRY is powered on

**The native S3 driver column.** The two Velocity9x columns are measured; the
vendor-driver column is the one that would say how far from the stock driver the
result sits. It needs a driver swap and a swap back on physical hardware, so it
is not free, and the earlier `SYSTEM.INI` incident on this machine is the reason
to treat any guest-configuration change there as deliberate work rather than a
quick errand.

## 4. Blocked, and on what

- ~~**`vbe` enable gate**~~ - **closed 2026-08-27** on a different, freshly built
  QEMU guest that the earlier registry failure did not affect. `Stage=enable-ok`
  on the merged build, and `/accel` `Result=PASS` with the engine-less shape:
  `Advertised=23`, `Enabled=0`, all 3231 calls declining at the first gate. That
  run also confirmed the `stats.enabled != 0` liveness guard on a **second**
  independent engine-less family - `ZeroCounterChecked=1` with
  `DeclineUploadDelta=0` is exactly the case
  `upload-declines-never-exercised` would have false-failed without it.
  The `vbe` **mode matrix** has now run too, via a new `-LiveSwitch` mode added to
  `run-vm-mode-matrix.ps1` that applies each mode with `V9XMSW /set:` instead of a
  registry write plus a reboot - the guest's reset path wedges the emulated
  machine ([issue](../issues/2026-08-27-qemu-vbe-guest-hangs-in-seabios-on-reset.md)),
  and live switching avoids it entirely. 6/6 modes, all six applied in a single
  boot with no reboot. `AppliedBy` is recorded per mode and at the top of
  `matrix.json`, because a live-switch run does **not** exercise establishing a
  mode at boot and must not be read as full coverage.
  **Read that 6/6 with one boundary**: immediately afterwards the emulated scanout
  showed whole-screen stripes while GDI read a clean desktop, and the matrix
  passed because every check in it reads back through GDI
  ([issue](../issues/2026-08-27-scanout-and-gdi-disagreed-once-on-vbe-guest.md)).
  Not reproduced, but the harness gap is real and has a cheap fix on QEMU targets.
  The original broken guest's registry issue
  ([issue](../issues/2026-08-27-vbe-qemu-guest-registry-error.md)) is moot for
  gating now, though that VM is still unbootable.
- **Post-run display artifact** - a screenshot after the CrystalMark run shows
  the window duplicated at several scales. Deliberately not attributed to the
  driver: a `/probe` moments later was pixel-exact, which a mis-addressing driver
  cannot produce. Distinguishing screen damage from a capture artifact needs eyes
  on the physical CRT.
- **ViRGE/DX hardware validation** - never run on real silicon. Now that a bug
  class is known which emulation does not model, this path shipping default-on is
  a recorded risk rather than a tested one. Needs a physical ViRGE card.

## What this document does not claim

The two Velocity9x CrystalMark columns are **one run each**, and one control -
sequential disk write - moved 7.9%, about the size of Square's gain. The 2D
conclusions above rest on Circle and CPU coming back bit-identical, not on the
margins. A repeat of both columns would make item 2's argument quantitative
rather than directional; the ninety-fold gap between Text and Square is large
enough that it does not depend on that, but the smaller comparisons do.
