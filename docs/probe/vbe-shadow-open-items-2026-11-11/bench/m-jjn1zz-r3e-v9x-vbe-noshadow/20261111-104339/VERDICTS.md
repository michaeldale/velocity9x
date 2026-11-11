# Verdicts: m-jjn1zz-r3e-v9x-vbe-noshadow

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x8, build a80e784, driver Velocity9x AMD Radeon R3E (Mullins)

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | 2,415.8 |
| MemCopy | 1,191.3 |
| DibToDibBlt | 1,444.6 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | CPU | 4.6 | 261.999/px | - | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 2.5 | 478.050/px | - | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 2.6 | 462.740/px | - | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 2.5 | 478.386/px | - | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 4.6 | 262.341/px | - | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 4.6 | 261.854/px | - | - |  |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 2,497.1 | 0.480/px | - | 4.0 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 1,463.2 | 0.819/px | - | 4.0 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 70.0 | 17.100/px | - | 4.0 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU-fast | 2,219.3 | 0.540/px | - | 4.0 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 217.6 | 5.505/px | - | 4.0 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 2,442.8 | 0.490/px | - | 4.0 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 677.0 | 1.769/px | - | 4.0 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 19.9 | 60.114/px | - | 4.0 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 465.7 | 2.572/px | - | 6.6 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 387.1 | 3.094/px | - | 6.6 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 35.1 | 34.101/px | - | 6.6 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 437.6 | 2.737/px | - | 6.6 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 114.4 | 10.467/px | - | 6.6 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 459.0 | 2.610/px | - | 6.6 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 265.0 | 4.520/px | - | 6.6 |  |
| Gdi.BitBltDibToScreen.8x8 | noisy | 14.6 | 82.006/px | - | 6.6 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 5.3 | 226.296/px | - | 12.4 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 5.2 | 228.422/px | - | 12.4 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 5.1 | 236.379/px | - | 12.4 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 5.3 | 225.701/px | - | 12.4 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 4.8 | 248.615/px | - | 12.4 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 5.3 | 226.108/px | - | 12.4 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 5.1 | 234.552/px | - | 12.4 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 4.4 | 274.509/px | - | 12.4 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 4.7 | 256.034/px | - | 18.3 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 4.5 | 263.404/px | - | 18.3 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 4.7 | 253.110/px | - | 18.3 |  |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 4.3 | 276.267/px | - | 18.3 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 4.6 | 261.829/px | - | 18.3 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 4.0 | 302.304/px | - | 18.3 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 4.7 | 256.047/px | - | 19.7 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 4.3 | 277.958/px | - | 19.7 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 4.7 | 252.527/px | - | 19.7 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 4.3 | 276.632/px | - | 19.7 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 4.7 | 252.579/px | - | 19.7 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 4.6 | 262.228/px | - | 19.7 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 3.5 | 344.678/px | - | 19.7 |  |
| Gdi.Ellipse.128x128 | CPU | 101.4 | 11.817/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 15,005.269/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 21,469.604/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 15,997.666/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 19,646.083/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 2,500.1 | 0.479/px | - | 4.8 |  |
| Gdi.FillRect.128x128 | CPU-fast | 1,441.4 | 0.831/px | - | 4.8 |  |
| Gdi.FillRect.16x16 | CPU | 53.3 | 22.480/px | - | 4.8 |  |
| Gdi.FillRect.256x256 | CPU-fast | 2,200.6 | 0.544/px | - | 4.8 |  |
| Gdi.FillRect.32x32 | CPU | 176.8 | 6.775/px | - | 4.8 |  |
| Gdi.FillRect.512x512 | CPU-fast | 2,440.4 | 0.491/px | - | 4.8 |  |
| Gdi.FillRect.64x64 | CPU | 607.5 | 1.972/px | - | 4.8 |  |
| Gdi.FillRect.8x8 | CPU | 14.7 | 81.714/px | - | 4.8 |  |
| Gdi.LineDiagonal30 | n/a | - | 6,710.372/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,156.235/line | - | - |  |
| Gdi.LineVertical | n/a | - | 14,857.878/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 2,239.9 | 0.535/px | - | 3.7 |  |
| Gdi.PatBltHatch.128x128 | CPU-fast | 1,553.0 | 0.771/px | - | 3.7 |  |
| Gdi.PatBltHatch.16x16 | CPU | 68.2 | 17.555/px | - | 3.7 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 1,849.1 | 0.648/px | - | 3.7 |  |
| Gdi.PatBltHatch.32x32 | CPU | 220.5 | 5.431/px | - | 3.7 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 2,117.6 | 0.566/px | - | 3.7 |  |
| Gdi.PatBltHatch.64x64 | CPU | 821.0 | 1.459/px | - | 3.7 |  |
| Gdi.PatBltHatch.8x8 | CPU | 19.6 | 60.977/px | - | 3.7 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 2,238.7 | 0.535/px | - | 3.7 |  |
| Gdi.PatBltPattern.128x128 | CPU-fast | 1,548.3 | 0.774/px | - | 3.7 |  |
| Gdi.PatBltPattern.16x16 | CPU | 68.1 | 17.591/px | - | 3.7 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 1,848.3 | 0.648/px | - | 3.7 |  |
| Gdi.PatBltPattern.32x32 | CPU | 220.4 | 5.434/px | - | 3.7 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 2,117.1 | 0.566/px | - | 3.7 |  |
| Gdi.PatBltPattern.64x64 | CPU | 813.6 | 1.472/px | - | 3.7 |  |
| Gdi.PatBltPattern.8x8 | CPU | 19.4 | 61.721/px | - | 3.7 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 2,514.8 | 0.476/px | - | 2.9 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 1,720.9 | 0.696/px | - | 2.9 |  |
| Gdi.PatBltSolid.16x16 | CPU | 87.5 | 13.681/px | - | 2.9 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 2,351.0 | 0.509/px | - | 2.9 |  |
| Gdi.PatBltSolid.32x32 | CPU | 262.3 | 4.567/px | - | 2.9 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 2,481.3 | 0.483/px | - | 2.9 |  |
| Gdi.PatBltSolid.64x64 | CPU | 840.5 | 1.425/px | - | 2.9 |  |
| Gdi.PatBltSolid.8x8 | CPU | 25.4 | 47.190/px | - | 2.9 |  |
| Gdi.Polygon.128x128 | CPU | 161.5 | 7.415/px | - | - |  |
| Gdi.Polyline | n/a | - | 764.804/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 425.7 | 2.814/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 313.7 | 3.818/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | unverified | 4.6 | 262.269/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 311.7 | 3.842/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 2.4 | 489.960/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | unverified | 2.5 | 487.750/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 4.6 | 261.838/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | unverified | 2.5 | 487.931/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 313.6 | 3.819/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 227.9 | 5.256/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 4.7 | 252.663/px | - | - |  |
| Gdi.ScrollDC | CPU | 4.7 | 253.208/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 278.4 | 4.302/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 272.9 | 4.389/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 279.7 | 4.282/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 275.8 | 4.343/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 70.4 | 17.017/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 72.4 | 16.552/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 71.3 | 16.810/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 72.6 | 16.497/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 455.4 | 2.630/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 437.5 | 2.738/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 461.1 | 2.598/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 448.6 | 2.670/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 1.3 | 951.172/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 1.2 | 995.520/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 1.3 | 954.221/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 1.2 | 994.987/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 14.8 | 80.675/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 16.8 | 71.498/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 14.8 | 80.789/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 16.8 | 71.409/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 25.0 | 47.894/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 32.6 | 36.729/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 25.1 | 47.771/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 32.6 | 36.726/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 14,685.407/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 21,386.335/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 15,711.121/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 19,462.978/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- DBGDI: truncated report
- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
- 3 unverified measurements
