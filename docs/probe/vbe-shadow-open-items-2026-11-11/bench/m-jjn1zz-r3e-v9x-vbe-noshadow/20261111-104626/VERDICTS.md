# Verdicts: m-jjn1zz-r3e-v9x-vbe-noshadow

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
| MemFill | 2,284.9 |
| MemCopy | 1,166.1 |
| DibToDibBlt | 1,979.9 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 4.7 | 1,012.469/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 4.7 | 1,012.651/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 4.7 | 1,010.493/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 4.7 | 1,010.118/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 4.7 | 1,010.830/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 4.7 | 1,010.149/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 1,802.0 | 2.659/px | - | 3.2 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 2,221.6 | 2.157/px | - | 3.2 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 286.5 | 16.721/px | - | 3.2 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU-fast | 2,444.4 | 1.960/px | - | 3.2 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 825.0 | 5.807/px | - | 3.2 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 2,500.7 | 1.916/px | - | 3.2 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU-fast | 1,787.0 | 2.681/px | - | 3.2 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 77.7 | 61.682/px | - | 3.2 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU-fast | 1,829.7 | 2.618/px | - | 6.3 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU-fast | 1,969.3 | 2.433/px | - | 6.3 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 150.7 | 31.791/px | - | 6.3 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU-fast | 2,418.2 | 1.981/px | - | 6.3 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 500.7 | 9.568/px | - | 6.3 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU-fast | 2,491.3 | 1.923/px | - | 6.3 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,325.0 | 3.616/px | - | 6.3 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 39.1 | 122.652/px | - | 6.3 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 5.3 | 904.536/px | - | -6.7 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 5.3 | 902.329/px | - | -6.7 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 5.3 | 903.780/px | - | -6.7 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 5.3 | 902.586/px | - | -6.7 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 5.2 | 918.416/px | - | -6.7 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 5.3 | 903.403/px | - | -6.7 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 5.3 | 901.584/px | - | -6.7 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 5.1 | 942.334/px | - | -6.7 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 4.7 | 1,009.478/px | - | -11.2 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 4.7 | 1,012.216/px | - | -11.2 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 4.7 | 1,015.038/px | - | -11.2 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 4.7 | 1,025.996/px | - | -11.2 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 4.7 | 1,012.849/px | - | -11.2 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 4.6 | 1,051.281/px | - | -11.2 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 4.7 | 1,011.877/px | - | 16.2 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 4.7 | 1,019.367/px | - | 16.2 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 4.7 | 1,011.240/px | - | 16.2 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 4.7 | 1,026.982/px | - | 16.2 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 4.7 | 1,010.194/px | - | 16.2 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 4.7 | 1,012.396/px | - | 16.2 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 4.5 | 1,053.401/px | - | 16.2 |  |
| Gdi.Ellipse.128x128 | CPU | 375.5 | 12.759/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 18,347.657/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 26,707.753/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 19,771.682/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 24,981.881/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 2,516.0 | 1.904/px | - | 4.3 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU-fast | 2,198.8 | 2.179/px | - | 4.3 |  |
| Gdi.FillRect.16x16 | CPU | 221.3 | 21.647/px | - | 4.3 |  |
| Gdi.FillRect.256x256 | CPU-fast | 2,385.2 | 2.009/px | - | 4.3 |  |
| Gdi.FillRect.32x32 | CPU | 694.6 | 6.897/px | - | 4.3 |  |
| Gdi.FillRect.512x512 | CPU-fast | 2,487.9 | 1.926/px | - | 4.3 |  |
| Gdi.FillRect.64x64 | CPU-fast | 1,586.2 | 3.020/px | - | 4.3 |  |
| Gdi.FillRect.8x8 | CPU | 57.2 | 83.804/px | - | 4.3 |  |
| Gdi.LineDiagonal30 | n/a | - | 7,153.221/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,169.057/line | - | - |  |
| Gdi.LineVertical | n/a | - | 9,400.885/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 2,525.0 | 1.897/px | - | 3.6 |  |
| Gdi.PatBltHatch.128x128 | CPU-fast | 2,236.2 | 2.142/px | - | 3.6 |  |
| Gdi.PatBltHatch.16x16 | CPU | 254.9 | 18.798/px | - | 3.6 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 2,451.6 | 1.954/px | - | 3.6 |  |
| Gdi.PatBltHatch.32x32 | CPU | 785.5 | 6.099/px | - | 3.6 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 2,509.2 | 1.909/px | - | 3.6 |  |
| Gdi.PatBltHatch.64x64 | CPU-fast | 1,657.0 | 2.891/px | - | 3.6 |  |
| Gdi.PatBltHatch.8x8 | CPU | 64.1 | 74.703/px | - | 3.6 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 2,523.2 | 1.899/px | - | 3.6 |  |
| Gdi.PatBltPattern.128x128 | CPU-fast | 2,233.1 | 2.145/px | - | 3.6 |  |
| Gdi.PatBltPattern.16x16 | CPU | 254.3 | 18.837/px | - | 3.6 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 2,448.6 | 1.957/px | - | 3.6 |  |
| Gdi.PatBltPattern.32x32 | CPU | 783.3 | 6.116/px | - | 3.6 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 2,509.0 | 1.910/px | - | 3.6 |  |
| Gdi.PatBltPattern.64x64 | CPU-fast | 1,655.1 | 2.895/px | - | 3.6 |  |
| Gdi.PatBltPattern.8x8 | CPU | 64.0 | 74.863/px | - | 3.6 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 2,523.6 | 1.898/px | - | 2.2 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 2,350.4 | 2.038/px | - | 2.2 |  |
| Gdi.PatBltSolid.16x16 | CPU | 373.7 | 12.820/px | - | 2.2 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 2,483.4 | 1.929/px | - | 2.2 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,020.6 | 4.694/px | - | 2.2 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 2,517.5 | 1.903/px | - | 2.2 |  |
| Gdi.PatBltSolid.64x64 | CPU-fast | 1,938.6 | 2.471/px | - | 2.2 |  |
| Gdi.PatBltSolid.8x8 | noisy | 98.3 | 48.737/px | - | 2.2 |  |
| Gdi.Polygon.128x128 | CPU | 585.5 | 8.182/px | - | - |  |
| Gdi.Polyline | n/a | - | 745.005/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 985.3 | 4.862/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 1,068.1 | 4.485/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 4.7 | 1,011.300/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 1,063.3 | 4.506/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 2.5 | 1,891.472/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 2.5 | 1,891.749/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 4.7 | 1,012.363/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 2.5 | 1,892.366/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 1,068.7 | 4.483/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 735.5 | 6.514/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 4.7 | 1,011.576/px | - | - |  |
| Gdi.ScrollDC | CPU | 4.7 | 1,013.926/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 187.7 | 25.531/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 187.0 | 25.622/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 187.7 | 25.521/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 187.6 | 25.545/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 1,222.1 | 3.920/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU-fast | 1,395.9 | 3.432/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 1,323.0 | 3.621/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU-fast | 1,414.9 | 3.386/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 932.2 | 5.139/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 918.7 | 5.215/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 936.0 | 5.119/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 924.7 | 5.181/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 1.3 | 3,685.933/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 1.3 | 3,720.185/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 1.3 | 3,694.138/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 1.3 | 3,713.718/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 18.9 | 253.921/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 19.4 | 246.611/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 18.8 | 254.667/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 19.4 | 246.436/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU-fast | 1,797.8 | 2.665/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU-fast | 2,228.6 | 2.150/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 126.0 | 38.034/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 184.6 | 25.947/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 18,214.493/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 26,725.401/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 19,370.482/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 24,523.050/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
