# NVIDIA GeForce 210 (GT218): a Tesla generation in the nvidia family

Date: 2026-11-11

Status: Phase 0 done from Windows' own records
([record](../decisions/2026-11-11-m-bxmfa8-gt218-first-look.md)); config
space itself is read in Phase 2. Phase 1 done: the tier-0 `gt218` row runs
on M-BXMFA8 since boot 82, `enable-ok`, V9XGDI and V9XMSW pass
([record](../decisions/2026-11-11-gt218-tier0-enable-ok.md)); open: it
drops the software Direct3D the `vbe` package offered. Phase 2's first
read done ([record](../decisions/2026-11-11-gt218-register-probe.md)):
chipset A8 stepping B1, 512 MiB DDR2/3 on one partition, the VBE
framebuffer is BAR3 + 16 MiB, PFIFO/PGRAPH/copy engine on, and **no BAR
VM** (1704h/1708h/170Ch all zero), so hard part 1 below is wrong as
written. Next: a read-only BAR1/BAR3 and VGA CRTC pass, then Phase 3. Register facts below come from
open-source and reverse-engineered sources (nouveau, envytools) unless a
record says the card confirmed them.

Target: the GeForce 210, PCI `10DE:0A65`, a GT218 (nouveau chipset `NVA8`,
Tesla 2 / G200-class), as the second generation of the `nvidia` family
(`one-family-per-vendor`). It shares nothing with NV3 below PCI/VGA, so it
gets its own module tree `src/chipsets/nvidia/gt218/` and its own engine
type; no NV3 fact moves into family-wide code and no GT218 fact goes into
the NV3 files.

## The machine

M-BXMFA8, computer W9QI3632, remote-help over the relay only
(`v9xctl -Relay M-BXMFA8`). The owner asked for GeForce 210 support
(chat msg 164, 2026-11-08) and was told on 2026-11-11 that bring-up has
started (msg 166).

- Dell Q45/ICH10, Core 2 Duo E7500, Windows 98 SE, agent 0.15.0.
- GeForce 210 `10DE:0A65`. Subsystem, revision, BAR layout and memory size
  are not yet read (Phase 0).
- Runs the signed `vbe` package since boot 78; boot 81 is `enable-ok` at
  1024x768x16 after the failed ati swap of boot 79 was undone
  (`FIX16.REG`).
- VBE 3.0, OEM revision `7018`, reports 224 x 64 KiB = **14 MiB**, which
  is a VBE window, not the card's memory. 29 modes listed, 26 published,
  320x200 to 1280x1024, 8/16/32 bpp; 16 bpp is 5:6:5. No 1600 or 1920
  mode. Every mode's PhysBasePtr is `CF000000`.
- No EDID through VBE.
- MTRR 0 is write-combining over 8 MiB at `CF000000` (the driver's
  default WC span).

Evidence: `build/driver-results/m-bxmfa8-gt218-boot81/` (gitignored:
V9XBOOT.INI, V9XHW.INI, V9XMODES.INI, V9XDDH.INI); goes into the Phase 0
record.

## Sources to fetch (not yet in `build/external/`)

- Linux nouveau, `drivers/gpu/drm/nouveau/nvkm/`: `subdev/mc`, `subdev/fb`
  (`nv50.c`, `gt215.c`), `subdev/bar/nv50.c`, `subdev/mmu`,
  `engine/fifo` (nv50/g84 channels), `engine/gr` (`nv50.c`, the
  generated `ctxnv50.c` context program), `engine/ce` (`gt215.c` and its
  open falcon microcode), `engine/disp` (nv50/gt215 EVO).
- envytools rnndb (register database) and its hwdocs for NV50 VM, PFIFO,
  PDISP.
- Mesa `nv50` gallium driver for 2D/3D class methods.

## What the sources say the hard parts are

All unmeasured; the probes exist to check them.

1. **Every engine runs through the NV50 VM.** Channels, PGRAPH, PCOPY and
   the BAR1 aperture all translate through page tables in VRAM. The VBE
   LFB works, so the BIOS has set up a BAR1 mapping; whether it covers
   more than the 14 MiB window, and where its page directory is, is the
   first thing to read. *Measured 2026-11-11: wrong. The LFB is in BAR3
   and the BAR VM registers are all zero; there is no mapping to inherit
   ([record](../decisions/2026-11-11-gt218-register-probe.md)).*
2. **PGRAPH needs a context program.** Nouveau generates it in C (no
   external firmware for NVA8 graphics). Porting it is the price of the
   2D (`502D`) and 3D classes.
3. **PCOPY (GT215+) is a falcon with nouveau's own MIT microcode**, a
   copy engine without PGRAPH's context program. It may be the cheaper
   first engine for DirectDraw copies; it does not fill.
4. **Scanout is the EVO display engine.** A flip is a method on an EVO
   channel, not a register poke. Until then flips stay emulated.
5. **Video memory beyond 14 MiB** needs either a larger BAR1 mapping we
   build ourselves or the PRAMIN window (BAR0 + 700000h, 1 MiB).

## Phases

### Phase 0 — survey (in progress)

Read PCI config space (ids, subsystem, revision, every BAR with its size,
expansion ROM), and the VBE facts above, into
`docs/decisions/2026-11-11-m-bxmfa8-gt218-first-look.md`. Done when the
record names BAR0/BAR1/BAR3 bases and sizes.

### Phase 1 — tier-0 claim

A `gt218` chip row in `packaging/families/nvidia/family.psd1` for
`10DE:0A65`, with the VBE's own mode list, CPU drawing, no engine;
`src/chipsets/nvidia/gt218/gt218_hw16.c`; the backend's probe accepts the
exact id. Gates: check-tree, build-host, run-checks. Machine: boot it,
`enable-ok`, V9XGDI and V9XMSW pass, same modes as `vbe`.

### Phase 2 — read-only register probe (V9XNV50MM)

A mini-VDD mapping of BAR0 and a Win32 probe that reads, writes nothing:
PMC_BOOT_0 (chipset `A8` expected), PMC_ENABLE, the PFB memory size and
type, the BAR1 VM state and page directory, the PRAMIN window base, PFIFO
and PGRAPH enable/idle, PDISP head 0 surface address and pitch versus the
VBE mode, and whether the falcon engines (PCOPY, PDAEMON) are running.
Record each against what the sources predicted.

### Phase 3 — engine choice (decision for Michael)

Once Phase 2 shows the VM state:

- **A. PCOPY first.** Channel + VM + falcon load; copies only, fills stay
  on the CPU. Less code before first light.
- **B. PGRAPH 2D first.** Port the context program, then the `502D`
  class gives fill and copy, and is the road to Direct3D anyway.

### Phase 4 — DirectDraw on the engine, heap beyond 14 MiB

Fill/copy behind `engine32`, then the video-memory heap sized from PFB
rather than VBE. V9XDDP and V9XD32 at 16 and 32 bpp.

### Phase 5 — flips through EVO

### Phase 6 — Direct3D (its own plan)
