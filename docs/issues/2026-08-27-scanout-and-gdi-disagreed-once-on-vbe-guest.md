# The scanout and GDI disagreed once on the vbe guest, and the matrix could not see it

Date: 2026-08-27
Status: **open, not reproduced.** One clear observation, no repro. The harness
gap it exposed is real regardless and is the more useful half of this issue.

## What was seen

Immediately after the `vbe` live-switch mode matrix finished at 1024x768x16 -
which it reported as **6/6 PASS** - the two capture paths disagreed completely:

| Capture path | Reads | Result |
|---|---|---|
| QEMU monitor `screendump` | the emulated scanout | **2 distinct colours**, 2-pixel vertical stripes, whole screen |
| Agent screenshot | the framebuffer through GDI | **39 distinct colours**, mid-row teal `(0,127,127)` - a normal desktop |

Two consecutive monitor screendumps either side of the agent screenshot both
showed the stripes, so it was not a transient mid-repaint capture. GDI saw a
correct desktop while the CRTC was scanning out garbage.

The matrix's own per-mode screenshots (`desktop.bmp`) are clean teal desktops at
every mode including this one - but those are agent screenshots, so they are the
GDI view, and they would look clean under exactly this fault.

## What is known about it

- **Transient.** A subsequent live mode switch cleared it. Sequencing
  800x600x16 -> 1024x768x16 -> 800x600x16 afterwards gave clean scanout at all
  three, so the mode itself is fine.
- **Not reproduced.** A single DOS box (`v9xctl shell`, the last thing that ran
  before the striped state) does not do it: screen clean before, clean after.
- **Not the SDL staleness** documented in the reset-hang issue. That one is the
  host window lying while `screendump` is correct; this is the reverse -
  `screendump` is the one showing the fault.

Candidate mechanism, unverified: the driver hands GDI an LFB mapping while the
CRTC's scanline length or display start ends up inconsistent with it, so writes
land correctly in memory and are displayed wrongly. The `vbe` path programs
modes through VBE calls (`ModeSwitching=vbe-lfb`), and the matrix had just
performed six live switches in one boot without a reboot between them, which is
a sequence nothing had exercised before. That is a hypothesis and is not
supported by a repro.

## The harness gap, which does not depend on the above

**Every display check in `run-vm-mode-matrix.ps1` reads back through GDI.** The
GDI framebuffer test, the acceleration phase's comparison, the palette test and
the captured screenshot all go through the driver and GDI, so all of them see
GDI's own writes. A fault that corrupts only what the CRTC scans out is
invisible to the entire matrix, and would report 6/6 PASS - which is exactly what
happened here.

That is the same shape as defects this plan has already been caught by twice: a
check that reads back through the thing it is testing. The `/probe` instrument
built for the Trio64 defect has the same limitation, and its exactness on
emulated guests does not cover this, because it too reads through GDI.

**On this QEMU target there is a fix available and no excuse not to take it.**
The monitor `screendump` reads the emulated framebuffer with no driver, no GDI
and no host renderer in between. Wiring one screendump per mode into the matrix -
even just asserting "more than N distinct colours", which is enough to separate a
desktop from a stripe pattern - would have failed this run instead of passing it.
86Box has no equivalent, so this would be QEMU-only coverage, which is still
strictly more than none.

## What this does and does not say about the live-switch matrix run

The `-LiveSwitch` run genuinely established, on a guest that cannot be rebooted:
driver enable at every declared mode (`Stage=enable-ok`), correct reported
geometry, GDI-visible pixel correctness, and palette behaviour at 8 bpp. Those
results stand.

It did **not** establish that the displayed output was correct, because nothing
in it looks at the displayed output. Read the 6/6 with that boundary in mind
until the screendump check exists.

## Next

1. Add a per-mode monitor `screendump` assertion to the matrix for QEMU targets.
   Cheap, and it converts this class of fault from invisible to failing.
2. Only then re-run the `vbe` live-switch matrix, and see whether the stripes
   recur with something watching for them.
