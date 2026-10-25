# Verdicts: w9qi2be2-gma950-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x576x16, build 31b28a4, driver Velocity9x Intel GMA 950 (945GSE)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 105.5 |
| FbRead | 25.6 |
| MemFill | 1,274.8 |
| MemCopy | 903.6 |
| DibToDibBlt | 876.9 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 58,028.0 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 34 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 89.6 | 37.101/px | - | 127.5 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 253.6 | 13.110/px | - | 127.5 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 1,149.4 | 2.893/px | - | 127.5 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 220.9 | 15.049/px | - | 127.5 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 330.9 | 10.047/px | - | -1.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 1,267.8 | 2.623/px | - | -1.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 1,185.6 | 2.805/px | - | -1.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 1,237.3 | 2.687/px | - | -1.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 309.4 | 10.746/px | - | -3.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 2,160.3 | 1.539/px | - | -3.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 1,694.0 | 1.963/px | - | -3.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 1,498.6 | 2.219/px | - | -3.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 43.7 | 76.001/px | 105.5 | 295.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 87.5 | 37.980/px | 105.5 | 295.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 237.1 | 14.026/px | 105.5 | 295.9 | D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 81.7 | 40.682/px | 105.5 | 295.9 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 64.9 | 51.239/px | 105.5 | 0.1 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 213.3 | 15.591/px | 105.5 | 0.1 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 208.3 | 15.961/px | 105.5 | 0.1 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 184.5 | 18.020/px | 105.5 | 0.1 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 62.2 | 53.452/px | 105.5 | 7.8 | D4; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 206.5 | 16.102/px | 105.5 | 7.8 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 209.9 | 15.838/px | 105.5 | 7.8 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 179.8 | 18.493/px | 105.5 | 7.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 18.6 | 178.367/px | 25.6 | 6.6 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 23.9 | 139.090/px | 25.6 | 6.6 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 23.9 | 139.012/px | 25.6 | 6.6 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 23.3 | 142.445/px | 25.6 | 6.6 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 32.8 | 101.305/px | 25.6 | 13.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 50.5 | 65.783/px | 25.6 | 13.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 51.1 | 65.008/px | 25.6 | 13.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 49.2 | 67.556/px | 25.6 | 13.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 45.7 | 72.769/px | 25.6 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 97.2 | 34.193/px | 25.6 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 96.7 | 34.397/px | 25.6 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 90.8 | 36.611/px | 25.6 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 14.4 | 231.248/px | 105.5 | -47.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 20.0 | 165.876/px | 105.5 | -47.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 19.5 | 170.493/px | 105.5 | -47.2 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 19.5 | 170.802/px | 105.5 | -47.2 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW? | 69.5 | 47.861/px | 105.5 | 5.2 | D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 1,078.7 | 3.082/px | 105.5 | 5.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 1,117.2 | 2.976/px | 105.5 | 5.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 776.3 | 4.283/px | 105.5 | 5.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 29.3 | 113.396/px | 105.5 | 7.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 68.2 | 48.768/px | 105.5 | 7.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 68.2 | 48.781/px | 105.5 | 7.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 63.2 | 52.576/px | 105.5 | 7.5 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 370.4 | 8.977/px | - | -1.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 1,299.9 | 2.558/px | - | -1.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 1,208.0 | 2.752/px | - | -1.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 1,270.7 | 2.617/px | - | -1.9 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 67.3 | 49.424/px | 105.5 | -1.1 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 212.1 | 15.676/px | 105.5 | -1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 205.9 | 16.151/px | 105.5 | -1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 181.0 | 18.365/px | 105.5 | -1.1 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 317.6 | 10.469/px | 25.6 | -0.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 1,232.4 | 2.698/px | 25.6 | -0.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 1,190.4 | 2.793/px | 25.6 | -0.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 1,170.7 | 2.840/px | 25.6 | -0.5 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW? | 73.3 | 45.366/px | 105.5 | 5.1 | D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 1,075.7 | 3.091/px | 105.5 | 5.1 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 1,115.6 | 2.981/px | 105.5 | 5.1 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 774.7 | 4.292/px | 105.5 | 5.1 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 44.4 | 74.888/px | 105.5 | 295.5 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 87.5 | 37.991/px | 105.5 | 295.5 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 236.2 | 14.075/px | 105.5 | 295.5 | D3; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 81.5 | 40.808/px | 105.5 | 295.5 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 396.5 | 8.385/px | - | 1.2 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 2,388.7 | 1.392/px | - | 1.2 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 2,399.4 | 1.386/px | - | 1.2 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 1,616.9 | 2.056/px | - | 1.2 |  |
| Dd.ColorFill.Vid.16x16 | noisy | 65.6 | 50.686/px | 105.5 | 4.9 | D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 2,068.5 | 1.607/px | 105.5 | 4.9 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 2,150.9 | 1.546/px | 105.5 | 4.9 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 1,181.6 | 2.814/px | 105.5 | 4.9 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 8,475.003/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 8,507.610/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 8,446.481/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 2,253.374/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 4,210,994.545/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | CPU | 20.8 | 159.566/px | 105.5 | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 17.3 | 192.037/px | 105.5 | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 20.8 | 159.756/px | 105.5 | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 17.3 | 192.037/px | 105.5 | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 20.8 | 159.515/px | 105.5 | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 20.9 | 159.471/px | 105.5 | - |  |
| Gdi.BitBltBitmapToScreen.1024x576 | CPU | 107.4 | 30.960/px | 105.5 | 5.0 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 105.2 | 31.594/px | 105.5 | 5.0 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 48.0 | 69.315/px | 105.5 | 5.0 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 107.8 | 30.848/px | 105.5 | 5.0 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 80.4 | 41.364/px | 105.5 | 5.0 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 107.3 | 30.984/px | 105.5 | 5.0 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 98.2 | 33.842/px | 105.5 | 5.0 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 18.3 | 182.078/px | 105.5 | 5.0 |  |
| Gdi.BitBltDibToScreen.1024x576 | CPU | 107.2 | 31.024/px | 105.5 | 11.8 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 103.5 | 32.119/px | 105.5 | 11.8 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 33.7 | 98.790/px | 105.5 | 11.8 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 106.2 | 31.296/px | 105.5 | 11.8 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 68.8 | 48.341/px | 105.5 | 11.8 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 107.5 | 30.929/px | 105.5 | 11.8 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 93.6 | 35.514/px | 105.5 | 11.8 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 11.3 | 295.207/px | 105.5 | 11.8 |  |
| Gdi.BitBltScreenToBitmap.1024x576 | CPU | 25.2 | 131.755/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 23.8 | 139.849/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 18.6 | 178.392/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 23.9 | 138.834/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 23.6 | 141.002/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 25.3 | 131.413/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 23.6 | 141.006/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 11.2 | 295.980/px | 25.6 | 60.1 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 21.0 | 158.498/px | 105.5 | 7.7 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 17.0 | 195.942/px | 105.5 | 7.7 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 21.2 | 156.963/px | 105.5 | 7.7 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 20.0 | 166.476/px | 105.5 | 7.7 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 20.9 | 159.408/px | 105.5 | 7.7 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 10.7 | 310.387/px | 105.5 | 7.7 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 19.7 | 168.449/px | 105.5 | 11.1 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 15.1 | 219.665/px | 105.5 | 11.1 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 19.9 | 166.929/px | 105.5 | 11.1 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 18.5 | 180.166/px | 105.5 | 11.1 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 20.0 | 166.544/px | 105.5 | 11.1 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 19.5 | 170.353/px | 105.5 | 11.1 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 10.6 | 314.208/px | 105.5 | 11.1 |  |
| Gdi.Ellipse.128x128 | CPU | 45.9 | 72.422/px | 105.5 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 185,618.048/string | 105.5 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 105,304.868/string | 105.5 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 153,262.013/string | 105.5 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 80,893.052/string | 105.5 | - |  |
| Gdi.FillRect.1024x576 | CPU | 108.9 | 30.528/px | 105.5 | 7.3 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 106.4 | 31.262/px | 105.5 | 7.3 |  |
| Gdi.FillRect.16x16 | CPU | 43.4 | 76.621/px | 105.5 | 7.3 |  |
| Gdi.FillRect.256x256 | CPU | 108.3 | 30.710/px | 105.5 | 7.3 |  |
| Gdi.FillRect.32x32 | CPU | 78.9 | 42.149/px | 105.5 | 7.3 |  |
| Gdi.FillRect.512x512 | CPU | 108.8 | 30.558/px | 105.5 | 7.3 |  |
| Gdi.FillRect.64x64 | CPU | 99.3 | 33.492/px | 105.5 | 7.3 |  |
| Gdi.FillRect.8x8 | CPU | 15.0 | 221.627/px | 105.5 | 7.3 |  |
| Gdi.LineDiagonal30 | n/a | - | 14,017.294/line | 105.5 | - |  |
| Gdi.LineHorizontal | n/a | - | 14,717.567/line | 105.5 | - |  |
| Gdi.LineVertical | n/a | - | 15,077.270/line | 105.5 | - |  |
| Gdi.PatBltHatch.1024x576 | CPU | 108.8 | 30.567/px | 105.5 | 6.8 |  |
| Gdi.PatBltHatch.128x128 | CPU | 105.7 | 31.468/px | 105.5 | 6.8 |  |
| Gdi.PatBltHatch.16x16 | CPU | 46.0 | 72.264/px | 105.5 | 6.8 |  |
| Gdi.PatBltHatch.256x256 | CPU | 107.6 | 30.891/px | 105.5 | 6.8 |  |
| Gdi.PatBltHatch.32x32 | CPU | 80.1 | 41.536/px | 105.5 | 6.8 |  |
| Gdi.PatBltHatch.512x512 | CPU | 108.1 | 30.767/px | 105.5 | 6.8 |  |
| Gdi.PatBltHatch.64x64 | CPU | 98.2 | 33.860/px | 105.5 | 6.8 |  |
| Gdi.PatBltHatch.8x8 | CPU | 17.3 | 192.065/px | 105.5 | 6.8 |  |
| Gdi.PatBltPattern.1024x576 | CPU | 108.7 | 30.579/px | 105.5 | 7.6 |  |
| Gdi.PatBltPattern.128x128 | CPU | 105.7 | 31.459/px | 105.5 | 7.6 |  |
| Gdi.PatBltPattern.16x16 | CPU | 45.8 | 72.549/px | 105.5 | 7.6 |  |
| Gdi.PatBltPattern.256x256 | CPU | 108.0 | 30.785/px | 105.5 | 7.6 |  |
| Gdi.PatBltPattern.32x32 | CPU | 80.3 | 41.410/px | 105.5 | 7.6 |  |
| Gdi.PatBltPattern.512x512 | CPU | 108.8 | 30.568/px | 105.5 | 7.6 |  |
| Gdi.PatBltPattern.64x64 | CPU | 98.8 | 33.640/px | 105.5 | 7.6 |  |
| Gdi.PatBltPattern.8x8 | CPU | 17.3 | 192.250/px | 105.5 | 7.6 |  |
| Gdi.PatBltSolid.1024x576 | CPU | 108.7 | 30.597/px | 105.5 | 4.1 |  |
| Gdi.PatBltSolid.128x128 | CPU | 107.2 | 31.005/px | 105.5 | 4.1 |  |
| Gdi.PatBltSolid.16x16 | CPU | 59.1 | 56.226/px | 105.5 | 4.1 |  |
| Gdi.PatBltSolid.256x256 | CPU | 108.5 | 30.638/px | 105.5 | 4.1 |  |
| Gdi.PatBltSolid.32x32 | CPU | 90.0 | 36.932/px | 105.5 | 4.1 |  |
| Gdi.PatBltSolid.512x512 | CPU | 108.8 | 30.562/px | 105.5 | 4.1 |  |
| Gdi.PatBltSolid.64x64 | CPU | 103.5 | 32.135/px | 105.5 | 4.1 |  |
| Gdi.PatBltSolid.8x8 | CPU | 25.0 | 133.135/px | 105.5 | 4.1 |  |
| Gdi.Polygon.128x128 | CPU | 63.0 | 52.807/px | 105.5 | - |  |
| Gdi.Polyline | n/a | - | 6,726.435/line | 105.5 | - |  |
| Gdi.Rectangle.128x128 | CPU | 100.3 | 33.151/px | 105.5 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 101.6 | 32.716/px | 105.5 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 19.4 | 171.431/px | 105.5 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 101.4 | 32.784/px | 105.5 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 11.1 | 298.442/px | 105.5 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 11.0 | 302.171/px | 105.5 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 20.8 | 159.783/px | 105.5 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 11.0 | 302.044/px | 105.5 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 101.7 | 32.708/px | 105.5 | - |  |
| Gdi.RoundRect.128x128 | CPU | 75.2 | 44.240/px | 105.5 | - |  |
| Gdi.ScrollBitBlt | CPU | 21.2 | 156.862/px | 105.5 | - |  |
| Gdi.ScrollDC | CPU | 21.2 | 156.942/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x576 | CPU | 106.6 | 31.189/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x288 | CPU | 107.4 | 30.963/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x576 | CPU | 106.6 | 31.205/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x288 | CPU | 107.2 | 31.024/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x576 | CPU | 105.6 | 31.494/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x288 | CPU | 106.0 | 31.377/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x576 | CPU | 106.2 | 31.313/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x288 | CPU | 106.0 | 31.372/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x576 | CPU | 107.2 | 31.015/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x288 | CPU | 107.5 | 30.929/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x576 | CPU | 107.6 | 30.908/px | 105.5 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x288 | CPU | 107.5 | 30.919/px | 105.5 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 3.4 | 991.655/px | 105.5 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 3.2 | 1,041.801/px | 105.5 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 3.1 | 1,074.382/px | 105.5 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 3.0 | 1,125.870/px | 105.5 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 19.1 | 173.704/px | 105.5 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 20.8 | 159.957/px | 105.5 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 15.9 | 209.277/px | 105.5 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 17.0 | 195.923/px | 105.5 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 21.8 | 152.643/px | 105.5 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 20.3 | 163.690/px | 105.5 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 17.7 | 188.068/px | 105.5 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 25.9 | 128.242/px | 105.5 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 185,556.446/string | 105.5 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 105,309.952/string | 105.5 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 153,510.569/string | 105.5 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 81,019.463/string | 105.5 | - |  |

## CLAIMED-NOT-DELIVERED

None.
