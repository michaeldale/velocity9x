# Vendor documents this tree cites

The Intel documents the decision records rely on, with the hash of the
copy that was read. Same purpose as `docs\ddk-inputs.md`: a claim sourced
to a document should be checkable against the same bytes.

One document is vendored here. The rest are not, because the repository
is mirrored to a public GitHub and Intel's datasheets and open-source
PRMs carry "NO LICENSE, EXPRESS OR IMPLIED ... All rights reserved" with
no grant to redistribute. The OpRegion specification is different: it
carries a Creative Commons Attribution-NoDerivs grant on page 2 ("You
are free: to Share - to copy, distribute, display, and perform the
work"), so a verbatim copy may be shared. It is included unmodified, and
the attribution is Intel Corporation, copyright 2008.

The working copies of the others live outside the tree in
`C:\everything\claude\personal\intel driver research\`, whose README
records where each came from and what it answers.

## Vendored

| File | Document | SHA-256 |
|---|---|---|
| `intel-igd-opregion-spec-rev1.0-2008.pdf` | Intel Integrated Graphics Device OpRegion Specification, rev 1.0, 1 October 2008, 133 pages, CC-BY-ND | `B7B3E89C6151E92E35CFE520078CFDA3EE7B2A16EBE84F8AACC925EE3D9CBFC5` |

## Cited, not vendored

| Document | Where it came from | SHA-256 |
|---|---|---|
| Mobile Intel 945 Express Chipset Family Datasheet, 309219-006, June 2008 | `intel.com/Assets/PDF/datasheet/309219.pdf` via the Wayback Machine | `D549265702D5F0ED8CB32B4B4DFA1A32C654A41647339EA62CACC459113FC13C` |
| Intel 945G/945GZ/945GC/945P/945PL Datasheet, 307502-005 | `intel.com/Assets/PDF/datasheet/307502.pdf` via the Wayback Machine | `C160ECEAF28F92F2096E927C2F22FDECBB693CA0117F7C167FC422C047036639` |
| Mobile Intel 945 Express Chipset Family Specification Update, 309220-013 | `intel.com/Assets/PDF/specupdate/309220.pdf` via the Wayback Machine | `02A242BE60CC06B8835A2CA0C43A3EEEE69ECF46FDEED0B3966B8DBC7B64B17A` |
| Mobile Intel 915 and 910 Express Chipset Family Datasheet, 305264-002, April 2007 | `intel.com/content/dam/doc/datasheet/mobile-915-910-express-chipset-datasheet.pdf` | `A856A49BE3D2BDEF655C83556ACAEEA34DF43641D58AF9B71C89675F7BCE99FA` |
| 965 Express / G35 Graphics Controller PRM Vol 3, Display Registers | `x.org/docs/intel/VOL_3_display_registers.pdf`, also `cdrdv2.intel.com` 690980 | `CB9DDB082C4F4031183C6F74081811D970996644983B6D2087DBC6888613ABD2` |
| Haswell PRM Vol 11b, Display Watermark Guide, December 2013 | `cdrdv2-public.intel.com/690992/` | `57EBFC3954FAD72544017F6B7AA21D2CC9A216D5E4F79C7402187CBBA73339E2` |

## What is not documented anywhere

No programmer's reference for any Gen3 part - 915, 945, G33 - was ever
published, for the 3D engine or for the display registers. The Wayback
inventory of `intellinuxgraphics.org`, the site Intel published on, is
recorded in `docs\plans\intel-3dmark99-missing-textures.md`. The chipset
datasheets above contain no graphics MMIO at all. `FW_BLC` at 20D8h
appears in no Intel document, and where a Gen4 PRM would give a watermark
number it defers to a "high priority bandwidth analysis spreadsheet" that
was never published.
