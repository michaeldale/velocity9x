# FirePro W4100, W7000 and W9000 (Cape Verde, Pitcairn, Tahiti) claimed on the Oland's path, from DOS surveys

Date: 2026-11-28

Status: **claimed unrun. Nothing in this record has run on any of the
three cards.** The `ati` package binds `1002:682C`, `1002:6808` and
`1002:6780` from 1.1.0, tier Recognised. Source: GitHub issue 15, three
`V9XSURV` reports (builds `8e6b7bd6` and `275369a1`, October 2026) from
one owner, who runs the `vbe` package on all of them. A fourth card in
the same issue, a FirePro V7900, is Cayman and out of scope here.

The real risk is the one the Oland never had: Pitcairn and Tahiti have
two shader engines, and Cape Verde, Pitcairn and Tahiti two shader
arrays in each. Every place our GCN 1 code assumed one of each is listed
below with what replaced it. The replacement is Linux's code, host-tested
against hand-worked cases; it is not measured.

## What the surveys show

| | W4100 | W7000 | W9000 | Oland XT (W9QI0DAD) |
|---|---|---|---|---|
| PCI id, rev | `1002:682C` 00 | `1002:6808` 00 | `1002:6780` 00 | `1002:6610` 00 |
| Subsystem | `1028:2B1E` (Dell) | `103C:030C` (HP) | `1002:031E` | `1028:2120` |
| BAR0 | `D000000C` | `D000000C` | `E000000C` | `D000000C` |
| BAR1 (BAR0 high) | 0 | 0 | 0 | 0 |
| BAR2 | `FE4C0004` | `EFE00004` | `FBD00004` | `FDD80004` |
| BAR4 (I/O) | `B000` | `E000` | `E000` | |
| VBIOS part string | `C755 Verde GL PRO 2G GDDR5` | `Pitcairn A1 GLXT GD5 128Mx16 4GB` | `Tahiti B0 GL XT C38801 GDDR5 6GB 500e/150m` | |
| VBE | 3.0, 16 MiB reported | 3.0, 16 MiB | 3.0, 16 MiB | 3.0, 16 MiB |
| Bus position | bus 2, behind a PLX 8112 PCI-to-PCIe bridge on an Intel 865 board (an 865G IGD also present, the W4100's VBIOS at C000h) | bus 1, the CPU's root port | bus 0Ch, behind a PLX 8749 switch | the chipset's root port |

Every BAR passes `V9xPciReadSiBars`' checks as the Oland's do: BAR0 a
64-bit memory BAR whose high half is zero and that is 256 MiB aligned;
BAR2 a 64-bit memory BAR at or above 16 MiB, 256 KiB aligned, high half
zero. Each card's VBE modes report its BAR0 as the linear frame buffer.

What a DOS survey cannot show, and the Oland path assumes:

- **BAR0's size.** The ring window is BAR0 + 240 MiB, so BAR0 must be
  256 MiB. On the Olands V9XSIMM read it; here only the alignment is
  known.
- **VRAM at BAR0's start** (`HDP_NONSURFACE_BASE` = `MC_VM_FB_LOCATION`'s
  start) and the VM off.
- **The fused-off units.** AMD's specification for the W4100 gives 512
  stream processors, which would be eight of Cape Verde's ten CUs, a
  harvested die; not read here. The disables are now read at run time
  (below), so nothing is assumed either way.
- **The DRAM row size**: also read at run time now.
- **Which display controller the VBE drives.** Flips write D1's
  surface address (`eg_scanout.c`) and are taken only while D1's CRTC
  and graphics plane are on. The W7000 and W9000 have DisplayPort
  outputs only. If the BIOS lit another controller, flips decline,
  which is safe; whether one is lit but not scanning the desktop is
  unknown.

## Per chip, from Linux

Sources: amdgpu `si.c` (SHA-256 `7f6c0e6d...e3d`) and `gfx_v6_0.c`
(`28aff5db...62f`), the files `gen-oland-gpu-init.ps1` already hashed.

| `gfx_v6_0_constants_init` | Cape Verde | Pitcairn | Tahiti | Oland |
|---|---|---|---|---|
| shader engines x arrays (SH) per SE | 1 x 2 | 2 x 2 | 2 x 2 | 1 x 1 |
| CUs per SH, at most | 5 | 5 | 8 | 6 |
| RBs per SE | 4 | 4 | 4 | 2 |
| tile pipes | 4 | 8 | 12 | 4 |
| texture channel caches | 4 | 8 | 12 | 4 |
| `sc_prim_fifo_size_backend` | 40h | 100h | 100h | 40h |
| GB_ADDR_CONFIG golden | `12010002` (VERDE) | `12011003` (TAHITI) | `12011003` (TAHITI) | `12010002` (VERDE) |
| GB_ADDR_CONFIG with a 4 KiB row | `22010002` | `22011003` | `22011003` | `22010002` |
| `gfx_v6_0_raster_config` | `00001240` | `2A001260` | `2A001260` | `00000080` |
| tile table | `CHIP_VERDE` | `CHIP_TAHITI`/`PITCAIRN` | the same | `CHIP_OLAND` |
| golden lists, graphics entries | 18, 6, 80 | 16, 6, 80 | 19, 6, 110 | 16, 6, 60 |

