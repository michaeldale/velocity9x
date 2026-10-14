# Gamma ramps reach the Rage XL: GDI calls display-driver ordinal 32, and the DAC is written through DAC_REGS

Date: 2026-10-14

Status: Measured on A8U4I5 (ATI 3D Rage XL PCI, 16 bpp), boots 471-477.
Thief II's brightness slider confirmed on the monitor by Michael at boot
477. Evidence: `docs/probe/a8u4i5-rage-xl-gamma-colour-key-2026-10-14/`.
ATI family only; every other family still claims no gamma.

## Why

Thief II sets its brightness through DirectDraw's gamma control. Without
it, the game was too dark to judge. Velocity9x claimed no gamma, so
`SetDeviceGammaRamp` failed (boot 471, `V9XGAMMA.INI`: every call 0,
`DDCAPS2_PRIMARYGAMMA` clear).

## The interface, which the DDK does not document

The Windows 98 DDK names the capability, `C1_GAMMA_RAMP` in GDIINFO
`dpCaps1`, and nothing else. Its samples and headers do not show how GDI
then hands the driver a ramp.

- **Claiming the cap alone** made DirectDrawCreate crash (boot 472).
  `V9XGAMMA.EXE` died before writing its first key.
- **ATI's shipped driver for this family**, `MACXW4.DRV` 4.11.2474 (from
  `C:\ATI\SUPPORT\w82474en` on A8U4I5), exports `DDIGAMMARAMP` at ordinal
  32. Its body:
  - is `RETF 10`;
  - tests a WORD at `[bp+0Ah]` and loads a far pointer from `[bp+6]`;
  - so its signature is `(LPVOID lpDevice, WORD fSet, LPVOID lpRamp)`.
  - **Set:** keeps the high byte of each of the 3 x 256 WORDs, red then
    green then blue.
  - **Get:** returns those bytes shifted up 8.
  - Acts only above 8 bpp, and returns 1.

  This is a shipped binary's export table and a few instructions, not the
  leaked 3dfx tree, which is still not read.
- **Ours:** `DdiGammaRamp` exported at ordinal 32 (`ddi.c`). With it,
  boot 473 shows DirectDraw reporting `DDCAPS2_PRIMARYGAMMA`, and
  Get, Set, read-back and restore all succeeding.

## Two faults on the way to the screen

1. **The ramp was stored, never written, while a game ran.** The first
   version skipped the hardware write while the DIB engine marked the
   device BUSY, as `SetPalette` does. A DirectDraw game holds BUSY for
   as long as it runs. At boot 475 Thief II's slider had loaded a ramp
   (entry 64 = 7700h, 128 = AF00h), but the DAC still held the identity.
   The write is now skipped only while the driver is disabled (a
   full-screen DOS box), and Enable writes the stored ramp.
2. **The VGA DAC ports do not reach the Rage XL's DAC in this mode.**
   `ATIRX /pro /dacport` (boot 476):

   | Path | Result |
   |---|---|
   | Port read (3C7h/3C9h) | FFh |
   | Port write (3C8h/3C9h), read through `DAC_REGS` | unchanged |
   | Write through `DAC_REGS` (MM 0_30), read through `DAC_REGS` | lands |

   The ramp now goes through the family's register window, the one its
   engine descriptor names and `V9xEngineSelector` maps. The new hw16
   hook `load_gamma` carries it, and the ATI implementation:
   - writes `DAC_REGS` with the primary table selected in `DAC_CNTL`
     (bit 5);
   - takes eight bits or six from `DAC_8BIT_EN`. The XL reads 8601010A,
     eight bits.

   Boot 477: with a 1.5x ramp held, the DAC read 60h at entry 40h and C0h
   at entry 80h. The identity ramp read back as the identity.

## Diagnostics added

- `C:\V9XDIAG\V9XESC.INI`:
  - `[Velocity9xEscapes]`: every escape the driver does not answer, once
    per (function, queried escape).
  - `[Velocity9xGamma]`: the first 16 ramp sets, whether the DAC was
    written, and red entries 64 and 128.
- `V9XGAMMA.EXE` (`tools\diag\gamma_probe_win32.c`): the caps, Get, a
  bright Set held three seconds, then restore. `/keep` leaves the ramp
  loaded; `/identity` loads the identity.

## Only where the window exists (2026-10-15)

The ATI family's tier-0 VT2, VT3 and VT4 have no register-window
descriptor. Claiming gamma there would accept ramps that change nothing,
so C1_GAMMA_RAMP needs the hook and a device that names its window.

GDI asks for the caps before Enable scans PCI, when the active device is
still the family's first entry, the VT2. The check therefore makes the
PCI scan itself; it is BIOS reads only.

On the 86Box Mach64 VT2 (Win98SE-Mach64VT2, boot 122), V9XGAMMA reads
`DdPrimaryGamma` 0, Get 0 and Set 0, as on the old build (boot 121).
DirectDraw creates normally.

Not yet re-run on the Rage XL with this check: A8U4I5 was off.

## Not done

- No other family has a `load_gamma` hook. Each needs its own DAC path
  measured; the VGA ports are not a safe default, as above.
- A mode change reloads the table from the BIOS; Enable then writes the
  stored ramp back. Only the Enable path was exercised, not a live mode
  change during a game.
