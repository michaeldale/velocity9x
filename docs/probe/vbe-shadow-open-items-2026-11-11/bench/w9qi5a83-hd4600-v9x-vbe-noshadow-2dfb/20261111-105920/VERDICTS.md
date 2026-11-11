# Verdicts: w9qi5a83-hd4600-v9x-vbe-noshadow-2dfb

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
| MemFill | 31,070.8 |
| MemCopy | 20,270.7 |
| DibToDibBlt | 8,678.1 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 25.3 | 252.576/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 24.1 | 264.714/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 25.3 | 252.547/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 24.1 | 264.866/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 25.2 | 253.166/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 25.2 | 253.093/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 11,244.5 | 0.568/px | - | 2.2 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 8,328.7 | 0.767/px | - | 2.2 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 476.8 | 13.391/px | - | 2.2 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 4,287.4 | 1.489/px | - | 2.2 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,865.8 | 3.422/px | - | 2.2 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 7,438.5 | 0.858/px | - | 2.2 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 4,734.9 | 1.349/px | - | 2.2 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 131.0 | 48.735/px | - | 2.2 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 4,388.7 | 1.455/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 3,289.7 | 1.941/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 190.9 | 33.453/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 4,098.1 | 1.558/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 703.3 | 9.078/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 4,355.5 | 1.466/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,902.5 | 3.356/px | - | 2.5 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 51.5 | 123.894/px | - | 2.5 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 226.7 | 28.163/px | - | 204.0 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.4 | 180.461/px | - | 204.0 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 32.3 | 197.823/px | - | 204.0 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 148.0 | 43.155/px | - | 204.0 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 34.6 | 184.323/px | - | 204.0 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 192.3 | 33.211/px | - | 204.0 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.3 | 181.016/px | - | 204.0 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 28.3 | 225.255/px | - | 204.0 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 25.2 | 253.588/px | - | 230.2 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.3 | 274.216/px | - | 230.2 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 103.1 | 61.924/px | - | 230.2 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 23.7 | 269.148/px | - | 230.2 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.6 | 259.896/px | - | 230.2 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 20.4 | 312.888/px | - | 230.2 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 23.7 | 269.172/px | - | 1.6 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 22.3 | 286.940/px | - | 1.6 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 23.8 | 268.149/px | - | 1.6 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.4 | 272.736/px | - | 1.6 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 23.8 | 268.171/px | - | 1.6 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 23.8 | 268.300/px | - | 1.6 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 20.3 | 314.158/px | - | 1.6 |  |
| Gdi.Ellipse.128x128 | CPU | 770.8 | 8.284/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 19,809.786/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 14,268.941/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 18,453.569/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 14,506.485/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 11,268.9 | 0.567/px | - | 3.0 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU-fast | 6,845.2 | 0.933/px | - | 3.0 |  |
| Gdi.FillRect.16x16 | CPU | 329.1 | 19.401/px | - | 3.0 |  |
| Gdi.FillRect.256x256 | CPU | 4,210.8 | 1.516/px | - | 3.0 |  |
| Gdi.FillRect.32x32 | CPU | 989.7 | 6.452/px | - | 3.0 |  |
| Gdi.FillRect.512x512 | CPU-fast | 7,430.2 | 0.859/px | - | 3.0 |  |
| Gdi.FillRect.64x64 | CPU | 3,042.3 | 2.099/px | - | 3.0 |  |
| Gdi.FillRect.8x8 | CPU | 88.2 | 72.408/px | - | 3.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 4,826.614/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 3,734.167/line | - | - |  |
| Gdi.LineVertical | n/a | - | 6,990.572/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 10,319.5 | 0.619/px | - | 1.1 |  |
| Gdi.PatBltHatch.128x128 | CPU-fast | 7,546.1 | 0.846/px | - | 1.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 384.0 | 16.630/px | - | 1.1 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 9,647.0 | 0.662/px | - | 1.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 1,464.5 | 4.360/px | - | 1.1 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 9,897.2 | 0.645/px | - | 1.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 4,107.2 | 1.555/px | - | 1.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 105.8 | 60.357/px | - | 1.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 10,315.6 | 0.619/px | - | 1.1 |  |
| Gdi.PatBltPattern.128x128 | CPU-fast | 7,545.2 | 0.846/px | - | 1.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 399.2 | 15.994/px | - | 1.1 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 9,646.6 | 0.662/px | - | 1.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 1,460.7 | 4.371/px | - | 1.1 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 9,831.7 | 0.649/px | - | 1.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 4,110.2 | 1.554/px | - | 1.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 105.7 | 60.428/px | - | 1.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 11,313.7 | 0.564/px | - | 2.0 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 9,103.8 | 0.701/px | - | 2.0 |  |
| Gdi.PatBltSolid.16x16 | CPU | 570.9 | 11.184/px | - | 2.0 |  |
| Gdi.PatBltSolid.256x256 | CPU | 4,295.4 | 1.487/px | - | 2.0 |  |
| Gdi.PatBltSolid.32x32 | CPU | 2,238.2 | 2.853/px | - | 2.0 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 7,501.6 | 0.851/px | - | 2.0 |  |
| Gdi.PatBltSolid.64x64 | CPU | 5,390.8 | 1.184/px | - | 2.0 |  |
| Gdi.PatBltSolid.8x8 | CPU | 161.0 | 39.667/px | - | 2.0 |  |
| Gdi.Polygon.128x128 | CPU | 1,087.6 | 5.871/px | - | - |  |
| Gdi.Polyline | n/a | - | 657.248/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 3,281.0 | 1.946/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,540.3 | 2.514/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.5 | 260.394/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,521.3 | 2.532/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 14.3 | 446.499/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 13.8 | 462.795/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.6 | 259.750/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 14.3 | 445.261/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,536.3 | 2.518/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,571.1 | 4.064/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 153.2 | 41.681/px | - | - |  |
| Gdi.ScrollDC | CPU | 153.2 | 41.673/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 4,240.9 | 1.506/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 4,169.5 | 1.531/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 4,264.4 | 1.497/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 4,192.2 | 1.523/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 741.1 | 8.616/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 739.2 | 8.638/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 741.6 | 8.610/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 738.4 | 8.648/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 3,340.4 | 1.912/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 3,327.0 | 1.919/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 3,375.9 | 1.891/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 3,328.2 | 1.919/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 18.7 | 340.566/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 7.2 | 890.312/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 18.7 | 340.577/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 7.2 | 891.114/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 80.3 | 79.524/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 84.8 | 75.275/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 80.4 | 79.455/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 84.8 | 75.282/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 182.3 | 35.028/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 230.3 | 27.729/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 182.1 | 35.059/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 230.8 | 27.671/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 19,715.994/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 14,238.679/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 18,319.050/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 14,045.169/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
