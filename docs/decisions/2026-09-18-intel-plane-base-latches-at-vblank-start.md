# The Intel plane base latches at the start of the vertical blank

**Status:** model from a measured video; the fix it licenses is UNMEASURED
until the next boot. **Machine:** MICHAEL-NETBOOK, 945GSE (8086:27AE), LVDS
on pipe B, plane B, 640x480 with 576 active lines and a 672-line frame
(intel56/69). **Evidence:** the operator's phone video of Final Reality
under the intel7x flip builds; the intel74 to intel77 snapshots.

## What was measured

The video (60 fps, 663 frames) was measured frame by frame: mean grey of the
middle and lower thirds of the panel area. Once every seven video frames
(8.6 Hz, the game's frame rate) the middle third drops from about 194 to
about 100 to 120 and the lower third from about 225 to about 180, then both
climb back over the next two video frames and hold. The dark frame, viewed,
is the cleared buffer with the sky drawn and the walls faintly begun; a
frame elsewhere in the run shows the finished upper half above a flat grey
lower half with a hard horizontal edge. The panel is presenting the buffer
the game is drawing into, for about one display frame in every game frame.

The intel74 to intel77 snapshots for the same builds read `DrawsToFront=0`
against 642,574 to 688,384 draws: every batch was aimed at the buffer the
plane base register did NOT name at that moment.

## What that rules in and out

Both readings are true only if the base register does not say what the
panel shows for one frame after it is written. A register that reads back
the new value at once (intel73: "applies immediately", by readback, on both
the register and the MI_DISPLAY_FLIP paths) while the panel keeps the old
buffer for a frame is a double-buffered register: the write goes to the
pending copy, and the pending copy is latched into the live one at a fixed
point in the frame. The live one is what the panel fetches from.

Which point. Every build from intel66 wrote the base inside the blank (DSL
576 to 671; intel74 to intel77 in the first 24 lines of it) and released
the buffer at the frame tick, which intel69 measured at line 671. If the
latch were at the tick, those writes would have taken effect at the release
and the construction would not be visible. It is visible, so the latch is
before the write: at the start of the blank, line 576, the first line after
active video. A write at 576 to 600 then waits for the NEXT frame's line
576, and the buffer released at 671 is on screen for the whole frame in
between - which is what the video shows.

This is also how i915 v4.4 treats the plane registers on this generation:
`intel_pipe_update_start` (drivers/gpu/drm/i915/intel_sprite.c) waits for
the scanline to be within `VBLANK_EVASION_TIME_US` (100 us) BEFORE
`crtc_vblank_start` and writes the plane registers there, so they latch at
that vblank rather than the one after. Gen2/3 page flips use
MI_DISPLAY_FLIP in the ring, applied by the display at the same point.

## Hypotheses this evidence kills

- The base applies the moment it is written (intel73). The readback said
  so; the panel says otherwise. A double-buffered register reads back what
  was written.
- A plane fetching ahead of the beam, so that no CPU-chosen moment is safe
  (`docs\plans\intel-gen3-ring-flip.md`). Not needed: one latch point
  explains intel65, intel66, intel74 and the video together.
- A stale render cache at the blend destination (intel76), a stale map
  cache at the texels (intel77) and a skipped depth test (intel77). None
  could have produced a cleared buffer on screen; the flushes stay for
  the reasons given in the issue record.
- Tearing in intel65 and intel66. What the operator saw and called tearing
  was the same construction, with the release earlier still.

## intel78: the model held; the gate was in the wrong place

With the write in active video the still-drawing polls fell from
1,871,046 to 79,873 and every flip was taken at its completion, and the
operator saw the picture "a little better" - but the construction still
shows. The latch point was not the only gap: Flip returns when the base is
written, and only Lock and Blt are held on GetFlipStatus by the runtime.
Direct3D draws are not, so the first batches of every frame land in the
buffer the panel is still fetching until the latch, one display frame at
most, in every build regardless of where the write fell. The engine now
waits for a pending flip before drawing (`v9x_flip_wait_done`), and counts
how often it had to (`DrawsFlipWaited`). That count is the measurement of
this claim; it is unmeasured until the next boot.

## What the driver does from this record

The Intel flip write is issued while DSL is in active video and at least
`V9X_I9XX_FLIP_LATCH_GUARD_LINES` (8) lines short of the first blank line,
and completes at the frame tick, which follows the latch in the same blank
(`src\display32\engines\i9xx_scanout.c`). If the next boot still shows
the construction, the latch model is wrong as well and the front-buffer
readback instrument in the issue record is the next step.
