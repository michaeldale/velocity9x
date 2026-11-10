# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow-out

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x16, build a80e784, driver Velocity9x Intel HD Graphics 4600 (Haswell)

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | 31,085.3 |
| MemCopy | 20,234.7 |
| DibToDibBlt | 8,661.3 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 796.7 | 8.015/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 784.2 | 8.142/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 786.6 | 8.117/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 786.5 | 8.119/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 789.6 | 8.087/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 785.8 | 8.126/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 6,994.1 | 0.913/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,530.5 | 1.809/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 338.0 | 18.890/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 3,109.8 | 2.053/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,077.0 | 5.929/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 4,997.1 | 1.278/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,522.3 | 2.531/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 95.8 | 66.673/px | - | 3.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,141.7 | 2.032/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,171.0 | 2.941/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 203.9 | 31.311/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,057.7 | 3.103/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 647.7 | 9.858/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,710.0 | 2.356/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,562.0 | 4.088/px | - | 4.2 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 56.9 | 112.134/px | - | 4.2 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU-fast | 6,939.4 | 0.920/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 3,463.5 | 1.844/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 339.2 | 18.825/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 3,112.1 | 2.052/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 1,066.2 | 5.989/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 4,860.7 | 1.314/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 2,535.7 | 2.518/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 97.5 | 65.522/px | - | 3.5 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,125.0 | 5.676/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 120.6 | 52.947/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,121.9 | 3.009/px | - | 7.1 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 282.4 | 22.609/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 608.1 | 10.500/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 48.8 | 130.838/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 3,174.4 | 2.011/px | - | 3.6 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 244.1 | 26.157/px | - | 3.6 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 2,663.0 | 2.398/px | - | 3.6 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 789.7 | 8.085/px | - | 3.6 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 3,848.9 | 1.659/px | - | 3.6 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 1,995.4 | 3.200/px | - | 3.6 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 88.3 | 72.307/px | - | 3.6 |  |
| Gdi.Ellipse.128x128 | CPU | 142.8 | 44.730/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 25,839.866/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 19,062.114/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 24,345.757/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 18,951.559/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 7,607.0 | 0.839/px | - | 4.0 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 3,607.4 | 1.770/px | - | 4.0 |  |
| Gdi.FillRect.16x16 | CPU | 264.0 | 24.185/px | - | 4.0 |  |
| Gdi.FillRect.256x256 | CPU | 3,199.6 | 1.996/px | - | 4.0 |  |
| Gdi.FillRect.32x32 | CPU | 874.1 | 7.305/px | - | 4.0 |  |
| Gdi.FillRect.512x512 | CPU | 5,254.4 | 1.215/px | - | 4.0 |  |
| Gdi.FillRect.64x64 | CPU | 2,247.0 | 2.842/px | - | 4.0 |  |
| Gdi.FillRect.8x8 | CPU | 71.8 | 88.872/px | - | 4.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 9,240.069/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,514.291/line | - | - |  |
| Gdi.LineVertical | n/a | - | 10,510.237/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 5,290.3 | 1.207/px | - | 3.1 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,573.0 | 1.787/px | - | 3.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 293.3 | 21.773/px | - | 3.1 |  |
| Gdi.PatBltHatch.256x256 | CPU | 2,905.9 | 2.197/px | - | 3.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 944.5 | 6.760/px | - | 3.1 |  |
| Gdi.PatBltHatch.512x512 | CPU | 4,112.6 | 1.553/px | - | 3.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,347.1 | 2.720/px | - | 3.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 82.6 | 77.306/px | - | 3.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 5,290.2 | 1.207/px | - | 3.1 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,573.9 | 1.787/px | - | 3.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 293.0 | 21.796/px | - | 3.1 |  |
| Gdi.PatBltPattern.256x256 | CPU | 2,902.8 | 2.200/px | - | 3.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 921.3 | 6.931/px | - | 3.1 |  |
| Gdi.PatBltPattern.512x512 | CPU | 4,112.4 | 1.553/px | - | 3.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,347.0 | 2.721/px | - | 3.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 82.5 | 77.398/px | - | 3.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 7,633.2 | 0.837/px | - | 3.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,902.8 | 1.636/px | - | 3.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 384.9 | 16.591/px | - | 3.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 3,255.9 | 1.961/px | - | 3.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,187.1 | 5.379/px | - | 3.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 5,287.8 | 1.208/px | - | 3.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,721.6 | 2.346/px | - | 3.3 |  |
| Gdi.PatBltSolid.8x8 | CPU | 112.5 | 56.757/px | - | 3.3 |  |
| Gdi.Polygon.128x128 | CPU | 104.1 | 61.312/px | - | - |  |
| Gdi.Polyline | n/a | - | 823.798/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 2,283.1 | 2.797/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,284.8 | 2.795/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 573.9 | 11.127/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,276.2 | 2.805/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 539.7 | 11.830/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 559.0 | 11.422/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 607.7 | 10.507/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 560.3 | 11.397/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,285.0 | 2.794/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 195.8 | 32.615/px | - | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 7,008.0 | 0.911/px | - | - |  |
| Gdi.ScrollDC | CPU-fast | 6,963.8 | 0.917/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 3,057.2 | 2.089/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 2,668.5 | 2.393/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 3,072.7 | 2.078/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 2,640.1 | 2.419/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 694.6 | 9.193/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 671.5 | 9.509/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 695.0 | 9.187/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 665.4 | 9.597/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 2,549.1 | 2.505/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 2,275.9 | 2.806/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 2,572.4 | 2.482/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 2,254.3 | 2.832/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 39.9 | 160.039/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 36.0 | 177.512/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 39.9 | 159.970/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 35.9 | 177.721/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 176.6 | 36.165/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 204.2 | 31.268/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 176.5 | 36.169/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 204.3 | 31.248/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 175.8 | 36.311/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 225.7 | 28.285/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 176.0 | 36.285/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 225.9 | 28.269/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 25,760.398/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 18,431.765/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,204.999/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 19,176.130/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- DBGDI: truncated report
- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
