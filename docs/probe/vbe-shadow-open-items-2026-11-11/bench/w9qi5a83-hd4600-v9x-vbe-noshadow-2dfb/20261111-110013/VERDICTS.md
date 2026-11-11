# Verdicts: w9qi5a83-hd4600-v9x-vbe-noshadow-2dfb

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
| MemFill | 30,815.3 |
| MemCopy | 20,238.9 |
| DibToDibBlt | 6,982.4 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | CPU | 23.7 | 134.769/px | - | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 21.1 | 151.051/px | - | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 22.4 | 142.762/px | - | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 21.1 | 151.213/px | - | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 23.8 | 134.360/px | - | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 23.7 | 134.457/px | - | - |  |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 7,945.4 | 0.402/px | - | -0.4 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 6,242.8 | 0.511/px | - | -0.4 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 238.3 | 13.399/px | - | -0.4 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU-fast | 9,468.1 | 0.337/px | - | -0.4 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 753.1 | 4.240/px | - | -0.4 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 4,726.5 | 0.675/px | - | -0.4 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 3,031.4 | 1.053/px | - | -0.4 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 65.8 | 48.509/px | - | -0.4 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 946.9 | 3.372/px | - | 1.4 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 882.3 | 3.619/px | - | 1.4 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 152.8 | 20.893/px | - | 1.4 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 928.9 | 3.437/px | - | 1.4 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 412.1 | 7.747/px | - | 1.4 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 943.4 | 3.384/px | - | 1.4 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 726.9 | 4.392/px | - | 1.4 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 45.0 | 70.940/px | - | 1.4 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 191.7 | 16.652/px | - | 264.9 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.5 | 90.025/px | - | 264.9 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 29.6 | 107.895/px | - | 264.9 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 35.4 | 90.164/px | - | 264.9 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 33.9 | 94.307/px | - | 264.9 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 148.2 | 21.537/px | - | 264.9 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.1 | 90.944/px | - | 264.9 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 19.8 | 161.442/px | - | 264.9 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 24.2 | 131.898/px | - | 7.3 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 22.6 | 141.472/px | - | 7.3 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 25.2 | 126.656/px | - | 7.3 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 23.7 | 134.837/px | - | 7.3 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 23.8 | 133.928/px | - | 7.3 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 18.3 | 174.668/px | - | 7.3 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 24.3 | 131.424/px | - | 0.4 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 21.8 | 146.486/px | - | 0.4 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 24.2 | 132.121/px | - | 0.4 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.6 | 135.556/px | - | 0.4 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 24.2 | 132.080/px | - | 0.4 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 23.9 | 133.351/px | - | 0.4 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 18.3 | 174.575/px | - | 0.4 |  |
| Gdi.Ellipse.128x128 | CPU | 419.8 | 7.605/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 15,231.667/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 13,674.653/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 14,830.316/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 12,575.421/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 7,929.1 | 0.403/px | - | 0.0 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU-fast | 5,523.9 | 0.578/px | - | 0.0 |  |
| Gdi.FillRect.16x16 | CPU | 164.7 | 19.388/px | - | 0.0 |  |
| Gdi.FillRect.256x256 | CPU-fast | 9,225.2 | 0.346/px | - | 0.0 |  |
| Gdi.FillRect.32x32 | CPU | 547.7 | 5.829/px | - | 0.0 |  |
| Gdi.FillRect.512x512 | CPU | 4,700.5 | 0.679/px | - | 0.0 |  |
| Gdi.FillRect.64x64 | CPU | 2,343.8 | 1.362/px | - | 0.0 |  |
| Gdi.FillRect.8x8 | CPU | 44.3 | 72.115/px | - | 0.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 4,773.949/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 3,565.987/line | - | - |  |
| Gdi.LineVertical | n/a | - | 6,948.869/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 8,373.9 | 0.381/px | - | 1.1 |  |
| Gdi.PatBltHatch.128x128 | CPU-fast | 5,672.4 | 0.563/px | - | 1.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 235.3 | 13.569/px | - | 1.1 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 7,713.4 | 0.414/px | - | 1.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 727.4 | 4.389/px | - | 1.1 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 8,569.9 | 0.373/px | - | 1.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,775.2 | 1.150/px | - | 1.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 57.8 | 55.254/px | - | 1.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 8,372.4 | 0.381/px | - | 1.1 |  |
| Gdi.PatBltPattern.128x128 | CPU-fast | 5,674.4 | 0.563/px | - | 1.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 235.2 | 13.576/px | - | 1.1 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 7,735.1 | 0.413/px | - | 1.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 726.8 | 4.393/px | - | 1.1 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 8,574.7 | 0.372/px | - | 1.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,774.3 | 1.151/px | - | 1.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 57.8 | 55.266/px | - | 1.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 7,980.4 | 0.400/px | - | -0.6 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 6,937.5 | 0.460/px | - | -0.6 |  |
| Gdi.PatBltSolid.16x16 | CPU | 283.3 | 11.271/px | - | -0.6 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 10,108.3 | 0.316/px | - | -0.6 |  |
| Gdi.PatBltSolid.32x32 | CPU | 839.0 | 3.805/px | - | -0.6 |  |
| Gdi.PatBltSolid.512x512 | CPU | 4,753.6 | 0.672/px | - | -0.6 |  |
| Gdi.PatBltSolid.64x64 | CPU | 3,550.3 | 0.899/px | - | -0.6 |  |
| Gdi.PatBltSolid.8x8 | CPU | 80.3 | 39.752/px | - | -0.6 |  |
| Gdi.Polygon.128x128 | CPU | 591.1 | 5.401/px | - | - |  |
| Gdi.Polyline | n/a | - | 703.513/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 1,653.9 | 1.930/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 1,412.0 | 2.261/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | unverified | 23.8 | 134.013/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 1,402.9 | 2.276/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 13.7 | 232.301/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | unverified | 13.8 | 232.136/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 23.8 | 133.920/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | unverified | 13.7 | 232.264/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 1,410.0 | 2.264/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 897.4 | 3.557/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 130.6 | 24.438/px | - | - |  |
| Gdi.ScrollDC | CPU | 130.4 | 24.489/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 1,523.2 | 2.096/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 1,488.4 | 2.145/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 1,539.2 | 2.074/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 1,519.7 | 2.101/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 369.9 | 8.631/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 368.4 | 8.665/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 370.9 | 8.609/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 369.2 | 8.648/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 934.5 | 3.417/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 928.8 | 3.437/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 941.0 | 3.393/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 932.2 | 3.425/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 7.8 | 407.063/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 7.4 | 428.730/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 8.0 | 401.337/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 7.6 | 422.221/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 78.2 | 40.840/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 91.0 | 35.078/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 78.1 | 40.856/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 91.2 | 35.003/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 92.8 | 34.386/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 120.7 | 26.451/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 92.9 | 34.380/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 121.5 | 26.275/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 15,113.786/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 13,657.051/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 14,723.362/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 12,548.203/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
- 3 unverified measurements
