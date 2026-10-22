# Rage 128: the CCE runs packets through its PIO ports, and draws a triangle

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

## A triangle through 3D_RNDR_GEN_PRIM (2026-10-23, boots 522-523)

The probe then sent one untextured triangle, (4,2), (60,2), (4,30) in opaque
blue, through PIO. Its state followed X's Render path (`r128_exa_render.c`
`COMPOSITE_SETUP`): the 2D destination with `GMC_3D_FCN_EN`,
`SCALE_3D_CNTL`, `SETUP_CNTL` (Gouraud, vertices 1 to 3, 4 sub-pixel
bits), `PM4_VC_FPU_SETUP` (no culling), `PLANE_3D_MASK_C`,
`MISC_3D_STATE_CNTL_REG` (blend one, zero) and `TEX_CNTL_C` (shade only).
The primitive was `3D_RNDR_GEN_PRIM` with `DIFFUSE_ARGB` vertices,
`TRI_LIST`, three vertices. Evidence: `R128CCE-triangle-boot523.TXT`
beside the first capture.

| Variant | Changed pixels (area 784) | Pixel at (10,6) | Outside the box | Result |
|---|---|---|---|---|
| X's 2D-register state only | 0 | prefill | 0 | Nothing drawn, idle in 4 reads |
| + the context block's first four: `DST_PITCH_OFFSET_C`, `DP_GUI_MASTER_CNTL_C`, `SC_TOP_LEFT_C` 0, `SC_BOTTOM_RIGHT_C` 1FFF1FFFh (type-0, count 4) | **784** | `001F`, the blue | 0 | **Drawn** |
| + an RHW of 1.0 in each vertex | **784** | `001F` | 0 | **Drawn** |

The pixel at (58,28), just outside the hypotenuse, kept the prefill in
both. The 3D path clips to the context scissor, which POST leaves at zero
(`SC_BOTTOM_RIGHT_C` read 0 in the
[register probe](2026-10-22-rage128-register-probe.md)). X's Render path
does not write it because the DRM's context emit has, by then.

**A hang, and what it was not.** The first triangle run (boot 522, only
the plain variant) finished, then restored `PM4_MICRO_CNTL` to its POST
free-run value with this microcode loaded. About a minute later, idle at
the desktop, A8U4I5 stopped answering ping. It needed a power cycle from
Michael (boot 523). The agent's log ends with that run's completed
output. The next upload's `.PART` file is whole but was never renamed. So
the hang was not in the three variants above, which had not yet run. The
probe now stops the microengine and resets the engine after every
triangle, and leaves `PM4_MICRO_CNTL` at 0 (as `r128_do_cce_stop` does).
After the boot 523 run it answered ping for four minutes and V9XGDI
passed. What exactly hung is not established: the reading most
consistent with it is a microengine left free-running behind a packet
it had not finished.

## What it changes in the plan

Decision 1 is taken on measurement: **Direct3D goes through the CCE in
PIO mode**, pairs to EVEN/ODD. Type-0 register writes and a type-3
primitive both executed. The 3D context block, including the 3D scissor,
must be written before any primitive. The microengine runs only while the
driver is submitting, and is stopped with an engine reset before NONPM4
direct writes resume. Owed: textures, Z, the rate of PIO submission, and
whether a bus-master ring is needed for speed.

Gates: `run-checks.ps1` passed on the commit carrying the probe.
