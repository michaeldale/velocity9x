# Handover: GDI acceleration does not work on physical S3 Trio64

Date: 2026-08-27
Status: **RESOLVED later the same day - root cause found and fixed.** See the
"Root cause" section added to the issue: ADVFUNC_CNTL (4AE8H) bit 0 is cleared
by DOS-box/VDD activity on real silicon, after which the engine executes
commands and discards every memory write; a mode set rewrites the register,
which is what made the 32-bit HAL (whose probe mode-sets first, and at
640x480, not the failing mode) look immune. Neither candidate in section 7
was the cause, and the elimination in section 3 ("same silicon, same boot,
same mode") compared different modes. The per-operation guard now lives in
`v9x_gdi_trio_prepare()` and `v9x_trio_ensure_enhanced()`. This handover is
kept for the record of what was and was not established at the time.
Issue: [`2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md`](../issues/2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md)

This is a review handover for one unresolved defect. It exists because the
session that found it eliminated six candidate causes without reaching the
cause, and the value left on the table is entirely in *what has already been
ruled out* and *which instrument to trust*. Read the elimination table before
forming a theory; six of the obvious ones are already spent.

---

## 1. The defect in one paragraph

Builds `gdi-accel-001` through `003` gave the 16-bit display driver's `BitBlt`
entry point a fill, copy and overlap path on the S3 2D engine. All three pass
the randomized comparison harness on every accelerated mode of three emulated
guests - 11/11 on the ViRGE, 11/11 on the emulated Trio64, twice each. On
**physical** S3 Trio64 silicon the fill path draws essentially nothing into the
displayed framebuffer, and the desktop is left visibly corrupted. The engine
accepts the command and executes; the pixels do not arrive.

## 2. Current state of the tree

| | |
|---|---|
| Branch | `gdi-accel-000` |
| Default | **off** - `V9X_GDI_DEFAULT_MASTER` is `0` in `src/display16/gdi_accel.c` |
| Per-primitive defaults | unchanged; each still records what its build earned in emulation. The master switch is what makes them unreachable |
| Build 005 | not started, deliberately. Adding ROPs to a fill path that mis-addresses on hardware widens the blast radius |
| Emulated guests | green. ViRGE 11/11, Trio64 11/11, mach64-vt2 6/6 |
| BARRY | `GdiAccel=0`, boot 93, clean desktop, verified by screenshot |

Relevant commits, newest first:

```
8e98525  Program the Trio64 engine's latched state, per the databook that names it
c6dc406  Measure where the Trio64 fill goes: a validated probe, and three dead hypotheses
12a556b  Turn GDI acceleration off by default: it corrupts real Trio64 silicon
19ae09a  Record that GDI acceleration corrupts the display on real Trio64 silicon
be7b2f8  Land build 004's monochrome upload, and the struct confusion that hid it
```

Nothing here has shipped. Released is 0.5.x, which predates `gdi-accel-000`.

## 3. What is established, with the evidence

**The fill path alone reproduces it.** `GdiAccelFill=1` with copy and overlap off:
`Compared=FAIL` at the same operation 25, `MismatchBytes=208902` against 213372
for all three primitives together. Copy and overlap are not involved. This is
build 001's 8514/A solid-rectangle path.

**Eight fills are enough.** A run with the threshold raised to reject nearly
everything still executed 8 fills in the smoke phase, recorded `FillsDelta=0`
for the measured window, and still failed the comparison - the damage from those
8 persisted. This is not a rare race; it is close to every fill.

**The engine executes.** The Trio64 status word reads `0x0400` on entry and
`0x0600` immediately after the command is written - the busy bit sets - and is
back to `0x0400` by the next fill. So the port writes reach the hardware, the
engine accepts the command, and it completes. `IdleTimeouts=0` and `Poisoned=0`
throughout, which also clears the spin limits as a suspect.

**Almost nothing reaches the displayed framebuffer.** The single-fill probe
(section 5) requests a 96x32 rectangle - 3072 pixels - and measures 0, 14, 17, 22
or 29 changed pixels across five runs, scattered thinly across a wide band 4-8
pixels tall at a different y each time. That is readback noise, not a displaced
rectangle.

**The same sequence is correct from 32-bit on the same silicon in the same boot.**
`src/display32/engines/eng_s3_trio.c` drives the same registers in the same
order with the same constants. `V9XDDP` run immediately before the GDI probe
reports `BltFillPixelOk=1`, `SrcCopyPixelOk=1`, and four overlap probes at 64
pixels each - and the GDI probe then fails unchanged.

**The harness is sound on this machine.** With `GdiAccel=0` on BARRY at the same
mode: `Enabled=0`, `Fills=0`, `Compared=PASS`, `Result=PASS`. This control was
run *after* two failing arms, which was the wrong order and is worth not
repeating.

## 4. What has been eliminated

All six were hypotheses formed and then refuted by measurement in one session.
Re-testing any of them is wasted effort unless something below is wrong.

| # | Hypothesis | Refuted by |
|---|---|---|
| 1 | Pixel-depth mismatch (16-bit colour written as 8-bit) | 800x600x**8** fails identically: `MismatchBytes=195553`, same operation, same signature |
| 2 | Surface base offset folded into y | Measured `LastBase=0x00000000`, `LastPitch=1600` on hardware - identical to every emulated guest |
| 3 | Missing one-time engine initialisation | `V9XDDP` immediately before `/probe` in the same boot: `BltFillPixelOk=1`, probe still fails |
| 4 | Wrong engine width or pixel length | `CR50=0x90` on BARRY, `0x92` emulated. Both decode to `GE-SCR-W`=800 and `PXL-LNGH`=2 bytes; they differ only in bit 1, which the databook lists as Reserved |
| 5 | Stale scissors, on their own | Programmed wide open: no change. And see the EXT CLIP trap in section 6 |
| 6 | Unestablished latched engine state | The full databook 13.4.2 set programmed: perfect probe in emulation (3072/3072), no change on BARRY |

The command word is also confirmed, so it is not a seventh candidate: the
databook's own worked example ends `ES:[CMD] <= 0100000010110001b`, which is
`0x40b1` - `V9X_TRIO_CMD_RECT_SOLID`, unchanged.

## 5. The instrument to trust: `V9XGDI.EXE /probe`

Added in `c6dc406`, in `tools/diag/gdi_smoke_win32.c`. It snapshots the whole
screen, issues **one** `PatBlt`, snapshots again, and reports the bounding box of
pixels that actually changed. It assumes nothing about the background, and both
snapshots go through the same 24-bpp readback so that conversion cancels out.

Why it matters: every other check in the harness compares against a reference
render and reports *whether* the pixels are wrong. That is the wrong instrument
for a driver that mis-addresses, because it cannot say where the writes went.

**It is validated, and validation is the point.** On the emulated ViRGE and the
emulated Trio64, requesting (64,48) 96x32 returns:

```
ProbeActualX0=64  ProbeActualY0=48  ProbeActualW=96  ProbeActualH=32
ProbeChangedPixels=3072  ProbeExpectedPixels=3072
```

Exact, with not one stray pixel in the sampled region. A probe that could not
confirm a known-good case would be worthless on a failing one.

Output keys land in `C:\V9XACCE.INI` on the guest. `ProbeActual=nothing-changed`
is written when the count is zero, so a silent zero is not mistaken for a pass.

Also useful, all reported by `/accel` and `/probe`: `LastBase`, `LastPitch`,
`LastCR50`, `LastCR6A`, `LastCR51`, `LastCR31`, `LastStatusEntry`,
`LastStatusIssued`, `LastColor`, and the build 004 upload counters.

One diagnostic was **retracted** rather than kept: a `last_status_settled` field
claimed the engine stayed busy after a settle, but its delay loop was an empty
`while` the compiler was free to delete, so it sampled microseconds after the
command where busy is normal. It proved nothing. Do not reintroduce it without a
real delay.

## 6. The databook, and one trap in it

`bitsavers.org/components/s3/DB014-B_Trio32_Trio64_Graphics_Accelerators_Mar1995.pdf`
- 278 pages, the Trio32/Trio64 part, which is BARRY's 86C764. Fetch from plain
`bitsavers.org` over **https**; `www.bitsavers.org` returns 403. `DB018-A`
covers the Trio64V+, a different part, and was not needed.

Sections that earned their reading:

- **13.4.2 "Initial Setup"** - the list of global state every drawing operation
  depends on: clipping registers (BEE8H indices 1-4) plus the internal/external
  clipping choice (index 0EH bit 5), colour compare (index 0EH bit 8, colour in
  B2E8H), and the Write Mask (AAE8H). All write-only; the scissors' power-on
  default is **Undefined**.
- **13.3.3.3 "Rectangle Fill Solid"** - the worked register sequence.
- **CR50, "Extended System Cont 1"** - bits 7-6 plus bit 0 are `GE-SCR-W`, the
  Graphics Engine Command Screen Pixel Width (bit 0 is the field's MSB);
  bits 5-4 are `PXL-LNGH`, the pixel length for Enhanced mode command execution.
  The engine keeps its own width and pixel length, independent of display pitch.

**The trap.** Index 0EH bit 5 is `EXT CLIP`: with it set, *"only pixels outside
the clipping rectangle are drawn"*. So opening the clip rectangle wide **on its
own** excludes the entire screen - a partial fix here is worse than no fix. An
intermediate commit in this session did exactly that. `v9x_gdi_trio_prepare()`
now sets the whole set together, which is the only safe way to touch any of it.

`v9x_gdi_trio_prepare()` is written per operation rather than once at Enable,
because the 32-bit HAL drives the same engine through the same registers with no
lock shared with the 16-bit side, so state established once could be changed
underneath by a DirectDraw blit between two GDI calls.

## 7. Where to start next

The fact the session kept failing to exploit, and the right starting point:

> The identical register sequence is **correct from the 32-bit HAL** on this
> silicon in this boot, and **wrong from the 16-bit driver**, while the engine
> demonstrably responds to the 16-bit writes.

So the difference is the surrounding context, not the sequence. Two candidates,
neither tested:

1. **I/O virtualisation.** Windows 9x traps ring-3 port access through VMM, and a
   VxD may virtualise the S3 enhanced-register range per VM. The 16-bit driver's
   writes and the 32-bit HAL's writes do not necessarily arrive by the same
   route. Finding a way to *test* this is worth more than another register
   theory - the engine going busy proves the CMD write arrives, but says nothing
   about whether the coordinate and extent writes did.
2. **What the DIB Engine has done to the chip.** The 16-bit path runs mid-GDI
   operation, after `deBeginAccess` has prepared the surface for CPU access. The
   32-bit HAL runs under DirectDraw's exclusive lock instead. The build 000 drain
   hooks guard CPU-after-engine; nothing guards the other directions.

One cheap in-repo lead is still unchecked: `V9XDD.INI` on BARRY reports
`GblDisplayPitch=0x640` (1600, correct for the mode) alongside
`PrimaryPitch=1280`, which is 640x480x16's pitch. That file may simply be stale
from an earlier run, but a HAL and an engine disagreeing about pitch would
displace every fill and would leave the 32-bit path unaffected. Ten minutes.

And one foundational assumption deserves a check, because everything rests on it:
the probe reads back through GDI, so it measures what GDI sees. The photographs
prove fills reach the framebuffer *sometimes*. A readback that disagreed with the
CRTC would invalidate "nothing changed" - though the probe returning an exact
3072-pixel match on two emulated chips argues it is sound.

## 8. How to reproduce

BARRY: `10.0.1.47:9869`, S3 Trio32/64 86C764, 2 MB, Windows 98 SE, agent 0.6.0.
Mode **800x600x16** throughout - the pinned comparison mode, and above 16 bpp
every operation declines by design.

```powershell
# deploy (note: update-associated-driver.ps1 takes -GuestHost)
scripts\update-associated-driver.ps1 -PackagePath build\win98se-s3 `
  -ControllerPath $ctl -GuestHost 10.0.1.47 -Port 9869 `
  -JobId probe-run -BootTimeoutSeconds 420 -Json
```

Then set `[Velocity9x]` in `C:\WINDOWS\SYSTEM.INI` (see the guard in section 9),
reboot, and:

```powershell
# note: v9xctl.ps1 takes -EndpointHost, NOT -GuestHost
$job = 'C:\V9XREMOTE\JOBS\probe-run'
v9xctl.ps1 exec  -EndpointHost 10.0.1.47 -Port 9869 -Application "$job\V9XMSW.EXE" -Arguments '/set:800x600x16' -WorkingDirectory $job
v9xctl.ps1 shell -EndpointHost 10.0.1.47 -Port 9869 -ShellCommand "START $job\V9XGDI.EXE /probe"
# wait ~60s, then
v9xctl.ps1 shell -EndpointHost 10.0.1.47 -Port 9869 -ShellCommand 'TYPE C:\V9XACCE.INI'
```

Arms worth having ready:

| Arm | `[Velocity9x]` keys |
|---|---|
| Safe / control | `GdiAccel=0` |
| Fill only | `GdiAccel=1 GdiAccelFill=1 GdiAccelCopy=0 GdiAccelOverlap=0 GdiAccelUpload=0` |
| Measure without drawing | as above plus `GdiAccelThreshold=<large>` - the geometry gate records `LastBase`/`LastPitch`/`LastCR50` *before* the threshold gate declines, so this reads chip state with no engine writes |

Guest ports elsewhere, from the family manifests: `9869` s3/virge-dx (`Win86SE`),
`9871` s3/trio64 (`Win98SE-Trio64`), `9873` ati/mach64-vt2, `9872` vbe/std-vga
(QEMU, **broken** - see the separate vbe issue).

## 9. Cautions, all learned the hard way

**Do not write `SYSTEM.INI` from the result of a read you have not checked.** This
session destroyed BARRY's `SYSTEM.INI` by calling `v9xctl get` with
`-RemotePath` (it wants `-Source`), ignoring the failure, and writing the empty
result. The machine had not rebooted, so the running system was unaffected, and
it was recovered from a capture at
`C:\everything\claude\personal\barry-win98\audit\SYSTEM.INI`. That capture is the
only reason it was cheap: the on-machine backups in `C:\WINDOWS\SYSBCKUP` are
from **2002**, and `SYSTEM.PSS`/`SYSTEM.TSH` are the same vintage.

The guard that should have existed - build from the known-good capture, never
from a live read, and refuse to upload anything that does not look like a
`SYSTEM.INI`:

```powershell
$text = [IO.File]::ReadAllText($dst)
if ($text.Length -lt 1000) { throw "refusing to upload: only $($text.Length) bytes" }
foreach ($need in @('[boot]','shell=Explorer.exe','display.drv=pnpdrvr.drv','[386Enh]')) {
    if ($text -notlike "*$need*") { throw "refusing to upload: missing '$need'" }
}
```

Worth promoting into `scripts/` if anyone touches guest INI files again.

**Recovery depends on the agent staying reachable.** Every recovery in this
session was done over the network. A blit that *hangs* the chip rather than
mis-addressing it would need someone physically at the machine. Confirm physical
access before enabling acceleration on BARRY.

**`GdiAccel=0` plus a reboot is the recovery**, and it is verified: boot 76 and
boot 93 both came back to `GdiAcceleration=none` with a clean 800x600x16 desktop.

**Do not read `FlipPixelOk=0` as a regression.** It reads 0 in every recent
pre-004 capture including one taken with the **native S3 driver**. It belongs to
the probe's flip readback, not to this driver.

**Mode-directory counts are a progress signal, not completion.** Matrix runs
create a mode's directory before running its tests. Wait for `matrix.json`.

## 10. Also outstanding, not part of this defect

- **CrystalMark comparison.** The 0.5.x baseline on BARRY is recorded
  ([baseline](../decisions/2026-08-27-crystalmark-barry-baseline.md)) with a
  stated prediction. The accelerated column **cannot be measured** until this
  defect is fixed, because acceleration is not safe to enable. The native-S3
  column is still available to take.
- **`vbe` enable gate**, blocked on a broken QEMU guest -
  [issue](../issues/2026-08-27-vbe-qemu-guest-registry-error.md). Not a driver
  problem: `vbe` is engine-less and `ati/mach64-vt2` covers that path.
- **The ViRGE/DX is untested on real silicon.** Only the Trio64 exists
  physically here, so nothing in this handover says whether the ViRGE path is
  affected.
