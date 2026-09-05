# A power cycle does not clear the Trio3D's blend state

Date: 2026-09-05
Status: measured on A8U4I5 across four boots, with the driver A/B'd out. It
refutes the central hypothesis of
`2026-09-04-the-trilinear-two-pass-and-a-retraction.md`.

## What was being done

Updating the card to the driver at HEAD - the pair that publishes the render
target it programmed. The update itself went in cleanly: `DriverInitDone=1`,
`Ok=1`, no engine faults, and the new `D3dTarget*` keys reporting from the
card, so the ABI-bumped `V9XDISP.DRV` and `V9XHAL.DLL` matched.

The probe run after it read `TexMatrixOk=90` of 117, with `Alpha1555Ok`,
`Alpha4444Ok` and `SpriteOk` all zero. At boot 33, two days of work earlier,
the same rungs on the same card read 108 and 1.

## The A/B

Yesterday's retraction says a power cycle clears this state and a warm restart
does not. Both halves were put to the test, and so was the new driver, which
was the other thing that had changed.

```
                      b33            b35            b36            b37
driver           055d3c4        HEAD           HEAD           055d3c4
boot             warm           warm           POWER CYCLE    warm

TexMatrixOk      108            90             90             90
Alpha1555Ok        1             0              0              0
Alpha4444Ok        1             0              0              0
SpriteOk           1             0              0              0
MipLadderOk        1             1              1              1
MipTriDegradedOk   1             1              1              1
```

**The driver is not the cause.** Putting the pre-diagnostics pair back at boot
37 - built from `055d3c4` by export, the same source that produced boot 33's
good reading - gives 90, not 108. Whatever changed, it is not what was
installed.

**A power cycle did not clear it.** Boot 36 was a genuine power off and on, by
hand, and the state survived it unchanged.

## What that refutes, and what survives

Refuted: "Something the card carries across a warm restart makes its blend
wrong, and a power cycle clears it." A power cycle does not clear it. That
sentence should not be relied on, and the document it is in now says so.

What survives is the observation underneath it, which is weaker than the
sentence it was turned into: **this card has two states, and blending is
correct in one and wrong in the other.** The record now reads

- boots 22-27: wrong
- boot 28, after the machine dropped off the network and came back: **right**
- boots 29-33, warm restarts: right, and stable across five of them
- boot 34, after the machine dropped off the network and came back: wrong
- boots 35-38, including a deliberate power cycle: wrong

Both transitions coincide with the machine going away and returning, once into
each state. So the trigger is not the direction of a power cycle, and it is not
the driver. It is not known.

**The state is specific to blending.** `MipLadderOk` and `MipTriDegradedOk`
read 1 in every run above, so mip level selection and the trilinear degrade are
unaffected; so are all 90 unblended matrix cells. Only the 18 `alpha` cells and
the sprite rung move.

## Why this matters more than it looks

Every Trio3D measurement this project takes is now conditional on a state
nobody can name, set, or detect except by running the probe. The retraction of
2026-09-04 was caused by exactly that, and this document exists because the
correction to it was also wrong.

Until the trigger is known, **a Trio3D result is only meaningful beside its
`TexMatrixOk`**: 108 is the state where blending works and 90 is the state
where it does not, and a run in the wrong state can be reported as a chip
defect by anyone not looking. The probe already prints it in every file.

## Open

- What the trigger is. Both transitions happened while the machine was
  unreachable, so nothing here observed the transition itself.
- Whether it is the card, the BIOS's state at POST, or something Windows
  leaves. Nothing measured yet distinguishes them.
- The `Solo_*` chain rung has now run on the card, but only in the wrong state,
  where a blend drawing nothing on the chain cannot be told from the blend
  fault. That measurement is still owed.

## Gates

None; no code changed. The evidence is four probe files and the exported
`055d3c4` build used for the A/B.

## Later the same day

Boot 39 read 108 again, after the machine reset itself with nothing running.
Both states now have a register capture and the diff between them is in
`2026-09-05-a-register-capture-of-both-trio3d-blend-states.md`, along with
what that capture cannot see.
