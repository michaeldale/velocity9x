# The HD 7660D's first HAL Direct3D run hangs the machine

Date: 2026-11-03
Status: **open, cause unknown.** M-D7TFW4's HD 7660D (`1002:9901`,
Trinity), boot 19, on an uncommitted `ati-trinity` working tree built on
`7517eac2`. The first V9XD32 run (32 bpp) hung the machine; it needs a
power cycle by the owner.
Plan: [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md).
Capture: [`docs/probe/m-d7tfw4-trinity-d3d-2026-11-03/`](../probe/m-d7tfw4-trinity-d3d-2026-11-03/).

## What was on the machine

The ati set (DRV, VxD, HAL, ICD) swapped in by WININIT renames after
every staged hash matched. HAL `V9XHAL.DLL` sha256 `5e6fa826...`.
`V9XHW.INI` after boot: `Direct3D=hardware-evergreen-cp`,
`EngineCaps=04000017`, `Direct3DMode=hardware`.

The build adds, over `7517eac2`:

- `v9x_cm_shader_from_eg`: re-lays an Evergreen shader into Cayman form
  (VC fetches to TC, fetch words fixed, `END_OF_PROGRAM` replaced by
  `CF_END`). Host tests rebuild Linux 3.9's `cayman_vs`/`cayman_ps` from
  the Evergreen `r600_blit` pair and check every D3D slot shader and the
  keyed lit PS.
- Aruba PFP/ME microcode (2176 dwords each) in the HAL.
- `eg_cp.c`: on Trinity, the `cm_3d.c` gpu_init table, three-ring
  bring-up, `ME_INITIALIZE` with eight contexts, `CP_ME_CNTL` = 0, the
  Cayman preamble and `CP_COHER_CNTL2`, relocated slot shaders.
- `d3d_eg.c`: Cayman default state and `CP_COHER_CNTL2` per batch, the
  lit PS relocated.

`run-checks.ps1` was green on that tree.

## What happened

V9XD32 started from `C:\V9XDIAG\EGTN` at its default 32 bpp. About a
minute later the relay lost the agent (`Timed out waiting for the agent
through the relay`); the portal last saw it at 01:05:57Z. No V9XD32
report or `V9XEGCP.TXT` could be fetched before the machine went.

## What is not known

Where it stopped. Everything above that differs from V9XCM3D's six
byte-exact copies is a candidate, and nothing measured separates them:

- the CP bring-up running inside the HAL rather than the probe;
- the relocated D3D shaders. The relocator turns the Evergreen blit
  pair into Linux's Cayman words exactly (host test), so V9XCM3D's
  draws cover that pair; no relocated D3D slot shader has run on the
  chip;
- the per-batch state stream;
- the 32 bpp target (V9XCM3D used RGBA8 as well, so this one is weak).

## Next

1. After the power cycle, before any D3D or GL program, read
   `C:\V9XDIAG\V9XEGCP.TXT` and V9XD32's report if they were written.
2. Rename the `58a3b6aa` set back in (DirectDraw only) so a game
   cannot start the CP on this build.
3. Separate the candidates in V9XCM3D, not the driver: draw with one
   relocated D3D slot shader pair, then with the HAL's per-batch state.
