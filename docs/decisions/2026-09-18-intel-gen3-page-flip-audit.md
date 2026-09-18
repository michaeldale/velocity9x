# Intel Gen3 page flip audit: how i915 knows a flip is done, and what this driver got wrong

Date: 2026-09-18
Source: Linux `v4.4` `drivers/gpu/drm/i915/` - `intel_display.c`,
`i915_irq.c`, `i915_reg.h`, `intel_sprite.c` - fetched from
`github.com/torvalds/linux` at that tag on 2026-09-18 into the session
scratchpad. v4.4 is the last long-term kernel in which gen2/gen3 page flips
were an actively maintained path. GPL-2.0; findings below are stated in this
document's words, not transcribed.
Status: desk audit. It explains intel65 through intel71 and changes the flip
state machine; it measures nothing itself. The readback counters added after
intel71 are what confirm it.

## 1. The finding that matters: this driver read the wrong bits

`i915_reg.h`:

```
#define I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT   (1<<11)   line 1979
#define I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT   (1<<10)   line 1981
#define ISR                                            0x020ac   line 1736
```

and, a different register family entirely:

```
#define MI_WAIT_FOR_EVENT              MI_INSTR(0x03, 0)         line 263
#define   MI_WAIT_FOR_PLANE_B_FLIP     (1<<6)                    line 265
#define   MI_WAIT_FOR_PLANE_A_FLIP     (1<<2)                    line 266
```

The commit that built the ring flip (`3de8c26`) cited "(1 << 2) plane A,
(1 << 6) plane B" as the ISR bits. Those are the `MI_WAIT_FOR_EVENT` operand
bits. The ISR bits are 11 and 10. So `v9x_scanout_hw_flip_pending` tested
bits that are not the flip-pending status, read them clear, and the state
machine declared every ring flip complete the instant it was issued -
`FlipStillDrawing=0` across 795 flips in intel71 is exactly that. The
application was then told the back buffer was free while the display was
still scanning it, and drew the next frame into it. That is the tear, and
it is the same tear under both mechanisms because both were completed the
same wrong way.

## 2. How i915 completes a gen3 flip

There is no flip-done interrupt on gen2/gen3. `use_mmio_flip`
(`intel_display.c:11302`) says so in as many words and refuses MMIO flips
below gen5 because "older platforms derive flip done using some clever
tricks involving the flip_pending status bits and vblank irqs".

The trick is `i915_handle_vblank` (`i915_irq.c:3995-4020`), called from the
vblank interrupt:

- If IIR shows the plane's flip-pending bit, a flip was queued since the
  last vblank.
- If ISR still shows it, the flip is not yet taken: do nothing this vblank.
- If ISR no longer shows it, the flip completed: "We detect FlipDone by
  looking for the change in PendingFlip from '1' to '0' on the following
  vblank."

So the flip-pending bit in ISR is SET while a queued flip is outstanding and
CLEARS when the display has taken it, and i915 treats the buffer as
released only then. It also applies to the plain register write:
`intel_prepare_page_flip` notes that "an MMIO update of the plane base
pointer will also generate a page-flip completion irq" - a DSPADDR write is
a flip to the hardware, pending until the retrace like any other.

As a backstop, `__intel_pageflip_stall_check` (`intel_display.c:11330`)
waits three vblanks and then READS THE PLANE BASE REGISTER BACK - `DSPADDR`
below gen4 - and treats equality with the flipped-to offset as the flip
having happened. That is the readback the counters added after intel71
perform; i915 uses the same register as the same evidence.

## 3. What a gen3 flip looks like in the ring

`intel_gen3_queue_flip` (`intel_display.c:10952`), six dwords:

```
MI_WAIT_FOR_EVENT | MI_WAIT_FOR_PLANE_x_FLIP    "can't queue multiple flips"
MI_NOOP
MI_DISPLAY_FLIP_I915 | MI_DISPLAY_FLIP_PLANE(plane)
fb->pitches[0]
gtt_offset
MI_NOOP
```

This driver's four-dword stream is the last four. The leading wait exists
because i915 may queue a flip while one is outstanding; this driver refuses
a Flip while one is pending, so it needs no wait - PROVIDED it knows what
pending means, which until now it did not.

## 4. Vblank evasion, for the record

`intel_pipe_update_start` (`intel_sprite.c`) delays a batch of plane
register writes if the next vblank is within 100 us, so the set is not
split across two frames. It concerns multi-register updates (stride, base,
offsets together) and is not needed for a single base write or a ring flip.
Noted so nobody reintroduces the write-inside-the-blank experiment for the
wrong reason.

## 5. What this disputes in the earlier records

- intel66's "worse" and intel65's "lower half" were both read as evidence
  about WHEN the base takes effect. Neither was: in both boots the flip was
  declared done at the wrong moment, and the tear is the application
  overwriting a buffer still on screen. The frame-tick measurement
  (intel69) was right and remains right; it was answering a question that
  was not the cause.
- The "plane prefetch" model in the intel69 record is withdrawn as
  unnecessary. Nothing here needs it.

## 6. What changes

- The ISR flip-pending bits become 1 << 11 (plane A) and 1 << 10 (plane B).
- The pending-bit wait applies to BOTH Intel flip paths, because i915 says a
  register write is also a pending flip. `WAIT_HW` is the Intel flip state;
  the line-register states remain for the VGA path only.
- The readback counters stay: `FlipBaseDeferred` then `FlipTakenAtDone`
  is the i915 stall-check evidence, and `FlipRingPendingSeen` should now
  read as one per flip.

## 7. What this does not claim

That the 945GSE's bits are where v4.4's header says; that is the next boot's
to show, with `FlipRingPendingSeen` and `FlipStillDrawing` both non-zero.
Nor that the tearing has no second cause. If the picture is clean it had
one.
