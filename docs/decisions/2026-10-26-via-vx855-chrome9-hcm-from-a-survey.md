# VIA VX855 Chrome9 HCM from a DOS survey: framebuffer BAR0, engine BAR1, VBE 3.0 to 1920x1200, and the M1 2D engine Linux already drives

Date: 2026-10-26
Machine: none in the fleet. An embedded VIA VX855 board with a VIA C7 CPU;
nothing below has run on it or on any VIA graphics part.
Evidence: one V9XSURV report (build 39efca9e, schema 2, `query-only`,
`/rom`, 2026-10-25), committed unmodified as
[`via-vx855-chrome9-hcm-vgasurv-2026-10-25-39efca9e.ini`](../probe/references/via-vx855-chrome9-hcm-vgasurv-2026-10-25-39efca9e.ini).
It reached Michael as `VIA9HCM.INI`; who ran it is not recorded here.

Driver source checked: Linux `drivers/video/fbdev/via/` (`via-core.c`,
`hw.c`, `accel.c`, `accel.h`, `chip.h`, `viafbdev.c`), master, fetched
2026-10-26. The xf86-video-openchrome tree on gitlab.freedesktop.org
returned a sign-in page instead of raw files and was not read.

## What the machine is

| | Value | Report section |
|---|---|---|
| Display | VIA VX855/VX875 Chrome9 HCM IGP, `1106:5122` rev 00, subsystem `1106:5122` | `[PciDevice.0]` |
| BIOS | VIA "UEVNI90E", 07/17/09, "VX855 Embed NoTV", code rev 0151, VBE 3.0 | `[VideoBios]`, `[VBE]` |
| CPU | CentaurHauls, family 6 model 13 stepping 0 (C7): MTRR, PAT, SSE2 | `[Platform]` |
| North bridge | `1106:0409`-`7409`, VX855 host functions 0-7 | `[PciInventory]` |
| South side | `1106:8409` ISA, `C409` IDE, `3038` x3 UHCI, `3104` EHCI, `3288` HD Audio, `B353` PCI bridge | `[PciInventory]` |
| Network | `1106:3119` VT6122 Velocity Gigabit on bus 1, PXE ROM at CC80 | `[PciInventory]`, `[OptionRom.0]` |
| RAM | ~509 MiB usable below 512 MiB (E820 01 up to `1FED0000`) | `[Platform]` |
| OS at survey | DOS 7.10, no Windows running | `[System]` |

## Findings

1. **Framebuffer BAR0, engine BAR1.** BAR0 = `C0000008` (memory,
   prefetchable), BAR1 = `F0000000` (memory, not prefetchable). viafb maps
   exactly that on the VX855: `fbmem_start` from BAR0 (only the VX900 moves
   it to BAR2), `engine_mmio` from BAR1 (`via_pci_setup_mmio`). Every VBE
   mode's PhysBasePtr is `C0000000`. The BAR sizes were not measured.

2. **32 MiB of frame buffer, by VBE.** `TotalMemory64K=512`. viafb does not
   ask VBE: it reads the size from the host bridge's function 3
   (`1106:3409`) config dword A0h, bits 14:12 (3 = 32 MiB, up to 7 =
   512 MiB, "only on VX855/875"). The survey dumped only the display
   device's config space, so that register is not in the report.

3. **Where the 32 MiB comes from is not visible.** E820 shows RAM to
   `1FED0000`, ACPI tables and NVS just above, then `20000000`-`3FFFFFFF`
   (512 MiB) reserved. There is no 32 MiB hole below 512 MiB, so the
   frame buffer is not carved from the usable range. One reading is a
   1 GiB board whose BIOS reserves the upper half (with the frame buffer
   at its top) — that would also keep Windows 98 under its 512 MiB
   comfort limit. Unverified; the function 3 register above, or the
   board's manual, would settle it.

4. **VBE 3.0, 39 modes, 8/16/32 bpp to 1920x1200.** No 15 bpp and no
   24 bpp packed modes. Every mode is linear at `C0000000`. The pitch is
   width x bytes per pixel rounded up to 32 bytes: 1368x768 gives 1376
   (8 bpp) and 2752 (16 bpp); 1680x1050x8 gives 1696. A driver must take
   the pitch from the mode, not compute it. `Capabilities=00000001`: the
   DAC can switch to 8 bits per gun.

