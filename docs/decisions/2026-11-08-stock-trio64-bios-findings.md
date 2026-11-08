# Stock Trio64-family BIOS behaviour that reaches this driver

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Every claim
cites the record with the log, emulator run or card measurement behind it.
Facts about the stock BIOSes, the chips and 86Box only.

## 1. Interlace: modes the stock BIOSes only offer interlaced

Measured on the stock Phoenix 1.5-07 (Trio64), emulator, every refresh index
walked ([#68](https://github.com/perchrh/trio64-vbios-tools/issues/68)):

- 106h (1280x1024x4), 118h (1024x768x16M), 119h and 11Ah
  (1280x1024x32K/64K) and 120h (1600x1200) exist only interlaced.
- 1024x768 and 1280x1024 at 8, 15 and 16 bpp default to their lowest
  refresh entry, which is interlaced; LCDs report out of range.
- 800x600 defaults to 56 Hz, not the VESA DMT 60 Hz.

The S3 1.03-08 base of the Trio64V+ shows the same family of faults: its
1600x1200 entry runs interlaced (S3COMPAT warning,
[765-rom](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/765-rom.md)).

What it means here: the shared S3 mode table has 0x0118 (1024x768x32) and
0x011A (1280x1024x16), and nothing in the driver knows about interlace. On a
card whose stock BIOS accepts one of those numbers, 4F02h succeeds and the
mode comes up interlaced; `ValidateMode` and the runtime scan cannot see it.
A 4F01h check of the ModeAttributes bits (bit 3, interlaced mode) or a CR42
bit 5 read after the set would catch it. Not urgent: every stock BIOS
measured either refuses 0x0118 on 2 MB cards or runs it interlaced on 4 MB
ones.

## 2. Refresh defaults the stock 2.04.07 picks for the Trio64V2

Measured on the stock S3 2.04.07 (Trio64V2/DX), emulator and card
([#64](https://github.com/perchrh/trio64-vbios-tools/issues/64),
[#65](https://github.com/perchrh/trio64-vbios-tools/issues/65),
[#56](https://github.com/perchrh/trio64-vbios-tools/issues/56)):

- CR41 = 08h and CR52 = 80h select interlaced timings: 107h (1280x1024)
  about 87 Hz fields at 46.7 kHz, 120h (1600x1200) about 98 Hz fields at
  63.7 kHz. LCDs and many CRTs report out of range. Windows 98's own S3
  driver picks no rate itself; it gets the interlaced default.
- Confirmed on a card: with the stock ROM, a monitor reports out of range
  at 1280x1024 and 1600x1200.
- The 640x480 tables run 792 or 796 pixels per line instead of VESA's 800,
  so one LCD preset cannot cover the class.
- Mode 120h displays 1201 lines (CR12 = B0h) while 4F01 reports 1200.

The 2.04 BIOSes offer the S3 OEM function 4F14h to choose a rate: BL = 01h
sets the rate the next set of a mode uses, BH = 01h reads it, BH = 02h lists
the rates (chip-rules, "S3 OEM functions (4F14h)",
[chip-rules.md](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/chip-rules.md)).
No Trio64 or Trio64V+ BIOS offers 4F14h.

What it means here: this driver, like S3's, sets no rate, so on a stock V2
ROM the high-resolution modes come up at whatever CR41/CR52 hold. 4F14h
BL = 01h is a route to 60 Hz on the V2, if the family ever wants it.

## 3. 4F15h (DDC): interrupts off for the whole call, and a PIT write

Measured on the stock 2.04.07, on the card
([#117](https://github.com/perchrh/trio64-vbios-tools/issues/117)): 4F15h
BL = 00h runs with interrupts off from start to end and takes 66 ms; an
EDID read (BL = 01h) takes longer. The BIOS tick falls behind and a UART
without a FIFO can lose bytes. The 1.03-08 base reprograms PIT channel 0
to the standard rate inside its DDC delay on every call.

What it means here: the mini-VDD's EDID collection calls 4F15h once at init,
at ring 0, after the mode scan (`loader.asm`). Its ordering already contains
a hang. The two hazards it does not contain: 66 ms or more with interrupts
off during VDD init, and a PIT channel 0 reprogram that leaves the rate
standard but discards whatever the system had programmed.

## 4. Grey-scale summing: the stock POST monitor sense can switch it on

Measured in the emulator with the sense line modelled as a colour monitor, a
monochrome monitor and nothing attached, on the stock 2.04.07, 1.03-08 and
1.5-07 ([#134](https://github.com/perchrh/trio64-vbios-tools/issues/134)):
POST shows black, blue and green in turn, reads the RAMDAC sense comparator
(3C2h bit 4), and on a monochrome reading switches the BIOS state to
grey-scale summing (mode 13h). BIOS palette loads then come out grey. INT
10h AX = 1200h BL = 33h switches it off.

What it means here: an LCD or a converter that reads as monochrome at POST
leaves the palette summing grey, from the POST screen onward. No module here
touches AX = 1200h. If a palette ever looks washed out on a working panel,
this is the first thing to read.

## 5. 4F02h clears all of video memory unless bit 15 is set

Measured in the emulator
([#131](https://github.com/perchrh/trio64-vbios-tools/issues/131)): a 4F02h
mode set with the clear bit absent clears all of video memory, 4 MB
included - a traced run burned more than 50 million instructions on the
clear alone.

What it means here: nothing to change. `v9x_vbe_mode_flags = 0x8000` and
the S3 family's `V9X_HW16_VBE_NO_CLEAR` are the measured right choice; this
is the measurement behind it.

## 6. The S3 RAMDAC has no 8-bit LUT; 4F08h always answers 6

From the Trio64 data book (no width bit in SR18, CR55 or the palette ports)
and the emulator run of S3's own Windows 9x driver
([spec-c-vbe, VBE-80](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/cleanroom/spec-c-vbe.md)):
4F08h returns 6 whatever BH asks, and nothing in the hardware changes.
S3's driver asks for 8 bits and ends up with a 6-bit DAC in mode 03h.

What it means here: the open record
[2026-11-06-vbe-8bit-dac-unused.md](2026-11-06-vbe-8bit-dac-unused.md)
plans to call 4F08h and read back the granted width. On the S3 family the
read-back always says 6, so the plan is right and yields nothing there. No
separate S3 work is owed.

## 7. The stock 4F04h save/restore covers ADVFUNC_CNTL

The D3 (Super VGA) class of 4F04h saves and restores ADVFUNC_CNTL (4AE8h)
bits 0, 2, 4 and 5, with the data book's write rules
([spec-c-vbe, VBE-74](https://github.com/perchrh/trio64-vbios-tools/blob/main/docs/cleanroom/spec-c-vbe.md)).

What it means here: this is the mechanistic link missing from
[2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md](../issues/2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md).
A DOS-box transition runs a VBE save/restore through that class, so the
path that clears ENB EHFC behind the driver is a 4F04h D3 cycle, and a D3
restore is a second cure besides a mode set. The shipped fix re-asserts the
bit per operation, which covers both; nothing is owed.

## 8. Two caveats

- 4F03h never returns bit 15, even when the mode was set with it (VBE 1.2,
  measured in the emulator). The mini-VDD captures the entry mode with
  4F03h and puts it back at the end of its sweep (`loader.asm`): if that
  restore ever goes through 4F02h with the returned BX, the memory clears.
- The stock BIOSes may end a mode set with the 3C0h attribute flip-flop
  pointing at data
  ([#52](https://github.com/perchrh/trio64-vbios-tools/issues/52)); a read
  of 3DAh resets it to index. No module here writes 3C0h directly today, so
  this is a footnote for whoever does.

## Checked and already handled

- 4F02h accepts modes that do not fit the memory on the 1.03-08 base
  ([#107](https://github.com/perchrh/trio64-vbios-tools/issues/107)):
  `ValidateMode` plus the VBE 1.2 runtime scan already refuse or hide them.
- 4F06h changes the upper halves of EAX, ECX and EDX on the 1.03-08 base
  ([#108](https://github.com/perchrh/trio64-vbios-tools/issues/108)): this
  driver's 4F06h callers are 8086-compiled C through `#pragma aux`, and the
  mini-VDD never calls 4F06h, so no 32-bit state is exposed.
