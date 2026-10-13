# Rage IIC 3DCIF reference run on ATI's driver: four demos draw, four refuse, one locks the machine

Date: 2026-10-13

Status: Measured on A8U4I5, boots 450 and 451. Phase 0, step 4 of
[ati-3dcif-wrapper.md](../plans/ati-3dcif-wrapper.md).

## Setup

- A8U4I5, Rage IIC AGP `1002:4757` SUBSYS_47571002 REV_7A, 4 MiB,
  devnode `PCI\...\000800`, class key `Display\0006`.
- Boot 448 ran Velocity9x (ati package 0c26021, enable-ok
  1024x768x16). `C:\3DCIF\SWAP\TOATI.REG` (the 2026-10-03
  `ATICLS6.REG` plus `BitsPerPixel=16`, 1024x768) imported with
  `REGEDIT /s`, then a warm restart: boot 450's inventory reports
  "RAGE IIC AGP (4757)", provider "ATI Tech. - Enhanced", the community
  4.11.2611 build, 1024x768x16. The previous key is exported to
  `C:\3DCIF\SWAP\BEFATI.REG`; `TOV9X.REG` switches back.
- The 3DCIF DLL used is the one already in `C:\WINDOWS\SYSTEM`:
  `ATI3DCIF.DLL`, version resource 4.11.2692, 179,200 bytes, SHA-256
  `BE2D386C...D48E93`. It matches none of the 15 DLLs in gona's
  `all_ATI3DCIF_DLL.zip`, including that set's own 4.11.2692. No demo
  folder carries its own copy.
- Demos from marxveix's pack, `C:\3DCIF\PACK\ATi 3D C InterFace`. Each
  is launched detached from its own folder; screenshots are taken 12-60 s
  later and again 5 s after that; `V9XWND` lists the windows; Escape is
  sent; `V9XWND` again.

## Results

| Demo | Result | Evidence |
|---|---|---|
| `CUBES\ATICUBES.EXE` | Draws: two textured, lit cubes, 640x480. Exits on Escape. | `cubes-b.png` |
| `PIXIE\PIXIE.EXE` | Draws: stones, grass, sky, title card, 640x480. Exits on Escape. | `pixie-*.png` |
| `FAIRY\FAIRY.EXE` | The same scene as Pixie (same window class). Draws. marxveix notes a background error in Fairy; not seen in these two frames. | `fairy-*.png` |
| `LAPUTA\LAPUTA.EXE` | Draws: the island, tents, carousel, after ~20 s of grey fade-in. Exits on Escape. | `laputa-long-b.png` |
| `ATi-3D\ATi-3D.exe` | The capture is noise both runs (bands of random pixels, pink, green). Unresolved: the agent's capture of the GDI primary may not show what this demo puts on screen. Exits on a key, as its readme says. | `ati-3d-*.png` |
| `Knight\DEMO.EXE`, `MORPH.EXE`, `VQ.EXE` | Refuse: message box "ERROR: Must be run on a RagePRO". | `knight-*-b.png` |
| `VIDSAMPL\BIN\EXVD2.EXE` | Refuses: "DirectSound Failed. Exiting." A8U4I5 has no sound device. | `vidsampl-exvd2-b.png` |
| SDK 4.1 `samples\specular\specular.exe` | **Locks the machine.** Window "RAGE PRO Specular Example" stays grey; the agent stops answering within ~12 s, ICMP still answers, then the port refuses. Michael saw it frozen on the monitor and reset it with Ctrl+Alt+Del (boot 451). | Michael's photo; no files |

Not run, because each is a Rage PRO sample and specular showed that
such a sample can lock the IIC instead of refusing it: `rpexpl1`-`4`,
`RPROEX1`-`4`, `R3TEXCMP`.

`V9XDDP` under ATI's driver (boot 450, `V9XDD-ATI-boot450.INI`):
DirectDraw and D3D hardware present (`D3DMainIsHardware=1`), and the D3D
render target is 555 (`D3DTargetRMask=31744`, G 992, B 31). That agrees
with marxveix's report that ATI's default 16-bit layout is 555. The
desktop is 565 under Velocity9x on the same card (`is555=0`, boot 448).

## What it means for the wrapper

- The Knight demos check the chip and refuse a Rage II-class answer.
  The plan's Identity section proposes reporting a Rage II-class device;
  under that answer they will refuse on ours as well. Whether they
  check `GetInfo`'s `u32ASICID` or something else is a census question.
- Cubes, Pixie, Fairy and Laputa are the native reference set for the
  IIC: four demos that draw on ATI's driver, with frames to compare.
- Specular is a Rage PRO sample that does not check the chip. On ours it
  must not hang whatever state it sets; on ATI's it does.
- EXVD2, and any title that needs DirectSound (Tomb Raider), needs a
  sound device in A8U4I5 first.

Probe files: `docs/probe/a8u4i5-rage-iic-3dcif-ati-2026-10-13/`.
