# The 915GM's GMA 900 joins the intel-gma family on a DOS survey: same resources as the netbook, one mip layout of its own, and a ring mapping that would have landed on its registers

Date: 2026-10-08
Machine: no 915 is in the fleet and nothing below has run on one. The
netbook (945GSE, boot 120) ran this build for regressions.
Evidence: three V9XSURV reports (build `6b22fc2`, DOS 7.10,
`WindowsPresent=no`) attached to GitHub issue 3 by its reporter, committed
unmodified:

- [`lenovo-3000-c100-915gm-vgasurv-2026-10-07.ini`](../probe/references/lenovo-3000-c100-915gm-vgasurv-2026-10-07.ini)
  - Intel 915GM, GMA 900.
- [`dell-inspiron-560-g45-vgasurv-2026-10-07.ini`](../probe/references/dell-inspiron-560-g45-vgasurv-2026-10-07.ini)
  - G45/G43, GMA X4500.
- [`hp-8000-elite-sff-q45-vgasurv-2026-10-07.ini`](../probe/references/hp-8000-elite-sff-q45-vgasurv-2026-10-07.ini)
  - Q45/Q43, GMA 4500.

The date in the file names is the issue's; the Lenovo's own clock says
2005-01-01.

## Why

The issue asks for 3D on a GMA 900 laptop and two GMA 4500 desktops.
Michael asked for the GMA 900 in 0.13. The GMA 4500s are Gen4, a different
3D architecture, and stay where
[`intel-gen4-gen5-bringup.md`](../plans/intel-gen4-gen5-bringup.md) puts them.

## What the 915GM survey establishes

Every value the Gen3 path reads from configuration space, set beside the
netbook's (945GSE `8086:27AE` rev 03), whose values are pinned in
`scripts\check-intel-gtt-capture.ps1` and `check-intel-ring-plan.ps1`:

| | 915GM (survey) | 945GSE (netbook) |
|---|---|---|
| Function 0 | `8086:2592` rev 04, class 030000 | `8086:27AE` rev 03 |
| Function 1 | `8086:2792`, class 038000 | `8086:27A6` |
| BAR0 (MMIO) | `D0000000` | `FE980000` |
| BAR2 (GMADR) | `A0000008`, 256 MiB aligned | `D0000008` |
| BAR3 (GTTADR) | `D0080000`, 256 KiB aligned | `FE940000` |
| GGC (config 52h) | `0030`: GMS 3, 8 MiB | `0030`, `StolenBytes=00800000` |
| BSM (config 5Ch) | `7F800000` | `7F800000` |
| VBE total | `0x7B0000` (123 x 64 KiB) | `0x7B0000` |
| VBE | 3.0, LFB at `A0000000` = BAR2 | 3.0, LFB at `D0000000` = BAR2 |

The netbook's BARs are its DOS assignment
([2026-08-17](2026-08-17-intel-gma-phase0-dos-evidence.md)); Windows moved
BAR0 and BAR3 there and left GMADR where it was.

Same BAR layout, same stolen size and encoding, same BSM, same VBE size. The
reserve, the ring offset (`0x6B0000`) and the heap end are therefore the
netbook's on this machine. The GGC and BSM values are read from function 0's
own config dump; the driver reads GGC from the host bridge, which the survey
did not dump. The 945 encoding was applied to the 915's GMS field on the
strength of Linux's `I855_GMCH_GMS_*` table covering both, and it agrees with
VBE.

The BIOS describes 640x480, 800x600 and 1024x768 at 8, 16 and 32 bpp. 800x600
at 8 bpp has an 832-byte pitch. It leaves the 1024x576 OEM rows `0160`/`0161`
undescribed, as it does `0160` to `0171` and the high modes.

## The defect found on the way: the runtime ring was mapped through the netbook's constant

`loader.asm` composed every reserve mapping as `V9X_I9XX_GMADR_BASE` +
offset, with `V9X_I9XX_GMADR_BASE EQU 0D0000000h`: the netbook's BAR2. The
armed diagnostic paths also check `V9xI9xxMmioBase == FE980000h`, so they
cannot run anywhere else. The runtime ring open checked neither. On the 915GM
it would have enabled the ring at graphics address `0x6B0000` and mapped
physical `D06B0000` for the CPU's side: past BAR0's 512 KiB at `D0000000`
and past BAR3's 256 KiB, so a window onto nothing the ring is in. The HAL's
commands would have gone there and the GPU would have fetched whatever was in
the real ring's pages.

