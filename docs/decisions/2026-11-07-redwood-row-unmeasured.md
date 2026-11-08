# The HD 5570 (Redwood) row: built from a survey, not yet run

Date: 2026-11-07
Status (2026-11-09): **first run on the reporter's card, 0.40.0: the row
claims and Direct3D draws; V9XDDP fails only the five checks every
Evergreen card fails. Many game reports unexplained** (section "First
run" at the end). Original status: **unmeasured.** The `ati` package claims PCI `1002:68D9` as a
fifth row in `cedar_hw16.c`, with Juniper's claims (fill, copy, flip,
Direct3D) and Redwood's microcode and configuration. No Redwood card has
run it. The only evidence is the reporter's survey on
[GitHub issue 16](https://github.com/michaeldale/velocity9x/issues/16).
That issue is also where a first run is to come from.

## What the survey shows

`V9XSURV.INI`, `V9XSURV /rom`, attached to issue 16 (dated 2026-11-02):

| Field | Value |
|---|---|
| PCI id, revision | `1002:68D9`, rev 00 (Redwood PRO: HD 5570/5550) |
| Subsystem | `1458:21E0` (Gigabyte) |
| BAR0 | `D000000C`: 64-bit prefetchable VRAM, as Juniper's |
| BAR2 | `EFFE0004`: 64-bit registers, as Juniper's |
| BAR4 | I/O `E000` |
| VBE | 3.0, `ATI ATOMBIOS`, product `REDWOOD`, 256 x 64 KiB reported |
| ATOMBIOS | `012.020.000.001` |
| Modes | 53 listed; the seven baseline modes the Juniper row carries are among them |

The INI itself is not copied into the repo. It is the reporter's file,
and its PCI inventory describes their whole machine (an nForce board,
AMD K8 northbridge).

## What the row is, and where each value comes from

Every value is Linux's. The copies read are under
`build\external\evergreen\linux-radeon\`: `evergreen.c` for
`evergreen_gpu_init`, `r600.c` for `r6xx_remap_render_backend`, and
`v3.9\evergreen_blit_kms.c` for `set_default_state`.

- **Microcode.** `REDWOOD_pfp.bin` and `REDWOOD_me.bin` were fetched
  from linux-firmware (`gitlab.com/kernel-firmware`, `main`) into
  `build\external\evergreen\firmware\`. `REDWOOD_pfp.bin` is
  `CEDAR_pfp.bin` byte for byte: both have SHA-256 `bf614bcd...fcf3`. So
  Redwood loads Cedar's PFP array, and only the ME is generated as
  `v9x_eg_redwood_me_microcode` (SHA-256 `bc98d6af...cade`; first dword
  `7C408000`, last `000F04BA`).
- **`evergreen_gpu_init`.** Linux gives `CHIP_REDWOOD` Juniper's numbers
  except three: 5 SIMDs (Juniper 10), 2 backends (4) and 256 stack
  entries (512). The SIMD count is used nowhere in our table. Two values
  change:
  - `GB_BACKEND_MAP` = `00001100`. Worked through
    `r6xx_remap_render_backend` with 4 pipes and 2 of 8 backends: the six
    absent backends are masked (`FCh`), 4 / 2 = 2 pipes each, RB 1 then
    RB 0 in 4-bit fields. The same arithmetic gives Juniper's `3210` and
    Cedar's `0`, both measured.
  - `SQ_STACK_RESOURCE_MGMT_1..3` = `002A002A`, 256 / 6 = 42 a stage:
    Cedar's value.
  - Golden `GB_ADDR_CONFIG` is `REDWOOD_GB_ADDR_CONFIG_GOLDEN` =
    `02010002`, the same as Juniper's.
  - The SX export sizes, FIFOs, CF instructions, `VC_ENABLE`,
    `VC_AND_TC` and thread split (128 PS, 16 for each other stage) are
    Juniper's.
- **Contexts.** `max_hw_contexts` is 8, so `ME_INITIALIZE` takes 7, as
  on Juniper.
- **Default state.** Linux 3.9's `set_default_state` for `CHIP_REDWOOD`
  uses Juniper's GPRs and threads (128 PS, 20 for the others) with 42
  stack entries a stage, and `VC_ENABLE` on.
- **Vertex cache.** Redwood is not in Linux's no-vertex-cache list
  (Cedar, Palm, Sumo, Caicos), so batches sync vertices with `VC_ACTION`,
  as on Juniper.
- **Identity bit.** `V9X_DD_ENGINE_CAP_EG_REDWOOD` = `08000000`, stamped
  only by the Redwood row.

Host tests: `test_gpu_init_redwood` checks the table entry by entry
against Juniper's. `test_cp_streams` checks Redwood's contexts and
default state, and `test_cp_microcode` the ME's first and last dwords.

## What is assumed, and how it could be wrong

- **No render backend is fused off.** Linux reads `CC_RB_BACKEND_DISABLE`;
  we write the map for both backends enabled, as the Juniper row does.
  A harvested board would draw to an absent backend. The HD 5570 is the
  full Redwood PRO, so this is not expected on the reporter's card.
- **The DMA engine, display controller and CP bring-up are Evergreen's.**
  They have behaved the same on Cedar, Caicos, Juniper and Trinity's DMA
  engine. Redwood is the same DCE 4 and DMA block, but that is from the
  specification, not a measurement.
- Linux's `evergreen_init_golden_registers` and the Redwood
  `mgcg_init` sequence are not written. The Juniper row does not write
  them either, and it measured clean.
- Video memory: VBE reports 16 MiB. As on the other Evergreen rows, the
  heap is sized by `v9x_cedar_measure_video_memory` at boot.

## Owed

A first run on the reporter's card through issue 16. Ask for:
1. `V9XHW.INI`: EngineCaps should read `08000017`.
2. `V9XDDP`.
3. `V9XD32` at 16 and 32 bpp.
4. A fullscreen game that flickered under `vbe`.

What it shows goes here, beside the hypotheses it kills. If the DMA
engine or flips fail, the row should fall back to tier-0 rather than
stay claimed.

## First run (reporter's card, 2026-11-07, 0.40.0)

The reporter installed the released 0.40.0 `ati` package (`5977e375`) on
their HD 5570 (Athlon II X2 250, ASRock N68-VS3 FX, DirectX 6.1a) and
sent `V9XBOOT.INI`, `V9XHW.INI` and `V9XDD.INI` on issue 16. The files
are theirs and are not copied into the repo; the values below are read
from them.

**The row came up as designed.** `V9XHW.INI`: `Adapter=AMD Radeon HD 5570
(Redwood)`, `EngineCaps=08000017` (the Redwood identity bit plus fill,
copy, flip and Direct3D), `EvergreenEngine=claimed`,
`EvergreenBar0=D0000000`, `EvergreenBar2=EFFE0000`, `Direct3D=hardware-evergreen-cp`,
`Direct3DMode=hardware`. `V9XBOOT.INI`: `Stage=enable-ok`, 53 VBE modes,
`Aperture=m=7 b=d0000000`, `MtrrWrite` not attempted by this family,
`Mtrr= ... r=0 s=1`, 128 MiB mapped (`DpmiMap=08000000:ok`). That boot's
desktop was 640x480x8.

**V9XDDP** (`Build=5977e375`, `Result=COMPLETE`), run on the 8 bpp
desktop, so it set 640x480x16 for its Direct3D part:

- D3D HAL found (`D3DHalFlags=451`, Z 16, 1024 vertices), the triangle
  and all 12 shapes correct, edge rule and slot permutations pass,
  bilinear, modulate, decal and clamp pass, four texture formats
  (565, 1555, 4444, 8888).
- Five sub-checks fail: `Tex8X8888HalvesOk`, `Tex8DecalAlpha4444Ok`,
  `Tex8DecalAlpha565Ok`, `MipLadderShapeOk`, `MipTriDegradedOk`. M-BXMFA8's
  Caicos fails exactly these five
  (`docs/probe/m-bxmfa8-caicos-regression-2026-11-04/boot61-V9XDD.INI`),
  so none is Redwood's.
- The Z and Mixed sections did not run (`88760231`, the probe's own
  not-run marker, as on other machines started from 8 bpp).
- `VBlankStatusHr=0`, `CoopExclusiveHr=0`, primary and back buffer
  created at 640x480x16.

Not sent: V9XD32 at 16 and 32 bpp, owed item 3 above.

**Hypotheses this kills.** The DMA engine, the CP with Redwood's ME
microcode and `GB_BACKEND_MAP` `00001100`, and the display controller all
work well enough for Direct3D games to draw: the reporter says Need for
Speed: High Stakes and Half-Life run correctly on Direct3D. No fall-back
to tier-0 is called for.

**What the reporter saw, not explained.** Their account, not measured:

| Program | Report |
|---|---|
| Need for Speed III | Direct3D works, highest mode offered 800x600x16 |
| NFS: High Stakes, Half-Life (D3D) | correct |
| Bug! | 640x480 works with the picture duplicated; 320x240 hangs the machine |
| Bug Too!, Moto Racer (menus) | flicker; Moto Racer's menu text clipped |
| Moto Racer 2 (D3D) | heavy stutter |
| Virtua Cop 2 (D3D) | wrong drawing, then loops between the game and the desktop |
| Unreal (D3D) | effects missing, looks like its software renderer |
| Quake, Quake 2, Half-Life (OpenGL) | texture corruption, very dark; Quake 2 weapons and enemies missing |
| Quake 3 (OpenGL) | some textures white |

Quake 1, Quake 2 and Half-Life on OpenGL have never been run on any
Evergreen card; only Quake 3 was measured, on Caicos and Trinity. So the
OpenGL reports are first sightings, not regressions. The darkness in
Quake 1 and 2 is not the gamma ramp (they do not call it), but reading
for this found that the family's gamma hook writes the wrong registers
on Evergreen:
[`../issues/2026-11-09-ati-gamma-hook-writes-mach64-dac-on-evergreen.md`](../issues/2026-11-09-ati-gamma-hook-writes-mach64-dac-on-evergreen.md).

**DOS games.** The reporter ran them with the desktop at 16 colours, the
workaround for the black screen after full-screen DOS. 16 colours is the
INF's 4 bpp row, which runs Windows' `vga.drv`
([`../issues/2026-11-07-first-install-stays-at-16-colours.md`](../issues/2026-11-07-first-install-stays-at-16-colours.md)),
so those results probably say nothing about Velocity9x's display driver.
To be confirmed with the reporter.

## Owed after the first run

- V9XDDP from a 16 bpp desktop, so Z runs. V9XD32 is not in the
  released package, so it needs a test build sent to the reporter.
- For the OpenGL games: `V9XGL.LOG` from one Quake 2 session.
- For one flickering game and Bug! at 320x240: the desktop depth, the
  game's mode, and `V9XDDT.TXT` after the hang.
- On our own Evergreen machines: Quake 1 and Quake 2 OpenGL, which no
  Evergreen card has run.
