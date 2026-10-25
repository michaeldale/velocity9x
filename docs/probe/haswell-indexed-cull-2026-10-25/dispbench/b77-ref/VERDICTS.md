# Verdicts: w9qi5a83-hd4600-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 800x600x16, build 31b28a4, driver Velocity9x Intel HD Graphics 4600 (Haswell)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 65.7 |
| FbRead | 35.5 |
| MemFill | 31,034.2 |
| MemCopy | 20,044.9 |
| DibToDibBlt | 8,663.3 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 231 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 876.3 | 7.287/px | - | 19.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,776.3 | 3.595/px | - | 19.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,444.5 | 0.611/px | - | 19.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,418.5 | 4.501/px | - | 19.4 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,221.3 | 2.875/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 12,073.5 | 0.529/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,717.6 | 0.434/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,344.0 | 0.617/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,916.2 | 3.332/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 14,390.1 | 0.444/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,642.1 | 0.384/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 9,103.4 | 0.701/px | - | 0.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | HW? | 63.3 | 100.893/px | 65.7 | 277.5 | D4; not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | HW? | 74.2 | 86.055/px | 65.7 | 277.5 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 151.0 | 42.298/px | 65.7 | 277.5 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | HW? | 73.7 | 86.658/px | 65.7 | 277.5 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | HW? | 101.0 | 63.228/px | 65.7 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 131.4 | 48.607/px | 65.7 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 131.9 | 48.394/px | 65.7 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 130.0 | 49.109/px | 65.7 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 99.8 | 63.963/px | 65.7 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 131.3 | 48.613/px | 65.7 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 132.0 | 48.380/px | 65.7 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 129.7 | 49.248/px | 65.7 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 32.3 | 197.878/px | 35.5 | 1.4 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 35.4 | 180.215/px | 35.5 | 1.4 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.4 | 180.128/px | 35.5 | 1.4 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 35.3 | 180.795/px | 35.5 | 1.4 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 60.6 | 105.422/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 70.6 | 90.451/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 70.6 | 90.413/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 69.8 | 91.447/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 105.6 | 60.454/px | 35.5 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 140.7 | 45.381/px | 35.5 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 141.6 | 45.092/px | 35.5 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 139.0 | 45.935/px | 35.5 | 2.6 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 22.4 | 284.718/px | 65.7 | -9.7 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.6 | 259.183/px | 65.7 | -9.7 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.5 | 260.901/px | 65.7 | -9.7 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.6 | 259.821/px | 65.7 | -9.7 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW | 158.2 | 40.364/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 2,423.2 | 2.635/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 2,611.0 | 2.445/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 1,150.4 | 5.551/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 51.7 | 123.588/px | 65.7 | 2.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 65.9 | 96.893/px | 65.7 | 2.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 66.0 | 96.718/px | 65.7 | 2.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 65.2 | 97.937/px | 65.7 | 2.5 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,515.0 | 2.539/px | - | 0.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 11,480.0 | 0.556/px | - | 0.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,576.2 | 0.438/px | - | 0.8 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 10,674.3 | 0.598/px | - | 0.8 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | HW? | 101.5 | 62.937/px | 65.7 | 2.0 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 131.4 | 48.577/px | 65.7 | 2.0 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 132.0 | 48.376/px | 65.7 | 2.0 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 130.1 | 49.076/px | 65.7 | 2.0 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 2,524.9 | 2.529/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 11,995.5 | 0.532/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 14,444.9 | 0.442/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 10,670.4 | 0.598/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW | 154.1 | 41.444/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 2,422.2 | 2.636/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 2,609.2 | 2.447/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 1,150.6 | 5.550/px | 65.7 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | HW? | 63.5 | 100.547/px | 65.7 | 276.7 | D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | HW | 74.3 | 85.976/px | 65.7 | 276.7 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 150.7 | 42.358/px | 65.7 | 276.7 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | HW | 73.7 | 86.655/px | 65.7 | 276.7 | D3; D4; not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,772.1 | 2.303/px | - | 0.4 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 16,133.7 | 0.396/px | - | 0.4 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 18,922.4 | 0.337/px | - | 0.4 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,445.0 | 0.413/px | - | 0.4 |  |
| Dd.ColorFill.Vid.16x16 | HW | 186.7 | 34.208/px | 65.7 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 4,614.7 | 1.384/px | 65.7 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 4,977.0 | 1.283/px | 65.7 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 1,740.2 | 3.669/px | 65.7 | 2.8 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 3,769.597/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,769.041/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,768.644/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 580.624/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 12,686,949.000/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 24.8 | 257.412/px | 65.7 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 23.6 | 270.876/px | 65.7 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 24.8 | 257.497/px | 65.7 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 23.6 | 270.670/px | 65.7 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 24.1 | 264.481/px | 65.7 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 24.8 | 257.641/px | 65.7 | - | D6 |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 65.7 | 97.163/px | 65.7 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 61.7 | 103.430/px | 65.7 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 65.3 | 97.758/px | 65.7 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 64.7 | 98.676/px | 65.7 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 65.7 | 97.216/px | 65.7 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 65.5 | 97.480/px | 65.7 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.800x600 | CPU | 65.7 | 97.132/px | 65.7 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 51.8 | 123.151/px | 65.7 | 1.7 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 65.6 | 97.343/px | 65.7 | 2.1 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 55.6 | 114.911/px | 65.7 | 2.1 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 65.5 | 97.512/px | 65.7 | 2.1 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 62.1 | 102.821/px | 65.7 | 2.1 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 65.7 | 97.200/px | 65.7 | 2.1 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 64.8 | 98.490/px | 65.7 | 2.1 |  |
| Gdi.BitBltDibToScreen.800x600 | CPU | 65.1 | 98.025/px | 65.7 | 2.1 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 37.6 | 169.874/px | 65.7 | 2.1 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.4 | 180.392/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 33.3 | 191.736/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 35.3 | 180.843/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 34.9 | 183.078/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 35.4 | 180.559/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.3 | 181.016/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToBitmap.800x600 | CPU | 35.4 | 180.219/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 28.4 | 225.144/px | 35.5 | 1.2 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 24.7 | 258.069/px | 65.7 | 2.9 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.0 | 277.170/px | 65.7 | 2.9 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 24.9 | 256.446/px | 65.7 | 2.9 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 24.3 | 262.830/px | 65.7 | 2.9 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.7 | 258.266/px | 65.7 | 2.9 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 21.3 | 300.144/px | 65.7 | 2.9 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 24.1 | 264.406/px | 65.7 | 2.7 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 22.6 | 282.535/px | 65.7 | 2.7 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 24.2 | 264.376/px | 65.7 | 2.7 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.8 | 268.834/px | 65.7 | 2.7 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 24.2 | 263.958/px | 65.7 | 2.7 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 24.0 | 266.084/px | 65.7 | 2.7 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 20.9 | 306.228/px | 65.7 | 2.7 |  |
| Gdi.Ellipse.128x128 | CPU | 59.5 | 107.244/px | 65.7 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 489,788.498/string | 65.7 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 150,246.862/string | 65.7 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 374,450.980/string | 65.7 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 95,773.409/string | 65.7 | - |  |
| Gdi.FillRect.128x128 | CPU | 65.7 | 97.176/px | 65.7 | 1.6 |  |
| Gdi.FillRect.16x16 | CPU | 54.7 | 116.810/px | 65.7 | 1.6 |  |
| Gdi.FillRect.256x256 | CPU | 65.5 | 97.437/px | 65.7 | 1.6 |  |
| Gdi.FillRect.32x32 | CPU | 62.6 | 101.977/px | 65.7 | 1.6 |  |
| Gdi.FillRect.512x512 | CPU | 65.7 | 97.190/px | 65.7 | 1.6 |  |
| Gdi.FillRect.64x64 | CPU | 65.3 | 97.803/px | 65.7 | 1.6 |  |
| Gdi.FillRect.800x600 | CPU | 65.8 | 97.107/px | 65.7 | 1.6 | D2 -0.02 |
| Gdi.FillRect.8x8 | CPU | 43.1 | 148.249/px | 65.7 | 1.6 |  |
| Gdi.LineDiagonal30 | n/a | - | 19,135.403/line | 65.7 | - |  |
| Gdi.LineHorizontal | n/a | - | 21,781.263/line | 65.7 | - |  |
| Gdi.LineVertical | n/a | - | 21,326.850/line | 65.7 | - |  |
| Gdi.PatBltHatch.128x128 | CPU | 65.6 | 97.311/px | 65.7 | 1.4 |  |
| Gdi.PatBltHatch.16x16 | CPU | 59.7 | 106.873/px | 65.7 | 1.4 |  |
| Gdi.PatBltHatch.256x256 | CPU | 65.5 | 97.482/px | 65.7 | 1.4 |  |
| Gdi.PatBltHatch.32x32 | CPU | 64.1 | 99.566/px | 65.7 | 1.4 |  |
| Gdi.PatBltHatch.512x512 | CPU | 65.7 | 97.209/px | 65.7 | 1.4 |  |
| Gdi.PatBltHatch.64x64 | CPU | 65.3 | 97.714/px | 65.7 | 1.4 |  |
| Gdi.PatBltHatch.800x600 | CPU | 65.7 | 97.139/px | 65.7 | 1.4 |  |
| Gdi.PatBltHatch.8x8 | CPU | 46.6 | 137.071/px | 65.7 | 1.4 |  |
| Gdi.PatBltPattern.128x128 | CPU | 65.7 | 97.198/px | 65.7 | 1.2 |  |
| Gdi.PatBltPattern.16x16 | CPU | 59.7 | 107.001/px | 65.7 | 1.2 |  |
| Gdi.PatBltPattern.256x256 | CPU | 65.5 | 97.466/px | 65.7 | 1.2 |  |
| Gdi.PatBltPattern.32x32 | CPU | 64.1 | 99.565/px | 65.7 | 1.2 |  |
| Gdi.PatBltPattern.512x512 | CPU | 65.7 | 97.206/px | 65.7 | 1.2 |  |
| Gdi.PatBltPattern.64x64 | CPU | 65.4 | 97.675/px | 65.7 | 1.2 |  |
| Gdi.PatBltPattern.800x600 | CPU | 65.7 | 97.149/px | 65.7 | 1.2 |  |
| Gdi.PatBltPattern.8x8 | CPU | 46.6 | 137.096/px | 65.7 | 1.2 |  |
| Gdi.PatBltSolid.128x128 | CPU | 65.8 | 97.092/px | 65.7 | 0.9 |  |
| Gdi.PatBltSolid.16x16 | CPU | 63.0 | 101.319/px | 65.7 | 0.9 |  |
| Gdi.PatBltSolid.256x256 | CPU | 65.5 | 97.466/px | 65.7 | 0.9 |  |
| Gdi.PatBltSolid.32x32 | CPU | 65.1 | 98.010/px | 65.7 | 0.9 |  |
| Gdi.PatBltSolid.512x512 | CPU | 65.7 | 97.171/px | 65.7 | 0.9 |  |
| Gdi.PatBltSolid.64x64 | CPU | 65.7 | 97.248/px | 65.7 | 0.9 |  |
| Gdi.PatBltSolid.800x600 | CPU | 65.8 | 97.108/px | 65.7 | 0.9 |  |
| Gdi.PatBltSolid.8x8 | CPU | 55.5 | 115.097/px | 65.7 | 0.9 |  |
| Gdi.Polygon.128x128 | CPU | 58.1 | 109.914/px | 65.7 | - |  |
| Gdi.Polyline | n/a | - | 19,568.574/line | 65.7 | - |  |
| Gdi.Rectangle.128x128 | CPU | 62.7 | 101.764/px | 65.7 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 65.7 | 97.136/px | 65.7 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.6 | 259.182/px | 65.7 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 65.7 | 97.116/px | 65.7 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 14.9 | 429.275/px | 65.7 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 14.9 | 428.914/px | 65.7 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.7 | 258.351/px | 65.7 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 14.9 | 428.439/px | 65.7 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 65.8 | 97.107/px | 65.7 | - |  |
| Gdi.RoundRect.128x128 | CPU | 61.8 | 103.374/px | 65.7 | - |  |
| Gdi.ScrollBitBlt | CPU | 24.0 | 266.003/px | 65.7 | - |  |
| Gdi.ScrollDC | CPU | 24.1 | 265.029/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.400x300 | CPU | 65.6 | 97.377/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.800x600 | CPU | 65.7 | 97.166/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.400x300 | CPU | 65.6 | 97.329/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.800x600 | CPU | 65.7 | 97.198/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.400x300 | CPU | 65.5 | 97.415/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.800x600 | CPU | 65.7 | 97.229/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.400x300 | CPU | 65.0 | 98.258/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.800x600 | CPU | 65.7 | 97.187/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.400x300 | CPU | 65.5 | 97.458/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.800x600 | CPU | 65.6 | 97.265/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.400x300 | CPU | 65.6 | 97.326/px | 65.7 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.800x600 | CPU | 65.7 | 97.158/px | 65.7 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 6.6 | 970.364/px | 65.7 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 6.5 | 985.798/px | 65.7 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 6.6 | 969.925/px | 65.7 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 6.5 | 985.615/px | 65.7 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 36.3 | 175.969/px | 65.7 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 37.3 | 171.037/px | 65.7 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 36.2 | 176.173/px | 65.7 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 37.3 | 171.048/px | 65.7 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 48.4 | 131.817/px | 65.7 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 51.6 | 123.824/px | 65.7 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 48.3 | 132.235/px | 65.7 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 51.6 | 123.784/px | 65.7 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 471,919.428/string | 65.7 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 149,938.826/string | 65.7 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 373,841.602/string | 65.7 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 95,825.904/string | 65.7 | - |  |

## CLAIMED-NOT-DELIVERED

None.
