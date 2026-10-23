# The G45 and Q45 (GMA X4500, GMA 4500) join the intel-gma family on two DOS surveys: the GM965's path with Ironlake's GTT offset and two G4x 3D encodings

Date: 2026-10-23
Machine: no G4x part is in the fleet and nothing below has run on one.
Evidence: two V9XSURV reports attached to GitHub issue 3 by its reporter,
committed unmodified on 2026-10-08 with the GMA 900's
([`2026-10-08-intel-915gm-gma900-support.md`](2026-10-08-intel-915gm-gma900-support.md)):

- [`dell-inspiron-560-g45-vgasurv-2026-10-07.ini`](../probe/references/dell-inspiron-560-g45-vgasurv-2026-10-07.ini)
  - G45/G43, GMA X4500, `8086:2E22`.
- [`hp-8000-elite-sff-q45-vgasurv-2026-10-07.ini`](../probe/references/hp-8000-elite-sff-q45-vgasurv-2026-10-07.ini)
  - Q45/Q43, GMA 4500, `8086:2E12`.

The manuals: the G45 PRM, four volumes (Intel, 2008), which cover the 965
([DevBW], [DevCL]) and the G4x parts ([DevCTG], [DevEL]) together. Linux 6.6
`intel-gtt.c`, Mesa 24.0.9 (`brw_inst.h`, genxml `gen45.xml`, crocus) and
xf86-video-intel `i965_render.c` where the PRM is silent or disagrees.

## Why

Issue 3 asks for 3D on these two desktops. The GMA 900 from the same issue
was shipped from its survey alone and worked. The GM965 (Gen4) and Ironlake
(Gen5) now draw Direct3D on the Gen4 engine (`d3d_g4.c`), and G4x sits
between them: Gen4's display and most of Gen4's 3D encodings, with a few of
Gen5's. A test build for the reporter costs little and is the only way any
of it gets measured.

## What the surveys establish

| | Dell (G45) | HP (Q45) | GM965 (TOMMOHP) | Ironlake (W9QI2F4F) |
|---|---|---|---|---|
| Function 0 | `8086:2E22` rev 03 | `8086:2E12` rev 03 | `8086:2A02` | `8086:0042` |
| BAR0 (GTTMMADR) | `FE400004` | `F0000004` | 64-bit, 1 MiB | 64-bit, 4 MiB |
| BAR2 (GMADR) | `D000000C` | `E000000C` | 64-bit pref. | 64-bit pref. |
| BAR1, BAR3 (high dwords) | 0, 0 | 0, 0 | 0, 0 | 0, 0 |
| GGC (52h) | `0350` | `0B50` | | |
| BSM (5Ch) | `CE000000` | `DE000000` | | |
| VBE TotalMemory64K | 511 | 511 | | |
| VBE OEM string | "Intel(r)Eaglelake Graphics Chip Accelerated VGA BIOS" | same | | |

- BAR0 is 64-bit memory (bits 2:1 = 10b), 4 MiB aligned; the function 1
  BAR0s sit at `FE900000` and `F0400000`, so the 4 MiB is real, as on
  Ironlake. BAR2 is 64-bit prefetchable, 256 MiB aligned. There is no
  GTTADR BAR: the GTT is inside BAR0, as on the GM965.
- GGC's GMS (bits 7:4) is 5 on both, 32 MiB in the table
  `intel_gtt16.c` already uses. BSM, the E820 reserved range below it and
  VBE's 511 x 64 KiB (32 MiB less the 64 KiB the BIOS keeps) agree.
- GGMS (bits 11:8) is 3 on the Dell and Bh on the HP. Linux decodes Bh as
  the VT-d form, a 2 MiB GTT (`G4x_GMCH_SIZE_VT_2M`); 3 is 2 MiB without
  VT. Either way the driver reads only the first 256 KiB, which maps the
  256 MiB aperture.
- The BIOS sets no 1024x576 mode (0160-0168 have attributes 0000). The
  runtime scan publishes what the BIOS has; the family's table is the
  family's, as on TOMMOHP.

## The GTT: GTTMMADR + 2 MiB, as Ironlake

The G45 PRM does not give the GTT's place in BAR0 in the volumes here.
Linux `intel-gtt.c` `i9xx_setup()` puts it at `reg_addr + MB(2)` for
`IS_G4X` as for Gen5, and at `+ KB(512)` only for the 965. The BAR's 4 MiB
size, which the function 1 BAR placement confirms, leaves no room for
anything else: 2 MiB of registers, then 2 MiB of GTT, the GGMS size.

`V9xPciReadIntelGttConfig` therefore takes `2E22` and `2E12` down its Gen4
branch with the Gen5 offset and the 4 MiB alignment check. The mini-VDD's
GTT mapping already accepts BAR0 + 2 MiB for Ironlake and needs no change.

The 32 MiB VBE figure is above the 16 MiB the family maps. Since the
Ironlake fix, the GTT inventory and the ring placement are bounded by
`v9x_vbe_vram_bytes`, which `v9x_vram_usable_bytes` clamps to the mapped
window; that covers 32 MiB as it covered Ironlake's 128.

## The display: the GM965's

G4x has no PCH; the display block is the 965's at 70000h, with DSPSURF and
DSPLINOFF and the Gen3 ISR. The GM965's register flip, ring flip and FW_BLC
omission apply unchanged, so the descriptor stamps `I9XX_GEN4` as the
GM965's does, and not `I9XX_GEN5`, whose register omissions are Ironlake's.