Changed: `V9XMINI_FN_I9XX_RING_OPEN` takes GMADR in EBX, which the descriptor
reads fresh from BAR2 (`V9xPciReadIntelGttConfig`) before the call, as the ATI
and SiS map verbs take their BARs. The mini-VDD refuses a base below 16 MiB,
one not 256 MiB aligned, one equal to the mapped MMIO or GTT base, and one
that differs from the base an existing mapping was made through, before its
first register store. On the netbook the value is `D0000000` and nothing
changes. The armed paths keep the constant.

## A second guard: the generated ring offset against the reported size

The ring's offset is a build-time constant (`V9X_I9XX_RING_START`, generated
for VBE `0x7B0000`). The DirectDraw heap ends wherever the reported size puts
the reserve. A machine reporting any other size, a 915 or 945 BIOS set to
16 MiB stolen for example, would have its heap run over the ring. The
descriptor now withholds the ring, as `ring-layout-mismatch`, when the
reported size is known and its reserve offset is not the generated one.
`V9X_I9XX_RUNTIME_RING_START` in `intel_gma.h` restates the constant, and
check-tree fails if the two differ. A size of zero is let through, because
the first descriptor call at enable-start comes before 4F00h is read; the
call that stamps DirectDraw's caps comes after.

## The one 3D difference Mesa knows of: where the sampler finds mip levels

Desk evidence, not measured. Gallium's i915 driver splits the two parts by
`is_i945`, false for `I915_G` and `I915_GM`, and uses it in two places:

- **Mip layout** (`i915_resource_texture.c`). The 945 gets
  `i945_texture_layout_2d`: level 1 below level 0 and level 2 beside level 1.
  The 915 gets `i915_texture_layout_2d`: every level at the left edge, each
  below the one before. MAP_STATE has a layout bit (`MS4_MIP_LAYOUT_*`, bit 8)
  that Mesa never sets, so with the same zero the two parts read different
  places. A 945-placed chain on a 915 samples garbage below level 1.
- **Early Z**, with tiled depth only. This driver tiles nothing.

So the HAL gains a layout: `v9x_d3d_i9xx_layout_miptree` takes `stacked`,
and `V9X_DD_ENGINE_CAP_I9XX_MIP_STACKED` (engine_abi.h, `0x8000`) carries it.
Only the 915GM's descriptor stamps it. A host test pins three chains in the
stacked layout. It was written beside the code, then shown to fail with the
945 layout forced on.

Single-level textures are the same in both layouts.

## What the 915GM gets

Function 1 (`2792`) is not claimed, as the 945's `27A6` is not
([2026-09-17](2026-09-17-intel-945-function1-ids-claimed-by-decision.md)).

- PCI identity in the backend, the device list, the registry table and the
  INF (`gma900-915gm`). The settings page names it "Intel GMA 900 (915GM)".
- The netbook's Gen3 Direct3D, blits and ring flips, under the same
  IntelRuntime3D, IntelFlip and IntelAsyncSubmit keys and the same `V9X3D OFF`
  recovery. This extends the 2026-09-16 sustained-3D amendment to the errata
  gate to a chip whose errata nobody here has read. The 915 has its own
  specification update.
- The family's mode table, which the manifest contract makes identical for
  every chip. The 1024x576 rows are unavailable on this BIOS, so the runtime
  VBE scan is what hides them and offers 1024x768. The scan and the registry
  sync are generic and have run elsewhere, but not on this BIOS.
- Not the armed Phase 4/5/6 diagnostics: they require revision 03, MMIO at
  `FE980000` and BSM `7F800000`, and refuse here.

## The netbook on this build: nothing regressed

The ring path changed for the 945 too: GMADR now comes from a fresh BAR2
read through a new parameter. So the whole set (DRV, VxD, HAL, settings page,
ICD, `42a5f64-dirty`) went onto the netbook by WININIT rename, boot 120,
before the release. Evidence:
[`netbook-915gm-build-check-2026-10-08/`](../probe/netbook-915gm-build-check-2026-10-08/).

