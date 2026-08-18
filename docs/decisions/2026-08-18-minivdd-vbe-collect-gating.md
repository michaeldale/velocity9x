# Per-family gating of the mini-VDD's boot-time VBE collection

Date: 2026-08-18
Status: accepted

The first physical-hardware run (BARRY, S3 Trio64) hung the boot with a Windows
protection error, isolated to `V9XMINI.VXD` and then to its `Device_Init` VBE
collection (`docs\issues\2026-08-18-trio64-minivdd-boot-hang.md`). This record
covers three decisions made fixing it.

## 1. The alignment fix

`V9xMini_Vbe_Collect` allocated its V86 scratch with
`_Allocate_Global_V86_Data_Area, <512, 0>` and truncated the returned linear
address to a real-mode segment with `shr eax, 4`. Flags 0 requests **byte**
alignment, so the truncation could point the segment up to 15 bytes below the
allocation: the `'VBE2'` stamp then corrupted a neighbouring V86 data area,
every peek offset was skewed, and the BIOS could write past the end. The fix is
`GVDAParaAlign + GVDAZeroInit` - the shift is now exact and the buffer starts
zeroed, so a BIOS that returns success without writing cannot leave stale bytes
to be read back as answers. The stamp also uses the returned linear address
directly instead of recomputing it from the truncated segment.

Verified on the card that exposed it: the fixed collection boots the physical
Trio64 clean twice; the unfixed one hung it every time. 86Box guests evidently
received paragraph-aligned blocks by luck, which is why three families of
guest testing never saw this.

## 2. Families that never read the cache do not run the collection

One `V9XMINI.VXD` used to ship identically to every family, so every family ran
eight nested `Exec_Int 10h` BIOS calls at boot to fill a cache that only the
tier-0 aperture default (`v9x_vbe_default_aperture` in
`src\display16\enable16.c`) ever reads. Families with a `read_aperture` hook -
`s3`, `matrox-m2` - never take that path: for them the collection was all risk
and no benefit, and the risk stopped being hypothetical on BARRY.

The gate is a build-time assembly switch, not a runtime check:
`Build.MiniVddVbeCollect = $false` in the family manifest makes
`build-active-package.ps1` (and `build-matrox-candidate.ps1`) pass
`-DisableVbeCollect` to `build-minivdd-skeleton.ps1`, which defines
`V9X_NO_VBE_COLLECT` and assembles the collection out of the image. The 4F9Ch
API stays; its zeroed cache is the designed "collection never ran" state the
16-bit side already treats as a refusal. A no-collect image announces itself
with a `V9X-MINI vbe-collect disabled` serial line, and the build asserts the
marker's presence (or absence) so the two variants cannot be confused.

Rejected: **runtime registry gating** from the mini-VDD. The DDK samples only
read the devnode registry at mode-change time, never at `Device_Init`; on a
first boot the display devnode is not reliably bound yet, and a raw
`_RegOpenKey` cannot know which `Display\000x` instance is ours (BARRY itself
carries a stale Cirrus `Display\0000`). Curing a boot hang by adding new
untested ring-0 registry code to the boot path would be the wrong direction.

Rejected: **keeping the (fixed) collection everywhere.** It passed on one real
BIOS after the fix, but for hooked families a residual risk with zero benefit
is not a trade. `Exec_Int` runs the real video BIOS with no possible timeout; a
BIOS that never IRETs hangs the boot and nothing ring 0 can do bounds it. Tier-0
families (`vbe`, `ati`) keep the collection because they have no other way to
learn the aperture - for them the new per-call serial markers
(`vbe-call fn=/arg=/ret=`) make any future hang name its exact BIOS call.

## 3. `VDD_Init_Order` stays as it is

The question came up whether the mini-VDD should init at `VDD_Init_Order + 1`
to sort after the master VDD before calling `VDD_Get_Mini_Dispatch_Table`. All
eight mini-VDD samples in the Windows 98 DDK (`src\display\vdd\{ati, cirrus,
s3, s3v, tseng, video7, xga}`) declare plain `VDD_Init_Order` and make that
call from `Device_Init`; our declaration already matches the convention, and it
is not implicated in the hang. Left unchanged.
