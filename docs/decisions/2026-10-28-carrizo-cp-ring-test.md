# Carrizo's graphics CP runs AMD's microcode, loaded through its own ports: Linux's ring test passes

Date: 2026-10-28

Status: **measured, one machine, three runs on one boot.** M-XP7053's
Radeon R7E (`1002:9874`), boot 19, with the `ati` driver running
DirectDraw on SDMA0 alongside. Phase 1 of
[the 3D plan](../plans/ati-carrizo-gfx8-3d.md). Reports in
[`../probe/m-xp7053-carrizo-cp-ring-test-2026-10-28/`](../probe/m-xp7053-carrizo-cp-ring-test-2026-10-28/).

## The probe

`CZCP.VXD` + `V9XCZCP.EXE` (`scripts/build-carrizo-cp-probe.ps1`),
CZDMA's op-list design over the graphics registers. Its writes are limited
to the CP and RLC registers Linux v4.9 `gfx_v8_0.c` writes, plus
`SCRATCH_REG0`, the HDP pair, and `GRBM_SOFT_RESET` with the RLC's bit or
0. Its bulk op reaches only the four microcode data ports. Its window is
BAR0 + 248 MiB (GPU `F4_0F800000`), clear of the driver's SDMA window at
240-244 MiB. The microcode is linux-firmware's
`amdgpu/carrizo_{pfp,ce,me,rlc}.bin` (hashes in `FIRMWARE-SHA256.TXT`),
copied to the guest; it is not in the tree.

The order is v4.9's with `smc_load_fw=0`:
1. `rlc_resume`: stop the RLC with the GUI-idle interrupts off and the
   SERDES idle; turn clock gating (`RLC_CGCG_CGLS_CTRL` 3Fh to 3Ch) and
   power gating off; pulse the RLC soft reset; load the RLC microcode
   (3,072 dwords, version 9Ch); start the RLC.
2. `cp_gfx_load_microcode`: halt the CP; load PFP (4,192 dwords, E1h),
   CE (2,144, 81h) and ME (4,192, 97h).
3. `cp_gfx_resume`: a 16 KiB ring at VMID 0 (`CP_RB0_CNTL` 0041890Bh),
   the read-pointer writeback in the window, no doorbell.
4. `cp_gfx_start`: `CP_MAX_CONTEXT` 7, `ENDIAN_SWAP` 0, `DEVICE_ID` 1,
   then HDP flush and unhalt.

Two submissions followed, each padded to 16 dwords with
`PACKET3(NOP, 3FFFh)`:
1. Linux's ring test: `SET_UCONFIG_REG` writing `DEADBEEF` to
   `SCRATCH_REG0` over the CPU's `CAFEDEAD`.
2. `cp_gfx_start`'s preamble without `vi_cs_data`: `PREAMBLE_CNTL`,
   `CONTEXT_CONTROL`, `PA_SC_RASTER_CONFIG` = 2, `CLEAR_STATE`, and the CE
   partitions. Then `600DCAFE` to the scratch register.

Afterwards the CP was halted and the RLC stopped, and every register
written was put back.

## What it found

- **All 13,600 dwords went into the ports, and the CP ran them.** After
  the unhalt `CP_ME_CNTL` read 0. The ring test landed within 2 polls,
  and the preamble's scratch write within 3.
- **The CP fetches from the carve-out through VMID 0** with the GART off
  and the system aperture zero, as SDMA did. `CP_RB0_RPTR` reached the
  write pointer (10h, then 30h dwords). The writeback in the window read
  30h.
- **The RLC started**: `RLC_CNTL` 1, `RLC_STAT` 3, `RLC_GPM_STAT` 17h
  (16h, plus the busy bit). GRBM read `A0003028` while the CP ran, and
  `00003028` again after.
- **Three runs, the same each time:** the same poll counts, pointers and
  status words. Nothing was reset between runs; each reloaded all four
  images.
- **Teardown**: `CP_ME_CNTL` 15000000h and `RLC_CNTL` 0, as found.
  `CP_STALLED_STAT1` read C00h after the halt, against 0 before. Not
  decoded.
- **SDMA0 unaffected**: V9XDDP afterwards passed fill, copy and the four
  overlap cases, with flips at 60 Hz (323 ms for 20). The desktop was
  intact and the machine stayed on boot 19.

## What it settles for the 3D plan

- The direct-load path works for every engine Linux v4.9 loaded that
  way: SDMA, RLC, PFP, CE, ME. No SMU message protocol is needed.
- The CP's ring and its fetches use the same GPU addresses as SDMA's,
  so the driver's 4 MiB window can hold the CP's ring, shaders and
  vertices, as on Cedar.

## Not checked

- `vi_cs_data`, the full clear state.
- `gfx_v8_0_gpu_init`'s writes.
- Any drawing.
- Whether the CP survives a VBE mode set while running.
- What `CP_STALLED_STAT1` C00h means.
