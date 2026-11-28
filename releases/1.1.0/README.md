# Velocity9x 1.1.0

Built from commit `381bbe57`: the output of
`scripts\build-all-packages.ps1` and `scripts\build-vga-survey.ps1`,
zipped for download.

Windows 98SE. Each zip carries its own instructions and its own recovery
notes - start with the `.TXT` files at the top level of the archive, and
read them before you install anything. This is a from-scratch display
driver: a bad install leaves the machine at a black screen until you
recover it.

## Which zip

| Download | Card | Hardware ID | Tested |
| --- | --- | --- | --- |
| [`velocity9x-1.1.0-3dfx.zip`](velocity9x-1.1.0-3dfx.zip) | 3dfx VSA-100 | `PCI\VEN_121A&DEV_0009` | Supported: Voodoo4 4500 in A8U4I5 (2026-11-27, 0.61.0 tree), every release probe passing but the V9XD32 32-bit texture test; 3DMark 2000 runs. Record: docs/decisions/2026-11-21-1.0-baseline-checks.md |
| [`velocity9x-1.1.0-ati.zip`](velocity9x-1.1.0-ati.zip) | ATI Mach64 / Rage | `PCI\VEN_1002&DEV_4742<br>PCI\VEN_1002&DEV_4744<br>PCI\VEN_1002&DEV_4747<br>PCI\VEN_1002&DEV_4749<br>PCI\VEN_1002&DEV_474C<br>PCI\VEN_1002&DEV_474D<br>PCI\VEN_1002&DEV_474E<br>PCI\VEN_1002&DEV_474F<br>PCI\VEN_1002&DEV_4750<br>PCI\VEN_1002&DEV_4751<br>PCI\VEN_1002&DEV_4752<br>PCI\VEN_1002&DEV_4753<br>PCI\VEN_1002&DEV_4754<br>PCI\VEN_1002&DEV_4755<br>PCI\VEN_1002&DEV_4756<br>PCI\VEN_1002&DEV_4757<br>PCI\VEN_1002&DEV_4759<br>PCI\VEN_1002&DEV_475A<br>PCI\VEN_1002&DEV_4C42<br>PCI\VEN_1002&DEV_4C44<br>PCI\VEN_1002&DEV_4C47<br>PCI\VEN_1002&DEV_4C49<br>PCI\VEN_1002&DEV_4C4D<br>PCI\VEN_1002&DEV_4C4E<br>PCI\VEN_1002&DEV_4C50<br>PCI\VEN_1002&DEV_4C51<br>PCI\VEN_1002&DEV_4C52<br>PCI\VEN_1002&DEV_4C53<br>PCI\VEN_1002&DEV_4C54<br>PCI\VEN_1002&DEV_5046<br>PCI\VEN_1002&DEV_5654<br>PCI\VEN_1002&DEV_5655<br>PCI\VEN_1002&DEV_5656<br>PCI\VEN_1002&DEV_5960<br>PCI\VEN_1002&DEV_5961<br>PCI\VEN_1002&DEV_5962<br>PCI\VEN_1002&DEV_5964<br>PCI\VEN_1002&DEV_6610<br>PCI\VEN_1002&DEV_6611<br>PCI\VEN_1002&DEV_675D<br>PCI\VEN_1002&DEV_6778<br>PCI\VEN_1002&DEV_6780<br>PCI\VEN_1002&DEV_6808<br>PCI\VEN_1002&DEV_682C<br>PCI\VEN_1002&DEV_68B8<br>PCI\VEN_1002&DEV_68D9<br>PCI\VEN_1002&DEV_68F9<br>PCI\VEN_1002&DEV_7146<br>PCI\VEN_1002&DEV_791F<br>PCI\VEN_1002&DEV_94C3<br>PCI\VEN_1002&DEV_9588<br>PCI\VEN_1002&DEV_9854<br>PCI\VEN_1002&DEV_9874<br>PCI\VEN_1002&DEV_9901` | Supported: Oland XT 6610 (V9XD32 passes, 2026-11-25), R3E, R7E, HD 7660D, X1200 and X1300, release probes passing (2026-11-21 to 25, 0.61.0 tree); Rage 128 Pro (2026-11-27), Rage XL and Rage IIC (2026-11-26) in A8U4I5 and HD 5450 (2026-11-26), release probes run on the 0.61.0 tree; Radeon 9250 (2026-11-16), HD 7470 (2026-11-23) and Rage Mobility-M on their last run, not run for 1.0; HD 2600 XT and HD 2400 Pro on their last run, the cards gone; HD 5770 from a reporter (GitHub issue 21 open), Oland 6611 on our probes, Mach64 VT2 on 86Box only. Recognised, not run: HD 5570, HD 7570, Radeon 9200 5961 and 5962, FirePro W4100, W7000 and W9000. Record: docs/decisions/2026-11-21-1.0-baseline-checks.md, docs/decisions/2026-11-23-1.0-b1-b3-b12-checks.md, docs/decisions/2026-11-23-1.0-b1-oland-flipsafety.md, docs/decisions/2026-11-25-t19-specular-saturate-before-fog.md |
| [`velocity9x-1.1.0-intel-gma.zip`](velocity9x-1.1.0-intel-gma.zip) | Intel GMA and HD Graphics | `PCI\VEN_8086&DEV_0042<br>PCI\VEN_8086&DEV_0046<br>PCI\VEN_8086&DEV_0112<br>PCI\VEN_8086&DEV_0122<br>PCI\VEN_8086&DEV_0152<br>PCI\VEN_8086&DEV_0162<br>PCI\VEN_8086&DEV_0166<br>PCI\VEN_8086&DEV_0412<br>PCI\VEN_8086&DEV_041E<br>PCI\VEN_8086&DEV_2592<br>PCI\VEN_8086&DEV_27AE<br>PCI\VEN_8086&DEV_29C2<br>PCI\VEN_8086&DEV_2A02<br>PCI\VEN_8086&DEV_2A42<br>PCI\VEN_8086&DEV_2E12<br>PCI\VEN_8086&DEV_2E22<br>PCI\VEN_8086&DEV_2E32<br>PCI\VEN_8086&DEV_A011` | Supported: GMA 950, HD 4600, HD 3000 0112, GMA X3100 and GMA 4500MHD, release probes passing (2026-11-21 to 23, 0.61.0 tree); GMA 900 too (2026-11-26, a second 915GM); GMA 3150 (2026-10-20) and HD 4000 mobile (2026-11-05) on their last run, not run for 1.0; Ironlake 0042 and HD 4000 0162 on their last run, the cards gone; G45, Q45, HD 2500 and Ironlake mobile 0046 from field reports. Recognised, not run: GMA 3100, HD 3000 0122, G41 2E32. Record: docs/decisions/2026-11-21-1.0-baseline-checks.md, docs/decisions/2026-11-23-1.0-b1-b3-b12-checks.md, docs/decisions/2026-11-26-gma900-m8z30st-release-probes.md |
| [`velocity9x-1.1.0-matrox.zip`](velocity9x-1.1.0-matrox.zip) | Matrox Millennium | `PCI\VEN_102B&DEV_0519<br>PCI\VEN_102B&DEV_051A<br>PCI\VEN_102B&DEV_051B<br>PCI\VEN_102B&DEV_051E<br>PCI\VEN_102B&DEV_051F` | Supported: Mystique (every release probe passing but V9XD32 T22 and the Glide half blend) and Millennium (the 2D-side probes pass; no 3D engine), in A8U4I5 (2026-11-27, 0.61.0 tree); Millennium II on its last run (2026-10-22), not run for 1.0. Recognised, not run: Mystique AGP and Millennium II AGP. Record: docs/decisions/2026-11-21-1.0-baseline-checks.md |
| [`velocity9x-1.1.0-nvidia.zip`](velocity9x-1.1.0-nvidia.zip) | NVIDIA RIVA 128, GeForce 210 and GeForce 9500 GT | `PCI\VEN_10DE&DEV_0640<br>PCI\VEN_10DE&DEV_0A65<br>PCI\VEN_12D2&DEV_0018` | Supported: GeForce 210 (GT218), release probes passing (2026-11-25, 0.61.0 tree). Experimental: RIVA 128ZX, with its known V9XD32 and Glide BlendHalf failures (2026-11-21); GeForce 9500 GT (G96), DirectDraw, GDI, flips, Direct3D and OpenGL (V9XGLP, Quake 3 demo), V9XD32 failing only its 16-bit depth-precision check (the Z is 24-bit) (2026-11-28). Record: docs/decisions/2026-11-21-1.0-baseline-checks.md, docs/decisions/2026-11-25-gt218-cube-levels-dxt-pool-counters.md, docs/decisions/2026-11-28-g96-2d-engine-first-run.md, docs/decisions/2026-11-28-g96-flips-from-the-vbios.md, docs/decisions/2026-11-28-g96-direct3d.md, docs/decisions/2026-11-28-g96-dxt-decode.md |
| [`velocity9x-1.1.0-s3.zip`](velocity9x-1.1.0-s3.zip) | S3 | `PCI\VEN_5333&DEV_8810<br>PCI\VEN_5333&DEV_8811<br>PCI\VEN_5333&DEV_8812<br>PCI\VEN_5333&DEV_8813<br>PCI\VEN_5333&DEV_8814<br>PCI\VEN_5333&DEV_8901<br>PCI\VEN_5333&DEV_8904<br>PCI\VEN_5333&DEV_8A01<br>PCI\VEN_5333&DEV_8A13` | Supported: ViRGE/DX (2026-11-27) and Trio3D/2X (2026-11-26) in A8U4I5 on the 0.61.0 tree, the ViRGE/DX failing only what 86Box fails and the Trio3D/2X only V9XD32 T21 and the Glide half blend and fog table; Trio64 on 86Box (release probes pass, 2026-11-24) and on a physical card last run 2026-11-04; Trio3D 8904 not run for 1.0. Record: docs/decisions/2026-11-21-1.0-baseline-checks.md |
| [`velocity9x-1.1.0-sis.zip`](velocity9x-1.1.0-sis.zip) | SiS 6326 | `PCI\VEN_1039&DEV_6326` | Supported: SiS 6326 in A8U4I5 (2026-11-27, 0.61.0 tree), every release probe passing but one V9XD32 texture check (X8R8G8B8); 3DMark 2000 runs at 640x480. Record: docs/decisions/2026-11-21-1.0-baseline-checks.md |
| [`velocity9x-1.1.0-vbe.zip`](velocity9x-1.1.0-vbe.zip) | VBE tier-0 (generic VESA) | `PCI\VEN_1234&DEV_1111` | Supported, on other evidence: release probes pass on an HD 4600 (2026-11-21, 0.61.0 tree); no other card run. Record: docs/decisions/2026-11-21-1.0-baseline-checks.md |

