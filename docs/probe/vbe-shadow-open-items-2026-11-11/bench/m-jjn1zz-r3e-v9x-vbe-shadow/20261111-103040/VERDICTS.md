# Verdicts: m-jjn1zz-r3e-v9x-vbe-shadow

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x32, build a80e784, driver Velocity9x AMD Radeon R3E (Mullins)

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | 2,253.9 |
| MemCopy | 1,166.9 |
| DibToDibBlt | 1,978.0 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 181.9 | 26.342/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 181.4 | 26.409/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 182.2 | 26.291/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 181.3 | 26.425/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 182.1 | 26.313/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 175.0 | 27.379/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 700.1 | 6.844/px | - | -21.8 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 826.1 | 5.800/px | - | -21.8 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 161.0 | 29.762/px | - | -21.8 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 961.2 | 4.984/px | - | -21.8 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 336.9 | 14.221/px | - | -21.8 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 614.6 | 7.795/px | - | -21.8 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 622.1 | 7.702/px | - | -21.8 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 50.0 | 95.827/px | - | -21.8 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 686.5 | 6.979/px | - | -21.3 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 807.6 | 5.933/px | - | -21.3 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 140.2 | 34.174/px | - | -21.3 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 954.9 | 5.017/px | - | -21.3 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 313.3 | 15.293/px | - | -21.3 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 609.2 | 7.865/px | - | -21.3 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 599.4 | 7.993/px | - | -21.3 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 42.1 | 113.927/px | - | -21.3 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 703.7 | 6.808/px | - | -18.4 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 801.5 | 5.977/px | - | -18.4 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 155.9 | 30.736/px | - | -18.4 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 943.5 | 5.078/px | - | -18.4 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 321.5 | 14.901/px | - | -18.4 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 630.2 | 7.602/px | - | -18.4 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 592.2 | 8.090/px | - | -18.4 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 50.3 | 95.156/px | - | -18.4 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 217.2 | 22.061/px | - | 72.9 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 25.6 | 186.785/px | - | 72.9 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 399.6 | 11.988/px | - | 72.9 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 54.4 | 88.027/px | - | 72.9 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 111.2 | 43.076/px | - | 72.9 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 11.2 | 427.063/px | - | 72.9 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 719.6 | 6.657/px | - | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 102.1 | 46.910/px | - | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 864.4 | 5.543/px | - | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 254.2 | 18.844/px | - | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 889.5 | 5.386/px | - | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 486.0 | 9.857/px | - | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 43.5 | 110.052/px | - | 11.8 |  |
| Gdi.Ellipse.128x128 | CPU | 251.2 | 19.071/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 34,518.958/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 36,664.648/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 31,008.720/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 33,376.364/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU | 802.5 | 5.970/px | - | 1.1 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 840.0 | 5.704/px | - | 1.1 |  |
| Gdi.FillRect.16x16 | CPU | 136.1 | 35.209/px | - | 1.1 |  |
| Gdi.FillRect.256x256 | CPU | 998.4 | 4.799/px | - | 1.1 |  |
| Gdi.FillRect.32x32 | CPU | 314.2 | 15.248/px | - | 1.1 |  |
| Gdi.FillRect.512x512 | CPU | 873.7 | 5.484/px | - | 1.1 |  |
| Gdi.FillRect.64x64 | CPU | 600.7 | 7.976/px | - | 1.1 |  |
| Gdi.FillRect.8x8 | CPU | 40.5 | 118.233/px | - | 1.1 |  |
| Gdi.LineDiagonal30 | n/a | - | 24,549.128/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 5,744.330/line | - | - |  |
| Gdi.LineVertical | n/a | - | 18,219.352/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 801.3 | 5.979/px | - | -1.9 |  |
| Gdi.PatBltHatch.128x128 | CPU | 896.1 | 5.347/px | - | -1.9 |  |
| Gdi.PatBltHatch.16x16 | CPU | 148.8 | 32.191/px | - | -1.9 |  |
| Gdi.PatBltHatch.256x256 | CPU | 1,034.9 | 4.629/px | - | -1.9 |  |
| Gdi.PatBltHatch.32x32 | CPU | 349.1 | 13.722/px | - | -1.9 |  |
| Gdi.PatBltHatch.512x512 | CPU | 870.7 | 5.502/px | - | -1.9 |  |
| Gdi.PatBltHatch.64x64 | CPU | 643.7 | 7.443/px | - | -1.9 |  |
| Gdi.PatBltHatch.8x8 | CPU | 44.3 | 108.256/px | - | -1.9 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 802.9 | 5.967/px | - | -1.5 |  |
| Gdi.PatBltPattern.128x128 | CPU | 897.1 | 5.340/px | - | -1.5 |  |
| Gdi.PatBltPattern.16x16 | CPU | 150.2 | 31.888/px | - | -1.5 |  |
| Gdi.PatBltPattern.256x256 | CPU | 1,024.7 | 4.676/px | - | -1.5 |  |
| Gdi.PatBltPattern.32x32 | CPU | 348.8 | 13.736/px | - | -1.5 |  |
| Gdi.PatBltPattern.512x512 | CPU | 871.5 | 5.498/px | - | -1.5 |  |
| Gdi.PatBltPattern.64x64 | CPU | 643.0 | 7.451/px | - | -1.5 |  |
| Gdi.PatBltPattern.8x8 | CPU | 43.8 | 109.267/px | - | -1.5 |  |
| Gdi.PatBltSolid.1024x768 | CPU | 804.5 | 5.955/px | - | 0.6 |  |
| Gdi.PatBltSolid.128x128 | CPU | 835.8 | 5.732/px | - | 0.6 |  |
| Gdi.PatBltSolid.16x16 | CPU | 183.9 | 26.057/px | - | 0.6 |  |
| Gdi.PatBltSolid.256x256 | CPU | 996.2 | 4.809/px | - | 0.6 |  |
| Gdi.PatBltSolid.32x32 | CPU | 369.7 | 12.960/px | - | 0.6 |  |
| Gdi.PatBltSolid.512x512 | CPU | 879.7 | 5.446/px | - | 0.6 |  |
| Gdi.PatBltSolid.64x64 | CPU | 592.6 | 8.085/px | - | 0.6 |  |
| Gdi.PatBltSolid.8x8 | CPU | 58.4 | 81.982/px | - | 0.6 |  |
| Gdi.Polygon.128x128 | CPU | 220.0 | 21.778/px | - | - |  |
| Gdi.Polyline | n/a | - | 1,392.611/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 488.2 | 9.814/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 561.1 | 8.538/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 109.9 | 43.588/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 560.8 | 8.543/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 104.7 | 45.760/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 107.7 | 44.483/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 111.5 | 42.963/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 107.9 | 44.414/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 562.5 | 8.517/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 428.5 | 11.180/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 765.0 | 6.263/px | - | - |  |
| Gdi.ScrollDC | CPU | 763.3 | 6.277/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 163.1 | 29.375/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 171.9 | 27.867/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 164.1 | 29.191/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 169.4 | 28.284/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 636.0 | 7.533/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 787.3 | 6.085/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 663.6 | 7.220/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 747.2 | 6.412/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 513.5 | 9.329/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 638.9 | 7.499/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 531.1 | 9.021/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 598.2 | 8.008/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 50.9 | 94.169/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 37.9 | 126.510/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 50.9 | 94.176/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 37.9 | 126.530/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 143.9 | 33.302/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 200.2 | 23.935/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 144.3 | 33.195/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 200.2 | 23.927/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 918.3 | 5.217/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 1,148.9 | 4.170/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 114.6 | 41.800/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 164.9 | 29.048/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 33,856.680/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 36,606.798/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 30,530.543/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 32,826.700/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
