# DirectDraw fill and copy run on Carrizo's SDMA0, pixel-correct through the HAL

Date: 2026-10-28

Status: **measured, one machine, one boot.** M-XP7053's Radeon R7E
(`1002:9874`), boot 18, the `ati` DRV, VXD and HAL from this tree over the
0.32.0 `vbe` install. Phase 4 of [the plan](../plans/ati-carrizo-gfx8.md).
Captures in
[`../probe/m-xp7053-carrizo-directdraw-on-sdma-2026-10-28/`](../probe/m-xp7053-carrizo-directdraw-on-sdma-2026-10-28/).

## The change

- **Microcode in the tree.** `scripts/gen-carrizo-microcode.ps1` checks
  linux-firmware's `amdgpu/carrizo_sdma.bin` and `LICENSES/LICENSE.amdgpu`
  against recorded SHA-256s and writes `vi_sdma_microcode.c`: the
  2,592-dword payload, its version 22h, and the licence text. Cedar's CP
  microcode went in the same way (Michael, 2026-10-26). LICENSE.amdgpu
  differs from LICENSE.radeon only in its copyright line. Its notice is
  added to the package's `NOTICES.TXT`.
- **Packets.** `vi_sdma.c` writes Evergreen's DirectDraw plans
  (`eg_dma.c`) as SDMA packets with 64-bit addresses. The carve-out is at
  GPU `F4_00000000`, past the 4 GiB the Evergreen packets reach. The
  planner's limits stand: dword-aligned fill rows and no overlapping byte
  copies. A fill row past one packet (3FFFE0h bytes) becomes two packets. A
  copy row that long is declined, since splitting an overlapping copy
  changes what it reads. Fills use fill size 2. Host-tested first
  (`test_vi_sdma.c`, a model of the engine as the probe measured it); the
  test failed to build before the module existed.
- **Engine.** `eng_vi.c`, engine type `ATI_VI` (12). Once per mode it does
  the write probe's sequence: halt, golden settings, microcode, ring
  setup in a 4 MiB window at BAR0 + 240 MiB, unhalt, then a fence that
  must land before the engine is claimed. Every submission ends with a
  fence carrying a sequence number, and idle is that number landing. The
  read pointer is not used, because in the probe it was read short of the
  write pointer in a submission's NOP padding.
- **16-bit and mini-VDD.** `V9xPciReadViBars` (BAR0 64-bit and 256 MiB
  aligned, BAR5 32-bit and 256 KiB aligned), and `V9XMINI_FN_VI_MAP`
  (0029h), Evergreen's map over BAR5's size. `carrizo_hw16.c` claims
  `SOLID_FILL | SCREEN_COPY` and publishes the hook's outcome to
  `V9XHW.INI` through `v9x_carrizo_publish_diagnostics`, a new
  family-internal external symbol like Cedar's.

## Boot 18

- DRV, VXD and HAL by WININIT rename, each SHA-256 checked staged and
  installed. Enable-ok at 1024x768x16. `V9XHW.INI`: `CarrizoEngine=claimed`,
  BARs `E0000000` and `FEB00000`, `EngineCaps=3`.
- **V9XDDP**, six runs: three at 1024x768x16, two at 1024x768x32 after
  `V9XMSW /set`, and one after `V9XMSW /cycle:10` (10 of 10). Every run had
  `Result=COMPLETE` and `BltFillPixelOk`, `SrcCopyPixelOk` and all four
  overlap cases (down, right, and both with pitch) at 1.
- **The engine did the work.** V9XTRACE (this tree's build; the 0.32.0
  one reports `abi-mismatch`) reads `EngineType=12`, no idle timeouts,
  no resets. `CountBltEngine` equals `CountBlt`: 6 after the first run, 36
  after six. The register probe in `/readonly` mode read SDMA0 running
  (`F32_CNTL` 0), the ring enabled at GPU `F4_0F000000`, and RPTR = WPTR =
  24C0h, the same after the first run and after the series. Each V9XDDP
  run restarts the ring from 0 and does identical work. No `V9XVIDMA.TXT`
  (the timeout record) was written.
- `V9XGDI /auto` PASS at 1024x768x16, and the desktop was intact at the
  end.
- `FlipPixelOk=0` in every run, as on Cedar before its flips went in:
  flips are not claimed on this chip yet.

## Not checked

- Throughput, or any game.
- 8 bpp: this BIOS lists no 8 bpp mode.
- The DOS box with the engine running.
- What the monitor shows.
- SDMA1, and a copy row long enough to be declined.
