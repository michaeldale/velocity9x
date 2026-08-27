# Intel GMA Gen3 (915/945/G33) hardware audit

Provenance: written on the `intel-gma-tier0` branch, whose family scaffolding
was never merged (archived at tag `archive/intel-gma-tier0`); salvaged to main
2026-08-28 per docs\plans\family-structure-and-next-d3d-roadmap.md Track A4.
Code paths named here (src\chipsets\intel\..., intel_gma.h,
scripts\lib\vbe-cache.ps1) exist only in that archive. The measured evidence
stands regardless; the netbook is meanwhile served by the vbe family
(docs\issues\2026-08-27-netbook-gma950-findings.md).

Status: accepted. **Phase 0 complete, 2026-08-17** - Windows half in
`2026-08-17-intel-gma-phase0-windows-evidence.md`, DOS half (which overturned
Â§4's central assumption) in `2026-08-17-intel-gma-phase0-dos-evidence.md`
Scope: informs the `intel-gma` family (tier-0 bring-up) and the later
`eng_i9xx.c` engine and native-modeset stages

## Why this exists

The physical target is an HP Mini 110-1000 netbook with a **945GSE / GMA 950**
(**measured**: `PCI 8086:27AE SUBSYS 308F103C REV 03`, host bridge `8086:27AC`,
second display function `8086:27A6`; BAR0 MMIO `FEA80000`, BAR2 GMADR
`D0000000` 256 MiB, BAR3 GTT `FEA40000`), Atom N280, 2 GB RAM, fixed
**1024x576 LVDS panel** (AUO B101AW01 V2, 54.20 MHz / 1344x672 totals @ 60 Hz),
VGA output on the side. There is no emulated
target at all: neither 86Box nor QEMU implements any Gen3 Intel IGD, which is
why `docs\plans\multi-chip-restructure.md` deferred this family to physical
hardware and why its manifest carries `Vm.Emulator = 'none'`. Everything the
ati family could split between an emulator and a laptop lands on one netbook
here, so this document has to be right before any register is touched - and
tier-0 is designed so that none is.

Intel never shipped a Windows 9x driver for any of these parts (official 9x
support ended with Extreme Graphics 2 / 865G), so unlike the Mach64 there is no
retail driver to benchmark against. The goal of the family's first release is
existence: a stable, correct tier-0 desktop on hardware that never had one.

Sources audited. All are licence-compatible with this project (GPL-3.0),
one-way:

| Source | Licence | Role |
|---|---|---|
| Linux `drivers/gpu/drm/i915` + `drivers/char/agp/intel-gtt.c` (torvalds/linux master, Aug 2026) | GPL-2.0 | Primary register reference |
| `xf86-video-intel` 2.7.x (`src/i830_display.c`, `i830_ring.h`, `i830_accel.c`) | MIT/X11, copyright Intel | Cleanest minimal UMS modeset + blitter usage |
| grub-extras `915resolution/915resolution.c` | GPL | VBE mode-table patch mechanics |
| Intel 965/G45 PRM Vol 3 (Display Registers), x.org mirror | public documentation | Register-level backup for the display block (9xx-compatible ranges) |

**Usage rule.** Register offsets, bit fields and hardware programming sequences
are *facts about the hardware* and are recorded here as such. Velocity9x source
remains independently written, per `docs\ddk-inputs.md`. This document is the
interface between the two: implement from this, not by transcribing driver code.

**Kernel path note.** The device-id table moved from `include/drm/i915_pciids.h`
to `include/drm/intel/pciids.h` in 2024; the old path 404s. Cited paths below
are current as of August 2026.

---

## 1. The addressing model

A Gen3 IGD is a UMA device: it has **no video RAM of its own**. The BIOS steals
system RAM at boot, and the chip scans out of that stolen memory through a
GTT-translated PCI aperture.

Four BARs on function 0 (`drivers/char/agp/intel-agp.h`):

| BAR | Contents | Size |
|---|---|---|
| 0 | **MMADR** - the MMIO register file | 512 KiB |
| 1 | I/O BAR (VGA-style index/data access to the same registers) | 8 bytes |
| 2 | **GMADR** - the graphics aperture; GTT-translated reads/writes; where scanout "is" | 128 or 256 MiB |
| 3 | **GTTADR** - the GTT itself, as its own BAR. **Gen3 only**: on Gen4+ the GTT moves into MMADR at offset 512 KiB | 128 KiB - 1 MiB |

There is also a **second PCI display function** (0x27A6 on the 945GM/GSE, and
siblings 0x2782/0x2792/0x2776/0x29C3 across the family), class 0380, present
for dual-independent-display OS licensing. The Linux driver ignores it
entirely; so do we. It must be recorded in the Phase 0 capture so its
appearance in Device Manager is a documented fact rather than a surprise.

Two config-space facts a driver needs that are **not** in any BAR:

- **GGC / GMCH Graphics Control** - config offset **0x52 of the host bridge**
  (device 0:0:0, *not* the IGD). The GMS field (bits 6:4) encodes stolen size:
  1/4/8/16/32/48/64 MiB (values 1-7); G33 adds 128 MiB (0x8) and 256 MiB (0x9).
  G33 also carries GTT-size bits 9:8. Reference: `intel-gtt.c`
  (`I830_GMCH_CTRL`, `I915_GMCH_GMS_*`, `G33_GMCH_SIZE_MASK`).
- **BSM / Base of Stolen Memory** - config offset **0x5C of the IGD**
  (function 0), 1 MiB aligned (`INTEL_BSM` in `include/drm/intel/i915_drm.h`).

**Tier-0 touches none of this.** VBE 4F01h reports a PhysBasePtr inside GMADR,
4F00h reports the VBIOS's view of usable memory, and the 16-bit driver maps
that and draws. GGC and BSM are Phase 0 diagnostics (the DOS survey reads both)
and become driver-published diagnostics in the native phase - the ati family's
"publish, don't act" pattern.

## 2. Display engine: pipes, planes, ports

The display block is register-compatible from 915 through 965 in the ranges
below (which is why the public 965 PRM Vol 3 documents our part). Offsets from
`drivers/gpu/drm/i915/display/intel_display_regs.h`, `i9xx_plane_regs.h`,
`intel_crt_regs.h`, `intel_lvds_regs.h`, `intel_pfit_regs.h`.

Two pipes, A and B. Timing block at **0x60000 (pipe A)** / **0x61000 (pipe B)**:

```
HTOTAL   +0x00   VTOTAL  +0x0C     bits 31:16 total-1, 15:0 active-1
HBLANK   +0x04   VBLANK  +0x10     bits 31:16 end-1,   15:0 start-1
HSYNC    +0x08   VSYNC   +0x14
PIPESRC  +0x1C                     bits 31:16 width-1, 15:0 height-1
```

Pipe enable: **PIPEACONF 0x70008 / PIPEBCONF 0x71008**, enable = bit 31.

Display planes: **DSPACNTR 0x70180 / DSPBCNTR 0x71180**; enable bit 31, pixel
format bits 29:26 (**2 = 8 bpp palettized, 5 = BGRX565, 6 = BGRX888** - exactly
our tier-0 depths plus the 32-bpp door this project keeps closed), pipe select
bits 25:24. **DSPAADDR 0x70184** (base, latched on write), **DSPASTRIDE
0x70188** (bytes, 64-byte aligned). 8-bpp palette: LGC_PALETTE at 0x0A000
(pipe A) / 0x0A800 (pipe B), 256 dword entries.

DPLLs: **DPLL_A 0x06014 / DPLL_B 0x06018** with dividers in **FPA0 0x06040 /
FPA1 0x06044** (N at bits 21:16, M1 at 13:8, M2 at 5:0). Clock math
(`intel_dpll.c`, `i9xx_calc_dpll_params`):

```
m = 5*(m1+2) + (m2+2);  p = p1*p2
vco = refclk * m / (n+2)         refclk = 96 MHz (non-SSC)
dot = vco / p
```

i9xx limits, identical for 915/945/G33 (`intel_limits_i9xx_sdvo`/`_lvds`):
dot 20-400 MHz, **VCO 1.4-2.8 GHz**, n 1-6, m 70-120, m1 8-18, m2 3-7, p1 1-8;
DAC p2 = 10 (dot <= 200 MHz) else 5; LVDS p2 = 14 (single channel) / 7 (dual).
P1 is written **one-hot**: `(1 << (p1-1)) << 16`. Set `DPLL_VGA_MODE_DIS`
(bit 28) always; mode select bit 26 = DAC/serial, bits 27:26 = 10b for LVDS.

**Gen3 DPLL enable quirk** (`i9xx_enable_pll`): write FP0/FP1, write DPLL
without VCO_ENABLE, then with VCO_ENABLE (bit 31), wait 150 Âµs - then **rewrite
the same DPLL value twice more with 150 Âµs waits**. The kernel does this
unconditionally on display ver 3; a driver that skips it gets a PLL that
sometimes doesn't lock.

Output ports:

- **ADPA 0x61100** - the VGA DAC. Enable bit 31, pipe select bit 30, sync
  polarity bits 4:3, DPMS via hsync/vsync-disable bits 11:10.
- **LVDS 0x61180** - mobile parts only. Port enable bit 31, pipe select
  bit 30; channel power: CLKA bits 9:8 = 3, and for 24-bpp panels A3 bits
  7:6 = 3; dual-channel adds B0B3/CLKB. **Read what the VBIOS configured and
  preserve it** - panel wiring (18 vs 24 bpp, single vs dual channel) is not
  discoverable any other way on a part with no VBT parser.
- Panel power sequencing: PP_STATUS 0x61200, PP_CONTROL 0x61204 (power on =
  bit 0), PP_ON_DELAYS 0x61208, PP_OFF_DELAYS 0x6120C.

**VGA plane hand-off - the one dangerous sequence tier-0's successor must
know.** The VBIOS leaves the VGA plane owning scanout. To take over natively:
first set VGA SR01 bit 5 (screen off) through ports 3C4h/3C5h, wait ~100 Âµs,
*then* set bit 31 of **VGACNTRL 0x71400**. Disabling the VGA plane while SR01
still shows the screen on can hang the chip (`intel_vga.c`,
`intel_vga_disable`). Tier-0 never does this - the VBE mode set handles the
VGA plane itself - but any native modeset starts here.

GMBUS (DDC/I2C): GMBUS0-GMBUS5 at 0x5100/0x5104/0x5108/0x510C/0x5110/0x5120.
Pin select in GMBUS0: **2 = VGA DDC, 3 = LVDS panel EDID**. 100 kHz rate,
128-byte EDID read = GMBUS1 command `SW_RDY | CYCLE_WAIT | (128<<16) |
(0x50<<1) | READ`, poll GMBUS2 HW_RDY (bit 11), 4 bytes per GMBUS3 read.
Bit-bang fallback via GPIOA 0x5010 (VGA) / GPIOC 0x5018 (panel).

## 3. Video memory - the UMA model, and why three answers disagree

Four different numbers all describe "how much video memory" this part has, and
a bug report will contain all four. Measured values on this machine in
parentheses:

1. **GGC stolen size** - what the BIOS took from system RAM (**8 MiB**, GGC
   0x0030). This is the physical backing; BSM says where it sits
   (**0x7F800000**, the top 8 MiB of the 2 GiB space).
2. **VBE 4F00h TotalMemory** - the VBIOS's view; less than stolen size, because
   it reserves pages for itself (**7.69 MiB** = 8 MiB minus 256 KiB of GTT for
   a 256 MiB aperture, minus 64 KiB scratch - the numbers reconcile exactly,
   confirming the GTT-in-stolen-memory model on this machine). This is what
   tier-0 believes, floored by `enable16.c` per D2.
3. **GMADR aperture size** - 128 or 256 MiB of address space (**256 MiB**).
   Almost all of it is **unbacked**: a GMADR page reads/writes real memory only
   where the VBIOS built a GTT entry. Mapping generously past stolen memory
   buys nothing and risks touching unbacked addresses.
4. **What a running WDDM/UMA driver claims** (**256 MiB** AdapterRAM) - a
   dynamic allocation figure, unrelated to anything this driver can use.

Consequence, encoded in `intel_hw16.c`: the family maps 16 MiB
(`map_pages 0x00FF,0xFFFF`), the same floor the ati family uses - covering the
largest tier-0 mode's visible bytes plus the DirectDraw heap - and the 4F00h
answer clamps further at run time. **Do not raise the mapping without Phase 0
evidence** of both the stolen size and the VBIOS's GTT coverage.

GTT mechanics, for the native phases only: PGTBL_CTL at MMIO 0x2020 (page
table base | enable bit 0, set by the VBIOS; keep the BIOS's table); PTEs are
32-bit little-endian `physaddr | 1`, written through the BAR3 mapping; on
915/945 the GTT size is implied by the aperture (aperture/4096 entries), on
G33 it is explicit in GGC bits 9:8. Reference: `intel-gtt.c`
(`i830_write_entry`, `i9xx_setup`).

## 4. The panel: LVDS, the fitter, and the 1024x576 non-problem

The netbook's panel is a fixed 1024x576 (WSVGA) LVDS, AUO B101AW01 V2, native
timing 1024x576@60 at 54.20 MHz with 1344x672 totals (measured EDID, confirmed
from two independent paths - the Windows registry and VBE/DDC 4F15h from DOS).

**This section originally assumed the classic 915resolution problem applied.
Measurement says it does not on this VBIOS**, and that assumption's removal is
the most consequential thing Phase 0 produced. The VBIOS publishes the panel's
native mode directly as Intel OEM VBE modes **0x0160 (8 bpp) and 0x0161
(16 bpp)**, both with the linear-framebuffer attribute and PhysBasePtr at the
GMADR base. Tier-0 reaches native resolution pixel-exact with no patching. Full
detail, and the second finding (this VBIOS refuses every mode taller than the
panel, so 800x600 and 1024x768 do not exist here) in
`2026-08-17-intel-gma-phase0-dos-evidence.md`.

What survives from the original reasoning:

- **640x480 still arrives scaled.** It is shorter than the panel, so the VBIOS
  routes it through the **panel fitter** (PFIT_CONTROL 0x61230, PFIT_PGM_RATIOS
  0x61234 - hardwired to **pipe B** on Gen3) and it appears soft and mildly
  aspect-distorted (4:3 source on a 16:9 panel). That is expected-good for that
  mode and a bring-up photograph must be labelled so, or D5 review will misread
  correct behaviour as corruption. The two 1024x576 modes have no such caveat.
- The VGA output (ADPA) is the **unscaled** truth, and remains Phase 3's
  geometry-check path under D5 discipline.

Native modeset (Â§2) is therefore no longer needed to reach the panel at all. It
remains on the roadmap for capability alone - arbitrary refresh, independence
from the VBIOS mode list, and the pipe CRC scanout hash of Â§6. The
915resolution-style route (PAM unlock, BT_3 table patch in the shadow at
0xC0000) is **deleted from the plan rather than deferred**: it was the only step
that wrote outside the IGD, and nothing needs it.

## 5. The 2D engine - ring-buffer-only, and what that changes

Every card this project has driven so far offered MMIO-poked immediate 2D.
**Gen3 has no MMIO-immediate path at all.** All rendering commands - including
plain blits - are dwords written into a **ring buffer** in graphics memory,
consumed after a tail-pointer write. From `gt/intel_engine_regs.h` and
`gt/intel_ring_submission.c` (base 0x2000):

```
RING_TAIL  0x2030   write to submit (8-byte granularity)
RING_HEAD  0x2034   hardware consumption pointer
RING_START 0x2038   4 KiB-aligned GTT offset of the ring
RING_CTL   0x203C   (size_bytes - 4096) | 1 (valid)
HWS_PGA    0x2080   status page physical address (set before use)
```

Init order: HEAD=0, TAIL=0, START, then CTL with the valid bit. Commands from
`gt/intel_gpu_commands.h`:

- `XY_COLOR_BLT` (2<<29 | 0x50<<22): 6 dwords - cmd|write-mask|len,
  BR13 = depth|ROP F0h|dst pitch, (y1<<16|x1), (y2<<16|x2), dst address, color.
- `XY_SRC_COPY_BLT` (2<<29 | 0x53<<22): 8 dwords, ROP CCh, plus src
  coordinates/pitch/address.
- `MI_FLUSH` (0x04<<23) to flush; `MI_NOOP` = 0. Idle = HEAD == TAIL.

Depth field in BR13: 8 bpp = 0<<24, RGB565 = 1<<24, 32 bpp = 3<<24. Addresses
are **GTT/aperture offsets** (the blitter walks the GTT), pitches in bytes,
coordinates 16-bit, pitch < 32 KiB.

Consequences:

- `eng_i9xx.c` is a **32-bit HAL engine with real memory management**: it needs
  a 4 KiB+ ring inside the mapped, GTT-backed region, which the 16-bit side
  must reserve out of the heap the way `dd16.c` computes the VRAM floor. That
  is a design step, not a port of `eng_s3_virge.c`.
- `wait_idle` is ring-drain (HEAD == TAIL after MI_FLUSH), cheap and honest.
- There is no partial engine reset worth having: a wedged Gen3 ring is a
  full-GPU-reset event in the kernel driver. Our recovery story is ring
  re-init or nothing - `intel_backend.c` says so.

## 6. Synchronisation and scanout status

- Vblank: pipe status in PIPEASTAT (0x70024); the VGA-era 3DAh bit 3 also
  works while the VGA plane owns scanout, which at tier-0 it does. The
  32-bit HAL's current `in_vblank` path is S3-specific (defect D1) and stays
  off for this family (`EngineCaps = @()`).
- **Pipe CRC registers exist on Gen3** and hash the actual scanout pixels.
  This is the eventual machine-checkable answer to defect D5 (all current
  matrix checks are GDI-side): program the CRC source, read back per-frame
  hashes, compare against a software CRC of the framebuffer. Native-phase
  work; recorded here so D5's fix has a named mechanism on this family.

## 7. Errata and hazards, ranked

1. **VGA-plane disable without SR01 screen-off first hangs the chip** (Â§2).
   Native phase only; tier-0 immune.
2. **Mode availability depends on the attached display, and is evaluated per
   call.** The mode *list* is a static 36 entries, but 4F01h attributes are
   not: with the panel alone, everything taller than 576 lines reports
   unsupported. Attach a VGA monitor and 800x600/1024x768 may flip to
   supported - so a mode table captured with a monitor attached would refuse at
   stage 9 the moment the machine booted panel-only. **Advertise only what the
   panel alone supports.** Measured; the family's table obeys this.
3. **Panel-fitter-scaled output misread as corruption** (Â§4). Now applies to
   640x480 only, since 1024x576 is native and pixel-exact. Process hazard,
   active from the first boot; mitigated by labelling and VGA-out checks.
4. **DPLL enable without the Gen3 triple-write does not always lock** (Â§2).
   Native phase only.
5. **GMADR access beyond VBIOS GTT coverage touches unbacked addresses** (Â§3).
   Mitigated at tier-0 by the 16 MiB mapping ceiling + 4F00h clamp (measured:
   4F00h answers 7.69 MiB, so the clamp is what actually binds).
6. **DOS-box VBE answers are artefacts** (measured on ati; same mechanism
   here - a WDDM/VESA driver owns the hardware). All baselines from real DOS,
   which is how the Phase 0 survey was run.
7. **2 GB RAM kills stock Win98** before any driver loads: `MaxPhysPage=40000`
   + `MaxFileCache=262144` must be in place before first boot. Platform, not
   GPU, but it gates every other row in this table.
8. **BAR assignments differ between BIOS/DOS and Windows** - measured 1 MiB
   apart for MMIO and the GTT on this machine, with GMADR identical in both.
   Read BARs from PCI config at run time; never cache one across a boot, and
   expect Win98's assignment to be a third answer (`PCIRebalance=1`).
9. **Intel OEM mode numbers are a per-VBIOS fact.** 0x0160/0x0161 are this
   ROM's panel modes; a sibling chip may number differently or lack them. Each
   new chip re-runs the survey before its rows are trusted.

Retired by measurement: the mode-list doubt that made the manifest provisional
(now settled - see Â§4 and the DOS evidence record), and the PAM shadow-write
hazard (the 915resolution route is deleted, not deferred, so nothing in this
family's plan writes outside the IGD).

## 8. Consequences for the family as declared

- Tier-0 with all hooks NULL is not a placeholder, it is the correct driver
  for this part until an engine exists: the VBIOS does the only mode set we
  trust, 4F01h names the right aperture (measured, D0000000 on every supported
  mode), and no register access means no exposure to Â§7 items 1/4/5. Phase 0
  strengthened this - tier-0 also reaches the panel's native resolution, which
  the plan had expected to need a whole native phase.
- **Four modes, not seven**: 640x480 and 1024x576 at 8 and 16 bpp. Measured -
  this VBIOS refuses everything taller than the panel, and publishes the panel
  mode as an OEM number. See Â§4 and the DOS evidence record.
- `pci_match_optional = 0`: strict, per the ati precedent - the permissive
  flag stays unique to the vbe package.
- `EngineType NONE, EngineCaps @()` until `eng_i9xx.c` exists *and is
  measured on this machine* - there is no emulator to measure it anywhere
  else.
- One chip claimed (8086:27AE). The roster in `intel_gma.h` is data-ready but
  unclaimed until someone runs the hardware; G31's function-0 id is unknown
  and stays out until read off a real board.

## 9. Constants recorded for implementation

| Fact | Value | Source |
|---|---|---|
| 945GSE IGD / host bridge / fn1 | 8086:27AE / 8086:27AC / 8086:27A6 | `include/drm/intel/pciids.h`, `915resolution.c` |
| Reference clock | 96 MHz (non-SSC) | `intel_dpll.c` `i9xx_pll_refclk` |
| VCO window | 1.4-2.8 GHz | `intel_limits_i9xx_*` |
| GGC / GMS field | host bridge cfg 0x52, bits 6:4 (+9:8 GTT on G33) | `intel-gtt.c` |
| BSM | IGD cfg 0x5C, 1 MiB aligned | `include/drm/intel/i915_drm.h` |
| PGTBL_CTL | MMIO 0x2020 | `intel-gtt.c` |
| Pipe A/B timing base | 0x60000 / 0x61000 | `intel_display_regs.h` |
| PIPEACONF / PIPEBCONF | 0x70008 / 0x71008 | `intel_display_regs.h` |
| DSPACNTR/ADDR/STRIDE | 0x70180/0x70184/0x70188 (B: +0x1000) | `i9xx_plane_regs.h` |
| DPLL_A / FPA0 / FPA1 | 0x06014 / 0x06040 / 0x06044 | `intel_display_regs.h` |
| ADPA / LVDS / PFIT_CONTROL | 0x61100 / 0x61180 / 0x61230 | `intel_crt_regs.h`, `intel_lvds_regs.h`, `intel_pfit_regs.h` |
| VGACNTRL | 0x71400, disable bit 31, SR01 first | `intel_vga.c` |
| GMBUS block | 0x5100-0x5120, pin 2 VGA / 3 panel | `intel_gmbus_regs.h` |
| Ring PRB0 | TAIL 0x2030 HEAD 0x2034 START 0x2038 CTL 0x203C | `gt/intel_engine_regs.h` |
| XY_COLOR_BLT / XY_SRC_COPY_BLT | 2<<29|0x50<<22 / 2<<29|0x53<<22 | `gt/intel_gpu_commands.h` |
| LGC palette | 0x0A000 / 0x0A800 | `intel_display_regs.h` |
| Panel native timing (this unit) | 1024x576@60, 54.20 MHz, H 1024/48/32/1344, V 576/4/4/672 | measured EDID, AUO B101AW01 V2 |
| This unit's BARs, Windows | MMIO FEA80000, GMADR D0000000 (256 MiB), GTT FEA40000, I/O DC80 | measured (Windows resource map) |
| This unit's BARs, BIOS/DOS | MMIO FE980000, GMADR D0000000, GTT FE940000, I/O DC80 | measured (DOS survey, PCI config) |
| Panel VBE modes (this VBIOS) | 0x0160 = 1024x576x8 pitch 1024; 0x0161 = 16 bpp pitch 2048; both LFB at D0000000 | measured (DOS survey) |
| Stolen memory (this unit) | GGC 0x0030 = 8 MiB at BSM 0x7F800000; 4F00h offers 7.69 MiB | measured (DOS survey) |

## 10. Confidence

**High** (multiple independent sources, register-level): PCI ids except G31;
BAR roles; GGC/BSM locations and encodings; pipe/plane/DPLL/port offsets;
i9xx DPLL limits and math; ring registers and BLT opcodes; the SR01-first VGA
disable; the Gen3 triple DPLL write.

**Measured on this machine, 2026-08-17 - Phase 0 complete.** Windows half via
RMM task 303 (`...-phase0-windows-evidence.md`): PCI ids, subsystem and
revision of all three functions (fn0 27AE confirms the INF claim exactly); the
BAR layout; the panel EDID and native timing. DOS half via `v9xintl.exe` from
real DOS (`...-phase0-dos-evidence.md`): the full 36-entry mode list with
per-mode attributes and LFB base (which produced the family's four-row table
and deleted a planned phase); GGC/BSM/4F00h reconciled to the byte; the DOS-vs-
Windows BAR divergence; VESA 3.0 and the VBIOS identity strings; DDC EDID from
a second independent path; the VBIOS image (55AA, 60416 bytes, PCIR 8086:27A2 -
HP ships the generic 945GM ROM).

**Medium** (inference, or true of the platform but not separately proven here):
the panel fitter is what scales 640x480 (the scaling is certain, the register
path is inferred); LVDS channel/bpp wiring on this panel.

**Unmeasured - one item, and it needs a Win98 boot rather than a survey**:
whether **4F02h with bit 14** actually sets a mode and leaves the linear
framebuffer usable. Every static indicator says yes (VESA 3.0, the LFB
attribute on all six supported modes, PhysBasePtr matching GMADR in both
venues). It is proven or disproven at stage 3/9 of the first boot - Phase 3.
