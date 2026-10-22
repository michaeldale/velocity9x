# Rage 128: the CCE runs packets fed through its PIO ports, even and odd alternating

Date: 2026-10-22

Status: **measured, one card.** A8U4I5's Rage Fury Pro (`1002:5046`),
boot 522, ati package `9f992739` installed (the CCE in NONPM4, as POST
leaves it). This settles the first half of decision 1 in
[ati-rage128.md](../plans/ati-rage128.md). Evidence:
[`../probe/a8u4i5-rage128-cce-pio-2026-10-22/R128CCE.TXT`](../probe/a8u4i5-rage128-cce-pio-2026-10-22/R128CCE.TXT).

## The question

Can the Concurrent Command Engine take packets the CPU writes to its FIFO
ports (`PM4_FIFO_DATA_EVEN` 1000h, `PM4_FIFO_DATA_ODD` 1004h), with no ring
in memory and no bus mastering? No open source shows this path working:
the Linux 6.2 DRM refuses every PIO mode, X's `CCEPIOMode` only passes the
mode to the DRM, and ATI's own driver runs a bus-master ring
([register probe](2026-10-22-rage128-register-probe.md)). The register
guide names the PIO modes but does not say how the two ports are used.

## The tool

`R128CCE.EXE` + `R128CCE.VXD` (`tools\diag\r128_cce*`,
`scripts\build-r128-cce.ps1`) are R1282D's shape with a wider write list.
That list adds the PM4 block 0700h-071Fh, the microcode ports
07D4h-07E3h, `PM4_MICRO_CNTL`, the two FIFO ports and the 3D context
registers. There are two new ops:

- a PM4 idle wait: `PM4_STAT` free entries, then `PM4_BUSY` and
  `GUI_ACTIVE` clear, then a pixel-cache flush;
- R128EngineReset as a fixed sequence, writing the PLL index a byte at a
  time so the pixel-clock divider select in bits 9:8 is not disturbed.

The microcode is linux-firmware's `r128/r128_cce.bin`: 2,048 bytes, SHA-256
`13a9e203bd49387926088448d2dc45d1e442bfc0646bf2255da5c17c959e3f63`, MIT,
"Copyright 2000 Advanced Micro Devices". It was read from
`C:\V9XDIAG\R128CCE.BIN` at run time and is not in the repository yet.

## Results

| Step | Result |
|---|---|
| At start | `PM4_BUFFER_CNTL` 0 (NONPM4), `PM4_MICRO_CNTL` `40003FFF`, `PM4_STAT` C0h (192 free) |
| Microcode RAM as POST left it (microengine stopped, RADDR then DATAH/DATAL per word) | 13 of 256 words equal the file, 12 zero. Words 0 and 1 are the file's (`00000000 10803800`, `00000000 10003800`) |
| After loading the file (ADDR 0, then DATAH/DATAL pairs) | 256 of 256 words read back equal |
| 192PIO + NOUPDATE, microengine free-running; the r128_2d.c fill as six type-0 packets, header and value **alternating EVEN, ODD** | **PASS**: 0 of 4,096 bytes differ. PM4 idle in 6 reads; `PM4_STAT` C0h, `GUI_STAT` 40h afterwards |
| The same, **every dword to EVEN** | Nothing drawn: 480 bytes differ, exactly the 24x10 fill area left at the prefill. PM4 idle in 5 reads, no hang |
| At end | `PM4_BUFFER_CNTL` `08000000` (NONPM4 + NOUPDATE), `PM4_MICRO_CNTL` `40003FFF` restored |

V9XGDI `/auto` passed afterwards. The driver's own engine path (NONPM4)
was unaffected.

## What it says

- **The CCE runs packets fed through PIO, with no ring.** The FIFO is
  written as pairs: the first dword of each pair to EVEN, the second to
  ODD. Writing everything to EVEN was accepted, left the FIFO empty and
  the engine idle, and executed nothing. So PIO is a working, measured way
  to drive the command engine on this card, and it needs no system-memory
  ring, AGP or DMA from a VxD.
- **Linux's microcode loads and runs.** The RAM read back exactly, and
  the packets executed under it.
- **POST leaves a different program, or leftovers.** The microcode RAM
  after POST matches the file in 13 words, including the first two. That
  could be an older microcode in the BIOS, or partly uninitialised RAM.
  Not established. ATI's driver's microcode was not read back.

## What it changes in the plan

Decision 1's recommendation, CCE in PIO mode first, now has silicon
behind it for type-0 packets. Still owed before Direct3D is built on it:
a type-3 primitive packet (`3D_RNDR_GEN_PRIM`) with the 3D context set,
and its rate against the direct 2D path.

Gates: `run-checks.ps1` passed on the commit carrying the probe.
