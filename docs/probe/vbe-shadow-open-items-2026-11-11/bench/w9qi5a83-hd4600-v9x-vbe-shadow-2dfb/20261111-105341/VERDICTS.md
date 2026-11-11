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
| MemFill | 30,984.1 |
| MemCopy | 19,198.5 |
| DibToDibBlt | 8,678.4 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 787.8 | 8.106/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 774.2 | 8.247/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 780.3 | 8.183/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 775.8 | 8.230/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 782.2 | 8.163/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 775.9 | 8.230/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 6,936.5 | 0.921/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,445.6 | 1.853/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 336.9 | 18.950/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 3,133.2 | 2.038/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,076.7 | 5.930/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 5,021.1 | 1.272/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,490.6 | 2.564/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 97.2 | 65.660/px | - | 3.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,140.0 | 2.033/px | - | 4.1 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,256.9 | 2.829/px | - | 4.1 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 202.3 | 31.557/px | - | 4.1 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,078.5 | 3.072/px | - | 4.1 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 645.9 | 9.886/px | - | 4.1 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,719.7 | 2.348/px | - | 4.1 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,546.0 | 4.130/px | - | 4.1 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 56.8 | 112.350/px | - | 4.1 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU-fast | 6,897.6 | 0.926/px | - | 3.6 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 3,336.3 | 1.914/px | - | 3.6 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 335.3 | 19.044/px | - | 3.6 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 3,079.2 | 2.074/px | - | 3.6 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 1,062.1 | 6.012/px | - | 3.6 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 4,823.9 | 1.324/px | - | 3.6 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 2,415.9 | 2.643/px | - | 3.6 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 97.3 | 65.630/px | - | 3.6 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,104.8 | 5.780/px | - | 7.2 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 120.8 | 52.870/px | - | 7.2 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,111.5 | 3.024/px | - | 7.2 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 284.5 | 22.443/px | - | 7.2 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 600.4 | 10.636/px | - | 7.2 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 48.5 | 131.605/px | - | 7.2 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 3,153.9 | 2.025/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 243.0 | 26.272/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 2,663.8 | 2.397/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 785.0 | 8.134/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 3,854.5 | 1.657/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 1,971.1 | 3.239/px | - | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 87.8 | 72.687/px | - | 3.7 |  |
| Gdi.Ellipse.128x128 | CPU | 613.9 | 10.401/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 25,752.207/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 18,759.437/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 24,304.796/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 19,117.462/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 7,606.9 | 0.839/px | - | 4.1 | D2 -0.02 |
| Gdi.FillRect.128x128 | CPU | 3,423.8 | 1.865/px | - | 4.1 |  |
| Gdi.FillRect.16x16 | CPU | 256.2 | 24.924/px | - | 4.1 |  |
| Gdi.FillRect.256x256 | CPU | 3,200.8 | 1.995/px | - | 4.1 |  |
| Gdi.FillRect.32x32 | CPU | 849.1 | 7.520/px | - | 4.1 |  |
| Gdi.FillRect.512x512 | CPU | 5,255.4 | 1.215/px | - | 4.1 |  |
| Gdi.FillRect.64x64 | CPU | 2,207.5 | 2.892/px | - | 4.1 |  |
| Gdi.FillRect.8x8 | CPU | 71.2 | 89.738/px | - | 4.1 |  |
| Gdi.LineDiagonal30 | n/a | - | 9,349.840/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,590.966/line | - | - |  |
| Gdi.LineVertical | n/a | - | 10,340.465/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 5,292.0 | 1.207/px | - | 3.2 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,355.9 | 1.903/px | - | 3.2 |  |
| Gdi.PatBltHatch.16x16 | CPU | 285.2 | 22.387/px | - | 3.2 |  |
| Gdi.PatBltHatch.256x256 | CPU | 2,910.0 | 2.194/px | - | 3.2 |  |
| Gdi.PatBltHatch.32x32 | CPU | 923.2 | 6.916/px | - | 3.2 |  |
| Gdi.PatBltHatch.512x512 | CPU | 4,098.8 | 1.558/px | - | 3.2 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,297.4 | 2.779/px | - | 3.2 |  |
| Gdi.PatBltHatch.8x8 | CPU | 82.7 | 77.171/px | - | 3.2 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 5,289.9 | 1.207/px | - | 3.2 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,356.1 | 1.903/px | - | 3.2 |  |
| Gdi.PatBltPattern.16x16 | CPU | 285.3 | 22.382/px | - | 3.2 |  |
| Gdi.PatBltPattern.256x256 | CPU | 2,909.9 | 2.194/px | - | 3.2 |  |
| Gdi.PatBltPattern.32x32 | CPU | 924.3 | 6.908/px | - | 3.2 |  |
| Gdi.PatBltPattern.512x512 | CPU | 4,113.3 | 1.552/px | - | 3.2 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,297.3 | 2.779/px | - | 3.2 |  |
| Gdi.PatBltPattern.8x8 | CPU | 82.8 | 77.120/px | - | 3.2 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 7,639.2 | 0.836/px | - | 3.4 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,669.5 | 1.740/px | - | 3.4 |  |
| Gdi.PatBltSolid.16x16 | CPU | 386.2 | 16.535/px | - | 3.4 |  |
| Gdi.PatBltSolid.256x256 | CPU | 3,258.3 | 1.960/px | - | 3.4 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,184.9 | 5.389/px | - | 3.4 |  |
| Gdi.PatBltSolid.512x512 | CPU | 5,295.6 | 1.206/px | - | 3.4 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,667.2 | 2.394/px | - | 3.4 |  |
| Gdi.PatBltSolid.8x8 | CPU | 112.4 | 56.814/px | - | 3.4 |  |
| Gdi.Polygon.128x128 | CPU | 679.9 | 9.392/px | - | - |  |
| Gdi.Polyline | n/a | - | 825.933/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 2,077.4 | 3.074/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,279.8 | 2.801/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 569.2 | 11.217/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,268.5 | 2.815/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 515.5 | 12.387/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 554.2 | 11.522/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 599.9 | 10.644/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 554.6 | 11.512/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,279.7 | 2.801/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,271.1 | 5.024/px | - | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 7,002.6 | 0.912/px | - | - |  |
| Gdi.ScrollDC | CPU-fast | 6,955.4 | 0.918/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 3,046.1 | 2.096/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 2,666.0 | 2.395/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 3,070.5 | 2.079/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 2,642.2 | 2.417/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 693.2 | 9.211/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 672.2 | 9.500/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 693.5 | 9.207/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 669.2 | 9.541/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 2,550.6 | 2.503/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 2,274.3 | 2.808/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 2,563.1 | 2.491/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 2,238.2 | 2.853/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 40.1 | 159.406/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 35.8 | 178.242/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 40.0 | 159.639/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 35.9 | 177.972/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 175.6 | 36.361/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 203.4 | 31.399/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 175.7 | 36.336/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 203.7 | 31.344/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 173.8 | 36.745/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 224.8 | 28.405/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 169.0 | 37.778/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 224.7 | 28.418/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 25,654.398/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 18,353.421/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,200.218/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 19,006.023/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