5. **No EDID.** The BIOS reports DDC1 only (`DdcLevel=01`) and the `4F15`
   read returned `014F`. `4F11` (flat panel) is unsupported. viafb lists
   CRT, DVP1, LVDS1 and LVDS2 as the VX855's outputs; which one this board
   drives is not in the report.

6. **DPMS `0710`:** VBE/PM 1.0, standby, suspend and off (no reduced-on).
   Protected-mode interface present (355 bytes).

7. **IRQ 10, bus master on** (`Command=0007`). Capabilities chain: PM at
   60h, then MSI at 90h, end. Config 70h holds a PCI Express capability
   header (ID 10h) that no pointer reaches.

8. **The ROM runs past its PCIR length.** The header says 97 blocks
   (49,664 bytes) and that whole image checksums to zero; the PCIR says
   32,768 bytes, over which the sum is `E6`. The usual pattern for a VGA
   BIOS that keeps runtime code beyond its PCI image after POST; not a
   fault. ROM BAR is 0, so the image is only readable at C000.

9. **The BIOS leaves its own clock and extended registers set.** In text
   mode Misc is `6F` (clock select 3, where a stock 80x25 uses 1),
   SR10 = `01`, and CR30-CR3F are populated. Read-only state, recorded for
   comparison when a VIA driver first sets a mode; no claim about what the
   bits mean is made here.

## The 2D engine (Linux viafb)

- The VX800, VX855 and VX900 use the **M1** engine
  (`twod_engine = VIA_2D_ENG_M1`, `hw.c`), driven by `hw_bitblt_2`; the
  older UniChrome parts use H2/H5 and `hw_bitblt_1`.
- M1 registers sit at the start of BAR1 (`accel.h`): GECMD 000h,
  GEMODE/GESTATUS 004h, PITCH 008h, DIMENSION 00Ch, DSTPOS 010h, DSTBASE
  014h, SRCPOS 018h, SRCBASE 01Ch, ... CLIPTL 040h, CLIPBR 044h,
  KEYCONTROL 048h. viafb clears 000h-05Ch to reset it.
- Commands go through a virtual queue (256 KiB of VRAM, `VQ_SIZE`) set up
  with `VIA_REG_CR_TRANSET`/`TRANSPACE` writes; viafb also takes 8 KiB for
  the cursor. Both come off the top of VRAM.
- viafb uses only fills, copies and mono expansion with ROPs. The M1 has a
  colour-key control register, so hardware DirectDraw colour keys look
  possible, but nothing here shows them working.

## Survey tool notes

- `[OptionRom.0] PcirClassCode=000002` for the VT6122 PXE ROM, where the
  NIC's class is `020000`. The same build read the display ROM's class
  correctly (`030000`, bytes `00 00 03` at PCIR+0Dh), so the NIC ROM most
  likely stores its class bytes in the other order. Its bytes were not
  dumped (`DumpScope=none`), so this is unconfirmed. Display-irrelevant.
- `[Chipset] Status=unsupported` (no port-level probe for VIA). Adding the
  host bridge's function 3 config dump to the survey would answer finding 2
  without any writes.
- `String.04`-`String.61` are the BIOS fonts and tables read as text.

## What it means for Velocity9x

- No VIA family exists in the tree. A `via` family would cover every VIA
  IGP, with the chip generations (H2/H5/M1 engines, BAR layouts — the VX900
  moves the frame buffer to BAR2) as hw16 data.
- Tier 0 is plain VBE 3.0 with a linear BAR and an 8-bit DAC, up to
  1920x1200x32. The C7 has MTRRs and PAT, so the `C0000000` aperture can
  be write-combined the way the Intel parts now are.
- DirectDraw blits would be the M1 engine through BAR1, with the virtual
  queue to set up first. That is more than the NeoMagic or S3 engines need,
  and only viafb's use of it is known to work.
- 3D: the Chrome9 HCM has a DirectX 9-class engine, but no open driver in
  the sources read here programs it. Direct3D, OpenGL and Glide would start
  on the software paths.
- No work starts on this without someone who owns the board running test
  builds.
