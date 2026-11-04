# The HD 7660D's CP runs AMD's Aruba microcode: ring test three of three, no RLC

Date: 2026-11-03
Status: **Phase 4 done** on M-D7TFW4's HD 7660D (`1002:9901`, Trinity),
boot 18, on the ati build from `58a3b6aa`. The CP loads Cayman-sized
`ARUBA_pfp/me.bin`, runs `ME_INITIALIZE` and executes a `SET_CONFIG_REG`
from its ring. The RLC microcode was not loaded. The driver does not use
the CP yet.
Plan: [ati-trinity-hd7660d.md](../plans/ati-trinity-hd7660d.md).
Capture: [`docs/probe/m-d7tfw4-trinity-cp-2026-11-03/`](../probe/m-d7tfw4-trinity-cp-2026-11-03/).

## The microcode

From the gitlab.com `kernel-firmware/linux-firmware` mirror (`main`,
`radeon/`), into `build\external\evergreen\firmware\` beside Cedar's,
Caicos's and Juniper's:

| File | Bytes | Dwords | SHA-256 |
|---|---|---|---|
| `ARUBA_pfp.bin` | 8,704 | 2,176 (`CAYMAN_PFP_UCODE_SIZE`) | `70f9a3bf69f39215795d7d868b2f28a3e6dd140515fb7ac3a132a9b730c6b443` |
| `ARUBA_me.bin` | 8,704 | 2,176 (`CAYMAN_PM4_UCODE_SIZE`) | `d1b2ce040adf9b2229abd53cb9439dfe3b8e30b7ffe7fba6b2f90803b9e859c3` |
| `ARUBA_rlc.bin` | 6,144 | 1,536 (`ARUBA_RLC_UCODE_SIZE`) | `a759f856e80c04d4fe6289f1f184ef3a7fcc6ac319eeff220f2c1cc4a8c376ef` |

The PFP and ME are twice the Evergreen images' length (1,120 and 1,376
dwords). They are not yet in `gen-evergreen-microcode.ps1`; they go there
with the HAL work, pinned to these hashes. The RLC image was fetched but
not used.

## What Linux does on Cayman (ni.c), and the probe

V9XEGCP followed `evergreen_cp_*` and ran Cedar, Caicos and Juniper. On
`9901` it now runs Cayman's order instead:

- **Load** (`cayman_cp_load_microcode`, `ni.c:1497`): no `CP_RB_CNTL`
  write first, 2,176 dwords to each port.
- **Resume** (`cayman_cp_resume`, `ni.c:1598`): the same `GRBM_SOFT_RESET`
  pulse (CP, PA, SH, VGT, SPI, SX), and `CP_DEBUG` with bit 27 alone. Then
  **three rings**. Each ring's size and read-pointer address are set, then
  all three bases, because writing one base resets every ring. Then each
  read pointer goes to 0 through `CP_RB0/1/2_RPTR` (`8700h`, `86FCh`,
  `86F8h`) under `RB_RPTR_WR_ENA`. The BIOS left `CP_RB1_BASE` and
  `CP_RB2_BASE` at uninitialised patterns (`FEDCBADF`, `FEDCBACF`). The
  probe gives RB1 and RB2 idle 16 KiB rings of their own in its window
  (offsets `8000h` and `C000h`), with both pointers at 0. `RB_NO_UPDATE` is
  set on all three, since there is no read-pointer writeback, as on
  Evergreen here.
- **Start** (`cayman_cp_start`, `ni.c:1524`): `ME_INITIALIZE` with
  `max_hw_contexts - 1` = 7 (eight contexts on `9901`, `ni.c:923`), then
  `CP_ME_CNTL` 0 (`cayman_cp_enable`, which writes 0 where Evergreen writes
  `FFh`).
- **Ring test**: `SCRATCH_REG0` = `CAFEDEAD` by MMIO, then
  `SET_CONFIG_REG` of `DEADBEEF` through ring 0, polled.
- **Teardown**: halt, then every register call 1 read, including RB1's and
  RB2's, is put back.

Linux also runs `evergreen_rlc_resume` on every IGP, with power gating on
Trinity (`evergreen.c:4379-4410`). The probe does not: `RLC_CNTL` read 0
(RLC off) in the register probe, and this run asked whether the CP works
in that state.

Probe changes, all in `tools\diag\`: EGCP.VXD accepts `9901` and 32-bit
BARs, as EGDMA.VXD now does. Its data area grows from 4,096 to 8,192
dwords to hold 4,352. The RB1/RB2 and Cayman read-pointer registers join
its read and write lists. V9XEG3D's request grows to match, since it
shares the VxD. V9XEGCP takes the chip's id from a first read-only call
before reading `PFP.BIN` and `ME.BIN`, so it knows the sizes.

## Runs

Files in `C:\V9XDIAG\EGTN\CP` (`PFP.BIN` = `ARUBA_pfp.bin`, `ME.BIN` =
`ARUBA_me.bin`, all hash-checked). A `/readonly` pass, then three runs.
The DMA engine was running for DirectDraw throughout.

| | Every run |
|---|---|
| Before | `CP_ME_CNTL` `14000000` (halted), `GRBM_STATUS` `00003828` |
| Window | GPU `F_0F100000` |
| `GRBM_SOFT_RESET` read during the pulse | `00004721` |
| `ME_INITIALIZE` | PASS: read pointer = `10h` at the first poll |
| Ring test | PASS: `SCRATCH_REG0` `DEADBEEF`, read pointer `20h` = write pointer |
| During | `CP_STAT` `80000042`, `GRBM_STATUS` `A0003828`, `CP_ME_CNTL` 0 |
| After | `CP_ME_CNTL` `14000000`, `GRBM_STATUS` `00003828` |

After the three runs, V9XDDP read `COMPLETE` with fill and copy correct,
and the desktop screenshot is clean.

## What it settles

1. **The CP runs without the RLC**, in the state the BIOS leaves (RLC off,
   so no power gating). Plan decision 2 (shipping `ARUBA_rlc.bin`) is not
   needed for the CP. Whether long 3D runs need it, for power or clock
   gating, is open.
2. Cayman's three-ring bring-up works as Linux writes it, with the two
   spare rings idle.

## Not established

- Clear state (`cayman_default_state`, from `cayman_blit_shaders.c`, not
  yet fetched) and any draw. Those are Phase 5, with the VLIW4 shader
  encoder.
- `RLC_CNTL` and the `TN_RLC_*` registers after the runs. Not re-read.

## Next

Plan decision 1 (a Cayman 3D module beside Evergreen's, or more chip bits
in it), then Phase 5: GPU init from `cayman_gpu_init` for `9901`, the
VLIW4 encoder held to Linux's Cayman blit shaders on the host, and
V9XEG3D's textured copy on the chip.
