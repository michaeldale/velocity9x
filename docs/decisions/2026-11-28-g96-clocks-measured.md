# GeForce 9500 GT (G96) clocks: POST leaves core 400 and shader 800 MHz below the VBIOS's 550 and 1400; the driver now raises them

Date: 2026-11-28

On M-Z2T02D (GitHub issue 30), boot 18, the driver set of
[2026-11-28-g96-3dmark2001se.md](2026-11-28-g96-3dmark2001se.md). The
owner asked why the 9500 GT scores below the HD 2500 and the GeForce 210.
`V9XHW.INI` said `ClockStatus=unavailable`: nothing in the tree read the
G96's clocks. Evidence:
[g96-clocks-m-z2t02d-2026-11-28/](../probe/g96-clocks-m-z2t02d-2026-11-28/).

## The probe

`N5MM.VXD` gains read-only windows for the registers nouveau's
`nv50_clk_read` reads on chipset 96h (`subdev/clk/nv50.c`): the PLLs at
4000h-403Fh, the divider word at 4800h, the source select C040h, the
reference select C050h and the reference PLLs at E81Ch-E84Fh; and the GPIO
state words E104h-E10Bh and E280h-E287h (`subdev/gpio/nv50.c`).
`V9XN5MM.EXE` asks for them on chipset 96h only (the GT218 clocks through
`clk/gt215.c`'s unit). A new `/prom` mode reads the ROM chip's first
128 KiB through BAR0 + 300000h into `C:\V9XDIAG\V9XN5PRM.BIN`, after
checking that PBUS 1850h bit 0 (the ROM shadow) is clear (it read 0).
Nothing is written to the card.

## The registers, decoded by nouveau's rules

Crystal 27 MHz (PSTRAPS 80408086h, bits 22 and 6 = 1, 0).
Source select C040h = 2E801B33h, reference select C050h = 0000500Eh.

| Clock | Registers | Path | Result |
|---|---|---|---|
| core | 4028h A0010000h, 402Ch 00001002h | C040h bits 1:0 = 3: NVPLL; ref = PCIe 100 MHz (C050h bits 12:11 = 2); 100 x 16 / 2 = 800, post-divider P = 1 | **400 MHz** |
| shader | 4020h 80010000h, 4024h 00001001h | C040h bits 5:4 = 3: SPLL; ref = 100 MHz (C050h bits 1:0 = 2); 100 x 16 / 1 = 1600, P = 1 | **800 MHz** |
| memory | 4008h 90596400h, 400Ch 00002504h | ref = reference PLL 0: 27 x 32 / 1 >> 3 = 108 MHz; 108 x 37 / 4 = 999, P = 1 | **499.5 MHz** |

Memory type 100714h bits 2:0 = 2: GDDR3, so 499.5 MHz is 999 MT/s.

## What the card's own VBIOS asks for

The shadow at C0000h is the image the BIOS trimmed after POST (56,832
bytes): its BIT 'P' perf-table pointer (CDD2h) lands in font data there.
The PROM image (`Version 62.94.2C.00.91`, 63,488 bytes in its first PCIR
image; SHA-256 `338a6f8a...01cb` for the 128 KiB read; not committed, it is
NVIDIA's) holds the table at CDD2h, version 35h, four entries of 29 bytes,
read by nouveau's `nvbios_perfEp` rules for 35h:

| Level | pstate | core | shader | memory | vdec | voltage id |
|---|---|---|---|---|---|---|
| 0 | FFh | 200 | 400 | 100 | 200 | 5Fh |
| 1, 2 | FFh | 0 | 0 | 0 | 0 | 0 (empty) |
| 3 | 0Fh | **550** | **1400** | **500** | 450 | 64h |

The voltage table (BIT 'P' + 10h, D0A6h, version 20h) has two entries:
0.95 V at VID 1 (id 5Fh) and 1.00 V at VID 0 (id 64h). One GPIO selects
it: the DCB GPIO table puts function 04h (VID 0) on line 5.

## What this says

- Memory runs at the clock of the VBIOS's top level, 500 MHz. Nothing to
  raise there.
- Core and shader run at 400 and 800 MHz: the VBIOS's POST values, which
  match neither level. The top level is 550 and 1400, so the core is at
  73% of it and the shaders at 57%.
- The NVIDIA driver moves the card to level 3 when it loads; this driver
  never touches the clocks, so the card stays where POST left it all the
  time. That is one measured reason the G96 is slower than its rated
  speed; how much of the 3DMark gap it explains is not measured (no
  reclocked run exists). The GT218 on W9QI0DAD was never read either.

## The PLL limits

BIT 'C' table version 21h, seven entries, reference 100 MHz for the
clock PLLs. NVPLL (4028h): VCO 500-1000 MHz, input 14-75 MHz. SPLL
(4020h): VCO 1000-2000 MHz, input up to 100 MHz. Level 3 fits both at
P 0: core N 11 M 2 (50 MHz in, VCO 550), shader N 14 M 1 (VCO 1400). A
P 1 core would need VCO 1100, outside the range.

## The voltage

The VID line (GPIO 5, DCB function 04h, entry 200004E5h) reads E104h =
74244440h: line 5's nibble is 2, sense bit clear, so VID 0 by nouveau's
`log` rule: **1.00 V, level 3's voltage already**. POST leaves the
voltage at the top level and only the clocks low, so raising them needs
no voltage change on this board.

## Raising the clocks (step 2 of the plan)

`V9XN5MM /reclock` (this commit) builds nouveau's `nv50_clk_calc` script
for core and shader with the shader on SPLL, encodes it by
`bus/hwsq.c`'s rules, and `N5MM.VXD`'s new `N5_DIOC_HWSQ` runs it as
`g94_bus_hwsq_exec` does (script at 80000h, 130Ch = 1, wait for 1308h
bit 8). It refuses unless the PLLs are at POST's or level 3's values on
the PCIe reference and VID is 0. `/reclock:post` puts POST's clocks
back. The 19-word script (in `V9XN5CK.INI`) blocks PFIFO, disables FB,
unhooks core to dom6 and shader to hclk, writes the four PLL words,
hooks both back (C040h 2E801B33h -> 2E801BB3h, nouveau's bit 7 kept),
re-enables FB and unblocks PFIFO.

Boot 18, the desktop at 1024x768x32, no Direct3D application open:
`Result=COMPLETE`, status 0000082Dh, the sequencer done at once
(4,000,000 spins, 3D08FAh left). The script, as `V9XN5CK.INI` listed
it (that file was later overwritten by the `/reclock:post` run; the
words are also `tests/host/test_nv50_clk.c`'s):

    000001E2 2504E000 B0060000 E201005F 2E801B81 E2C04040 A0000000
    E2402840 00000B02 E2402C40 80000000 E2402040 00000E01 E2402440
    2E801BB3 D0C04040 E200005F 00000000 7F250440

Read back: 4028h A0000000h, 402Ch 00000B02h, 4020h 80000000h,
4024h 00000E01h, C040h 2E801BB3h, 2504h 0, 1308h 4Bh, 1098h 21CA001Ch:
**core 550, shader 1400 MHz**. The agent, desktop and relay stayed up.

At those clocks, same boot:

- V9XD32 (32 bpp): one failure, T5 (24-bit Z, by design, as before); the
  rest pass, T19 by the NVIDIA DXT rule.
- Quake 3 demo001, 640x480x32 windowed: 1346 frames, 29.4 s, 45.8 fps.
- 3DMark 2001 SE, its defaults (16-bit Z): ran to a score of **890**
  (`3dmark2001se-boot18-score.png`, details in
  `3dmark2001se-boot18-details.png`). Recorded, not compared. It did not
  stop with "failed to validate material", the error the owner reported
  once on this driver set (issue 30); that stays unreproduced.

## In the driver (step 4 of the plan)

`src/chipsets/nvidia/gt218/nv50_clk.c` builds the same script as an
`nv50_init.h` op list (three writes, the 19 words at 80000h, two writes,
a wait on 1308h bit 8), with nouveau's `g94_bus_hwsq_exec` order. It
builds a list only from the state measured: chipset 96h, both PLLs on
the PCIe reference, core and shader on their PLLs at POST's coefficients,
VID 0. Anything else, the GT218 included, keeps its clocks. The host test
checks the 19 words against the run above and the refusals.
`eng_nv50.c` replays the list in `v9x_nv50_bring_up`, before the channel
is built, so nothing is on PFIFO; a sequencer that never finishes gives
the bring-up up. Two new external symbols, `v9x_nv50_clk_build` and
`v9x_nv50_clk_at_level3`, in the pattern of `v9x_nv50_init_build`.

Run on M-Z2T02D, the nvidia set from this tree (7ca586c8-dirty: DRV,
VxD, HAL, SETP, ICD) installed by WININIT rename, each hash-checked
after boot 19:

- Boot 19 came up at 550/1400 with no probe run, but the probe had left
  the card there before the reset, so that alone proved nothing.
- `V9XN5MM /reclock:post` then put POST's clocks back (4028h A0090000h,
  402Ch 1002h, 4020h 80010000h, 4024h 1001h: 400/800;
  `V9XN5CK-post-boot19.INI`), and the machine was reset.
- **Boot 20, no probe run before the read: 4028h A0000000h, 402Ch
  0B02h, 4020h 80000000h, 4024h 0E01h, 550/1400**
  (`V9XN5MM-boot20-driver.INI`). The driver raised them.
- V9XD32 on boot 19: T5 only, as before (`V9XD32-boot19.TXT`). Quake 3
  demo001 on boot 20: 1346 frames, 30.5 s, 44.2 fps.

Whether the CF9h reset re-runs POST was not measured separately; the
boot-20 reading does not depend on it, since the card went into the
reset at 400/800.

## The clock record (boot 21)

The 16-bit driver writes `V9XHW.INI` at enable, before the HAL's
bring-up raises the clocks, so the HAL reports them: on chipset 96h it
writes `C:\V9XDIAG\V9XN5CLK.INI` (`[Nv50Clock]`: `Result` raised, kept
or sequencer-timeout; core and shader kHz before and after, decoded by
`v9x_nv50_clk_pll_khz` on the PCIe reference; the raw words), and
V9XTRACE copies it into its report as `[EngineClock]`. Installed by
WININIT rename after boot 20 (hash-checked), reset into boot 21:

    Result=raised
    CoreKHzBefore=400000  ShaderKHzBefore=800000
    CoreKHz=550000        ShaderKHz=1400000
    NvpllCtrlBefore=A0010000 ... Source=2E801B33

(`V9XN5CLK-boot21.INI`, `V9XSNA4-boot21.INI`.) The words before are
POST's exactly, after a boot that had ended at 550/1400, so the CF9h
reset does re-run the VBIOS's POST.

## Not done

- `V9XHW.INI` keeps `ClockStatus=unavailable`; the clocks are in the
  HAL's record instead, for the reason above.
- The GeForce 210 (GT218) clocks were not read (`clk/gt215.c`'s unit).
- Pictures at the new clocks were not inspected beyond V9XD32's pixel
  checks.

Gates: check-tree, build-host (the new `test_nv50_clk.c`), run-checks;
the probe built with `build-nv50-mmio-probe.ps1`.
