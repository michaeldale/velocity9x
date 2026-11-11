# Verdicts: m-jjn1zz-r3e-v9x-vbe-shadow

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
| MemFill | 2,355.4 |
| MemCopy | 1,190.8 |
| DibToDibBlt | 1,450.7 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 132.3 | 9.054/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 133.2 | 8.991/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 134.0 | 8.940/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 133.4 | 8.980/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 131.4 | 9.113/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 131.2 | 9.126/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 1,163.5 | 1.029/px | - | 10.1 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 452.8 | 2.645/px | - | 10.1 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 46.8 | 25.605/px | - | 10.1 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 704.9 | 1.699/px | - | 10.1 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 118.0 | 10.149/px | - | 10.1 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 864.6 | 1.385/px | - | 10.1 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 291.9 | 4.103/px | - | 10.1 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 14.3 | 83.779/px | - | 10.1 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 388.3 | 3.085/px | - | 10.5 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 262.3 | 4.566/px | - | 10.5 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 37.3 | 32.106/px | - | 10.5 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 321.1 | 3.730/px | - | 10.5 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 91.5 | 13.090/px | - | 10.5 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 351.5 | 3.407/px | - | 10.5 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 191.6 | 6.252/px | - | 10.5 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 11.3 | 105.777/px | - | 10.5 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU-fast | 1,151.4 | 1.040/px | - | 10.9 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 421.9 | 2.839/px | - | 10.9 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 47.0 | 25.487/px | - | 10.9 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 682.0 | 1.756/px | - | 10.9 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 119.0 | 10.061/px | - | 10.9 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 854.9 | 1.401/px | - | 10.9 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 272.1 | 4.401/px | - | 10.9 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 14.5 | 82.752/px | - | 10.9 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 183.2 | 6.537/px | - | 23.4 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 19.0 | 63.094/px | - | 23.4 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 354.7 | 3.376/px | - | 23.4 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 42.6 | 28.097/px | - | 23.4 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 92.8 | 12.909/px | - | 23.4 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 7.5 | 160.645/px | - | 23.4 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 440.0 | 2.722/px | - | 11.0 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 39.1 | 30.643/px | - | 11.0 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 619.8 | 1.932/px | - | 11.0 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 100.0 | 11.980/px | - | 11.0 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 770.2 | 1.555/px | - | 11.0 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 273.4 | 4.381/px | - | 11.0 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 12.6 | 94.804/px | - | 11.0 |  |
| Gdi.Ellipse.128x128 | CPU | 73.3 | 16.334/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 19,903.004/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 27,606.555/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 19,678.049/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 25,258.553/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 1,217.8 | 0.984/px | - | 11.5 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 455.3 | 2.631/px | - | 11.5 |  |
| Gdi.FillRect.16x16 | CPU | 38.5 | 31.143/px | - | 11.5 |  |
| Gdi.FillRect.256x256 | CPU | 697.1 | 1.718/px | - | 11.5 |  |
| Gdi.FillRect.32x32 | CPU | 106.1 | 11.293/px | - | 11.5 |  |
| Gdi.FillRect.512x512 | CPU | 896.7 | 1.336/px | - | 11.5 |  |
| Gdi.FillRect.64x64 | CPU | 273.5 | 4.379/px | - | 11.5 |  |
| Gdi.FillRect.8x8 | CPU | 11.2 | 106.540/px | - | 11.5 |  |
| Gdi.LineDiagonal30 | n/a | - | 15,823.289/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 5,602.062/line | - | - |  |
| Gdi.LineVertical | n/a | - | 14,216.531/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 1,106.4 | 1.083/px | - | 9.5 |  |
| Gdi.PatBltHatch.128x128 | CPU | 496.7 | 2.411/px | - | 9.5 |  |
| Gdi.PatBltHatch.16x16 | CPU | 48.6 | 24.641/px | - | 9.5 |  |
| Gdi.PatBltHatch.256x256 | CPU | 676.0 | 1.772/px | - | 9.5 |  |
| Gdi.PatBltHatch.32x32 | CPU | 125.3 | 9.559/px | - | 9.5 |  |
| Gdi.PatBltHatch.512x512 | CPU | 852.1 | 1.406/px | - | 9.5 |  |
| Gdi.PatBltHatch.64x64 | CPU | 314.3 | 3.811/px | - | 9.5 |  |
| Gdi.PatBltHatch.8x8 | CPU | 14.4 | 83.039/px | - | 9.5 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 1,105.7 | 1.083/px | - | 9.5 |  |
| Gdi.PatBltPattern.128x128 | CPU | 495.4 | 2.418/px | - | 9.5 |  |
| Gdi.PatBltPattern.16x16 | CPU | 48.5 | 24.702/px | - | 9.5 |  |
| Gdi.PatBltPattern.256x256 | CPU | 677.1 | 1.769/px | - | 9.5 |  |
| Gdi.PatBltPattern.32x32 | CPU | 124.8 | 9.596/px | - | 9.5 |  |
| Gdi.PatBltPattern.512x512 | CPU | 851.8 | 1.406/px | - | 9.5 |  |
| Gdi.PatBltPattern.64x64 | CPU | 312.9 | 3.828/px | - | 9.5 |  |
| Gdi.PatBltPattern.8x8 | CPU | 14.4 | 83.144/px | - | 9.5 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 1,223.8 | 0.979/px | - | 9.5 |  |
| Gdi.PatBltSolid.128x128 | CPU | 485.1 | 2.469/px | - | 9.5 |  |
| Gdi.PatBltSolid.16x16 | CPU | 54.7 | 21.904/px | - | 9.5 |  |
| Gdi.PatBltSolid.256x256 | CPU | 713.4 | 1.679/px | - | 9.5 |  |
| Gdi.PatBltSolid.32x32 | CPU | 131.8 | 9.090/px | - | 9.5 |  |
| Gdi.PatBltSolid.512x512 | CPU | 902.8 | 1.327/px | - | 9.5 |  |
| Gdi.PatBltSolid.64x64 | CPU | 316.6 | 3.784/px | - | 9.5 |  |
| Gdi.PatBltSolid.8x8 | CPU | 17.1 | 70.041/px | - | 9.5 |  |
| Gdi.Polygon.128x128 | CPU | 80.3 | 14.910/px | - | - |  |
| Gdi.Polyline | n/a | - | 1,076.606/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 232.8 | 5.146/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 232.8 | 5.145/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | unverified | 92.0 | 13.015/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 231.8 | 5.166/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 87.3 | 13.715/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | unverified | 89.3 | 13.408/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 92.7 | 12.916/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | unverified | 89.3 | 13.405/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 232.8 | 5.145/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 145.0 | 8.258/px | - | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 1,180.4 | 1.015/px | - | - |  |
| Gdi.ScrollDC | CPU-fast | 1,172.3 | 1.022/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 233.0 | 5.140/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 239.9 | 4.994/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 240.9 | 4.972/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 234.1 | 5.117/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 68.1 | 17.595/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 69.8 | 17.164/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 69.2 | 17.308/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 69.4 | 17.265/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 379.9 | 3.152/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 356.5 | 3.360/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 384.1 | 3.118/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 345.5 | 3.467/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 21.1 | 56.775/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 13.1 | 91.585/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 21.1 | 56.844/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 13.0 | 91.911/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 48.6 | 24.631/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 76.2 | 15.726/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 48.4 | 24.737/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 76.1 | 15.739/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 24.2 | 49.408/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 31.6 | 37.922/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 24.2 | 49.461/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 31.6 | 37.873/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 19,933.932/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 27,520.446/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 19,220.560/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 25,202.528/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
- 3 unverified measurements
