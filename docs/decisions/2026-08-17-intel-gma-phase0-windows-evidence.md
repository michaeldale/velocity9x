# intel-gma Phase 0 evidence - the Windows half, measured

Provenance: written on the `intel-gma-tier0` branch, whose family scaffolding
was never merged (archived at tag `archive/intel-gma-tier0`); salvaged to main
2026-08-28 per docs\plans\family-structure-and-next-d3d-roadmap.md Track A4.
Code paths named here (src\chipsets\intel\..., intel_gma.h,
scripts\lib\vbe-cache.ps1) exist only in that archive. The measured evidence
stands regardless; the netbook is meanwhile served by the vbe family
(docs\issues\2026-08-27-netbook-gma950-findings.md).

Status: recorded, 2026-08-17
Scope: closes the Windows-half items in the Gen3 hardware audit's unmeasured
list (`2026-08-17-intel-gma-gen3-hardware-audit.md` Â§10); the DOS half (VBE
mode list, GGC/BSM, VBIOS image) remains open.

## How it was captured

Remotely, through the Bluetrait RMM (agent 1036, MICHAEL-NETBOOK), as task 303
"Velocity9x intel-gma Phase 0 evidence capture (read-only)" - the query-only
registry/WMI script `scripts\capture-intel-gma-evidence.ps1` encodes the same
reads for console use. Run 2026-08-17 20:14 on Windows 10 Pro 32-bit 19042
with the stock WDDM 1.0 driver 8.15.10.2697 active. Nothing on the machine was
changed.

## PCI identity - the INF claim is confirmed

| Function | Hardware id | Notes |
|---|---|---|
| IGD (fn 0) | `PCI\VEN_8086&DEV_27AE&SUBSYS_308F103C&REV_03` | **exactly the id the family claims**; subsystem 308F 103C = HP |
| Second display (fn 1) | `PCI\VEN_8086&DEV_27A6&SUBSYS_308F103C&REV_03` | class 0380 as predicted; ignored by the driver, recorded so Device Manager's second entry is a documented fact |
| Host bridge | `VEN_8086&DEV_27AC&SUBSYS_308F103C&REV_03` | the 945GME/GSE bridge - config 0x52 here is where the DOS survey reads GGC |

## BAR layout (from allocated resources; DOS survey will read the raw BARs)

| Resource | Address | Reading |
|---|---|---|
| Memory | 0xFEA80000 | BAR0 MMADR (512 KiB register file) |
| Memory | 0xD0000000 | BAR2 GMADR aperture (256 MiB region) |
| Memory | 0xFEA40000 | BAR3 GTTADR (the Gen3 separate GTT BAR) |
| I/O | 0xDC80 | BAR1 I/O pair |
| Memory | 0x000A0000 | VGA legacy window |
| I/O | 0x3B0 / 0x3C0 | VGA legacy ports |

Textbook Gen3 shape, matching audit Â§1. Expect VBE 4F01h PhysBasePtr to land
at 0xD0000000; if the DOS survey disagrees, that is a stage-3 finding.
AdapterRAM=268435456 (256 MiB) is the WDDM driver's dynamic UMA view, not
stolen memory - a fourth "how much memory" number for audit Â§3's list, and
another reason only GGC and 4F00h matter to us.

## Panel EDID - the native-mode target, decoded

Panel: AU Optronics **B101AW01 V2** (EDID vendor 06AF, product 12D1). Raw EDID
preserved in the task 303 result and reproducible any time with the capture
script. The single detailed timing descriptor decodes to:

| Fact | Value |
|---|---|
| Native mode | 1024 x 576 @ 60.01 Hz |
| Pixel clock | **54.20 MHz** |
| Horizontal | active 1024, blank 320 (total **1344**), sync offset 48, sync width 32 |
| Vertical | active 576, blank 96 (total **672**), sync offset 4, sync width 4 |

Cross-check: 54.2 MHz / (1344 x 672) = 60.01 Hz. These are the timings the
915resolution-style BT_3 patch writes and the eventual native DPLL targets
(54.2 MHz is comfortably a single-channel LVDS clock, p2 = 14 domain).

The current WDDM desktop runs 1024x576 natively - confirming the panel and
that only the VBIOS's VBE table, not the hardware, stands between tier-0 and
native resolution.

## Still open (needs the DOS boot)

Full VBE mode list with per-mode LFB attributes (gates the provisional
manifest mode table), GGC and BSM raw values, whether the VBIOS honours 4F02h
bit 14 from real DOS, the VBIOS product string and image. All gathered by
`v9xintl.exe` (`scripts\build-intel-survey.ps1`).