## The 3D: Gen4 with two Gen5 encodings

Every command `g4_3d.c` emits, checked against the G45 PRM:

| Command | 965 (Gen4) | G4x | Ironlake (Gen5) | Source for G4x |
|---|---|---|---|---|
| PIPELINE_SELECT | `61040000` | `69040000` (chosen, below) | `69040000` | Mesa, xf86 |
| STATE_BASE_ADDRESS | 6 dwords | 6 dwords | 8 dwords | PRM Vol 1a 3.6.1, p43: no Instruction Base Address |
| 3DSTATE_DEPTH_BUFFER | 5 dwords | 6 dwords | 6 dwords | PRM Vol 2 8.4.4.1, p272-279: DW5 Depth Coordinate Offset, [DevCTG+] |
| VERTEX_BUFFER_STATE DW3 | Max Index | Max Index | End Address | PRM Vol 2 3.4.2, p90 |
| VERTEX_ELEMENT_STATE | Destination Element Offset | Destination Element Offset | MBZ | PRM Vol 2 3.5.2, p95 |
| VS_STATE URB entries | count | count | count / 4 | unchanged from the 965 in Vol 2; Mesa divides only for Gen5 |
| WM_STATE | 8 dwords | 8 dwords | 11 dwords | PRM Vol 2 8.2.1, p245 |
| Sampler send descriptor | type 15:14, format 13:12 | type 15:12, no format | Gen5 layout | PRM Vol 4 4.8.1.1.2 |
| EU send descriptor | Gen4 | Gen4 | Gen5 | Mesa `brw_inst.h` 4.5 column |

The sampler descriptor's change costs nothing here: the kernels send type 0
with return format 0 (FLOAT32), which is bits 15:12 = 0 in both layouts, and
Vol 4 4.8.1.3 makes type 0 with message length 5 the SIMD16 `sample`.

Two G4x notes in the PRM that this build reads and does not act on:

- WM_STATE's Depth Coefficient and Setup URB Read Offsets "[DevCTG+]: ...
  programmed according to the transposed (attribute-major) layout,
  regardless of the setting of Transposed URB Read Enable" (Vol 2 8.2.1).
  The offsets here are 1 and 0, crocus's for every Gen4 part including
  G4x; the SF kernel writes the layout the GM965 draws with. If G4x draws
  with Z or attributes wrong, this is the first place to look.
- The URB is 384 rows on G4x (Vol 1a 3.4), not 256. The 965's fences fit
  inside it; using less of the URB is legal.

### PIPELINE_SELECT: the conflict, and 0x6904

The G45 PRM Vol 1a 3.3 (and the 3.10.1 table) gives PIPELINE_SELECT as
"GFXPIPE[28:27 = 0h, 26:24 = 1h, 23:16 = 04h]", the 965's `61040000`, with
no device tag.

Mesa's i965 uses `CMD_PIPELINE_SELECT_GM45` (`69040000`, sub-type 1) when
`is_g4x`, and the 965 value only for the original 965. xf86-video-intel's
`i965_render.c` sends `NEW_PIPELINE_SELECT` (the same `69040000`) under
`IS_G4X || IS_GEN5`. Both drivers drew on G4x hardware for years. The PRM
page reads as one carried over from the 965 manual; that is an inference,
not something the PRM says.

`69040000` is chosen. The evidence for it is two drivers that ran on the
silicon; the evidence for `61040000` is one table that tags nothing in it for
G4x. What happens if the choice is wrong was not looked up and cannot be
measured here: a G4x boot that hangs at, or refuses, its first 3D batch is
the symptom to expect, and the other value is the first thing to try.

## The G4x encodings need the HAL to know it is on G4x

The HAL chooses `d3d_g4.c`'s encodings from the engine caps: `I9XX_GEN5` for
Ironlake, Gen4 otherwise. G4x stamps `I9XX_GEN4` for its display, so the
HAL cannot tell it from a GM965 without something new in the descriptor.
Building Gen4 batches for G4x is not an option: a 5-dword
3DSTATE_DEPTH_BUFFER where 6 are expected runs the next command's header
as DW5.

The pure encoding is in `g4_3d.c` as a third generation value, `V9X_G4_GEN45`,
host-tested. How the HAL learns to choose it is a change to
`include\velocity9x\engine_abi.h`. Michael chose A on 2026-10-23:

- **A (chosen):** a chip identity bit, `V9X_DD_ENGINE_CAP_I9XX_G4X`
  (`0x00400000`), stamped beside `I9XX_GEN4` by the G4x descriptors only,
  read by `v9x_d3d_g4_gen()`. Same form as `I9XX_GEN4`, `I9XX_GEN5` and
  `GEN7_IVB`, which were added without an ABI version change.
- B: carry the PCI device id to the HAL and decide there. A new descriptor
  field, and chip policy in display code.
- C: stamp both `I9XX_GEN4` and `I9XX_GEN5`. No new bit, but the scanout's
  Ironlake register omissions would then apply to a part that has those
  registers. Rejected.

## What is untested

All of it. No G4x part has run any of this. The gates (check-tree,
build-host, run-checks) show the code builds, the batch has the lengths the
PRM gives, and the manifests agree; they do not show that the GTT is where
Linux says, that `69040000` is accepted, or that the transposed-offset note
is harmless. The reporter's first boot is the measurement.
