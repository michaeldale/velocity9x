# Verdicts: m-jjn1zz-r3e-v9x-vbe-noshadow

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x16, build a80e784, driver Velocity9x AMD Radeon R3E (Mullins)

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | 2,467.6 |
| MemCopy | 1,183.5 |
| DibToDibBlt | 1,755.3 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | CPU | 4.7 | 513.073/px | - | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 2.6 | 911.018/px | - | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 4.7 | 513.183/px | - | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 2.6 | 910.777/px | - | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 4.7 | 512.272/px | - | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 4.7 | 511.388/px | - | - |  |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 2,507.1 | 0.955/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 1,968.0 | 1.217/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 136.1 | 17.603/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU-fast | 2,365.9 | 1.013/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 465.9 | 5.142/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 2,485.3 | 0.964/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 1,160.9 | 2.063/px | - | 3.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 39.3 | 60.932/px | - | 3.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 974.1 | 2.459/px | - | 7.3 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 776.6 | 3.085/px | - | 7.3 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 86.4 | 27.735/px | - | 7.3 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 904.0 | 2.650/px | - | 7.3 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 213.8 | 11.205/px | - | 7.3 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 964.7 | 2.483/px | - | 7.3 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 518.7 | 4.619/px | - | 7.3 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 23.7 | 100.901/px | - | 7.3 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 5.3 | 451.754/px | - | 12.3 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 5.3 | 451.326/px | - | 12.3 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 5.2 | 459.355/px | - | 12.3 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 5.3 | 452.048/px | - | 12.3 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 5.1 | 472.097/px | - | 12.3 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 5.3 | 451.379/px | - | 12.3 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 5.2 | 457.205/px | - | 12.3 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 4.8 | 497.628/px | - | 12.3 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 4.7 | 505.791/px | - | 9.6 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 4.7 | 513.076/px | - | 9.6 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 4.7 | 505.749/px | - | 9.6 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 4.5 | 527.175/px | - | 9.6 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 4.7 | 511.176/px | - | 9.6 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 4.0 | 594.910/px | - | 9.6 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 4.7 | 505.146/px | - | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 4.6 | 517.291/px | - | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 4.7 | 505.455/px | - | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 4.5 | 526.904/px | - | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 4.7 | 506.065/px | - | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 4.7 | 513.108/px | - | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 4.3 | 552.475/px | - | 2.2 |  |
| Gdi.Ellipse.128x128 | CPU | 213.6 | 11.213/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 17,676.182/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 22,825.416/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 23,447.796/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 25,069.216/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 2,513.8 | 0.953/px | - | 4.4 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU-fast | 1,941.2 | 1.234/px | - | 4.4 |  |
| Gdi.FillRect.16x16 | CPU | 102.6 | 23.339/px | - | 4.4 |  |
| Gdi.FillRect.256x256 | CPU-fast | 2,351.1 | 1.019/px | - | 4.4 |  |
| Gdi.FillRect.32x32 | CPU | 377.6 | 6.345/px | - | 4.4 |  |
| Gdi.FillRect.512x512 | CPU-fast | 2,482.6 | 0.965/px | - | 4.4 |  |
| Gdi.FillRect.64x64 | CPU | 1,051.7 | 2.278/px | - | 4.4 |  |
| Gdi.FillRect.8x8 | CPU | 28.5 | 84.092/px | - | 4.4 |  |
| Gdi.LineDiagonal30 | n/a | - | 6,921.533/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 4,208.755/line | - | - |  |
| Gdi.LineVertical | n/a | - | 9,411.635/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 2,509.3 | 0.955/px | - | 3.5 |  |
| Gdi.PatBltHatch.128x128 | CPU-fast | 2,047.7 | 1.170/px | - | 3.5 |  |
| Gdi.PatBltHatch.16x16 | CPU | 129.4 | 18.515/px | - | 3.5 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 2,338.6 | 1.024/px | - | 3.5 |  |
| Gdi.PatBltHatch.32x32 | CPU | 507.7 | 4.718/px | - | 3.5 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 2,495.5 | 0.960/px | - | 3.5 |  |
| Gdi.PatBltHatch.64x64 | CPU-fast | 1,278.0 | 1.874/px | - | 3.5 |  |
| Gdi.PatBltHatch.8x8 | CPU | 36.8 | 65.146/px | - | 3.5 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 2,517.8 | 0.951/px | - | 3.5 |  |
| Gdi.PatBltPattern.128x128 | CPU-fast | 2,043.6 | 1.172/px | - | 3.5 |  |
| Gdi.PatBltPattern.16x16 | CPU | 127.4 | 18.807/px | - | 3.5 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 2,338.9 | 1.024/px | - | 3.5 |  |
| Gdi.PatBltPattern.32x32 | CPU | 506.0 | 4.734/px | - | 3.5 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 2,494.3 | 0.960/px | - | 3.5 |  |
| Gdi.PatBltPattern.64x64 | CPU-fast | 1,268.9 | 1.888/px | - | 3.5 |  |
| Gdi.PatBltPattern.8x8 | CPU | 36.5 | 65.594/px | - | 3.5 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 2,522.6 | 0.950/px | - | 2.5 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 2,193.2 | 1.092/px | - | 2.5 |  |
| Gdi.PatBltSolid.16x16 | CPU | 169.4 | 14.142/px | - | 2.5 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 2,435.5 | 0.984/px | - | 2.5 |  |
| Gdi.PatBltSolid.32x32 | CPU | 589.2 | 4.066/px | - | 2.5 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 2,505.6 | 0.956/px | - | 2.5 |  |
| Gdi.PatBltSolid.64x64 | CPU-fast | 1,404.4 | 1.706/px | - | 2.5 |  |
| Gdi.PatBltSolid.8x8 | CPU | 49.8 | 48.106/px | - | 2.5 |  |
| Gdi.Polygon.128x128 | CPU | 306.6 | 7.814/px | - | - |  |
| Gdi.Polyline | n/a | - | 773.341/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 778.7 | 3.076/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 547.3 | 4.377/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 4.7 | 511.476/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 543.2 | 4.410/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 2.5 | 953.187/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 2.5 | 953.702/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 4.7 | 512.022/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 2.5 | 952.793/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 547.7 | 4.374/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 432.5 | 5.538/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 4.7 | 505.143/px | - | - |  |
| Gdi.ScrollDC | CPU | 4.7 | 506.529/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 948.7 | 2.525/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 919.0 | 2.607/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 953.5 | 2.512/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 934.3 | 2.564/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 172.2 | 13.913/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 177.3 | 13.509/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 174.4 | 13.735/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 177.7 | 13.477/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 1,007.1 | 2.379/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 963.3 | 2.487/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 1,018.4 | 2.352/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 989.9 | 2.420/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 1.2 | 1,969.587/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 1.2 | 1,997.135/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 1.2 | 1,966.714/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 1.2 | 1,992.861/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 15.4 | 155.087/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 16.4 | 146.207/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 15.5 | 155.035/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 16.4 | 146.160/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 52.3 | 45.812/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 69.4 | 34.525/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 52.2 | 45.927/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 69.4 | 34.503/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 20,182.250/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 22,417.401/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 23,368.897/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 25,044.956/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
