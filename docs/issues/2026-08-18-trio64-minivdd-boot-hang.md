# V9XMINI.VXD hangs the boot on a physical S3 Trio64

Status: **root-caused and fixed, 2026-08-18.** The `Device_Init` VBE collection
allocated its V86 scratch byte-aligned and truncated the address to a real-mode
segment, so the 'VBE2' stamp and the BIOS wrote outside the buffer. A
paragraph-aligned, zero-initialised allocation boots clean on the same card, and
the S3 family additionally ships the mini-VDD with the collection assembled out
entirely. See "Isolation, round 2" below.

Originally isolated to `V9XMINI.VXD` on 2026-08-18. The DRV is not
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

## Isolation, round 2: the collection is the trigger, alignment is the cause

Reading the collection code found the defect before any further boot was spent.
`V9xMini_Vbe_Collect` allocated its 512-byte V86 scratch with
`_Allocate_Global_V86_Data_Area, <512, 0>` - flags 0, **byte** alignment - and
then `shr eax, 4` to make a real-mode segment. On a non-paragraph-aligned
allocation that segment starts up to 15 bytes *below* the block, so:

- the ring-0 `'VBE2'` stamp writes into whatever V86 global data precedes the
  block,
- every `V9xMini_Vbe_Peek_Word` offset is skewed, and
- the BIOS, handed `ES:DI` at the truncated segment, can write past the end.

86Box guests evidently received paragraph-aligned blocks; the physical machine
did not have to.

Two candidate builds, tested on BARRY in sequence, each over two boots with the
installed 0.4.1 DRV (build `2094b52`) unchanged and ASD confirmed clear:

| `V9XMINI.VXD` build | Collection | Result |
|---|---|---|
| `8976898-novbe` | assembled out (`V9X_NO_VBE_COLLECT`) | `enable-ok`, 1024x768x16, both boots |
| `8976898-vbefix` | kept, `GVDAParaAlign + GVDAZeroInit`, direct-linear stamp | `enable-ok`, 1024x768x16, both boots |

The first result confirms the collection is the only part of the VxD this
hardware objects to - the rest of `Device_Init` (dispatch-table install, DPMS
and power callbacks, serial diagnostics) is exonerated. The second pins the
fault inside the collection to the allocation, since the alignment fix is the
only behavioural difference from the code that hung.

The fixed collection also now emits bounded serial markers -
`vbe-collect start`, one `vbe-call fn=/arg=` line per BIOS call, `ret=` after
each, `vbe-collect done` - so any future hang in it names the exact BIOS call
on a serial capture.

## Resolution

- **Root cause fixed** in `src\minivdd32\loader.asm`: paragraph-aligned,
  zero-initialised V86 allocation; the stamp uses the returned linear address.
- **The S3 family no longer runs the collection at all.** Its chips read the
  aperture from hardware and never consult the 4F9Ch cache, so the s3 package
  (and matrox-m2, same reasoning) ships a mini-VDD with the collection
  assembled out via `Build.MiniVddVbeCollect = $false` in the family manifest.
  Tier-0 families (`vbe`, `ati`) keep the fixed collection - they have no other
  way to learn the aperture. Decision record:
  `docs\decisions\2026-08-18-minivdd-vbe-collect-gating.md`.
- What remains true: `Exec_Int` into the video BIOS has no timeout, so a BIOS
  that never IRETs would still hang a tier-0 boot; the per-call markers exist
  to make that failure legible.

For the record, the abandoned route: `BOOTLOG.TXT` via `BootLog=1` in
`MSDOS.SYS` produced a 0-byte file on this machine and was never needed.
