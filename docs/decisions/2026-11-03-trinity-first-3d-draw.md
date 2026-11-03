# The HD 7660D's first 3D draw: Linux's textured copy, byte-exact six of six

Date: 2026-11-03
Status: **Phase 5's first draw done** on M-D7TFW4's HD 7660D
(`1002:9901`, Trinity), boot 18, on the ati build from `58a3b6aa`.
`cayman_gpu_init`, the Cayman CP with Aruba microcode, the Cayman clear
state and Linux 3.9's Cayman blit shaders drew a 64x8 RGBA copy
byte-exact, three times linear aligned and three times 1D tiled. The HAL
does not use any of it yet.
Plan: [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md).
Capture: [`docs/probe/m-d7tfw4-trinity-3d-2026-11-03/`](../probe/m-d7tfw4-trinity-3d-2026-11-03/).

## The probe

V9XCM3D (`tools\diag\cayman_3d_win32.c`) is new. V9XEG3D, the Evergreen
model, is Evergreen-specific throughout and has 32-bit addresses only (it
refuses VRAM above 4 GiB, which is why it never ran on the HD 7470).
V9XCM3D is its blit alone on Cayman's path. It shares EGCP.VXD and
takes every Cayman value from the host-tested module:

- `cm_3d.c`: `cayman_gpu_init`'s 41 writes for `9901`, with
  `CGTS_SM_CTRL_REG`'s sixteen `OVERRIDE` writes and its read value back
  after the four texture-cache disables, where Linux has them.
  `ME_INITIALIZE` with eight contexts, `cayman_cp_start`'s preamble around
  `cayman_default_state`, `set_default_state`'s Cayman form, and
  `CP_COHER_CNTL2` = 0 before each ring `SURFACE_SYNC`.
- `cm_shader.c`: Linux 3.9's `cayman_vs` and `cayman_ps`, rebuilt word
  for word.
- The CP bring-up is V9XEGCP's Cayman one: three rings, the spare two
  idle.
- **40-bit addresses**: every 256-byte field (`SQ_PGM_START_VS`/`_PS`,
  texture base and mip base, `CB_COLOR0_BASE`, `CP_COHER_BASE`, the three
  ring bases, `SCRATCH_ADDR`) carries bits 39:32 (`0F`) in its top byte.
  The vertex resource carries them in word 2 bits 7:0, and the
  read-pointer address highs carry them whole.
- The vertex buffer is synced through the texture cache. Cayman fetches
  vertices through it and has no vertex cache, where Linux 3.9 names
  `VC_ACTION` for every chip but Cedar, Palm, Sumo and Caicos.
- The linear-aligned colour buffer has `NON_DISP_TILING_ORDER`, as Mesa
  sets on every linear one, Cayman included (`evergreen_state.c:1167`).

EGCP.VXD's lists gained the nine registers `cayman_gpu_init` writes that
Evergreen's does not (`VGT_OFFCHIP_LDS_BASE`, the six TMP ring bases,
`CGTS_SM_CTRL_REG`) and `CP_COHER_CNTL2`, so they can be read and put
back.

## Runs

`C:\V9XDIAG\EGTN\CP`, with `ARUBA_pfp/me.bin` as `PFP.BIN`/`ME.BIN`. A
`/readonly` pass (every saved register accepted, `CGTS_SM_CTRL_REG`
`00600200`), then six runs. The ati driver's DMA engine was running for
the desktop throughout.

| | Every run |
|---|---|
| Ring | 2A0h dwords: `ME_INITIALIZE` to 10h, preamble to 120h, test to 130h, draw to 2A0h |
| `ME_INITIALIZE`, preamble, ring test | PASS (`DEADBEEF`) |
| Draw mark (`SCRATCH_REG0` = `3D3D3D3D` through the ring) | PASS, then GUI idle |
| Destination, 512 pixels | **0 mismatches**, guard intact, each row's first pixel from its own source row |
| After | CP halted (`CP_ME_CNTL` `14000000`), `GRBM_STATUS` `00003828` |

Three runs linear aligned (`ArrayMode=1`), three 1D tiled (`/tiled`).
After the six, V9XDDP reads `COMPLETE` with fill and copy correct, and
the desktop screenshot is clean.

## What it settles

1. Trinity's configuration from `cayman_gpu_init`, with the backend map
   `10h` from the ported remap, draws to the right pixels in both linear
   and 1D-tiled layouts.
2. The Cayman encoder's blit pair runs, and so do Cayman's clear state
   and its `CP_COHER_CNTL2` rule.
3. The 40-bit address handling carries over from Caicos's HAL work to the
   3D stream on this chip.

## Not established

- Gouraud, depth, modulate and the Direct3D shaders on Cayman. Only the
  blit's two shaders exist in `cm_shader.c`, and the D3D shaders use ALU
  forms (`MUL`, literals, `KILLGT`) whose VLIW4 grouping is not yet
  checked against a Cayman reference.
- The RLC. It is still off, and the draw did not need it.

## Next

The Direct3D shaders on Cayman, host-tested against whatever reference
can be found (Mesa's Cayman assembly rules for ALU groups, no T slot),
then the HAL: `eg_cp.c` and `d3d_eg.c` choosing the Cayman tables, the
microcode in `gen-evergreen-microcode.ps1` and `D3D` in the Trinity
row's claim.
