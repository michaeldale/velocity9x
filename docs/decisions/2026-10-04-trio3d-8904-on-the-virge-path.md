# The Trio3D (`5333:8904`) on the ViRGE path: first boot on A8U4I5

Date: 2026-10-04. Machine: A8U4I5, P3 1 GHz, 440BX, Windows 98 SE.
Card: `PCI\VEN_5333&DEV_8904&SUBSYS_89045333&REV_01`, fitted in place of
the Rage XL PCI. Evidence: `docs/probe/a8u4i5-trio3d-8904-2026-10-04/`.

## Decision

`8904` is bound as a second alias of the s3 family's `virge-dx` chip, on
the Trio3D/2X's terms: the ViRGE's CR53 aperture hook and
`v9x_trio3d2x_fill_engine`, so no two-pass blend and no unlit alpha
(Michael: "this card is very similar to the trio3d/2x"). Before this, no
family bound the id and Windows ran it on Standard PCI Graphics Adapter
(VGA) at 640x480x4.

The name "S3 Trio3D 86C365" is the public PCI id list's.
`scripts/parse-vga-survey.ps1` calls `8904` a Trio3D/2X (86C362) and
`8903` the Trio3D. Not settled; no register read was taken to decide it.

The VBE tier-0 manual model was pushed to `C:\V9XVBE` and its preflight
passed, but it was not installed; the alias replaced that route.

## Host side

The manifest entry went in first. `build-host.ps1` then failed two
checks, as expected: the family-matrix test found `5333:8904` refused by
the backend probe, and the hw16 test counted 8 device entries for 9
manifest ids. Adding the id to `v9x_s3_device_ids` and
`v9x_trio3d_device` to the device list made both pass. `run-checks.ps1`
passed.

## Install

`V9XSTAGE.EXE` from the s3 package: PASS. Update Driver, Have Disk
`C:\V9XS3` offered one compatible model, "Velocity9x S3 Trio3D 86C365"
(`have-disk-model-list.png`). Windows' restart prompts were declined and
the restart was done by the agent (boot 197 -> 198, warm).

## Measured, boot 198

- `V9XBOOT.INI`: `Stage=enable-ok` at 640x480x8. Aperture from CR59/CR5A
  at `0xD8000000`.
- `V9XHW.INI`: `Adapter=S3 Trio3D 86C365`, `Direct3D=hardware-s3d`,
  `VideoMemoryBytes=2097152`, core and memory clock 75,170 kHz,
  `GdiAcceleration=gdi-fill-copy-overlap-text`, `ColourLayout=555-auto`.
- `V9XMSW.INI`: live switch 640x480x8 -> 1024x768x16, `Result=PASS`.
- `V9XACCE.INI` (`V9XGDI /accel` at 1024x768x16): `Result=PASS`, 500
  operations, 20 comparisons against the DIB engine all equal, no idle,
  FIFO or reset events outside the deliberate fault injection.
- `V9XDD.INI` (V9XDDP): `Result=COMPLETE`, HAL device hardware. Pass:
  triangle fill and every one of 12 shapes, slot permutations, base and
  tiled texture, mip level select, colour key, vertex alpha, 4444 and
  1555 formats, perspective, 48 of 48 texture-matrix rungs.
  Fail: specular (Gouraud and textured), fog (vertex and depth),
  trilinear blend, bilinear (`Tex8BilinearOk`), the 2x2 texture and the
  565-reference 8x8 texture, decal alpha, clamp, `ChainB`, `ChainRows`,
  `MipTri`, alpha curves F1, F2 and F4. Z and Mixed did not run: every
  call returned `0x88760231`, as on the Rage XL on 2026-10-03.

None of the failures has been compared with the Trio3D/2X's matrix on
this machine, so which are this chip and which are the probe at 2 MiB is
not known.

## Memory size: 2 MiB, measured

CR36 bits 7:5 = 4, which the ViRGE/Trio64 decoder reads as 2 MiB. 86Box's
model encodes the Trio3D/2X differently from the ViRGE/DX
(`build/reference-vid_s3_virge.c`, 4 MiB = code 2, 8 MiB = code 0), so
the decode alone proved nothing for this part. Michael expected more
than 2 MiB.

**Retracted: "VBE reports about 8 MiB".** The `VbeController`,
`VbeCache` and `VbeModeNN` lines in boot 198's `V9XBOOT.INI` are the
Rage XL's, left from boot 181: they match
`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/first-boot/V9XBOOT.INI` line
for line. The s3 family builds its mini-VDD without the VBE collection
(`MiniVddVbeCollect = $false`) and never calls `v9x_vbe_trace_cache`,
which is the only writer that clears those keys. The Trio3D's BIOS was
never asked anything; the 8 MiB, the `0xDC000000` base and the mode list
were another card's.

`V9XVRAM.EXE` (new, `tools/diag/vram_walk_win32.c`) wrote a signature
every 512 KiB from 0 to 7.5 MiB through the primary's lock, highest
first, read them back and restored the originals (`V9XVRAM.INI`,
640x480x16 desktop):

| Offset | Read back |
|--------|-----------|
| 0 - 1.5 MiB | own signatures 0-3 |
| 2 - 3.5 MiB | unrelated values, not a signature |
| 4 - 5.5 MiB | signatures 0-3 |
| 6 - 7.5 MiB | unrelated values |

