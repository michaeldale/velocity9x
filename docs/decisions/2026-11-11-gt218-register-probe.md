# GT218 register probe on M-BXMFA8: BAR3 carries the VBE framebuffer, no BAR VM

Date: 2026-11-11

Phase 2 of [docs/plans/nvidia-gt218.md](../plans/nvidia-gt218.md).

## The probe

`N5MM.VXD` + `V9XN5MM.EXE` (`tools/diag/nv50_mmio_probe*.{asm,c}`,
`scripts/build-nv50-mmio-probe.ps1`), modelled on the RIVA 128 probe. It
reads configuration space through CF8h/CFCh and BAR0 offsets on a
whitelist, each twice; it writes no register and no memory. The VxD
refuses anything but `10DE:0A65` and stops unless PMC_BOOT_0 names chipset
A8h. PFIFO, PGRAPH and the copy engine are read only when PMC_ENABLE has
them on; PDISP always, as nouveau has no enable bit for it. Every offset
and field comes from Linux nouveau (nvkm, master, saved in
`build/external/nv50/nouveau/`), unmeasured until this run.

Run on boot 82 (the tier-0 `nvidia` build from the
[Phase 1 record](2026-11-11-gt218-tier0-enable-ok.md)), from
`C:\V9XDIAG\GT218`, probe build `3514028d-dirty`. `Result=COMPLETE`, VxD
status `2D` (identity match, mapped, snapshot), exit 0 after 1.5 s. The
desktop stayed up. Evidence: `build/driver-results/m-bxmfa8-gt218-boot82/V9XN5MM.INI`.

## Configuration space

| Offset | Value | Reading |
|---|---|---|
| 00h | `0A6510DE` | 10DE:0A65 |
| 04h | `00100007` | I/O, memory, bus master on; capabilities list |
| 08h | `030000A2` | VGA class, revision A2 |
| 10h | `FD000000` | BAR0, 32-bit, registers |
| 14h/18h | `D000000C` / `0` | BAR1, 64-bit prefetchable, `D0000000` |
| 1Ch/20h | `CE00000C` / `0` | BAR3, 64-bit prefetchable, `CE000000` |
| 24h | `0000DC01` | BAR5, I/O `DC00` |
| 2Ch | `140110B0` | subsystem 10B0:1401 |
| 30h | `FEA80000` | expansion ROM base, decode bit off; not the `FE000000` Config Manager listed (unexplained) |

Sizes are Config Manager's (Phase 0): BAR0 16 MiB, BAR1 256 MiB, BAR3
32 MiB. This settles Phase 0's open question: Config Manager's order was
BAR order, and **the VBE framebuffer `CF000000` is BAR3 + 16 MiB**, not in
BAR1. Reading 2 of the Phase 0 record holds; reading 1 (a shrunken BAR1)
is dead.

## Registers

| Register | Value | Reading (nouveau's layout) |
|---|---|---|
| PMC_BOOT_0 | `0A8280B1` | chipset A8h (GT218), stepping B1 |
| PMC_ENABLE | `DFF3F113` | PFIFO, PGRAPH, copy engine all on |
| PFB 10020Ch | `20000000` | **512 MiB** |
| PFB 100714h | `F0000001` | type 1: DDR2 or DDR3 (nouveau asks the BIOS which) |
| PBUS 1540h | `F3010001` | one memory partition (64-bit) |
| PFB 100200h | `00222800` | one rank |
| PBUS 1700h | `00001FF0` | PRAMIN window at `1FF00000`, the top 1 MiB |
| PBUS 1704h | `0` | **BAR VM not set up** (bit 30 clear, instance 0) |
| PBUS 1708h | `0` | no BAR1 object |
| PBUS 170Ch | `0` | no BAR3 object |
| PTIMER | moving | `80FC73E0` then `80FCB2A0` between the two reads |
| PGRAPH 400108h | `00000001` | trap status bit 0 set |
| PFIFO 2500h | `00000002` | |
| PFIFO 2600h-261Ch | noise-like | e.g. `0FF7FD5B`, `1DEFDFFA`: not a channel table anyone wrote |
| copy-engine falcon 104040h, 104100h-10410Ch | all `0` | CPUCTL 0: not the halted state nouveau polls for; meaning open |

### PDISP

- Core channel control 610200h = `48070008`, interrupts 610024h = `10C`,
  capabilities 610184h = `7F304777`.
- Head 0's mirrors carry timings. Method 0814h (610AF8h) = `04650898`: 2200
  by 1125, the 1080p60 totals. 0820h `0460083F`, 0818h `0004002B`, 081Ch
  `002800BF`, 0824h `025A0692`, 08D8h `03BA06A0`, 0804h `02824414`.
  Which field each is has not been checked against a source; these are
  raw words, and the 1080p reading is a hypothesis (the monitor's native
  mode, with the BIOS scaling 1024x768 up).
- Head 0's surface offset method (0860h, 610A84h) is **0**, as is its size
  (0868h) and storage (086Ch); format 0870h is `0000E900`. Head 1 has the
  same format word and partial state, no timings.
- Every mirror and its "prev" word (+4) read the same.

### PRAMIN window

509 of 768 sampled words (first KiB at every dword, one word per 4 KiB
across the MiB) are non-zero with no structure: noise, as uninitialised
DRAM reads. No BIOS tables were recognised in the top MiB.

## What the evidence disputes

- **The plan's first hard part, as written, is wrong for this card's
  state.** It said the VBE LFB proves the BIOS set up a BAR1 VM mapping.
  The framebuffer is in BAR3, and 1704h/1708h/170Ch are all zero. With
  nouveau's reading of those registers, there is no BAR VM at all under
  VBE, so BAR3 is decoding without one. How BAR3 + 16 MiB reaches video
  memory with no BAR VM is open.
- **The head's surface offset does not say where the desktop is.** It
  reads 0 on a lit 1080p head. Either the shift or the register is wrong,
  or the VBE scans out by another path (the VGA/legacy one). Not decided.
- **Nouveau's VRAM reservation fits.** With 512 MiB and the PRAMIN window
  parked at the top MiB, the "vbios etc" tail nouveau keeps free
  (`rsvd_tail`, 1 MiB) is where the window points. Its content does not
  look like BIOS tables, though.

## Next

Phase 2 is done for its questions: memory size and type, BAR layout,
which engines are on, and that no VM exists to inherit. Before Phase 3's
engine choice, a second read-only pass worth having:

- sample BAR1 at `D0000000` and BAR3 below `CF000000` (VRAM reads, no
  writes) to see what decodes without a BAR VM, and whether BAR3 + 16 MiB
  is VRAM 0 (the desktop) or something else;
- read the VGA CRTC start through the VGA ports to place the desktop, as
  the NV3 probe's `/crtc` read half does.

Either engine option in Phase 3 now starts by building the BAR VM and a
channel VM from nothing; nothing from the BIOS can be inherited.
