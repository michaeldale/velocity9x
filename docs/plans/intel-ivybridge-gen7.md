# Ivy Bridge (Gen7, HD 4000): bring-up on W9QI4E14

Status: **audit done 2026-10-21; no code.** The machine that produced the
survey is on remote help as `M-C71087` and runs Windows 98 SE on standard
VGA. The config-space decode from Windows is accepted by the Haswell code
unchanged (`docs\decisions\2026-10-21-ivybridge-w9qi4e14-audit.md`).
Nothing is implemented, and `8086:0162` stays with the `vbe` family until
a chip entry exists.

Parked 2026-10-20 for want of hardware; resumed when the owner lent the
machine.

## Machine

W9QI4E14: MSI B75MA-P45, i5-3570K, HD 4000 `8086:0162` rev 09 SUBSYS
`1462:2111`, 4 GiB, AOC 2769 at 1920x1080. Windows 98 SE built with
98QuickInstall, RLoew AHCI, NDIS2 RTL8168, agent 0.12.0. The owner has
said to go ahead, and remote help is on (`remote-help-no-chat-needed`):
install, reboot and test without asking.

Full table and captures are in the audit doc.

## Evidence so far

- **DOS survey, 2026-10-19:**
  `docs\probe\references\msi-b75ma-p45-ivb-hd4000-vgasurv-2026-10-19-16503bf2.ini`.
  - VBE 3.0 lists 15 usable modes up to 1920x1080 at 8, 16 and 32 bpp, with
    the LFB at `E0000000`. `0103` (800x600x8) has an 832-byte pitch, and
    the 24 OEM modes return attributes 0.
  - The VBIOS PCIR id is `0102` (Sandy Bridge), so identify the chip by its
    PCI id only.
  - EDID block 1 reads back block 0.
  - `4F11h` is unsupported.
