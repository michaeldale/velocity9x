# Direct3D and OpenGL on the HD 7470's CP: V9XD32 passes, Quake 3 34.6 fps

Date: 2026-11-01
Status: **Phase 5 done** on M-D7TFW4's HD 7470 (`1002:6778`, Caicos),
boot 6. V9XD32 passes at 16 and 32 bpp; the owner's Quake 3 demo runs
fullscreen with every batch on the engine. This closes the regression the
[Phase 3 record](2026-10-31-caicos-page-flips.md) found.
Plan: [ati-evergreen-juniper-caicos.md](../plans/ati-evergreen-juniper-caicos.md).
Captures: [`docs/probe/m-d7tfw4-caicos-d3d-2026-10-31/`](../probe/m-d7tfw4-caicos-d3d-2026-10-31/).

The floppy set is no longer a constraint (Michael, 2026-10-31), so
Caicos's microcode ships in `V9XHAL.DLL`.

## The change

**Chip identity.** `V9X_DD_ENGINE_CAP_EG_CAICOS` (bit 24), a chip-fact bit
as Intel's and Matrox's are, stamped by `cedar_hw16.c`'s Caicos row, which
now also claims `D3D` (`Direct3D=hardware-evergreen-cp`).

**Microcode.** `gen-evergreen-microcode.ps1` pins and emits
`CAICOS_pfp.bin` and `CAICOS_me.bin` beside Cedar's (Cedar's arrays
regenerate unchanged); `eg_cp.c` loads them when the bit is set.

**GPU configuration.** Linux's `evergreen_gpu_init` numbers for Caicos are
Cedar's except two writes: no `SMX_SAR_CTL0` (only up to SUMO2), and 128 PS
threads, which with 192 threads gives `SQ_THREAD_RESOURCE_MGMT`
`08080880` / `_2` `00000808`. `v9x_eg_caicos_gpu_init` is Cedar's table
with those two changes. Linux 3.9's `set_default_state` for Caicos
(`evergreen_blit_kms.c`, fetched) gives 128 PS threads and 10 for every
other stage (`0A0A0A80` / `00000A0A`); `v9x_eg_3d_default_state` takes the
chip. The GPR and stack splits, `SQ_CONFIG` (no vertex cache) and golden
`GB_ADDR_CONFIG` `02010001` match Cedar's. Caicos's golden registers
(`caicos_golden_registers`) are not written, as Cedar's are not.

**40-bit addresses in the CP's streams.** Every batch address lives in
VRAM, which on M-D7TFW4 is GPU `F_00000000`, so `v9x_eg_3d_state` gains
`gpu_high` (bits 39:32 of all of them). Each 256-byte field (SQ_PGM_START
VS/PS, texture base and mip base, CB_COLOR0_BASE, the four DB bases,
CP_COHER_BASE) takes it in its top byte; the vertex resource takes it in
word 2 bits 7:0 beside the stride. `eg_cp.c` writes `CP_RB_BASE`,
`SCRATCH_ADDR` and `CP_RB_RPTR_ADDR_HI` as `evergreen_cp_resume` does, the
way V9XEGCP ran them, and no longer refuses VRAM above 4 GiB.
`v9x_eg_cp_addresses` returns the high byte, which `d3d_eg.c` puts on
each batch. Cedar's high byte is 0, so its streams are unchanged.

Host tests written first, failing to compile against the old interfaces:
the Caicos table against Cedar's, the Caicos default state's thread words,
`surface_sync` with a high byte, the Caicos microcode's sizes and end
dwords, and a whole-stream comparison in which `gpu_high = 0Fh` changes
exactly the ten 256-byte fields and the vertex word and nothing else.
`run-checks.ps1` passed (after setting the manifest's Direct3D word, which
check-tree requires when `EngineCaps` carries `D3D`).

## Boot 6

`V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL` and `V9XGL.DLL` by WININIT
renames and `reset` (boot 5 to 6), each matched by hash afterwards.
V9XD32, V9XDDP and V9XTRACE from the same build (`C:\V9XDIAG`).

| | Result |
|---|---|
| `V9XHW.INI` | `EngineCaps=01000017`, `Direct3D=hardware-evergreen-cp`, `Direct3DMode=hardware` |
| V9XD32 at 1024x768x16 | `Result=PASS`, `Failures=0`; T13-T16 PASS, T12 one stage, T18 no T&L device, T19 no PS |
| V9XD32 at 1024x768x32 | `Result=PASS`, `Failures=0`, the same lines |
| V9XDDP (it works at 16 bpp) | `Result=COMPLETE`; `V9XDD.INI` 93 `Ok=1` / 13 `Ok=0`, `V9XDD2.INI` 121 / 1 (software path, boot 3: 60 / 47 and 87 / 33) |
| Quake 3 demo, owner's settings (640x480 fullscreen, 16-bit), `timedemo 1`, `demo001` | **1346 frames, 38.9 s: 34.6 fps**; `GL_RENDERER` "Velocity9x Radeon HD 5450" (see below), PIXELFORMAT 16-bit colour, 16-bit Z |
| ICD paths in that run | hw 210,062 batches / 5,302,522 triangles; cpu 0; hw-refused 0; 355 surface textures, 6,286 KB, no failures or evictions |
| V9XTRACE after | `Ok=1`, `BuildsAgree=1`, `EngineFlags=9`; flips handled 31, declined 0, taken at done 29, not taken 0 |

The ICD's counts match Cedar's run of the same demo (210,992 batches, 355
textures, 6,286 KB). The 34.6 fps is recorded, not compared. The owner's
`q3config.cfg` was put back after the run (hash matches the backup). The
console's one "failed" line is DirectSound's.

V9XDDP's `Ok=0` set was not compared rung by rung with Cedar's.

## The renderer name

`d3d_core.c` named every Evergreen engine "Velocity9x Radeon HD 5450".
After this run it names the engine by the Caicos bit ("Velocity9x Radeon
HD 7470"), as the Intel names are chosen. That change is in a separate
commit and has not been on the machine.

## Not established

- Vertical-blank pacing of flips (Phase 3's gap), and what the monitor
  shows during Quake 3.
- 3DMark 2000 and other Direct3D titles.
- Cedar on this build (its card is gone); its streams are unchanged by
  construction and by the host test with `gpu_high` 0.
- Juniper: four backends, four pipes and a vertex cache, still to do.
