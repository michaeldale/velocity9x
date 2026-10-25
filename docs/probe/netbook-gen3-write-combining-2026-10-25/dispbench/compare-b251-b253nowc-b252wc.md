# GDI and DirectDraw: w9qi2be2-gma950-v9x / w9qi2be2-gma950-v9x / w9qi2be2-gma950-v9x

Caveats, always printed: every verdict is against that run's own machine
ceilings (FbWrite/FbRead/MemFill/MemCopy/DibToDibBlt); cycles per unit
compare across machines, MB/s and microseconds do not. RASTERCAPS says
nothing about GDI acceleration on Win9x. There is no single score.

| | w9qi2be2-gma950-v9x | w9qi2be2-gma950-v9x | w9qi2be2-gma950-v9x |
|---|---|---|---|
| Driver | Velocity9x Intel GMA 950 (945GSE) (v9xdisp.drv 0x3B5FD3C3) | Velocity9x Intel GMA 950 (945GSE) (v9xdisp.drv 0xE197671F) | Velocity9x Intel GMA 950 (945GSE) (v9xdisp.drv 0xE197671F) |
| Run | 20261025-131336, build 31b28a4 | 20261025-142004, build 31b28a4 | 20261025-135956, build 31b28a4 |
| Mode, environment | 1024x576x16, metal | 1024x576x16, metal | 1024x576x16, metal |
| FbWrite / FbRead MB/s | 105.5 / 25.6 | 105.1 / 25.7 | 2,814.4 / 25.7 |

