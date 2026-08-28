# A full-screen DOS box destroys the picture on the HP Mini 110

*Filed as "hangs the machine". It does not: see the status line.*

Date: 2026-08-28
Status: **open. The title of this issue is wrong and so was most of what
follows it: the machine does not hang.** Windows keeps running - the mouse
moves - and only the picture is destroyed. The display driver is never
entered on this path at all: a build tracing nine points across `Disable`,
`ResetHiResMode` and `ReEnable` wrote none of them. See "What the trace
proves", which supersedes the three eliminations above it.

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

**Neither depth nor geometry is the variable.** 1024x576 at 32bpp and at
16bpp, and 640x480 at 8bpp, all hang. That is every mode this machine offers
tried at both extremes of the surface, and it exhausts what can be varied from
the machine's side. `V9XBOOT.INI` is byte-identical across the two collections but for
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

### The control

Same file, same machine, stock Windows standard VGA driver
(`claude\personal\v9x-intel950\standard\`):

```
Trial03=mode=640x480x1 action=fullscreen  survived
Trial04=mode=640x480x1 action=fullscreen  survived
```

**Two full-screen DOS boxes, no fault.** So the machine is capable of the
transition and Velocity9x is what breaks it. Every "the machine hangs while
Velocity9x is loaded" hedge above this line is discharged.

Two things about those two lines, both worth having in writing.

`640x480x1` is the tool under-describing a planar mode, not a mono desktop.
The standard VGA driver is four one-bit planes and `GetDeviceCaps(BITSPIXEL)`
reports per plane, so the record should have read `640x480x4`. Fixed in
`dos_box_test_win32.c` by multiplying through `PLANES`, and the record now
also carries `driver=` read from `SYSTEM.INI`, so a trial says which driver
produced it rather than relying on the covering note. The trials above are
still sound - the depth in them is wrong, the outcome is not.

**640x480 under Velocity9x remains untested.** Trials 3 and 4 are the stock
driver's own resolution, not a Velocity9x run at 640x480, and it would be easy
to read them as having covered that. They have not.

It also says something about where to look. The stock VGA driver never leaves
a VGA mode, so the master VDD's save and restore across the transition has
almost nothing to do. Velocity9x is in a VESA linear-framebuffer mode at
`d0000000`, and that is the whole of the difference.

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

## Where to look next

The mini-VDD installs exactly four callbacks, and the build gate asserts that
it installs exactly these and no others:

```
VESA_SUPPORT   VESA_CALL_POST_PROCESSING
SET_MONITOR_POWER_STATE   GET_MONITOR_POWER_STATE_CAPS
```

**Neither end of the hi-res/VGA round trip is among them.** The master VDD
therefore performs its own default save and restore of video state across the
transition, for a card it has been told nothing about, sitting in a VESA
linear-framebuffer mode it did not set. Against the stock VGA driver that
default has nothing to do; against this one it has everything to do, and the
control above says the difference lands exactly there.

That is the leading hypothesis and it is **not** a new idea. A chip-agnostic
CRTC snapshot in the mini-VDD was drafted for the corrupt band, found to be
the wrong fix, and reverted - see the "Not what was first written here"
section of `docs\issues\2026-08-28-fullscreen-dos-scanout.md`. Being the wrong
explanation for the band does not make it the wrong explanation for the hang;
those are different symptoms, and the band's fix turned out to be a missing
call one layer up in the display driver, which cannot explain a wedge on the
way in. But the reasoning was wrong twice on this round trip already, so the
next move is an experiment, not a patch.

Because the machine cannot be traced - no serial port, no boot stage written
on this path - the instrument has to be a differential build, the way the mode
sweep is.

### The experiment that was proposed does not work, and why

The first plan was to hook the round trip and do the minimum, on the theory
that hooking it would keep the master VDD's default handling out of the way.
`MINIVDD.INC` and the DDK's own s3v mini-VDD say otherwise. **These callbacks
are additive, not overriding.** The reference implementations do their
hardware-specific work and `ret`; there is no carry-flag "I handled it"
convention on `PRE_HIRES_TO_VGA` the way there is on `VESA_SUPPORT`, and the
VDD proceeds regardless. A no-op hook would have changed nothing and returned
a clean-looking null result.

What the DDK asks for there is the opposite of minimal. `MiniVDD_PreHiResToVGA`
carries this instruction in the sample source: *"If your hardware does not
return to a standard VGA mode via a call to INT 10H, function 0, you should
also make sure to do whatever it takes to restore your hardware to a standard
VGA mode at this time."*

### The two builds

Both exist, neither has run on any machine.

1. **`-VgaReturn`** hooks `PRE_HIRES_TO_VGA` and sets INT 10h mode 3 through
   `V9xMini_Vbe_Call`, getting the adapter out of the linear-framebuffer mode
   before the master VDD takes its own route. If the hang goes, that route was
   the wedge. If it stays, a BIOS mode set from VxD context is the wedge
   whoever issues it, and the BIOS has to come out of that path entirely.
   `Exec_Int` runs the real video BIOS with no timeout, so this build can hang
   a machine that did not hang before.
2. **`-NoDpms`** excludes the S3 sequencer and CRTC writes below from the
   image - absent from the binary, not skipped at runtime; verified by the two
   instructions unique to that body occurring once each in the default
   mini-VDD and zero times here. The weaker experiment, and worth saying why:
   the routine's callers are the monitor-power and `4F10h` paths, which
   opening a DOS box is not obviously either of. It removes a variable rather
   than testing a mechanism.

Neither ships. The build audit asserts the image carries exactly what the
switch asked for in both directions, an experimental hook may be dispatched
only inside its own `IFDEF`, and the no-DPMS build carries its own serial
marker.

### Result: `-VgaReturn` hangs the same way

`claude\personal\v9x-intel950\1build\`. The driver came up on it -
`V9XMODES.INI` and `V9XSYNC.INI` both say `Build=4cb3de0-vgaret`,
`Stage=enable-ok`, desktop at 640x480x8 - and trial 6 did not return.

```
Trial05=mode=640x480x8 action=fullscreen  hung    (shipping build)
Trial06=mode=640x480x8 action=fullscreen  armed   (-VgaReturn)
```

So issuing the BIOS mode set ourselves, earlier, from the hook the DDK names
for the purpose, does not avoid the hang.

**What that does not settle.** The hook adds a BIOS call, it does not remove
the main VDD's. Whether it was our `INT 10h` that never returned or the VDD's
own subsequent route is not distinguishable here, and cannot be made so on
this machine: no serial port, and nothing on that path can write to disk. What
both readings share is the conclusion that matters - **going through the video
BIOS on this path wedges the machine**, and no build that keeps a BIOS call
there will fix it.

That closes the fix-it-properly route for tier-0. Returning an unknown chip to
text mode without asking its BIOS would need a register-level backend for that
chip, which is the entire thing tier-0 exists not to have.

*(These trials also show why the tool now records `driver=`: trials 5 and 6
are distinguishable only by the covering message and by `V9XMODES.INI` having
been collected between them. The build that records the driver name per trial
had not reached the machine yet.)*

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
3. ~~**Standard VGA driver, same machine, full screen.**~~ Done. No fault. The
   defect is ours.
4. ~~**16bpp desktop.**~~ Done. Hangs, as 32bpp does. Depth is not the
   variable.
5. ~~**640x480 under Velocity9x, and 8bpp.**~~ Done. 640x480x8 hangs. Geometry
   is not the variable either.

6. ~~**`-VgaReturn`.**~~ Done. Hangs the same way.
7. **`-NoDpms`.** Built, not run. Expected to hang; it removes a variable
   rather than testing a mechanism.
8. ~~**`-NoScreenSwitch`.**~~ Done. Hangs the same way.
9. **`-DosBoxTrace`.** Built, not run. The instrument rather than another
   elimination - see below.

## The answer this points at: refuse the switch

The Windows 98 DDK's own XGA mini-VDD refuses to let a DOS box go full screen
at all, and the comment in `MiniVDD_CheckScreenSwitchOK` says why:

> The XGA's HiRes screen cannot be reliably saved and restored if we're in a
> VESA mode. This is because of the fact that the XGA uses its own private DAC
> registers ... So.... we disallow switching away from any VESA HiRes mode DOS
> box.

Tier-0 is in that position by construction, and more so. It drives an unknown
chip through its VESA BIOS, it has no register-level backend to save and
restore state with, and on this machine the BIOS mode set that would return
the adapter to text hangs it - from every mode the panel offers, and again
with the driver issuing the mode set itself.

`CHECK_SCREEN_SWITCH_OK` returns carry set to prohibit the switch. The
`-NoScreenSwitch` build installs it and refuses unconditionally.

**Unconditionally, deliberately.** Refusing only when `EAX` says "a VESA mode"
would assume the main VDD reports a driver-set linear-framebuffer mode that
way, and assumptions about this round trip have been wrong twice already.
Establish that refusing prevents the hang first; narrowing the condition is
the next build's problem and is only worth having if this one works.

If it works, this is a candidate for shipping on tier-0 rather than a
stopgap - the same trade Microsoft made for the XGA, made for the same reason.
A windowed DOS box already works, and it is what a tier-0 user would get
instead of a machine that needs the power switch.

### Result: refusing the switch does not prevent it either

`claude\personal\v9x-intel950\2build\`, `Build=48ca2d3-noswitch`,
`Stage=enable-ok`, trials 7 and 8 both hung at 640x480x8. So either the main
VDD does not consult `CHECK_SCREEN_SWITCH_OK` on this path, or the fault is
reached before the question is asked. Which of those, is not distinguishable
from here - the same wall as everything else.

## Stop eliminating; get a trace

Three differential builds have now removed candidate causes and produced no
evidence about the fault. That is the signal that the approach is wrong, not
that the next elimination will land.

The reason there is no evidence is structural: everything suspected lives in
the mini-VDD, and on this machine a VxD cannot be traced at all.

**The display driver can, and has not been asked to.** It is ring 3 GDI code.
`Disable` already writes a file on this exact path through
`v9x_gdi_accel_flush_report`. The channel existed the whole time.

`-DosBoxTrace` writes a `DosBox=` step to `V9XBOOT.INI` at each point of the
round trip and flushes the profile cache after each one, because every step is
followed by something that may take the machine down before a cached write is
written back:

```
disable-enter, disable-pre-hardware, disable-exit
reset-enter, reset-hardware-ok, reset-exit
reenable-same-mode, reenable-hardware-ok, reenable-exit
```

Own key, not `Stage=`: a driver mid-DOS-box is not a driver that failed to
start, and the settings page and boot-trace tooling match on `Stage`.

**What the first run answers.** Whether the display driver is entered at all on
the way into a full-screen DOS box. No `DosBox=` after a hang means our 16-bit
code never runs, and every place examined so far has been the wrong one. A
`DosBox=` that stops at a named step names the last thing that completed.

Built as `c5fa4ed-trace`. It is expected to hang; that is not its failure
condition.

## What the trace proves

`claude\personal\v9x-intel950\3build\`, build `c5fa4ed-trace` confirmed
installed by `V9XMODES.INI` and `V9XSYNC.INI`.

**`V9XBOOT.INI` contains no `DosBox=` line.** Not one of the nine trace points
fired: not `disable-enter`, not `reset-enter`, not `reenable-same-mode`. The
16-bit display driver is never entered on the way into a full-screen DOS box,
and never on the way back.

And the machine is not hung. The photograph is annotated by the reporter: the
mouse cursor is on screen and it moves. Windows is running the whole time.

### The title of this issue is wrong, and so is the record above

Every trial recorded `hung` in `V9XDOSBX.INI` is really *"the display became
unusable and the machine was powered off"*. The tool cannot tell those apart -
it marks a trial `hung` when the run that started it never wrote an outcome,
and a user who cannot see the screen to type `EXIT` produces exactly that. The
outcomes still stand as measured (full screen breaks the display, windowed
does not, the stock driver is fine); the word for them was wrong.

That also disposes of the three experiments. `-VgaReturn` and
`-NoScreenSwitch` were aimed at a hang that does not exist, and their results
say nothing about whether their mechanisms matter.

### What the picture shows once you know it is not a crash

The cursor is a tall narrow bar rather than an arrow. The driver is still
drawing a correct desktop into memory; the CRTC is reading that memory with
the wrong horizontal geometry, squeezing each scanline into a fraction of its
width. Everything else in the frame - the stripes, the staircased bands - is
the same statement made about the rest of the desktop.

So: something reprogrammed the display timing, and nothing put it back.

### Which explains a fix that never had a chance

`V9xHardwareReset` is reached from `ResetHiResMode` and `ReEnable`. Neither is
called here. **The stride re-assert added in 0.6.1 for
`docs\issues\2026-08-28-fullscreen-dos-scanout.md` cannot fire on this path**,
and no fix placed in those functions can. Declining to call that a fix in the
0.6.1 changelog was right for a better reason than the one given at the time.

### What is not yet known

**Why the VDD never calls back.** Registration succeeded - it hands the VDD
`RESETHIRESMODE` as the callback, and a failure would have aborted `Enable`
with `Stage=fail-vdd-register` rather than the `Stage=enable-ok` on disk. So
the VDD holds our callback and does not use it on this transition. Whether it
believes it restored the state itself, from the snapshot
`VDD_SAVE_DRIVER_STATE` asks for at registration, is a guess and is written
here as one.

### The next test needs no build

The system is alive and the keyboard works, so the display can be poked while
it is broken. Blind-run `V9XMSW.EXE` from Start, Run while the picture is
destroyed.

If the desktop comes back, then re-asserting the mode is the whole of the
repair and the only thing missing is a trigger for it - which is a much
smaller problem than it has looked for the last four builds. If it does not,
the mode is not the whole of what was lost.