Writes between 2 and 4 MiB do not stick, and the decode wraps at 4 MiB.
The card holds 2 MiB as configured. CR36 and the 512 KiB heap at
1024x768x16 were right. Whether more
memory is fitted but strapped off was not looked at; counting the chips
on the board would answer that.

## Half-Life 1.1.1.0

Michael: the menu works, a map fails, in both Direct3D and OpenGL.

- **Direct3D**, `-d3d -w 640 -h 480 -full`: the menu draws in hardware
  (5 contexts, 128,718 primitives, 256 flips, the Z surface accepted,
  no texture or surface refusals in `V9XSNAP.INI`). Then HL itself
  reports "The selected D3D mode is not supported by your video card"
  (`half-life/d3d-mode-not-supported.png`). At 640x480x16 the front,
  back and Z surfaces take 1.8 MiB of the 2; the driver refused nothing,
  so the refusal is HL's own judgement of what is left.
- **OpenGL**, `-gl -w 640 -h 480 -full`: the menu draws. In `V9XGL.LOG`
  890 of 1,781 texture creations fail with `0x8876017C`
  (DDERR_OUTOFVIDEOMEMORY) and 1,374 batches are refused by the engine
  and redrawn by the CPU path, as designed. Why a map then fails was not
  watched.

## The s3 family merges its BIOS's mode list (boot 199)

Michael asked for the S3 BIOS list to be merged and for 512x384, 400x300
and 320x240 to be offered. Done without static rows:

- `s3` now builds its mini-VDD with the VBE collection
  (`MiniVddVbeCollect = $true`), reversing decision 2 of
  `2026-08-18-minivdd-vbe-collect-gating.md` for this family. The VBE
  1.2 S3 BIOSes measured on 2026-08-20 stop at the ring-0 2.0 check, so
  on them this is one 4F00h call at boot and the baseline table.
- `v9x_vbe_scan_admit_flags` with `V9X_VBE_ADMIT_FLAG_APERTURE_KNOWN`
  waives the linear-attribute and PhysBasePtr rules for a family with a
  `read_aperture` hook; `modes16.c` passes it, and judges VRAM against
  the smaller of the BIOS figure and the family's own (CR36 here). Host
  tests written first and watched failing (18 checks).
- `check-tree` now requires a hooked family to state the key rather than
  forbidding the collection; `PCIRebalance` follows the hook, not the key.
- The hook path now calls `v9x_vbe_trace_cache`, so a card swap can no
  longer leave the previous card's VBE lines in `V9XBOOT.INI`.

Evidence in `bios-mode-merge-b199/`. The Trio3D's own BIOS, read for the
first time: VBE 2.0, `mem=32` (2 MiB, agreeing with the walk), every mode
linear at `0xD8000000` - so the aperture waiver was not needed on this
card. 48 listed, 32 cached, 17 admitted, 10 merged into baseline rows.
`V9XMODES.INI`: 29 rows, 27 published. New: 320x200, 320x240 (`0133`),
400x300 (`0143`), 512x384 (`0153`) and 640x400 at 16 bpp; the same and
1152x864 at 8, 16 and 32 bpp as the BIOS lists them; 1600x1200x8.
1280x1024x16 and 1024x768x32 are hidden: this BIOS marks them
unsupported (attributes `009A`). `V9XSYNC.INI` added 17 modes to the
registry, `Status=ok`.

Open: the new sub-640 16-bpp rows carry S3 OEM numbers with no VESA
5:5:5 sibling, so they publish 5:6:5 while the baseline 16-bpp rows are
5:5:5 (`555-auto`, hardware S3D). Hardware Direct3D at one of those modes
would draw ZRGB1555 into a 5:6:5 surface. The BIOS lists 48 modes and
the ring-0 shape filter drops the 15-bpp ones, so whether it has 5:5:5
siblings for them is not recorded.

## Half-Life does not use them

HL's Direct3D video-mode list (`hl-d3d-video-modes.png`) offers 640x480,
800x600, 1024x768 and 1152x864 only: nothing below 640 for a hardware
renderer. Setting its saved mode to 512x384 in the registry did not
change what it selected; the value was put back to 640x480.

The refusal is also not the heap. With the desktop at 640x480x16 the
DirectDraw heap is 1.41 MiB (`GblHalVidMemTotal=0x16A000`), room for a
back and a Z surface, and HL still refuses. **Retracted:** "Z and Mixed
did not run: `0x88760231`" was read earlier as a failure; it is
`V9X_DDERR_UNSUPPORTED`, the probe's own not-run marker, and appears on
machines where HL runs.

Unmeasured hypothesis for the refusal: a texture format. The S3D path
enumerates 1555 and 4444 only (`TexFormatCount=2`, `TexFormat565=0`);
the Rage XL, which runs HL here, offers 565, and Incoming was recorded
refusing this chip for want of 565 or P8
(`docs/issues/2026-09-05-incoming-refuses-*.md`). Nothing has tested it.

## Disputed or unresolved

- **LFB address.** Retracted: the `0xDC000000` base was the Rage XL's.
  The driver maps the Trio3D's aperture from CR59/CR5A at `0xD8000000`.
  Nobody has looked at the monitor.
- The Trio3D/2X's two-state blend trap was not looked for on this card.
