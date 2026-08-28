# A DOS box hangs the HP Mini 110 before it draws a prompt

Date: 2026-08-28
Status: **open - reproduced, cause unknown. Nothing is measured beyond the
hang itself; no hypothesis below has been tested.**

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

1. **Windowed against full screen.** If a windowed box is clean and only full
   screen hangs, the mode-switch round trip is implicated. If windowed hangs
   too, it is not, and a whole branch dies.
2. **Standard VGA driver, same machine, same action.** Confirms the defect is
   ours rather than the machine's. Cheap, and it is the control every other
   result is read against.
3. **8bpp and 16bpp desktops.** The hang was seen at 32bpp with a 4096 pitch.
4. **640x480 rather than 1024x576.** Separates the panel's own mode from the
   round trip.

A pass in any of 3 or 4 narrows this to a mode-dependent path. A hang in all
of them says the depth and geometry are irrelevant and the round trip itself
is at fault.
