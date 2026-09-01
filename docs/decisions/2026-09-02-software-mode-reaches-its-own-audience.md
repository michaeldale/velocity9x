# The software mode was unreachable from the page on every card it was written for

Date: 2026-09-02
Branch: `main`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 2.

## The question that found it

"Does software Direct3D work in the VBE driver path?" The answer turned out to
be yes in the driver and no in the user interface, and the second half was the
one worth fixing.

## The driver path is generic, and the Trio64 already proved it

`src\chipsets\s3\s3_hw16.c:11` states it plainly: each device entry carries its
own `enable_aperture` and `fill_engine_descriptor`, "so the ViRGE opens its
new-MMIO window and describes an S3D engine **while the Trio64 does neither**".

So the Trio64 - the guest every mode 2 measurement has been taken on - is
already the descriptor-less path, the `else` arm of `dd16.c` that zeroes the
whole engine block. That is the same arm the VBE, ATI and Matrox families take.
The chain, end to end:

1. **Mode resolution.** `v9x_d3d_mode_resolve(REQUEST_SOFTWARE, V9X_FALSE)`
   returns `STATE_SOFTWARE` - a chip with no Direct3D still resolves to the
   rasterizer. Host-tested at `tests\host\test_d3dmode.c:64`, and gated on
   every run since.
2. **The capability stamp.** `dd16.c` sets
   `CAP_D3D | CAP_D3D_SOFTWARE | ENGINE_VALID` **outside** the
   `fill_engine_descriptor` branch, in both stamp sites, with a comment saying
   that is deliberately for the four families with no descriptor.
3. **Engine selection.** `v9x_d3d_engine()` tests `CAP_D3D_SOFTWARE` before it
   tests `engine_type`, so a chip whose type is NONE still resolves the
   rasterizer.
4. **The engine.** `d3d_soft.c` and `d3d_raster.c` touch no chip register. They
   need the framebuffer's linear base, the target offset, a pitch and an
   extent, all of which the chip-neutral DirectDraw path fills on every family.
5. **Measured**, on the Trio64, on 2026-09-01: depth-tested textured triangles
   through exactly those five steps.

Nothing needed adding to the driver.

## What did need adding

`tools\diag\settings_propsheet.c` offered two choices - Hardware and Disabled -
and greyed the control out entirely when `direct3d_capable` was false, which is
true of every card without an S3D unit.

Both halves were correct when written and both had gone stale:

- The list comment said Software joins it "when they render pixels". It renders
  them.
- The greying was justified because "on a chip with no 3D engine no value can
  produce Direct3D". That is no longer true, and the cards it applied to are
  precisely the ones software mode exists for. **The mode worked and only the
  page could not reach it.**

The control is now always live. The list carries all three implemented modes,
and `needs_engine` changes the *label* rather than hiding the entry: Hardware
still appears on a card without an engine, because it is the default and the
absent-key value, and a page that could not select it could not undo a change.
It reads "Hardware (this card has no 3D engine)" there.

## Measured

`Win98SE-Trio64`, port 9871, `V9XSETP.DLL` 46,080 bytes, `Build=72268a8-dirty`.
The Velocity9x page reports:

```
Adapter:    S3 Trio32/64 86C764        PCI ID: 5333:8811
Direct3D:   Software (CPU rasterizer - slow)   [combo box, enabled]
```

and the dropdown offers, in order:

```
Hardware (this card has no 3D engine)
Software (CPU rasterizer - slow)      <- selected
Disabled - advertise no Direct3D
```

Screenshots in the session record. Before this change the same control on the
same card was a greyed box reading "Requested mode is not in this build".

## What is still not measured

**No VBE guest ran.** `Win98SE-QEMU-StdVGA` was started and boots to a Windows
98 desktop that reports "Your display adapter is not configured properly", with
no shell icons and no remote agent answering on host port 9872 after several
minutes. The guest is in a broken display configuration left over from earlier
tier-0 work; reviving it is its own task and is not attempted here.

So the VBE claim rests on the five-step chain above - one host-tested step, one
generic-by-construction step, and an end-to-end measurement on a different
family that takes the identical arm - and not on a VBE guest. That is weaker
than a measurement and it is worth saying so plainly. The ATI family
(`Win98SE-Mach64VT2`, port 9873) is the cheaper of the two remaining ways to
turn it into one.
