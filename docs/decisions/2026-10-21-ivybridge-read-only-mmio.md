# Ivy Bridge read-only MMIO on W9QI4E14: CRT on pipe A, the BIOS's GTT, MT forcewake already held

Date: 2026-10-21
Status: **measured twice (identical), nothing written to the device.**
This is step 3 of `docs\plans\intel-ivybridge-gen7.md`, the read-only
half. The machine was on the tier-0 `vbe` driver at 640x480x8
(`2026-10-21-ivybridge-w9qi4e14-tier0-install.md`).

Captures: `docs\probe\w9qi4e14-ivb-hd4000-mmio-2026-10-21\V9XIVMM-run1.INI`
and `-run2.INI`, byte-identical.

## The tool

`V9XIVMM.EXE` + `IVMM.VXD` are new: `tools\diag\ivb_mmio_probe.asm`,
`ivb_mmio_probe_win32.c`, built by `scripts\build-ivb-mmio-probe.ps1`.
They are the Gen4 probe's design:

- an allowlist of windows in the VxD;
- two captures, the second only for the pipes, planes and transcoders the
  first found enabled;
- no scanline or frame counters (the 945 hang);
- the GTT copied and counted.

What differs on IVB:

- **No forcewake.** Only offsets i915 reads without the GT awake: display
  and PCH, `40000h`-`115FFFh` (`intel_uncore.c` `NEEDS_FORCE_WAKE`). The
  one exception is the three forcewake acknowledge registers, which i915
  reads raw before it takes forcewake. RC6, ECOBUS and the rings are not
  read.
- **The panel fitters' coefficient index and data ports are excluded.** A
  data-port read may step the index.
- **Layout.** GTTMMADR is 4 MiB with the GTT at +2 MiB. The GTT is sized
  from GGC GGMS. The first 16,384 entries (64 MiB) are copied.

Run from `C:\V9XDIAG` through the agent: exit 0 in 121 ms, then 175 ms.
The machine stayed up. Build `f9ed5b3b-dirty` (the probe's own sources
uncommitted at the time).

## The display: CRT, pipe A, transcoder A, 640x480

| Register | Value | Reading |
|---|---|---|
| `PIPE_CONF_A` 70008h | `C0000000` | enabled, active. B and C `00000000` |
| `PIPE_HTOTAL/VTOTAL_A` | `031F027F` / `020C01DF` | 800x525 total, 640x480 active: the VBE 640x480 timing |
| `PIPE_SRCSZ_A` 6001Ch | `027F01DF` | 640x480 source |
| `PRI_CTL_A` 70180h | `C8000000` | enabled, gamma on, format 0010b = 8 bpp indexed |
| `PRI_STRIDE_A` / `PRI_SURF_A` / `PRI_LINOFF_A` | `00000280` / `00000000` / `00000000` | stride 640; surface at graphics address 0, no offset |
| `PRI_TILEOFF_A` / `PRI_SURFLIVE_A` | `00000000` / `00000000` | |
| `PF_CTL_0` 68080h | `80800000` | panel fitter 0 on, pipe A, filter bit 23 |
| `PF_WIN_SZ_0` / `PF_WIN_POS_0` | `028001E0` / `00000000` | 640x480 at 0,0: fitting 1:1 |
| `FDI_TX_CTL_A` / `FDI_RX_CTL_A` | `80044F02` / `80002F50` | FDI A up; B and C off |
| `TRANS_CONF_A` F0008h | `C0000000` | PCH transcoder A on, timings equal pipe A's |
| `TRANS_DP_CTL_A` E0300h | `60000018` | port select 11b = none, as for an analogue port |
| `PCH_DPLL_A` C6014h | `C4800080` | enabled; `DPLL_SEL` `00000008`: transcoder A on DPLL A |
| `DAC_CTL` E1100h | `83C40000` | CRT on, transcoder select 00b = A |
| `HDMI_CTL_B/C/D`, `DP_CTL_A-D`, `LVDS_CTL` | bit 31 clear | off |
| `VGA_CONTROL` 41000h | `8000298E` | bit 31: VGA plane disabled |
| `WM0_PIPE_A/B/C` | `00783818` | LP1-3 all 0 |
| `RAWCLK_FREQ` C6204h | `0000007D` | 125 |

The AOC monitor is on the **VGA connector**, not the DVI-D the VBT
lists. The path is CPU pipe A, plane A, panel fitter 0, FDI A, PCH
transcoder A, DPLL A, the DAC. This is the ILK/SNB/IVB split display: on
Haswell, the 4570 drives its output through a DDI. The plane, pipe and
surface registers are where the plan said they would be.
`PRI_LINOFF_A` holds the linear offset, and `PRI_TILEOFF_A` is zero.

## The GTT

| | |
|---|---|
| Table | 2 MiB, 524,288 entries (GGMS 2); 269,759 valid in the whole table |
| Entries 0 to `3FFE` | valid, linear over stolen from BDSM `DBA00000`: 16,383 pages, 64 MiB less 4 KiB |
| Entry `3FFF` | `97E0CC17`: valid, cacheability 11b, bits 11:4 = `C1`. Not a stolen page; not explained |
| Cacheability in the stolen run | **00b on every entry** |
| HSW open's check (entries 6144-6175 under mask `FFFFF7F1`) | **32 of 32 match**; entry 6144 = `DD200001` = BDSM + `1800000h` |

The BIOS writes its own entries with cacheability 00b. IVB PRM Vol 1 Pt 2
2.2.2.1 lists that value as "Reserved". So the plan's concern that HSW's
`UC = 0` encoding writes a reserved value on IVB is real in the PRM's
terms, but it matches what this BIOS does. Which value Velocity9x should
write for its own entries stays a decision for the PTE encoder, made
against the PRM rather than the BIOS.

The Haswell engine open's GTT check would pass on this machine. Whether
the top of stolen memory is CPU-writable, which was the 4570's B1
finding, is not something reads can say.

What maps the 253,375 valid entries past the copy is not recorded. The
probe counts them but copies only the first 64 MiB.

## Forcewake: MT forcewake is held, by someone else

| Register | Value |
|---|---|
| `130040h` FORCEWAKE_MT_ACK | `00000001` |
| `130044h` (HSW's acknowledge) | `00000000` |
| `130090h` FORCEWAKE_ACK (legacy) | `00000000` |

All four reads (two per run, two runs) agree. Nothing of Velocity9x
touches the GT at tier 0, so the BIOS or VBIOS left MT forcewake bit 0
acknowledged.

Two consequences for the forcewake step:

- The MT acknowledge responds on this part. That points to MT forcewake
  being the variant in use, but ECOBUS bit 5 is the deciding read, and it
  needs the GT awake.
- The Haswell open refuses when the acknowledge is already set (refusal
  5). With IVB's acknowledge address and the same rule, the open would
  refuse on this machine as it boots. The forcewake step has to decide
  what a held acknowledge means here before it writes anything.

## Not measured

- Every GT register: RC6 state, ECOBUS, the rings, GFX_MODE. That needs
  the forcewake handshake.
- Whether the BIOS's register state changes at 16 or 32 bpp or at
  1920x1080.
- The GTT beyond the first 64 MiB.