`max_gprs` (256), `max_hw_contexts` (8, so ME_INITIALIZE's 7) and the
clear state (`clearstate_si.h` serves every SI chip) are the Oland's on
all three. `max_gs_threads` (32, the Oland 16) is not used by anything
here.

The golden lists are generated per chip by `gen-oland-gpu-init.ps1`,
graphics entries only as for the Oland. Two lists Linux applies to these
chips and not the Oland, `tahiti_golden_registers2` (`MCIF_MEM_CONTROL`)
and `verde_pg_init` (123 GMCON power-gating writes), hold no graphics
entry; the generator asserts that and names them as left out.
`tahiti_golden_rlc_registers` names `mmUVD_CGC_GATE`, whose header
(`uvd_4_0_d.h`) the cached tree lacks; it is the video decoder's and is
left out by name.

The tile indices Direct3D uses mean the same in all three tables: 4 is
1D-tiled depth, 8 linear aligned, 13 1D-tiled thin (BC textures). A 1D
layout does not depend on the pipe configuration, which is the field
Tahiti's and Pitcairn's tables change (`P8_32x32_8x16` for `P4_8x16`).
Cape Verde's and Tahiti's mode 8 is plain `ARRAY_LINEAR_ALIGNED`; the
Oland's carries bank and split fields as well.

## Microcode

linux-firmware `amdgpu/`, downloaded 2026-11-28, every header 1.0 and
every image the Oland's size. Byte-identical files are emitted once and
shared, which `gen-oland-gfx-microcode.ps1` asserts:

| image | Cape Verde | Pitcairn | Tahiti |
|---|---|---|---|
| RLC | its own, `50c69aef...0fa0`, v 1Ch | its own, `929f722b...ee92c`, v 7 | its own, `46da3468...bac3`, v Dh |
| PFP | its own, `a76ae8ae...158e`, v 54h | its own, `aabdb871...4b5b`, v 54h | Pitcairn's |
| CE | the Oland's (`04027c3a...e3c`) | the Oland's | the Oland's |
| ME | its own, `7bcda47e...e073`, v 91h | the Oland's (`87617358...31cb`) | the Oland's |

Six new arrays, about 50 KiB, in `si_gfx_microcode.c`; NOTICES.TXT names
the files.

## Single-SE and single-SH assumptions, and what replaced them

1. **`si_gpu_init.c`'s GB_ADDR_CONFIG constant** (`22010002`,
   NUM_SHADER_ENGINES 0). Now computed as constants_init does: the
   chip's golden value, ROW_SIZE from the row, NUM_SHADER_ENGINES 1 for
   two engines. Also written to HDP_ADDR_CONFIG and both DMA engines'
   TILING_CONFIG.
2. **setup_rb as one broadcast write of the Oland's raster value.** Right
   only when every RB is present. Now the RB disables are read per SE
   and SH (GRBM_GFX_INDEX selecting each, `CC_RB_BACKEND_DISABLE |
   GC_USER_RB_BACKEND_DISABLE`), and with all RBs there it is one
   broadcast write of the chip's value; with some off,
   `gfx_v6_0_write_harvested_raster_configs`' per-engine values, each
   under its own SE select. Ported line for line, including that its
   `se_mask[1]` shifts the already-masked `se_mask[0]`.
3. **setup_spi as one SE 0 / SH 0 select clearing CU 0's bit.** It missed
   every array but the first (Cape Verde's second SH as much as
   Pitcairn's and Tahiti's three others) and assumed CU 0 is active. Now
   every SE and SH is selected in turn, its CU disables read
   (`CC_GC_SHADER_ARRAY_CONFIG | GC_USER_SHADER_ARRAY_CONFIG`), and the
   bit Linux's walk finds is cleared. The walk shifts its mask by the step
   number (1, 2, 8, 64, ...), so after CU 1 it tests CU 3; that is
   Linux's and is kept. The W4100's harvest makes this live.
4. **setup_tcc not done.** The Oland had no TCC off (read by V9XSICP).
   Now `CGTS_TCC_DISABLE | CGTS_USER_TCC_DISABLE` is read and, if any is
   off, the steering is patched from the golden list's values and
   TCP_ADDR_CONFIG's bank count set, as Linux does.
