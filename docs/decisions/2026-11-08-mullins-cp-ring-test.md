# Mullins' GFX7 command processor runs Linux's ring test and start preamble

Date: 2026-11-08

Status: **measured, one machine, one run.** M-JJN1ZZ's Radeon R3E
(`1002:9854`), boot 14, with the `ati` driver from `8e80796e` running (its
SDMA ring at BAR0 + 240 MiB). Phase 6's first step in
[the plan](../plans/ati-mullins-gfx7.md). Reports in
[`../probe/m-jjn1zz-mullins-cp-ring-test-2026-11-08/`](../probe/m-jjn1zz-mullins-cp-ring-test-2026-11-08/).

## The probe

`CKCP.VXD` + `V9XCKCP.EXE` (`scripts/build-cik-cp-probe.ps1`), a copy of the
Carrizo CP probe (`carrizo_cp*`) with what Linux's `gfx_v7_0.c` does
differently:

- The RLC at C300h (`RLC_CNTL`) to C4E8h, its microcode port at C388h, and
  the CP's PFP, ME and CE microcode ports at C150h-C16Fh (`gfx_7_2_d.h`);
  VI has them at 3B000h and 3E050h.
- `CP_INT_CNTL_RING0`'s GUI-idle enables are CNTX_BUSY and CNTX_EMPTY
  only (180000h); the non-CU SERDES busy mask is 7FFFFh.
- `CP_RB0_CNTL` = RB_BUFSZ 11 (a 16 KiB ring) and RB_BLKSZ 9, with no
  MTYPE or MIN_IB_AVAILSZ: 90Bh.
- No gfx-ring doorbell register on CIK.
- The preamble in `gfx_v7_0_cp_gfx_start`'s order: `SET_BASE` (CE
  partitions) first, `PREAMBLE_CNTL` begin, `CONTEXT_CONTROL`,
  `PA_SC_RASTER_CONFIG` = 0, 0 (Mullins), `PREAMBLE_CNTL` end,
  `CLEAR_STATE`, then `VGT_VERTEX_REUSE_BLOCK_CNTL` = Eh and
  `VGT_OUT_DEALLOC_CNTL` = 10h. `ci_cs_data`'s context registers are left
  out, as Carrizo's probe left out VI's.
- Linux's RLC load-balancing setup (`RLC_LB_*`) is left out.

Microcode: linux-firmware's `amdgpu/mullins_{pfp,ce,me,rlc}.bin`, header
version 1 each: PFP E5h, CE 7Ah, ME BBh (2,144 dwords each), RLC 9h
(2,560 dwords). SHA-256: PFP `11e6b126...211f`, CE `938d3f60...f374`, ME
`bf462933...89d2`, RLC `21e4d297...6ce1`. The ring and writeback are in a 64 KiB scratch window at
BAR0 + 248 MiB (GPU `F4_0F800000`), past the driver's SDMA window.

## Result

`/readonly`: CP halted (`CP_ME_CNTL` 15000000h), RLC stopped, nothing in
reset; `RLC_CGCG_CGLS_CTRL` 3Fh (coarse-grain clock gating and light sleep
on, as the BIOS left them; the run clears bits 0-1 as Linux does).

The full run, `Result=COMPLETE`:

- 8,992 microcode dwords written. RLC started (`RLC_CNTL` 1), SERDES idle
  at once.
- CP unhalted (`CP_ME_CNTL` 0). **Ring test PASS**: `SCRATCH_REG0` read
  DEADBEEF over the CPU's CAFEDEAD, read pointer 10h.
- **Preamble PASS**: the scratch write behind it read 600DCAFE, read
  pointer 30h, writeback 30h. `CP_STAT` 0 after each.
- Afterwards the CP was halted, the RLC stopped and every register put
  back; `CP_STALLED_STAT1` read C00h after the halt (not read before the
  run's halt on Carrizo either; not interpreted).

## What it settles

The GFX7 CP takes Mullins' microcode through CIK's ports, and a ring in
the carve-out works with the 40-bit address split Carrizo's code uses. So
Direct3D on Mullins needs what Carrizo's 3D needed after its ring test:
gpu_init (GFX7's tables), the clear state (`ci_cs_data`), and shaders in
the GCN 2 encoding, which differs from GCN 3's.

Not measured: anything drawn.
