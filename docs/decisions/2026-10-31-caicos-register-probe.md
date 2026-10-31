# The HD 7470's registers: 2 GiB of DDR3 at GPU F_00000000, no MC microcode needed

Date: 2026-10-31
Status: **Phase 1 read done** on M-D7TFW4
([plan](../plans/ati-evergreen-juniper-caicos.md)). One run, at the
owner's 1024x768x16, on boot 3 with 0.33.0 `vbe`. Nothing was written to
the device.
Capture: [`docs/probe/m-d7tfw4-caicos-registers-2026-10-31/`](../probe/m-d7tfw4-caicos-registers-2026-10-31/).

## The probe

V9XEGMM, the Cedar register probe, refused every id but `68F9` in both the
EXE and EGMM.VXD. Changes for this run (`63936bb9-dirty`):

- Both halves accept `1002:6778` (Caicos) and `1002:68B8` (Juniper). The
  status bit for the refusal is now `NOT_ACCEPTED`.
- New plain reads, each a register Linux reads with no side effect:
  `MC_SEQ_SUP_CNTL` 28C8h, `MC_IO_PAD_CNTL_D0` 29D0h and `MC_SEQ_MISC0`
  2A00h (`nid.h`, now fetched into `build\external\evergreen\linux-radeon\`
  with `ni.c`), `CC_SYS_RB_BACKEND_DISABLE` 3F88h, `RLC_GFX_INDEX` 3FC4h,
  `GRBM_GFX_INDEX` 802Ch, `CC_GC_SHADER_PIPE_CONFIG` 8950h,
  `GC_USER_SHADER_PIPE_CONFIG` 8954h, `CC_RB_BACKEND_DISABLE` 98F4h,
  `GC_USER_RB_BACKEND_DISABLE` 9B7Ch.
- Not read: the RCU fuse straps behind `RCU_IND_INDEX`/`_DATA`, which is
  where Juniper keeps its backend-disable mask. Reading them takes an
  index write.
- Four display controllers, as on Cedar. Caicos has four (`radeon_asic.c`);
  Juniper's fifth and sixth are not read yet.

## What the card reports

| | Caicos (M-D7TFW4) | Cedar (M-BXMFA8, 2026-10-26) |
|---|---|---|
| Config address | `80010000` (bus 1, device 0) | not compared |
| BAR0 | `C000000C`, 64-bit prefetchable, high half 0 | `D0000000`, 64-bit |
| BAR2 (registers) | `FEA20004`, 64-bit, high half 0 | `FEAE0000` |
| BAR4 | I/O `E000` | I/O `D000` |
| `CONFIG_MEMSIZE` | **2048 MiB** | 1024 MiB |
| Memory | 2 channels, 64-bit; `MC_SEQ_MISC0` 31:28 = `B` (not GDDR5) | 64-bit |
| `MC_VM_FB_LOCATION` | `0F7F0F00`: VRAM at GPU **`F_00000000`-`F_7FFFFFFF`** | VRAM at GPU 0 |
| D1 scanout | `GRPH_PRIMARY_SURFACE_ADDRESS` 0, `_HIGH` `0F`: VRAM's first byte | GPU 0 |
| `GB_ADDR_CONFIG` | 0 (BIOS does not set it) | 0 |
| `DMA_TILING_CONFIG` | `02011203` | `02011203` |
| `HDP_ADDR_CONFIG` | `02011003` | |
| CP | `CP_ME_CNTL` `14000000`: ME and PFP halted | the same |
| DMA engine | not in reset, ring disabled, idle (`DMA_STATUS_REG` `44C83D57`) | the same |
| GART | `VM_L2_CNTL` `0005C002` (bit 0 clear), context 0 off | off |
| `MC_SEQ_SUP_CNTL` | 0 (RUN clear) | not read |
| Backend disable (`CC_RB_BACKEND_DISABLE` 23:16) | 0: the one backend is enabled | not read |
| SIMD disable (`CC_GC_SHADER_PIPE_CONFIG` 31:16) | `FFFC`: two SIMDs | not read |

D1 is the only enabled controller (`CRTC_CONTROL` bit 0): 1024x768, 16 bpp
format 1 (565), pitch 1024 pixels, the surface at VRAM offset 0. D2 has its
graphics enabled at the same surface and its CRTC off. Every register read
the same twice except D1's CRTC position and H/V counters, as expected.

## What it settles

1. **No memory-controller microcode.** `ni_mc_load_microcode` loads
   `CAICOS_mc.bin` only when `MC_SEQ_MISC0` 31:28 reads 5 (GDDR5) *and*
   `MC_SEQ_SUP_CNTL` RUN is clear. This card's memory type is `B`. RUN is
   clear, but the memory is not GDDR5, so Linux would skip the load. The
   plan's decision 3 drops out for this board. A GDDR5 Caicos would raise
   it again.
2. **Caicos's shape is Cedar's.** One backend, none disabled; two SIMDs;
   the DMA engine and CP in the state Cedar's bring-up started from.
3. **The GPU address of VRAM is above 4 GiB.** On Cedar, VRAM sat at GPU 0.
   Here it starts at `F_00000000`, as Carrizo's carve-out does at
   `F4_00000000`. `v9x_eg_fb_start` (`eg_dma.c`) refuses an
   `FB_LOCATION` start field of 100h or more, so the shipped Evergreen
   engine would decline this card rather than send it wrong addresses.
   Phase 2 has to carry 40-bit GPU addresses through the DMA packets
   (destination high bits 23:16, already in the packet layout), the CP
   streams and `eg_scanout.c`'s `_HIGH` write. The VI path already does
   this for Carrizo.
4. **2 GiB of VRAM is inside the mini-VDD's 256 MiB-4096 MiB acceptance
   window** (`loader.asm`, `eng_eg.c`). The BAR0 size was not read, so
   whether the engine window at BAR0 + 240 MiB fits is still open. Windows
   assigned `C0000000`; 256 MiB would end at `CFFFFFFF`.

## Not measured

- BAR0's size (needs a BAR-sizing write, so not done here; the LogConfig
  in the registry has it).
- Other VBE modes. Cedar's probe ran at three; this run used one.
- The DMA engine's behaviour. That is Phase 2's write probe.

## Next

Phase 2 on Caicos: V9XEGDMA's write probe with 40-bit destinations, after
the ring's GPU address is computed from `FB_LOCATION` instead of assumed
to sit below 4 GiB.
