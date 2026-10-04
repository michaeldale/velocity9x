# Half-Life refuses Direct3D and OpenGL on the 2 MiB Trio3D

Date: 2026-10-04. Machine: A8U4I5, S3 Trio3D `5333:8904` (86C365), 2 MiB,
s3 family on the ViRGE path, build `6eab427`. Status: open, parked.
Decision record: `docs/decisions/2026-10-04-trio3d-8904-on-the-virge-path.md`.
Evidence: `docs/probe/a8u4i5-trio3d-8904-2026-10-04/` (`half-life/`,
`bios-mode-merge-b199/`).

## Symptom

Michael: the menu works, a map fails, in both renderers. Half-Life 1.1.1.0
has not run on the ViRGE path on any machine before, so there is no
baseline.

- **Direct3D.** The menu draws in hardware (5 contexts, 128,718
  primitives, 256 flips, no texture or surface refusal in `V9XSNAP.INI`).
  Starting a game, HL reports "The selected D3D mode is not supported by
  your video card" (`half-life/d3d-mode-not-supported.png`,
  `bios-mode-merge-b199/hl-d3d-refusal-after-merge.png`).
- **OpenGL.** The menu draws. `V9XGL.LOG`: 890 of 1,781 hardware
  texture creations fail `0x8876017C` (DDERR_OUTOFVIDEOMEMORY) and 1,374
  batches are redrawn on the CPU path, as designed
  (`half-life/V9XGL-tail-gl-menu.log`). The map failure itself was not
  watched.

## Ruled out

- **Too little VRAM for the mode, as the cause of the D3D refusal.** With
  the desktop at 640x480x16 the DirectDraw heap is 1.41 MiB
  (`GblHalVidMemTotal=0x16A000`, `V9XDD-640x480-desktop.INI`), room for a
  640x480x16 back and Z surface, and HL still refuses.
- **A smaller mode.** The s3 family now merges the BIOS's list, so
  512x384, 400x300 and 320x240 at 16 bpp are offered (boot 199). HL's
  Direct3D mode list (`hl-d3d-video-modes.png`) shows only 640x480,
  800x600, 1024x768 and 1152x864: nothing below 640 for a hardware
  renderer. `-w/-h` and `EngineModeW/H` in
  `HKCU\Software\Valve\Half-Life\Settings` did not change the selection.
- **Z surfaces failing.** `D3DZ*Hr=0x88760231` in `V9XDD.INI` is
  `V9X_DDERR_UNSUPPORTED`, the probe's not-run marker, not a failure.

## Suspect, untested

A texture format. The S3D path enumerates two, 1555 and 4444
(`TexFormatCount=2`, `TexFormat565=0`). The Rage XL in the same machine
offers 565 and runs HL Direct3D. Incoming refuses this chip for want of
565 or P8 (`2026-09-05-incoming-refuses-the-hal-texture-formats.md`). The
S3D engine has no 565 texture format, so offering one would mean
converting to 1555 at upload, losing a green bit.

For OpenGL the likelier cause is plain memory: at 640x480x16 a 2 MiB card
keeps about 200 KiB for textures after front, back and Z.

## Also open

The merged sub-640 16-bpp rows (`0133`, `0143`, `0153`) carry S3 OEM
numbers with no VESA 5:5:5 sibling, so they publish 5:6:5 while the
baseline 16-bpp rows are 5:5:5 (`555-auto`). Hardware Direct3D at one of
them would draw ZRGB1555 into a 5:6:5 surface
(`2026-09-01-virge-3d-writes-zrgb1555.md`).

## Next, when resumed

1. Find what HL's D3D start-up actually checks: a D3D enumeration log
   from HL, or a DirectDraw/D3D call trace, before changing the driver.
2. If it is the texture format, offer 565 converted to 1555 and rerun.
3. Driving HL here: its menu ignores injected mouse moves until a focusing
   click, keys work only with focus; `valve\autoexec.cfg` is now an empty
   file left by this investigation.
