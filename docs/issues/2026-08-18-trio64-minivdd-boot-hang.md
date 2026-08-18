# V9XMINI.VXD hangs the boot on a physical S3 Trio64

Status: **open.** Isolated to `V9XMINI.VXD` on 2026-08-18. The DRV is not
implicated: with the stock `S3.VXD` in the `minivdd` slot the same
`V9XDISP.DRV` reaches `enable-ok` and drives the card.

Target: BARRY, physical S3 Trio64 (86C764), `PCI\VEN_5333&DEV_8811`, 2 MiB VRAM,
Windows 98 SE 4.10.2222, 32 MB RAM, no MMX. Driver 0.4.1, build `2094b52`.

This is the first time any Velocity9x family has run on physical hardware rather
than under 86Box, and the first thing it found was a boot hang.

## What happens

Installing the `s3` package through Have Disk succeeds cleanly - SetupX offers
only the Trio32/64 model for `DEV_8811`, binds `Display\0001` and leaves the
stale Cirrus `Display\0000` alone. The **first boot afterwards hangs** with:

```
Windows protection error.  You need to restart your computer.
```

A Windows protection error is a VxD failing initialisation during the
protected-mode boot phase, so the newly bound `minivdd` is the immediate
suspect. That is what the isolation below confirms.

## Automatic Skip Driver makes every later boot lie

**Read this before re-testing anything.** Windows 98's Automatic Skip Driver
records the hang and permanently blacklists the device. Device Manager then
reports, on the General tab:

> Windows stopped responding while attempting to start this device, and
> therefore will never attempt to start this device again. (Code 11.)

From that point Windows never calls the driver's Enable at all. The desktop
comes up through the INF's own `MODES\4\640,480` -> `vga.drv` entry at 640x480
in 16 colours, and `C:\V9XBOOT.INI` stops at:

```
Stage=query-ok
```

That is **not** the driver failing at the query. `query-ok` is written during the
GDIINFO query, which happens regardless; the enable pass is simply never
attempted, so no `enable-start`, no `fail-*`, and no `C:\V9XHW.INI`.

The trap is that this looks exactly like a reproducible driver fault. Two clean
reboots were spent "confirming" it before the Code 11 text was read. A retest
after any hang **must** clear ASD first - `ASD.EXE` from Start -> Run lists the
skipped drivers and re-enables them - or reassociate the driver through SetupX,
which also clears it. Otherwise the result is predetermined.

## Isolation

The DRV does not need our mini-VDD on this family, which is what makes the swap
a fair test:

- `V9xVddRegister` in `src\display16\runtime.asm` talks to the **master VDD**
  (`VDD_DEVICE_ID EQU 000ah`), which is present whatever mini-VDD is loaded.
- Our mini-VDD's private API (`4F9Ch`) is used only by the tier-0 VBE query path
  in `v9x_vbe_default_aperture`. The S3 family has a `read_aperture` hook and
  never takes it.
- `V9xMiniApiInitialize` is written to refuse and latch when the VxD is absent or
  answers with the wrong magic, so running without it is a designed-for state.

Single-variable test, everything else the INF's own configuration:

| `drv` | `minivdd` | Result |
|---|---|---|
| `v9xdisp.drv` | `v9xmini.vxd` | Windows protection error, boot hangs |
| `v9xdisp.drv` | `s3.vxd` | `enable-ok`, desktop at 1024x768x16 |

Confirmed over two boots in the working configuration, the second a clean reboot
with no preceding fault.

## What the working configuration proves

With the stock mini-VDD the driver publishes a correct `C:\V9XHW.INI`:

```
Adapter=S3 Trio32/64 86C764
VendorId=5333
DeviceId=8811
Direct3D=not-advertised
VideoMemoryBytes=2097152
VideoMemoryStatus=valid
CoreClockKHz=59957
MemoryClockKHz=59957
CoreClockRelation=shared-memory-clock
```

`V9XDDP.EXE` on the same boot, at 1024x768x16:

| Field | Value | Note |
|---|---|---|
| `GblHalVidMemTotal` | `0x00080000` | 524,288 - see the 0.4.1 note below |
| `BltFillHr` / `BltFillPixelOk` | `0` / `1` | Trio64 8514/A fill, pixel-verified |
| `VBlankHr`, `ExclusiveVBlankHr` | `0` | real vblank services |
| `FlipHr` | `0` | `Flip20Ms=1`, `FlipMaxMs=1` |
| `SrcCopyBltHr`, `StretchBltHr`, `KeySrcBltHr` | `0` | |
| `TexSurfaceHr`, `TexHandleHr`, `TexSwapHr` | `0x80004005` | correct: no S3d core |
| `FlipPixelOk` | **`0`** | open, see below |

So the 2D path, the DirectDraw HAL, the flip path and the vblank services all
work on this card. The hang is the mini-VDD alone.

## Bonus: the 0.4.1 heap fix is verified here

`GblHalVidMemTotal=0x00080000` is 524,288 bytes, which is exactly
`2,097,152 - (2048 x 768)`. Before the 0.4.1 fix `dd16.c` would have used its
4 MiB literal and advertised `4,194,304 - 1,572,864 = 2,621,440`
(`0x00280000`) - five times the off-screen memory the card actually has. This is
the first hardware measurement of that fix, and it lands on the predicted number.

## Open, and not to be confused with the above

- **`FlipPixelOk=0`** while `FlipHr=0`. The flip is accepted and timed at 1 ms,
  but the post-flip pixel check does not see what it expects. Could be a
  verification-timing artefact in the probe or a real scanout issue. Untriaged.

## Next step

`BOOTLOG.TXT` is the evidence that would name the failing init. `BootLog=1` was
added to `MSDOS.SYS` `[Options]` on this machine and produced a **0-byte**
`C:\BOOTLOG.TXT`, so that switch alone does not work here; the F8 "Logged" boot
menu option is the route to try, and it needs someone at the keyboard.

Until then the suspect list is led by the 0.4.0 ring-0 VBE collection -
`Device_Init` gathering 4F00h and the seven standard 4F01h answers under nested
execution, reached through `INT 2Fh AX=1684h`. It is the newest code in the VxD
and it had never met a real video BIOS.

Note that `V9XMINI.VXD` is unchanged in behaviour between 0.4.0 and 0.4.1: every
file the 0.4.1 fix touched compiles into `V9XDISP.DRV`. This is a pre-existing
fault that only physical hardware exposed.
