# SiS 6326 first survey: two cards, one locks, and four datasheet questions settled

Date: 2026-10-04. Machine: A8U4I5, boots 201-207. Evidence:
`docs/probe/a8u4i5-sis6326-registers-2026-10-04/` (runs A-F, both BIOS
images, a screenshot). Tool: `SIS6326.EXE` + `SIS6326.VXD`, builds `b` and
`c`. Register meanings:
[sis6326-registers.md](../specifications/sis6326-registers.md).

## Measured

Two SiS 6326 cards, both AGP on bus 1 of the 440BX, both 4 MiB.

- **Card 1:** revision C3, subsystem `63261039`, add-on BIOS 1.06 of
  12-18-97, 1-cycle EDO timing (SR23 D5, SR33 D3), SR0C = E4h.
- **Card 2:** revision 0Bh, subsystem `63261569`, BIOS 1.28q of 10/21/1999,
  SGRAM timing (SR33 D0), SR0C = B4h, PCI power-management capability at
  40h.

Under vga.drv the extensions are locked: SR05 and every SR06-SR3F read 21h.

Under SiS/AOpen driver 2.28 (runs C and F), both cards read the same way:

- SR05 = A1h, so the extensions are unlocked.
- SRB = 6Ch: MMIO through BAR1.
- SR27 = C0h: Turbo Queue on, engine registers enabled. SR2C = 7Eh, and
  SR3C D[1:0] = 0, so all of the queue goes to 2D.
- SR39 = 00h: 3D off, as no Direct3D application had run.
- 640x480x8 at a 1024-byte pitch (8288h = 04000400h, CR13 = 80h,
  SR27 D[5:4] = 0).

The 2D registers in both runs hold the same last operation:

- destination 72804h (row 458, column 4 at pitch 1024);
- width/height 828Ch = 00110031h;
- foreground 8290h = CC070707h, ROP SRCCOPY;
- background 8294h = 00C00007h;
- command word 8034h: BitBlt, source from the background colour, X and Y
  incrementing.

**Card 1 locked the machine twice under SiS 2.28.** At boot 202 it stopped
answering ICMP within a minute of boot, before any tool ran. At boot 204 it
stopped about a minute after run C, while an agent screenshot was being
taken. Boot 203 never reached the agent.

**Card 2, under the same driver, probe and board, stayed up** for over ten
minutes at boot 207, through three probe runs and a screenshot.

## What the evidence settles

1. **The MCLK post-scale extension is SR13 D7, and the VCLK one is SR13 D6,**
   as SR13's own definition says. The SR29 and SR2B texts have the two
   swapped.
   - MCLK: card 1's 7Ch/E7h decodes to 55.9 MHz and card 2's B3h/C5h to
     82.7 MHz. Those are xf86-video-sis's "55/56" and "83" MHz table
     entries; the swapped reading gives 28 and 41 MHz.
   - VCLK: SR2A/SR2B = 1Bh/E1h decodes to 25.06 MHz with D6 set, the
     640x480 dot clock; the swapped reading gives 50.1 MHz.
2. **SRC D[2:1] gives the installed memory on both cards** (10 = 4 MiB, and
   BAR0 is 4 MiB). SRC D4 is not reserved in practice: it is 0 on card 1 and
   1 on card 2. Xorg reads it as bus width (64-bit vs 32-bit). The boards
   have not been inspected, so that reading is unconfirmed.
3. **Width and height are programmed as n-1, width in bytes** (inference,
   strong). The screenshot puts the Start button at x 2-55, y 456-477. Its
   face inside the 2-pixel bevel is x 4-53, y 458-475: 50x18 pixels, at
   exactly the destination the registers hold. The registers say 49 and 17.
4. **SiS's own driver runs the Turbo Queue and MMIO through BAR1 from the
   first frame.** Both are documented paths the 2D engine will use.

## Hypotheses this kills

- "The probe causes the locks." Boot 202 locked with no tool run. Card 2
  ran the same probe three times under the same driver without a lock.
- "SiS 2.28 locks this board." It runs card 2.
- REF §14.1's SR0E[1:0] DRAM-type decode. Card 1 reads SR0E[1:0] = 01
  ("fast page" by that decode) while its timing enables say 1-cycle EDO.
  Card 2 reads 11 with SGRAM timing. The datasheet calls these bits
  reserved, and the evidence agrees they are not the type.

## Not established

- **Why card 1 locks:** the card, its EDO memory, its 1997 BIOS, or its
  interaction with this board's AGP. It held up under vga.drv for 25
  minutes, so the trigger needs SiS's driver. No measurement here separates
  those causes. Card 2 is the bring-up card from now on; card 1 is parked.
- **The bus-width meaning of SRC D4.**
- **Anything about 3D.** SR39 was 0 in every run.
- **Revision order.** Which of C3 and 0Bh is the later silicon is not
  known; neither matches the datasheet's Ax/Bx.

## Probe defects found and fixed on the way

- **Build a** decoded locked registers and followed the bogus window into
  VGA memory.
- **Build b** stopped reading anything while locked, but still took the
  first `VEN_1039&DEV_6326` Enum key. That was card 1's stale entry, so card
  2 got no Config Manager ranges.
- **Build c** tries each instance until Config Manager locates a present
  devnode.

Gates: check-tree; the probe builds with `-wx`.
