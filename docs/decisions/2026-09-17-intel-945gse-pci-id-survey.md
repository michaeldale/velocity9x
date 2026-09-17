# The 945GSE graphics device has exactly one PCI ID; the wider Gen3 IDs are a different decision

Date: 2026-09-17
Status: recorded. Research only; no code changed.

## Question

Michael Dale asked whether the 945GSE GPU is ever exposed under a PCI device
ID other than `8086:27AE`, so that the driver could claim every variant even
where we have not tested one.

## Answer

No. Intel's own documents give the 945GSE integrated graphics one device ID,
and the Linux, X.org and Windows drivers all agree with it.

### Intel datasheet, document 309219-006 (Mobile Intel 945 Express Chipset Family)

Section 8.1.2, `DID2 - Device Identification`, B/D/F 0/2/0, quoted from the
pdftotext extract:

```
Default Value:   27A2h (1)
                 27AEh (2)
NOTES:
1. Valid for all Mobile Intel 945 Express Chipsets except for the Mobile
   Intel 945GME/GSE Express Chipset.
2. Valid for the Mobile Intel 945GME/GSE Express Chipset only.
```

Section 8.2.2, `DID2` for Device 2 Function 1 (the second display function
Device Manager lists), is `27A6h` for **every** mobile 945 SKU, no footnote.

The same footnote pattern holds for the host bridge (section 5.1.2, `27A0h`
vs `27ACh`) and the PEG root port (7.1.2, `27A1h` vs `27ADh`). So the
GME/GSE silicon carries a distinct ID set, and the GSE is not distinguished
from the GME anywhere in PCI config space.

### Intel specification update, document 309220 (November 2009)

The component identification table:

```
Product                                            Stepping  CRID  Device
Mobile Intel 945GM Express Chipset (lead-free)     A3        03    27A0h
Mobile Intel 945PM Express Chipset (lead-free)     A3        03    27A0h
Intel 945GT Express Chipset (lead-free)            A3        03    27A0h
Mobile Intel 940GML Express Chipset (lead-free)    A3        03    27A0h
Mobile Intel 945GMS Express Chipset (lead-free)    A3        03    27A0h
Mobile Intel 945GME Express Chipset (lead-free)    A3        03    27ACh
Mobile Intel 945GSE Express Chipset (lead-free)    A3        03    27ACh
```

Only one stepping (A3, CRID 03) was ever shipped for the 945GSE. That matches
the `REV_03` measured on the HP Mini 110 in
`2026-08-17-intel-gma-phase0-windows-evidence.md`, and it means the
`revision != 0x0003` check in `i9xx_arm.c` excludes no real 945GSE.

### Driver tables

- Linux `include/drm/intel/pciids.h`: `INTEL_I945GM_IDS` is `0x27a2` (I945_GM)
  and `0x27ae` (I945_GME). `INTEL_I945G_IDS` is `0x2772` only.
- The `pci.ids` database names `27ae` "Mobile 945GSE Express Integrated
  Graphics Controller"; its only recorded subsystem is `1775:11cc`. `27a2` is
  "Mobile 945GM/GMS, 943/940GML"; `2772` is "82945G/GZ".
- Intel's XP `igxp32.inf` maps `DEV_2772` i945G0, `DEV_2776` i945G1,
  `DEV_27A2` i945GM0, `DEV_27AE` i945GME0 (secondary-source; see below).

### Desktop 945 datasheet, document 307502

`DID2` for Device 2 Function 0 is `2772h` on 945G/GZ/GC; Function 1 is
`2776h`. `2776` is therefore the desktop second display function, not a
separate GPU, and must not be claimed as a primary.

## What this means for "support as many as possible"

There is nothing to widen *for the 945GSE*. Every 945GSE machine presents
`8086:27AE` revision 03 on function 0, so the strict match in
`gma950_hw16.c`, `intel_backend.c`, `i9xx_arm.c` and `family.psd1` already
covers every unit that exists.

Widening means claiming siblings, and they are three distinct decisions:

| ID | Part | Same die as GSE? | Difference that matters to this driver |
|---|---|---|---|
| `27AE` | 945GME | yes, identical silicon | none in config space; only the CPU differs. Already claimed. |
| `27A2` | 945GM / GMS / 940GML / 943GML | same generation, different silicon | 940GML/943GML have cut-down clocks and display limits; GMS is low-power. Erratum set overlaps but is not identical (309220 lists SKUs per erratum). |
| `2772` | 945G / GZ / GC desktop | same generation, desktop | different host bridge (`2770`), different PEG, no LVDS; the sandbox-layout and BSM assumptions were measured on a netbook only. |

Recommendation: keep the 945GSE match exact, and treat `27A2` and `2772` as
a separate "untested sibling" tier that the packaging exposes as opt-in
rather than as default compatible IDs. The reason is the same one that
governs the whole Intel effort: the Phase 4 and Phase 5 gates were opened on
a risk assessment written against the 945GSE A3 erratum list, and that
assessment does not transfer to another SKU without re-reading 309220 for
that SKU. A DOS survey run on a `27A2` or `2772` machine (the survey tool
already probes both IDs) is the cheap first evidence.

`27A6` and `2776` are function 1 and must stay ignored, as they are now.

## Hypotheses this killed

- "Some 945GSE boards report `27A2`." No source shows it; the datasheet
  footnote forbids it.
- "There are 945GSE revisions other than 03." Only A3 shipped.
- "`2776` is an alternative 945G graphics ID." It is the desktop function 1.

## Sources

- Intel 309219-006, Mobile Intel 945 Express Chipset Family Datasheet
  (https://www.intel.com/Assets/PDF/datasheet/309219.pdf), sections 5.1.2,
  7.1.2, 8.1.2, 8.2.2.
- Intel 309220, Mobile Intel 945 Express Chipset Family Specification Update,
  November 2009 (https://www.intel.com/Assets/PDF/specupdate/309220.pdf),
  component identification table.
- Intel 307502, 945G/945GZ/945GC/945P/945PL Datasheet
  (https://www.intel.com/Assets/PDF/datasheet/307502.pdf), tables for D2:F0
  and D2:F1.
- Linux `include/drm/intel/pciids.h`, torvalds/linux master.
- pci-ids.ucw.cz entries for 8086:27ae, 27a2, 2772.
- The `igxp32.inf` mapping is from a web search summary, not a file we
  opened; treat it as corroboration only.
