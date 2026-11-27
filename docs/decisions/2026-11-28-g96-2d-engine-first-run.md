# GeForce 9500 GT (G96): the GeForce 210's channel and 2D engine run on a G9x

Date: 2026-11-28

Phase 3 of [docs/plans/nvidia-g96-geforce9500gt.md](../plans/nvidia-g96-geforce9500gt.md),
GitHub issue 30, on M-Z2T02D (ASRock B75M R2.0, i3-3250, 9500 GT
`10DE:0640` rev A1, 256 MiB GDDR3). Follows the
[register probe](2026-11-28-g96-register-probe.md).

## BAR1 before the claim

`V9XN5MM /fbwrite` (probe build `7b43193c-dirty`, boot 9, Standard VGA
driver): BAR1 at `E0000000` held its pattern at 16, 32, 64 and 128 MiB,
none landed on BAR1 + 0, and every page was restored. The last target,
256 MiB - 4 KiB, did not take the pattern (`Patterned=0`, restored): the
top of this 256 MiB card's memory, where the PRAMIN window sits
(`0FF00000`), is not writable through BAR1. On the GT218's 512 MiB card
that page was mid-memory. The driver maps 128 MiB, which the test
covers. `/fb` read nothing useful: it maps BAR3 only when it is 64-bit
prefetchable, and this card's is not (`F4000004`), so its BAR3 windows
read as zero.

## The change

- `gt218_hw16.c`: a `v9x_g96_device` row (`10DE:0640`) on the GT218's
  BAR1 map (128 MiB), heap and reserve, the same engine type (NV50) and
  BAR0 map, caps `SOLID_FILL | SCREEN_COPY` only. No `D3D`: the 3D class
  for a G96 is NV84_3D (`8297h`) and `d3d_nv50.c` binds `8597h`. No
  `FLIP`: this VBIOS's 4F07 sequence is not read.
- Mini-VDD (`loader.asm`): the BAR0 map accepts PMC_BOOT_0 chipset 96h
  beside A8h. A short jump past the new check went out of range and was
  made near.
- `nv50_init.c`: the VM trap enable by chipset, `g84_fb`'s `001D07FFh`
  for 84h to A0h, `gt215_fb`'s `000D0FFFh` for A3h to A8h and AFh
  (nouveau `subdev/fb/g84.c`, fetched for this). A host test asserts the
  G96 value and was seen failing first.
- `nv50_grctx.c` unchanged; its pre-A0h branches are nouveau's. A host
  test checks the 96h program: 114 words, ends, every branch inside it;
  image 104,192 words (407 KiB), inside the 512 KiB context slot.
- Backend, family manifest and INF: the id claimed beside the GT218's.

Gates: `run-checks` passed.

## On the card

The package (build `53c77cdf-dirty`) was installed by Have Disk from
`C:\V9XNV` onto the "Standard PCI Graphics Adapter (VGA)" devnode
(hardware version 161 = rev A1), driven by keyboard over the relay;
screenshots at 4 bpp came back scrambled. The HD 2500's installed
`intel-gma` 0.60.0 files share these names and were overwritten; copies
are in `C:\V9XDIAG\INTELBAK` on the machine. Restarted with the agent's
`reset`.

Boot 10:

- `V9XBOOT.INI`: `enable-ok`, 640x480x8, `ApertureFromBar=b=e0000000`,
  `DpmiMap=08000000:ok`, `VddReserve ... vram=134217728`.
- `V9XHW.INI`: `Adapter=NVIDIA GeForce 9500 GT`,
  `GdiAcceleration=gdi-fill-copy-overlap`, `EngineCaps=00000003`,
  `Direct3DMode=none`.
- `V9XDDH.INI`: `setinfo-ok`.
- V9XGDI `/auto`: `Result=PASS`. V9XMSW: `Result=PASS`, two cycles.
- V9XDDP: `Result=COMPLETE`, `GblHalVidMemTotal=0x05DB5000` (93.7 MiB:
  128 MiB less the reserve, the 32 MiB texture pool and the screen).
  `BltFillPixelOk`, `SrcCopyPixelOk`, `KeySrcFillPixelOk` and the four
  `Overlap*PixelOk` keys are 1. `FlipPixelOk=0`, as on the GT218 before
  its flips. The Direct3D keys are 0: no Direct3D device.
- V9XTRACE `-dd` snapshot: `EngineType=16` (NV50), `EngineFlags=1`,
  `EngineResets=1`, `EngineFifoTimeouts=0`, `EngineIdleTimeouts=0`.

That it was the engine, read from the card afterwards with V9XN5MM's
read-only pass:

| Register | Value | Reading |
|---|---|---|
| 200h | `C0311111` | PGRAPH now on (was `C0110111` under Standard VGA) |
| 2604h | `8007E042` | channel 1's RAMFC at `07E04200`, in the reserve at `07E00000` |
| 32F4h / 32ECh | `00007E20` / 1 | the driver's runlist, one channel |
| 40032Ch | `80007E00` | PGRAPH holds this channel's context |
| 2100h, 400100h, 400108h | 0 | no PFIFO or PGRAPH interrupt, no trap |

The IB fetch counters (3330h/3334h) are outside V9XN5MM's list and were
not read.

## Not done

- Engine page flips: read this VBIOS's 4F07 handler from the survey's
  ROM dump, or measure the start address with V9XN5MM `/startmove`.
- Direct3D (Phase 5): the 3D class and method audit.
- Only 640x480x8 was run. 16 and 32 bpp and the owner's 1440x900 are not.
- `1540h` reads `F3010001` here as on the GT218 (one TP bit, one
  partition bit); for a 9500 GT that is fewer units than expected, and
  the 2D engine does not depend on it. Unexplained.
