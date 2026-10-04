# SiS 6326 family: survey, tier-0, then the 2D engine

Date: 2026-10-04

Status: Phase 1 done 2026-10-05 ([first boot](../decisions/2026-10-05-sis6326-first-velocity9x-bind.md)):
the sis family reaches enable-ok on card 2 (rev 0Bh) with 22 modes, GDI,
mode switching and a DirectDraw HAL passing; DirectDraw refuses three
low-resolution 8 bpp modes (open). Phase 0 survey: card 1 (rev C3) locks
A8U4I5 under SiS 2.28 and is parked. Next: Phase 2, the 2D engine.

The first SiS chip, and the first new vendor since Matrox. The card goes into
A8U4I5 (10.0.1.172), in place of the Trio3D. That machine has no DOS mode, so
every step runs as a Windows-side tool or as the driver itself. The local
86Box build has no 6326 device model (no "6326" device string in
`C:\86Box\86Box.exe`, checked 2026-10-04), so there is no emulator loop:
every iteration costs a physical boot.

The 2026-08-08 direction record ranked the 6326 last for want of
documentation (`docs/decisions/2026-08-08-initial-direction.md`). That has
changed: the SiS Rev. Ax/Bx datasheet is now in hand, including the 2D and 3D
register chapters. The reconciled reference is
[sis6326-registers.md](../specifications/sis6326-registers.md).

## What is already true

- A new family starts tier-0: VBE mode set, `EngineType NONE`, CPU drawing,
  modes merged from the card's own BIOS at boot (`MiniVddVbeCollect`). The ati
  family and the Rage IIC (`397da71`) started this way. Sequence:
  `docs/specifications/family-manifest.md`, "Adding a family".
- The DirectDraw HAL attaches with no engine and blits through `blt_cpu.c`;
  Direct3D falls to the software rasterizer. A tier-0 6326 is a usable
  desktop with DirectDraw and software Direct3D, before any SiS register is
  written.
- Rev. Ax/Bx has at most 4 MiB and no 32 bpp mode; its "16.8M colour" modes
  are packed 24 bpp (DS L548, L917). The 2D engine is restricted at 24 bpp.

## Phase 0 — read-only survey under SiS's own driver

Tool: `SIS6326.EXE` + `SIS6326.VXD`, `scripts/build-sis6326-probe.ps1`
(built 2026-10-04 as `sis6326-20261004-a`). It reads PCI configuration,
SR00-SR3F, CR00-CR3F, CR80, the MMIO window the stock driver selected, and
the BIOS shadow. Its only writes are the VGA index ports, each restored.

Runs, each saved as `docs/probe/a8u4i5-sis6326-registers-<date>/`:

1. Stock driver, desktop at its default depth.
2. The same at 16 bpp and at 24 bpp, if the stock driver offers them.
3. After a stock-driver Direct3D application has drawn a frame, so that the
   3D state registers hold SiS's own choices. Add `/force3d` only if SR39 D2
   reads 0 and the 2D-only run was clean.

Questions it answers, each settling a [Disputed] or [Not found] item in the
register reference:

- PCI revision: Rev. Ax/Bx (the datasheet's chip) or a later part (Xorg's
  8 MiB variants).
- BAR layout as Config Manager allocated it: 4 MiB framebuffer, 64 KiB MMIO,
  16-byte I/O.
- Which MMIO window the stock driver uses (SRB D[6:5]), whether it leaves the
  extensions unlocked, and whether it runs the Turbo Queue (SR27 D7, SR2C,
  SR3C).
- Memory: SRC raw against the datasheet's and Xorg's decodes, and against the
  installed size the stock driver reports. DRAM type from SR23/SR33.
- MCLK: which SR13 bit extends the post-scale, by which decode gives a
  plausible frequency.
- The 3D destination format and pitch SiS's HAL programs, and the enable bits
  it leaves set.

Output: `docs/decisions/<date>-sis6326-register-survey.md` with Measured /
What the sources add / Hypotheses this kills / Not established, as the Rage
IIC survey did.

## Phase 1 — tier-0 family `sis`

Files, following the Rage IIC bind:

- `packaging/families/sis/family.psd1`: chip `sis6326`, `EngineType NONE`,
  `MiniVddVbeCollect = $true`, `Vm.Emulator = 'none'`. Static modes only where
  the survey's ROM or the boot-time collection shows them.
