# GT218 with no BAR VM: BAR1 + 0 and BAR3 show the desktop, and BAR1 holds writes across 256 MiB

Date: 2026-11-11

Phase 2, second read, of [docs/plans/nvidia-gt218.md](../plans/nvidia-gt218.md),
following the [register probe](2026-11-11-gt218-register-probe.md), which
found the VBE framebuffer at BAR3 + 16 MiB and PBUS 1704h-170Ch all zero.

## What ran

`V9XN5MM.EXE` build `803ae36a-dirty` on M-BXMFA8 boot 82 (tier-0 `nvidia`
driver, 1024x768x16 desktop), two new modes, both read-only:

- `/crtc` (`V9XN5CR.INI`): the VGA CRTC through 3D4h/3D5h, without
  unlocking the extended set; only the index port is written, and put back.
- `/fb` (`V9XN5FB.INI`): N5MM.VXD maps four 2 MiB windows from config
  space (BAR1 `D0000000`, BAR3 `CE000000`, both 64-bit, high dwords zero)
  and reads the same 256 offsets, one every 8 KiB, through each, twice:
  window 0 the VBE framebuffer (BAR3 + 16 MiB), 1 BAR3 + 0, 2 BAR1 +
  16 MiB, 3 BAR1 + 0, in that order. The report is flushed per window.

Both finished `COMPLETE`, exit 0; the desktop stayed up and the agent
answered throughout. Evidence: `build/driver-results/m-bxmfa8-gt218-boot82/`.

## Results

### Video memory windows

| Window | Same as VBE framebuffer | Zero | All ones | Moved |
|---|---|---|---|---|
| 0: BAR3 + 16 MiB (VBE LFB) | 256 (itself) | 22 | 27 | 0 |
| 1: BAR3 + 0 | **256** | 22 | 27 | 0 |
| 2: BAR1 + 16 MiB | 0 | 0 | 0 | 0 |
| 3: BAR1 + 0 | **256** | 22 | 27 | 0 |

The framebuffer's first words are `03EF03EF`, two 5:6:5 pixels of the
teal desktop background (G 31, B 15). Window 2 holds a few words repeated
with no relation to the screen (`AF47C2C9` 47 times, `FEDFE5DC` 36,
`73F77AFF` 21, ...), the look of DRAM nothing has written.

### VGA CRTC

Misc `EB` (colour). CR01 `7F` (horizontal display end: 1024 / 8 - 1),
CR12 `FF` (vertical display end low byte: 767), start address CR0C/CR0D
`0000`. CR13 and every extended index (19, 1A, 1F, 25, 28, 2D) read `00`
while locked.

## What it shows, and what it does not

- **BAR1 decodes video memory with no BAR VM**, at least at offset 0: the
  desktop reads identically through BAR1 + 0. BAR1 + 16 MiB reads
  something else, consistent with linear video memory at 16 MiB that
  nobody has written, but a read alone cannot prove it is linear.
- **BAR3 shows the first 16 MiB twice**: BAR3 + 0 and BAR3 + 16 MiB agree
  on all 256 samples. The VBE puts its framebuffer in the upper copy.
  Whether BAR3 is a 16 MiB window repeated, or two views of one region,
  is not decided.
- **The desktop is at video memory 0**, as far as reads show: BAR1 + 0,
  the VGA start address 0, and nouveau's view that BAR1 is the VRAM
  aperture agree. The head's EVO surface-offset mirror reading 0 in the
  register probe now reads as "offset 0", not "no information".
- Disputes the register probe's "BAR3 is decoding without a VM, how is
  open": the open part is narrower now. Both BARs decode with the VM off;
  nouveau's code only ever sees them after it turns the VM on.

## Write test (`/fbwrite`, same boot, later)

`V9XN5MM.EXE /fbwrite` (`V9XN5FW.INI`), the probe's only mode that writes,
and only video memory through BAR1. N5MM.VXD maps one 4 KiB page at BAR1
+ 0 and at five targets past the VBE's 14 MiB: 16, 32, 64 and 128 MiB, and
the last page of the 256 MiB BAR (`0FFFF000h`). It saves each page's first
16 dwords, writes `A5000000h | window << 16 | byte` into each target
(never into BAR1 + 0), reads all six pages, restores the targets from last
to first, rewrites BAR1 + 0 with its own saved words, and reads all six
again. `COMPLETE`, status `42D`, exit 0.

| Window | BAR1 offset | Own pattern read back | Another window's pattern | Restored |
|---|---|---|---|---|
| 0 | 0 (desktop) | (not written) | 0 | 16/16 |
| 1 | 16 MiB | 16/16 | 0 | 16/16 |
| 2 | 32 MiB | 16/16 | 0 | 16/16 |
| 3 | 64 MiB | 16/16 | 0 | 16/16 |
| 4 | 128 MiB | 16/16 | 0 | 16/16 |
| 5 | 256 MiB - 4 KiB | 16/16 | 0 | 16/16 |

BAR1 + 0 read `03EF03EF` (the desktop) before, during and after: none of
the targets wrapped onto the desktop. The word saved at 16 MiB,
`D3281603`, is the one the read pass found there earlier, so that memory
held its contents between the two runs. The screenshot after the run is
the unchanged desktop.

What it shows: with no BAR VM, BAR1 stores and returns 16 distinct
dwords at each of six offsets spanning the whole 256 MiB aperture, with no
alias between any two of them and none onto the desktop. That is
consistent with BAR1 decoding video memory linearly. What it does not
show: that BAR1 + x is video memory address x (a fixed offset or a
scramble would pass), or that the 256 MiB covers memory below 256 MiB
rather than some other range of the 512. Nothing on the card displays or
reads those addresses yet, so no test here could tell.

## Next

Phase 4's heap item looks reachable before any engine: the DirectDraw
heap can grow from the VBE's 14 MiB towards BAR1's 256 MiB, through BAR1,
with no VM. That needs the driver to map BAR1 rather than the VBE's BAR3
window, which also puts the desktop at BAR1 + 0. The engines still need
their own channel VM; nothing here says anything about that.
