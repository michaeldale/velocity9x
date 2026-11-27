# The HD 7570 (Turks, `1002:675D`) claimed on the Evergreen path from one survey: not run

Date: 2026-11-28
Status: **unmeasured.** The `ati` package claims PCI `1002:675D` as a
row in `cedar_hw16.c`, with Cedar's hook and claims (fill, copy, flip,
Direct3D) and, in the HAL, Turks's microcode and configuration. No Turks
card has run it. The only evidence is the owner's DOS survey on GitHub
issue 36, which is also where a first run is to come from.

## What the survey shows

`V9XSURV.INI` (survey build `b3996f07`, dated 2026-11-26, plain DOS 7.10
on an AMD family 10h machine), attached to issue 36. The file is the
owner's and is not copied into the repo; the values below are read from
it.

| Field | Value |
|---|---|
| PCI id, revision | `1002:675D`, rev 00, class 030000, multifunction (HDMI audio `1002:AA90` at function 1) |
| Subsystem | `1028:2B20` (Dell OEM) |
| BAR0 | `D000000C`: 64-bit prefetchable, where every VBE mode's linear frame buffer is |
| BAR2 | `FEAE0004`: 64-bit registers, 128 KiB aligned; the same address as M-BXMFA8's HD 5450 |
| BAR4 | I/O `E000` |
| Expansion ROM | `FEAC0000` |
| Video BIOS | "AMD ATOMBIOS", `113-C3340200-105`, "TURKS ProL C33402 GDDR5 64Mx32", ATOMBIOSBK `013.012.000.041.001452`, dated 07/05/12 |
| VBE | 3.0, product `TURKS`, 256 x 64 KiB (16 MiB) reported |
| Modes | 53 listed, 38 described, up to 1920x1440 at 8, 16 and 32 bpp; the seven baseline modes the other Evergreen rows carry are among them (`0103h` with an 832-byte pitch) |

The survey does not size the BARs and does not read `CONFIG_MEMSIZE`.
"64Mx32" GDDR5 parts on Turks's 128-bit bus would be 1 GiB; that is a
reading of the BIOS string, not a measurement.

## Turks against the rows already here

