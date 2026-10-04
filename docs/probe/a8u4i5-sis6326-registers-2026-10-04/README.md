# A8U4I5: SiS 6326 first contact, 2026-10-04

- Machine: A8U4I5 (10.0.1.172), boot 201, agent 0.8.0.
- Card: SiS 6326, PCI `1039:6326`, subsystem `63261039`, **revision C3**, on
  bus 1 device 0 (behind the AGP bridge), AGP enabled with 2X (cfg 58h =
  `01000102h`).
- Driver: none from SiS. Windows runs the standard VGA driver, 640x480x4.
- Tool: `SIS6326.EXE` + `SIS6326.VXD`, `scripts/build-sis6326-probe.ps1`.

| File | Build | Notes |
|---|---|---|
| `SIS6326-A-VGADRV.TXT` | `sis6326-20261004-a` | First build. Its decodes and `M_*` values are **invalid**, see below |
| `SIS6326-B-VGADRV.TXT` | `sis6326-20261004-b` | Fixed build, no `/unlock`: `Result=LOCKED`, no MMIO read |
| `SIS6326-CARD1.ROM` | `sis6326-20261004-a` | Card 1, 64 KiB from C0000h; image 48 KiB (96 blocks), checksum 0 |
| `SIS6326-C-CARD1-SIS228-B204.TXT` | `sis6326-20261004-b` | Card 1 under SiS 2.28, boot 204; the machine locked about a minute later |
| `SIS6326-D-CARD2-VGADRV-B205.TXT` | `sis6326-20261004-b` | Card 2 under vga.drv: locked, and no Config Manager ranges (build `b` read card 1's stale key) |
| `SIS6326-E-CARD2-SIS228-B207.TXT` | `sis6326-20261004-b` | Card 2 under SiS 2.28: registers, but no snapshot for the same reason |
| `SIS6326-F-CARD2-SIS228-B207.TXT` | `sis6326-20261004-c` | Card 2 under SiS 2.28: full snapshot |
| `SIS6326-CARD2.ROM` | `sis6326-20261004-c` | Card 2, image 32 KiB (64 blocks), checksum 0 |
| `card2-sis228-desktop-b207.png` | - | Agent screenshot, boot 207, 640x480x8, taken after run F |

## What run A got wrong

Under vga.drv the extensions are locked: SR05 reads 21h, and every SR06-SR3F
reads 21h too, the lock value. Build `a` decoded those as real (MMIO select
"A0000", "SGRAM", 121 MHz clocks) and followed the bogus A0000h select, so
its `M_*` lines are VGA plane memory at A8280h+, not engine registers. It
also took the MMIO window from the last 64 KiB Config Manager range
(DC000000h) where the live BAR1 is DD000000h, and stopped at eight ranges,
which dropped the I/O range. Build `b` fixes all three and reads no MMIO
while locked.

## Measured

- PCI: BAR0 `DE000008h` (4 MiB, prefetchable), BAR1 `DD000000h` (64 KiB),
  BAR2 `0000D001h`, an I/O range of **128 bytes** at D000h per Config Manager.
  The Rev. Ax/Bx datasheet gives 16 bytes; 128 bytes matches xf86-video-sis's
  relocated-I/O window (SR at base + 44h). Config Manager also lists a second
  64 KiB memory range at DC000000h that no BAR names; not explained.
- Command `0007h`: I/O, memory, bus master on. Status 66 MHz capable plus
  capabilities list (`0230h`). Cfg 3Ch interrupt pin 02h.
- Extensions locked under vga.drv (SR05 = 21h). CR80 also reads 21h.
- BIOS: "SiS 6326 PCI True Color Graphics and Video Accelerator ... Add-on-Card
  BIOS Ver 1.06 12-18-97", "Support VESA BIOS Extension ver 2.0".

## Not established

Everything behind the lock: memory size and type, clocks, the MMIO window,
the engine registers. Those need either `/unlock` under vga.drv (BIOS state
only) or a run under SiS's own driver (engine state as well).

## SiS 2.28 install, boot 201 -> 202

SiS's Windows 98 driver 2.28 (AOpen-branded `SIS6326M.DRV/VXD`, `DD326`,
`DD326_32`, `glsis326.dll`; INF matches `SUBSYS_63261039`) was staged in
`C:\SIS228` and installed through Device Manager, Update Driver, Have Disk,
replacing Standard PCI Graphics Adapter (VGA) on `Display\0009`. The
pre-install registry is in `display-class-before-sis.reg` and
`enum-sis6326-before-sis.reg`.

The agent's warm restart reached boot 202 and reconnected with
`DesktopReady=False` at 640x480; within the following minute A8U4I5 stopped
answering both the agent and ICMP, and it was still silent 5.5 minutes later.
What the screen shows is not known. The window coincides with the SiS
display driver's first load, but this machine has also dropped off the
network on its own before (2026-09-05), so the driver is a suspect, not a
finding.

## Boots 203-207: card 1 locks, card 2 does not

Card 1 came back at boot 204 (boot 203 never reached the agent) running SiS
2.28 at 640x480x8. Run C succeeded; about a minute later, during an agent
screenshot, the machine stopped answering again.

Michael then fitted a second 6326: revision **0Bh**, subsystem `63261569`,
BIOS "1.28q" of 10/21/1999 (AGP and PCI strings), power-management
capability at 40h ahead of AGP at 50h. SiS 2.28 was forced onto it through
Have Disk (its INF names only `SUBSYS_63261039`; Windows warned the driver
"was not written specifically for the selected hardware"). Boot 207 stayed
up for over ten minutes with three probe runs and a screenshot, under the
same driver, probe and board.

| | Card 1 | Card 2 |
|---|---|---|
| Revision / subsystem | C3 / 63261039 | 0B / 63261569 |
| BIOS | 1.06, 12-18-97, 48 KiB | 1.28q, 10/21/1999, 32 KiB |
| SR0C | E4h (D4 = 0) | B4h (D4 = 1) |
| Timing enables | SR23 D5, SR33 D3: 1-cycle EDO | SR33 D0: SGRAM |
| SR28/SR29 | 7Ch/E7h = 55.9 MHz | B3h/C5h = 82.7 MHz |
| Under SiS 2.28 | locked twice (boots 202, 204) | stable, boot 207 |

Same in both under SiS 2.28: extensions unlocked, MMIO through BAR1
(SRB = 6Ch), Turbo Queue on (SR27 = C0h, SR2C = 7Eh, SR3C D[1:0] = 0), 3D
off (SR39 = 00h), pitch 1024 at 640x480x8, and the same last 2D operation
(below). Interpretation is in
`docs/decisions/2026-10-04-sis6326-first-survey.md`.
