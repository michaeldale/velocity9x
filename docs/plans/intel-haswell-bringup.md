# Haswell (Gen7.5) on the i5-4570: tier-0, the layout, then the ring

Status: **in progress. The gate and Phase 0 passed on 2026-10-17, and
Phase 1's decode accepts the measured config space** (Windows 98 SE boots,
tier-0 `enable-ok` at 640x480x8:
`docs\decisions\2026-10-17-haswell-tier0-enable-ok.md`; config space in
`docs\probe\hsw-4570-config-2026-10-17\`). The machine is an i5-4570 desktop
(HD Graphics 4600, GT2, `8086:0412`). Phase 2 is next.

Why Haswell and not the i7-6700 (Skylake, Gen9) also available: the
reasoning, with the PRM passages, is in
`C:\everything\claude\personal\intel driver research\haswell-skylake-research-notes.md`.
In short: an 8-series board is the more plausible Windows 98 host (EHCI, and
probably SATA IDE mode, neither confirmed on this board), and Haswell's GTT
and submission are the shapes the Gen3 code already uses - 4-byte PTEs and
plain ring buffers. Skylake's GTT entries are 8 bytes and its ring mode sits
beside execlists.

## Documentation

All Intel, all public, all in `C:\everything\claude\personal\intel driver research\`:

| Folder | What |
|---|---|
| `haswell-2013-gen7.5-prm/` | HSW PRM Vol 1 to 7, 10, 11a, 11b, 12 |
| `haswell-2013-datasheets/` | Desktop 4th Gen Core datasheet Vol 1 (328897-010) and Vol 2 (328898-003) |

The PRM's Vol 12 lists GGC but not the GMS encoding; datasheet Vol 2 3.1.13
has the table. The datasheet's BDSM and BGSM prose still cites the Gen3
location ("offset 52 bits 7:4") while its own field tables say 50h bits 7:3
and 9:8; the field tables are used.

## The gate

**Windows 98 SE boots on the i5-4570.** Things to set or check, none tried:

- SATA in IDE/legacy mode, CSM on, legacy USB emulation on (or PS/2).
- RAM limited for Windows 98 (`MaxPhysPage` or a patch).
- A network card the v9x agent can use.

Nothing below is buildable-on until this passes.

## Phase 0: tier-0 VBE, no code

The `vbe` family matches `PCI\CC_0300`. Install it and confirm the desktop,
the VBE mode list and the linear framebuffer against GMADR, exactly as the
Gen4/5 plan's Phase 0. Two things to record: the pitch from 4F01h (do not
compute it) and whether the VBIOS describes a widescreen native mode.

## Phase 1: the layout, from a survey (decode done)

V9XSURV already dumps the IGD's full config space and the host bridge's. A
report from real DOS on the 4570 answers every Phase 1 question:

| Fact | Where | Decode |
|---|---|---|
| Data-stolen size | GGC 00:00.0 50h (mirror 00:02.0 50h), GMS 7:3, 32 MB units | `v9x_intel_hsw_decode_ggc` |
| GTT-stolen size | GGC GGMS 9:8, 0/1/2 MB | same |
| MMIO, GTT | BAR0 4 MB, 64-bit; GTT at BAR0 + 2 MB | `v9x_intel_hsw_decode_layout` |
| Aperture size | MSAC 00:02.0 62h bits 2:1 | `v9x_intel_hsw_aperture_bytes` |
| Stolen base | BDSM 00:02.0 5Ch; cross-check BGSM 00:00.0 B4h = BDSM - GGMS | layout |
| PTE | Vol 5: 31:12 addr, 11 CC3, 10:4 addr 38:32, 3:1 CC2:0, 0 valid | `v9x_intel_hsw_decode_pte`, `_encode_pte` |

`src\chipsets\intel\hsw_config.c` with `tests\host\test_intel_hsw.c`. The
tests run against an invented desktop configuration; **the first survey
replaces it with the measured one**, and any refusal the decode raises on real
words is a finding for a decision doc, not a test to adjust.

The decode refuses rather than guesses: a GMS above 10h, GGMS 3, a 32-bit or
above-4 GB BAR, MSAC 10b, a GTT too small for the aperture, and a BGSM that
disagrees with BDSM minus GGMS.

## Phase 2: read-only MMIO, from Windows

Needs a ring-0 read of BAR0, as the Gen3 fingerprint does. Questions:

- **Forcewake.** HSW Vol 2c (ring buffer mode of scheduling): "SW must set
  the Force Wakeup bit to prevent GT from entering C6" before initialising
  the render ring, and clear it afterwards. Vol 12 points at MULTIFORCEWAKE,
  0xA188. A GT register read while the GT is in RC6 may not return the
  register; whether the BIOS leaves RC6 enabled is the first thing to read.
- The display pipe in use and its plane registers (Vol 11a), for the
  fingerprint: which DDI drives the monitor, which pipe, the plane's surface
  address in GTT space.
- The GTT's first entries, compared with stolen memory: the BIOS's mapping of
  the VBE framebuffer should be stolen-backed PTEs.

## Phase 3: ring and blits

The Gen3 order again: identity, GTT inventory, ring with `MI_NOOP` and
`MI_STORE_DWORD_IMM`, then `XY_COLOR_BLT` / `XY_SRC_COPY_BLT`. Haswell's
blitter has its own ring (BCS, Vol 6 `BCS_RINGBUF`), so the blits do not have
to touch the render engine's power-context requirements at all. That is the
reason to do blits on BCS first.

## Phase 4: 3D, its own plan

No fixed-function pixel pipe: URB, binding tables, `SURFACE_STATE`,
`SAMPLER_STATE`, VS and PS kernels in EU ISA (Vol 7). The render ring also
needs the dummy-context initialisation the Vol 2c note describes. A separate
plan, written once Phases 1 to 3 have produced evidence. Mesa's `crocus`
(Gen4 to Gen7.5) is the open reference and also covers the X61 Gen4 work in
`intel-gen4-gen5-bringup.md`.

## Family

One family per vendor: Haswell goes into `intel-gma` when it has a chip
entry, as per-chip data. It gets no entry until Phase 1's survey exists, so
the Gen3 engine can never be armed on it by accident. Until then the `vbe`
family is the driver.

## Order

1. Windows 98 SE on the 4570.
2. `vbe` tier-0; record the modes.
3. V9XSURV from real DOS; run the decode over the report; decision doc.
4. Read-only MMIO from Windows: forcewake state, display pipe, GTT head.
5. BCS ring and blits.
6. 3D plan.
