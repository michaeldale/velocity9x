# The GM45's GMA 4500MHD (8086:2A42, GL40 in an HP t5740) joins intel-gma on one DOS survey: the G45's path unchanged

Date: 2026-11-04
Machine: no GM45, GS45 or GL40 is in the fleet and nothing below has run on one.
Evidence: the V9XSURV report attached to GitHub issue 22 by its reporter
(an HP t5740 thin client), committed unmodified as
[`hp-t5740-gl40-vgasurv-2026-11-04-d9d7aeb9.ini`](../probe/references/hp-t5740-gl40-vgasurv-2026-11-04-d9d7aeb9.ini).
Survey build `d9d7aeb9` (0.34.0), run from DOS 7.10 with `/rom`.

## Why

The G45 and Q45 went in blind from two surveys
([`2026-10-23-g4x-gma4500-from-surveys.md`](2026-10-23-g4x-gma4500-from-surveys.md)),
and issue 3's reporter ran Direct3D, OpenGL and Glide on both with 0.28.0
(3DMark 2000 1942 and 1760). The GM45 is the mobile part of the same
generation (Cantiga beside the desktop Eaglelake). A test build costs one
descriptor, and the reporter's first boot is the only measurement available.

## What the survey establishes

| | GL40 (t5740) | G45 (Dell) | Q45 (HP) |
|---|---|---|---|
| Function 0 | `8086:2A42` rev 09 | `8086:2E22` rev 03 | `8086:2E12` rev 03 |
| Function 1 | `8086:2A43`, BAR0 `FEA00004` | | |
| BAR0 (GTTMMADR) | `FE400004`, 64-bit | `FE400004` | `F0000004` |
| BAR2 (GMADR) | `D000000C`, 64-bit prefetchable | `D000000C` | `E000000C` |
| BAR1, BAR3 (high dwords) | 0, 0 | 0, 0 | 0, 0 |
| BAR4 | `C081` (I/O) | | |
| GGC (52h) | `0370` | `0350` | `0B50` |
| BSM (5Ch) | `7C000000` | `CE000000` | `DE000000` |
| VBE TotalMemory64K | 1023 | 511 | 511 |
| VBE OEM string | "Intel(R)Cantiga Graphics Chip Accelerated VGA BIOS" | "...Eaglelake..." | same |

- The BARs have the G45's form, which `V9xPciReadIntelGttConfig`'s Gen4
  branch already validates: both 64-bit with zero high dwords, BAR2 256 MiB
  aligned. BAR0 is 4 MiB aligned (`FE400000`), which the + 2 MiB branch
  requires. Function 1's BAR0 at `FEA00000` is consistent with a 4 MiB
  BAR0 but does not prove it: a 1 MiB BAR would fit there too.
- GMS (bits 7:4) is 7, 64 MiB in `intel_gtt16.c`'s table. VBE's 1023 x
  64 KiB (64 MiB less the 64 KiB the BIOS keeps) agrees. The driver maps 16
  MiB and clamps to it (`v9x_vram_usable_bytes`), as for Ironlake's 128.
- GGMS (bits 11:8) is 3, a 2 MiB GTT without VT-d, as on the Dell. E820
  reports usable RAM to `7BCA0000`, then ACPI and reserved ranges to
  `7BE00000`, and nothing from there to BSM at `7C000000`: 2 MiB, the GTT's
  stolen size, directly below the 64 MiB of graphics memory, which ends at
  2 GiB.
- The CPU is an Atom (signature `106C2`, family 6 model 28).

## The GTT: + 2 MiB, as the G45

Linux `intel-gtt.c` lists the GM45's IGD id with the same G4x GTT driver
as the G45 and Q45, which `i9xx_setup()` sends to the + 2 MiB offset. That
is from memory of the source the G45 record read; it was not re-read for
this record. If the GL40's first boot reports `CONFIG-FAILED` or a GTT
that does not run linearly from BSM in `INTELGTT.TXT`, this is the first
place to look.

`2A42` joins `2E22` and `2E12` at both comparisons in
`V9xPciReadIntelGttConfig`.

## The display and the 3D: the G45's

The descriptor uses `v9x_g4x_fill_engine`, so it stamps `I9XX_GEN4` and
`I9XX_G4X`. The display is the 965's at 70000h with no PCH, as on the
GM965 (also a mobile part, measured) and the G45; the 3D encodings are
G4x's (PIPELINE_SELECT `69040000`, the 6-dword depth buffer), which the
G45 and Q45 have now run. Direct3D and OpenGL name it "Velocity9x GMA
4500", the identity bit's name.

## Modes

The BIOS describes 0160-0162 as 1280x800 and fills 0163-016E with 848x480,
1360x768, 1366x768 and 1440x900; 016F-0171 have attributes 0000. TOMMOHP's
mobile BIOS also reuses 0160-0165 for its own modes, and the runtime scan
publishes what the BIOS has, so the family's table stays the GM965's.

The EDID names a 1920x1080 monitor ("VX2470T") as preferred. The BIOS
lists no 1920x1080 mode, so the reporter's monitor will not run at its
native resolution unless the BIOS sets one the survey did not see.

## The descriptor did not fit in _TEXT

The descriptor, its strings and the two assembly comparisons put the
intel-gma driver's `_TEXT` at 57,439 bytes, 95 over the audit's 57,344
budget; `I9XXCODE` was full too. `hsw_config.c` (the Haswell layout
arithmetic, called only from `hsw_hw16.c`) moved to a third segment,
`HSWCODE`, with its seven calls declared far in `intel_hsw.h` and held by
check-tree to `intel16.h`'s rules. `_TEXT` is now 55,943 bytes and
`HSWCODE` 1,505.

Measured on the i5-4570 (HD 4600) the same day: main `366bd0e3`, built
clean, its eight intel-gma files deployed by WININIT rename, `reset` from
boot 172 (0.34.0) to 173, all eight hashes as built. `V9XHW.INI`, which
`hsw_hw16.c` writes from the three far calls it makes into `HSWCODE`
(`decode_ggc`, `protected_bytes`, `plan_heap`), is byte-identical to boot
172's: `HeapLayout=chip`, `StolenBytes=02000000`, `ProtectedBytes=00100000`,
`ReserveOffset` and `HeapEnd` `01EA0000`. V9XD32 PASS, V9XGLP PASS,
V9XGLIDP PASS, V9XR3DP COMPLETE
([evidence](../probe/hsw-4570-hswcode-2026-11-04/)). The HD 4400 and HD
4000, which share the code, were not run; no game was run.

## What is untested

All of it. The gates show the descriptor, manifest, registry and the two
assembly comparisons agree; they do not show that the GM45's GTT is at
BAR0 + 2 MiB, or that the mobile Cantiga takes the G45's 3D batches. The
reporter's first boot is the measurement.
