# Michael's Mystique is 051A revision 02, with the MGA-2064W's BAR order

Date: 2026-10-19

Status: measured on A8U4I5, boot 505. Phase 0, step 1 of
[matrox-mystique.md](../plans/matrox-mystique.md). No Velocity9x driver
code has run on this card.

## What was measured

Michael fitted his Mystique to A8U4I5 (10.0.1.172). Windows 98 bound
Microsoft's inbox Matrox driver to it (`DXMGA.INF`, `mgapdx64.drv`,
class key `Display\0017`, desktop 800x600).

The plan called for V9XSURV in real DOS. A8U4I5 takes no DOS programs, so a
Win32 reader was written instead: `V9XMGAPC.EXE`
(`tools/diag/matrox_pci_survey_win32.c`, `scripts/build-mga-pci-survey.ps1`).
It reads config space by mechanism #1 (CF8h/CFCh), writes nothing but CF8h,
and applies `v9x_matrox_mga_select_control_bar` with no fallback to what it
read. Build `0106cb97-dirty`. Output in
[`docs/probe/a8u4i5-mga1064sg-pci-2026-10-19/`](../probe/a8u4i5-mga1064sg-pci-2026-10-19/V9XMGAPC.INI):

| | Value | Reading |
|---|---|---|
| Device | 00:08.0, `102B:051A`, subsystem `051A102B` | |
| Revision | **02h** | |
| BAR0 | `DC000000` | memory, not prefetchable: control |
| BAR1 | `DD000008` | memory, prefetchable: framebuffer |
| BAR2 | `DE000000` | ILOAD |
| ROM BAR | `DF000000` | disabled (bit 0 clear) |
| OPTION (40h) | `5F094F21` | not decoded |
| Rule | accepted, BAR0, base `DC000000` | by the prefetch bit, not a fallback |
| UnstableReads | 0 | |

The Windows resource list (`ENUM-051A.REG`, LogConfig) agrees: 16 KiB at
DC000000, 8 MiB prefetchable at DD000000, 8 MiB at DE000000.

The video ROM image at C0000h reads `IBM COMPATIBLE MATROX/MYSTIQUE
VGA/VBE BIOS (V1.6 )`, 32 KiB (40h blocks of 512 bytes). Match451's card
reports V1.5.

Matrox's driver records `ChipType=1064SG`, `DacType=Internal`,
`VideoMemory=00400000` (4 MiB) in `Display\0017\INFO`
(`DISPLAY-0017.REG`). That is the driver's own identification, not a chip
marking.

## What this settles and what it does not

- **The card has the MGA-1064SG's BAR order** (control BAR0, framebuffer
  BAR1), the same as the MGA-2064W and the 1064SG specification (4-11,
  4-12).
- **Its revision is 02h, which no source in this tree mentions.** The
  specification says 00h; 86Box models 00h for the 1064SG and 03h for the
  1164SG; Match451's 1164SG reads 03h. So revisions 00h and 02h have the
  2064W's order and 03h the 2164W's, on the sources we have. Revision 01h
  is unseen.
- **The prefetch-bit rule needs nothing new for this card.** It picks
  BAR0 from the bit, as it did for the 2164W in the other order. A host
  test case for these values is added to `tests/host/test_mga_bars.c`.
- **For the 86Box fallback** (neither BAR prefetchable), Phase 1 needs a
  per-revision order for `051A`. On this evidence "revision below 03h
  means control in BAR0" fits every reading. It is still only three
  points, and 86Box itself never reports 02h.
- **Whether this is a 1064SG or an 1164SG by marking** is not read. The
  BAR order and Matrox's driver both say 1064SG; the chip's top marking
  would confirm it.

Gates: `build-host.ps1` passes with the new test case. `check-tree.ps1`
and `run-checks.ps1` are recorded in the commit.
