# A DOS box hangs the HP Mini 110 before it draws a prompt

Date: 2026-08-28
Status: **open, and narrowed by measurement: a windowed DOS box is clean and
the full-screen transition is what hangs.** See "Measured" below. The cause
within that transition is still unknown.

Reported here on the HP Mini 110 (Intel 945GSE, GMA 950, PCI `8086:27AE`),
running the released 0.6.1 vbe package, build `e938fdc`. Typing `command` in
Start, Run hangs the machine at once. The panel shows Windows' logo screen -
the progress bar without its logo bitmap - and the bar does not advance. A
hard power-off is the only way out.

**0.6.0 does the same thing on this machine.** So this is not a 0.6.1
regression, and the untested stride re-assert added to `V9xHardwareReset` is
not implicated in it either way.

Photograph and the guest's diagnostics:
`claude\personal\v9x-intel950\0.6.1\`.

## It is not the defect already on file

`docs\issues\2026-08-28-fullscreen-dos-scanout.md` is about the *return* from
a full-screen DOS box: a corrupt band across the top of a desktop that came
back, on this same machine. This hang is on the way in, before any DOS prompt
is drawn, and nothing comes back at all. The two may share a cause. Nothing
yet says they do, and treating them as one defect is how the first diagnosis
of the band went wrong twice.

## The driver was healthy going in

From the boot that preceded the hang - `V9XBOOT.INI`, `V9XHW.INI`,
`V9XSYNC.INI`:

- `Stage=enable-ok`.
- 1024x576x32, `pitch=4096`, linear framebuffer at `d0000000`.
- Adapter unclaimed, `PciVendorId=8086`, `PciDeviceId=27AE`,
  `ModeSwitching=vbe-lfb`, `Acceleration=none`, `GdiAcceleration=none`.
- `V9XSYNC.INI` `Build=e938fdc`, `Status=ok`, six modes published.

Whatever this is, it is not a driver that failed to come up.

## The mode cache kills a conflation

`VbeCache=s=1826 l=36 q=36 c=6 p=0 f=0107`.

`f=0107` is `CTRL_VALID | LIST_VALID | LIST_TERM | EDID_VALID`. `QUERY_FAILED`
(`0x0080`), `QUERY_LIMIT` (`0x1000`) and `CACHE_FULL` (`0x0040`) are all clear,
and the caps are 96 queries and 64 cache entries, so nothing was truncated.

**This BIOS lists thirty-six modes and describes all thirty-six.** Six are
admitted by the host-tested admit rules; the other thirty are rejected on
content, by us, not refused by the BIOS.

That matters beyond this issue. The HP Mini 110 is *not* a second instance of
the NAV50 finding: the Pineview BIOS refuses to describe thirty of its
thirty-six (`docs\decisions\2026-08-28-pineview-vbe-mode-list.md`), and this
one refuses none. It follows that **the mode sweep has nothing to sweep on
this machine** - the set it walks is empty here, so the Mini 110 cannot test
that build, and a clean boot from it here would mean nothing.

`SWEEP_RAN` (`0x2000`) is clear, which confirms the package installed was the
plain one.

## Measured

`V9XDOSBX.EXE` build `ecc3e52`, run on the machine.
`claude\personal\v9x-intel950\0.6.1a\V9XDOSBX.INI`:

```
Trial00=mode=1024x576x32 action=windowed    survived
Trial01=mode=1024x576x32 action=fullscreen  hung
Trial02=mode=1024x576x16 action=fullscreen  armed
```

**A windowed DOS box opens, runs and closes with the desktop intact.** Both
full-screen trials failed to return. Trial 1 is the tool's own written `hung`,
resolved on the run that armed trial 2; trial 2 is still `armed` in the
collection sent, which means the same thing one run later. The tool writes and
flushes its record before it opens the box precisely so that this absence is
readable: a cancel writes `cancelled` and a failed launch writes
`launch-failed`.

**Depth is not the variable.** 32bpp at pitch 4096 and 16bpp at pitch 2048
both hang. `V9XBOOT.INI` is byte-identical across the two collections but for
the `Surface=` line, and `Stage=enable-ok` both times, so the driver came up
the same way and the same mode cache backed both.

### What the 16bpp hang looks like

Photograph in `claude\personal\v9x-intel950\0.6.1b\`. At 32bpp the panel went
to a near-blank screen carrying Windows' progress bar; at 16bpp it holds a
stable, structured, thoroughly corrupt image instead. Three things follow from
the picture without needing to explain it:

1. **The display controller is still scanning out.** The panel is lit and the
   image is stable, not decaying. Whatever wedged, it did not stop the CRTC.
2. **It is not a VGA text screen.** If `PRE_HIRES_TO_VGA` had completed, an
   80x25 text page is what would be on the panel. So the machine did not get
   through the transition; it stopped inside it, with the controller left in
   whatever state it had reached.
3. **The corruption has two distinct signatures at once.** Regular fine
   vertical striping across the whole frame, and horizontal colour bands whose
   edges step diagonally down the screen.

The second of those is a stride mismatch - content shifting by a constant
per-line offset staircases exactly like that. The first is not; a stride error
skews, it does not stripe. Fine vertical striping at a regular pitch is what a
bytes-per-pixel disagreement looks like, or what scanning out character and
attribute byte pairs as pixels looks like.

**Which of those it is, is not settled here, and a photograph cannot settle
it.** That is the standing lesson of
`docs\issues\2026-08-28-fullscreen-dos-scanout.md`, whose first two diagnoses
were both read off a picture and were both wrong. Recorded as a signature to
be explained, not as a diagnosis.

### What it kills

- **"Opening a DOS box hangs the machine."** It does not. Trial 0 is the
  counter-example, at the same depth and resolution that trial 1 died at.
- Anything reached merely by starting `COMMAND.COM` - VDD virtualisation of a
  windowed box, our driver's ordinary operation while one is open.

### What it implicates

The full-screen transition, which is the same round trip as the corrupt band
in `docs\issues\2026-08-28-fullscreen-dos-scanout.md`. The two issues were
deliberately kept apart until something joined them; this is that something,
though it still falls short of one cause for both.

### It also reconciles the original report

The hang was first seen from Start, Run, `command`, which reads like an entry
defect rather than a full-screen one. The tool launches the interpreter with
`CreateProcess` and `SW_SHOWNORMAL` and that survives. The likeliest reading
is that the Run launch went straight to full screen from a PIF or registry
default on this machine, in which case both observations are the same finding
and nothing about the way in is implicated at all.

### Unrelated, noted so it is not read as a change

`V9XDDH.INI` moved from `Stage=get32bitname` to
`Stage=setinfo-callback-missing` between the two collections, with
`LastGoodStage=get32bitname` unchanged. Different subsystem, no bearing on
this, recorded only so a later reader does not treat it as a symptom.
`V9XBOOT.INI`, `V9XHW.INI` and the mode rows are byte-identical across both
collections; only the sync generation counter moved.

## Unproven, and a defect regardless: the mini-VDD writes S3 registers on Intel silicon

`V9xMini_Set_Dpms` (`src\minivdd32\loader.asm`) is documented in its own header
as updating the S3 ViRGE DPMS state. It unlocks the S3 extended sequencer by
writing `06h` to `SR08`, read-modify-writes `SR0D`, and clears `CR56[2:1]`.

There is no family or chip guard on it. The only conditionals in that file are
`V9X_NO_VBE_COLLECT` and `V9X_VBE_MODE_SWEEP`, so the generic VBE mini-VDD -
the build whose INF offers itself for any VESA VBE 2.0+ adapter - runs those
writes on whatever silicon it lands on. Here that is a GMA 950, where `SR08`
and `SR0D` are not the registers the code believes it is writing.

Its callers are `MiniVDD_SetMonitorPowerState` and the `4F10h` paths in
`MiniVDD_VESASupport` / `MiniVDD_VESACallPostProcessing`. Opening a DOS box is
not obviously any of those, which is why this is recorded as a defect in its
own right and **not** as the diagnosis of the hang. It wants fixing whether or
not it explains anything here.

## What is not known

- **Whether the box was full screen or windowed.** It was launched from Run
  with no other setting touched. The photograph shows a full-screen logo
  screen, which is suggestive and not evidence.
- **Where it stops.** Nothing on this path writes a boot stage, so the files
  on disk describe the previous successful boot and not the failure. There is
  no on-disk record of how far it got.
- **Whether our code is even executing at that moment.** The master VDD's own
  VGA save/restore runs on this path. `V9xMini_Serial_Write` would settle it,
  but this is a netbook with no serial port, so the mini-VDD's trace output
  cannot be captured on this machine at all. That constraint shapes every test
  below: they are all differential, because nothing here can be instrumented.

## Tests to run

1. ~~**Windowed against full screen.**~~ Done. Windowed survives, full screen
   hangs. See "Measured".
2. **One more run of the tool**, to have it write `hung` for trial 1 itself
   rather than leaving the outcome to be inferred from an absence.
3. **Standard VGA driver, same machine, full screen.** The control every other
   result is read against: it says whether the full-screen transition is ours
   to fix or the machine's. Until this runs, "Velocity9x hangs on full screen"
   is not established - only that the machine does while Velocity9x is loaded.
4. ~~**16bpp desktop.**~~ Done. Hangs, as 32bpp does. Depth is not the
   variable.
5. **640x480, and 8bpp.** Geometry is still untested, and 640x480 is the one
   mode here that a BIOS is most likely to handle conventionally. A
   full-screen trial that survives at some geometry makes this mode-dependent
   and points at the surface; a hang at every one says the transition itself
   is at fault regardless of what it is transitioning from.

Test 3 is the one that matters most and is the cheapest. Until it runs, what
is established is that this machine hangs on a full-screen DOS box while
Velocity9x is loaded - not that Velocity9x is what hangs it.
