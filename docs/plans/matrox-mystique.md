# Matrox Mystique (102B:051A) in the matrox family

Date: 2026-10-18

Status: proposed. Decision 1 taken 2026-10-18 (the prefetch bit) and
coded for the two Millennium chips, with an 86Box fallback
([record](../decisions/2026-10-18-mga-control-bar-from-the-prefetch-bit.md)).
Nothing Mystique-specific is coded; no Mystique has run any Velocity9x
code.

Target: the Mystique, PCI `102B:051A`, as a third chip in the one `matrox`
family beside the MGA-2064W and MGA-2164W
([matrox-millennium-family.md](matrox-millennium-family.md)). Per-chip
differences are hw16 data and per-chip hooks, not a new family. First
physical gate: Michael's own Mystique. Second: Match451's card (VOGONS),
if they are willing.

## Two chips answer to 051A, with opposite BAR orders

This is the finding the rest of the plan is built around.

| | MGA-1064SG ("Mystique") | MGA-1164SG ("Mystique 220") |
|---|---|---|
| PCI revision | 00h (spec 4-3) | 03h (86Box; Match451's card) |
| BAR0 (10h) | MGABASE1, control, 16 KiB | MGABASE2, framebuffer, prefetchable |
| BAR1 (14h) | MGABASE2, framebuffer, prefetchable | MGABASE1, control |
| BAR2 (18h) | MGABASE3, 8 MiB ILOAD | MGABASE3, ILOAD |
| Same order as | the 2064W | the 2164W |

Sources:

- **MGA-1064SG Developer Specification**, Matrox, February 1997,
  `C:\everything\L10GL\docs\datasheets\MGA-1064sg_199702.pdf`: MGABASE1 at
  10h (3-2, 4-11), MGABASE2 at 14h and prefetchable (4-12), MGABASE3 at 18h,
  revision "(00h)" (4-3). The trademark page names the MGA-1164SG; the body
  does not describe it.
- **86Box** `build/upstream-86box/src/video/vid_mga.c`, PCI config read:
  revision 0 for `MGA_1064SG`, 3 for `MGA_1164SG`; BAR0 is "Control aperture
  for Millennium and Mystique, LFB for Mystique 220 and later". Emulation
  source, not silicon.
- **Match451's survey**,
  `docs/probe/references/matrox-mystique-pci-vogons-vgasurv-2026-10-17-d746481.ini`:
  revision 03, `Bar0=F5000008` (prefetchable), `Bar1=F4800000`,
  `Bar2=F4000000`, VBE `PhysBase=F5000000` on every linear mode. Measured on
  silicon, and it contradicts the 1064SG specification's BAR order. With
  86Box, it says this card is a Mystique 220. Its ROM does not say which
  (`MATROX/MYSTIQUE VGA/VBE BIOS (V1.5)`, copyright 1995).

Which one Michael's card is is unknown. It was described as a 1064SG; its
revision has not been read.

### What this does to the family's code

`millennium_hw16.c` states `framebuffer_bar` per device id (1 on the 2064W, 0
on the 2164W), and the engine hook maps the other BAR. One static value
cannot serve `051A`, because the order depends on the revision.

Recommendation: the control aperture is **the one of BAR0 and BAR1 that is
not prefetchable**. That holds on every source in this tree. The 2064W was
measured on 2026-09-09 (BAR0 non-prefetchable control, BAR1 prefetchable
framebuffer,
[record](../decisions/2026-09-09-millennium-2064w-bar-ordering.md)). The
2164W specification gives BAR0 prefetchable framebuffer
([extraction](../specifications/mga2164w-3d-engine.md)). The 1064SG
specification and Match451's 1164SG agree. The decision is a pure function
of the two raw BAR dwords, in host-testable C under `src/chipsets/matrox/`
with a unit test. It refuses when both or neither are prefetchable, so a
mis-wired card stays on the CPU path. runtime.asm reads the raw dwords and
interprets nothing (the `mtrr.c` pattern). It replaces the per-chip
`framebuffer_bar` for all three chips, so the 2064W and 2164W must be
re-run after it lands.

Alternative: key on PCI revision (below 3 means the 2064W order). It is
cheaper, but it trusts a boundary seen only in 86Box and one survey.

Either way a new exported symbol in runtime.asm is a design change, and it
needs agreement first (Decision 1).

## What else differs from the Millennium path

Read from the 1064SG specification. Nothing here was measured on a Mystique.

- **2D core.** "The same Windows acceleration core" as the 2064W (1-2).
  The family's 2D specification already cites this document for the
  2064W's drawing registers
  ([mga2064w-2d-engine.md](../specifications/mga2064w-2d-engine.md)), so
  `mga_engine.c` should apply unchanged. That has to be measured, not
  assumed: the write probe in Phase 2.
- **MACCESS** (MGABASE1+1C04h, 4-64): pwidth <1:0>, jedecrst <14>,
  memreset <15>, tlutload, nodither <30>, dit555 <31>. **memreset must stay
  0** outside the reset sequence (5-21). `eng_mga.c` and `gdi_accel.c`
  write MACCESS before operations, and the value written must leave bits
  14 and 15 clear on this chip. Bit 3, the 2164W's zwidth, is reserved
  here.
- **Z buffer: 16-bit only** ("16-bit Z-buffer", 1-4). The 2164W's 32-bit
  Z path (`mga_3d.c` ZWIDTH_32, the DRn_Z32 registers, the 32-bit depth
  record in `d3d_mga.c`) must not be offered on `051A`. The 1164SG is not
  documented here; until a 1164SG is measured, it gets 16-bit Z too.
- **Memory.** SGRAM or SDRAM, not WRAM (1-2, chapter 6); block-mode fills
  need OPTION hardpwmsk (4-50,
  [2D spec](../specifications/mga2064w-2d-engine.md)). The family writes
  no memory-controller register and uses no BLK fill, so this costs
  nothing unless BLK is tried.
- **RAMDAC.** Integrated, in the control aperture near MGABASE1+3C00h
  (Table 3-3 on 3-4; the text extraction of that table is shifted by a
  row, as the 2164W's was, so check the PDF before relying on the exact
  range), not a TVP3026. The
  family writes no RAMDAC register (matrox-millennium-family.md, "Decisions
  fixed"), so mode setting stays the BIOS's. The planned TVP3026 hardware
  cursor would not apply to this chip.
- **Texture engine.** Described in 1-4 and 2-4 (section 2.9), but its registers are cut
  from the public specification, as on the 2164W. The 2164W's texture
  interface was measured on that chip
  ([plan](matrox-mga2164w-hardware-3d.md)). Third-party descriptions say
  the 2164W carries the Mystique's texture engine; that is a hypothesis to
  measure here, not a licence to stamp D3D.
- **Modes.** Match451's BIOS offers 19 linear modes on 4 MiB, standard
  numbering, 32 bpp at 0112h/0115h/0118h up to 1024x768
  (`V9XMODES` will merge whatever the card's own BIOS lists).

## Phases

### Phase 0 - identify the cards (no driver code)

1. V9XSURV on Michael's Mystique, in real DOS, with `/rom`: revision, the
   three BARs and their prefetch bits, the ROM version.
2. Two 86Box profiles cloned from `Win98SE-Millennium` (port 9877), one
   `mystique`, one `mystique_220`. Both ROMs are present in
   `C:\86Box\roms\video\matrox` (`MYSTIQUE.VBI`, `Myst220_66-99mhz.vbi`).
   Run V9XSURV in each. The two profiles cover both BAR orders. They are
   the cheap loop, not the gate.
3. A decision doc recording which chip each card is, and whether
   86Box's revision/BAR model matches silicon.

Gate: both BAR orders seen, at least one on silicon (Match451's survey
already counts as one), and the BAR rule's unit test written and failing
before the code exists.

### Phase 1 - bind, VBE modes, CPU drawing

- `051A` as a chip in `packaging/families/matrox/family.psd1` (registry row
  regenerated by `update-backend-registry.ps1`), `mga_backend.c` accepting
  it by exact id, a third `V9X_HW16_DEVICE` in `millennium_hw16.c`.
- The BAR rule is in place for the 2064W and 2164W (2026-10-18). For
  `051A`, a fallback order for 86Box, chosen by revision.
- The engine hook does **not** claim the MGA engine for `051A` in this
  phase: engine_type NONE, everything on the CPU.

Gate: both 86Box profiles and Michael's card reach `Stage=enable-ok`,
`V9XMODES.INI` shows the merged list, `V9XGDI` passes, and `V9XMSW` runs
across the modes. The 2064W (86Box and A8U4I5 if it is back in) and the
2164W re-run on the new BAR rule.

### Phase 2 - the drawing engine

- The guarded write probe (`MGA2D.EXE`) under the Phase 1 driver: fill,
  forward copy and both overlap copies at 8, 16 and 32 bpp, read back by
  the CPU. This is what licenses `mga_engine.c` on this chip, and it
  settles the MACCESS value (bits 14/15 clear) and the fill/blit edges.
- Then the hook claims SOLID_FILL, SCREEN_COPY and FLIP for `051A`, and
  GDI and text follow the family's existing arm.

Gate: the probe passes on silicon at all three depths; `V9XGDI /accel`
passes; `V9XDDP` fills and flips with correct pixels.

### Phase 3 - Direct3D (separate approval)

Only after Phase 2, and only once the texture interface is measured on a
Mystique the way it was on the 2164W. D3D is stamped for `051A` with
16-bit Z only, and the 32-bit depth record is withheld. Check ARGB4444
textures early: they are garbage on the 2164W. OpenGL and Glide follow the
render interface as on the 2164W, if Phase 3 lands.

## Decisions wanted

1. **The BAR rule.** Taken 2026-10-18: the prefetch bit, with the new
   runtime.asm export (`V9xPciReadMgaBarRaw`). 86Box marks neither BAR
   prefetchable, so there the chip's stated order is the fallback. For
   `051A` there is no single stated order, so in Phase 1 both Mystique
   86Box profiles need one chosen by revision, or they run CPU-only. Real
   cards set the bit and never reach the fallback.
2. **Whether Phase 1 ships with the engine off** for `051A`, as above, or
   waits for Phase 2. Recommendation: ship CPU-only first. There is no gated
   default to add; the engine claim is code, and Phase 2 turns it on.
3. **Whether to ask Match451** to run the Phase 1 package on their 1164SG.
   Their card is the only measured 1164SG.

## Not in scope

The G-series (G100/G200/G400) and the 1164SG-AGP variants: different
memory controllers and RAMDACs, and no survey of any in this tree.
