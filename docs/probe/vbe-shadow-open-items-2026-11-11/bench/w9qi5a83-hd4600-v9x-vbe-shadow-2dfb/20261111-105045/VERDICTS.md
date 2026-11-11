# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow-2dfb

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x8, build a80e784, driver Velocity9x Intel HD Graphics 4600 (Haswell)

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | 30,991.6 |
| MemCopy | 19,904.9 |
| DibToDibBlt | 6,973.8 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 514.3 | 6.207/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 496.4 | 6.431/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 497.8 | 6.413/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 497.9 | 6.412/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 502.6 | 6.352/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 504.6 | 6.326/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 5,428.4 | 0.588/px | - | 0.7 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,073.4 | 1.039/px | - | 0.7 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 171.9 | 18.574/px | - | 0.7 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 4,122.7 | 0.774/px | - | 0.7 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 554.8 | 5.754/px | - | 0.7 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 3,348.0 | 0.954/px | - | 0.7 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 1,530.9 | 2.085/px | - | 0.7 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 48.7 | 65.537/px | - | 0.7 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 846.3 | 3.772/px | - | 0.8 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 768.6 | 4.154/px | - | 0.8 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 121.2 | 26.336/px | - | 0.8 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 828.9 | 3.852/px | - | 0.8 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 323.6 | 9.867/px | - | 0.8 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 782.6 | 4.079/px | - | 0.8 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 587.0 | 5.439/px | - | 0.8 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 36.2 | 88.093/px | - | 0.8 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU-fast | 5,335.3 | 0.598/px | - | 0.9 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 2,691.1 | 1.186/px | - | 0.9 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 171.6 | 18.606/px | - | 0.9 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 3,726.6 | 0.857/px | - | 0.9 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 552.6 | 5.777/px | - | 0.9 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 3,278.8 | 0.974/px | - | 0.9 |  |
| Gdi.BitBltScreenToBitmap.64x64 | noisy | 1,510.1 | 2.114/px | - | 0.9 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 48.7 | 65.555/px | - | 0.9 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 880.6 | 3.626/px | - | 5.0 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 79.9 | 39.935/px | - | 5.0 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 1,537.2 | 2.077/px | - | 5.0 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 190.8 | 16.731/px | - | 5.0 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 431.1 | 7.405/px | - | 5.0 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 29.4 | 108.459/px | - | 5.0 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 2,658.9 | 1.201/px | - | 0.8 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 128.6 | 24.822/px | - | 0.8 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 3,789.7 | 0.842/px | - | 0.8 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 432.3 | 7.384/px | - | 0.8 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 2,905.8 | 1.099/px | - | 0.8 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 1,240.4 | 2.574/px | - | 0.8 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 44.7 | 71.344/px | - | 0.8 |  |
| Gdi.Ellipse.128x128 | CPU | 340.4 | 9.378/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 20,316.834/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 20,592.952/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 17,596.945/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 15,175.267/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 5,692.5 | 0.561/px | - | 1.2 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 2,877.8 | 1.109/px | - | 1.2 |  |
| Gdi.FillRect.16x16 | CPU | 130.1 | 24.536/px | - | 1.2 |  |
| Gdi.FillRect.256x256 | CPU | 4,251.8 | 0.751/px | - | 1.2 |  |
| Gdi.FillRect.32x32 | CPU | 438.2 | 7.286/px | - | 1.2 |  |
| Gdi.FillRect.512x512 | CPU | 3,480.5 | 0.917/px | - | 1.2 |  |
| Gdi.FillRect.64x64 | CPU | 1,318.4 | 2.422/px | - | 1.2 |  |
| Gdi.FillRect.8x8 | CPU | 35.7 | 89.320/px | - | 1.2 |  |
| Gdi.LineDiagonal30 | n/a | - | 8,745.680/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,575.386/line | - | - |  |
| Gdi.LineVertical | n/a | - | 9,999.802/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 4,039.9 | 0.790/px | - | 0.5 |  |
| Gdi.PatBltHatch.128x128 | CPU | 2,907.2 | 1.098/px | - | 0.5 |  |
| Gdi.PatBltHatch.16x16 | CPU | 167.0 | 19.116/px | - | 0.5 |  |
| Gdi.PatBltHatch.256x256 | CPU | 3,885.4 | 0.822/px | - | 0.5 |  |
| Gdi.PatBltHatch.32x32 | CPU | 542.7 | 5.883/px | - | 0.5 |  |
| Gdi.PatBltHatch.512x512 | CPU | 2,985.5 | 1.069/px | - | 0.5 |  |
| Gdi.PatBltHatch.64x64 | CPU | 1,444.4 | 2.210/px | - | 0.5 |  |
| Gdi.PatBltHatch.8x8 | CPU | 44.2 | 72.296/px | - | 0.5 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 4,045.1 | 0.789/px | - | 0.6 |  |
| Gdi.PatBltPattern.128x128 | CPU | 2,909.4 | 1.097/px | - | 0.6 |  |
| Gdi.PatBltPattern.16x16 | CPU | 166.9 | 19.128/px | - | 0.6 |  |
| Gdi.PatBltPattern.256x256 | CPU | 3,886.2 | 0.822/px | - | 0.6 |  |
| Gdi.PatBltPattern.32x32 | CPU | 411.2 | 7.764/px | - | 0.6 |  |
| Gdi.PatBltPattern.512x512 | CPU | 2,986.9 | 1.069/px | - | 0.6 |  |
| Gdi.PatBltPattern.64x64 | CPU | 1,444.8 | 2.210/px | - | 0.6 |  |
| Gdi.PatBltPattern.8x8 | CPU | 44.1 | 72.421/px | - | 0.6 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 5,723.2 | 0.558/px | - | 0.5 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,232.6 | 0.988/px | - | 0.5 |  |
| Gdi.PatBltSolid.16x16 | CPU | 194.8 | 16.390/px | - | 0.5 |  |
| Gdi.PatBltSolid.256x256 | CPU | 4,417.4 | 0.723/px | - | 0.5 |  |
| Gdi.PatBltSolid.32x32 | CPU | 607.1 | 5.259/px | - | 0.5 |  |
| Gdi.PatBltSolid.512x512 | CPU | 3,528.2 | 0.905/px | - | 0.5 |  |
| Gdi.PatBltSolid.64x64 | CPU | 1,646.1 | 1.939/px | - | 0.5 |  |
| Gdi.PatBltSolid.8x8 | CPU | 56.3 | 56.686/px | - | 0.5 |  |
| Gdi.Polygon.128x128 | CPU | 418.2 | 7.635/px | - | - |  |
| Gdi.Polyline | n/a | - | 803.952/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 1,483.6 | 2.152/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 1,323.8 | 2.412/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | unverified | 406.8 | 7.849/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 1,341.5 | 2.380/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 387.7 | 8.235/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | unverified | 399.0 | 8.002/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 431.1 | 7.406/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | unverified | 398.3 | 8.015/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 1,345.2 | 2.373/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 782.6 | 4.080/px | - | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 5,576.4 | 0.573/px | - | - |  |
| Gdi.ScrollDC | CPU-fast | 5,507.7 | 0.580/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 1,276.6 | 2.501/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 1,130.5 | 2.824/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 1,288.0 | 2.479/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 1,133.7 | 2.816/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 352.6 | 9.056/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 341.9 | 9.339/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 354.4 | 9.009/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 341.6 | 9.346/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 835.5 | 3.821/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 773.2 | 4.129/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 841.4 | 3.794/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 769.9 | 4.147/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 71.2 | 44.843/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 47.9 | 66.694/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 70.8 | 45.099/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 47.9 | 66.703/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 164.3 | 19.435/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 239.8 | 13.312/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 165.9 | 19.246/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 236.2 | 13.514/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 90.7 | 35.193/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 119.2 | 26.794/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 90.8 | 35.177/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 119.4 | 26.730/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 20,184.302/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 20,706.791/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 17,446.975/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 15,135.038/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
- 3 unverified measurements
