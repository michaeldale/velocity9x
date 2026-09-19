# The edge write made it worse, and what the upstream record says

2026-09-19, A8U4I5 with the Trio3D/2X, Final Reality Robots, two recordings
of the panel at 1080p60 either side of one change.

## Measured: the first fix was wrong, and backwards

e93027f moved the VGA start-address write to the blank-to-active edge, on
the reasoning that it puts the write as far from the latch as a single
status bit allows. The operator reported the flicker was still bad, and the
recordings say it was worse. Twelve seconds of each, counting frames whose
mean luminance falls below three quarters of the local median:

| | dips / 12 s | dip depth vs baseline | consecutive-frame dips |
|---|---|---|---|
| before (`intel87`, 15-48-23.mkv) | 23 | 62% | none |
| after (`intel88`, 16-22-12.mkv) | **70** | **38%** | yes |

The reasoning was inverted. Putting the write just after the blank ends
means the swap happens at the NEXT retrace, nearly a whole frame later,
while Flip still returns at once and the application starts drawing
immediately. The exposure went from an average of half a frame to reliably
almost one. Reverted.

## What the standard discipline actually is

The VGA page-flip rule is: change the start address, then **wait until the
end of the vertical retrace before drawing to the now-hidden page**, because
until that retrace ends the panel is still fetching it. The margin between
the write and the latch is not the thing to maximise; the thing to respect
is that the buffer is not free until the retrace completes.

This path did neither. `v9x_flip_done` completed `WAIT_BLANK` at the START
of the blank, and nothing on the ViRGE draw path waited for a flip at all -
`v9x_flip_wait_done` was called from `d3d_i9xx.c` and nowhere else.

That combination also explains why `VirgeDrawsFlipPending` read zero on
silicon while the panel plainly showed a half-drawn buffer: "pending"
cleared at the first blank, which is almost always true by the time the
application submits its next batch, so the window that matters was never
observable. That zero was reported in
`2026-09-19-the-missing-virge-flip-guard-is-not-the-cause.md` as evidence
the guard was unnecessary. It was evidence about the definition, not the
hazard, and that document's conclusion should be read with this one.

Both halves are now in: the flip completes when the blank ENDS
(`WAIT_BLANK` hands over to `WAIT_UNBLANK_DONE`), and the ViRGE draw path
waits for a pending flip before its first batch, counting the wait and
drawing anyway on a timeout as the Intel path does.

## The upstream record on Gen3, which we had not consulted

**ECOSKPD 0x021D0 bit 0, `ECO_FLIP_DONE`.** On Gen3 - and only Gen3 - the
hardware declares what its flip-pending interrupt means. Set, and the bit
means the flip is DONE; clear, and it means queued, with completion arriving
at the vblank instead. i915 reads this at init and keys its whole flip
completion path off it. This driver has never read it, across intel71,
intel72 and intel86, all three of which concluded the flip-pending bit is
simply never set. It is read now and recorded raw as `Ecoskpd`; nothing acts
on it yet. It reads zero on an S3 guest for the trivial reason that the
Intel path does not run there.

**Our ISR bits were right.** i915 uses
`DISPLAY_PLANE_FLIP_PENDING(plane) = 1 << (11 - plane)` - plane A bit 11,
plane B bit 10 - in GEN2_ISR at 0x20AC, which is exactly what intel72 chose
and what intel86 ORed to zero across 605 flips. So the zeros are not a
wrong-bit error, and that narrows the question rather than reopening it.

**i915 treats the plane address readback as a COMPLETION test.** Its
page-flip stall check asks whether the display address register has reached
the expected offset after a flip was queued, and warns if it has not. That
only works if the register reads back the ACTIVE value once latched. The
latch model in `i9xx_scanout.c` says the opposite - that the readback
returns the pending value - and that was inferred from a video, never
measured. One of the two is wrong. It matters, because
`FlipBaseImmediate=605/605` means very different things under each reading:
under i915's, every flip completed instantly, which a vblank-latched flip
should not.

**Gen3 page flipping is a known-bad area upstream**, with dedicated patches
for stall detection, for a different flip command, and for the rule that
queuing a flip while the pending bit is set can hang the part. This driver
is not fighting something unique to it.

## Status

The revised S3 fix is gated and runs clean on the 86Box ViRGE guest: 383
flips against 386 before it, no declines, no forced idles, no wait
timeouts, so completing at unblank does not hang a flip. That is a
regression check and nothing more - 86Box's retrace resolves at once, so
the guest cannot exercise the window the fix is about, and
`VirgeDrawsFlipPending` and `DrawsFlipWaited` are both still zero there for
that reason.

**UNVERIFIED on silicon.** The Trio3D machine is switched off. The fix
needs one Robots pass with the panel recorded, read the same way as the two
runs above: if the dips per twelve seconds fall well below 23 the fix
holds, and if they sit at 23 it does not.
