# The MGA-2064W at tier-0: enable-ok on the first physical boot, and a padded stride the DIB did not follow

Date: 2026-10-09. 86Box `Win98SE-Millennium` (port 9877, boots 29-31) and
A8U4I5 (10.0.1.172, boots 354-356), a physical MGA-2064W, revision 01,
subsystem 0000, 8 MiB.

The 2064W had sat in `matrox-m2`, a guarded drop-in that never set a mode
under Windows. It moves to a new INF family, `matrox`, at tier-0: the VBE
BIOS sets modes, 4F01h reports the framebuffer, the CPU draws, and no MGA
register is written ([plan](../plans/matrox-millennium-family.md)).

## Native baseline

Windows 98 bound the card to its inbox `MGAPDX64.DRV` (DXMGA.INF,
4-23-1999) at 800x600x16. Under it `V9XDDP` completes with Matrox's HAL:
`GblHalVidMemTotal=0x00715A00`, 99 modes, fills correct
(`docs/probe/a8u4i5-mga2064w-native-2026-10-09/`). Configuration Manager
puts the 16 KiB control aperture at `DC000000` (BAR0) and the 8 MiB
framebuffer at `DD000000` (BAR1).

## The guest found a shared-layer bug first

First boot in the guest: `Stage=enable-ok`, 640x480x8, a correct picture.
The first live switch to 800x600x8 reported `V9XMSW Result=PASS` while the
emulator's window showed every row sheared
(`docs/probe/a8u4i5-mga2064w-tier0-2026-10-09/guest/800x600x8-before-fix.png`).
`V9XBOOT.INI` said why:

    Surface=pitch=960 bpp=8 dwb=800 dds=800 w=800 h=600

The mode row merged from the BIOS carries the BIOS's 960-byte stride; stage
9 asked 4F06h for 960 pixels' worth and the card kept scanning at 960; but
`CreateDIBPDevice` derives the stride from `biWidth` and built the DIB at
800. GDI writes and reads back through the same 800, so the pixel check
agrees with itself - the hazard the comment over `v9x_vbe_default_pitch`
describes.

`ddi.c` already corrects exactly this after `CreateDIBPDevice`, but only for
a family that sets `unalias_pitch` (Gen3's 2112-byte stride). The gate is
removed: the correction applies whenever the DIB's stride differs from the
selected mode's, and changes nothing where they agree. After it, 800x600x8
reads `dwb=960 dds=960` and the picture is correct
(`guest/800x600x8-after-fix.png`). Any family whose BIOS pads a mode was
exposed to this; none had a padded row in its merged list until now, as far
as the recorded `V9XBOOT.INI` files show - not checked exhaustively.

Guest after the fix: every 8, 16 and 32 bpp mode the 4 MiB guest offers
switches live with a correct emulator picture, `V9XGDI /auto` PASS at
800x600x16, `V9XDDP` COMPLETE with the HAL attached and CPU fills correct.

## The physical card

Installed by Update Driver from `C:\V9XMGA`. First boot (356):

    Stage=enable-ok
    Surface=pitch=1920 bpp=16 dwb=1920 dds=1920 w=800 h=600
    VbeController=v=0200 mem=128            (8 MiB)
    Aperture=m=0 bar=1 pci=1 b=dd000000
    MgaCrtcExt3=81

- **CRTCEXT3 is 81h: the BIOS sets mgamode itself** when it enters a linear
  mode (bit 7), with a video clock scale of /2 (bits 2:0 = 001). This is
  the read the 2026-09-11 record asked for; the driver does not need to
  write the bit. Measured at 800x600x16 only.
- **The BIOS pads 800-wide modes on this card too**: 960 bytes at 8 bpp,
  1920 at 16; 800x600x32 is packed at 3200. The same as the BringupKit
  card's BIOS.
- `V9XGDI /auto` PASS at 800x600x16.
- `V9XMSW /set` PASS for all 13 modes it was given: 640x480, 800x600,
  1024x768, 1280x1024, 1600x1200 at 8 and 16 bpp, and 640x480, 800x600,
  1024x768 at 32 bpp; each one's DIB stride equals its mode stride
  (`modes-v9xmsw.txt`).
- `V9XDDP` COMPLETE under `V9XHAL.DLL`: `GblNoHardware=0`, 6.9 MiB
  off-screen (`0x006E6C00`, against Matrox's 7.1), 17 modes, CPU fill and
  all four overlap copies pixel-correct. No flips (`FlipPixelOk=0`): the HAL
  declines them without a display-start capability.

**Not established: the picture on the monitor.** Every check above reads
back through the driver's own stride. The guest's emulator window shows the
scanout is right on 86Box's model, and the physical card has the same BIOS
behaviour, but nobody has looked at A8U4I5's screen under this driver.

## The preflight broke Matrox's driver

`V9XSTAGE.EXE`, run under MGAPDX64 before the install, loads `V9XDISP.DRV`
inactively through `V9X16LD`. It refused as designed
(`Stage=fail-validate-no-identify-hook`), but afterwards agent screenshots
were noise and `V9XDDP` found `GblNoHardware=1`, no video memory and 26
modes instead of 99. A warm restart restored everything. Filed:
[issue](../issues/2026-10-09-preflight-disturbs-the-stock-matrox-driver.md).

## Rollback

The INF reused `Display\0011`, Matrox's class key, rather than adding one,
so there is no one-value swap. MGAPDX64's files are still in SYSTEM and the
pre-install export of the class key is
`docs/probe/a8u4i5-mga2064w-native-2026-10-09/DISPCLS.REG`; re-importing its
`Display\0011` section and a warm restart is the route the ATI swap used.

## Gates

`check-tree`, `build-host` and `run-checks` green (see the commit).
`build-active-package -Family matrox` builds and audits.
