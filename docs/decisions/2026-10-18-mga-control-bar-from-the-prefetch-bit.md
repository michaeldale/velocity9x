# The MGA control aperture is the BAR that is not prefetchable, except in 86Box

Date: 2026-10-18

Status: **coded and measured in 86Box only.** The prefetch-bit path, the
one real cards take, has run only in host tests. A8U4I5's Millennium II
was unreachable when this was written.

## Why the rule changed

The matrox family mapped "the BAR that is not the framebuffer", with the
framebuffer BAR stated per device id: 1 on the MGA-2064W, 0 on the
MGA-2164W. `102B:051A` breaks that. It is two chips with opposite orders,
the MGA-1064SG (revision 00h, control in BAR0) and the MGA-1164SG
(revision 03h, control in BAR1)
([plan](../plans/matrox-mystique.md)). So no single per-id value serves the
Mystique.

What does not vary on silicon is the prefetchable bit (BAR bit 3). The
framebuffer is prefetchable and the control aperture is not:

| Chip | Source | BAR0 | BAR1 |
|---|---|---|---|
| 2064W | measured ([record](2026-09-09-millennium-2064w-bar-ordering.md)) | `FDCFC000` control | `FD000008` framebuffer |
| 2164W | Matrox specification ([extraction](../specifications/mga2164w-3d-engine.md)) | framebuffer, prefetchable | control |
| 1064SG | Matrox specification 4-11, 4-12 | control | framebuffer, prefetchable |
| 1164SG | Match451's survey, `docs/probe/references/matrox-mystique-pci-vogons-vgasurv-2026-10-17-d746481.ini` | `F5000008` framebuffer | `F4800000` control |

## What was built

- `src/chipsets/matrox/mga_bars.c` (header `include/velocity9x/mga_bars.h`):
  `v9x_matrox_mga_select_control_bar`. It takes the two raw BAR dwords and a
  fallback index and returns the control BAR and its base. Pure, host-tested
  in `tests/host/test_mga_bars.c` on every value above. A new exported
  symbol, agreed by Michael on 2026-10-18 (option 1 of the plan's
  Decision 1).
- `runtime.asm`: `V9xPciReadMgaMmioBar`, which masked the flag bits and
  checked the base, is replaced by `V9xPciReadMgaBarRaw`, which returns the
  raw dword and interprets nothing. The base checks moved into the C.
- `millennium_hw16.c`'s engine hook reads both BARs and asks the rule.

## The measurement that changed the rule

Deployed to 86Box `Win98SE-Millennium` (2064W, port 9877, boot 53) by
WININIT rename. `V9XHW.INI` then read `EngineCaps=00000000` and
`GdiAcceleration=none`, where the build before had `00000007` and
`gdi-fill-copy-overlap-text`. The rule had refused.

The cause is in 86Box's device model,
`build/upstream-86box/src/video/vid_mga.c`, PCI config read: cases 10h and
14h, the low bytes of BAR0 and BAR1, return 00h for every Millennium and
Mystique. 86Box marks neither BAR prefetchable, and the rule as first
written refused "neither".

So when **neither** BAR is prefetchable, the chip's stated order decides:
the hook passes the per-chip `framebuffer_bar` as the fallback.
"Both prefetchable" still refuses, whatever the fallback; no source shows
it. Silicon, which sets the bit, never reaches the fallback. The host tests
check every measured case with each fallback value, so a wrong fallback
cannot change a silicon answer.

With that build (boot 54): `EngineCaps=00000007`,
`GdiAcceleration=gdi-fill-copy-overlap-text`, and `V9XGDI /accel`
`Result=PASS` with `EngineType=7`, build `b39cad6e-dirty`.

## What this does not establish

- The prefetch path on a real card. Owed: the 2164W on A8U4I5 (control in
  BAR1, expected `EngineCaps` with D3D set) and the 2064W if it goes back
  in.
- The Mystique. `051A` is still refused by `mga_backend.c`. In 86Box both
  Mystique profiles will take the fallback, and for `051A` there is no
  single stated order to fall back on. That is Phase 1 of the plan's
  business, not this change's.

Gates: check-tree, build-host and run-checks pass.
