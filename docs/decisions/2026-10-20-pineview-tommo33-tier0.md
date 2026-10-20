# Pineview on TOMMO33: tier-0 enable-ok, VBE reports 0x7F0000, and this BIOS describes 1024x600

Date: 2026-10-20
Status: **measured once each, no DOS.** Phase 0 of
`docs\plans\intel-pineview-gma3150.md`. A read-only config survey came
first; then the `vbe` family was installed and its boot report read.

Raw capture: `docs\probe\tommo33-pineview-tier0-2026-10-20\V9XHSW.INI`.

| | |
|---|---|
| Machine | Tom Rossell's TOMMO33, remote help `M-BWCR7F` (agent 0.12.0) |
| Board | Lenovo, BIOS Rev 1.0 09/23/10 |
| CPU | Atom N455, 1.66 GHz |
| IGD | `8086:A011` rev 00, SUBSYS `17AA:3908`; function 1 `8086:A012` |
| Host bridge | `8086:A010` rev 00 |
| Panel | `CMO1018` (EDID monitor id) |
| OS | Windows 98 4.10.2222 A (Second Edition), DirectX 4.09.00.0904 |
| Display driver | VBEMP (Anapa, 2-14-2014), 1024x600x32 |

## Method

`V9XHSW.EXE`, build `0800cb48-dirty`, from `C:\V9XREMOTE\JOBS\PNVSURV`. It
reads 256 bytes of config space from 00:00.0 and 00:02.0 through CF8h/CFCh,
reading each dword twice. `UnstableReads=0000`. Its `[Decode]` section is
the Haswell layout and does not apply to this part; the raw words are what
count. No DOS program ran, because DOS programs run through the agent leave
Windows unable to reboot itself.

## Words

| Register | Value | Meaning (N400/N500 Vol 2, 322848) |
|---|---|---|
| IGD BAR0 MMADR | `F0200000` | 512 KB MMIO |
| IGD BAR1 IOBAR | `000018E9` | I/O at `18E8` |
| IGD BAR2 GMADR | `D0000008` | prefetchable; MSAC (62h) `02`, the same as the Acer |
| IGD BAR3 GTTADR | `F0000000` | |
| GGC (host 52h) / MGGC (IGD 52h) | `0130` / `0130` | GMS 3 = 8 MiB, GGMS 1 = 1 MiB GTT |
| BSM (IGD 5Ch) | `7F800000` | |
| GBSM (host A4h) | `7F800000` | agrees with BSM |
| BGSM (host A8h) | `7F700000` | GBSM less the 1 MiB GTT |
| host ACh | `7F600000` | probably TSEG; not decoded |
| TOM (host A0h) | `0020` | bits 35:26, so 2 GiB installed |
| TOLUD (host B0h) | `0080` | raw; not decoded |
| DEVEN (host 54h) | `00000019` | |
| GCFGC (IGD F0h) | `0079` | display clock field 7 = 167 MHz (Linux `GC_DISPLAY_CLOCK_167_MHZ_PNV`), low-frequency bit clear |
| LBB (IGD F4h) | `000000FF` | legacy backlight at full |
| ASLS (IGD FCh) | `7F5E0E7C` | OpRegion present |

Windows reports 1,072,181,248 bytes of physical memory against TOM's 2 GiB,
presumably capped for Windows 98. The cap is not recorded.

## Against the Acer NAV50 (real DOS, August)

The values that matter to the plan are identical:

- GGC `0130`
- BSM `7F800000`
- MSAC `02`
- the BAR layout

Only the BAR addresses and the OpRegion address differ. Item 4 of the plan,
GGMS 1, is confirmed on a second machine.

## Tier-0: the driver's own VBE report, instead of a DOS survey

The driver makes the same `4F00h`/`4F01h`/`4F15h` calls as V9XSURV, in its
boot path, and writes the answers to `C:\V9XDIAG`. So the VBE questions can
be answered from Windows by installing it.

`build\win98se-vbe` was built from a clean worktree at `0800cb48`, the HEAD
commit, because the working tree held another session's uncommitted
Haswell edits. It was pushed to `C:\V9XPKG`. It was installed through Device
Manager, Update Driver, Have Disk, on the `(VGA)` entry, which is function 0;
its Resources tab shows the VGA ranges. Windows warned that the existing
driver (VBEMP) was a closer match, and the warning was accepted. Setup left a
`WININIT.INI` holding two plain renames: the `glide2x.dll` and `glide3x.dll`
already in `C:\WINDOWS\SYSTEM` were renamed to `.001`. There was no `NUL=`
line. One agent reboot followed: `BootCounter` 10 to 11, reconnected, and
the desktop was ready.

Captured in this folder: `V9XBOOT.INI`, `V9XMODES.INI`, `V9XHW.INI`,
`V9XDDH.INI`, `V9XESC.INI`, `V9XSYNC.INI` and `desktop-after-install.png`.

- **`Stage=enable-ok`** at 640x480x8. The desktop draws, and an agent
  screenshot reads it back.
- **VBE 3.0, `mem=127`: 8,323,072 bytes, `0x7F0000`.** The Acer reports the
  same. The ring offset blocker (plan item 1) is therefore real on both
  known Pineview machines. A Gen3 claim today would end in
  `ring-layout-mismatch`.
- **36 modes listed, 36 queried, 9 described.** The Acer described 6. This
  BIOS describes `0160`, `0161` and `0162`: 1024x600 at 8, 16 and 32 bpp,
  pitches 1024, 2048 and 4096, all with the linear framebuffer at
  `D0000000`, which is GMADR. 800x600x8 has an 832-byte pitch, as on the
  Acer.
- **EDID** v1.3; preferred mode 1024x600. The driver recommends
  1024x600 (`reason=edid-preferred`). It hides 1024x768 at 8 and 16 bpp as
  EDID-contradicted, and 640x400 as scan-contradicted.
- **MTRRs present:** 8 variable ranges, write-back over RAM, and none on
  the framebuffer. The driver reports `r=0`.
- `V9XHW.INI`: the unclaimed generic adapter, `Direct3DMode=software`,
  `Acceleration=none`. That is what tier-0 is.

The panel's native mode is therefore reachable through VBE on this BIOS.
The plan's Phase 4 applies to BIOSes like the Acer's, not to this one.

## What this does not answer

- Any MMIO. That is Phase 2.
- Whether 1024x600x32 works under this driver. The desktop is still at its
  first-boot mode.
