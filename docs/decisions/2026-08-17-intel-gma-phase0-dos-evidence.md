# intel-gma Phase 0 evidence - the DOS half, measured

Provenance: written on the `intel-gma-tier0` branch, whose family scaffolding
was never merged (archived at tag `archive/intel-gma-tier0`); salvaged to main
2026-08-28 per docs\plans\family-structure-and-next-d3d-roadmap.md Track A4.
Code paths named here (src\chipsets\intel\..., intel_gma.h,
scripts\lib\vbe-cache.ps1) exist only in that archive. The measured evidence
stands regardless; the netbook is meanwhile served by the vbe family
(docs\issues\2026-08-27-netbook-gma950-findings.md).

Status: accepted, 2026-08-17. **Phase 0 is complete**; two findings changed the
family's declared mode table and removed a planned phase.
Scope: closes the remaining unmeasured items in
`2026-08-17-intel-gma-gen3-hardware-audit.md` Â§10; pairs with
`2026-08-17-intel-gma-phase0-windows-evidence.md`.

## How it was captured

`tools\diag\intel_survey_dos.c` (built as `v9xintl.exe` by
`scripts\build-intel-survey.ps1`), run from **real DOS** on the netbook -
not a DOS box, per the ati family's measurement that a DOS box under a running
Windows returns artefacts. Query-only. Artefacts: `V9XINTL.TXT` (the report)
and `V9XVBIOS.BIN` (the 64 KiB VBIOS shadow), retained under
`build\intel-survey\` (untracked, like every other raw capture).

Build id of the surveying binary: `d511a6b-dirty`.

## Finding 1: the panel's native mode is a VBE mode. The plan was wrong.

**1024x576 is published by this VBIOS as Intel OEM mode numbers**, with a
linear framebuffer:

| Mode | Geometry | Attributes | Pitch | PhysBasePtr |
|---|---|---|---|---|
| **0x0160** | 1024x576x8 (packed, model 4) | 009B | 1024 | D0000000 |
| **0x0161** | 1024x576x16 (direct, model 6) | 009B | 2048 | D0000000 |
| 0x0162 | 1024x576x32 | 009B | 4096 | D0000000 |

Attributes 0x009B = supported | colour | graphics | **linear framebuffer
available**. The 32-bpp row is out of project scope (the INF audit forbids
`MODES\32`) and is recorded only for completeness.

**The classic 915resolution problem does not apply to this VBIOS.** The audit's
Â§4 assumed - on the strength of universal platform reports - that the native
panel mode would be absent and every mode would arrive panel-fitter-scaled.
On this HP VBIOS it is present, and tier-0 therefore reaches the panel's native
resolution **pixel-exact, with no mode-table patching at all**.

Consequences:

- The user decision of 2026-08-17 ("panel-fitter-scaled modes are acceptable
  for the first release, native 1024x576 is a later phase") is satisfied
  without the later phase. The gate is met by a better outcome than it asked
  for.
- Native-mode roadmap **option A is deleted**, not deferred: no PAM unlock, no
  BT_3 table patch in the mini-VDD, no chipset config write anywhere in the
  plan. That was the single riskiest item in the native roadmap and the only
  one that wrote outside the IGD.
- Erratum 8 in the audit (PAM shadow writes) no longer applies to this family.
- The remaining native-modeset case (option B) is now purely about capability -
  arbitrary refresh, no VBIOS dependency - not about reaching the panel at all.

## Finding 2: this VBIOS refuses every mode taller than the panel

The VBIOS lists **36 mode numbers** in `VideoModePtr` but marks most of them
unsupported - 4F01h returns success with **Attributes 0000** (mode-supported
bit clear) and a zeroed block. The pattern is exact: **every mode whose height
exceeds the panel's 576 lines is refused.**

Supported (6): 0x160/0x161/0x162 (1024x576 at 8/16/32), 0x101 (640x480x8, pitch
640), 0x111 (640x480x16, pitch 1280), 0x112 (640x480x32, pitch 2560).

Refused (30), including every row the provisional manifest assumed:

| Mode | Geometry | Why |
|---|---|---|
| 0x103 / 0x114 / 0x115 | 800x600 at 8/16/32 | 600 > 576 |
| 0x105 / 0x117 / 0x118 | 1024x768 at 8/16/32 | 768 > 576 |
| 0x107 / 0x11A / 0x11B | 1280x1024 | far too tall |
| 0x163-0x171, 0x13A/0x13C, 0x14B/0x14D, 0x15A/0x15C | other OEM panel modes (1280x768, 1280x800, 1400x1050 and similar) | not this panel |

**0x100 (640x400) is not in the mode list at all** - so the doubt the ati plan
carried about 640x400, which turned out to be real on the Rage, resolves the
other way here.

This is not a limit tier-0 can work around: 800x600 and 1024x768 exceed what
the panel can display, and there is no scaling direction that helps. The
family's mode table is now four rows - 640x480 and 1024x576, at 8 and 16 bpp -
in `src\chipsets\intel\intel_hw16.c` and the manifest together.

**Hazard for siblings, and for this machine with an external monitor.** Mode
availability is a function of the attached display, evaluated by the VBIOS at
call time; the mode *list* is static but the per-mode attributes are not. With
a VGA monitor attached, the 800x600 and 1024x768 rows may well flip to
supported. A driver that advertised them on that basis would then refuse at
stage 9 the moment the machine booted panel-only. The family advertises only
what the panel alone supports, which is the configuration that always holds.
OEM mode *numbers* are likewise a per-VBIOS fact: a 915GM or G33 sibling
re-surveys before its rows are trusted.

## Memory: the UMA numbers, all four of them, reconciled

| Source | Value | Meaning |
|---|---|---|
| GGC (host bridge 0x52) | **0x0030** â†’ GMS 3 â†’ **8 MiB** | what the BIOS stole from system RAM |
| BSM (IGD 0x5C) | **0x7F800000** = 2040 MiB | stolen base - the top 8 MiB of the 2 GiB address space |
| VBE 4F00h TotalMemory64K | **123** â†’ **7.69 MiB** | what the VBIOS offers the driver |
| WDDM AdapterRAM (Windows) | 256 MiB | the WDDM driver's dynamic view; meaningless to us |

The 8 MiB / 7.69 MiB gap is **320 KiB**, which reconciles exactly: a 256 MiB
GMADR aperture needs 256 KiB of GTT entries (65536 pages x 4 bytes), and the
GTT lives in stolen memory, plus 64 KiB of VBIOS scratch. This is the audit's
Â§3 "three answers disagree" made concrete, and it confirms the GTT-in-stolen
-memory model from the kernel driver on this machine.

Tier-0 consequences: the driver will believe 7.69 MiB from 4F00h, well inside
the family's 16 MiB mapping ceiling and far above the largest advertised mode
(1024x576x16 = 1.125 MiB visible). The declared `VideoMemoryBytes = 4194304`
floor for the host mode-layout check remains correct and untouched.

## BARs: DOS and Windows do not agree, and that is fine

| BAR | DOS (BIOS assignment) | Windows 10 | Role |
|---|---|---|---|
| 0 | FE980000 | FEA80000 | MMIO register file |
| 1 | 0000DC81 â†’ I/O DC80 | DC80 | I/O pair |
| 2 | D0000008 â†’ **D0000000**, prefetchable | D0000000 | **GMADR aperture** |
| 3 | FE940000 | FEA40000 | GTT (the Gen3 separate BAR) |

Windows relocates MMIO and the GTT up by exactly 1 MiB; **GMADR is identical in
both venues**, and identical to the PhysBasePtr every supported mode reports.
So the one address tier-0 depends on is stable, and the two that move are ones
tier-0 never reads. For the native phase the lesson is explicit: read BARs from
PCI config at run time, never cache one across a boot, and expect Win98's own
assignment to be a third answer (the INF already sets `PCIRebalance=1`).

## VBIOS image

Genuine option ROM: `55 AA` signature, 118 blocks = 60416 bytes of ROM inside
the 64 KiB shadow, PCIR data structure at offset 64 naming **8086:27A2** - the
945GM. So HP ships the generic 945-mobile VBIOS and the GSE's own 27AE is not
in the ROM's PCI data; the ROM matches the family, not the exact SKU. Worth
knowing before any future ROM-parsing work; irrelevant to tier-0.

Identity strings: `Intel(r) 82945GM Chipset Family Graphics Chip Accelerated
VGA BIOS`, vendor `Intel Corporation`, VESA version **0300** (VBE 3.0), product
revision "Hardware Version 0.0". VBE/DDC (4F15h) works from DOS - capability
and read both returned 004F - and returns the same AUO B101AW01 V2 EDID the
Windows registry holds, which independently confirms the panel timings
(1024x576@60, 54.20 MHz, 1344x672 totals) from a second path.

## What remains unmeasured

Only one item from the original Phase 0 list, and it cannot be answered by a
query-only tool: **whether 4F02h with bit 14 actually sets a mode and leaves the
linear framebuffer usable.** Every static indicator says yes (VBE 3.0, the LFB
attribute on all six supported modes, PhysBasePtr matching GMADR). It is proven
or disproven at stage 3/9 of the first Win98 boot, which is Phase 3's job.