Linux drives Turks, like Caicos, as a BTC (Northern Islands) part through
`evergreen_gpu_init`, `btc_asic` and `ni_init_microcode`. The copies read
are under `build\external\evergreen\linux-radeon\`: `evergreen.c`,
`evergreend.h`, `ni.c`, `radeon_asic.c`, `radeon_family.h` and
`v3.9\evergreen_blit_kms.c`. Each way Caicos differs from Cedar, Juniper
and Redwood in our code, decided for Turks:

| Difference | Caicos | BTC-wide or Caicos's own | Turks |
|---|---|---|---|
| `SMX_SAR_CTL0` not written | omitted | BTC-wide: `evergreen_gpu_init` writes it only `if (rdev->family <= CHIP_SUMO2)`, and `CHIP_TURKS` comes after `CHIP_SUMO2` in `radeon_family.h` | omitted |
| 4 hardware contexts (`ME_INITIALIZE` 3) | 4 | Caicos's: `max_hw_contexts` is 8 in the `CHIP_TURKS` case | 8 (7) |
| No vertex cache: `VC_ENABLE` clear, `TC_ONLY`, `TC_ACTION` vertex sync | yes | Caicos's: Cedar, Palm, Sumo, Sumo2 and Caicos are Linux's no-vertex-cache list in `evergreen_gpu_init` and in 3.9's `set_default_state` and `set_vtx_resource` | vertex cache on, `VC_AND_TC`, `VC_ACTION` sync |
| 128 PS threads in `evergreen_gpu_init` | `08080880` | Not Caicos's at all: every part outside the 96-thread list (Cedar, Palm, Sumo, Sumo2) gets 128 | 128, (248 - 128) / 6 / 8 * 8 = 16 each: `10101080` |
| 10 threads a stage in 3.9's `set_default_state` | `0A0A0A80` | Caicos's: the `CHIP_TURKS` case gives 20 | `14141480`, Redwood's |
| Own PFP and ME microcode | `CAICOS_pfp`, `CAICOS_me` | Per chip | `TURKS_pfp.bin` is its own (SHA-256 `65d98665...3089`); `TURKS_me.bin` is `CAICOS_me.bin` byte for byte (both `37fa5fb7...363b`), so Turks loads Caicos's ME array |
| VRAM at GPU `F_00000000`, addresses carrying bits 39:32 | measured on M-D7TFW4 | Not chip-keyed: `eng_eg.c` and `eg_cp.c` read the base from the memory controller on every row | whatever its MC reports |
| Identity bit `V9X_DD_ENGINE_CAP_EG_CAICOS` | stamped | Every cap bit is taken (`engine_abi.h`) | none; the HAL tells Turks by `V9X_DD_ENGINE.pci_device == 675Dh` (ABI 2026112001), as `d3d_hsw.c` tells the HD 2500 |

The rest of `evergreen_gpu_init`'s `CHIP_TURKS` case is `CHIP_REDWOOD`'s
value for value (1 SE, 4 pipes, 4 tile pipes, 2 backends, 256 GPRs, 248
threads, 256 stack entries, export sizes 256/64/192, FIFOs
100h/30h/130h, 2 CF instructions, 8 contexts) except 6 SIMDs (Redwood
5), which nothing here writes. `TURKS_GB_ADDR_CONFIG_GOLDEN` is
`02010002`, Redwood's and Juniper's. So:

- **`evergreen_gpu_init`**: `v9x_eg_turks_gpu_init` is Redwood's table,
  entry for entry, without `SMX_SAR_CTL0` (44 writes).
  `GB_BACKEND_MAP` is Redwood's `00001100`: `r6xx_remap_render_backend`
  for 4 pipes and 2 of 8 backends.
- **Contexts**: `ME_INITIALIZE` with 7.
- **Default state**: 3.9's `set_default_state` `CHIP_TURKS` case is
  `CHIP_REDWOOD`'s: GPRs 93/46/4/31/31/23/23, 128 PS threads and 20 for
  the others, 42 stack entries a stage, `VC_ENABLE` on.
- **Vertex cache**: on, as Juniper and Redwood.
- **Microcode**: `v9x_eg_turks_pfp_microcode` (generated, first dword
  `7C408000`, dword 513 `041C0008` where it first differs from Caicos's,
  last `00000005`) with `v9x_eg_caicos_me_microcode`.
- **16-bit side**: a `cedar_hw16.c` row `v9x_evergreen_turks_device`
  with Cedar's fill hook, so it claims `00000017` (fill, copy, flip,
  Direct3D) and stamps no identity bit; Cedar's 128 MiB heap map,
  write-combining and screen shadow.
- **Renderer name**: "Velocity9x Radeon HD 7570", by the PCI id.

Not applied, as for Caicos and every Evergreen row:

- `turks_golden_registers` (`evergreen_init_golden_registers`). Caicos
  runs measured without `caicos_golden_registers`.
- The disabled-backend read. On BTC Linux reads `CC_RB_BACKEND_DISABLE`
  per shader engine, not the RCU fuse straps; neither is read here.
- The MC microcode. `ni_mc_load_microcode` loads `TURKS_mc.bin` only for
  GDDR5 whose memory sequencer is not running. This card is GDDR5. A
  card the system BIOS posted has its sequencer running, so Linux skips
  the load there too; that is the reasoning, not a measurement. Caicos
  on M-D7TFW4 is DDR3, so that machine never tested this.
- Six display controllers (Caicos 4): flips go through D1 only, and
  `eg_scanout.c` checks D1 is the controller scanning.

Host tests, written first and seen failing (contexts and default state)
before `eg_3d.c` was routed: `test_gpu_init_turks` checks Turks's table
against Redwood's less `SMX_SAR_CTL0`; `test_cp_streams` Turks's
contexts and default state; `test_cp_microcode` three PFP dwords; and
`test_hw16_modes.c` that the row claims no gamma.

## What is assumed, and how it could be wrong

- **No render backend is fused off.** A harvested board would draw to an
  absent backend. The HD 7570 is the Turks PRO part with both.
- **BAR0 is 256 MiB and the card has at least 256 MiB.** The heap map
  (128 MiB) and the engine window at BAR0 + 240 MiB need both; the
  mini-VDD refuses the window otherwise and the row stays tier-0, which
  `V9XHW.INI` reports as `EvergreenEngine=map-refused`.
- **The DMA engine, display controller and CP bring-up are Evergreen's**,
  as `btc_asic` uses `evergreen_dma_*` and DCE 5. They behaved the same
  on Cedar, Caicos, Juniper and Redwood.
- **The VBIOS has trained the GDDR5**, as above.

## Owed

A first run on the owner's card through issue 36. Ask for:
1. `V9XHW.INI`: `Adapter=AMD Radeon HD 7570 (Turks)`,
   `EngineCaps=00000017`, `EvergreenEngine=claimed`, and the two BARs.
   On `map-refused`, `EvergreenMapReason` and `EvergreenMapValue`.
2. `V9XBOOT.INI` and `V9XDD.INI` (the renderer name).
3. `V9XDDP` from a 16 bpp desktop.
4. `V9XD32` at 16 and 32 bpp (a test build).
5. One Direct3D game and one fullscreen game that flips.

What it shows goes here, beside the hypotheses it kills.
