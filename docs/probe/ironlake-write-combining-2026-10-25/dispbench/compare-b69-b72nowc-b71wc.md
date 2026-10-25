# GDI and DirectDraw: w9qi2f4f-ilk-v9x / w9qi2f4f-ilk-v9x / w9qi2f4f-ilk-v9x

Caveats, always printed: every verdict is against that run's own machine
ceilings (FbWrite/FbRead/MemFill/MemCopy/DibToDibBlt); cycles per unit
compare across machines, MB/s and microseconds do not. RASTERCAPS says
nothing about GDI acceleration on Win9x. There is no single score.

| | w9qi2f4f-ilk-v9x | w9qi2f4f-ilk-v9x | w9qi2f4f-ilk-v9x |
|---|---|---|---|
| Driver | Velocity9x Intel HD Graphics (Ironlake) (v9xdisp.drv 0x488F0B6E) | Velocity9x Intel HD Graphics (Ironlake) (v9xdisp.drv 0x34F30EF6) | Velocity9x Intel HD Graphics (Ironlake) (v9xdisp.drv 0x34F30EF6) |
| Run | 20261025-213315, build 0af3316 | 20261025-223425, build 0af3316 | 20261025-220043, build 0af3316 |
| Mode, environment | 1024x768x32, metal | 1024x768x32, metal | 1024x768x32, metal |
| FbWrite / FbRead MB/s | 32.2 / 28.4 | 32.2 / 28.4 | 3,992.1 / 107.6 |

