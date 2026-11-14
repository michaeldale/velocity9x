# GDI and DirectDraw: tommohp-gm965-b50-uncached / tommohp-gm965-b51-wc

Caveats, always printed: every verdict is against that run's own machine
ceilings (FbWrite/FbRead/MemFill/MemCopy/DibToDibBlt); cycles per unit
compare across machines, MB/s and microseconds do not. RASTERCAPS says
nothing about GDI acceleration on Win9x. There is no single score.

| | tommohp-gm965-b50-uncached | tommohp-gm965-b51-wc |
|---|---|---|
| Driver | Velocity9x Intel GMA X3100 (GM965) (v9xdisp.drv 0xFF27C3E2) | Velocity9x Intel GMA X3100 (GM965) (v9xdisp.drv 0x6B32D5CA) |
| Run | 20261113-193224, build 95c470c | 20261113-193925, build 95c470c |
| Mode, environment | 1280x800x16, metal | 1280x800x16, metal |
| FbWrite / FbRead MB/s | 130.2 / 31.3 | 3,310.3 / 217.4 |

| Operation | tommohp-gm965-b50-uncached cyc/unit, setup us, verdict | tommohp-gm965-b51-wc cyc/unit, setup us, verdict |
|---|---|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | 10.95/px, 55.9, **CPU** | 10.96/px, 55.9, **CPU** |
| Dd.Blt.SysToSys.KeySrc.256x256 | 6.50/px, 55.9, **CPU** | 6.50/px, 55.9, **CPU** |
| Dd.Blt.SysToSys.KeySrc.512x512 | 1.03/px, 55.9, **CPU-fast** | 1.03/px, 55.9, **CPU-fast** |
| Dd.Blt.SysToSys.KeySrc.64x64 | 6.88/px, 55.9, **CPU** | 6.87/px, 55.9, **CPU** |
| Dd.Blt.SysToSys.Plain.16x16 | 3.93/px, 0.7, **CPU-fast** | 4.02/px, 0.7, **CPU-fast** |
| Dd.Blt.SysToSys.Plain.256x256 | 0.69/px, 0.7, **CPU-fast** | 0.68/px, 0.7, **CPU-fast** |
| Dd.Blt.SysToSys.Plain.512x512 | 0.66/px, 0.7, **CPU-fast** | 0.66/px, 0.7, **CPU-fast** |
| Dd.Blt.SysToSys.Plain.64x64 | 1.02/px, 0.7, **CPU-fast** | 1.03/px, 0.7, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.16x16 | 4.66/px, 1.4, **CPU** | 4.64/px, 1.4, **CPU** |
| Dd.Blt.SysToSys.Stretch.256x256 | 0.57/px, 1.4, **CPU-fast** | 0.57/px, 1.4, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.512x512 | 0.49/px, 1.4, **CPU-fast** | 0.49/px, 1.4, **CPU-fast** |
| Dd.Blt.SysToSys.Stretch.64x64 | 1.01/px, 1.4, **CPU-fast** | 1.00/px, 1.4, **CPU-fast** |
| Dd.Blt.SysToVid.KeySrc.16x16 | 41.57/px, 142.2, **HW?** | 24.53/px, 7.9, **CPU** |
| Dd.Blt.SysToVid.KeySrc.256x256 | 27.34/px, 142.2, **HW?** | 8.90/px, 7.9, **CPU** |
| Dd.Blt.SysToVid.KeySrc.512x512 | 13.44/px, 142.2, **HW?** | 8.31/px, 7.9, **CPU** |
| Dd.Blt.SysToVid.KeySrc.64x64 | 28.27/px, 142.2, **HW?** | 10.28/px, 7.9, **CPU** |
| Dd.Blt.SysToVid.Plain.16x16 | 28.95/px, 1.9, **CPU** | 22.57/px, 2.8, **CPU** |
| Dd.Blt.SysToVid.Plain.256x256 | 15.40/px, 1.9, **HW?** | 1.33/px, 2.8, **CPU-fast** |
| Dd.Blt.SysToVid.Plain.512x512 | 15.35/px, 1.9, **HW?** | 1.22/px, 2.8, **CPU-fast** |
| Dd.Blt.SysToVid.Plain.64x64 | 16.22/px, 1.9, **HW?** | 1.98/px, 2.8, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.16x16 | 29.89/px, 2.0, **HW?** | 23.27/px, 2.8, **CPU** |
| Dd.Blt.SysToVid.Stretch.256x256 | 15.42/px, 2.0, **HW?** | 1.32/px, 2.8, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.512x512 | 15.36/px, 2.0, **HW?** | 1.21/px, 2.8, **CPU-fast** |
| Dd.Blt.SysToVid.Stretch.64x64 | 16.32/px, 2.0, **HW?** | 1.98/px, 2.8, **CPU-fast** |
| Dd.Blt.VidToSys.KeySrc.16x16 | 146.97/px, 6.2, **CPU** | 147.28/px, 25.7, **CPU** |
| Dd.Blt.VidToSys.KeySrc.256x256 | 132.38/px, 6.2, **CPU** | 124.10/px, 25.7, **CPU** |
| Dd.Blt.VidToSys.KeySrc.512x512 | 131.96/px, 6.2, **CPU** | 122.30/px, 25.7, **CPU** |
| Dd.Blt.VidToSys.KeySrc.64x64 | 133.95/px, 6.2, **CPU** | 131.86/px, 25.7, **CPU** |
| Dd.Blt.VidToSys.Plain.16x16 | 78.11/px, 2.8, **HW?** | 47.91/px, -17.2, **CPU** |
| Dd.Blt.VidToSys.Plain.256x256 | 65.74/px, 2.8, **HW?** | 55.43/px, -17.2, **CPU** |
| Dd.Blt.VidToSys.Plain.512x512 | 65.60/px, 2.8, **HW?** | 56.91/px, -17.2, **CPU** |
| Dd.Blt.VidToSys.Plain.64x64 | 66.63/px, 2.8, **HW?** | 53.18/px, -17.2, **CPU** |
| Dd.Blt.VidToSys.Stretch.16x16 | 47.25/px, 1.8, **HW?** | 31.10/px, 22.1, **HW?** |
| Dd.Blt.VidToSys.Stretch.256x256 | 33.72/px, 1.8, **HW?** | 17.93/px, 22.1, **HW?** |
| Dd.Blt.VidToSys.Stretch.512x512 | 33.69/px, 1.8, **HW?** | 16.59/px, 22.1, **HW?** |
| Dd.Blt.VidToSys.Stretch.64x64 | 34.66/px, 1.8, **HW?** | 27.62/px, 22.1, **HW?** |
| Dd.Blt.VidToVid.KeySrc.16x16 | 229.39/px, 14.5, **CPU** | 161.85/px, 24.9, **CPU** |
| Dd.Blt.VidToVid.KeySrc.256x256 | 203.58/px, 14.5, **CPU** | 128.70/px, 24.9, **CPU** |
| Dd.Blt.VidToVid.KeySrc.512x512 | 202.52/px, 14.5, **CPU** | 127.05/px, 24.9, **CPU** |
| Dd.Blt.VidToVid.KeySrc.64x64 | 206.64/px, 14.5, **CPU** | 136.73/px, 24.9, **CPU** |
| Dd.Blt.VidToVid.Plain.16x16 | 26.90/px, 2.5, **HW** | 25.28/px, 2.6, **CPU** |
| Dd.Blt.VidToVid.Plain.256x256 | 3.65/px, 2.5, **HW** | 3.66/px, 2.6, **CPU-fast** |
| Dd.Blt.VidToVid.Plain.512x512 | 3.57/px, 2.5, **HW** | 3.57/px, 2.6, **CPU-fast** |
| Dd.Blt.VidToVid.Plain.64x64 | 4.29/px, 2.5, **HW** | 4.29/px, 2.6, **CPU-fast** |
| Dd.Blt.VidToVid.Stretch.16x16 | 79.97/px, 4.5, **CPU** | 53.40/px, 32.6, **CPU** |
| Dd.Blt.VidToVid.Stretch.256x256 | 54.96/px, 4.5, **CPU** | 19.33/px, 32.6, **CPU** |
| Dd.Blt.VidToVid.Stretch.512x512 | 54.75/px, 4.5, **CPU** | 17.01/px, 32.6, **CPU** |
| Dd.Blt.VidToVid.Stretch.64x64 | 56.21/px, 4.5, **CPU** | 28.49/px, 32.6, **CPU** |
| Dd.BltFast.SysToSys.Plain.16x16 | 3.27/px, 0.5, **CPU-fast** | 3.25/px, 0.6, **CPU-fast** |
| Dd.BltFast.SysToSys.Plain.256x256 | 0.67/px, 0.5, **CPU-fast** | 0.67/px, 0.6, **CPU-fast** |
| Dd.BltFast.SysToSys.Plain.512x512 | 0.66/px, 0.5, **CPU-fast** | 0.66/px, 0.6, **CPU-fast** |
| Dd.BltFast.SysToSys.Plain.64x64 | 0.97/px, 0.5, **CPU-fast** | 0.98/px, 0.6, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.16x16 | 28.38/px, 1.7, **CPU** | 21.93/px, 2.5, **CPU** |
| Dd.BltFast.SysToVid.Plain.256x256 | 15.40/px, 1.7, **HW?** | 1.31/px, 2.5, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.512x512 | 15.35/px, 1.7, **HW?** | 1.22/px, 2.5, **CPU-fast** |
| Dd.BltFast.SysToVid.Plain.64x64 | 16.17/px, 1.7, **HW?** | 1.93/px, 2.5, **CPU-fast** |
| Dd.BltFast.VidToSys.Plain.16x16 | 3.38/px, 0.9, **HW?** | 3.40/px, 0.9, **HW?** |
| Dd.BltFast.VidToSys.Plain.256x256 | 0.69/px, 0.9, **HW?** | 0.69/px, 0.9, **HW?** |
| Dd.BltFast.VidToSys.Plain.512x512 | 0.64/px, 0.9, **HW?** | 0.64/px, 0.9, **HW?** |
| Dd.BltFast.VidToSys.Plain.64x64 | 1.00/px, 0.9, **HW?** | 1.00/px, 0.9, **HW?** |
| Dd.BltFast.VidToVid.Plain.16x16 | 26.20/px, 2.5, **HW** | 25.35/px, 2.5, **CPU** |
| Dd.BltFast.VidToVid.Plain.256x256 | 3.65/px, 2.5, **HW** | 3.67/px, 2.5, **CPU-fast** |
| Dd.BltFast.VidToVid.Plain.512x512 | 3.57/px, 2.5, **HW** | 3.58/px, 2.5, **CPU-fast** |
| Dd.BltFast.VidToVid.Plain.64x64 | 4.28/px, 2.5, **HW** | 4.29/px, 2.5, **CPU-fast** |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | 40.78/px, 141.9, **HW?** | 23.93/px, 13.9, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | 27.32/px, 141.9, **HW** | 9.48/px, 13.9, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | 13.44/px, 141.9, **HW** | 8.30/px, 13.9, **CPU** |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | 28.24/px, 141.9, **HW?** | 11.01/px, 13.9, **CPU** |
| Dd.ColorFill.Sys.16x16 | 3.39/px, 1.5, **CPU** | 3.38/px, 1.5, **CPU** |
| Dd.ColorFill.Sys.256x256 | 0.52/px, 1.5, **CPU-fast** | 0.52/px, 1.5, **CPU-fast** |
| Dd.ColorFill.Sys.512x512 | 0.41/px, 1.5, **CPU-fast** | 0.41/px, 1.5, **CPU-fast** |
| Dd.ColorFill.Sys.64x64 | 0.77/px, 1.5, **CPU-fast** | 0.77/px, 1.5, **CPU-fast** |
| Dd.ColorFill.Vid.16x16 | 25.00/px, 2.8, **HW** | 21.46/px, 2.6, **CPU** |
| Dd.ColorFill.Vid.256x256 | 1.62/px, 2.8, **HW** | 1.62/px, 2.6, **CPU** |
| Dd.ColorFill.Vid.512x512 | 1.53/px, 2.8, **HW** | 1.53/px, 2.6, **CPU** |
| Dd.ColorFill.Vid.64x64 | 2.51/px, 2.8, **HW** | 2.52/px, 2.6, **CPU** |
| Dd.Lock.Back | 3,283.69/lock, -, **n/a** | 3,333.42/lock, -, **n/a** |
| Dd.Lock.OffscreenVid | 3,287.11/lock, -, **n/a** | 3,338.24/lock, -, **n/a** |
| Dd.Lock.Primary | 3,333.41/lock, -, **n/a** | 3,332.85/lock, -, **n/a** |
| Dd.Lock.Sys | 791.77/lock, -, **n/a** | 785.99/lock, -, **n/a** |
| Dd.LockAfterBlt.VidToVid | 4,027,821.43/lock, -, **n/a** | 324,109.52/lock, -, **n/a** |
| Gdi.Align.Dx0.64x64 | 205.96/px, -, **HW?** | 65.67/px, -, **CPU** |
| Gdi.Align.Dx1.64x64 | 226.28/px, -, **HW?** | 80.15/px, -, **CPU** |
| Gdi.Align.Dx2.64x64 | 205.85/px, -, **HW?** | 68.64/px, -, **CPU** |
| Gdi.Align.Dx3.64x64 | 226.30/px, -, **HW?** | 80.75/px, -, **CPU** |
| Gdi.Align.Dx4.64x64 | 205.79/px, -, **HW?** | 67.56/px, -, **CPU** |
| Gdi.Align.Dx8.64x64 | 205.86/px, -, **HW?** | 68.48/px, -, **CPU** |
| Gdi.BitBltBitmapToScreen.1280x800 | 30.79/px, 1.5, **CPU** | 1.51/px, 1.8, **CPU-fast** |
| Gdi.BitBltBitmapToScreen.128x128 | 30.87/px, 1.5, **CPU** | 1.79/px, 1.8, **CPU** |
| Gdi.BitBltBitmapToScreen.16x16 | 40.40/px, 1.5, **CPU** | 17.96/px, 1.8, **CPU** |
| Gdi.BitBltBitmapToScreen.256x256 | 30.85/px, 1.5, **CPU** | 1.55/px, 1.8, **CPU** |
| Gdi.BitBltBitmapToScreen.32x32 | 33.08/px, 1.5, **CPU** | 3.71/px, 1.8, **CPU** |
| Gdi.BitBltBitmapToScreen.512x512 | 30.75/px, 1.5, **CPU** | 1.46/px, 1.8, **CPU-fast** |
| Gdi.BitBltBitmapToScreen.64x64 | 31.27/px, 1.5, **CPU** | 1.71/px, 1.8, **CPU** |
| Gdi.BitBltBitmapToScreen.8x8 | 69.62/px, 1.5, **CPU** | 49.51/px, 1.8, **CPU** |
| Gdi.BitBltDibToScreen.1280x800 | 30.77/px, 2.8, **CPU** | 1.86/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.128x128 | 31.06/px, 2.8, **CPU** | 2.20/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.16x16 | 51.21/px, 2.8, **CPU** | 28.25/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.256x256 | 30.78/px, 2.8, **CPU** | 1.90/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.32x32 | 35.79/px, 2.8, **CPU** | 6.84/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.512x512 | 30.70/px, 2.8, **CPU** | 1.77/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.64x64 | 31.94/px, 2.8, **CPU** | 2.85/px, 3.2, **CPU** |
| Gdi.BitBltDibToScreen.8x8 | 111.23/px, 2.8, **CPU** | 92.19/px, 3.2, **CPU** |
| Gdi.BitBltScreenToBitmap.1280x800 | 129.89/px, 4.8, **CPU** | 87.33/px, 91.5, **CPU** |
| Gdi.BitBltScreenToBitmap.128x128 | 130.56/px, 4.8, **CPU** | 59.21/px, 91.5, **CPU** |
| Gdi.BitBltScreenToBitmap.16x16 | 139.45/px, 4.8, **CPU** | 77.96/px, 91.5, **CPU** |
| Gdi.BitBltScreenToBitmap.256x256 | 130.11/px, 4.8, **CPU** | 24.15/px, 91.5, **CPU** |
| Gdi.BitBltScreenToBitmap.32x32 | 130.31/px, 4.8, **CPU** | 70.29/px, 91.5, **CPU** |
| Gdi.BitBltScreenToBitmap.512x512 | 129.40/px, 4.8, **CPU** | 20.86/px, 91.5, **CPU** |
| Gdi.BitBltScreenToBitmap.64x64 | 128.29/px, 4.8, **CPU** | 64.60/px, 91.5, **CPU** |
| Gdi.BitBltScreenToBitmap.8x8 | 171.20/px, 4.8, **CPU** | 106.38/px, 91.5, **CPU** |
| Gdi.BitBltScreenToScreen.128x128 | 206.34/px, 3.0, **CPU** | 61.34/px, 97.2, **CPU** |
| Gdi.BitBltScreenToScreen.16x16 | 214.61/px, 3.0, **CPU** | 83.29/px, 97.2, **CPU** |
| Gdi.BitBltScreenToScreen.256x256 | 205.69/px, 3.0, **CPU** | 25.93/px, 97.2, **CPU** |
| Gdi.BitBltScreenToScreen.32x32 | 207.58/px, 3.0, **CPU** | 75.02/px, 97.2, **CPU** |
| Gdi.BitBltScreenToScreen.512x512 | 205.47/px, 3.0, **CPU** | 21.99/px, 97.2, **CPU** |
| Gdi.BitBltScreenToScreen.64x64 | 205.95/px, 3.0, **CPU** | 68.52/px, 97.2, **CPU** |
| Gdi.BitBltScreenToScreen.8x8 | 243.62/px, 3.0, **CPU** | 114.50/px, 97.2, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.128x128 | 207.53/px, 6.4, **CPU** | 58.10/px, 8.1, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.16x16 | 229.52/px, 6.4, **CPU** | 70.23/px, 8.1, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.256x256 | 206.79/px, 6.4, **CPU** | 56.68/px, 8.1, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.32x32 | 212.23/px, 6.4, **CPU** | 73.53/px, 8.1, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.512x512 | 206.18/px, 6.4, **CPU** | 56.33/px, 8.1, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.64x64 | 206.92/px, 6.4, **CPU** | 61.13/px, 8.1, **CPU** |
| Gdi.BitBltScreenToScreenOverlap.8x8 | 276.93/px, 6.4, **CPU** | 114.40/px, 8.1, **CPU** |
| Gdi.Ellipse.128x128 | 39.65/px, -, **CPU** | 14.92/px, -, **CPU** |
| Gdi.ExtTextOut.FixedOpaque | 156,066.47/string, -, **n/a** | 26,355.77/string, -, **n/a** |
| Gdi.ExtTextOut.FixedTransparent | 57,009.96/string, -, **n/a** | 31,067.19/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeOpaque | 127,563.89/string, -, **n/a** | 27,380.38/string, -, **n/a** |
| Gdi.ExtTextOut.TrueTypeTransparent | 39,330.53/string, -, **n/a** | 25,836.64/string, -, **n/a** |
| Gdi.FillRect.1280x800 | 30.65/px, 2.1, **CPU** | 1.21/px, 2.1, **CPU-fast** |
| Gdi.FillRect.128x128 | 30.89/px, 2.1, **CPU** | 1.75/px, 2.1, **CPU** |
| Gdi.FillRect.16x16 | 44.40/px, 2.1, **CPU** | 21.70/px, 2.1, **CPU** |
| Gdi.FillRect.256x256 | 30.84/px, 2.1, **CPU** | 1.51/px, 2.1, **CPU-fast** |
| Gdi.FillRect.32x32 | 34.05/px, 2.1, **CPU** | 4.50/px, 2.1, **CPU** |
| Gdi.FillRect.512x512 | 30.72/px, 2.1, **CPU** | 1.43/px, 2.1, **CPU-fast** |
| Gdi.FillRect.64x64 | 31.50/px, 2.1, **CPU** | 1.93/px, 2.1, **CPU** |
| Gdi.FillRect.8x8 | 87.48/px, 2.1, **CPU** | 67.88/px, 2.1, **CPU** |
| Gdi.LineDiagonal30 | 7,932.85/line, -, **n/a** | 4,518.33/line, -, **n/a** |
| Gdi.LineHorizontal | 8,771.69/line, -, **n/a** | 3,121.23/line, -, **n/a** |
| Gdi.LineVertical | 9,361.21/line, -, **n/a** | 8,508.62/line, -, **n/a** |
| Gdi.PatBltHatch.1280x800 | 30.58/px, 1.7, **CPU** | 2.08/px, 2.7, **CPU** |
| Gdi.PatBltHatch.128x128 | 30.87/px, 1.7, **CPU** | 2.78/px, 2.7, **CPU** |
| Gdi.PatBltHatch.16x16 | 42.67/px, 1.7, **CPU** | 19.87/px, 2.7, **CPU** |
| Gdi.PatBltHatch.256x256 | 30.70/px, 1.7, **CPU** | 2.38/px, 2.7, **CPU** |
| Gdi.PatBltHatch.32x32 | 33.65/px, 1.7, **CPU** | 4.44/px, 2.7, **CPU** |
| Gdi.PatBltHatch.512x512 | 30.66/px, 1.7, **CPU** | 2.20/px, 2.7, **CPU** |
| Gdi.PatBltHatch.64x64 | 31.44/px, 1.7, **CPU** | 2.51/px, 2.7, **CPU** |
| Gdi.PatBltHatch.8x8 | 78.29/px, 1.7, **CPU** | 58.16/px, 2.7, **CPU** |
| Gdi.PatBltPattern.1280x800 | 30.62/px, 1.7, **CPU** | 2.08/px, 2.7, **CPU** |
| Gdi.PatBltPattern.128x128 | 30.88/px, 1.7, **CPU** | 2.78/px, 2.7, **CPU** |
| Gdi.PatBltPattern.16x16 | 42.70/px, 1.7, **CPU** | 19.81/px, 2.7, **CPU** |
| Gdi.PatBltPattern.256x256 | 30.71/px, 1.7, **CPU** | 2.38/px, 2.7, **CPU** |
| Gdi.PatBltPattern.32x32 | 33.68/px, 1.7, **CPU** | 4.44/px, 2.7, **CPU** |
| Gdi.PatBltPattern.512x512 | 30.66/px, 1.7, **CPU** | 2.20/px, 2.7, **CPU** |
| Gdi.PatBltPattern.64x64 | 31.41/px, 1.7, **CPU** | 2.51/px, 2.7, **CPU** |
| Gdi.PatBltPattern.8x8 | 78.96/px, 1.7, **CPU** | 58.24/px, 2.7, **CPU** |
| Gdi.PatBltSolid.1280x800 | 30.62/px, 1.2, **CPU** | 1.21/px, 1.2, **CPU-fast** |
| Gdi.PatBltSolid.128x128 | 30.80/px, 1.2, **CPU** | 1.65/px, 1.2, **CPU** |
| Gdi.PatBltSolid.16x16 | 37.67/px, 1.2, **CPU** | 15.16/px, 1.2, **CPU** |
| Gdi.PatBltSolid.256x256 | 30.81/px, 1.2, **CPU** | 1.46/px, 1.2, **CPU-fast** |
| Gdi.PatBltSolid.32x32 | 32.40/px, 1.2, **CPU** | 2.91/px, 1.2, **CPU** |
| Gdi.PatBltSolid.512x512 | 30.71/px, 1.2, **CPU** | 1.43/px, 1.2, **CPU-fast** |
| Gdi.PatBltSolid.64x64 | 31.09/px, 1.2, **CPU** | 1.53/px, 1.2, **CPU** |
| Gdi.PatBltSolid.8x8 | 58.58/px, 1.2, **CPU** | 38.46/px, 1.2, **CPU** |
| Gdi.Polygon.128x128 | 37.12/px, -, **CPU** | 10.56/px, -, **CPU** |
| Gdi.Polyline | 6,193.62/line, -, **n/a** | 821.27/line, -, **n/a** |
| Gdi.Rectangle.128x128 | 32.44/px, -, **CPU** | 5.62/px, -, **CPU** |
| Gdi.Rop.BLACKNESS.64x64 | 31.19/px, -, **CPU** | 6.96/px, -, **CPU** |
| Gdi.Rop.NOTSRCCOPY.64x64 | 211.37/px, -, **CPU** | 69.47/px, -, **CPU** |
| Gdi.Rop.PATCOPY.64x64 | 31.24/px, -, **CPU** | 6.99/px, -, **CPU** |
| Gdi.Rop.PSDPXAX.64x64 | 342.48/px, -, **CPU** | 113.37/px, -, **CPU** |
| Gdi.Rop.SRCAND.64x64 | 342.31/px, -, **CPU** | 103.96/px, -, **CPU** |
| Gdi.Rop.SRCCOPY.64x64 | 205.97/px, -, **CPU** | 68.47/px, -, **CPU** |
| Gdi.Rop.SRCINVERT.64x64 | 342.20/px, -, **CPU** | 104.06/px, -, **CPU** |
| Gdi.Rop.WHITENESS.64x64 | 31.20/px, -, **CPU** | 7.00/px, -, **CPU** |
| Gdi.RoundRect.128x128 | 34.65/px, -, **CPU** | 7.94/px, -, **CPU** |
| Gdi.ScrollBitBlt | 207.16/px, -, **CPU** | 18.97/px, -, **CPU** |
| Gdi.ScrollDC | 207.18/px, -, **CPU** | 18.98/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.1280x800 | 30.87/px, -, **CPU** | 2.08/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16BottomUp.640x400 | 30.74/px, -, **CPU** | 1.78/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.1280x800 | 30.88/px, -, **CPU** | 2.03/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.16TopDown.640x400 | 30.75/px, -, **CPU** | 1.76/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.1280x800 | 31.37/px, -, **CPU** | 7.15/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24BottomUp.640x400 | 30.75/px, -, **CPU** | 6.83/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.1280x800 | 31.25/px, -, **CPU** | 6.94/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.24TopDown.640x400 | 30.74/px, -, **CPU** | 6.81/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.1280x800 | 30.71/px, -, **CPU** | 7.32/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8BottomUp.640x400 | 30.71/px, -, **CPU** | 7.36/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.1280x800 | 30.64/px, -, **CPU** | 7.32/px, -, **CPU** |
| Gdi.SetDIBitsToDevice.8TopDown.640x400 | 30.71/px, -, **CPU** | 7.36/px, -, **CPU** |
| Gdi.StretchBltDown.128x128 | 672.94/px, -, **CPU** | 231.01/px, -, **CPU** |
| Gdi.StretchBltDown.64x64 | 691.36/px, -, **CPU** | 388.04/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.128x128 | 672.88/px, -, **CPU** | 230.66/px, -, **CPU** |
| Gdi.StretchBltDownHalftone.64x64 | 690.94/px, -, **CPU** | 383.94/px, -, **CPU** |
| Gdi.StretchBltUp.128x128 | 92.61/px, -, **CPU** | 50.13/px, -, **CPU** |
| Gdi.StretchBltUp.256x256 | 87.31/px, -, **CPU** | 41.98/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.128x128 | 92.69/px, -, **CPU** | 50.10/px, -, **CPU** |
| Gdi.StretchBltUpHalftone.256x256 | 87.27/px, -, **CPU** | 42.27/px, -, **CPU** |
| Gdi.StretchDIBits.128x128 | 63.64/px, -, **CPU** | 35.00/px, -, **CPU** |
| Gdi.StretchDIBits.256x256 | 55.09/px, -, **CPU** | 25.97/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.128x128 | 63.61/px, -, **CPU** | 34.98/px, -, **CPU** |
| Gdi.StretchDIBitsHalftone.256x256 | 55.13/px, -, **CPU** | 25.97/px, -, **CPU** |
| Gdi.TextOut.FixedOpaque | 156,082.37/string, -, **n/a** | 26,400.23/string, -, **n/a** |
| Gdi.TextOut.FixedTransparent | 56,947.90/string, -, **n/a** | 30,981.09/string, -, **n/a** |
| Gdi.TextOut.TrueTypeOpaque | 127,417.00/string, -, **n/a** | 27,554.24/string, -, **n/a** |
| Gdi.TextOut.TrueTypeTransparent | 39,335.08/string, -, **n/a** | 27,505.68/string, -, **n/a** |