| Operation | w9qi2be2-gma950-v9x cyc/unit, setup us, verdict | w9qi2be2-gma950-v9x cyc/unit, setup us, verdict | w9qi2be2-gma950-v9x cyc/unit, setup us, verdict |
|---|---|---|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | 37.10/px, 127.5, **CPU** | 24.21/px, 125.7, **CPU** | 23.98/px, 125.6, **CPU** |
| Dd.Blt.SysToSys.KeySrc.256x256 | 13.11/px, 127.5, **CPU** | 13.11/px, 125.7, **CPU** | 13.07/px, 125.6, **CPU** |
| Dd.Blt.SysToSys.KeySrc.512x512 | 2.89/px, 127.5, **CPU-fast** | 2.91/px, 125.7, **CPU-fast** | 2.86/px, 125.6, **CPU-fast** |
| Dd.Blt.SysToSys.KeySrc.64x64 | 15.05/px, 127.5, **CPU** | 14.24/px, 125.7, **CPU** | 14.10/px, 125.6, **CPU** |
| Dd.Blt.SysToSys.Plain.16x16 | 10.05/px, -1.7, **CPU** | 10.16/px, -1.6, **CPU** | 9.92/px, -2.9, **CPU** |
| Dd.Blt.SysToSys.Plain.256x256 | 2.62/px, -1.7, **CPU-fast** | 2.64/px, -1.6, **CPU-fast** | 2.45/px, -2.9, **CPU-fast** |
| Dd.Blt.SysToSys.Plain.512x512 | 2.80/px, -1.7, **CPU-fast** | 2.81/px, -1.6, **CPU-fast** | 2.74/px, -2.9, **CPU-fast** |
| Dd.Blt.SysToSys.Plain.64x64 | 2.69/px, -1.7, **CPU-fast** | 2.64/px, -1.6, **CPU-fast** | 2.63/px, -2.9, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.16x16 | 10.75/px, -3.9, **CPU** | 10.68/px, -5.9, **CPU** | 10.60/px, -4.5, **CPU** |
| Dd.Blt.SysToSys.Stretch.256x256 | 1.54/px, -3.9, **CPU-fast** | 1.44/px, -5.9, **CPU-fast** | 1.45/px, -4.5, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.512x512 | 1.96/px, -3.9, **CPU-fast** | 2.04/px, -5.9, **CPU-fast** | 1.92/px, -4.5, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.64x64 | 2.22/px, -3.9, **CPU-fast** | 2.22/px, -5.9, **CPU-fast** | 2.21/px, -4.5, **CPU-fast** |
| Dd.Blt.SysToVid.KeySrc.16x16 | 76.00/px, 295.9, **CPU** | 75.87/px, 296.7, **CPU** | 56.57/px, 28.8, **CPU** |
| Dd.Blt.SysToVid.KeySrc.256x256 | 37.98/px, 295.9, **CPU** | 38.11/px, 296.7, **CPU** | 13.05/px, 28.8, **CPU** |
| Dd.Blt.SysToVid.KeySrc.512x512 | 14.03/px, 295.9, **HW?** | 14.12/px, 296.7, **HW?** | 11.15/px, 28.8, **CPU** |
| Dd.Blt.SysToVid.KeySrc.64x64 | 40.68/px, 295.9, **CPU** | 41.19/px, 296.7, **CPU** | 16.31/px, 28.8, **CPU** |
| Dd.Blt.SysToVid.Plain.16x16 | 51.24/px, 0.1, **CPU** | 51.01/px, 2.3, **HW?** | 48.15/px, 4.2, **CPU** |
| Dd.Blt.SysToVid.Plain.256x256 | 15.59/px, 0.1, **HW?** | 15.81/px, 2.3, **HW?** | 1.89/px, 4.2, **CPU-fast** |
| Dd.Blt.SysToVid.Plain.512x512 | 15.96/px, 0.1, **HW?** | 15.98/px, 2.3, **HW?** | 2.00/px, 4.2, **CPU-fast** |
| Dd.Blt.SysToVid.Plain.64x64 | 18.02/px, 0.1, **HW?** | 18.12/px, 2.3, **HW?** | 4.65/px, 4.2, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.16x16 | 53.45/px, 7.8, **HW?** | 53.20/px, 7.3, **HW?** | 48.61/px, 7.5, **CPU** |
| Dd.Blt.SysToVid.Stretch.256x256 | 16.10/px, 7.8, **HW?** | 16.09/px, 7.3, **HW?** | 1.40/px, 7.5, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.512x512 | 15.84/px, 7.8, **HW?** | 15.88/px, 7.3, **HW?** | 1.20/px, 7.5, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.64x64 | 18.49/px, 7.8, **HW?** | 18.58/px, 7.3, **HW?** | 3.69/px, 7.5, **CPU-fast** |
| Dd.Blt.VidToSys.KeySrc.16x16 | 178.37/px, 6.6, **CPU** | 191.88/px, 2.5, **CPU** | 178.87/px, -11.8, **CPU** |
| Dd.Blt.VidToSys.KeySrc.256x256 | 139.09/px, 6.6, **CPU** | 139.42/px, 2.5, **CPU** | 139.22/px, -11.8, **CPU** |
| Dd.Blt.VidToSys.KeySrc.512x512 | 139.01/px, 6.6, **CPU** | 140.15/px, 2.5, **CPU** | 140.63/px, -11.8, **CPU** |
| Dd.Blt.VidToSys.KeySrc.64x64 | 142.44/px, 6.6, **CPU** | 147.77/px, 2.5, **CPU** | 142.22/px, -11.8, **CPU** |
| Dd.Blt.VidToSys.Plain.16x16 | 101.31/px, 13.3, **HW?** | 101.76/px, 8.0, **noisy** | 101.41/px, 6.0, **HW?** |
| Dd.Blt.VidToSys.Plain.256x256 | 65.78/px, 13.3, **HW?** | 65.35/px, 8.0, **HW?** | 65.22/px, 6.0, **HW?** |
| Dd.Blt.VidToSys.Plain.512x512 | 65.01/px, 13.3, **HW?** | 65.08/px, 8.0, **HW?** | 65.09/px, 6.0, **HW?** |
| Dd.Blt.VidToSys.Plain.64x64 | 67.56/px, 13.3, **HW?** | 67.81/px, 8.0, **HW?** | 67.57/px, 6.0, **HW?** |
| Dd.Blt.VidToSys.Stretch.16x16 | 72.77/px, 2.3, **HW?** | 71.93/px, 1.9, **HW?** | 71.73/px, 4.7, **HW?** |
| Dd.Blt.VidToSys.Stretch.256x256 | 34.19/px, 2.3, **HW?** | 34.23/px, 1.9, **HW?** | 34.23/px, 4.7, **HW?** |
| Dd.Blt.VidToSys.Stretch.512x512 | 34.40/px, 2.3, **HW?** | 34.47/px, 1.9, **HW?** | 34.27/px, 4.7, **HW?** |
| Dd.Blt.VidToSys.Stretch.64x64 | 36.61/px, 2.3, **HW?** | 36.81/px, 1.9, **HW?** | 37.21/px, 4.7, **HW?** |
| Dd.Blt.VidToVid.KeySrc.16x16 | 231.25/px, -47.2, **CPU** | 232.94/px, 19.0, **CPU** | 209.10/px, 18.8, **CPU** |
| Dd.Blt.VidToVid.KeySrc.256x256 | 165.88/px, -47.2, **CPU** | 166.51/px, 19.0, **CPU** | 139.94/px, 18.8, **CPU** |
| Dd.Blt.VidToVid.KeySrc.512x512 | 170.49/px, -47.2, **CPU** | 165.68/px, 19.0, **CPU** | 139.10/px, 18.8, **CPU** |
| Dd.Blt.VidToVid.KeySrc.64x64 | 170.80/px, -47.2, **CPU** | 171.39/px, 19.0, **CPU** | 144.49/px, 18.8, **CPU** |
| Dd.Blt.VidToVid.Plain.16x16 | 47.86/px, 5.2, **HW?** | 63.14/px, 6.2, **HW?** | 45.74/px, 5.2, **CPU** |
| Dd.Blt.VidToVid.Plain.256x256 | 3.08/px, 5.2, **HW** | 3.09/px, 6.2, **HW** | 3.09/px, 5.2, **CPU-fast** |
| Dd.Blt.VidToVid.Plain.512x512 | 2.98/px, 5.2, **HW** | 2.98/px, 6.2, **HW** | 2.98/px, 5.2, **CPU-fast** |
| Dd.Blt.VidToVid.Plain.64x64 | 4.28/px, 5.2, **HW** | 4.25/px, 6.2, **HW** | 4.29/px, 5.2, **CPU-fast** |
| Dd.Blt.VidToVid.Stretch.16x16 | 113.40/px, 7.5, **CPU** | 131.40/px, 10.6, **CPU** | 120.08/px, 13.8, **CPU** |
| Dd.Blt.VidToVid.Stretch.256x256 | 48.77/px, 7.5, **CPU** | 48.85/px, 10.6, **CPU** | 33.24/px, 13.8, **CPU** |
| Dd.Blt.VidToVid.Stretch.512x512 | 48.78/px, 7.5, **CPU** | 48.75/px, 10.6, **CPU** | 32.87/px, 13.8, **CPU** |
| Dd.Blt.VidToVid.Stretch.64x64 | 52.58/px, 7.5, **CPU** | 53.22/px, 10.6, **CPU** | 37.40/px, 13.8, **CPU** |
| Dd.BltFast.SysToSys.Plain.16x16 | 8.98/px, -1.9, **CPU** | 9.26/px, -2.2, **CPU** | 8.85/px, -3.1, **CPU** |
| Dd.BltFast.SysToSys.Plain.256x256 | 2.56/px, -1.9, **CPU-fast** | 2.57/px, -2.2, **CPU-fast** | 2.43/px, -3.1, **CPU-fast** |
| Dd.BltFast.SysToSys.Plain.512x512 | 2.75/px, -1.9, **CPU-fast** | 2.78/px, -2.2, **CPU-fast** | 2.73/px, -3.1, **CPU-fast** |
| Dd.BltFast.SysToSys.Plain.64x64 | 2.62/px, -1.9, **CPU-fast** | 2.54/px, -2.2, **CPU-fast** | 2.55/px, -3.1, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.16x16 | 49.42/px, -1.1, **CPU** | 49.45/px, 0.3, **CPU** | 60.03/px, 3.6, **CPU** |
| Dd.BltFast.SysToVid.Plain.256x256 | 15.68/px, -1.1, **HW?** | 15.68/px, 0.3, **HW?** | 1.81/px, 3.6, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.512x512 | 16.15/px, -1.1, **HW?** | 16.01/px, 0.3, **HW?** | 2.00/px, 3.6, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.64x64 | 18.37/px, -1.1, **HW?** | 18.01/px, 0.3, **HW?** | 4.18/px, 3.6, **CPU-fast** |
| Dd.BltFast.VidToSys.Plain.16x16 | 10.47/px, -0.5, **HW?** | 10.45/px, -3.8, **HW?** | 10.34/px, -3.2, **HW?** |
| Dd.BltFast.VidToSys.Plain.256x256 | 2.70/px, -0.5, **HW?** | 2.49/px, -3.8, **HW?** | 2.43/px, -3.2, **HW?** |
| Dd.BltFast.VidToSys.Plain.512x512 | 2.79/px, -0.5, **HW?** | 2.87/px, -3.8, **HW?** | 2.77/px, -3.2, **HW?** |
| Dd.BltFast.VidToSys.Plain.64x64 | 2.84/px, -0.5, **HW?** | 2.81/px, -3.8, **HW?** | 2.84/px, -3.2, **HW?** |
| Dd.BltFast.VidToVid.Plain.16x16 | 45.37/px, 5.1, **HW?** | 45.66/px, 5.1, **HW?** | 43.56/px, 5.0, **CPU** |
| Dd.BltFast.VidToVid.Plain.256x256 | 3.09/px, 5.1, **HW** | 3.09/px, 5.1, **HW** | 3.09/px, 5.0, **CPU-fast** |
| Dd.BltFast.VidToVid.Plain.512x512 | 2.98/px, 5.1, **HW** | 2.98/px, 5.1, **HW** | 2.98/px, 5.0, **CPU-fast** |
| Dd.BltFast.VidToVid.Plain.64x64 | 4.29/px, 5.1, **HW** | 4.29/px, 5.1, **HW** | 4.28/px, 5.0, **CPU-fast** |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | 74.89/px, 295.5, **CPU** | 74.90/px, 298.0, **CPU** | 55.18/px, 29.6, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | 37.99/px, 295.5, **CPU** | 38.25/px, 298.0, **CPU** | 13.33/px, 29.6, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | 14.08/px, 295.5, **HW** | 14.10/px, 298.0, **HW** | 11.35/px, 29.6, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | 40.81/px, 295.5, **CPU** | 40.76/px, 298.0, **CPU** | 16.40/px, 29.6, **CPU** |
| Dd.ColorFill.Sys.16x16 | 8.38/px, 1.2, **CPU** | 8.33/px, 1.3, **CPU** | 8.31/px, 1.5, **CPU** |
| Dd.ColorFill.Sys.256x256 | 1.39/px, 1.2, **CPU-fast** | 1.39/px, 1.3, **CPU-fast** | 1.39/px, 1.5, **CPU-fast** |
| Dd.ColorFill.Sys.512x512 | 1.39/px, 1.2, **CPU-fast** | 1.37/px, 1.3, **CPU-fast** | 1.36/px, 1.5, **CPU-fast** |
| Dd.ColorFill.Sys.64x64 | 2.06/px, 1.2, **CPU-fast** | 2.04/px, 1.3, **CPU-fast** | 2.04/px, 1.5, **CPU-fast** |
| Dd.ColorFill.Vid.16x16 | 50.69/px, 4.9, **noisy** | 45.90/px, 4.5, **HW?** | 42.50/px, 4.4, **CPU** |
| Dd.ColorFill.Vid.256x256 | 1.61/px, 4.9, **HW** | 1.59/px, 4.5, **HW** | 1.60/px, 4.4, **CPU-fast** |
| Dd.ColorFill.Vid.512x512 | 1.55/px, 4.9, **HW** | 1.55/px, 4.5, **HW** | 1.54/px, 4.4, **CPU-fast** |
| Dd.ColorFill.Vid.64x64 | 2.81/px, 4.9, **HW** | 2.86/px, 4.5, **HW** | 2.81/px, 4.4, **CPU-fast** |
| Dd.Lock.Back | 8,475.00/lock, -, **n/a** | 8,378.06/lock, -, **n/a** | 8,345.42/lock, -, **n/a** |
| Dd.Lock.OffscreenVid | 8,507.61/lock, -, **n/a** | 8,411.07/lock, -, **n/a** | 8,358.37/lock, -, **n/a** |
| Dd.Lock.Primary | 8,446.48/lock, -, **n/a** | 8,359.73/lock, -, **n/a** | 8,295.83/lock, -, **n/a** |
| Dd.Lock.Sys | 2,253.37/lock, -, **n/a** | 2,225.29/lock, -, **n/a** | 2,216.23/lock, -, **n/a** |
| Dd.LockAfterBlt.VidToVid | 4,210,994.55/lock, -, **n/a** | 4,224,847.27/lock, -, **n/a** | 551,757.73/lock, -, **n/a** |
| Gdi.Align.Dx0.64x64 | 159.57/px, -, **CPU** | 160.11/px, -, **CPU** | 133.42/px, -, **CPU** |
| Gdi.Align.Dx1.64x64 | 192.04/px, -, **CPU** | 192.81/px, -, **CPU** | 154.62/px, -, **CPU** |
| Gdi.Align.Dx2.64x64 | 159.76/px, -, **CPU** | 160.13/px, -, **CPU** | 135.28/px, -, **CPU** |
| Gdi.Align.Dx3.64x64 | 192.04/px, -, **CPU** | 193.07/px, -, **CPU** | 154.29/px, -, **CPU** |
| Gdi.Align.Dx4.64x64 | 159.51/px, -, **CPU** | 159.49/px, -, **CPU** | 134.55/px, -, **CPU** |
| Gdi.Align.Dx8.64x64 | 159.47/px, -, **CPU** | 159.41/px, -, **CPU** | 133.63/px, -, **CPU** |
| Gdi.BitBltBitmapToScreen.1024x576 | 30.96/px, 5.0, **CPU** | 30.94/px, 6.9, **CPU** | 9.67/px, 6.5, **CPU** |
| Gdi.BitBltBitmapToScreen.128x128 | 31.59/px, 5.0, **CPU** | 31.58/px, 6.9, **CPU** | 10.49/px, 6.5, **CPU** |
| Gdi.BitBltBitmapToScreen.16x16 | 69.32/px, 5.0, **CPU** | 80.68/px, 6.9, **CPU** | 54.03/px, 6.5, **CPU** |
| Gdi.BitBltBitmapToScreen.256x256 | 30.85/px, 5.0, **CPU** | 30.97/px, 6.9, **CPU** | 9.81/px, 6.5, **CPU** |
| Gdi.BitBltBitmapToScreen.32x32 | 41.36/px, 5.0, **CPU** | 40.48/px, 6.9, **CPU** | 19.67/px, 6.5, **CPU** |
| Gdi.BitBltBitmapToScreen.512x512 | 30.98/px, 5.0, **CPU** | 30.88/px, 6.9, **CPU** | 9.72/px, 6.5, **CPU** |
| Gdi.BitBltBitmapToScreen.64x64 | 33.84/px, 5.0, **CPU** | 33.72/px, 6.9, **CPU** | 12.18/px, 6.5, **CPU** |
| Gdi.BitBltBitmapToScreen.8x8 | 182.08/px, 5.0, **CPU** | 227.31/px, 6.9, **CPU** | 174.30/px, 6.5, **CPU** |
| Gdi.BitBltDibToScreen.1024x576 | 31.02/px, 11.8, **CPU** | 31.00/px, 5.8, **CPU** | 7.92/px, 14.0, **CPU** |
| Gdi.BitBltDibToScreen.128x128 | 32.12/px, 11.8, **CPU** | 32.13/px, 5.8, **CPU** | 9.00/px, 14.0, **CPU** |
| Gdi.BitBltDibToScreen.16x16 | 98.79/px, 11.8, **CPU** | 112.12/px, 5.8, **CPU** | 85.87/px, 14.0, **CPU** |
| Gdi.BitBltDibToScreen.256x256 | 31.30/px, 11.8, **CPU** | 30.99/px, 5.8, **CPU** | 8.06/px, 14.0, **CPU** |
| Gdi.BitBltDibToScreen.32x32 | 48.34/px, 11.8, **CPU** | 48.48/px, 5.8, **CPU** | 30.97/px, 14.0, **CPU** |
| Gdi.BitBltDibToScreen.512x512 | 30.93/px, 11.8, **CPU** | 31.71/px, 5.8, **CPU** | 7.96/px, 14.0, **CPU** |
| Gdi.BitBltDibToScreen.64x64 | 35.51/px, 11.8, **CPU** | 36.49/px, 5.8, **CPU** | 18.50/px, 14.0, **CPU** |
| Gdi.BitBltDibToScreen.8x8 | 295.21/px, 11.8, **CPU** | 349.13/px, 5.8, **CPU** | 288.29/px, 14.0, **CPU** |
| Gdi.BitBltScreenToBitmap.1024x576 | 131.75/px, 60.1, **CPU** | 131.71/px, 0.0, **CPU** | 138.79/px, 7.3, **CPU** |
| Gdi.BitBltScreenToBitmap.128x128 | 139.85/px, 60.1, **CPU** | 132.64/px, 0.0, **CPU** | 141.29/px, 7.3, **CPU** |
| Gdi.BitBltScreenToBitmap.16x16 | 178.39/px, 60.1, **CPU** | 177.76/px, 0.0, **CPU** | 178.12/px, 7.3, **CPU** |
| Gdi.BitBltScreenToBitmap.256x256 | 138.83/px, 60.1, **CPU** | 139.41/px, 0.0, **CPU** | 138.74/px, 7.3, **CPU** |
| Gdi.BitBltScreenToBitmap.32x32 | 141.00/px, 60.1, **CPU** | 151.62/px, 0.0, **CPU** | 140.98/px, 7.3, **CPU** |
| Gdi.BitBltScreenToBitmap.512x512 | 131.41/px, 60.1, **CPU** | 138.50/px, 0.0, **CPU** | 138.37/px, 7.3, **CPU** |
| Gdi.BitBltScreenToBitmap.64x64 | 141.01/px, 60.1, **CPU** | 141.08/px, 0.0, **CPU** | 133.72/px, 7.3, **CPU** |
| Gdi.BitBltScreenToBitmap.8x8 | 295.98/px, 60.1, **CPU** | 344.23/px, 0.0, **CPU** | 292.83/px, 7.3, **CPU** |
| Gdi.BitBltScreenToScreen.128x128 | 158.50/px, 7.7, **CPU** | 157.67/px, 5.9, **CPU** | 131.57/px, 8.0, **CPU** |
| Gdi.BitBltScreenToScreen.16x16 | 195.94/px, 7.7, **CPU** | 194.77/px, 5.9, **CPU** | 173.82/px, 8.0, **CPU** |
| Gdi.BitBltScreenToScreen.256x256 | 156.96/px, 7.7, **CPU** | 157.37/px, 5.9, **CPU** | 130.43/px, 8.0, **CPU** |
| Gdi.BitBltScreenToScreen.32x32 | 166.48/px, 7.7, **CPU** | 166.76/px, 5.9, **CPU** | 141.99/px, 8.0, **CPU** |
| Gdi.BitBltScreenToScreen.64x64 | 159.41/px, 7.7, **CPU** | 160.23/px, 5.9, **CPU** | 133.45/px, 8.0, **CPU** |
| Gdi.BitBltScreenToScreen.8x8 | 310.39/px, 7.7, **CPU** | 304.76/px, 5.9, **CPU** | 293.84/px, 8.0, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.128x128 | 168.45/px, 11.1, **CPU** | 168.40/px, 10.6, **CPU** | 132.55/px, 15.8, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.16x16 | 219.66/px, 11.1, **CPU** | 230.18/px, 10.6, **CPU** | 186.37/px, 15.8, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.256x256 | 166.93/px, 11.1, **CPU** | 167.20/px, 10.6, **CPU** | 131.27/px, 15.8, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.32x32 | 180.17/px, 11.1, **CPU** | 180.67/px, 10.6, **CPU** | 145.04/px, 15.8, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.512x512 | 166.54/px, 11.1, **CPU** | 166.86/px, 10.6, **CPU** | 130.44/px, 15.8, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.64x64 | 170.35/px, 11.1, **CPU** | 170.17/px, 10.6, **CPU** | 138.39/px, 15.8, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.8x8 | 314.21/px, 11.1, **CPU** | 356.31/px, 10.6, **CPU** | 292.10/px, 15.8, **CPU** |
| Gdi.Ellipse.128x128 | 72.42/px, -, **CPU** | 62.29/px, -, **CPU** | 35.72/px, -, **CPU** |
| Gdi.ExtTextOut.FixedOpaque | 185,618.05/string, -, **n/a** | 186,561.57/string, -, **n/a** | 82,012.07/string, -, **n/a** |
| Gdi.ExtTextOut.FixedTransparent | 105,304.87/string, -, **n/a** | 107,821.86/string, -, **n/a** | 70,706.79/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeOpaque | 153,262.01/string, -, **n/a** | 173,274.36/string, -, **n/a** | 76,585.93/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeTransparent | 80,893.05/string, -, **n/a** | 101,343.51/string, -, **n/a** | 68,144.97/string, -, **n/a** |
| Gdi.FillRect.1024x576 | 30.53/px, 7.3, **CPU** | 30.71/px, 7.4, **CPU** | 1.76/px, 8.8, **CPU-fast** |
| Gdi.FillRect.128x128 | 31.26/px, 7.3, **CPU** | 31.53/px, 7.4, **CPU** | 2.76/px, 8.8, **CPU-fast** |
| Gdi.FillRect.16x16 | 76.62/px, 7.3, **CPU** | 76.51/px, 7.4, **CPU** | 59.74/px, 8.8, **CPU** |
| Gdi.FillRect.256x256 | 30.71/px, 7.3, **CPU** | 30.82/px, 7.4, **CPU** | 2.04/px, 8.8, **CPU-fast** |
| Gdi.FillRect.32x32 | 42.15/px, 7.3, **CPU** | 42.99/px, 7.4, **CPU** | 14.50/px, 8.8, **CPU** |
| Gdi.FillRect.512x512 | 30.56/px, 7.3, **CPU** | 30.70/px, 7.4, **CPU** | 1.83/px, 8.8, **CPU-fast** |
| Gdi.FillRect.64x64 | 33.49/px, 7.3, **CPU** | 33.46/px, 7.4, **CPU** | 5.18/px, 8.8, **CPU-fast** |
| Gdi.FillRect.8x8 | 221.63/px, 7.3, **CPU** | 222.60/px, 7.4, **CPU** | 203.05/px, 8.8, **CPU** |
| Gdi.LineDiagonal30 | 14,017.29/line, -, **n/a** | 20,103.55/line, -, **n/a** | 14,227.20/line, -, **n/a** |
| Gdi.LineHorizontal | 14,717.57/line, -, **n/a** | 14,682.66/line, -, **n/a** | 9,336.49/line, -, **n/a** |
| Gdi.LineVertical | 15,077.27/line, -, **n/a** | 21,253.88/line, -, **n/a** | 19,051.70/line, -, **n/a** |
| Gdi.PatBltHatch.1024x576 | 30.57/px, 6.8, **CPU** | 30.55/px, 7.6, **CPU** | 6.81/px, 7.3, **CPU** |
| Gdi.PatBltHatch.128x128 | 31.47/px, 6.8, **CPU** | 31.40/px, 7.6, **CPU** | 7.67/px, 7.3, **CPU** |
| Gdi.PatBltHatch.16x16 | 72.26/px, 6.8, **CPU** | 71.49/px, 7.6, **CPU** | 54.16/px, 7.3, **CPU** |
| Gdi.PatBltHatch.256x256 | 30.89/px, 6.8, **CPU** | 30.78/px, 7.6, **CPU** | 7.05/px, 7.3, **CPU** |
| Gdi.PatBltHatch.32x32 | 41.54/px, 6.8, **CPU** | 44.59/px, 7.6, **CPU** | 17.11/px, 7.3, **CPU** |
| Gdi.PatBltHatch.512x512 | 30.77/px, 6.8, **CPU** | 30.65/px, 7.6, **CPU** | 6.86/px, 7.3, **CPU** |
| Gdi.PatBltHatch.64x64 | 33.86/px, 6.8, **CPU** | 34.43/px, 7.6, **CPU** | 9.48/px, 7.3, **CPU** |
| Gdi.PatBltHatch.8x8 | 192.07/px, 6.8, **CPU** | 189.28/px, 7.6, **CPU** | 177.53/px, 7.3, **CPU** |
| Gdi.PatBltPattern.1024x576 | 30.58/px, 7.6, **CPU** | 30.62/px, 8.0, **CPU** | 6.81/px, 7.3, **CPU** |
| Gdi.PatBltPattern.128x128 | 31.46/px, 7.6, **CPU** | 31.64/px, 8.0, **CPU** | 7.67/px, 7.3, **CPU** |
| Gdi.PatBltPattern.16x16 | 72.55/px, 7.6, **CPU** | 71.58/px, 8.0, **CPU** | 54.07/px, 7.3, **CPU** |
| Gdi.PatBltPattern.256x256 | 30.78/px, 7.6, **CPU** | 30.83/px, 8.0, **CPU** | 7.06/px, 7.3, **CPU** |
| Gdi.PatBltPattern.32x32 | 41.41/px, 7.6, **CPU** | 44.51/px, 8.0, **CPU** | 17.12/px, 7.3, **CPU** |
| Gdi.PatBltPattern.512x512 | 30.57/px, 7.6, **CPU** | 30.64/px, 8.0, **CPU** | 6.87/px, 7.3, **CPU** |
| Gdi.PatBltPattern.64x64 | 33.64/px, 7.6, **CPU** | 33.68/px, 8.0, **CPU** | 9.48/px, 7.3, **CPU** |
| Gdi.PatBltPattern.8x8 | 192.25/px, 7.6, **CPU** | 190.05/px, 8.0, **CPU** | 178.14/px, 7.3, **CPU** |
| Gdi.PatBltSolid.1024x576 | 30.60/px, 4.1, **CPU** | 30.56/px, 5.3, **CPU** | 1.76/px, 5.5, **CPU-fast** |
| Gdi.PatBltSolid.128x128 | 31.00/px, 4.1, **CPU** | 31.15/px, 5.3, **CPU** | 2.42/px, 5.5, **CPU-fast** |
| Gdi.PatBltSolid.16x16 | 56.23/px, 4.1, **CPU** | 55.53/px, 5.3, **CPU** | 38.96/px, 5.5, **CPU** |
| Gdi.PatBltSolid.256x256 | 30.64/px, 4.1, **CPU** | 30.71/px, 5.3, **CPU** | 1.96/px, 5.5, **CPU-fast** |
| Gdi.PatBltSolid.32x32 | 36.93/px, 4.1, **CPU** | 36.83/px, 5.3, **CPU** | 9.24/px, 5.5, **CPU** |
| Gdi.PatBltSolid.512x512 | 30.56/px, 4.1, **CPU** | 30.53/px, 5.3, **CPU** | 1.81/px, 5.5, **CPU-fast** |
| Gdi.PatBltSolid.64x64 | 32.13/px, 4.1, **CPU** | 32.94/px, 5.3, **CPU** | 3.85/px, 5.5, **CPU-fast** |
| Gdi.PatBltSolid.8x8 | 133.13/px, 4.1, **CPU** | 130.55/px, 5.3, **CPU** | 120.31/px, 5.5, **CPU** |
| Gdi.Polygon.128x128 | 52.81/px, -, **CPU** | 46.51/px, -, **CPU** | 21.64/px, -, **CPU** |
| Gdi.Polyline | 6,726.44/line, -, **n/a** | 6,634.42/line, -, **n/a** | 1,439.59/line, -, **n/a** |
| Gdi.Rectangle.128x128 | 33.15/px, -, **CPU** | 33.32/px, -, **CPU** | 6.52/px, -, **CPU** |
| Gdi.Rop.BLACKNESS.64x64 | 32.72/px, -, **CPU** | 33.45/px, -, **CPU** | 9.35/px, -, **CPU** |
| Gdi.Rop.NOTSRCCOPY.64x64 | 171.43/px, -, **CPU** | 172.15/px, -, **CPU** | 134.53/px, -, **CPU** |
| Gdi.Rop.PATCOPY.64x64 | 32.78/px, -, **CPU** | 33.64/px, -, **CPU** | 9.45/px, -, **CPU** |
| Gdi.Rop.PSDPXAX.64x64 | 298.44/px, -, **CPU** | 298.13/px, -, **CPU** | 375.51/px, -, **CPU** |
| Gdi.Rop.SRCAND.64x64 | 302.17/px, -, **CPU** | 302.07/px, -, **CPU** | 374.67/px, -, **CPU** |
| Gdi.Rop.SRCCOPY.64x64 | 159.78/px, -, **CPU** | 160.37/px, -, **CPU** | 133.60/px, -, **CPU** |
| Gdi.Rop.SRCINVERT.64x64 | 302.04/px, -, **CPU** | 302.39/px, -, **CPU** | 374.16/px, -, **CPU** |
| Gdi.Rop.WHITENESS.64x64 | 32.71/px, -, **CPU** | 33.45/px, -, **CPU** | 9.34/px, -, **CPU** |
| Gdi.RoundRect.128x128 | 44.24/px, -, **CPU** | 41.59/px, -, **CPU** | 15.62/px, -, **CPU** |
| Gdi.ScrollBitBlt | 156.86/px, -, **CPU** | 156.84/px, -, **CPU** | 130.30/px, -, **CPU** |
| Gdi.ScrollDC | 156.94/px, -, **CPU** | 156.90/px, -, **CPU** | 130.58/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x576 | 31.19/px, -, **CPU** | 31.21/px, -, **CPU** | 8.28/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.512x288 | 30.96/px, -, **CPU** | 30.98/px, -, **CPU** | 8.11/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.1024x576 | 31.21/px, -, **CPU** | 31.23/px, -, **CPU** | 8.09/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.512x288 | 31.02/px, -, **CPU** | 30.97/px, -, **CPU** | 8.13/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x576 | 31.49/px, -, **CPU** | 31.50/px, -, **CPU** | 26.79/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.512x288 | 31.38/px, -, **CPU** | 31.33/px, -, **CPU** | 25.20/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.1024x576 | 31.31/px, -, **CPU** | 31.40/px, -, **CPU** | 24.77/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.512x288 | 31.37/px, -, **CPU** | 31.22/px, -, **CPU** | 24.94/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x576 | 31.02/px, -, **CPU** | 31.47/px, -, **CPU** | 21.65/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.512x288 | 30.93/px, -, **CPU** | 30.93/px, -, **CPU** | 21.32/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.1024x576 | 30.91/px, -, **CPU** | 30.90/px, -, **CPU** | 21.18/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.512x288 | 30.92/px, -, **CPU** | 31.00/px, -, **CPU** | 21.31/px, -, **CPU** |
| Gdi.StretchBltDown.128x128 | 991.65/px, -, **CPU** | 994.35/px, -, **CPU** | 970.71/px, -, **CPU** |
| Gdi.StretchBltDown.64x64 | 1,041.80/px, -, **CPU** | 1,041.12/px, -, **CPU** | 1,020.40/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.128x128 | 1,074.38/px, -, **CPU** | 993.08/px, -, **CPU** | 1,047.22/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.64x64 | 1,125.87/px, -, **CPU** | 1,045.14/px, -, **CPU** | 1,032.12/px, -, **CPU** |
| Gdi.StretchBltUp.128x128 | 173.70/px, -, **CPU** | 209.59/px, -, **CPU** | 152.97/px, -, **CPU** |
| Gdi.StretchBltUp.256x256 | 159.96/px, -, **CPU** | 159.77/px, -, **CPU** | 138.58/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.128x128 | 209.28/px, -, **CPU** | 173.44/px, -, **CPU** | 152.81/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.256x256 | 195.92/px, -, **CPU** | 160.13/px, -, **CPU** | 138.90/px, -, **CPU** |
| Gdi.StretchDIBits.128x128 | 152.64/px, -, **CPU** | 151.85/px, -, **CPU** | 130.88/px, -, **CPU** |
| Gdi.StretchDIBits.256x256 | 163.69/px, -, **CPU** | 128.18/px, -, **CPU** | 108.00/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.128x128 | 188.07/px, -, **CPU** | 187.32/px, -, **CPU** | 160.02/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.256x256 | 128.24/px, -, **CPU** | 164.23/px, -, **CPU** | 135.99/px, -, **CPU** |
| Gdi.TextOut.FixedOpaque | 185,556.45/string, -, **n/a** | 186,964.61/string, -, **n/a** | 81,687.66/string, -, **n/a** |
| Gdi.TextOut.FixedTransparent | 105,309.95/string, -, **n/a** | 108,097.90/string, -, **n/a** | 70,681.63/string, -, **n/a** |
| Gdi.TextOut.TrueTypeOpaque | 153,510.57/string, -, **n/a** | 174,417.73/string, -, **n/a** | 76,637.23/string, -, **n/a** |
| Gdi.TextOut.TrueTypeTransparent | 81,019.46/string, -, **n/a** | 101,112.32/string, -, **n/a** | 68,284.23/string, -, **n/a** |

## Opportunities: w9qi2be2-gma950-v9x on the CPU where another configuration proves hardware

None.

## CLAIMED-NOT-DELIVERED

- w9qi2be2-gma950-v9x: Dd.Blt.VidToVid.Plain.16x16 claimed, timed CPU
- w9qi2be2-gma950-v9x: Dd.Blt.VidToVid.Plain.256x256 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.Blt.VidToVid.Plain.512x512 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.Blt.VidToVid.Plain.64x64 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.BltFast.VidToVid.Plain.16x16 claimed, timed CPU
- w9qi2be2-gma950-v9x: Dd.BltFast.VidToVid.Plain.256x256 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.BltFast.VidToVid.Plain.512x512 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.BltFast.VidToVid.Plain.64x64 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.ColorFill.Vid.16x16 claimed, timed CPU
- w9qi2be2-gma950-v9x: Dd.ColorFill.Vid.256x256 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.ColorFill.Vid.512x512 claimed, timed CPU-fast
- w9qi2be2-gma950-v9x: Dd.ColorFill.Vid.64x64 claimed, timed CPU-fast

