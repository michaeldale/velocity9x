# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow-2dfb

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x32, build a80e784, driver Velocity9x Intel HD Graphics 4600 (Haswell)

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | 30,977.8 |
| MemCopy | 19,487.6 |
| DibToDibBlt | 13,414.3 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 1,020.1 | 12.519/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 1,002.8 | 12.735/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 1,004.4 | 12.715/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 1,004.3 | 12.716/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 1,002.1 | 12.744/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 994.9 | 12.836/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 7,101.4 | 1.798/px | - | 6.0 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 2,779.0 | 4.595/px | - | 6.0 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 672.2 | 18.999/px | - | 6.0 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 4,713.1 | 2.710/px | - | 6.0 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,857.4 | 6.875/px | - | 6.0 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 6,648.6 | 1.921/px | - | 6.0 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,462.0 | 5.187/px | - | 6.0 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 192.8 | 66.229/px | - | 6.0 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 6,980.2 | 1.830/px | - | 6.4 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,750.0 | 4.644/px | - | 6.4 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 555.1 | 23.008/px | - | 6.4 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 4,683.4 | 2.727/px | - | 6.4 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 1,615.8 | 7.903/px | - | 6.4 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 6,698.0 | 1.907/px | - | 6.4 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 2,340.4 | 5.456/px | - | 6.4 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 156.7 | 81.506/px | - | 6.4 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 7,255.3 | 1.760/px | - | 6.0 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 2,795.6 | 4.568/px | - | 6.0 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 667.0 | 19.146/px | - | 6.0 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 4,603.3 | 2.774/px | - | 6.0 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 1,849.2 | 6.906/px | - | 6.0 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 6,484.7 | 1.969/px | - | 6.0 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 2,395.7 | 5.330/px | - | 6.0 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 191.9 | 66.548/px | - | 6.0 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,446.5 | 8.829/px | - | 11.3 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 166.2 | 76.848/px | - | 11.3 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,752.0 | 4.640/px | - | 11.3 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 368.6 | 34.644/px | - | 11.3 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 733.0 | 17.421/px | - | 11.3 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 67.7 | 188.731/px | - | 11.3 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 2,322.5 | 5.499/px | - | 6.8 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 401.6 | 31.798/px | - | 6.8 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 3,686.5 | 3.464/px | - | 6.8 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 1,204.9 | 10.599/px | - | 6.8 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 4,872.1 | 2.621/px | - | 6.8 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 2,057.2 | 6.208/px | - | 6.8 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 174.1 | 73.353/px | - | 6.8 |  |
| Gdi.Ellipse.128x128 | CPU | 742.4 | 17.201/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 36,624.313/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 22,749.356/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 33,152.073/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 20,287.429/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU | 9,195.7 | 1.389/px | - | 6.8 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 2,810.5 | 4.544/px | - | 6.8 |  |
| Gdi.FillRect.16x16 | CPU | 520.9 | 24.516/px | - | 6.8 |  |
| Gdi.FillRect.256x256 | CPU | 4,935.9 | 2.587/px | - | 6.8 |  |
| Gdi.FillRect.32x32 | CPU | 1,555.5 | 8.210/px | - | 6.8 |  |
| Gdi.FillRect.512x512 | CPU | 7,488.0 | 1.705/px | - | 6.8 |  |
| Gdi.FillRect.64x64 | CPU | 2,410.2 | 5.299/px | - | 6.8 |  |
| Gdi.FillRect.8x8 | CPU | 145.5 | 87.750/px | - | 6.8 |  |
| Gdi.LineDiagonal30 | n/a | - | 18,930.498/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,589.151/line | - | - |  |
| Gdi.LineVertical | n/a | - | 24,386.054/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 6,227.0 | 2.051/px | - | 6.3 |  |
| Gdi.PatBltHatch.128x128 | CPU | 2,574.3 | 4.961/px | - | 6.3 |  |
| Gdi.PatBltHatch.16x16 | CPU | 392.7 | 32.523/px | - | 6.3 |  |
| Gdi.PatBltHatch.256x256 | CPU | 4,181.0 | 3.054/px | - | 6.3 |  |
| Gdi.PatBltHatch.32x32 | CPU | 1,547.1 | 8.255/px | - | 6.3 |  |
| Gdi.PatBltHatch.512x512 | CPU | 5,593.3 | 2.283/px | - | 6.3 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,326.6 | 5.489/px | - | 6.3 |  |
| Gdi.PatBltHatch.8x8 | CPU | 148.9 | 85.740/px | - | 6.3 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 6,269.8 | 2.037/px | - | 6.2 |  |
| Gdi.PatBltPattern.128x128 | CPU | 2,576.9 | 4.956/px | - | 6.2 |  |
| Gdi.PatBltPattern.16x16 | CPU | 523.1 | 24.411/px | - | 6.2 |  |
| Gdi.PatBltPattern.256x256 | CPU | 4,181.6 | 3.054/px | - | 6.2 |  |
| Gdi.PatBltPattern.32x32 | CPU | 1,548.4 | 8.247/px | - | 6.2 |  |
| Gdi.PatBltPattern.512x512 | CPU | 5,593.2 | 2.283/px | - | 6.2 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,326.2 | 5.490/px | - | 6.2 |  |
| Gdi.PatBltPattern.8x8 | CPU | 149.1 | 85.649/px | - | 6.2 |  |
| Gdi.PatBltSolid.1024x768 | CPU | 9,241.4 | 1.382/px | - | 6.2 |  |
| Gdi.PatBltSolid.128x128 | CPU | 2,888.8 | 4.421/px | - | 6.2 |  |
| Gdi.PatBltSolid.16x16 | CPU | 760.0 | 16.802/px | - | 6.2 |  |
| Gdi.PatBltSolid.256x256 | CPU | 5,000.0 | 2.554/px | - | 6.2 |  |
| Gdi.PatBltSolid.32x32 | CPU | 2,039.7 | 6.261/px | - | 6.2 |  |
| Gdi.PatBltSolid.512x512 | CPU | 7,531.0 | 1.696/px | - | 6.2 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,584.4 | 4.941/px | - | 6.2 |  |
| Gdi.PatBltSolid.8x8 | noisy | 220.4 | 57.952/px | - | 6.2 |  |
| Gdi.Polygon.128x128 | CPU | 642.9 | 19.865/px | - | - |  |
| Gdi.Polyline | n/a | - | 1,216.864/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 1,498.1 | 8.524/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,637.9 | 4.841/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 698.4 | 18.285/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,555.0 | 4.998/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 651.9 | 19.591/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 674.3 | 18.938/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 727.6 | 17.550/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 674.8 | 18.926/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,629.3 | 4.857/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,228.7 | 10.394/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 8,132.0 | 1.570/px | - | - |  |
| Gdi.ScrollDC | CPU | 8,092.3 | 1.578/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 610.1 | 20.933/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 604.0 | 21.142/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 611.0 | 20.902/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 602.4 | 21.198/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 3,172.3 | 4.026/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 3,075.9 | 4.152/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 3,260.1 | 3.917/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 3,068.1 | 4.162/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 5,464.3 | 2.337/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 5,001.3 | 2.553/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 5,559.8 | 2.297/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 4,945.9 | 2.582/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 131.9 | 96.813/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 103.5 | 123.375/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 131.8 | 96.895/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 103.4 | 123.512/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 406.7 | 31.399/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 536.1 | 23.822/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 406.3 | 31.432/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 537.3 | 23.767/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 2,343.0 | 5.450/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 3,881.3 | 3.290/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 364.6 | 35.027/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 529.4 | 24.120/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 34,589.179/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 22,813.282/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 33,148.009/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 20,183.983/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
