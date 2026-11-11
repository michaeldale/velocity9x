# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow-2dfb

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
| MemFill | 31,016.7 |
| MemCopy | 19,664.7 |
| DibToDibBlt | 8,700.0 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 753.9 | 8.469/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 783.3 | 8.152/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 786.0 | 8.124/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 785.5 | 8.128/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 789.1 | 8.092/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 785.1 | 8.133/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 6,984.7 | 0.914/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,549.9 | 1.799/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 338.3 | 18.875/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 3,130.0 | 2.040/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,073.9 | 5.946/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 5,008.4 | 1.275/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,535.0 | 2.519/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 96.4 | 66.237/px | - | 3.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,142.0 | 2.032/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,280.5 | 2.800/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 203.1 | 31.441/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,078.2 | 3.072/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 647.0 | 9.869/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,719.0 | 2.348/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,565.4 | 4.079/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 57.0 | 112.086/px | - | 4.0 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU-fast | 6,905.9 | 0.925/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 3,444.7 | 1.854/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 336.6 | 18.969/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 3,090.1 | 2.066/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 1,065.2 | 5.994/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 4,800.2 | 1.330/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 2,537.5 | 2.516/px | - | 3.5 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 97.7 | 65.346/px | - | 3.5 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,124.2 | 5.680/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 121.5 | 52.551/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,122.2 | 3.009/px | - | 7.1 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 285.0 | 22.401/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 608.7 | 10.490/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 48.8 | 130.869/px | - | 7.1 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 3,175.3 | 2.011/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 243.2 | 26.257/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 2,645.0 | 2.414/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 791.0 | 8.073/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 3,829.8 | 1.667/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 1,997.6 | 3.196/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 88.3 | 72.309/px | - | 3.7 |  |
| Gdi.Ellipse.128x128 | CPU | 620.7 | 10.288/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 25,753.729/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 18,528.951/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 24,327.295/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 18,801.831/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 7,610.1 | 0.839/px | - | 4.0 |  |
| Gdi.FillRect.128x128 | CPU | 3,622.3 | 1.763/px | - | 4.0 |  |
| Gdi.FillRect.16x16 | CPU | 263.1 | 24.268/px | - | 4.0 |  |
| Gdi.FillRect.256x256 | CPU | 3,200.1 | 1.995/px | - | 4.0 |  |
| Gdi.FillRect.32x32 | CPU | 870.4 | 7.336/px | - | 4.0 |  |
| Gdi.FillRect.512x512 | CPU | 5,255.7 | 1.215/px | - | 4.0 |  |
| Gdi.FillRect.64x64 | CPU | 2,242.9 | 2.847/px | - | 4.0 |  |
| Gdi.FillRect.8x8 | CPU | 71.4 | 89.423/px | - | 4.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 13,006.397/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,582.106/line | - | - |  |
| Gdi.LineVertical | n/a | - | 13,906.749/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 5,287.9 | 1.208/px | - | 3.0 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,581.3 | 1.783/px | - | 3.0 |  |
| Gdi.PatBltHatch.16x16 | CPU | 285.1 | 22.400/px | - | 3.0 |  |
| Gdi.PatBltHatch.256x256 | CPU | 2,907.9 | 2.196/px | - | 3.0 |  |
| Gdi.PatBltHatch.32x32 | CPU | 924.5 | 6.906/px | - | 3.0 |  |
| Gdi.PatBltHatch.512x512 | CPU | 4,047.1 | 1.578/px | - | 3.0 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,347.6 | 2.720/px | - | 3.0 |  |
| Gdi.PatBltHatch.8x8 | CPU | 83.0 | 76.967/px | - | 3.0 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 5,292.0 | 1.207/px | - | 3.1 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,574.9 | 1.786/px | - | 3.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 284.3 | 22.457/px | - | 3.1 |  |
| Gdi.PatBltPattern.256x256 | CPU | 2,909.3 | 2.195/px | - | 3.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 923.3 | 6.916/px | - | 3.1 |  |
| Gdi.PatBltPattern.512x512 | CPU | 4,117.9 | 1.551/px | - | 3.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,344.8 | 2.723/px | - | 3.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 83.0 | 76.939/px | - | 3.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 7,622.5 | 0.838/px | - | 3.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,918.4 | 1.630/px | - | 3.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 385.2 | 16.575/px | - | 3.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 3,256.4 | 1.961/px | - | 3.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,182.4 | 5.400/px | - | 3.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 5,296.6 | 1.206/px | - | 3.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,726.4 | 2.342/px | - | 3.3 |  |
| Gdi.PatBltSolid.8x8 | CPU | 112.9 | 56.554/px | - | 3.3 |  |
| Gdi.Polygon.128x128 | CPU | 701.2 | 9.107/px | - | - |  |
| Gdi.Polyline | n/a | - | 824.292/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 2,286.1 | 2.793/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 1,962.2 | 3.254/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 573.7 | 11.129/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,279.8 | 2.801/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 520.5 | 12.269/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 559.8 | 11.406/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 608.0 | 10.502/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 559.7 | 11.408/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 1,963.3 | 3.252/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,306.5 | 4.887/px | - | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 7,006.2 | 0.911/px | - | - |  |
| Gdi.ScrollDC | CPU-fast | 6,960.4 | 0.917/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 3,030.1 | 2.107/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 2,669.8 | 2.392/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 3,072.4 | 2.078/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 2,639.1 | 2.419/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 693.0 | 9.213/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 672.2 | 9.498/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 694.7 | 9.191/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 668.4 | 9.553/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 2,555.5 | 2.499/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 2,275.5 | 2.806/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 2,571.4 | 2.483/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 2,254.7 | 2.832/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 39.9 | 160.195/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 35.9 | 177.914/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 39.8 | 160.243/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 35.9 | 177.812/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 176.5 | 36.183/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 204.1 | 31.291/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 176.4 | 36.201/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 204.2 | 31.267/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 175.9 | 36.308/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 225.5 | 28.320/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 175.0 | 36.497/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 225.1 | 28.361/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 25,649.287/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 18,538.439/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,178.122/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 18,769.454/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- DBGDI: truncated report
- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
