# Intel Gen3: flip through the ring, not the register

Date: 2026-09-18
Status: built, the DEFAULT whenever the ring is up, UNMEASURED. It was
gated behind `IntelFlipRing=1` for one build; intel70 booted that build
without the key and so measured nothing, and the operator's standing
instruction from the same day is that a dev driver does not gate new
mechanisms behind commands. The register write remains only as the
fallback for a boot with no ring.
Record it amends: `docs\issues\2026-09-16-final-reality-renders-black-and-the-hal-faults.md`.

## Why

Three boots say a bare write to the plane base register does not give this
driver a clean flip on the 945GSE:

- intel65: written anywhere, waited on afterwards - tearing in the lower half.
- intel66: written inside the blank - tearing worse.
- intel69: the frame counter ticks with the line register at 671, so the
  blank is 576 to 671 and the line test the path used was right. The write is
  what does not fit.

A register applied at the retrace would not tear in intel65; one applied at
once would not tear in intel66. The one model consistent with both is a plane
fetching ahead of the beam, so that no moment the CPU can pick is safe. That
model is not established, and the plan does not depend on it: it uses the
mechanism the hardware provides for exactly this, which does not require the
driver to know when the plane fetches.

## What i915 does on this generation

`intel_gen3_queue_flip` puts the flip in the command ring. The display engine
applies the new base at the retrace and holds a flip-pending bit in the
interrupt status register until it has. From `i915_reg.h`:

```
MI_DISPLAY_FLIP_I915   = MI_INSTR(0x14, 1)      cmd, pitch, base
MI_DISPLAY_FLIP_PLANE(n) = n << 20
ISR                    = 0x020ac
I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT = 1 << 2
I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT = 1 << 6
```

i915 precedes the flip with `MI_WAIT_FOR_EVENT` on the same bit so a second
flip cannot overtake the first. This driver does not need the wait: its flip
state machine already refuses a new Flip while one is pending, and it now
learns "pending" from the ISR bit rather than from watching the line
register.

## What is built

- `src\chipsets\intel\i9xx_flip.c`: the four-dword stream (flip, pitch, base,
  NOOP pad) built and decoded as pure C with a host test. Plane 0 or 1, a
  64-byte-multiple pitch read from the plane's own stride register, a
  dword-aligned base inside the framebuffer.
- `src\display32\engines\i9xx_scanout.c`: with the ring flip armed,
  `v9x_set_display_start` builds, decodes and submits the stream through the
  same ring path the draws use, and `v9x_scanout_hw_flip_pending` reads the
  ISR bit for the resolved plane.
- `src\display32\ddhal_core.c`: a fourth flip state, `WAIT_HW`, done when the
  pending bit clears. The pending-poll bound and the untracked latch apply to
  it as to the others.
- `V9X_DD_ENGINE_CAP_FLIP_RING` is stamped whenever the ring is up. A boot
  with runtime 3D off has no ring and falls back to the register write.
- Counters: `FlipRingIssued`, `FlipRingRefused`.

## What the boot measures

Final Reality on a fresh install, then a snapshot:

- `FlipRingIssued` near `FlipHandled` and `FlipRingRefused` at zero says the
  stream was accepted by the parser every time.
- `FlipStillDrawing` in the thousands per flip says the pending bit was seen
  set and then clear - the display took the flip at a retrace.
- The picture is the result: no tearing anywhere is the claim being tested.
  Tearing still present with the bit behaving means the model above is wrong
  in a way this mechanism does not fix, and the record says so.
- A hang is the parser refusing MI_DISPLAY_FLIP on this part. The recovery is
  `V9X3D ON` from DOS, which turns the Intel flip off altogether (DirectDraw
  then copies), or `V9X3D OFF`.

## Amended 2026-09-18, after the audit

`docs\decisions\2026-09-18-intel-gen3-page-flip-audit.md` found the ISR
bits above wrong: the flip-pending bits are 11 (plane A) and 10 (plane B);
2 and 6 are `MI_WAIT_FOR_EVENT` operand bits. intel71 completed every flip
at once for that reason. Corrected, and the pending-bit completion now
applies to the register write as well, which i915 says is a pending flip
too.

## What this does not claim

That the plane prefetches, that MI_DISPLAY_FLIP behaves on the 945GSE as it
does in i915's tree, or that the ISR bit clears at the retrace rather than at
the write. All three are the boot's to answer.