- **Windows audit, 2026-10-21:** `docs\probe\w9qi4e14-ivb-hd4000-audit-2026-10-21\`.
  - `V9XHSW.INI`: GGC `0211` (64 MiB stolen, 2 MiB GTT), BDSM `DBA00000`,
    BGSM `DB800000`, BAR0 `F7800000`, BAR2 `E0000000`, MSAC 256 MiB, TOM
    4 GiB. The Haswell decode returns `Status=0000 Refused=0000`.
  - The VBT shows CRT plus one DVI-D on port B.
  - ASLS moved from `D9414018` to `D9415018` between the two reads.

## What a port changes

The documents and the full comparison are in
`C:\everything\claude\personal\intel driver research\ivybridge-research-notes.md`.
That folder holds the IVB PRM, the 3rd-gen Core and 7 Series PCH
datasheets, Mesa genxml and crocus, and Linux i915.

### Code audit against the `hd4600-hsw` path

Sites named by file and symbol, because the `intel-gma` files are being
edited for the GM965 at the same time. "Same" means checked against the
IVB PRM or Mesa genxml and found identical.

| Area | Where | IVB |
|---|---|---|
| Identity | `intel_backend.c` strict device list; `intel_hw16.c` `v9x_intel_is_haswell` gate; `hsw/hsw_hw16.c` descriptor; `family.psd1` chip entry; `backend_registry_table.inc`; `check-tree.ps1` Direct3D-word rule; engine type `INTEL_GEN75` | New chip entry, descriptor, Direct3D word and engine type (or a Gen7 variant of GEN75). Every `v9x_intel_is_haswell` caller decides whether IVB takes the same branch |
| GGC decode | `hsw_config.c`, `V9X_INTEL_HSW_GGC_GMS_MAX` = `10h` | Accepts this machine (GMS 2). IVB adds GMS `11h` = 1 GiB (326765), which the decode refuses |
| BARs, BDSM/BGSM, MSAC, GTT reach | `hsw_config.c`, `runtime.asm` `V9xPciReadHswConfig` | Same, measured |
| PTE encode | `hsw_config.c` `v9x_intel_hsw_encode_pte`, `intel_hsw.h` cache values | **Changes.** IVB: 11:4 = addr 39:32, bit 3 GFDT, 2:1 cacheability, `00` reserved. HSW's UC = 0 is reserved on IVB |
| PTE check at open | `loader.asm`, `V9X_HSW_PTE_MASK` `FFFFF7F1` over entries 6144-6175 | The mask still works (bits 11:4 are zero below 4 GiB, 3:1 masked). Whether the BIOS maps stolen memory linearly there is unmeasured |
| Forcewake | `loader.asm` `V9X_HSW_FW_REQUEST` `A188h`, `V9X_HSW_FW_ACK` `130044h`; `tools\diag\hsw_mmio_probe.asm` `HSW_FW_ACK` | **Changes.** IVB acks at `130040h`, or uses legacy `A18Ch` / `130090h` when ECOBUS `A180h` bit 5 is clear (i915). The HSW open would time out (refusal 6). The MMIO probe has the same constant, so V9XHSWMM cannot be run on IVB as built |
| Ring registers | `intel_hsw.h` RCS `2030h`-`203Ch`, BCS `22030h`-`2203Ch` | Same |
| Reserve, status page, slots | `intel_hsw.h` reserve at aperture `1800000h`, 128 KiB | Inside the 64 MiB stolen; the 4570's "top megabyte not CPU-writable" is unmeasured here |
| Plane flip | `hsw_display.c` page plus x-offset split; `hsw_scanout.c` writes `V9X_INTEL_HSW_DSPAOFFSET` `701A4h` | **Changes.** On IVB `701A4h` is PRI_TILEOFF, ignored for linear; linear panning is PRI_LINOFF `70184h`, in bytes |
| Plane, pipe, scanline | `intel_hsw_display.h` VTOTAL `6000Ch`, DSPCNTR `70180h`, STRIDE `70188h`, SURF `7019Ch`, SURFLIVE `701ACh`, PIPEADSL `70000h`, PIPEACONF `70008h`; DSL + 1 | Same (both PRMs; i915 `intel_vblank.c`). `6000Ch` is `PIPE_VTOTAL_A` in both PRMs |
| Blitter | `hsw_3d.c` `XY_COLOR_BLT`, `XY_SRC_COPY_BLT`, BR13 | Same encoding. The byte pitch unit was measured on the 4570 only |
| MI commands | `hsw_3d.c` NOOP, BATCH_BUFFER_END/START, STORE_DATA_IMM, LOAD_REGISTER_IMM | Same (HSW only adds BB_START fields the code leaves 0) |
| 3DSTATE lengths | `hsw_3d.c` command table | Same for every command in it (genxml diff) |
| SURFACE_STATE DW7 | `hsw_3d.c` `out[7]` channel selects R/G/B/A 4/5/6/7 | **Changes.** No channel selects on IVB; DW7 is built per chip |
| 3DSTATE_PS DW4 | `hsw_3d.c` `V9X_HSW_PS_MAX_THREADS_FIELD` at 31:23, `V9X_HSW_PS_SAMPLE_MASK_ONE` | **Changes.** IVB max threads 31:24, no sample mask; GT2 has 172 PS threads (Mesa) |
| URB, push constants | `hsw_3d.c` start 2, VS 64 entries, 8 + 8 KiB | Fit IVB GT2: VS 32-704 entries, 16 KiB push constants; narrower fields, same values |
| Depth formats | `intel_hsw_3d.h` D32F 1, D24X8 3, D16 5 | Same ("All" in the IVB PRM) |
| SAMPLER_STATE, border colour | `hsw_3d.c`, `d3d_hsw.c` 0x40 zero bytes | Same layout; the all-zero border colour is valid on both |
| EU encoding and sends | `hsw_eu.c`; SFIDs 2 and 5; RT write type 17:14; sampler fields | Same on Gen7 (Mesa `brw_inst.h`). `pln` exists on IVB |
| PIPE_CONTROL sequences | `hsw_3d.c` init and draw batches | **Add** the IVB-only rules: the VS flush before VS, URB_VS and CONSTANT_VS; a CS stall after PUSH_CONSTANT_ALLOC; a CS stall on every 4th PIPE_CONTROL; no depth stall combined with an RT or depth-cache flush |
| GT workarounds | none in the tree | i915 `ivb_gt_workarounds_init`: COMMON_SLICE_CHICKEN1 RHWO off, L3CNTLREG1 and the L3 chicken mode, L3SQCREG4. The 4570 runs without any; whether IVB does is unmeasured |

Of these, the decode (GMS `11h`), the PTE encoder, the forcewake choice
(as policy over the ECOBUS read), the LINOFF split and the PS and
SURFACE_STATE words are pure logic. Each can be written and host-tested
from the PRM before the machine runs it.

## Order

Following `intel-haswell-bringup.md`:

1. **Done.** The survey from DOS and the read-only config audit from
   Windows.
2. **Tier-0 `vbe`** on W9QI4E14. *Installed 2026-10-21 from `1cc25342`;
   the first restart had not come back after 45 minutes
   (`docs\decisions\2026-10-21-ivybridge-w9qi4e14-tier0-install.md`).*
   Install the `vbe` package. Record the
   modes it publishes, the pitch from 4F01h and whether 1920x1080 sets,
   and `enable-ok`. This boot also answers whether Windows' restart path
   works here (the netbook's does not).
3. **Read-only MMIO.** First an IVB build of the MMIO probe, with the
   forcewake protocol chosen from ECOBUS. Then from Windows: ECOBUS, which
   forcewake variant acknowledges, RC6 state (`A090h`), the pipe and
   plane in use, whether the AOC is on CRT or DVI, and the GTT head
   against BDSM.
4. **Chip entry and 2D:** the identity rows, the decode and PTE changes,
   forcewake, then BCS ring and blits as on the 4570. Re-measure the
   reserve placement, the top of stolen memory and the blit pitch unit.
5. **Flips:** the LINOFF split, measured with the /flip probe.
6. **3D:** the PS and SURFACE_STATE words and the IVB PIPE_CONTROL rules.
   Then the Phase C-F sequence the 4570 took, with its hang cases
   (all-zero WM/PS, the VF cache) checked again here.

## Not to assume

Everything the 4570 measured is a Haswell fact until this machine repeats
it:

- reserve placement
- the unwritable top megabyte of stolen memory
- the BIOS GTT mapping
- the blit pitch unit
- the RT-write type position
- the hang cases
- the 768-byte kernel slots
- which forcewake variant the part uses
