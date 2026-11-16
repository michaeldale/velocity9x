# Oland XT on W9QI0DAD: a survey, the row bound, and SI's DMA engine drawing

Date: 2026-11-16

Status: **measured, one machine, one boot of the ati row.** W9QI0DAD
(10.0.1.172, AMD FX-6300, Gigabyte 790GX/SB700, Win98 SE, agent 0.18.0),
where Michael swapped the GeForce 210 for an AMD Oland card. Plan:
[`../plans/ati-oland-gfx6.md`](../plans/ati-oland-gfx6.md), phases 0 to 5.
Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/`](../probe/oland-w9qi0dad-2026-11-16/).

## The card

Read by the agent's `inventory`, the devnode's registry keys and
`V9XSIMM` (boot 48, vbe package):

| | |
|---|---|
| PCI id | `1002:6610` rev 00, subsystem `1028:2120` (a Dell board). Linux: Oland XT |
| Slot | bus 1, device 0 (`PCI\...\001000`), class key `Display\0002` |
| BAR0 | `D000000C`: 64-bit, prefetchable, 256 MiB, high half 0 |
| BAR2 | `FDD80004`: 64-bit, 256 KiB register window, high half 0 |
| BAR4 | I/O `EE00` |
| VRAM | `CONFIG_MEMSIZE` 800h = 2048 MiB, not GDDR5 (`MC_SEQ_MISC0` type 11) |
| Shader array | `CC_GC_SHADER_ARRAY_CONFIG` FFC00000: 6 CUs active, the full die |
| VBE | 3.0, OEM rev 0F2A, 29 modes to 1920x1080x32, LFB `D0000000`, reports 16 MiB (its field's cap) |

The plan's stop condition (BAR0 above 4 GiB) does not apply: the BIOS
placed both 64-bit BARs below 4 GiB.

## What the BIOS leaves (V9XSIMM, read-only)

`Result=COMPLETE`, all 176 offsets accepted. From its `[Decode]`:

- `MC_VM_FB_LOCATION` F47FF400: VRAM at MC `F4_00000000`-`F4_7FFFFFFF`.
  `HDP_NONSURFACE_BASE` F4000000 is the same start, so **BAR0's first
  byte is VRAM's first** (`Bar0AtVramStart=yes`), which the ring window at
  BAR0 + 240 MiB assumed.
- **VM L2 and contexts 0 and 1 off**; the L1 TLB on. The DMA engine's
  MC addresses are therefore physical, as `si_dma.c` assumed.
- DMA0 and DMA1: not in reset, idle (`DMA0_STATUS_REG` 44C83D57), rings
  disabled.
- CP: not in reset, PFP/CE/ME halted (`CP_ME_CNTL` 15000000). RLC not
  running. GFX powered and clocked, no power gating.
- `GB_ADDR_CONFIG` reads 0, as Mullins' did: gpu_init (Phase 6) must
  write it from `si.c`'s Oland values, not read it.
- D1 scans out linear (array mode 0) 16 bpp, pitch 1024 px, at
  `F4_00000000` (offset 0 in BAR0), matching the VBE mode. D2's CRTC is
  off.

## Phase 0: the vbe package, tier 0

Before this card the box ran the nvidia build for the GT218, which
refused the AMD id (`Stage=fail-hardware-present`); Windows had fallen
back to `vga.drv` with `BitsPerPixel=4`, the known card-swap trap. The
vbe package built from `e5dbf0ba` (build id `oland-p0-vbe`) went in by
WININIT rename (DRV, VXD, HAL, SETP, SHA-256 matched), with
`BitsPerPixel` 16 and `Resolution` 1024,768 set by the agent. Boot 48:
`Stage=enable-ok`, 1024x768x16, write-combining MTRR on BAR0
(`MtrrWrite=ok s=4 b=d0000001`). V9XDDP `Result=COMPLETE`.

## Phases 1, 3, 4 and 5: the ati row, first run

The row written on `ati-oland` on 2026-11-12 had been keyed to 6613 and
stashed for lack of `_TEXT`. On `ati-oland-bringup` (main `e5dbf0ba`
merged with `ati-oland`, Oland's cap bit moved to `40000000h` because
RV610 took `20000000h`), the row was retargeted to 6610 and fits: `_TEXT`
0xDF75, 57,205 of 57,344 bytes. Package built by `run-checks.ps1` as
`546fd516-dirty`, the tree committed as `21990844`; DRV, VXD, HAL and SETP
by WININIT rename, hash-checked. The class key's `V9xFamily` still says
`vbe`: the agent refuses writes to that key.

Boot 49:

- `Stage=enable-ok`, 1024x768x16. V9XHW.INI: `Adapter=AMD Radeon Oland
  XT`, `OlandEngine=engine`, `OlandBar0=D0000000`, `OlandBar2=FDD80000`,
  `EngineCaps=40000007` (Oland identity, fill, copy, flip).
- **V9XDDP** (three runs: desktop at 16 bpp, desktop at 32 bpp, and after
  `/modestress`), each `Result=COMPLETE` with `BltFillPixelOk`,
  `SrcCopyPixelOk`, `KeySrcFillPixelOk` and all four `Overlap*PixelOk`
  = 1. V9XDDP's exclusive tests set 640x480x16 each time, so **no engine
  draw at 32 bpp has been tested.**
- **V9XTRACE** after the third run: `EngineType=12` (ATI_VI),
  `CountBltEngine` 9 of `CountBlt` 21, `EngineFifoTimeouts`,
  `EngineIdleTimeouts` and `EngineResets` all 0, builds agree.
- **Flips:** `FlipHandled` 69, `FlipTakenAtDone` 69. `Flip20Ms` 325-333
  ms, about one refresh per flip at 60 Hz. `FlipPixelOk=0` is expected
  with real flips.
- **Mode changes:** `/modestress` (32 VBE round trips) completed; the
  plain V9XDDP run after it found the DMA engine still claimed and
  drawing. The desktop at 1024x768x32 afterwards is intact
  (`desktop-boot49-32bpp.png`).

### What this does and does not settle

- SI's DMA ring started from the BIOS state above without microcode, as
  Linux's `si_dma_start` order says, and its fences landed.
- The byte-count copy (bit 26, `amdgpu si_dma_emit_copy_buffer`) drew
  copies whose sampled pixels are right. V9XDDP samples pixels; it does
  not compare whole surfaces byte for byte, so an overrun past a copy's
  end is **not ruled out** by this run. The plan's Phase 3 byte-exact
  write probe was skipped.
- Nine engine blits is a small sample. No GDI on the engine (`GdiAcceleration=none`).
- Overlapping copies go to the CPU by design (no byte copy has been
  measured overlapping itself).

## Not done

- A byte-exact DMA fill/copy check (a probe or dispbench run), and
  engine draws at 32 bpp.
- Phase 6 (GFX6 CP and Direct3D): its own plan. Direct3D on this row is
  `none`.
- The class key's `V9xFamily`, the INF install on this devnode, and
  the engine clocks (SI has no SMU path here).
