# The HD 5570 (Redwood) row: built from a survey, not yet run

Date: 2026-11-07
Status: **unmeasured.** The `ati` package claims PCI `1002:68D9` as a
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
