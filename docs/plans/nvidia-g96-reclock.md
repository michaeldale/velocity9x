# GeForce 9500 GT (G96): run core and shader at the VBIOS's top level

Status: steps 1-4 done 2026-11-28 (the driver raises core and shader at
bring-up; boot 20 on M-Z2T02D); the clocks are reported by the HAL in
`V9XN5CLK.INI` and V9XTRACE's `[EngineClock]` (boot 21), not in
`V9XHW.INI`, which is written before the raise. Step 5 remains.
Measurement in
[2026-11-28-g96-clocks-measured.md](../decisions/2026-11-28-g96-clocks-measured.md).

## Why

The card runs where POST left it: core 400 MHz, shader 800 MHz, memory
499.5 MHz. Its VBIOS perf table's level 3 is 550 / 1400 / 500. Memory is
already there. Core and shader are not, and nothing in the driver changes
them. The NVIDIA driver does; nouveau does too on chipset 94h and up
(`g84_clk_new`, `allow_reclock`).

## The targets, checked against the VBIOS's PLL limits

BIT 'C' table version 21h, reference 100 MHz (PCIe) for both:

| PLL | VCO range | input range | now | level 3 |
|---|---|---|---|---|
| NVPLL 4028h (core) | 500-1000 | 14-75 | N 16 M 2 P 1: VCO 800, 400 | N 11 M 2 P 0: VCO 550, **550** |
| SPLL 4020h (shader) | 1000-2000 | 50-100 | N 16 M 1 P 1: VCO 1600, 800 | N 14 M 1 P 0: VCO 1400, **1400** |

Both targets are inside the limits at P 0. Shader stays at least twice the
core, which nouveau warns some chips need.

## How nouveau does it (`clk/nv50.c` nv50_clk_calc, `bus/nv50.c`)

One PBUS hwsq script (up to 64 words at 1400h, kicked by 130Ch = 3,
done when 1308h bit 8 clears), so the switch is atomic with respect to the
CPU:

1. 2504h = 1: block PFIFO; wait 8 us.
2. disable FB (hwsq flag 10h = 0), wait for it.
3. C040h mask B3h -> 81h: core onto dom6, shader onto hclk.
4. 4028h mask C03F0100h -> 80000000h | P<<19 | P<<16; 402Ch low word
   N<<8 | M.
5. 4020h the same with the shader's P; 4024h N<<8 | M; C040h mask
   100033h -> 33h: both back on their PLLs.
6. enable FB, wait; 2504h = 0.

## Voltage

Level 3 wants voltage id 64h, 1.00 V (VID 0); level 0 wants 5Fh, 0.95 V
(VID 1). One GPIO carries VID 0: line 5 (DCB GPIO function 04h, entry
200004E5h, so the line reads inverted by nouveau's `log` bits). The
current state is read by the next V9XN5MM run (E104h bit 22). If the line
already gives 1.00 V, the switch needs no voltage change. If not, the
GPIO is raised first, as nouveau's `nvkm_volt_set_id` does before a
faster clock.

## Steps

1. **Read the VID line** (V9XN5MM, read-only, built): decide whether
   voltage moves.
2. **A write probe** (`V9XN5CK.EXE`, its own VxD): builds the script
   above from the measured words, runs it with no Direct3D application
   open, reads 4020h-402Ch and C040h back, and leaves the clocks there
   until the next reboot (POST restores 400/800). Pass: the read-back
   decodes to 550/1400, the desktop and the agent stay up, V9XDDP and
   V9XD32 pass afterwards. Failure mode is a hung card: the owner
   power-cycles, so it runs only after the owner is told.
3. **Measure** at the new clocks: V9XDDP's engine fill and copy counters,
   one 3DMark 2001 SE run, Quake 3 demo001. Recorded, not compared or
   chased.
4. **Into the driver**: the choice of N, M, P from the PLL limits and the
   perf table is pure arithmetic, host-tested (`src/common`, the MTRR
   pattern); the nv50 backend runs the script at enable, before the
   channel starts, and writes `ClockDetector=nvidia-g96-pll-v1`,
   `CoreClockKHz`, `ShaderClockKHz`, `MemoryClockKHz` to `V9XHW.INI`.
   No INI key or verb gates it (default on).
5. **The GeForce 210**: read its clocks the same way (`clk/gt215.c`
   registers) before deciding whether it needs the same.

## Not in scope

Memory reclocking (it is at level 3 already, and nouveau's nv50 memory
path is its riskiest). Fan control. Dynamic levels.
