# Ironlake: the heap to the BIOS's 128 MiB, and the settings page's blit line

Date: 2026-11-28. Source: GitHub issue 28, an Acer Aspire 4740 (Ironlake
mobile, `8086:0046`) running 0.56.2, whose owner sent `V9XHW.INI`,
`V9XBOOT.INI`, `INTELGTT.TXT`, two 3DMark 99 results and a screenshot of
the Velocity9x tab. Both changes answer what that reply promised. Neither
has run on an Ironlake: the desktop Ironlake (W9QI2F4F) has had a Quadro
in it since 2026-11-05.

## 1. The heap stopped at 16 MiB

`V9XBOOT.INI` shows `DpmiMap=01000000:ok` and `VddReserve ... vram=16777216`:
the family maps 16 MiB of GMADR (`intel_hw16.c`'s `map_pages`), and
`v9x_vram_usable_bytes` clamps the heap to the mapping. VBE 4F00h reports
134,152,192 bytes (`VbeVramBytes=`), the BIOS's 128 MiB (GGC `0380`) less
64 KiB. At 1366x768x32 three screen buffers and a 32-bit depth buffer take
about 16 MiB, so textures had next to none, which fits the owner's
"1366x768x32 runs really bad".

What already went past 16 MiB: the engine reserve. `v9x_gma950_ring_reserve`
places it from the VBE figure, `V9X_I9XX_GTT_RESERVE_BYTES` (1 MiB) below
it, at aperture `7EF0000h` on this laptop, and the ring has run there
(`EngineStamp ... ring=E6A78000`, Direct3D drawing in 3DMark 99). So the
GPU reaches that address and the mini-VDD writes it through its own
mapping.

`INTELGTT.TXT` (read-only capture) shows GTT entries 0 to 7FFEh linear
from BSM `98000000`, page for page; entry 7FFFh points elsewhere. The heap
and reserve end below 7FFFh.

**Change:** both Ironlake rows (`0042`, `0046`) set `map_bytes` to 128 MiB
(`gma950_hw16.c`), as the HD 4600 sets 64 MiB
([plan](../plans/intel-haswell-heap-beyond-16-mib.md)). The heap then runs
from the visible bytes to the reserve, about 127 MiB here. A smaller VBE
figure still bounds it: the reserve, and so the heap's end, comes from VBE.

The HD 4600 found its top megabyte of stolen memory unwritable through the
aperture; that megabyte was inside its VBE figure. Here the reserve sits in
the top megabyte and works, which is the evidence that it is reachable; the
CPU writing the heap just below it is not measured. Nothing in the Gen3 or
Gen4/5 HAL paths assumes a heap under 16 MiB (searched `eng_i9xx.c`,
`i9xx_*.c`, `d3d_i9xx.c`, `d3d_g4.c`, `g4_3d.c`); the GMA 950's texture
pool already places textures above 40 MiB on the Gen3 engine.

The laptop's BIOS uses all eight MTRR pairs, so its aperture is
write-combined through the page attribute table, which has no size limit
in `v9x_pat_plan_wc`; the HD 4600 maps 64 MiB the same way on testers'
machines.

## 2. "CPU fill and copy" on the settings page

The screenshot reads "DirectDraw: CPU fill and copy, page flip". The page
built that from the `EngineStamp=` caps' `SOLID_FILL` and `SCREEN_COPY`
bits. The Gen3-type descriptors (GMA 950, 900, 3150, 3100, X3100, the G4x
parts and Ironlake) set neither: the HAL routes their fills and copies by
engine type to `eng_i9xx.c`, which uses the ring whenever it is up
(`v9x_i9xx_engine_ready`) and the CPU otherwise. This laptop's stamp,
`type=00000003 caps=00102414 ring=E6A78000`, has a ring.

**Change:** `settings_status.c` reads `type=` and `ring=` from the stamp
and reports engine fill and copy for type 3 with a non-zero ring. The bits
are not set in the descriptors instead, because `gdi_accel.c` reads the
same bits to turn on GDI acceleration, which these chips do not have.
A `check-tree.ps1` assertion was added first and seen failing.

## Not done

- The panel's 1366x768 still needs the owner's BIOS patch: the BIOS lists
  no panel mode ([issue](../issues/2026-11-06-intel-vbios-mode-table-lacks-panel-modes.md)).
- Ask the owner, on 1.1.0: `V9XBOOT.INI` (`DpmiMap=08000000:ok`,
  `VddReserve vram=`), `V9XHW.INI`, the page's DirectDraw line, and
  3DMark 99 at 1366x768x32 again.
