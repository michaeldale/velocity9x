# Oland: Direct3D through the HAL, V9XD32 passing at 16 and 32 bpp

Date: 2026-11-16

Status: **measured, one boot.** W9QI0DAD (Oland XT `1002:6610`, 2 GiB),
boot 51, ati package built from `636a2fcd-dirty` (this commit's tree),
DRV, VXD, HAL and SETP by WININIT rename, all four hash-checked. Step 4 of
[`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md).
Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/`](../probe/oland-w9qi0dad-2026-11-16/)
`hal-d32-16bpp-1`, `hal-d32-32bpp-1`, `bltx-after-hal-d32`.

## What changed

The probe's bring-up and draws moved into V9XHAL.DLL behind the Oland
identity bit, the way Mullins' did:

- **`oland_engine16.c`** stamps `V9X_DD_ENGINE_CAP_D3D` when the
  mini-VDD mapped the whole 4 MiB window, as Mullins' and Carrizo's rows
  do. `_TEXT` is unchanged in size (the hook is in LEAFCODE).
- **`vi_cp.c`** gains `v9x_vi_cp_oland`, a third chip table with a
  `gfx6` flag for what a table cannot say:
  - the RLC through SI's indexed microcode port, with Linux's SI
    RL/LB/MC/UCODE values;
  - the CP images' address registers back to 0;
  - SI's ring-0 resume registers, and none of CI's;
  - ME_INITIALIZE and the CE partitions after the unhalt;
  - SCRATCH_REG0 at 8500h through SET_CONFIG_REG;
  - type-2 NOPs at the ring's wrap;
  - SI's microcode (`si_gfx_microcode.c`, now linked), gpu_init list and
    start of stream;
  - the MUBUF TL vertex shader, with the pixel shaders through
    `v9x_si_shader_from_vi`.

  Linux's RLC_STAT settle wait is skipped on SI: it times out on every
  run after the first (2026-11-16-oland-gpu-init.md).
- **`vi_3d.c`**: GFX6 streams pad with type-2 NOPs
  (`V9X_VI_RING_NOP_GFX6`, 80000000h). radeon's `si.c` pads SI's gfx
  ring with `RADEON_CP_PACKET2`. CI's and VI's `PACKET3(NOP, 3FFFh)`
  would read on SI as a NOP with a 16K-dword body. Alignment stays at 16
  dwords, as the HAL already runs on CIK and VI, although amdgpu pads all
  three to 256.
- **`d3d_vi.c`**: every stream in GFX6 mode on Oland, and its built pixel
  shaders translated for SI. **`d3d_core.c`** names the device
  "Velocity9x Radeon Oland".
- NOTICES.TXT names `oland_{rlc,pfp,ce,me}.bin` beside Carrizo's and
  Mullins' files. Same LICENSE.amdgpu (identical SHA-256) and the same
  WHENCE terms.

## Boots

- **Boot 50, the first on this build: startup stalled.** The OBS capture
  showed the desktop at 1024x768 with an hourglass; the boot trail shows
  `enable-ok` at 22:16. The agent never started listening, although the
  machine answered ping. About 15 minutes later the capture lost signal
  and ping stopped. The machine came back as boot 51 on its own or by a
  hand restart, which is not known. Nothing in V9XDIAG says why. Earlier
  boots of this box have stalled after a restart with nothing of ours
  changed (the GT218 notes), and boot 51 on the same files came up
  normally, so this is **recorded, not explained**. It is not shown to
  be the new code.
- **Boot 51:** `Stage=enable-ok`, V9XHW.INI `EngineCaps=40000017` (the
  D3D bit set), `Direct3DMode=hardware`.

## V9XD32

Built from this tree.

| Run | Result | Engine |
|---|---|---|
| `/bpp16` (640x480x16) | **PASS**, `Failures=0`: T2, T3, T5-T11, T13-T16, T20-T22 all their bits | 1,607 batches through `v9x_d3d_vi_draw` (92,871 triangles in), 13 textures placed by the VI path, 1 batch refused |
| default (32 bpp, `ModeBpp=32`) | **PASS**, `Failures=0`, the same tests | another 1,607 batches, 13 more textures, 1 more refused |

No CP fault record (`V9XCZCP.TXT`) was written, and
`EngineIdleTimeouts`/`EngineResets` stayed 0. The refused batch in each
run carries `M64TexOpRefused` 100h, a texture op the VI path declines on
any chip, which went to the software path. T12 is `SKIPPED-ONE-STAGE`,
T18 `NO-TNL-DEVICE` and T19 `SKIP-NO-PS`: the VI path offers one texture
stage, no T&L device and no pixel shaders, on Carrizo and Mullins too.

Afterwards V9XBLTX was byte-exact, 53 blits on the DMA engine, no
timeouts; the desktop intact.

## Not covered

- Carrizo and Mullins were not re-run. Every change for them is behind
  the Oland bit or the `gfx6` flag (cleared by `v9x_vi_pm4_init`), and
  their host tests pass, but no card has run this HAL.
- Programs: 3DMark 2000/2001 SE, Quake 2 and 3 through the ICD, Glide
  (step 5). V9XGL.DLL and the Glide DLLs were not updated on the box.
- Boot 50's stall.
