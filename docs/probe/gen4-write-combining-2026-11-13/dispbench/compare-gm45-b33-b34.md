# GDI and DirectDraw: t5740-gm45-b33-uncached / t5740-gm45-b34-wc

Caveats, always printed: every verdict is against that run's own machine
ceilings (FbWrite/FbRead/MemFill/MemCopy/DibToDibBlt); cycles per unit
compare across machines, MB/s and microseconds do not. RASTERCAPS says
nothing about GDI acceleration on Win9x. There is no single score.

| | t5740-gm45-b33-uncached | t5740-gm45-b34-wc |
|---|---|---|
| Driver | Velocity9x Intel GMA 4500MHD (GM45) (v9xdisp.drv 0xEB15B879) | Velocity9x Intel GMA 4500MHD (GM45) (v9xdisp.drv 0x833BD305) |
| Run | 20261113-194451, build 95c470c | 20261113-195934, build 95c470c |
| Mode, environment | 1024x768x16, metal | 1024x768x16, metal |
| FbWrite / FbRead MB/s | 100.9 / 27.2 | 2,550.0 / 27.4 |

| Operation | t5740-gm45-b33-uncached cyc/unit, setup us, verdict | t5740-gm45-b34-wc cyc/unit, setup us, verdict |
|---|---|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | 38.20/px, 135.4, **CPU** | 38.20/px, 135.6, **CPU** |
| Dd.Blt.SysToSys.KeySrc.256x256 | 13.81/px, 135.4, **CPU** | 13.77/px, 135.6, **CPU** |
| Dd.Blt.SysToSys.KeySrc.512x512 | 2.92/px, 135.4, **CPU-fast** | 2.82/px, 135.6, **CPU-fast** |
| Dd.Blt.SysToSys.KeySrc.64x64 | 15.41/px, 135.4, **CPU** | 14.89/px, 135.6, **CPU** |
| Dd.Blt.SysToSys.Plain.16x16 | 10.57/px, 7.1, **CPU** | 23.75/px, 5.2, **CPU** |
| Dd.Blt.SysToSys.Plain.256x256 | 3.08/px, 7.1, **CPU-fast** | 2.77/px, 5.2, **CPU-fast** |
| Dd.Blt.SysToSys.Plain.512x512 | 2.51/px, 7.1, **CPU-fast** | 2.52/px, 5.2, **CPU-fast** |
| Dd.Blt.SysToSys.Plain.64x64 | 2.78/px, 7.1, **CPU-fast** | 3.67/px, 5.2, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.16x16 | 11.21/px, -1.7, **CPU** | 11.24/px, -2.1, **CPU** |
| Dd.Blt.SysToSys.Stretch.256x256 | 1.64/px, -1.7, **CPU-fast** | 1.68/px, -2.1, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.512x512 | 1.89/px, -1.7, **CPU-fast** | 1.95/px, -2.1, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.64x64 | 2.34/px, -1.7, **CPU-fast** | 2.31/px, -2.1, **CPU-fast** |
| Dd.Blt.SysToVid.KeySrc.16x16 | 102.44/px, 305.5, **CPU** | 80.80/px, 24.9, **CPU** |
| Dd.Blt.SysToVid.KeySrc.256x256 | 39.58/px, 305.5, **CPU** | 13.88/px, 24.9, **CPU** |
| Dd.Blt.SysToVid.KeySrc.512x512 | 15.01/px, 305.5, **HW?** | 12.54/px, 24.9, **CPU** |
| Dd.Blt.SysToVid.KeySrc.64x64 | 42.78/px, 305.5, **CPU** | 18.53/px, 24.9, **CPU** |
| Dd.Blt.SysToVid.Plain.16x16 | 55.60/px, 1.2, **CPU** | 56.04/px, 1.6, **CPU** |
| Dd.Blt.SysToVid.Plain.256x256 | 16.82/px, 1.2, **HW?** | 1.98/px, 1.6, **CPU-fast** |
| Dd.Blt.SysToVid.Plain.512x512 | 17.10/px, 1.2, **HW?** | 2.34/px, 1.6, **CPU-fast** |
| Dd.Blt.SysToVid.Plain.64x64 | 19.02/px, 1.2, **HW?** | 4.64/px, 1.6, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.16x16 | 57.60/px, 5.4, **HW?** | 55.42/px, 8.0, **CPU** |
| Dd.Blt.SysToVid.Stretch.256x256 | 16.92/px, 5.4, **HW?** | 1.60/px, 8.0, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.512x512 | 17.00/px, 5.4, **HW?** | 1.40/px, 8.0, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.64x64 | 20.91/px, 5.4, **HW?** | 3.97/px, 8.0, **CPU-fast** |
| Dd.Blt.VidToSys.KeySrc.16x16 | 197.10/px, 4.2, **CPU** | 195.84/px, 31.3, **CPU** |
| Dd.Blt.VidToSys.KeySrc.256x256 | 135.08/px, 4.2, **CPU** | 136.21/px, 31.3, **CPU** |
| Dd.Blt.VidToSys.KeySrc.512x512 | 135.36/px, 4.2, **CPU** | 134.05/px, 31.3, **CPU** |
| Dd.Blt.VidToSys.KeySrc.64x64 | 138.98/px, 4.2, **CPU** | 137.67/px, 31.3, **CPU** |
| Dd.Blt.VidToSys.Plain.16x16 | 102.89/px, 7.7, **HW?** | 122.27/px, 13.7, **HW?** |
| Dd.Blt.VidToSys.Plain.256x256 | 62.20/px, 7.7, **HW?** | 62.18/px, 13.7, **HW?** |
| Dd.Blt.VidToSys.Plain.512x512 | 61.96/px, 7.7, **HW?** | 61.62/px, 13.7, **HW?** |
| Dd.Blt.VidToSys.Plain.64x64 | 64.52/px, 7.7, **HW?** | 65.38/px, 13.7, **HW?** |
| Dd.Blt.VidToSys.Stretch.16x16 | 74.30/px, 5.9, **HW?** | 95.14/px, 5.6, **HW?** |
| Dd.Blt.VidToSys.Stretch.256x256 | 33.17/px, 5.9, **HW?** | 32.97/px, 5.6, **HW?** |
| Dd.Blt.VidToSys.Stretch.512x512 | 33.10/px, 5.9, **HW?** | 33.15/px, 5.6, **HW?** |
| Dd.Blt.VidToSys.Stretch.64x64 | 35.76/px, 5.9, **HW?** | 36.97/px, 5.6, **HW?** |
| Dd.Blt.VidToVid.KeySrc.16x16 | 252.58/px, -0.8, **CPU** | 211.99/px, 19.9, **CPU** |
| Dd.Blt.VidToVid.KeySrc.256x256 | 183.12/px, -0.8, **CPU** | 135.00/px, 19.9, **CPU** |
| Dd.Blt.VidToVid.KeySrc.512x512 | 183.87/px, -0.8, **CPU** | 134.24/px, 19.9, **CPU** |
| Dd.Blt.VidToVid.KeySrc.64x64 | 187.29/px, -0.8, **CPU** | 141.00/px, 19.9, **CPU** |
| Dd.Blt.VidToVid.Plain.16x16 | 52.47/px, 6.2, **HW?** | 50.98/px, 6.2, **CPU** |
| Dd.Blt.VidToVid.Plain.256x256 | 2.46/px, 6.2, **HW** | 2.46/px, 6.2, **CPU-fast** |
| Dd.Blt.VidToVid.Plain.512x512 | 2.32/px, 6.2, **HW** | 2.32/px, 6.2, **CPU-fast** |
| Dd.Blt.VidToVid.Plain.64x64 | 4.01/px, 6.2, **HW** | 4.01/px, 6.2, **CPU-fast** |
| Dd.Blt.VidToVid.Stretch.16x16 | 142.55/px, 18.4, **CPU** | 127.78/px, 10.7, **CPU** |
| Dd.Blt.VidToVid.Stretch.256x256 | 48.70/px, 18.4, **CPU** | 33.79/px, 10.7, **CPU** |
| Dd.Blt.VidToVid.Stretch.512x512 | 48.08/px, 18.4, **CPU** | 33.73/px, 10.7, **CPU** |
| Dd.Blt.VidToVid.Stretch.64x64 | 54.03/px, 18.4, **CPU** | 38.11/px, 10.7, **CPU** |
| Dd.BltFast.SysToSys.Plain.16x16 | 22.95/px, 8.7, **CPU** | 9.46/px, 2.2, **CPU** |
| Dd.BltFast.SysToSys.Plain.256x256 | 3.08/px, 8.7, **CPU-fast** | 2.63/px, 2.2, **CPU-fast** |
| Dd.BltFast.SysToSys.Plain.512x512 | 2.50/px, 8.7, **CPU-fast** | 2.50/px, 2.2, **CPU-fast** |
| Dd.BltFast.SysToSys.Plain.64x64 | 3.61/px, 8.7, **CPU-fast** | 2.71/px, 2.2, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.16x16 | 54.82/px, 3.0, **HW?** | 53.43/px, 2.6, **CPU** |
| Dd.BltFast.SysToVid.Plain.256x256 | 16.96/px, 3.0, **HW?** | 1.97/px, 2.6, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.512x512 | 17.05/px, 3.0, **HW?** | 2.22/px, 2.6, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.64x64 | 18.82/px, 3.0, **HW?** | 4.55/px, 2.6, **CPU-fast** |
| Dd.BltFast.VidToSys.Plain.16x16 | 11.06/px, 2.4, **HW?** | 11.16/px, 1.5, **HW?** |
| Dd.BltFast.VidToSys.Plain.256x256 | 2.68/px, 2.4, **HW?** | 2.73/px, 1.5, **HW?** |
| Dd.BltFast.VidToSys.Plain.512x512 | 2.54/px, 2.4, **HW?** | 2.67/px, 1.5, **HW?** |
| Dd.BltFast.VidToSys.Plain.64x64 | 2.92/px, 2.4, **HW?** | 2.93/px, 1.5, **HW?** |
| Dd.BltFast.VidToVid.Plain.16x16 | 52.00/px, 6.0, **HW?** | 50.14/px, 6.1, **CPU** |
| Dd.BltFast.VidToVid.Plain.256x256 | 2.46/px, 6.0, **HW** | 2.46/px, 6.1, **CPU-fast** |
| Dd.BltFast.VidToVid.Plain.512x512 | 2.34/px, 6.0, **HW** | 2.32/px, 6.1, **CPU-fast** |
| Dd.BltFast.VidToVid.Plain.64x64 | 4.02/px, 6.0, **HW** | 4.02/px, 6.1, **CPU-fast** |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | 98.35/px, 306.5, **CPU** | 60.46/px, 21.4, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | 39.80/px, 306.5, **CPU** | 13.91/px, 21.4, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | 15.18/px, 306.5, **HW** | 12.65/px, 21.4, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | 43.81/px, 306.5, **CPU** | 17.19/px, 21.4, **CPU** |
| Dd.ColorFill.Sys.16x16 | 8.98/px, 1.4, **CPU** | 8.81/px, 1.6, **CPU** |
| Dd.ColorFill.Sys.256x256 | 1.46/px, 1.4, **CPU-fast** | 1.49/px, 1.6, **CPU-fast** |
| Dd.ColorFill.Sys.512x512 | 1.43/px, 1.4, **CPU-fast** | 1.45/px, 1.6, **CPU-fast** |
| Dd.ColorFill.Sys.64x64 | 2.12/px, 1.4, **CPU-fast** | 2.14/px, 1.6, **CPU-fast** |
| Dd.ColorFill.Vid.16x16 | 50.54/px, 5.2, **HW?** | 48.86/px, 5.0, **CPU** |
| Dd.ColorFill.Vid.256x256 | 1.76/px, 5.2, **HW** | 1.76/px, 5.0, **CPU-fast** |
| Dd.ColorFill.Vid.512x512 | 1.67/px, 5.2, **HW** | 1.67/px, 5.0, **CPU-fast** |
| Dd.ColorFill.Vid.64x64 | 2.92/px, 5.2, **HW** | 2.83/px, 5.0, **CPU-fast** |
| Dd.Lock.Back | 9,478.96/lock, -, **n/a** | 9,650.09/lock, -, **n/a** |
| Dd.Lock.OffscreenVid | 13,026.50/lock, -, **n/a** | 9,644.69/lock, -, **n/a** |
| Dd.Lock.Primary | 9,477.60/lock, -, **n/a** | 9,513.99/lock, -, **n/a** |
| Dd.Lock.Sys | 2,334.40/lock, -, **n/a** | 2,344.85/lock, -, **n/a** |
| Dd.LockAfterBlt.VidToVid | 4,394,473.00/lock, -, **n/a** | 636,201.19/lock, -, **n/a** |
| Gdi.Align.Dx0.64x64 | 184.95/px, -, **CPU** | 131.63/px, -, **CPU** |
| Gdi.Align.Dx1.64x64 | 201.48/px, -, **CPU** | 149.50/px, -, **CPU** |
| Gdi.Align.Dx2.64x64 | 183.91/px, -, **CPU** | 133.50/px, -, **CPU** |
| Gdi.Align.Dx3.64x64 | 205.19/px, -, **CPU** | 152.53/px, -, **CPU** |
| Gdi.Align.Dx4.64x64 | 186.04/px, -, **CPU** | 132.99/px, -, **CPU** |
| Gdi.Align.Dx8.64x64 | 185.81/px, -, **CPU** | 131.90/px, -, **CPU** |
| Gdi.BitBltBitmapToScreen.1024x768 | 32.27/px, 10.9, **CPU** | 10.05/px, 7.5, **CPU** |
| Gdi.BitBltBitmapToScreen.128x128 | 33.42/px, 10.9, **CPU** | 11.23/px, 7.5, **CPU** |
| Gdi.BitBltBitmapToScreen.16x16 | 86.27/px, 10.9, **CPU** | 58.24/px, 7.5, **CPU** |
| Gdi.BitBltBitmapToScreen.256x256 | 33.25/px, 10.9, **CPU** | 10.41/px, 7.5, **CPU** |
| Gdi.BitBltBitmapToScreen.32x32 | 46.66/px, 10.9, **CPU** | 20.33/px, 7.5, **CPU** |
| Gdi.BitBltBitmapToScreen.512x512 | 32.55/px, 10.9, **CPU** | 10.23/px, 7.5, **CPU** |
| Gdi.BitBltBitmapToScreen.64x64 | 36.01/px, 10.9, **CPU** | 12.83/px, 7.5, **CPU** |
| Gdi.BitBltBitmapToScreen.8x8 | 191.95/px, 10.9, **noisy** | 186.28/px, 7.5, **CPU** |
| Gdi.BitBltDibToScreen.1024x768 | 32.51/px, 6.8, **CPU** | 8.40/px, 14.9, **CPU** |
| Gdi.BitBltDibToScreen.128x128 | 33.85/px, 6.8, **CPU** | 9.50/px, 14.9, **CPU** |
| Gdi.BitBltDibToScreen.16x16 | 103.16/px, 6.8, **CPU** | 90.01/px, 14.9, **CPU** |
| Gdi.BitBltDibToScreen.256x256 | 33.02/px, 6.8, **CPU** | 8.43/px, 14.9, **CPU** |
| Gdi.BitBltDibToScreen.32x32 | 50.88/px, 6.8, **CPU** | 32.54/px, 14.9, **CPU** |
| Gdi.BitBltDibToScreen.512x512 | 33.37/px, 6.8, **CPU** | 8.30/px, 14.9, **CPU** |
| Gdi.BitBltDibToScreen.64x64 | 37.14/px, 6.8, **CPU** | 19.18/px, 14.9, **CPU** |
| Gdi.BitBltDibToScreen.8x8 | 303.83/px, 6.8, **CPU** | 300.43/px, 14.9, **CPU** |
| Gdi.BitBltScreenToBitmap.1024x768 | 128.75/px, 8.3, **CPU** | 133.85/px, 10.3, **CPU** |
| Gdi.BitBltScreenToBitmap.128x128 | 131.47/px, 8.3, **CPU** | 135.17/px, 10.3, **CPU** |
| Gdi.BitBltScreenToBitmap.16x16 | 168.56/px, 8.3, **CPU** | 175.66/px, 10.3, **CPU** |
| Gdi.BitBltScreenToBitmap.256x256 | 127.30/px, 8.3, **CPU** | 132.91/px, 10.3, **CPU** |
| Gdi.BitBltScreenToBitmap.32x32 | 139.26/px, 8.3, **CPU** | 143.58/px, 10.3, **CPU** |
| Gdi.BitBltScreenToBitmap.512x512 | 127.86/px, 8.3, **CPU** | 132.73/px, 10.3, **CPU** |
| Gdi.BitBltScreenToBitmap.64x64 | 130.87/px, 8.3, **CPU** | 136.37/px, 10.3, **CPU** |
| Gdi.BitBltScreenToBitmap.8x8 | 290.11/px, 8.3, **CPU** | 288.74/px, 10.3, **CPU** |
| Gdi.BitBltScreenToScreen.128x128 | 181.36/px, 2.9, **CPU** | 130.51/px, 10.5, **CPU** |
| Gdi.BitBltScreenToScreen.16x16 | 223.90/px, 2.9, **CPU** | 184.90/px, 10.5, **CPU** |
| Gdi.BitBltScreenToScreen.256x256 | 183.08/px, 2.9, **CPU** | 129.30/px, 10.5, **CPU** |
| Gdi.BitBltScreenToScreen.32x32 | 192.31/px, 2.9, **CPU** | 145.59/px, 10.5, **CPU** |
| Gdi.BitBltScreenToScreen.64x64 | 185.02/px, 2.9, **CPU** | 133.85/px, 10.5, **CPU** |
| Gdi.BitBltScreenToScreen.8x8 | 388.97/px, 2.9, **CPU** | 353.13/px, 10.5, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.128x128 | 188.22/px, -9.6, **CPU** | 130.99/px, 13.3, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.16x16 | 241.75/px, -9.6, **CPU** | 186.88/px, 13.3, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.256x256 | 186.01/px, -9.6, **CPU** | 129.39/px, 13.3, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.32x32 | 200.85/px, -9.6, **CPU** | 142.72/px, 13.3, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.512x512 | 188.75/px, -9.6, **CPU** | 128.91/px, 13.3, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.64x64 | 191.20/px, -9.6, **CPU** | 135.01/px, 13.3, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.8x8 | 346.46/px, -9.6, **CPU** | 304.64/px, 13.3, **CPU** |
| Gdi.Ellipse.128x128 | 67.53/px, -, **CPU** | 49.25/px, -, **CPU** |
| Gdi.ExtTextOut.FixedOpaque | 195,019.19/string, -, **n/a** | 94,394.76/string, -, **n/a** |
| Gdi.ExtTextOut.FixedTransparent | 112,301.57/string, -, **n/a** | 84,392.12/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeOpaque | 161,520.90/string, -, **n/a** | 81,185.20/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeTransparent | 88,731.40/string, -, **n/a** | 76,474.56/string, -, **n/a** |
| Gdi.FillRect.1024x768 | 31.72/px, 8.4, **CPU** | 1.88/px, 9.8, **CPU-fast** |
| Gdi.FillRect.128x128 | 33.23/px, 8.4, **CPU** | 3.01/px, 9.8, **CPU-fast** |
| Gdi.FillRect.16x16 | 83.12/px, 8.4, **CPU** | 68.36/px, 9.8, **CPU** |
| Gdi.FillRect.256x256 | 32.26/px, 8.4, **CPU** | 2.19/px, 9.8, **CPU-fast** |
| Gdi.FillRect.32x32 | 44.82/px, 8.4, **CPU** | 15.94/px, 9.8, **CPU** |
| Gdi.FillRect.512x512 | 32.14/px, 8.4, **CPU** | 1.96/px, 9.8, **CPU-fast** |
| Gdi.FillRect.64x64 | 35.52/px, 8.4, **CPU** | 5.68/px, 9.8, **CPU** |
| Gdi.FillRect.8x8 | 237.00/px, 8.4, **CPU** | 229.37/px, 9.8, **CPU** |
| Gdi.LineDiagonal30 | 14,737.12/line, -, **n/a** | 24,614.25/line, -, **n/a** |
| Gdi.LineHorizontal | 15,419.33/line, -, **n/a** | 9,844.13/line, -, **n/a** |
| Gdi.LineVertical | 16,105.83/line, -, **n/a** | 35,629.57/line, -, **n/a** |
| Gdi.PatBltHatch.1024x768 | 31.67/px, 12.6, **CPU** | 7.09/px, 8.6, **CPU** |
| Gdi.PatBltHatch.128x128 | 33.69/px, 12.6, **CPU** | 8.04/px, 8.6, **CPU** |
| Gdi.PatBltHatch.16x16 | 86.89/px, 12.6, **CPU** | 60.26/px, 8.6, **CPU** |
| Gdi.PatBltHatch.256x256 | 33.03/px, 12.6, **CPU** | 7.35/px, 8.6, **CPU** |
| Gdi.PatBltHatch.32x32 | 44.31/px, 12.6, **CPU** | 18.11/px, 8.6, **CPU** |
| Gdi.PatBltHatch.512x512 | 32.25/px, 12.6, **CPU** | 7.05/px, 8.6, **CPU** |
| Gdi.PatBltHatch.64x64 | 35.89/px, 12.6, **CPU** | 9.84/px, 8.6, **CPU** |
| Gdi.PatBltHatch.8x8 | 251.54/px, 12.6, **CPU** | 191.86/px, 8.6, **CPU** |
| Gdi.PatBltPattern.1024x768 | 31.85/px, 11.1, **CPU** | 7.12/px, 7.8, **CPU** |
| Gdi.PatBltPattern.128x128 | 33.43/px, 11.1, **CPU** | 8.21/px, 7.8, **CPU** |
| Gdi.PatBltPattern.16x16 | 88.19/px, 11.1, **CPU** | 59.01/px, 7.8, **CPU** |
| Gdi.PatBltPattern.256x256 | 32.91/px, 11.1, **CPU** | 7.44/px, 7.8, **CPU** |
| Gdi.PatBltPattern.32x32 | 46.38/px, 11.1, **CPU** | 18.30/px, 7.8, **CPU** |
| Gdi.PatBltPattern.512x512 | 32.35/px, 11.1, **CPU** | 7.27/px, 7.8, **CPU** |
| Gdi.PatBltPattern.64x64 | 36.02/px, 11.1, **CPU** | 10.08/px, 7.8, **CPU** |
| Gdi.PatBltPattern.8x8 | 248.78/px, 11.1, **CPU** | 192.13/px, 7.8, **CPU** |
| Gdi.PatBltSolid.1024x768 | 32.15/px, 2.1, **CPU** | 1.84/px, 6.6, **CPU-fast** |
| Gdi.PatBltSolid.128x128 | 32.59/px, 2.1, **CPU** | 2.58/px, 6.6, **CPU-fast** |
| Gdi.PatBltSolid.16x16 | 59.30/px, 2.1, **CPU** | 46.47/px, 6.6, **CPU** |
| Gdi.PatBltSolid.256x256 | 32.50/px, 2.1, **CPU** | 2.10/px, 6.6, **CPU-fast** |
| Gdi.PatBltSolid.32x32 | 39.08/px, 2.1, **CPU** | 9.81/px, 6.6, **CPU** |
| Gdi.PatBltSolid.512x512 | 32.64/px, 2.1, **CPU** | 1.91/px, 6.6, **CPU-fast** |
| Gdi.PatBltSolid.64x64 | 34.00/px, 2.1, **CPU** | 4.12/px, 6.6, **CPU-fast** |
| Gdi.PatBltSolid.8x8 | 138.89/px, 2.1, **CPU** | 192.60/px, 6.6, **CPU** |
| Gdi.Polygon.128x128 | 48.60/px, -, **CPU** | 34.35/px, -, **CPU** |
| Gdi.Polyline | 7,015.88/line, -, **n/a** | 1,608.89/line, -, **n/a** |
| Gdi.Rectangle.128x128 | 35.40/px, -, **CPU** | 9.64/px, -, **CPU** |
| Gdi.Rop.BLACKNESS.64x64 | 34.34/px, -, **CPU** | 11.37/px, -, **CPU** |
| Gdi.Rop.NOTSRCCOPY.64x64 | 192.99/px, -, **CPU** | 136.90/px, -, **CPU** |
| Gdi.Rop.PATCOPY.64x64 | 34.83/px, -, **CPU** | 12.02/px, -, **CPU** |
| Gdi.Rop.PSDPXAX.64x64 | 322.32/px, -, **CPU** | 349.02/px, -, **CPU** |
| Gdi.Rop.SRCAND.64x64 | 321.57/px, -, **CPU** | 349.46/px, -, **CPU** |
| Gdi.Rop.SRCCOPY.64x64 | 183.90/px, -, **CPU** | 131.63/px, -, **CPU** |
| Gdi.Rop.SRCINVERT.64x64 | 318.81/px, -, **CPU** | 348.71/px, -, **CPU** |
| Gdi.Rop.WHITENESS.64x64 | 34.39/px, -, **CPU** | 11.10/px, -, **CPU** |
| Gdi.RoundRect.128x128 | 45.02/px, -, **CPU** | 21.33/px, -, **CPU** |
| Gdi.ScrollBitBlt | 181.78/px, -, **CPU** | 131.15/px, -, **CPU** |
| Gdi.ScrollDC | 179.31/px, -, **CPU** | 129.88/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | 32.68/px, -, **CPU** | 8.87/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | 34.08/px, -, **CPU** | 8.82/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | 32.56/px, -, **CPU** | 8.52/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | 33.84/px, -, **CPU** | 8.53/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | 33.41/px, -, **CPU** | 26.10/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | 33.66/px, -, **CPU** | 27.07/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | 32.80/px, -, **CPU** | 25.67/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | 33.27/px, -, **CPU** | 26.90/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | 33.04/px, -, **CPU** | 22.52/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | 32.71/px, -, **CPU** | 22.26/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | 32.79/px, -, **CPU** | 22.05/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | 32.70/px, -, **CPU** | 21.99/px, -, **CPU** |
| Gdi.StretchBltDown.128x128 | 1,007.42/px, -, **CPU** | 1,042.40/px, -, **CPU** |
| Gdi.StretchBltDown.64x64 | 1,057.45/px, -, **CPU** | 1,118.73/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.128x128 | 1,003.45/px, -, **CPU** | 1,047.84/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.64x64 | 1,070.49/px, -, **CPU** | 1,102.25/px, -, **CPU** |
| Gdi.StretchBltUp.128x128 | 181.88/px, -, **CPU** | 188.46/px, -, **CPU** |
| Gdi.StretchBltUp.256x256 | 168.05/px, -, **noisy** | 174.60/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.128x128 | 179.57/px, -, **CPU** | 190.00/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.256x256 | 168.19/px, -, **CPU** | 173.55/px, -, **CPU** |
| Gdi.StretchDIBits.128x128 | 161.93/px, -, **CPU** | 166.88/px, -, **CPU** |
| Gdi.StretchDIBits.256x256 | 174.19/px, -, **CPU** | 144.54/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.128x128 | 162.44/px, -, **CPU** | 137.39/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.256x256 | 135.90/px, -, **CPU** | 111.19/px, -, **CPU** |
| Gdi.TextOut.FixedOpaque | 195,569.06/string, -, **n/a** | 92,679.52/string, -, **n/a** |
| Gdi.TextOut.FixedTransparent | 110,404.78/string, -, **n/a** | 83,389.17/string, -, **n/a** |
| Gdi.TextOut.TrueTypeOpaque | 161,223.78/string, -, **n/a** | 83,321.30/string, -, **n/a** |
| Gdi.TextOut.TrueTypeTransparent | 88,050.46/string, -, **n/a** | 75,198.86/string, -, **n/a** |

## Opportunities: t5740-gm45-b33-uncached on the CPU where another configuration proves hardware

None.

## CLAIMED-NOT-DELIVERED

- t5740-gm45-b34-wc: Dd.Blt.VidToVid.Plain.16x16 claimed, timed CPU
- t5740-gm45-b34-wc: Dd.Blt.VidToVid.Plain.256x256 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.Blt.VidToVid.Plain.512x512 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.Blt.VidToVid.Plain.64x64 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.BltFast.VidToVid.Plain.16x16 claimed, timed CPU
- t5740-gm45-b34-wc: Dd.BltFast.VidToVid.Plain.256x256 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.BltFast.VidToVid.Plain.512x512 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.BltFast.VidToVid.Plain.64x64 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.ColorFill.Vid.16x16 claimed, timed CPU
- t5740-gm45-b34-wc: Dd.ColorFill.Vid.256x256 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.ColorFill.Vid.512x512 claimed, timed CPU-fast
- t5740-gm45-b34-wc: Dd.ColorFill.Vid.64x64 claimed, timed CPU-fast