5. **The DRAM row as a constant** (4 KiB, W9QI0DAD's `MC_ARB_RAMCFG`).
   Now read from `MC_ARB_RAMCFG` at bring-up and clamped to 4 KiB as
   Linux clamps it; the tile modes whose split is the row's (a mask the
   generator emits: 3, 17, 19 and 20 on Cape Verde and Tahiti) take it.
   Only 2D-tiled modes use the split, and Direct3D uses none, so a wrong
   row would not have shown in its draws; it is read anyway.
6. **The scan converter's backend FIFO** (40h). Per chip: 100h on
   Pitcairn and Tahiti.
7. **`vi_cp.c` chose the bring-up table by the identity bit alone.** Now,
   under the Oland's bit, `v9x_hal->engine.pci_device` picks Cape Verde's,
   Pitcairn's or Tahiti's table (its microcode, and
   `v9x_vi_cp_si_read_ops`, which does the reads in 2-5 and builds the
   list from them); 6610, 6611 and a zero id keep the Oland's.

The reads happen before the list is applied, where Linux makes them
after the golden lists. That is the same only if no golden list writes
them: `test_si_gpu.c` holds every chip's list to writing none of the
seven registers read, and to GRBM_GFX_INDEX values that are broadcast or
a select, ending broadcast.

The Oland keeps its measured inputs as constants (`v9x_si_gpu_init_ops`),
not reads: its list is the 144 entries that ran on W9QI0DAD, entry for
entry, and a test holds it to that (an FNV-1a of the list built before
this change).

Checked and left as they are:

- the clear state (one SI table; it skips PA_SC_RASTER_CONFIG, so
  setup_rb's value stands after CLEAR_STATE);
- the RLC bring-up: `pg_flags` is 0 for all four chips, so
  `gfx_v6_0_init_pg` does nothing. Load balancing stays off, as on the
  Oland; Linux, with it off (every GDDR5 card), also writes
  `SPI_LB_CU_MASK` FFh broadcast, which the Oland path never has, the
  GDDR5 Oland 6611 included;
- the fence: one end-of-pipe write, whatever the engine count. Nothing
  here reads GRBM_STATUS_SE* or counts RBs (no occlusion queries);
- the DMA engine (SI's, the Oland's, at the same offsets on all SI
  chips).

## VRAM

The DirectDraw heap is 128 MiB of BAR0 on these rows, as on the Oland,
and the VBE's 16 MiB is ignored there; the cards' 2, 4 and 6 GiB beyond
BAR0 are not used.

One limit was in the way: the mini-VDD's `VI_MAP` and `eng_vi.c` refused
a `CONFIG_MEMSIZE` above 4096 MiB, which would have refused the W9000's
engine outright (mini-VDD reason 5). The ceiling is now 16384 MiB in
both. The check is there to refuse a window that decodes nothing, which
reads FFFFFFFFh, and still does; the count feeds nothing else. Carrizo,
Mullins and the Oland are under 4096 and see no difference.
`MC_VM_FB_LOCATION`'s 16 MiB units carry 6 GiB without change.

## Clocks

`si_clock_apply.c` sets the clocks only when the shadowed VBIOS at C000h
names device 6610, so on these cards (and the 6611) it records
`no-vbios` and leaves the clocks the BIOS set. The W9000's part string
ends `500e/150m`, which may be its boot engine and memory clocks; if so,
its memory runs far below its rating. Not measured.

## Cost

The three rows put the ati driver's `_TEXT` 121 bytes over its 57,344-byte
budget in `oland_hw16.c`. They are data, reached through the PCI scan's
far pointers, so they moved to `oland_engine16.c` in LEAFCODE; the hook
they name stays a near function in `_TEXT` (`v9x_oland_fill_engine`,
now external, a family-internal symbol). `_TEXT` is then 57,201 bytes.
The family declares 53 PCI ids of `V9X_PCI_ID_LIMIT`'s 56. The HAL grows
by the six images and the per-chip tables; it has no size budget.

## What to ask the owner

1. The V9x Remote Agent machine code (M-XXXXXX) on the machine with
   each card, with remote help on. With it every item below can be done
   by us.
2. A V9XSIMM snapshot of each card under the `vbe` package, read-only:
   BAR0's size, `CONFIG_MEMSIZE`, `MC_VM_FB_LOCATION` against
   `HDP_NONSURFACE_BASE`, the VM's state, `MC_ARB_RAMCFG`, and the RB,
   CU and TCC disables. V9XSIMM is read-only and never writes
   GRBM_GFX_INDEX, so it reads the disables of whichever engine and array
   are selected; the other arrays' need a probe that selects each.
3. Which output the monitor is on, and whether D1 drives it.
4. Then the ati package over `vbe` (WININIT rename, as on the 6611),
   V9XDDP and V9XD32 at 16 and 32 bpp, and the fault records if the CP
   does not start.

Until then all three are Recognised: they bind, and nothing says they
work.
