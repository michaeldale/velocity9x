# Verdicts: m-jjn1zz-r3e-v9x-vbe-shadow

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
| MemFill | 2,462.7 |
| MemCopy | 961.1 |
| DibToDibBlt | 1,756.7 |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Gdi.Align.Dx0.64x64 | HW? | 162.0 | 14.791/px | - | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 163.5 | 14.650/px | - | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 161.5 | 14.831/px | - | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 162.8 | 14.719/px | - | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 161.6 | 14.823/px | - | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 160.7 | 14.908/px | - | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 847.8 | 2.826/px | - | 10.7 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 644.9 | 3.715/px | - | 10.7 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 81.2 | 29.496/px | - | 10.7 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 857.9 | 2.792/px | - | 10.7 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 229.7 | 10.429/px | - | 10.7 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 976.8 | 2.452/px | - | 10.7 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 406.9 | 5.888/px | - | 10.7 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 27.9 | 85.732/px | - | 10.7 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 608.4 | 3.937/px | - | 11.8 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 476.3 | 5.030/px | - | 11.8 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 59.6 | 40.172/px | - | 11.8 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 576.0 | 4.159/px | - | 11.8 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 172.9 | 13.853/px | - | 11.8 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 627.5 | 3.818/px | - | 11.8 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 322.4 | 7.431/px | - | 11.8 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 18.8 | 127.469/px | - | 11.8 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 846.6 | 2.830/px | - | 11.7 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 615.0 | 3.895/px | - | 11.7 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 59.9 | 39.985/px | - | 11.7 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 807.7 | 2.966/px | - | 11.7 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 174.3 | 13.745/px | - | 11.7 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 893.9 | 2.680/px | - | 11.7 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 345.4 | 6.936/px | - | 11.7 |  |
| Gdi.BitBltScreenToBitmap.8x8 | noisy | 28.1 | 85.368/px | - | 11.7 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 205.2 | 11.672/px | - | 40.3 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 22.4 | 106.858/px | - | 40.3 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 384.9 | 6.223/px | - | 40.3 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 49.7 | 48.193/px | - | 40.3 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 102.4 | 23.390/px | - | 40.3 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 9.7 | 247.244/px | - | 40.3 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 624.0 | 3.839/px | - | 12.3 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 60.7 | 39.462/px | - | 12.3 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 790.7 | 3.029/px | - | 12.3 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 195.7 | 12.243/px | - | 12.3 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 919.2 | 2.606/px | - | 12.3 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 381.8 | 6.274/px | - | 12.3 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 21.8 | 110.129/px | - | 12.3 |  |
| Gdi.Ellipse.128x128 | CPU | 126.1 | 18.992/px | - | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 26,960.712/string | - | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 31,095.010/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 33,558.632/string | - | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 33,983.604/string | - | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 1,246.5 | 1.922/px | - | 12.1 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 652.0 | 3.674/px | - | 12.1 |  |
| Gdi.FillRect.16x16 | CPU | 68.0 | 35.222/px | - | 12.1 |  |
| Gdi.FillRect.256x256 | CPU | 858.7 | 2.790/px | - | 12.1 |  |
| Gdi.FillRect.32x32 | CPU | 203.2 | 11.791/px | - | 12.1 |  |
| Gdi.FillRect.512x512 | CPU | 1,004.6 | 2.384/px | - | 12.1 |  |
| Gdi.FillRect.64x64 | CPU | 394.6 | 6.071/px | - | 12.1 |  |
| Gdi.FillRect.8x8 | CPU | 21.8 | 109.706/px | - | 12.1 |  |
| Gdi.LineDiagonal30 | n/a | - | 19,385.742/line | - | - |  |
| Gdi.LineHorizontal | n/a | - | 5,692.099/line | - | - |  |
| Gdi.LineVertical | n/a | - | 15,972.933/line | - | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 1,268.3 | 1.889/px | - | 11.1 |  |
| Gdi.PatBltHatch.128x128 | CPU | 703.7 | 3.404/px | - | 11.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 59.3 | 40.366/px | - | 11.1 |  |
| Gdi.PatBltHatch.256x256 | CPU | 899.7 | 2.663/px | - | 11.1 |  |
| Gdi.PatBltHatch.32x32 | noisy | 238.8 | 10.033/px | - | 11.1 |  |
| Gdi.PatBltHatch.512x512 | CPU | 1,041.0 | 2.301/px | - | 11.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 446.9 | 5.360/px | - | 11.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 18.2 | 131.919/px | - | 11.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 1,265.7 | 1.893/px | - | 10.3 |  |
| Gdi.PatBltPattern.128x128 | CPU | 704.9 | 3.398/px | - | 10.3 |  |
| Gdi.PatBltPattern.16x16 | CPU | 80.1 | 29.916/px | - | 10.3 |  |
| Gdi.PatBltPattern.256x256 | CPU | 899.9 | 2.662/px | - | 10.3 |  |
| Gdi.PatBltPattern.32x32 | CPU | 237.5 | 10.087/px | - | 10.3 |  |
| Gdi.PatBltPattern.512x512 | CPU | 1,041.6 | 2.300/px | - | 10.3 |  |
| Gdi.PatBltPattern.64x64 | CPU | 446.0 | 5.371/px | - | 10.3 |  |
| Gdi.PatBltPattern.8x8 | CPU | 27.0 | 88.866/px | - | 10.3 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 1,251.4 | 1.914/px | - | 11.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 679.2 | 3.527/px | - | 11.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 64.8 | 36.994/px | - | 11.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 886.0 | 2.704/px | - | 11.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 195.0 | 12.285/px | - | 11.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 1,013.8 | 2.363/px | - | 11.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 436.2 | 5.491/px | - | 11.3 |  |
| Gdi.PatBltSolid.8x8 | CPU | 20.2 | 118.584/px | - | 11.3 |  |
| Gdi.Polygon.128x128 | CPU | 132.1 | 18.135/px | - | - |  |
| Gdi.Polyline | n/a | - | 1,034.451/line | - | - |  |
| Gdi.Rectangle.128x128 | CPU | 381.2 | 6.283/px | - | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 320.1 | 7.485/px | - | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 99.6 | 24.061/px | - | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 351.9 | 6.807/px | - | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 95.4 | 25.106/px | - | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 100.9 | 23.740/px | - | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 102.9 | 23.286/px | - | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 101.0 | 23.730/px | - | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 320.0 | 7.486/px | - | - |  |
| Gdi.RoundRect.128x128 | CPU | 257.0 | 9.320/px | - | - |  |
| Gdi.ScrollBitBlt | CPU | 1,186.8 | 2.018/px | - | - |  |
| Gdi.ScrollDC | CPU | 1,182.3 | 2.026/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 574.2 | 4.172/px | - | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 670.3 | 3.574/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 601.8 | 3.981/px | - | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 617.9 | 3.877/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 155.6 | 15.392/px | - | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 165.3 | 14.488/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 158.6 | 15.109/px | - | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 162.0 | 14.786/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 621.7 | 3.853/px | - | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 682.6 | 3.509/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 673.9 | 3.555/px | - | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 630.3 | 3.801/px | - | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 13.9 | 172.442/px | - | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 11.8 | 202.795/px | - | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 13.9 | 172.400/px | - | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 11.8 | 203.476/px | - | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 54.7 | 43.796/px | - | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 67.4 | 35.561/px | - | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 54.6 | 43.903/px | - | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 67.2 | 35.671/px | - | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 49.6 | 48.284/px | - | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 65.8 | 36.396/px | - | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 49.5 | 48.369/px | - | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 65.8 | 36.417/px | - | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 26,968.881/string | - | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 31,005.432/string | - | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 33,319.973/string | - | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 33,891.574/string | - | - |  |

## CLAIMED-NOT-DELIVERED

None.

## Problems

- no DBDD.INI
- no FbRead: screen-sourced operations cannot be proved hardware (D3)
