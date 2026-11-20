# A Radeon X1300 (RV515) boots 0.57.0 on the VBE-generic path: field report V9X-EY69HT

Date: 2026-11-20
Machine: unknown reporter, no contact given. ATI Radeon X1300 class card,
`1002:7146` subsystem `1002:0322` rev 00 (RV515, ATI-branded board),
with its second function `1002:7166` subsystem `1002:0323`. Windows 98
SE (4.10.2222 A), DirectX 6.1 (4.09.00.0904), 768 MiB
(`MemoryKB=786940`), CPU family 0Fh (`Mtrr=cpu=000f`).
Evidence: diagnostic report `V9X-EY69HT` on the update server, submitted
2026-11-20 with the description "new device!" and nothing else. Built
`0.57.0 bfaa8d9e` (the release commit). Files: `V9XSNAP.INI`,
`V9XBOOT.INI`, `V9XHW.INI`. Nothing here was measured by us.

## What the report says

- `V9XHW.INI`: `Adapter=Generic VESA adapter (no chip-specific support)`,
  `VendorId=unclaimed`. No family claims `1002:7146`. `ModeSwitching=vbe-lfb`,
  `Acceleration=none`, `GdiAcceleration=none`, `Direct3D=not-advertised`,
  `Direct3DMode=software`, `ScreenShadow=on`, `D3DSoftSysMem=refused`,
  `ColourLayout=565-auto`, `VSync=application`. This is the generic VBE
  driver doing what it is meant to do on an unknown card.
- `V9XBOOT.INI`: `Stage=enable-ok`, `DriverInitResult=ok`. Desktop
  1024x768x16, 5:6:5 (`is555=0`), pitch 2048.
  - VBE 3.0 (`v=0300`), OEM software rev `090c`, `mem=256` x 64 KiB:
    the BIOS reports 16 MiB, and the driver uses 16 MiB
    (`VddReserve ... vram=16777216`). X1300 boards carry 128 or 256 MiB;
    how much this one has is not in the report. The same 16 MiB VBE
    figure was seen on the RS690
    ([`2026-11-03-m-gb8kre-rs690-vbe-first-look.md`](2026-11-03-m-gb8kre-rs690-vbe-first-look.md)),
    which suggests ATI's VBE caps what it reports; that is an inference
    from two BIOSes, not a measurement.
  - LFB at `d0000000` (`Aperture=m=none bar=0 pci=1`), mapped through
    DPMI (`DpmiMap=01000000:ok`).
  - MTRRs: variable range 0 is write-back over 0-2 GiB, range 1 is a
    1 MiB uncached hole at `7ff00000`; the driver wrote a 16 MiB
    write-combining range at `d0000000` into slot 2 (`MtrrWrite=ok s=2
    b=d0000001 m=ff000800`). So the framebuffer runs WC.
  - `VbeCache=s=1928 l=76 q=67 c=64 p=0 f=0147`: the BIOS lists 76
    modes, 67 were queried, 64 cached (the cache's limit, as in the
    2026-08-25 Stage 1 records), so 64 `VbeModeNN` lines. Each resolution comes as an
    8 bpp mode, a 555 and a 565 mode, and an 8:8:8 32 bpp mode, all with
    the LFB at `d0000000`. The 64 include 640x480 to 1600x1200 and 1856x1392; there
    is no widescreen mode among them. Whether the 12 modes past the cache
    limit include one is not known.
- `[Card]`: function 0 (`7146`) runs on Velocity9x (`Display\0000`,
  problem 0). Function 1 (`7166`) is bound to "Standard PCI Graphics
  Adapter" with problem code 22 (disabled). That is the normal state of
  an RV5xx secondary head under Windows 98, not a fault. A stale
  `Display\0001` (`vga.drv`, MSDISP.INF) entry is also listed; it is not
  bound to either present function. Leftover OpenGL drivers from
  NVIDIA (`RIVATNT`) and ATI (`atio9xxx`) are registered beside ours.
- `V9XSNAP.INI`: `HalBuild=` is empty and every HAL counter is zero
  (`DriverInitDone=0`, `ModeWidth=0`, `Processes=0`). V9XTRACE ran with
  no Direct3D or DirectDraw process alive, so the HAL's shared sections
  were not there to read; the same reading as the GM45 field report
  ([`2026-11-05-gm45-gl40-field-report.md`](2026-11-05-gm45-gl40-field-report.md)).
  The snapshot says nothing about any program the reporter ran.
- `[DiagFiles]` on the machine lists `V9XMODES.INI`, `V9XESC.INI`,
  `V9XSYNC.INI`, `V9XTRAIL.TXT` and `V9XDDH.INI`; they were not
  attached.

## What this settles, and what it does not

- The VBE-generic driver installs and sets a 16 bpp desktop with a WC
  framebuffer on an RV515 BIOS. That is all the report shows. No
  application, Direct3D, OpenGL or Glide result, no screenshot, and no
  statement from the reporter beyond "new device!".
- Nothing in it is a fault. There is no issue to file.
- The RV515 is the card whose display block the RS690 plan cites as
  the AVIVO reference (`D1GRPH_*` at 6100h-, `r500_reg.h`), and the
  2026-11-04 note in [`ati-rs690-x1200.md`](../plans/ati-rs690-x1200.md)
  names the R300-R500 discrete cards as the next users of that tree
  "should one turn up". One has, but without a contact or a remote agent
  there is no way to measure it: the R500 3D core (`IS_R500_3D`, a
  different fragment ISA from the RS690's R300 one), the VRAM size,
  and the BAR layout are all unread. Claiming `1002:7146` for the ati
  family on this report alone would be a claim about silicon nobody has
  measured.
- Open questions a follow-up from the reporter could answer: real VRAM
  (BAR sizes from the PCI config space), the unattached
  `V9XMODES.INI`, and a snapshot taken while a Direct3D program is
  running.
