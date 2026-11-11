# Verdicts: w9qi5a83-hd4600-v9x-vbe-noshadow-2dfb

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
| MemFill | 31,062.1 |
| MemCopy | 20,148.9 |
| DibToDibBlt | 15,357.3 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 24.9 | 512.909/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 24.9 | 512.812/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 24.9 | 513.207/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 24.9 | 512.233/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 24.9 | 513.019/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 24.9 | 513.224/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 13,458.7 | 0.949/px | - | 4.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,662.5 | 3.487/px | - | 4.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 1,049.5 | 12.168/px | - | 4.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 6,769.8 | 1.886/px | - | 4.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 3,184.5 | 4.010/px | - | 4.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 10,854.2 | 1.177/px | - | 4.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 6,953.7 | 1.837/px | - | 4.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 261.4 | 48.853/px | - | 4.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU-fast | 14,261.3 | 0.895/px | - | 5.2 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 3,539.6 | 3.608/px | - | 5.2 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 782.7 | 16.316/px | - | 5.2 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 6,718.9 | 1.901/px | - | 5.2 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 2,528.2 | 5.051/px | - | 5.2 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU-fast | 11,436.1 | 1.117/px | - | 5.2 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 6,113.9 | 2.089/px | - | 5.2 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 198.8 | 64.223/px | - | 5.2 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 245.2 | 52.086/px | - | 181.2 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 104.0 | 122.828/px | - | 181.2 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 34.4 | 371.737/px | - | 181.2 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 189.3 | 67.447/px | - | 181.2 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 35.2 | 363.172/px | - | 181.2 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 223.1 | 57.245/px | - | 181.2 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.3 | 361.329/px | - | 181.2 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 31.5 | 404.859/px | - | 181.2 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 103.7 | 123.136/px | - | 178.4 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.5 | 543.959/px | - | 178.4 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 133.6 | 95.558/px | - | 178.4 | D2 -0.05 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 24.4 | 523.560/px | - | 178.4 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.8 | 514.559/px | - | 178.4 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 21.6 | 592.006/px | - | 178.4 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 23.9 | 534.222/px | - | -4.0 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 23.0 | 554.171/px | - | -4.0 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 23.9 | 533.327/px | - | -4.0 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.8 | 536.750/px | - | -4.0 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 23.9 | 535.117/px | - | -4.0 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 23.7 | 539.184/px | - | -4.0 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 21.4 | 596.630/px | - | -4.0 |  |
| Gdi.Ellipse.128x128 | CPU | 1,374.4 | 9.292/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 26,979.563/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 17,937.611/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 25,250.857/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 16,630.883/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 14,956.2 | 0.854/px | - | 5.4 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 3,541.8 | 3.606/px | - | 5.4 |  |
| Gdi.FillRect.16x16 | CPU | 713.7 | 17.893/px | - | 5.4 |  |
| Gdi.FillRect.256x256 | CPU | 6,700.8 | 1.906/px | - | 5.4 |  |
| Gdi.FillRect.32x32 | CPU | 2,348.9 | 5.437/px | - | 5.4 |  |
| Gdi.FillRect.512x512 | CPU-fast | 11,637.8 | 1.097/px | - | 5.4 |  |
| Gdi.FillRect.64x64 | CPU | 5,828.7 | 2.191/px | - | 5.4 |  |
| Gdi.FillRect.8x8 | CPU | 176.0 | 72.568/px | - | 5.4 |  |
| Gdi.LineDiagonal30 | n/a | - | 4,972.872/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 3,604.649/line | - | - |  |
| Gdi.LineVertical | n/a | - | 21,932.570/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 12,808.8 | 0.997/px | - | 2.2 |  |
| Gdi.PatBltHatch.128x128 | CPU | 6,909.8 | 1.848/px | - | 2.2 |  |
| Gdi.PatBltHatch.16x16 | CPU | 718.0 | 17.786/px | - | 2.2 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 11,628.7 | 1.098/px | - | 2.2 |  |
| Gdi.PatBltHatch.32x32 | CPU | 2,429.4 | 5.257/px | - | 2.2 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 12,908.8 | 0.989/px | - | 2.2 |  |
| Gdi.PatBltHatch.64x64 | CPU | 4,930.1 | 2.590/px | - | 2.2 |  |
| Gdi.PatBltHatch.8x8 | CPU | 187.2 | 68.213/px | - | 2.2 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 12,832.6 | 0.995/px | - | 1.9 |  |
| Gdi.PatBltPattern.128x128 | CPU | 7,370.6 | 1.733/px | - | 1.9 |  |
| Gdi.PatBltPattern.16x16 | CPU | 740.7 | 17.241/px | - | 1.9 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 11,953.1 | 1.068/px | - | 1.9 |  |
| Gdi.PatBltPattern.32x32 | CPU | 2,427.8 | 5.260/px | - | 1.9 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 13,007.7 | 0.982/px | - | 1.9 |  |
| Gdi.PatBltPattern.64x64 | CPU | 6,002.7 | 2.127/px | - | 1.9 |  |
| Gdi.PatBltPattern.8x8 | CPU | 187.4 | 68.140/px | - | 1.9 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 14,993.9 | 0.852/px | - | 4.7 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,683.1 | 3.467/px | - | 4.7 |  |
| Gdi.PatBltSolid.16x16 | CPU | 1,295.8 | 9.855/px | - | 4.7 |  |
| Gdi.PatBltSolid.256x256 | CPU | 6,841.4 | 1.867/px | - | 4.7 |  |
| Gdi.PatBltSolid.32x32 | CPU | 3,726.8 | 3.427/px | - | 4.7 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 11,719.4 | 1.090/px | - | 4.7 |  |
| Gdi.PatBltSolid.64x64 | CPU | 7,619.2 | 1.676/px | - | 4.7 |  |
| Gdi.PatBltSolid.8x8 | noisy | 321.4 | 39.737/px | - | 4.7 |  |
| Gdi.Polygon.128x128 | CPU | 1,354.5 | 9.428/px | - | - |  |
| Gdi.Polyline | n/a | - | 738.688/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 2,215.3 | 5.765/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 4,965.4 | 2.572/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.8 | 515.350/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 4,939.9 | 2.585/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 15.8 | 810.506/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 15.8 | 808.723/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.8 | 514.650/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 15.8 | 808.965/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 4,960.3 | 2.575/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 2,200.4 | 5.804/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 171.9 | 74.278/px | - | - |  |
| Gdi.ScrollDC | CPU | 171.8 | 74.347/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 639.7 | 19.962/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 638.3 | 20.007/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 639.7 | 19.962/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 638.4 | 20.005/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 4,260.3 | 2.998/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 4,249.7 | 3.005/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 4,313.7 | 2.960/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 4,286.2 | 2.979/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 9,217.1 | 1.386/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 9,208.1 | 1.387/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 9,293.2 | 1.374/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 9,226.7 | 1.384/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 32.7 | 390.505/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 27.2 | 469.967/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 33.1 | 385.885/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 27.6 | 462.201/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 106.9 | 119.439/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 285.6 | 44.721/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 106.9 | 119.495/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 289.4 | 44.121/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 5,496.3 | 2.323/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 9,491.8 | 1.345/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 373.7 | 34.177/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 552.6 | 23.109/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 26,983.706/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 17,802.228/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,085.022/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 16,418.888/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
