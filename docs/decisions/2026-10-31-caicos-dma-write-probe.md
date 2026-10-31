# DirectDraw on the HD 7470's DMA engine, with VRAM at GPU F_00000000

Date: 2026-10-31
Status: **Phase 2 done** on M-D7TFW4
([plan](../plans/ati-evergreen-juniper-caicos.md)). The DMA write probe is
byte-exact five runs in five with 40-bit addresses. The ati driver claims
the HD 7470 and draws DirectDraw fills and copies on the engine, checked at
16 bpp. No flips and no Direct3D on this chip yet.
Captures: [`docs/probe/m-d7tfw4-caicos-dma-2026-10-31/`](../probe/m-d7tfw4-caicos-dma-2026-10-31/)
(probe), [`docs/probe/m-d7tfw4-caicos-ati-2026-10-31/`](../probe/m-d7tfw4-caicos-ati-2026-10-31/)
(driver, boot 4).

## The write probe

V9XEGDMA accepted only `1002:68F9` and refused any VRAM start whose
window lay above 4 GiB. Both are changed. The EXE now carries the window
address's bits 39:32 (`0F` here) in each packet's high field: copy dwords
3 and 4, fill dword 3 bits 23:16, fence dword 2, as `evergreen_cs.c` and
`evergreen_dma_fence_ring_emit` lay them out. `DMA_RB_BASE` takes the
40-bit address shifted right by 8, which fits the register whole
(`r600_dma_resume`). A start whose 240 MiB window would carry into bit 32
is still refused. The VxD accepts `6778` beside `68F9`; its BAR and
`CONFIG_MEMSIZE` checks are unchanged and pass as they are (BAR0
`C000000C`, 256 MiB aligned, high half 0; 2048 MiB).

Boot 3, 0.33.0 `vbe`, 1024x768x16. A `/readonly` pass, then five runs:

| | Every run |
|---|---|
| Scratch window | GPU `F_0F000000` (`ScratchGpuHigh=0F`, `ScratchGpu=0F000000`) |
| Fences | `11111111` and `22222222` landed; read pointers `80h` and `C0h` = write pointers |
| Dword copy, 256 dwords | PASS, 0 mismatches including guards |
| Byte copy, 37 bytes, offsets 1 to 3 | PASS |
| Overlapping copy, 64 dwords 4 up | `memmove` |
| Constant fill, 256 dwords | PASS |
| After teardown | ring off, `DMA_STATUS_REG` back to `44C83D57`, `SRBM_SOFT_RESET` 0 |

The engine was as the BIOS left it (ring off, not busy, not in reset); the
probe's soft reset before and after each session is Cedar's.

## The driver

**40-bit addresses** (host-tested first, `test_eg_dma.c`
`test_high_addresses`, which failed to compile against the old
signatures):

- `v9x_eg_fb_start(fb_location, &high, &low)` returns both halves and
  refuses only a low half above `EF000000h`, where the window could carry.
  It used to refuse any start at or above 4 GiB, which is what kept the
  shipped engine off this card.
- `v9x_eg_dma_plan_row` and `v9x_eg_dma_fence` take the high byte and
  place it as the probe did; `v9x_eg_dma_ring_base` builds `DMA_RB_BASE`.
  The plan structs Carrizo shares are unchanged: the high byte is an
  argument, as in `vi_sdma.c`.
- `eng_eg.c` keeps the high byte from validation and passes it to every
  packet, the fence and `DMA_RB_BASE`. Its timeout record logs it.
- `eg_cp.c` refuses a non-zero high byte: the CP's streams carry 32-bit
  addresses, so on this card the CP stays halted rather than run shaders
  from the wrong place. Cedar's high byte is 0, so nothing changes there.

**Identity** (Michael, 2026-10-31: a row in Cedar's module):
`v9x_evergreen_caicos_device` in `cedar_hw16.c`, a new external symbol
for the per-object map audit. It shares the engine hook, the 128 MiB heap
map and the diagnostics. It claims `SOLID_FILL | SCREEN_COPY` only: no
`FLIP`, because `eg_scanout.c` takes the heap address from the CP, and no
`D3D`. The `ati` manifest has a `caicos` chip on the same object, the
backend registry and `v9x_ati_device_ids[]` list `6778`.

**Install.** Built from `eacd196c` plus this change; `run-checks.ps1`
passed. The vbe INF's registry was kept. `V9XDISP.DRV`, `V9XMINI.VXD` and
`V9XHAL.DLL` went in by plain `dest=src` WININIT renames (no `NUL=`), then
`reset`: boot counter 3 to 4, desktop ready. The staged files are gone and
the three installed files match the build by SHA-256.

## Boot 4

| | Value |
|---|---|
| `V9XHW.INI` | `Adapter=AMD Radeon HD 7470 (Caicos)`, `EvergreenEngine=claimed`, BARs `C0000000` / `FEA20000`, `EngineCaps=00000003`, `Direct3DMode=none` (as Cedar's fill/copy-only boot 7) |
| `V9XBOOT.INI` | `enable-ok`, `DpmiMap=08000000:ok` (the 128 MiB heap map), 1024x768x16 |
| V9XDDP at 1024x768x16 and after switching to 32 bpp | `Result=COMPLETE` both; `BltFillPixelOk`, `SrcCopyPixelOk` and the four overlap checks 1 both times |
| V9XMSW `/cycle:10`, `/depth:10` | PASS, 10 of 10; PASS |
| V9XGDI `/auto` at 1024x768x32 and 1024x768x16 | PASS, PASS |
| Registers after (V9XEGMM) | `DMA_RB_CNTL` `1B` (enabled, 2^13 dwords), `DMA_RB_BASE` **`0F0F0000`** (GPU `F_0F000000`), `RPTR` = `WPTR` = `1AC0`, `DMA_CNTL` `28421000`, CP halted |
| `C:\V9XDIAG\V9XEGDMA.TXT` (timeout record) | absent |

`DMA_RB_BASE`'s top byte is the high byte the old code would have dropped.
The pointers at `1AC0h` match Cedar's boot 7 to the byte, the same probe
sequence pushing the same packets, so the blits V9XDDP checked went
through the engine. The desktop screenshot afterwards is clean, back at
the owner's 1024x768x16.

## Not established

- 32 bpp through the HAL. V9XDDP switches to 16 bpp for its own work
  (`AfterModeBpp=16`) in both runs, as on Cedar; 8 and 32 bpp rest on the
  host tests and the probe's byte copies.
- What the monitor shows, and any game. No DirectDraw title was run.
- Cedar on this build. Its high byte is 0 and its paths are otherwise the
  same code, but the HD 5450 is no longer in M-BXMFA8 to boot it.
- A DOS box with the ring running.

## Next

Phase 3: flips, which needs `eg_scanout.c` to take the heap's address and
high byte from the DMA engine instead of the CP.
