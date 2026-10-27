# Verdicts: w9qi5a83-hd4600-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 800x600x16, build f2dd1cd, driver 

| Ceiling | MB/s |
|---|---:|
| FbWrite | - |
| FbRead | - |
| MemFill | - |
| MemCopy | - |
| DibToDibBlt | - |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 24.8 | 257.364/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 23.6 | 270.545/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 24.1 | 265.430/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 23.5 | 271.499/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 24.8 | 257.853/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 24.8 | 257.710/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 8,326.2 | 0.767/px | - | 1.9 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 476.4 | 13.404/px | - | 1.9 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 4,428.2 | 1.442/px | - | 1.9 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,896.3 | 3.367/px | - | 1.9 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 7,055.0 | 0.905/px | - | 1.9 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 4,723.3 | 1.352/px | - | 1.9 |  |
| Gdi.BitBltBitmapToScreen.800x600 | CPU | 9,042.8 | 0.706/px | - | 1.9 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 131.5 | 48.573/px | - | 1.9 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 3,295.3 | 1.938/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 190.6 | 33.507/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 4,099.1 | 1.558/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 704.3 | 9.066/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 4,367.4 | 1.462/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,908.1 | 3.346/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.800x600 | CPU | 4,356.1 | 1.466/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 51.5 | 124.015/px | - | 2.5 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.4 | 180.210/px | - | 201.1 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 33.3 | 191.759/px | - | 201.1 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 139.4 | 45.791/px | - | 201.1 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 34.9 | 183.083/px | - | 201.1 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 179.0 | 35.675/px | - | 201.1 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.3 | 180.895/px | - | 201.1 |  |
| Gdi.BitBltScreenToBitmap.800x600 | CPU | 214.3 | 29.792/px | - | 201.1 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 28.2 | 226.543/px | - | 201.1 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 24.6 | 259.881/px | - | 234.8 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.6 | 270.299/px | - | 234.8 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 103.9 | 61.443/px | - | 234.8 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 24.4 | 261.293/px | - | 234.8 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.7 | 258.507/px | - | 234.8 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 21.1 | 302.624/px | - | 234.8 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 24.1 | 264.675/px | - | 3.9 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 22.5 | 283.497/px | - | 3.9 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 24.1 | 265.069/px | - | 3.9 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.7 | 269.591/px | - | 3.9 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 24.2 | 264.265/px | - | 3.9 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 23.9 | 267.179/px | - | 3.9 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 20.7 | 308.344/px | - | 3.9 |  |
| Gdi.Ellipse.128x128 | CPU | 848.1 | 7.528/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 20,865.284/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 17,481.164/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 18,435.940/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 16,025.283/string | - | - |  |
| Gdi.FillRect.128x128 | CPU | 7,738.1 | 0.825/px | - | 2.4 |  |
| Gdi.FillRect.16x16 | CPU | 329.2 | 19.396/px | - | 2.4 |  |
| Gdi.FillRect.256x256 | CPU | 4,338.9 | 1.472/px | - | 2.4 |  |
| Gdi.FillRect.32x32 | CPU | 1,325.8 | 4.816/px | - | 2.4 |  |
| Gdi.FillRect.512x512 | CPU | 7,149.2 | 0.893/px | - | 2.4 |  |
| Gdi.FillRect.64x64 | CPU | 3,797.7 | 1.681/px | - | 2.4 |  |
| Gdi.FillRect.800x600 | CPU | 9,275.9 | 0.688/px | - | 2.4 | D2 0.00 |
| Gdi.FillRect.8x8 | CPU | 88.4 | 72.260/px | - | 2.4 |  |
| Gdi.LineDiagonal30 | n/a | - | 4,744.194/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 3,369.736/line | - | - |  |
| Gdi.LineVertical | n/a | - | 6,909.589/line | - | - |  |
| Gdi.PatBltHatch.128x128 | CPU | 7,558.2 | 0.845/px | - | 1.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 385.2 | 16.574/px | - | 1.1 |  |
| Gdi.PatBltHatch.256x256 | CPU | 9,615.3 | 0.664/px | - | 1.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 1,463.6 | 4.363/px | - | 1.1 |  |
| Gdi.PatBltHatch.512x512 | CPU | 9,711.7 | 0.657/px | - | 1.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 4,118.8 | 1.550/px | - | 1.1 |  |
| Gdi.PatBltHatch.800x600 | CPU | 10,164.6 | 0.628/px | - | 1.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 106.0 | 60.225/px | - | 1.1 |  |
| Gdi.PatBltPattern.128x128 | CPU | 7,547.9 | 0.846/px | - | 1.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 399.7 | 15.975/px | - | 1.1 |  |
| Gdi.PatBltPattern.256x256 | CPU | 9,640.6 | 0.662/px | - | 1.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 1,465.4 | 4.357/px | - | 1.1 |  |
| Gdi.PatBltPattern.512x512 | CPU | 9,800.7 | 0.652/px | - | 1.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 4,108.5 | 1.554/px | - | 1.1 |  |
| Gdi.PatBltPattern.800x600 | CPU | 10,157.7 | 0.629/px | - | 1.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 106.0 | 60.252/px | - | 1.1 |  |
| Gdi.PatBltSolid.128x128 | CPU | 9,137.6 | 0.699/px | - | 1.8 |  |
| Gdi.PatBltSolid.16x16 | CPU | 543.4 | 11.751/px | - | 1.8 |  |
| Gdi.PatBltSolid.256x256 | CPU | 4,438.3 | 1.439/px | - | 1.8 |  |
| Gdi.PatBltSolid.32x32 | CPU | 2,261.6 | 2.823/px | - | 1.8 |  |
| Gdi.PatBltSolid.512x512 | CPU | 7,208.7 | 0.886/px | - | 1.8 |  |
| Gdi.PatBltSolid.64x64 | CPU | 5,410.4 | 1.180/px | - | 1.8 |  |
| Gdi.PatBltSolid.800x600 | CPU | 9,342.2 | 0.683/px | - | 1.8 |  |
| Gdi.PatBltSolid.8x8 | CPU | 152.6 | 41.842/px | - | 1.8 |  |
| Gdi.Polygon.128x128 | CPU | 1,158.3 | 5.513/px | - | - |  |
| Gdi.Polyline | n/a | - | 685.517/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 3,279.3 | 1.947/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,159.8 | 2.956/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.6 | 259.212/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,527.3 | 2.526/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 14.9 | 428.437/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 14.9 | 427.358/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.7 | 258.301/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 14.9 | 427.377/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,150.6 | 2.969/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,814.1 | 3.520/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 148.8 | 42.904/px | - | - |  |
| Gdi.ScrollDC | CPU | 148.9 | 42.874/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.400x300 | CPU | 4,055.8 | 1.574/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.800x600 | CPU | 4,199.8 | 1.520/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.400x300 | CPU | 4,079.9 | 1.565/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.800x600 | CPU | 4,209.7 | 1.517/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.400x300 | CPU | 733.1 | 8.710/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.800x600 | CPU | 740.4 | 8.624/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.400x300 | CPU | 735.5 | 8.681/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.800x600 | CPU | 740.5 | 8.623/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.400x300 | CPU | 3,234.6 | 1.974/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.800x600 | CPU | 3,311.7 | 1.928/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.400x300 | CPU | 3,261.6 | 1.958/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.800x600 | CPU | 3,343.6 | 1.910/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 17.3 | 368.406/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 7.2 | 889.972/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 18.8 | 339.588/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 7.2 | 890.765/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 78.9 | 80.910/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 84.8 | 75.269/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 79.8 | 80.006/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 85.0 | 75.163/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 181.3 | 35.227/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 230.7 | 27.673/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 181.0 | 35.273/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 230.3 | 27.729/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 20,718.408/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 17,529.824/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 18,294.541/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 16,033.902/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no DBTIME.INI: no MemFill/MemCopy/DibToDibBlt references
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
