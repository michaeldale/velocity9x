# A live mode switch leaves the desktop unrepainted on the physical Trio64

Status: **open.** Found 2026-08-20 on driver 0.4.3. Not diagnosed.

Target: BARRY, physical S3 Trio64 (86C764), `PCI\VEN_5333&DEV_8811`, 2 MiB VRAM,
Windows 98 SE, 32 MB RAM. Reached over the remote agent at `10.0.1.47:9869`.

## What happens

After a **live** mode switch - `ChangeDisplaySettingsA` with
`CDS_UPDATEREGISTRY`, which is what Display Properties does - the desktop never
finishes repainting. Large areas keep the *previous* mode's framebuffer
contents, which at a different stride reads as a doubled or tiled image.

A **reboot** into the same mode is completely clean.

Observed at 800x600x32 and, earlier the same day, on a live 8 -> 16 switch at
1024x768. Both of those depths predate the 24/32-bpp work.

## What it is not

Three explanations were checked and ruled out, which is most of what is known:

**Not a capture race.** That trap is real on this machine and produced a
withdrawn regression report the same day
(`2026-08-20-barry-tiling-was-a-screenshot-race.md`), so it was the first
suspect. It is not this: the state persisted through a forced `Ctrl+Esc` /
`Escape` and more than six capture round trips over tens of seconds, where the
race resolves within one or two.

**Not a stride or layout fault.** The regions that *do* repaint - the "My
Computer" icon and label, the Start button - are crisp, correctly scaled and
correctly coloured. New drawing lands in the right place. The driver's own
`Surface=` line agrees with the mode at every step:

```
Surface=pitch=3200 bpp=32 dwb=3200 dds=3200 w=800 h=600 debpp=32
```

pitch, `deWidthBytes` and `deDeltaScan` all 3200 for 800 pixels at 4 bytes.

**Not the no-clear flag alone.** The S3 families set modes with
`V9X_HW16_VBE_NO_CLEAR`, so stale VRAM after a mode set is expected - but
Windows should then repaint over it, and on a fresh boot into the same mode it
does.

So the fault is in *invalidation*: after the in-place PDEVICE rebuild, something
is not telling GDI the whole screen is dirty, or the repaint it does issue is
being dropped.

## Not reproducible under emulation

The same 0.4.3 binary live-switches cleanly on both 86Box S3 targets - ViRGE/DX
`:9869` and Trio64 `:9871`, both 4 MiB. Only the physical 2 MiB card shows it.
Whether the variable is the VRAM size, the real BIOS, or simply that BARRY is a
32 MB machine slow enough to expose an ordering bug the emulated ones hide, is
not established.

**The mode matrix cannot see this.** `run-vm-mode-matrix.ps1` reboots between
every mode, so every capture it takes is of a freshly painted desktop. That is
why 11/11 passes on both emulated chips and 0.4.3 shipped without noticing.

## Where to look

`ReEnable` in `src\display16\ddi.c` is the live-switch path. It rebuilds the
PDEVICE in place between `DIB_BeginAccess` / `DIB_EndAccess` with
`CURSOREXCLUDE`, re-registers the visible byte count with the master VDD and
refills GDIINFO - the design is recorded in
`docs\decisions\2026-08-10-dynamic-mode-switching.md`, which verified live
switching at 8 and 16 bpp on the **86Box ViRGE**, never on physical hardware.

Worth checking in roughly this order:

1. Whether anything invalidates the full desktop after the rebuild, and whether
   Windows' own post-`ChangeDisplaySettings` repaint is arriving at all. A
   deliberate full-screen invalidate at the end of `ReEnable` would be the
   cheap test, even if it turns out to be papering over the real cause.
2. Whether `V9xVddReregister`'s visible-byte count matters here: it is the one
   figure that differs between a 2 MiB and a 4 MiB card, and the master VDD uses
   it for save/restore sizing.
3. Whether the 15-second revert applet is involved - the switch reports success,
   so it should not be, but it has a hand in screen state around a mode change.

## How to reproduce

```
v9xctl exec -Application C:\V9XREMOTE\JOBS\<job>\V9XMSW.EXE `
    -Arguments "/set:800x600x32" -Host 10.0.1.47
```

then screenshot **several times over 30 seconds** and confirm the stale content
persists rather than resolving. Reboot and screenshot again for the clean
comparison. `V9XMSW.INI` reports `Result=PASS` and `ChangeResult=0` throughout:
the switch itself succeeds, only the repaint does not.