| Operation | w9qi2f4f-ilk-v9x cyc/unit, setup us, verdict | w9qi2f4f-ilk-v9x cyc/unit, setup us, verdict | w9qi2f4f-ilk-v9x cyc/unit, setup us, verdict |
|---|---|---|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | 8.48/px, 10.9, **CPU** | 8.52/px, 11.1, **CPU** | 8.49/px, 10.8, **CPU** |
| Dd.Blt.SysToSys.KeySrc.256x256 | 3.79/px, 10.9, **CPU** | 3.80/px, 11.1, **CPU** | 3.79/px, 10.8, **CPU** |
| Dd.Blt.SysToSys.KeySrc.512x512 | 2.23/px, 10.9, **CPU** | 2.23/px, 11.1, **CPU-fast** | 2.23/px, 10.8, **CPU** |
| Dd.Blt.SysToSys.KeySrc.64x64 | 4.61/px, 10.9, **CPU** | 4.90/px, 11.1, **CPU** | 4.52/px, 10.8, **CPU** |
| Dd.Blt.SysToSys.Plain.16x16 | 6.03/px, 1.9, **CPU** | 6.08/px, 2.0, **CPU** | 6.05/px, 1.8, **CPU** |
| Dd.Blt.SysToSys.Plain.256x256 | 2.19/px, 1.9, **CPU** | 2.20/px, 2.0, **CPU-fast** | 2.19/px, 1.8, **CPU** |
| Dd.Blt.SysToSys.Plain.512x512 | 1.94/px, 1.9, **CPU** | 1.94/px, 2.0, **CPU-fast** | 1.95/px, 1.8, **CPU** |
| Dd.Blt.SysToSys.Plain.64x64 | 2.36/px, 1.9, **CPU** | 2.56/px, 2.0, **CPU** | 2.30/px, 1.8, **CPU** |
| Dd.Blt.SysToSys.Stretch.16x16 | 6.44/px, 1.1, **CPU** | 6.43/px, 1.1, **CPU** | 6.43/px, 1.1, **CPU** |
| Dd.Blt.SysToSys.Stretch.256x256 | 1.46/px, 1.1, **CPU-fast** | 1.47/px, 1.1, **CPU-fast** | 1.46/px, 1.1, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.512x512 | 1.34/px, 1.1, **CPU-fast** | 1.35/px, 1.1, **CPU-fast** | 1.34/px, 1.1, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.64x64 | 1.74/px, 1.1, **CPU** | 1.82/px, 1.1, **CPU-fast** | 1.73/px, 1.1, **CPU-fast** |
| Dd.Blt.SysToVid.KeySrc.16x16 | 355.42/px, 1,111.3, **HW?** | 355.13/px, 1,111.2, **HW?** | 27.52/px, 23.6, **CPU** |
| Dd.Blt.SysToVid.KeySrc.256x256 | 333.04/px, 1,111.3, **HW?** | 333.22/px, 1,111.2, **HW?** | 12.53/px, 23.6, **CPU** |
| Dd.Blt.SysToVid.KeySrc.512x512 | 164.99/px, 1,111.3, **HW?** | 165.14/px, 1,111.2, **HW?** | 9.10/px, 23.6, **CPU** |
| Dd.Blt.SysToVid.KeySrc.64x64 | 334.91/px, 1,111.3, **HW?** | 334.74/px, 1,111.2, **HW?** | 13.38/px, 23.6, **CPU** |
| Dd.Blt.SysToVid.Plain.16x16 | 205.34/px, 2.6, **HW?** | 205.01/px, 2.9, **HW?** | 20.35/px, 2.6, **CPU** |
| Dd.Blt.SysToVid.Plain.256x256 | 190.49/px, 2.6, **HW?** | 190.28/px, 2.9, **HW?** | 4.42/px, 2.6, **CPU** |
| Dd.Blt.SysToVid.Plain.512x512 | 190.13/px, 2.6, **HW?** | 189.88/px, 2.9, **HW?** | 4.15/px, 2.6, **CPU** |
| Dd.Blt.SysToVid.Plain.64x64 | 189.87/px, 2.6, **HW?** | 189.95/px, 2.9, **HW?** | 4.95/px, 2.6, **CPU** |
| Dd.Blt.SysToVid.Stretch.16x16 | 210.31/px, 5.0, **HW?** | 210.50/px, 4.0, **HW?** | 18.44/px, 2.8, **CPU** |
| Dd.Blt.SysToVid.Stretch.256x256 | 189.69/px, 5.0, **HW?** | 189.48/px, 4.0, **HW?** | 3.77/px, 2.8, **CPU** |
| Dd.Blt.SysToVid.Stretch.512x512 | 189.19/px, 5.0, **HW?** | 189.16/px, 4.0, **HW?** | 3.47/px, 2.8, **CPU** |
| Dd.Blt.SysToVid.Stretch.64x64 | 191.51/px, 5.0, **HW?** | 191.66/px, 4.0, **HW?** | 4.44/px, 2.8, **CPU** |
| Dd.Blt.VidToSys.KeySrc.16x16 | 445.96/px, 5.2, **CPU** | 445.53/px, 7.6, **CPU** | 453.29/px, 2.4, **CPU** |
| Dd.Blt.VidToSys.KeySrc.256x256 | 438.67/px, 5.2, **CPU** | 440.10/px, 7.6, **CPU** | 438.49/px, 2.4, **CPU** |
| Dd.Blt.VidToSys.KeySrc.512x512 | 438.01/px, 5.2, **CPU** | 439.05/px, 7.6, **CPU** | 438.38/px, 2.4, **CPU** |
| Dd.Blt.VidToSys.KeySrc.64x64 | 439.69/px, 5.2, **CPU** | 440.94/px, 7.6, **CPU** | 440.73/px, 2.4, **CPU** |
| Dd.Blt.VidToSys.Plain.16x16 | 228.96/px, 2.5, **HW?** | 228.70/px, 7.8, **HW?** | 228.68/px, 6.9, **CPU** |
| Dd.Blt.VidToSys.Plain.256x256 | 218.99/px, 2.5, **HW?** | 219.50/px, 7.8, **HW?** | 219.16/px, 6.9, **CPU** |
| Dd.Blt.VidToSys.Plain.512x512 | 218.69/px, 2.5, **HW?** | 218.37/px, 7.8, **HW?** | 218.18/px, 6.9, **CPU** |
| Dd.Blt.VidToSys.Plain.64x64 | 219.40/px, 2.5, **HW?** | 219.58/px, 7.8, **HW?** | 219.35/px, 6.9, **CPU** |
| Dd.Blt.VidToSys.Stretch.16x16 | 123.06/px, 2.6, **HW?** | 122.96/px, 2.5, **HW?** | 122.95/px, 2.4, **CPU** |
| Dd.Blt.VidToSys.Stretch.256x256 | 109.62/px, 2.6, **HW?** | 109.69/px, 2.5, **HW?** | 109.55/px, 2.4, **CPU** |
| Dd.Blt.VidToSys.Stretch.512x512 | 109.35/px, 2.6, **HW?** | 109.45/px, 2.5, **HW?** | 109.32/px, 2.4, **CPU** |
| Dd.Blt.VidToSys.Stretch.64x64 | 110.33/px, 2.6, **HW?** | 110.57/px, 2.5, **HW?** | 110.32/px, 2.4, **CPU** |
| Dd.Blt.VidToVid.KeySrc.16x16 | 757.13/px, 3.1, **CPU** | 757.26/px, -7.0, **CPU** | 782.45/px, -3.2, **CPU** |
| Dd.Blt.VidToVid.KeySrc.256x256 | 752.49/px, 3.1, **CPU** | 751.52/px, -7.0, **CPU** | 777.73/px, -3.2, **CPU** |
| Dd.Blt.VidToVid.KeySrc.512x512 | 752.26/px, 3.1, **CPU** | 752.87/px, -7.0, **CPU** | 778.38/px, -3.2, **CPU** |
| Dd.Blt.VidToVid.KeySrc.64x64 | 755.08/px, 3.1, **CPU** | 754.76/px, -7.0, **CPU** | 779.48/px, -3.2, **CPU** |
| Dd.Blt.VidToVid.Plain.16x16 | 55.05/px, 3.4, **HW** | 53.55/px, 3.4, **HW** | 29.33/px, 2.5, **CPU** |
| Dd.Blt.VidToVid.Plain.256x256 | 4.40/px, 3.4, **HW** | 4.40/px, 3.4, **HW** | 4.40/px, 2.5, **CPU** |
| Dd.Blt.VidToVid.Plain.512x512 | 4.27/px, 3.4, **HW** | 4.27/px, 3.4, **HW** | 4.27/px, 2.5, **CPU** |
| Dd.Blt.VidToVid.Plain.64x64 | 5.97/px, 3.4, **HW** | 5.97/px, 3.4, **HW** | 5.96/px, 2.5, **CPU** |
| Dd.Blt.VidToVid.Stretch.16x16 | 319.09/px, 6.9, **HW** | 319.36/px, 4.6, **HW** | 181.09/px, 4.5, **CPU** |
| Dd.Blt.VidToVid.Stretch.256x256 | 301.75/px, 6.9, **HW** | 301.60/px, 4.6, **HW** | 164.23/px, 4.5, **CPU** |
| Dd.Blt.VidToVid.Stretch.512x512 | 300.94/px, 6.9, **HW** | 301.11/px, 4.6, **HW** | 163.70/px, 4.5, **CPU** |
| Dd.Blt.VidToVid.Stretch.64x64 | 303.57/px, 6.9, **HW** | 303.09/px, 4.6, **HW** | 164.94/px, 4.5, **CPU** |
| Dd.BltFast.SysToSys.Plain.16x16 | 5.24/px, 1.8, **CPU** | 5.24/px, 1.9, **CPU** | 5.30/px, 1.7, **CPU** |
| Dd.BltFast.SysToSys.Plain.256x256 | 2.19/px, 1.8, **CPU** | 2.19/px, 1.9, **CPU-fast** | 2.18/px, 1.7, **CPU** |
| Dd.BltFast.SysToSys.Plain.512x512 | 1.94/px, 1.8, **CPU** | 1.94/px, 1.9, **CPU-fast** | 1.95/px, 1.7, **CPU** |
| Dd.BltFast.SysToSys.Plain.64x64 | 2.30/px, 1.8, **CPU** | 2.49/px, 1.9, **CPU** | 2.21/px, 1.7, **CPU** |
| Dd.BltFast.SysToVid.Plain.16x16 | 204.03/px, 3.0, **HW?** | 204.24/px, 2.4, **HW?** | 19.73/px, 2.5, **CPU** |
| Dd.BltFast.SysToVid.Plain.256x256 | 190.59/px, 3.0, **HW?** | 190.46/px, 2.4, **HW?** | 4.41/px, 2.5, **CPU** |
| Dd.BltFast.SysToVid.Plain.512x512 | 190.15/px, 3.0, **HW?** | 190.12/px, 2.4, **HW?** | 4.15/px, 2.5, **CPU** |
| Dd.BltFast.SysToVid.Plain.64x64 | 189.85/px, 3.0, **HW?** | 189.86/px, 2.4, **HW?** | 4.90/px, 2.5, **CPU** |
| Dd.BltFast.VidToSys.Plain.16x16 | 5.12/px, 1.7, **HW?** | 5.12/px, 1.8, **HW?** | 5.14/px, 1.6, **HW?** |
| Dd.BltFast.VidToSys.Plain.256x256 | 2.17/px, 1.7, **HW?** | 2.16/px, 1.8, **HW?** | 2.16/px, 1.6, **HW?** |
| Dd.BltFast.VidToSys.Plain.512x512 | 1.91/px, 1.7, **HW?** | 1.92/px, 1.8, **HW?** | 1.92/px, 1.6, **HW?** |
| Dd.BltFast.VidToSys.Plain.64x64 | 2.11/px, 1.7, **HW?** | 2.32/px, 1.8, **HW?** | 2.09/px, 1.6, **HW?** |
| Dd.BltFast.VidToVid.Plain.16x16 | 54.66/px, 3.4, **HW** | 52.99/px, 3.3, **HW** | 28.65/px, 2.5, **CPU** |
| Dd.BltFast.VidToVid.Plain.256x256 | 4.40/px, 3.4, **HW** | 4.40/px, 3.3, **HW** | 4.40/px, 2.5, **CPU** |
| Dd.BltFast.VidToVid.Plain.512x512 | 4.27/px, 3.4, **HW** | 4.27/px, 3.3, **HW** | 4.27/px, 2.5, **CPU** |
| Dd.BltFast.VidToVid.Plain.64x64 | 5.97/px, 3.4, **HW** | 5.97/px, 3.3, **HW** | 5.96/px, 2.5, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | 354.54/px, 1,109.7, **HW?** | 354.15/px, 1,108.9, **HW?** | 25.68/px, 22.0, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | 332.74/px, 1,109.7, **HW** | 332.73/px, 1,108.9, **HW** | 12.47/px, 22.0, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | 164.95/px, 1,109.7, **HW** | 165.05/px, 1,108.9, **HW** | 9.23/px, 22.0, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | 335.04/px, 1,109.7, **HW** | 334.90/px, 1,108.9, **HW** | 12.71/px, 22.0, **CPU** |
| Dd.ColorFill.Sys.16x16 | 4.68/px, 0.7, **CPU** | 4.84/px, 0.7, **CPU** | 4.84/px, 0.7, **CPU** |
| Dd.ColorFill.Sys.256x256 | 1.25/px, 0.7, **CPU** | 1.25/px, 0.7, **CPU** | 1.25/px, 0.7, **CPU** |
| Dd.ColorFill.Sys.512x512 | 1.18/px, 0.7, **CPU** | 1.18/px, 0.7, **CPU** | 1.18/px, 0.7, **CPU** |
| Dd.ColorFill.Sys.64x64 | 1.42/px, 0.7, **CPU** | 1.31/px, 0.7, **CPU** | 1.41/px, 0.7, **CPU** |
| Dd.ColorFill.Vid.16x16 | 51.17/px, 2.7, **HW** | 51.25/px, 2.7, **HW** | 29.91/px, 2.0, **CPU** |
| Dd.ColorFill.Vid.256x256 | 3.24/px, 2.7, **HW** | 3.24/px, 2.7, **HW** | 3.23/px, 2.0, **CPU** |
| Dd.ColorFill.Vid.512x512 | 3.16/px, 2.7, **HW** | 3.16/px, 2.7, **HW** | 3.16/px, 2.0, **CPU** |
| Dd.ColorFill.Vid.64x64 | 4.36/px, 2.7, **HW** | 4.37/px, 2.7, **HW** | 4.37/px, 2.0, **CPU** |
| Dd.Lock.Back | 3,283.16/lock, -, **n/a** | 3,399.01/lock, -, **n/a** | 3,399.84/lock, -, **n/a** |
| Dd.Lock.OffscreenVid | 3,283.70/lock, -, **n/a** | 3,410.68/lock, -, **n/a** | 3,407.32/lock, -, **n/a** |
| Dd.Lock.Primary | 3,290.03/lock, -, **n/a** | 3,397.67/lock, -, **n/a** | 3,398.76/lock, -, **n/a** |
| Dd.Lock.Sys | 961.89/lock, -, **n/a** | 960.59/lock, -, **n/a** | 959.95/lock, -, **n/a** |
| Dd.LockAfterBlt.VidToVid | 49,772,524.00/lock, -, **n/a** | 49,764,768.00/lock, -, **n/a** | 1,093,049.69/lock, -, **n/a** |
| Gdi.Align.Dx0.64x64 | 774.95/px, -, **HW?** | 774.75/px, -, **HW?** | 803.55/px, -, **HW?** |
| Gdi.Align.Dx1.64x64 | 759.96/px, -, **HW?** | 760.18/px, -, **HW?** | 789.34/px, -, **HW?** |
| Gdi.Align.Dx2.64x64 | 762.17/px, -, **HW?** | 761.72/px, -, **HW?** | 791.14/px, -, **HW?** |
| Gdi.Align.Dx3.64x64 | 763.69/px, -, **HW?** | 764.00/px, -, **HW?** | 793.06/px, -, **HW?** |
| Gdi.Align.Dx4.64x64 | 765.69/px, -, **HW?** | 765.76/px, -, **HW?** | 794.93/px, -, **HW?** |
| Gdi.Align.Dx8.64x64 | 773.25/px, -, **HW?** | 772.25/px, -, **HW?** | 801.00/px, -, **HW?** |
| Gdi.BitBltBitmapToScreen.1024x768 | 376.93/px, 6.2, **CPU** | 375.98/px, 5.5, **CPU** | 4.21/px, 3.2, **CPU** |
| Gdi.BitBltBitmapToScreen.128x128 | 379.73/px, 6.2, **CPU** | 379.86/px, 5.5, **CPU** | 6.22/px, 3.2, **CPU** |
| Gdi.BitBltBitmapToScreen.16x16 | 392.06/px, 6.2, **CPU** | 392.00/px, 5.5, **CPU** | 12.98/px, 3.2, **CPU** |
| Gdi.BitBltBitmapToScreen.256x256 | 378.04/px, 6.2, **CPU** | 377.85/px, 5.5, **CPU** | 5.09/px, 3.2, **CPU** |
| Gdi.BitBltBitmapToScreen.32x32 | 381.26/px, 6.2, **CPU** | 381.60/px, 5.5, **CPU** | 5.58/px, 3.2, **CPU** |
| Gdi.BitBltBitmapToScreen.512x512 | 377.00/px, 6.2, **CPU** | 377.09/px, 5.5, **CPU** | 4.56/px, 3.2, **CPU** |
| Gdi.BitBltBitmapToScreen.64x64 | 377.76/px, 6.2, **CPU** | 377.97/px, 5.5, **CPU** | 3.81/px, 3.2, **CPU** |
| Gdi.BitBltBitmapToScreen.8x8 | 413.25/px, 6.2, **CPU** | 413.16/px, 5.5, **CPU** | 51.64/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.1024x768 | 376.61/px, 4.7, **CPU** | 375.48/px, 6.3, **CPU** | 3.84/px, 4.0, **CPU** |
| Gdi.BitBltDibToScreen.128x128 | 379.82/px, 4.7, **CPU** | 380.14/px, 6.3, **CPU** | 6.26/px, 4.0, **CPU** |
| Gdi.BitBltDibToScreen.16x16 | 396.68/px, 4.7, **CPU** | 395.10/px, 6.3, **CPU** | 16.19/px, 4.0, **CPU** |
| Gdi.BitBltDibToScreen.256x256 | 377.88/px, 4.7, **CPU** | 378.17/px, 6.3, **CPU** | 4.91/px, 4.0, **CPU** |
| Gdi.BitBltDibToScreen.32x32 | 382.71/px, 4.7, **CPU** | 382.23/px, 6.3, **CPU** | 6.41/px, 4.0, **CPU** |
| Gdi.BitBltDibToScreen.512x512 | 377.42/px, 4.7, **CPU** | 377.23/px, 6.3, **CPU** | 4.30/px, 4.0, **CPU** |
| Gdi.BitBltDibToScreen.64x64 | 378.76/px, 4.7, **CPU** | 378.23/px, 6.3, **CPU** | 3.92/px, 4.0, **CPU** |
| Gdi.BitBltDibToScreen.8x8 | 431.44/px, 4.7, **CPU** | 425.47/px, 6.3, **CPU** | 64.16/px, 4.0, **CPU** |
| Gdi.BitBltScreenToBitmap.1024x768 | 432.49/px, 5.6, **CPU** | 432.49/px, 5.1, **CPU** | 115.41/px, 163.6, **CPU** |
| Gdi.BitBltScreenToBitmap.128x128 | 435.18/px, 5.6, **CPU** | 434.91/px, 5.1, **CPU** | 153.42/px, 163.6, **CPU** |
| Gdi.BitBltScreenToBitmap.16x16 | 433.47/px, 5.6, **CPU** | 433.31/px, 5.1, **CPU** | 433.79/px, 163.6, **CPU** |
| Gdi.BitBltScreenToBitmap.256x256 | 433.43/px, 5.6, **CPU** | 432.42/px, 5.1, **CPU** | 131.81/px, 163.6, **CPU** |
| Gdi.BitBltScreenToBitmap.32x32 | 434.69/px, 5.6, **CPU** | 433.16/px, 5.1, **CPU** | 433.07/px, 163.6, **CPU** |
| Gdi.BitBltScreenToBitmap.512x512 | 432.44/px, 5.6, **CPU** | 431.80/px, 5.1, **CPU** | 120.68/px, 163.6, **CPU** |
| Gdi.BitBltScreenToBitmap.64x64 | 432.75/px, 5.6, **CPU** | 432.66/px, 5.1, **CPU** | 432.51/px, 163.6, **CPU** |
| Gdi.BitBltScreenToBitmap.8x8 | 463.96/px, 5.6, **CPU** | 463.99/px, 5.1, **CPU** | 464.50/px, 163.6, **CPU** |
| Gdi.BitBltScreenToScreen.128x128 | 786.74/px, 0.8, **CPU** | 787.68/px, 1.2, **CPU** | 284.54/px, 253.9, **CPU** |
| Gdi.BitBltScreenToScreen.16x16 | 783.77/px, 0.8, **CPU** | 783.73/px, 1.2, **CPU** | 815.10/px, 253.9, **CPU** |
| Gdi.BitBltScreenToScreen.256x256 | 786.26/px, 0.8, **CPU** | 786.65/px, 1.2, **CPU** | 244.94/px, 253.9, **CPU** |
| Gdi.BitBltScreenToScreen.32x32 | 788.54/px, 0.8, **CPU** | 788.98/px, 1.2, **CPU** | 819.64/px, 253.9, **CPU** |
| Gdi.BitBltScreenToScreen.64x64 | 786.68/px, 0.8, **CPU** | 786.82/px, 1.2, **CPU** | 816.59/px, 253.9, **CPU** |
| Gdi.BitBltScreenToScreen.8x8 | 814.23/px, 0.8, **CPU** | 814.13/px, 1.2, **CPU** | 848.77/px, 253.9, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.128x128 | 796.56/px, 4.4, **CPU** | 796.61/px, 2.3, **CPU** | 827.89/px, 3.5, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.16x16 | 800.83/px, 4.4, **CPU** | 799.11/px, 2.3, **CPU** | 830.08/px, 3.5, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.256x256 | 795.55/px, 4.4, **CPU** | 794.59/px, 2.3, **CPU** | 826.69/px, 3.5, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.32x32 | 800.69/px, 4.4, **CPU** | 801.29/px, 2.3, **CPU** | 833.31/px, 3.5, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.512x512 | 795.02/px, 4.4, **CPU** | 794.94/px, 2.3, **CPU** | 826.43/px, 3.5, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.64x64 | 799.72/px, 4.4, **CPU** | 800.26/px, 2.3, **CPU** | 830.55/px, 3.5, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.8x8 | 786.16/px, 4.4, **CPU** | 786.91/px, 2.3, **CPU** | 817.83/px, 3.5, **CPU** |
| Gdi.Ellipse.128x128 | 400.06/px, -, **CPU** | 399.03/px, -, **CPU** | 24.89/px, -, **CPU** |
| Gdi.ExtTextOut.FixedOpaque | 1,831,843.67/string, -, **n/a** | 1,829,793.44/string, -, **n/a** | 35,089.34/string, -, **n/a** |
| Gdi.ExtTextOut.FixedTransparent | 358,370.75/string, -, **n/a** | 356,639.67/string, -, **n/a** | 59,383.02/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeOpaque | 1,443,708.38/string, -, **n/a** | 1,435,843.06/string, -, **n/a** | 34,394.25/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeTransparent | 219,224.84/string, -, **n/a** | 211,183.92/string, -, **n/a** | 49,098.37/string, -, **n/a** |
| Gdi.FillRect.1024x768 | 379.47/px, 4.0, **CPU** | 379.38/px, 4.9, **CPU** | 3.07/px, 5.8, **CPU** |
| Gdi.FillRect.128x128 | 382.09/px, 4.0, **CPU** | 382.16/px, 4.9, **CPU** | 6.21/px, 5.8, **CPU** |
| Gdi.FillRect.16x16 | 391.77/px, 4.0, **CPU** | 391.51/px, 4.9, **CPU** | 18.49/px, 5.8, **CPU** |
| Gdi.FillRect.256x256 | 380.33/px, 4.0, **CPU** | 380.29/px, 4.9, **CPU** | 4.62/px, 5.8, **CPU** |
| Gdi.FillRect.32x32 | 381.74/px, 4.0, **CPU** | 381.81/px, 4.9, **CPU** | 6.87/px, 5.8, **CPU** |
| Gdi.FillRect.512x512 | 379.80/px, 4.0, **CPU** | 379.56/px, 4.9, **CPU** | 3.67/px, 5.8, **CPU** |
| Gdi.FillRect.64x64 | 379.42/px, 4.0, **CPU** | 379.49/px, 4.9, **CPU** | 4.05/px, 5.8, **CPU** |
| Gdi.FillRect.8x8 | 430.32/px, 4.0, **CPU** | 429.84/px, 4.9, **CPU** | 73.33/px, 5.8, **CPU** |
| Gdi.LineDiagonal30 | 34,168.95/line, -, **n/a** | 34,788.15/line, -, **n/a** | 16,544.79/line, -, **n/a** |
| Gdi.LineHorizontal | 40,083.79/line, -, **n/a** | 40,445.91/line, -, **n/a** | 3,867.80/line, -, **n/a** |
| Gdi.LineVertical | 76,687.06/line, -, **n/a** | 77,073.52/line, -, **n/a** | 68,170.95/line, -, **n/a** |
| Gdi.PatBltHatch.1024x768 | 379.70/px, 7.1, **CPU** | 379.65/px, 7.2, **CPU** | 3.18/px, 2.8, **CPU** |
| Gdi.PatBltHatch.128x128 | 382.44/px, 7.1, **CPU** | 382.15/px, 7.2, **CPU** | 3.85/px, 2.8, **CPU** |
| Gdi.PatBltHatch.16x16 | 403.09/px, 7.1, **CPU** | 402.85/px, 7.2, **CPU** | 32.14/px, 2.8, **CPU** |
| Gdi.PatBltHatch.256x256 | 380.86/px, 7.1, **CPU** | 381.22/px, 7.2, **CPU** | 3.68/px, 2.8, **CPU** |
| Gdi.PatBltHatch.32x32 | 384.92/px, 7.1, **CPU** | 385.46/px, 7.2, **CPU** | 9.94/px, 2.8, **CPU** |
| Gdi.PatBltHatch.512x512 | 379.70/px, 7.1, **CPU** | 379.87/px, 7.2, **CPU** | 3.43/px, 2.8, **CPU** |
| Gdi.PatBltHatch.64x64 | 381.08/px, 7.1, **CPU** | 381.29/px, 7.2, **CPU** | 4.96/px, 2.8, **CPU** |
| Gdi.PatBltHatch.8x8 | 458.16/px, 7.1, **CPU** | 458.96/px, 7.2, **CPU** | 127.57/px, 2.8, **CPU** |
| Gdi.PatBltPattern.1024x768 | 379.83/px, 5.4, **CPU** | 379.81/px, 5.5, **CPU** | 3.19/px, 2.4, **CPU** |
| Gdi.PatBltPattern.128x128 | 382.69/px, 5.4, **CPU** | 382.64/px, 5.5, **CPU** | 3.85/px, 2.4, **CPU** |
| Gdi.PatBltPattern.16x16 | 403.08/px, 5.4, **CPU** | 403.16/px, 5.5, **CPU** | 24.92/px, 2.4, **CPU** |
| Gdi.PatBltPattern.256x256 | 380.86/px, 5.4, **CPU** | 380.87/px, 5.5, **CPU** | 3.68/px, 2.4, **CPU** |
| Gdi.PatBltPattern.32x32 | 385.26/px, 5.4, **CPU** | 385.34/px, 5.5, **CPU** | 8.16/px, 2.4, **CPU** |
| Gdi.PatBltPattern.512x512 | 380.20/px, 5.4, **CPU** | 380.17/px, 5.5, **CPU** | 3.42/px, 2.4, **CPU** |
| Gdi.PatBltPattern.64x64 | 381.40/px, 5.4, **CPU** | 381.30/px, 5.5, **CPU** | 4.51/px, 2.4, **CPU** |
| Gdi.PatBltPattern.8x8 | 458.19/px, 5.4, **CPU** | 458.23/px, 5.5, **CPU** | 98.23/px, 2.4, **CPU** |
| Gdi.PatBltSolid.1024x768 | 379.54/px, 4.5, **CPU** | 379.70/px, 6.2, **CPU** | 3.06/px, 5.1, **CPU** |
| Gdi.PatBltSolid.128x128 | 381.86/px, 4.5, **CPU** | 382.05/px, 6.2, **CPU** | 6.09/px, 5.1, **CPU** |
| Gdi.PatBltSolid.16x16 | 383.66/px, 4.5, **CPU** | 383.99/px, 6.2, **CPU** | 10.74/px, 5.1, **CPU** |
| Gdi.PatBltSolid.256x256 | 380.60/px, 4.5, **CPU** | 380.70/px, 6.2, **CPU** | 4.59/px, 5.1, **CPU** |
| Gdi.PatBltSolid.32x32 | 379.86/px, 4.5, **CPU** | 381.73/px, 6.2, **CPU** | 4.92/px, 5.1, **CPU** |
| Gdi.PatBltSolid.512x512 | 379.69/px, 4.5, **CPU** | 379.41/px, 6.2, **CPU** | 3.66/px, 5.1, **CPU** |
| Gdi.PatBltSolid.64x64 | 379.21/px, 4.5, **CPU** | 378.94/px, 6.2, **CPU** | 3.58/px, 5.1, **CPU** |
| Gdi.PatBltSolid.8x8 | 397.58/px, 4.5, **CPU** | 398.30/px, 6.2, **CPU** | 42.69/px, 5.1, **CPU** |
| Gdi.Polygon.128x128 | 414.88/px, -, **CPU** | 415.21/px, -, **CPU** | 28.39/px, -, **CPU** |
| Gdi.Polyline | 38,438.03/line, -, **n/a** | 38,418.58/line, -, **n/a** | 1,240.11/line, -, **n/a** |
| Gdi.Rectangle.128x128 | 384.39/px, -, **CPU** | 384.46/px, -, **CPU** | 14.60/px, -, **CPU** |
| Gdi.Rop.BLACKNESS.64x64 | 378.61/px, -, **CPU** | 378.55/px, -, **CPU** | 11.43/px, -, **CPU** |
| Gdi.Rop.NOTSRCCOPY.64x64 | 790.41/px, -, **CPU** | 790.73/px, -, **CPU** | 820.66/px, -, **CPU** |
| Gdi.Rop.PATCOPY.64x64 | 378.28/px, -, **CPU** | 378.31/px, -, **CPU** | 11.44/px, -, **CPU** |
| Gdi.Rop.PSDPXAX.64x64 | 1,227.68/px, -, **CPU** | 1,228.36/px, -, **CPU** | 1,260.95/px, -, **CPU** |
| Gdi.Rop.SRCAND.64x64 | 1,222.53/px, -, **CPU** | 1,221.98/px, -, **CPU** | 1,258.99/px, -, **CPU** |
| Gdi.Rop.SRCCOPY.64x64 | 786.61/px, -, **CPU** | 786.72/px, -, **CPU** | 816.94/px, -, **CPU** |
| Gdi.Rop.SRCINVERT.64x64 | 1,222.04/px, -, **CPU** | 1,222.61/px, -, **CPU** | 1,258.39/px, -, **CPU** |
| Gdi.Rop.WHITENESS.64x64 | 378.29/px, -, **CPU** | 378.44/px, -, **CPU** | 11.43/px, -, **CPU** |
| Gdi.RoundRect.128x128 | 392.35/px, -, **CPU** | 392.29/px, -, **CPU** | 15.77/px, -, **CPU** |
| Gdi.ScrollBitBlt | 799.74/px, -, **CPU** | 800.30/px, -, **CPU** | 222.55/px, -, **CPU** |
| Gdi.ScrollDC | 799.69/px, -, **CPU** | 799.87/px, -, **CPU** | 222.68/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | 377.42/px, -, **CPU** | 377.24/px, -, **CPU** | 27.36/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | 377.48/px, -, **CPU** | 377.53/px, -, **CPU** | 27.42/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | 377.19/px, -, **CPU** | 377.23/px, -, **CPU** | 27.35/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | 377.29/px, -, **CPU** | 377.79/px, -, **CPU** | 27.39/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | 379.20/px, -, **CPU** | 379.28/px, -, **CPU** | 9.48/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | 379.39/px, -, **CPU** | 379.17/px, -, **CPU** | 9.48/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | 379.52/px, -, **CPU** | 379.25/px, -, **CPU** | 9.48/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | 379.07/px, -, **CPU** | 379.32/px, -, **CPU** | 9.47/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | 380.16/px, -, **CPU** | 380.03/px, -, **CPU** | 3.84/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | 380.78/px, -, **CPU** | 381.02/px, -, **CPU** | 3.99/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | 380.26/px, -, **CPU** | 380.18/px, -, **CPU** | 3.85/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | 380.91/px, -, **CPU** | 380.50/px, -, **CPU** | 3.99/px, -, **CPU** |
| Gdi.StretchBltDown.128x128 | 2,204.41/px, -, **CPU** | 2,202.80/px, -, **CPU** | 687.44/px, -, **CPU** |
| Gdi.StretchBltDown.64x64 | 2,227.32/px, -, **CPU** | 2,229.49/px, -, **CPU** | 794.29/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.128x128 | 2,203.00/px, -, **CPU** | 2,200.60/px, -, **CPU** | 687.12/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.64x64 | 2,227.38/px, -, **CPU** | 2,224.47/px, -, **CPU** | 794.03/px, -, **CPU** |
| Gdi.StretchBltUp.128x128 | 518.67/px, -, **CPU** | 519.09/px, -, **CPU** | 146.37/px, -, **CPU** |
| Gdi.StretchBltUp.256x256 | 512.97/px, -, **CPU** | 513.11/px, -, **CPU** | 73.48/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.128x128 | 518.95/px, -, **CPU** | 519.12/px, -, **CPU** | 146.28/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.256x256 | 512.23/px, -, **CPU** | 512.14/px, -, **CPU** | 73.47/px, -, **CPU** |
| Gdi.StretchDIBits.128x128 | 383.37/px, -, **CPU** | 383.51/px, -, **CPU** | 4.62/px, -, **CPU** |
| Gdi.StretchDIBits.256x256 | 381.57/px, -, **CPU** | 381.36/px, -, **CPU** | 4.42/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.128x128 | 418.07/px, -, **CPU** | 417.62/px, -, **CPU** | 43.78/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.256x256 | 405.86/px, -, **CPU** | 406.29/px, -, **CPU** | 32.89/px, -, **CPU** |
| Gdi.TextOut.FixedOpaque | 1,830,238.69/string, -, **n/a** | 1,831,139.92/string, -, **n/a** | 37,137.62/string, -, **n/a** |
| Gdi.TextOut.FixedTransparent | 358,050.97/string, -, **n/a** | 356,560.09/string, -, **n/a** | 59,072.17/string, -, **n/a** |
| Gdi.TextOut.TrueTypeOpaque | 1,443,205.97/string, -, **n/a** | 1,437,484.25/string, -, **n/a** | 34,336.24/string, -, **n/a** |
| Gdi.TextOut.TrueTypeTransparent | 219,023.68/string, -, **n/a** | 210,579.61/string, -, **n/a** | 48,690.91/string, -, **n/a** |

## Opportunities: w9qi2f4f-ilk-v9x on the CPU where another configuration proves hardware

None.

## CLAIMED-NOT-DELIVERED

- w9qi2f4f-ilk-v9x: Dd.Blt.VidToVid.Plain.16x16 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.Blt.VidToVid.Plain.256x256 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.Blt.VidToVid.Plain.512x512 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.Blt.VidToVid.Plain.64x64 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.BltFast.VidToVid.Plain.16x16 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.BltFast.VidToVid.Plain.256x256 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.BltFast.VidToVid.Plain.512x512 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.BltFast.VidToVid.Plain.64x64 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.ColorFill.Vid.16x16 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.ColorFill.Vid.256x256 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.ColorFill.Vid.512x512 claimed, timed CPU
- w9qi2f4f-ilk-v9x: Dd.ColorFill.Vid.64x64 claimed, timed CPU

## Problems

- w9qi2f4f-ilk-v9x: DBGDI: truncated report