## Opportunities: tommohp-gm965-b50-uncached on the CPU where another configuration proves hardware

None.

## CLAIMED-NOT-DELIVERED

- tommohp-gm965-b51-wc: Dd.Blt.VidToVid.Plain.16x16 claimed, timed CPU
- tommohp-gm965-b51-wc: Dd.Blt.VidToVid.Plain.256x256 claimed, timed CPU-fast
- tommohp-gm965-b51-wc: Dd.Blt.VidToVid.Plain.512x512 claimed, timed CPU-fast
- tommohp-gm965-b51-wc: Dd.Blt.VidToVid.Plain.64x64 claimed, timed CPU-fast
- tommohp-gm965-b51-wc: Dd.BltFast.VidToVid.Plain.16x16 claimed, timed CPU
- tommohp-gm965-b51-wc: Dd.BltFast.VidToVid.Plain.256x256 claimed, timed CPU-fast
- tommohp-gm965-b51-wc: Dd.BltFast.VidToVid.Plain.512x512 claimed, timed CPU-fast
- tommohp-gm965-b51-wc: Dd.BltFast.VidToVid.Plain.64x64 claimed, timed CPU-fast
- tommohp-gm965-b51-wc: Dd.ColorFill.Vid.16x16 claimed, timed CPU
- tommohp-gm965-b51-wc: Dd.ColorFill.Vid.256x256 claimed, timed CPU
- tommohp-gm965-b51-wc: Dd.ColorFill.Vid.512x512 claimed, timed CPU
- tommohp-gm965-b51-wc: Dd.ColorFill.Vid.64x64 claimed, timed CPU
