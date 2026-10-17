# Haswell Phase 2: the display reads as VBE left it; the GT reads as zero

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE on the `vbe` family
at 640x480x8. `V9XHSWMM.EXE` + `HSWMM.VXD` (build `24808bd9-dirty`),
read-only: each offset read twice, no register written. Evidence:
`docs\probe\hsw-4570-mmio-2026-10-17\V9XHSWMM.INI`.

Status `0000006D`: IGD at 00:02.0 is Intel, MMIO (`F7800000`, 2 MB) and GTT
(`F7A00000`, 2 MB) mapped, 95 offsets accepted, none refused, the GTT read.
No reading moved between the two passes.

## Display

| Register | Value | Reading |
|---|---|---|
| `PIPE_HTOTAL_A` | `031F027F` | 640 active, 800 total |
| `PIPE_VTOTAL_A` | `020C01DF` | 480 active, 525 total |
| `PIPE_SRCSZ_A` | `027F01DF` | 640x480 |
| `PIPE_CONF_A` | `C0000010` | enabled, active |
| `PRI_CTL_A` | `C8000000` | enabled; format field 0010b |
| `PRI_STRIDE_A` | `00000280` | 640 bytes |
| `PRI_SURF_A` | `00000000` | GTT offset 0, the aperture base `E0000000` |
| `TRANS_HTOTAL_A`..`VTOTAL_A` | equal to pipe A | PCH transcoder A carries it |
| `DAC_CTL` | `83C40000` | bit 31 set: the analog port is on |
| `VGA_CONTROL` | `8000298E` | bit 31 set: legacy VGA plane disabled |
| `PWR_WELL_CTL1` | `C0000000` | display power well requested and enabled |
| `PWR_WELL_CTL2`-`4` | `40000000` | state bit only |
| `LCPLL_CTL` | `44000037` | not decoded |

That is the 640x480 at 60 Hz VBE mode the driver set, scanned from GTT
offset 0 by plane A through pipe A, PCH transcoder A and the DAC to the VGA
port. The PRI_CTL format value 0010b is read as 8 bpp indexed from the
mode's depth; the Vol 2c encoding table was not checked. `PWR_WELL_CTL3`,
`CTL4` and their meaning are from i915, not the public PRM.

## GTT

The first 4096 entries are `DD200001`, `DD201001`, ...: valid, uncached
(cacheability 0000b), consecutive 4 KB pages from BDSM `DD200000`. That is
the BIOS mapping stolen memory linearly through the aperture, the same
arrangement the 945GSE measured. 267,493 entries (`000414E5`) of the 524,288
are valid in the whole table, about 1 GB of mappings against 32 MB of
stolen memory; where the rest point was not read and is the next GTT
question.

## GT: everything reads zero

The render command streamer's first 256 bytes (ring HEAD/TAIL/START/CTL,
ACTHD at 2074h, EIR/EMR/ESR at 20B0h-20B8h), the blitter ring registers,
`A090h`/`A094h` (RC6 control and state, i915 names), `A188h` (the forcewake
request Vol 12 names) and the forcewake acknowledge at `130044h`: all
`00000000`.

Two readings fit and this probe cannot choose between them:

- the GT is idle and unprogrammed - no ring was ever set up, which is what
  a VBE-only boot would leave; or
- the GT is in RC6 and its registers are not answering - the reason HSW
  Vol 2c tells software to "set the Force Wakeup bit" before touching the
  render ring.

A zero forcewake acknowledge is consistent with both. Telling them apart
needs a write: a forcewake request, then a read of the acknowledge and of
the ring registers. That is the first write to this GPU and is designed
separately.

## Public documentation gap

The HSW PRM's register list gives no address for MULTIFORCEWAKE, RC6
control, ACTHD or EIR on this generation; Vol 12 names `0xA188` and the
status at `0x130044` only in passing. Those four offsets are i915's.
