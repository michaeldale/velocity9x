# The G33/G31's GMA 3100 (8086:29C2) joins intel-gma on one DOS survey: the 945's descriptor unchanged

Date: 2026-10-26
Machine: no G31/G33 part is in the fleet and nothing below has run on one.
Evidence: one V9XSURV report from an outside submitter, build `8804962d`,
committed unmodified:
[`g31-ich7-gma3100-vgasurv-2026-10-26-8804962d.ini`](../probe/references/g31-ich7-gma3100-vgasurv-2026-10-26-8804962d.ini).

References: Linux 6.6 `intel-gtt.c` (G33 GTT setup), i915's watermark
selection, Mesa gallium `i915_screen.c` (`is_i945` for `PCI_CHIP_G33_G`).

## Which chip it is

The IGD is `8086:29C2` rev 10, an id the G33 and G31 share. The ROM says
"Intel(r)Q33/Q35/G33 Graphics Chip Accelerated VGA BIOS", build 1508 of
06/08/2007, VBT `BEARLAKE-B`. The host bridge is `8086:29C0`
(G33/G31/P35/P31) and the south bridge ICH7 (`27B8` LPC, `27C0` SATA). G33
boards pair with ICH9 and G31 boards with ICH7, so this is most likely a
G31. The driver cannot tell the two apart and does not need to: one id,
one graphics core. CPU signature `000006FD` (Core 2 / Pentium Dual-Core).

## What the survey establishes

| | This board | GMA 950 (945GSE) | GMA 3150 (Pineview) |
|---|---|---|---|
| BAR0 (MMADR) | `FF900000`, 32-bit | 32-bit, 512 KiB | 32-bit |
| BAR1 (I/O) | `F141` | I/O | I/O |
| BAR2 (GMADR) | `D0000008`, 32-bit prefetchable | `D0000008` | 32-bit |
| BAR3 (GTTADR) | `FF700000` | 32-bit | 32-bit |
| GGC (52h) | `0130` | | |
| BSM (5Ch) | `CF800000` | | |
| ASLS (FCh) | `CF3CE010` | | |
| VBE TotalMemory64K | 127 (`0x7F0000`) | `0x7B0000` | `0x7F0000` |

- The BAR layout is the 945's: registers in BAR0, aperture in BAR2, GTT in
  BAR3, all 32-bit. `V9xPciReadIntelGttConfig`'s Gen3 arm accepts every
  one as it stands (BAR2 low bits 000b, 256 MiB aligned; BAR3 256 KiB
  aligned, below `FFFC0000`), so no assembly changes.
- GGC: GMS (bits 7:4) is 3, 8 MiB in the table `intel_gtt16.c` already
  carries; GGMS (bits 9:8) is 1, a 1 MiB GTT. The driver reads the first
  256 KiB, which maps the 256 MiB aperture.
- BSM `CF800000` plus 8 MiB ends at `D0000000`; VBE reports 8 MiB less the
  64 KiB the BIOS keeps, the same `0x7F0000` as Pineview, so the ring
  reserve lands where it does on TOMMO33. Usable RAM in E820 ends at
  `CF600000`; the 2 MiB between is presumably the GTT and TSEG, which the
  survey does not show.
- ASLS points into the ACPI NVS range E820 reports at `CF3C1000`.

## Why the 945's descriptor unchanged

- 3D: Mesa's gallium i915 sets `is_i945` for `PCI_CHIP_G33_G`, so the mip
  layout is the 945's, not the 915's stacked one (`I9XX_MIP_STACKED` stays
  clear).
- Watermarks: i915 programs G33 through `i9xx_update_wm`, which writes
  FW_BLC as on the 945. Only Pineview takes `pnv_update_wm`, so
  `I9XX_NO_FW_BLC` stays clear.
- GTT entries: Linux writes G33's with `i965_write_entry`, which differs
  from the 945's only in physical address bits 35:32. Windows 98 has none
  above 4 GiB.

So `v9x_g33_gma3100_device` uses `v9x_gma950_fill_engine` directly, with
write-combining as the rest of Gen3.

## What else the report shows

- Mode `0103h` (800x600x8) has a pitch of 832, not 800; every other mode's
  pitch is width x bytes per pixel. Not checked here whether the runtime
  VBE scan offers that mode or how it takes its pitch.
- No 24 bpp modes; 1920x1440 at 8 and 16 bpp only. `0160h`-`0168h` and
  `015Ch` are listed with attributes 0, as on the G4x BIOSes.
- EDID: `DdcLevel=00` and 4F15h returned `014F`. Not known whether that is
  the BIOS or the monitor.
- The shadowed ROM is 45 KiB against a PCIR length of 48 KiB, and its
  checksum does not balance; Intel BIOSes trim their shadow after POST.
- `[Chipset]` and `[Aperture]` are empty (no Intel port probe; no
  aperture switch), so nothing here was read from MMIO.

## Not measured

All of it on silicon: the ring opening at the reserve, blits, flips,
Direct3D, and whether FW_BLC as the 945 programs it suits a desktop pipe
at the BIOS's modes. If a boot fails, the engine status words in
V9XHW.INI and the Intel GTT inventory say which step stopped. The Q35 (`29B2`) and Q33 (`29D2`) are the same core and are not
bound: no survey of either.
