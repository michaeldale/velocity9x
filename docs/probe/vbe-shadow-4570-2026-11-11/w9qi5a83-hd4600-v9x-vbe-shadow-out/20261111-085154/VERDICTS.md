# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow-out

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x16, build a80e784, driver 

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | - |
| MemCopy | - |
| DibToDibBlt | - |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 796.8 | 8.014/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 783.9 | 8.145/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 784.6 | 8.138/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 786.4 | 8.119/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 788.6 | 8.097/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 786.4 | 8.120/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 6,871.6 | 0.929/px | - | 4.2 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,320.6 | 1.923/px | - | 4.2 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 244.4 | 26.128/px | - | 4.2 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 3,060.6 | 2.086/px | - | 4.2 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 822.6 | 7.762/px | - | 4.2 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 4,967.4 | 1.285/px | - | 4.2 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,144.3 | 2.978/px | - | 4.2 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 67.6 | 94.425/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,142.0 | 2.032/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,281.8 | 2.798/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 201.5 | 31.684/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,077.3 | 3.074/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 645.7 | 9.888/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,719.8 | 2.348/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,561.5 | 4.089/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 57.0 | 111.982/px | - | 4.0 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 4,759.2 | 1.342/px | - | 3.4 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 3,418.1 | 1.868/px | - | 3.4 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 337.3 | 18.933/px | - | 3.4 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 2,997.4 | 2.130/px | - | 3.4 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 1,060.2 | 6.023/px | - | 3.4 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 4,535.2 | 1.408/px | - | 3.4 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 2,518.8 | 2.535/px | - | 3.4 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 96.3 | 66.273/px | - | 3.4 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,122.6 | 5.688/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 120.9 | 52.829/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,121.8 | 3.009/px | - | 7.1 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 283.9 | 22.489/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 607.9 | 10.504/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 48.6 | 131.272/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 3,170.0 | 2.014/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 243.0 | 26.274/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 2,629.6 | 2.428/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 787.5 | 8.108/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 3,835.1 | 1.665/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 1,995.2 | 3.200/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 87.9 | 72.627/px | - | 3.7 |  |
| Gdi.Ellipse.128x128 | CPU | 142.9 | 44.697/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 25,851.930/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 18,749.876/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 24,335.314/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 18,926.291/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU | 7,604.3 | 0.840/px | - | 4.0 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 3,597.2 | 1.775/px | - | 4.0 |  |
| Gdi.FillRect.16x16 | CPU | 257.5 | 24.801/px | - | 4.0 |  |
| Gdi.FillRect.256x256 | CPU | 3,193.8 | 1.999/px | - | 4.0 |  |
| Gdi.FillRect.32x32 | CPU | 857.1 | 7.450/px | - | 4.0 |  |
| Gdi.FillRect.512x512 | CPU | 5,254.1 | 1.215/px | - | 4.0 |  |
| Gdi.FillRect.64x64 | CPU | 2,209.4 | 2.890/px | - | 4.0 |  |
| Gdi.FillRect.8x8 | CPU | 71.4 | 89.438/px | - | 4.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 9,292.565/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 8,108.581/line | - | - |  |
| Gdi.LineVertical | n/a | - | 13,764.732/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 5,260.5 | 1.214/px | - | 3.1 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,593.9 | 1.777/px | - | 3.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 285.5 | 22.364/px | - | 3.1 |  |
| Gdi.PatBltHatch.256x256 | CPU | 2,886.6 | 2.212/px | - | 3.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 925.0 | 6.903/px | - | 3.1 |  |
| Gdi.PatBltHatch.512x512 | CPU | 4,115.5 | 1.552/px | - | 3.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,347.8 | 2.720/px | - | 3.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 82.9 | 77.048/px | - | 3.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 5,291.6 | 1.207/px | - | 3.2 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,360.3 | 1.900/px | - | 3.2 |  |
| Gdi.PatBltPattern.16x16 | CPU | 292.5 | 21.827/px | - | 3.2 |  |
| Gdi.PatBltPattern.256x256 | CPU | 2,904.9 | 2.198/px | - | 3.2 |  |
| Gdi.PatBltPattern.32x32 | CPU | 924.2 | 6.909/px | - | 3.2 |  |
| Gdi.PatBltPattern.512x512 | CPU | 4,115.5 | 1.552/px | - | 3.2 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,346.0 | 2.722/px | - | 3.2 |  |
| Gdi.PatBltPattern.8x8 | CPU | 83.0 | 76.969/px | - | 3.2 |  |
| Gdi.PatBltSolid.1024x768 | CPU | 7,634.3 | 0.836/px | - | 3.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,901.0 | 1.637/px | - | 3.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 383.9 | 16.633/px | - | 3.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 3,253.0 | 1.963/px | - | 3.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,184.5 | 5.391/px | - | 3.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 5,296.3 | 1.206/px | - | 3.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,714.7 | 2.352/px | - | 3.3 |  |
| Gdi.PatBltSolid.8x8 | CPU | 112.6 | 56.693/px | - | 3.3 |  |
| Gdi.Polygon.128x128 | CPU | 104.2 | 61.257/px | - | - |  |
| Gdi.Polyline | n/a | - | 823.499/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 2,278.1 | 2.803/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,285.0 | 2.794/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 573.9 | 11.126/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,277.9 | 2.803/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 540.2 | 11.820/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 559.5 | 11.413/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 607.6 | 10.509/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 559.7 | 11.409/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,283.9 | 2.796/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 195.7 | 32.632/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 7,017.1 | 0.910/px | - | - |  |
| Gdi.ScrollDC | CPU | 6,966.7 | 0.917/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 3,063.8 | 2.084/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 2,668.5 | 2.393/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 3,074.8 | 2.077/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 2,639.4 | 2.419/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 693.4 | 9.208/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 672.3 | 9.497/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 694.5 | 9.194/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 669.9 | 9.531/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 2,554.9 | 2.499/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 2,276.4 | 2.805/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 2,571.5 | 2.483/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 2,253.6 | 2.833/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 39.8 | 160.503/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 35.8 | 178.464/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 39.8 | 160.474/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 35.7 | 178.661/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 175.9 | 36.292/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 204.1 | 31.280/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 175.4 | 36.405/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 203.9 | 31.311/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 173.4 | 36.830/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 224.6 | 28.432/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 172.9 | 36.920/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 224.4 | 28.452/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 25,786.086/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 18,626.690/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,438.608/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 18,798.958/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no DBTIME.INI: no MemFill/MemCopy/DibToDibBlt references
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