If your card is not listed, none of these will drive it. Run the survey
below and send the report in; that is what a new family is built from.

- **3dfx VSA-100** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **ATI Mach64 / Rage** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **Intel GMA and HD Graphics** modes: 640x480 and native 1024x576 at 8/16 bpp and 60 Hz
- **Matrox Millennium** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **NVIDIA RIVA 128, GeForce 210 and GeForce 9500 GT** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **S3** modes: 640x480, 800x600 and 1024x768 at 8/16/32 bpp, 1280x1024 at 8/16 bpp, 640x400 at 8 bpp, 60 Hz
- **SiS 6326** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz
- **VBE tier-0 (generic VESA)** modes: 640x480, 800x600, 1024x768 at 8/16 bpp and 60 Hz

## Hardware survey

[`velocity9x-survey-1.1.0.zip`](velocity9x-survey-1.1.0.zip) - a real-mode DOS program that reads
the PCI identifiers, video BIOS, advertised modes, monitor EDID and VGA
registers of whatever card is in the machine, and writes one report file.
It sets no video mode, installs nothing, and writes to no register.
Run it on an unsupported card and send the report; `README.TXT` inside
the zip has the instructions.

## Checksums

`SHA256SUMS.txt` covers the zips in this folder. Each package also ships
its own `SHA256.TXT` covering the files inside it. `SIGNED.TXT`
lists the same zip hashes per family under an Ed25519 signature; it is
what the built-in updater (`V9XUPD.EXE`) checks before installing.