- V9XDDP `Result=COMPLETE`, D3D HAL found. Every `*Ok` key that was 1 in
  the last Gen3 reference (`2026-09-26-phase1d-gen3-V9XDD-after.ini`) is 1
  again. Three went from 0 to 1 since then (depth fog, mip level select,
  trilinear blend). The mip ladder reads back right in the 945 layout,
  level 2 at +128 bytes beside level 1 (`MipLadderOk=1`, `MipTriOk=1`).
- Snapshot after it: driver and HAL `0.13.0 42a5f64-dirty`, `EngineCaps=
  0x00003614` (D3D, FLIP, FLIP_RING, async), `Direct3DMode=hardware`, 570
  draws submitted and 0 refused, no breadcrumb or ring-space timeouts, 23
  flips, 14 engine blits. FLIP_RING is stamped only with a ring, so the
  ring opened through the new GMADR path. The stacked bit (`0x8000`) is
  absent, as it should be on a 945.
- V9XGLP `Result=PASS`, every SgisMip case on its level.
- Quake 2 demo1 spawn `timerefresh`, 640x480 window, multitexture: 34.79
  fps, against 34.5 this morning. The frame is drawn correctly.

The renderer-name change in `d3d_core.c` (the ICD and the render interface
name the 915GM "GMA 900") came after this run. It reads only the stacked
bit, which the netbook does not carry.

## Unmeasured, in the order a first boot would meet it

1. Whether Windows 98 SE runs on the Lenovo at all.
2. The mode scan hiding `0160`/`0161` and publishing 1024x768.
3. The mini-VDD's MMIO capture and GTT inventory on BAR0/BAR3 at these
   addresses.
4. The ring coming up through GMADR `A0000000`, then a blit, then a triangle.
5. Display-side registers the HAL uses: the plane base for flips and the
   pipe's scan line. That they sit where they do on the 945 is assumed, not
   checked against any 915 source.
6. The stacked layout, through V9XGLP's mip ladder or V9XTSHP.
7. Anything in the 915GM's errata that the 945's do not share.

`IntelRuntime3D=0` (or `V9X3D OFF` from DOS) leaves a 2D VBE desktop if any
of 3 to 7 hangs.

## The GMA 4500 surveys

`8086:2E22` (G45/G43) and `8086:2E12` (Q45/Q43), both rev 03, Eaglelake
VBIOS build 1800. Both have the Gen4 layout the plan predicted from the X61
and Ironlake: BAR0 64-bit MMIO, BAR2 64-bit prefetchable aperture, I/O in
BAR4, no GTT BAR. 32 MiB stolen (VBE `TotalMemory64K=511`), VBE 3.0 with the
LFB at BAR2, modes to 1920x1440. The VBIOS's own PCI header names `2E02`,
not the function's ID. Nothing here changes for them. The tier-0 VBE family
already matches them by class, and Gen4 3D is the separate project the plan
describes.

## Every gate a 915GM meets, against its survey

Checked so that nothing added for safety stands between this machine and
Direct3D:

| Gate | Requires | Survey |
|---|---|---|
| PCI scan, backend probe | `8086:2592` | yes |
| BAR0 read (`V9xPciReadIntelMmioBar`), MMIO capture | 512 KiB aligned, 16 MiB to `FFF80000` | `D0000000` |
| BAR2/BAR3/BSM read (`V9xPciReadIntelGttConfig`) | BAR2 256 MiB aligned; BAR3 256 KiB aligned; BSM at least 16 MiB | `A0000000`, `D0080000`, `7F800000` |
| GTT capture | GGC stolen size non-zero, VBE size known | 8 MiB, `0x7B0000` |
| Ring layout (added) | VBE size whose reserve is at `0x6B0000` | `0x7B0000` |
| Ring open in the mini-VDD (added) | GMADR 256 MiB aligned, not BAR0 or BAR3 | `A0000000` |
| Arm identity, revision, MMIO `FE980000`, BSM | armed diagnostics only | refuses, as intended |

A BIOS that sets the aperture to 128 MiB at an address that is not 256 MiB
aligned would fail the BAR2 read, which predates this change. The Lenovo's
BAR2 is 256 MiB aligned whatever its size.

Gates: `check-tree.ps1`, `build-host.ps1`, `run-checks.ps1`.
