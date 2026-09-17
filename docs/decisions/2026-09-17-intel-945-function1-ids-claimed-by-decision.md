# 27A6 and 2776 are claimed as part of the 945GME/GSE family, by decision

Date: 2026-09-17
Status: accepted. Follows `2026-09-17-intel-945gse-pci-id-survey.md`.
Decided by Michael Dale. Decision only; no code or packaging changed yet.

## Decision

The intel-gma family will claim `8086:27A6` and `8086:2776` in addition to
`8086:27AE`, treating them as part of the 945GME/GSE device. The expectation
is that they behave the same way as `27AE`.

## What the survey record says about these IDs

The preceding record established, from Intel 309219 section 8.2.2 and Intel
307502 table 8-1, that:

- `27A6` is Device 2 Function 1 on every mobile 945 SKU (GM, GMS, GME, GSE,
  940GML, 943GML). It is the second entry Device Manager shows on the HP Mini
  110 (`PCI\VEN_8086&DEV_27A6&SUBSYS_308F103C&REV_03`, class 0380), measured
  in `2026-08-17-intel-gma-phase0-windows-evidence.md`.
- `2776` is Device 2 Function 1 on the desktop 945G/GZ/GC, whose Function 0
  is `2772`, not `27AE`.

Neither is a primary VGA function. Function 1 carries class code 0380
(display controller, non-VGA), has no VBE BIOS behind it, and is described by
the datasheet as an alias into the same graphics device rather than a second
GPU. Today the Win16 driver ignores it and the DOS survey does not probe it.

## Risk the decision accepts

This is a claim about what the silicon does that nobody has measured, and the
project's own rule is that such claims need a measurement before code relies
on them. The record therefore states the untested hypotheses plainly:

1. A Windows 98 SE Setup that binds this INF to the `27A6` node as well as
   the `27AE` node still produces one working display, not a conflict or a
   second failed device.
2. Function 1's BARs and MMIO alias Function 0 closely enough that the
   fingerprint, GTT inventory and ring paths written for `27AE` behave
   identically if entered through `27A6`.
3. `2776` on a desktop 945G behaves as (1) and (2) despite its Function 0
   being `2772`, a chip this driver has never run on.

The cheap first evidence is the existing DOS survey and Phase 0 Windows
capture (`capture-intel-gma-evidence.ps1`) run on the HP Mini with the INF
widened, comparing the Device Manager tree and the fingerprint result against
the 2026-08-17 records.

## Where the change lands when it is made

- `packaging/families/intel-gma/family.psd1`: `DeviceId` and
  `HardwareIdHint`, currently `27AE` exactly.
- `src/chipsets/intel/gma950/gma950_hw16.c`: `v9x_gma950_device`.
- `src/chipsets/intel/intel_backend.c`: `v9x_intel_gma_probe`.
- `src/chipsets/intel/i9xx_arm.c` and `src/display16/intel_exec16.c`: the
  arm identity check, which also pins revision 03.
- `include/velocity9x/intel_gma.h`: the single `V9X_PCI_DEVICE_GMA950_945GSE`
  constant would become a small list.

Each of those is a match on one ID; widening them is a design change to the
"strict PCI identity" comment in `intel_hw16.c` and should be a separate
commit that cites this record.
