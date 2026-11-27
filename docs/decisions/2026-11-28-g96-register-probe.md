# G96 register probe on M-Z2T02D: chipset 96h, 256 MiB GDDR3, no BAR VM, PGRAPH off

Date: 2026-11-28

Phase 2 of [docs/plans/nvidia-g96-geforce9500gt.md](../plans/nvidia-g96-geforce9500gt.md).
GitHub issue 30. The owner of issue 29's HD 2500 (ASRock B75M R2.0,
i3-3250) installed the remote agent; the machine is M-Z2T02D, computer
W9QI6CF2, agent 0.19.0.

## The machine as found

Agent `inventory`, read only:

- The 9500 GT, `10DE:0640` rev A1, subsystem `0000:0000`, on bus 1, bound
  to Microsoft's "Standard PCI Graphics Adapter (VGA)". It is the boot
  adapter: the desktop is 640x480 in 16 colours.
- The HD 2500, `8086:0152`, still has the 0.60.0 `intel-gma` package
  (build `b3996f07`). `C:\V9XDIAG\V9XBOOT.INI` from it says `enable-ok`
  at 1440x900x32, `Direct3D=hardware-gen7`. The agent gives no file
  times, so which boot wrote it is unknown; probably one before the
  9500 GT went in.

## The probe

`N5MM.VXD` + `V9XN5MM.EXE` now accept `10DE:0640` and chipset 96h beside
the GT218's id and A8h. The register list is unchanged: every unit in it
exists on a G96 (nouveau `nv96_chipset`) except the GT215 copy engine,
whose PMC_ENABLE bits mean other engines on G84-class chips
(`subdev/mc/g84.c`), so the EXE skips 104000h unless the chipset is A8h.
`/fb`, `/fbwrite`, `/crtc`, `/startmove` and `/vga` were not run and their
offsets are not checked for a G96.

Run once, boot 8, from `C:\V9XDIAG\G96`, under the Standard VGA driver,
probe build `7b43193c-dirty`: VxD status `2D`, `Result=COMPLETE`, exit 0
in 98 ms. Evidence:
[asrock-b75m-gt9500-g96-0640-n5mm-2026-11-28-7b43193c-dirty.ini](../probe/references/asrock-b75m-gt9500-g96-0640-n5mm-2026-11-28-7b43193c-dirty.ini).

| Register | Value | Reading |
|---|---|---|
| Config 10h | `F6000000` | BAR0, registers |
| Config 14h | `E000000C` | BAR1, 64-bit prefetchable, `E0000000` |
| Config 1Ch | `F4000004` | BAR3, 64-bit, **not** prefetchable (the GT218's is) |
| Config 24h | `0000E001` | I/O `E000` |
| PMC_BOOT_0 | `096000C1` | chipset 96h, stepping C1 |
| PMC_ENABLE 200h | `C0110111` | PFIFO on; PGRAPH (`00201000h`) off |
| PBUS 1540h | `F3010001` | one memory partition bit (23:16 = 01h) |
| PRAMIN 1700h | `00000FF0` | window at `0FF00000`, the top MiB of 256 |
| 1704h/1708h/170Ch | `0` | no BAR VM, no BAR1 or BAR3 object, as on the GT218 |
| PFB 10020Ch | `10000000` | **256 MiB** |
| PFB 100714h | `00033122` | type 2, GDDR3 |
| PSTRAPS 101000h | `80408086` | |
| PDISP 610B48h | `038405A0` | head 0 mirror holds 1440x900 (an earlier VBE mode, not the VGA mode on screen) |

The PRAMIN window's words look uninitialised (no driver has written
instance memory), and the BAR VM instance is outside the window.

## What this settles and what it does not

- The chip is a G96 (96h); the 3D class for Phase 5 is NV84_3D (`8297h`),
  as the plan assumed.
- 256 MiB confirms the survey. With no BAR VM, BAR1 decoding video memory
  linearly is the GT218's finding; it is not measured here (`/fbwrite`
  not run).
- One partition bit is unexpected for a 128-bit card. The decode follows
  nouveau's field for the GT218; whether it holds on a G96 is not checked.
- PGRAPH is held in reset under the Standard VGA driver; Phase 4 brings it
  up itself on the GT218 too.

## Not done: `vbe` on the 9500 GT

The plan's Phase 2 said "under the `vbe` package". It was not installed:
`vbe` and `intel-gma` ship the same file names (`V9XDISP.DRV`,
`V9XMINI.VXD`, `V9XHAL.DLL`, `V9XSETP.DLL` and the rest), so a Have Disk
install for the 9500 GT replaces the HD 2500's driver files in
`C:\WINDOWS\SYSTEM`. The probe does not need it.
