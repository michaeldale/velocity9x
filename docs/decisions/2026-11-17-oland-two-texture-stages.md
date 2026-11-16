# Oland: two texture stages in one pass

Date: 2026-11-17

Status: implemented and measured on W9QI0DAD (Oland XT `1002:6610`,
FX-6300, Win98 SE) at boot 54. The ati package was built from this
commit's tree (`28c632e4-dirty`), and its DRV, VXD, HAL and SETP were
put in place by WININIT rename and hash-checked after `reset`. This is
step 4 of [ati-gcn-ps11.md](../plans/ati-gcn-ps11.md). Carrizo and
Mullins still claim one stage. DDI 8 is still not published (step 5).
V9XD32's gate passes. 3DMark 2000 faulted in two of three runs; the
fault is the program's own read past an array, which the build's memory
layout exposes (below).

Evidence: [`../probe/oland-two-stages-w9qi0dad-2026-11-17/`](../probe/oland-two-stages-w9qi0dad-2026-11-17/).

## Why

Without two stages, the core's fixed-function DOT3 and BUMPENVMAP
(`v9x_d3d_ps_fixed`) have no second stage to draw with
(`d3d_core.c`'s stage pairing), and V9XD32 T12 skips. The core gives a
back end two stages only with `clip_in_core` 0 and no pixel-centre move
to make. GCN already had both: the PA's guard band, and the PA told
Direct3D's centres (`pixel_centre_half` 0), so nothing is moved.

## The change

- **The two-unit PS** (`vi_shader.c`, `struct v9x_vi_d3d_ps.two_units`).
  - It is built per draw, as the alpha-test, fog and colour-key PSs
    already are.
  - Both units' T# and S# come from a 24-dword table whose address is
    the PS's two user SGPRs, read with four `s_load`s (step 1's SMRD
    rewrite on the Oland).
  - Both samples run in whole-quad mode.
  - Each unit combines its texel with what came in (the diffuse, then
    unit 0's result): colour REPLACE, MODULATE, DECAL (Cin + At (Ct -
    Cin)) or ADD (saturated); alpha FRAGMENT, REPLACE or MODULATE. These
    are the Tesla programs' twelve per unit (`nv50_d3d.h`).
  - Specular, fog and the alpha test follow as in the one-unit PS.
  - In `test_gcn_sim.c`, all 144 unit pairs, half with specular and a
    third fogged, run in the GCN model and match the arithmetic in C.
    The largest program is 81 words.
- **The TL2 VS** (`si_shader.c`): the TL VS plus a third MUBUF load,
  IDXEN through a second V# in VS user SGPRs 4-7. It reads unit 1's
  coordinates from a stream of 16 bytes a vertex (tu1, tv1, 0, 1) and
  exports them as parameter 3 ahead of the position. It goes into a new
  CP shader slot, `V9X_VI_CP_SLOT_VS2`, filled on GFX6 only.
- **Builder.** `vi_3d.c` gains the second stream (`vertex1_*`, GFX6
  only) and `v9x_vi_3d_texture_sharps`, which exposes the existing T#
  and S# builder so the HAL can write a table. That is a new external
  symbol, a design change of this step.
- **Map.** `d3d_vi_map.c` takes a two-unit draw only with `unit1`. It
  maps each unit's texture with the one-unit rules and translates each
  combine. It refuses a colour key on a keyed unit, as the Tesla map
  does, and BLEND.
- **HAL.** `d3d_vi.c` writes the table and unit 1's stream into the
  vertex arena with each batch, beside the built PS.
- **Engine selection.** `d3d_select.c` gives the Oland its own engine
  table, `v9x_d3d_engine_vi_oland`, whose limits differ only in
  `texture_stages` 2. This mirrors Haswell and Ivy Bridge.
- **Why the Oland alone.** The TL2 VS exists for SI alone, and Carrizo
  and Mullins have not run the gate.

Host tests:
- the two-unit PS, as above;
- the TL2 VS's words (`test_si_gpu.c`);
- the map's two-unit path and its refusals (`test_d3d_vi_map.c`);
- the Oland's selection (`test_d3d_select.c`).

## On the card (boot 54)

V9XD32, the binary in `C:\V9XA1` that boot 51 ran, at 32 bpp and with
`/bpp16`, against boot 51's reports (`oland-w9qi0dad-2026-11-16/
hal-d32-*`):

| | boot 51 | boot 54 |
|---|---|---|
| T12 two stages (DX6 device) | `SKIPPED-ONE-STAGE` | `PASS`, `T12Ok=FF`, `BlendStages=2`, path `engine-two-unit` |
| T13 two-unit form | not drawn | `PASS`: 263 two-unit draws, centres drawn, nothing stray |
| T10 | `Ok=3F` | `Ok=3F` |
| every other `*Ok`, `*Result` | | identical at both depths |
| `Failures` | 0 | 0 |

Between the snapshots, each V9XD32 run added 266 to `TwoUnitBatches`
and nothing to `TwoUnitValidateRefused`. It also added one refusal under
policy 11, texture op 8 (`M64TexOpRefused` 100h), the same one boot 51's
runs show. `EngineIdleTimeouts` stayed 0.

**3DMark 2000 reached a score in one of three default runs**, 3872
(recorded, not compared). In the other two it faulted in its own code
(`C0000005` at `004E23D1`) about 4.2 minutes in, at the same flip each
time. One stage with 3DMark forced onto DDI 6, which two stages also
imply, completed (3671).

The exception context, which the HAL's fault filter writes to
`V9XTRACE.INI`, shows the cause, read against 3DMark's disassembly. The
program evaluates an animation track and reads one 40-byte key past the
end of a two-key array. That array ended 16 bytes before an unmapped
page, so the read faulted. The driver's two-stage build changes the
process's memory layout, not what the program receives. So the fault is
3DMark's, and the gate's question, whether the two stages break
3DMark 2000, is answered no by this evidence. The record:
[`../issues/2026-11-17-oland-3dmark2000-faults-with-two-stages.md`](../issues/2026-11-17-oland-3dmark2000-faults-with-two-stages.md).

During the runs the one-stage DDI 6 build ran at boot 55. Boot 56 was
meant to bring the step 4 build back and did not: the deploy script was
cut off before it wrote `WININIT.INI`. Boot 57 did, its four files
hash-checked.

## Gates

`build-host.ps1` and `run-checks.ps1` passed before the deploy.
