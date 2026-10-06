# A8U4I5: Rage XL PCI back on the ati build, 2026-10-06

- Machine: A8U4I5 (`10.0.1.172:9869`), card `PCI\VEN_1002&DEV_4752&SUBSYS_80081002&REV_27`,
  8 MB, in PCI slot DEV_09. Bound to `Display\0007` ("Velocity9x ATI Rage XL PCI"), the
  key from [2026-10-03](../a8u4i5-rage-xl-pci-2026-10-03/README.md).
- Purpose: a Mach64 stand-in for GitHub issue 2 (Rage Mobility-P in a Compaq Armada E500:
  DX8 "3D not available", 3DMark 99 black-screen hang, UT99 polygon dropouts and an OpenGL
  hang, a brief screen darkening on the caps query).
- SiS 6326 work is parked. Its last installed binaries are kept in `C:\V9XSIS` on the guest.

## What was wrong after the card swap (boot 297)

The card was swapped back while `C:\WINDOWS\SYSTEM` still held the SiS build of 5 Oct
(`V9XHAL.DLL` 325,120 bytes). Two separate faults:

1. Wrong family binaries. `V9XBOOT.INI` said `Stage=fail-hardware-present` and `V9XHW.INI`
   still said `Adapter=SiS 6326`. Fixed by installing the ati package.
2. A 4 bpp hardware profile. After installing the ati files, boot 298 came up on `vga.drv`
   with no `V9XBOOT.INI` written at all. `HKLM\Config\0001\Display\Settings` held
   `BitsPerPixel=4`, and every Velocity9x class key maps `MODES\4\640,480` to `vga.drv`, so
   our DRV was never loaded. The device node had no problem code and the DRV loaded fine
   under `V9XSTAGE.EXE` (`Stage=query-ok`). The 4 bpp value was most likely written by
   Windows when the SiS build failed on the Rage XL. Setting `BitsPerPixel=16`,
   `Resolution=800,600` fixed it.

Lesson: after a card swap that left a failed boot, check the hardware profile's
`BitsPerPixel` before suspecting the driver.

## Install

Package `build\win98se-ati`, build `526c508`, built with
`build-active-package.ps1 -Family ati`. Uploaded to `C:\V9XATI`, then one plain WININIT
rename of five files (no `NUL=` lines, per the A8U4I5 rules in
[the Rage IIC plan](../../plans/ati-rage-iic-hardware-3d.md)) and a warm restart. All five
dated 16:29 after boot 298:

| File | Bytes | SHA256 |
|------|------:|--------|
| V9XDISP.DRV | 51,022 | `F50CEA26483B0A7457BD1DBBC34253B3408DB4BBBBA5DC9B93C1A7D9BE85A362` |
| V9XMINI.VXD | 12,332 | `373699B3AB0A9DBD28D63C140F5BF5BD006766B55CC7E8F211F5C4E6A9F7BE89` |
| V9XHAL.DLL | 337,408 | `15FB52A2B8EDE2293A8733849DFA74C147FBD1092710E483CD7754E4347A418C` |
| V9XSETP.DLL | 50,688 | `3204D7AF5ECFAAE2932F77318060F31D2046148CB0FDEDE57E90306C0C8DC8F6` |
| V9XGL.DLL | 413,696 | `B3868F98C08DC4BF04D7DD2D16BBA970D50FD4FB3EE7245A10D3058929495F50` |

Hashes are the package's `SHA256.TXT`; the guest copies were matched by size and date,
not re-hashed.

`update-associated-driver.ps1` was not used: it writes `NUL=` lines and does not install
`V9XGL.DLL`.

## Boot 299 result

- `V9XBOOT.INI`: `Stage=enable-ok`, 800x600x16, `vram=8323072`, aperture `dc000000`.
- `V9XHW.INI`: `Adapter=ATI 3D Rage XL PCI`, `Direct3D=hardware-mach64`,
  `Acceleration=directdraw-fill`.
- `V9XMODES.INI`: build `526c508`, family `ati`, 19 rows published.
- Desktop screenshot at 16 bpp.

Open: `SYSTEM.INI` `[Velocity9x]` still carries `Direct3D=0` from earlier work; check what
it gates before the issue 2 runs.
