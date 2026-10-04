# The SiS 6326 runs Velocity9x at tier-0: enable-ok on the first boot

Date: 2026-10-05. Machine: A8U4I5 boot 208, SiS 6326 card 2 (rev 0Bh,
subsystem `63261569`, BIOS 1.28q). Package: the `sis` family, content of
`c58dfe5`. Evidence: `docs/probe/a8u4i5-sis6326-tier0-2026-10-05/`. Plan:
[sis-6326-family.md](../plans/sis-6326-family.md), Phase 1.

## Install

The package was pushed to `C:\V9XSIS`. The preflight `V9XSTAGE.EXE`
passed: the VxD and Win16 DRV loaded and unloaded cleanly. The driver was
installed through Device Manager, Update Driver, Have Disk, replacing
SiS/AOpen 2.28. Windows listed "Velocity9x SiS 6326" as compatible with no
warning, since the INF's bare `PCI\VEN_1039&DEV_6326` matches. A warm
restart followed.

The agent's `type` input delivers no characters on this machine since the
2026-10-04 session; the path was reached through the Browse dialog.

## Boot 208

`V9XBOOT.INI`:

- `Stage=enable-ok`, 640x480x8, pitch 640, aperture BAR0 `DE000000h`.
- VBE 2.0, 4 MiB (`mem=64`).
- The mini-VDD's collection listed 44 BIOS modes, queried 44, cached 29.
- Every BIOS mode reports a linear framebuffer at `DE000000h` and a packed
  pitch (width x bytes per pixel). That includes 24 bpp packed at three
  bytes a pixel (640x480x24, pitch 1920). There is no 32 bpp mode, as the
  datasheet says.

`V9XMODES.INI` publishes 22 modes: the 7 static rows, plus 15 from the BIOS
at 8 and 16 bpp. The 15 are 320x200, 320x240, 320x400, 400x300, 512x384,
1280x1024 and 1600x1200 at both depths, and 640x400 at 16 bpp. The seven
24 bpp modes are not published.

`V9XHW.INI` reports adapter "SiS 6326", 1039/6326, acceleration `none`,
Direct3D `not-advertised`, VBE VRAM 4194304.

## GDI and mode switching

| Run | Result |
|---|---|
| `V9XGDI /auto` at 640x480x8 | PASS: black, white, red, blit, SetPixel |
| `V9XMSW /cycle:10` | PASS, 10 of 10 |
| `V9XMSW /depth:10` | PASS, 10 of 10 |
| `V9XMSW /set:` each of the 22 published modes | PASS, `ChangeResult=0`, all 22 |
| `V9XMSW /set:640x480x8` back after each | exit 0 every time |

Three of the 44 switch results read `Result=INCOMPLETE` with exit code 0:

- **The return from 320x240x16.** Repeated twice, it passes both times. The
  first read was a stale INI.
- **Two switches to 640x480x8 from 640x480x8.** These read INCOMPLETE on
  every repeat, still with exit 0. When the mode does not change, the tool's
  INI is not seen updated. This is an instrument artefact; the exit code is
  the authority.

After the low-resolution switches, Windows had rearranged the desktop icons
and doubled the taskbar's height. Cosmetic, and Windows' own behaviour after
a very small mode.

## DirectDraw (`V9XDDP`)

| Run | Result |
|---|---|
| default | `COMPLETE`. The HAL attaches (`setinfo-ok`) with a 3.7 MiB video-memory heap (`3B5000h`). 640x480x16 primary and back buffer, colour fill correct, overlapping blits down, right and pitch-down correct, 20 flips succeed. |
| `/modestress` | 32 of 32 SetDisplayMode/RestoreDisplayMode round trips |
| `/pal8` | 640x480x8: primary, palette, lock and readback succeed. **640x400, 320x240 and 320x200 at 8 bpp: SetDisplayMode fails with `88760078h`** |

`FlipPixelOk=0` is the instrument defect filed on 2026-09-02, which reads 0
on every target including ones whose flips work. There is no Direct3D HAL,
as tier-0 advertises none.

## Not established

- **Why DirectDraw refuses three 8 bpp modes that GDI sets and that
  DirectDraw's own mode table lists.** `/pal8` passed the same modes on the
  Rage IIC on 2026-10-02, so this may be specific to this family's table or
  to the SiS BIOS. Not investigated.
- **Card 1** (rev C3) under Velocity9x: not run, card parked.
- **24 bpp:** the BIOS offers it packed; tier-0 does not publish it.
- **Software Direct3D:** `Direct3DMode=none`, not tried.
- **DOS boxes:** none opened. This machine has no DOS-mode testing, by
  standing instruction.