- `include/velocity9x/sis_6326.h`: vendor and device ids, backend getter.
- `src/chipsets/sis/sis_backend.c`: host policy backend; `enter_mode`,
  `wait_idle` and `recover` return unsupported, as `ati_backend.c` does.
- `src/chipsets/sis/sis_hw16.c` (family table, all hooks NULL unless the
  BIOS refuses the linear-framebuffer bit) and
  `src/chipsets/sis/sis6326/sis6326_hw16.c` (the device, hooks NULL).
- Regenerated `src/common/backend_registry_table.inc`, a
  `tests/host/test_hw16_modes.c` entry, `check-tree.ps1` required files.

Exit gate: `Stage=enable-ok` on A8U4I5, every published mode set through
`V9XMSW`, `V9XDDP` run under Velocity9x and compared with the stock driver's.
Deploy through the WININIT rename route; the stock SiS driver stays installed
as the rollback.

## Phase 2 — the 2D engine for DirectDraw

- New engine type `SIS_6326 = 6` in `engine_abi.h` and
  `scripts/lib/family.ps1`.
- Host-tested command builder, `src/chipsets/sis/sis6326_engine.c`: register
  values for solid fill and screen copy, overlap direction, the 12-bit field
  limits, the 22-bit address mask, the 24 bpp restriction. Pure C, no I/O.
- `fill_engine_descriptor` in the device; the mini-VDD maps BAR1 (a
  `V9XMAPI.INC` entry, `loader.asm`, and the family flag in
  `build-minivdd-skeleton.ps1`, as the Rage IIC needed).
- Engine enable in the per-chip hook, not display code: SR5 unlock, SRB
  D[6:5] = 11, SR27 D6 = 1, Turbo Queue off.
- `src/display32/engines/eng_sis6326.c`: fill and copy, bounded waits on
  82ABh D6.

Before the engine path ships, a guarded write probe answers the two items no
source settles: whether width and height are programmed as *n* or *n*-1, and
whether a reverse-X copy starts at the last byte of the pixel. Both writes go
to off-screen VRAM inside guards, read back by the CPU.

Progress, 2026-10-05: the builder and its host tests are in, and the write
probe (`SIS2D.EXE`) ran it on card 2 at 8 and 16 bpp: fill, forward copy,
right-overlap and down-overlap copy all match byte for byte. *n*-1 and the
last-byte start are measured
([decision](../decisions/2026-10-05-sis6326-2d-engine-writes.md)).

Done, 2026-10-05: `afd2154` adds the SIS_6326 engine type, the enable hook
after every mode set, the mini-VDD map of BAR1 and `eng_sis6326.c`. On boot
209 every DirectDraw blit went to the engine with correct pixels, through
24 mode-set enables and no timeouts
([decision](../decisions/2026-10-05-sis6326-engine-under-directdraw.md)).
Phase 2 is complete for DirectDraw; GDI acceleration and throughput are not
measured.

## Later (sketch only)

- GDI acceleration: `gdi_accel.c` has arms only for the S3 engines today.
- Direct3D: the setup engine takes floating-point X, Y, Z, U, V, W per
  vertex, one triangle per fire, with Z16, alpha test, blending, fog,
  specular, perspective-correct mipmapped textures from local or AGP memory
  (DS 7.14). The driver sorts the vertices and states the order; the chip
  does the setup. Its own plan, after Phase 2.
- Hardware cursor (SR14-SR1F, SR38): the tier-0 quality plan's cursor item.
- Turbo Queue, page flip through SR30-SR32, overlay at CR80h-CRBDh.

## Decisions fixed

- No memory-timing or clock write: SR23, SR26, SR28/29, SR33-SR35 stay as the
  BIOS left them.
- Turbo Queue off until the synchronous engine path is measured correct.
- At most 4 MiB is assumed until a survey shows otherwise.

## Open questions only hardware answers

From the register reference, section 0 items 14-15 and section 11: the SR0C
and SR0E decodes; the ARGB8332/8233/8232 texel codes; width/height *n* or
*n*-1; texture pitch units; the TDRAWDIR rule; whether W is RHW; what the odd
member of each texture-blend mode pair does.
