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
| FbWrite | 66.0 |
| FbRead | 35.5 |
| MemFill | 31,006.5 |
| MemCopy | 19,113.4 |
| DibToDibBlt | 8,684.0 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 231 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 878.1 | 7.272/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,796.2 | 3.555/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,445.5 | 0.611/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,416.3 | 4.508/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,219.8 | 2.876/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 11,946.2 | 0.534/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,625.1 | 0.437/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,290.1 | 0.621/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,922.2 | 3.322/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 14,218.8 | 0.449/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,701.3 | 0.382/px | - | 0.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 9,103.1 | 0.701/px | - | 0.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | HW? | 63.3 | 100.940/px | 66.0 | 276.4 | D4; not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | HW? | 74.3 | 85.941/px | 66.0 | 276.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 150.7 | 42.364/px | 66.0 | 276.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | HW? | 73.7 | 86.594/px | 66.0 | 276.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | HW? | 101.2 | 63.065/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 131.3 | 48.640/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 132.0 | 48.381/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 130.0 | 49.103/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 100.1 | 63.816/px | 66.0 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 131.3 | 48.618/px | 66.0 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 132.0 | 48.368/px | 66.0 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 129.6 | 49.268/px | 66.0 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 32.7 | 195.446/px | 35.5 | 3.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 35.4 | 180.554/px | 35.5 | 3.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.4 | 180.130/px | 35.5 | 3.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 35.3 | 180.789/px | 35.5 | 3.3 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 60.7 | 105.237/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 70.6 | 90.486/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 70.6 | 90.414/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 70.2 | 90.920/px | 35.5 | 1.2 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 105.6 | 60.459/px | 35.5 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 140.8 | 45.346/px | 35.5 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 141.6 | 45.096/px | 35.5 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 139.0 | 45.936/px | 35.5 | 2.4 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 22.4 | 284.709/px | 66.0 | -0.6 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.6 | 259.334/px | 66.0 | -0.6 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.6 | 259.593/px | 66.0 | -0.6 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.6 | 259.793/px | 66.0 | -0.6 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW | 158.7 | 40.239/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 2,420.1 | 2.638/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 2,609.9 | 2.447/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 1,151.2 | 5.546/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 51.9 | 123.121/px | 66.0 | 3.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 65.9 | 96.960/px | 66.0 | 3.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 66.1 | 96.642/px | 66.0 | 3.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 65.2 | 97.863/px | 66.0 | 3.3 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,514.9 | 2.539/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 11,329.0 | 0.564/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,538.0 | 0.439/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 10,660.9 | 0.599/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | HW? | 91.8 | 69.549/px | 66.0 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 131.2 | 48.667/px | 66.0 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 132.0 | 48.367/px | 66.0 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 128.9 | 49.522/px | 66.0 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 2,524.2 | 2.530/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 11,987.1 | 0.533/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 14,488.8 | 0.441/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 10,672.3 | 0.598/px | 35.5 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW | 155.7 | 41.021/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 2,423.0 | 2.635/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 2,610.4 | 2.446/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 1,152.7 | 5.539/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | HW? | 63.5 | 100.481/px | 66.0 | 275.6 | D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | HW | 74.4 | 85.879/px | 66.0 | 275.6 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 150.5 | 42.436/px | 66.0 | 275.6 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | HW | 73.7 | 86.649/px | 66.0 | 275.6 | D3; D4; not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,768.8 | 2.306/px | - | 0.7 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 14,425.4 | 0.443/px | - | 0.7 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 19,091.5 | 0.334/px | - | 0.7 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,419.0 | 0.414/px | - | 0.7 |  |
| Dd.ColorFill.Vid.16x16 | HW | 187.0 | 34.151/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 4,616.5 | 1.383/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 4,979.5 | 1.282/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 1,743.5 | 3.662/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 3,737.164/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,740.758/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,740.957/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 562.939/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 12,682,261.286/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 24.8 | 257.633/px | 66.0 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 23.6 | 270.600/px | 66.0 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 24.8 | 257.583/px | 66.0 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 23.6 | 270.885/px | 66.0 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 24.8 | 257.672/px | 66.0 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 24.8 | 257.784/px | 66.0 | - | D6 |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 65.2 | 97.891/px | 66.0 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 61.8 | 103.266/px | 66.0 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 65.5 | 97.481/px | 66.0 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 64.7 | 98.633/px | 66.0 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 65.7 | 97.208/px | 66.0 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 65.5 | 97.484/px | 66.0 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.800x600 | CPU | 65.7 | 97.114/px | 66.0 | 1.7 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 52.1 | 122.590/px | 66.0 | 1.7 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 65.6 | 97.351/px | 66.0 | 1.8 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 55.5 | 115.012/px | 66.0 | 1.8 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 65.5 | 97.509/px | 66.0 | 1.8 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 62.1 | 102.779/px | 66.0 | 1.8 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 65.6 | 97.290/px | 66.0 | 1.8 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 64.8 | 98.499/px | 66.0 | 1.8 |  |
| Gdi.BitBltDibToScreen.800x600 | CPU | 65.7 | 97.132/px | 66.0 | 1.8 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 37.6 | 169.711/px | 66.0 | 1.8 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.4 | 180.136/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 33.3 | 191.717/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 35.2 | 181.264/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 34.9 | 182.994/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 35.4 | 180.390/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 34.5 | 184.957/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToBitmap.800x600 | CPU | 35.4 | 180.255/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 28.3 | 225.246/px | 35.5 | 3.7 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 24.8 | 257.913/px | 66.0 | 2.7 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.6 | 270.972/px | 66.0 | 2.7 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 24.9 | 256.225/px | 66.0 | 2.7 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 24.5 | 261.081/px | 66.0 | 2.7 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.7 | 258.189/px | 66.0 | 2.7 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 21.3 | 300.075/px | 66.0 | 2.7 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 24.2 | 264.353/px | 66.0 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 22.6 | 282.430/px | 66.0 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 24.2 | 264.355/px | 66.0 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.8 | 268.605/px | 66.0 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 24.2 | 264.051/px | 66.0 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 24.0 | 265.846/px | 66.0 | 2.2 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 20.9 | 306.090/px | 66.0 | 2.2 |  |
| Gdi.Ellipse.128x128 | CPU | 59.5 | 107.228/px | 66.0 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 471,821.757/string | 66.0 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 150,263.113/string | 66.0 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 373,943.336/string | 66.0 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 95,841.169/string | 66.0 | - |  |
| Gdi.FillRect.128x128 | CPU | 65.7 | 97.185/px | 66.0 | 1.4 |  |
| Gdi.FillRect.16x16 | CPU | 59.1 | 108.110/px | 66.0 | 1.4 |  |
| Gdi.FillRect.256x256 | CPU | 65.5 | 97.444/px | 66.0 | 1.4 |  |
| Gdi.FillRect.32x32 | CPU | 63.9 | 99.914/px | 66.0 | 1.4 |  |
| Gdi.FillRect.512x512 | CPU | 65.7 | 97.162/px | 66.0 | 1.4 |  |
| Gdi.FillRect.64x64 | CPU | 65.3 | 97.708/px | 66.0 | 1.4 |  |
| Gdi.FillRect.800x600 | CPU | 65.7 | 97.173/px | 66.0 | 1.4 | D2 0.00 |
| Gdi.FillRect.8x8 | CPU | 44.1 | 144.893/px | 66.0 | 1.4 |  |
| Gdi.LineDiagonal30 | n/a | - | 19,138.714/line | 66.0 | - |  |
| Gdi.LineHorizontal | n/a | - | 21,784.629/line | 66.0 | - |  |
| Gdi.LineVertical | n/a | - | 21,341.251/line | 66.0 | - |  |
| Gdi.PatBltHatch.128x128 | CPU | 65.7 | 97.190/px | 66.0 | 1.3 |  |
| Gdi.PatBltHatch.16x16 | CPU | 59.8 | 106.842/px | 66.0 | 1.3 |  |
| Gdi.PatBltHatch.256x256 | CPU | 65.5 | 97.476/px | 66.0 | 1.3 |  |
| Gdi.PatBltHatch.32x32 | CPU | 64.2 | 99.529/px | 66.0 | 1.3 |  |
| Gdi.PatBltHatch.512x512 | CPU | 65.7 | 97.200/px | 66.0 | 1.3 |  |
| Gdi.PatBltHatch.64x64 | CPU | 65.4 | 97.681/px | 66.0 | 1.3 |  |
| Gdi.PatBltHatch.800x600 | CPU | 65.7 | 97.234/px | 66.0 | 1.3 |  |
| Gdi.PatBltHatch.8x8 | CPU | 46.6 | 136.899/px | 66.0 | 1.3 |  |
| Gdi.PatBltPattern.128x128 | CPU | 65.7 | 97.191/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.16x16 | CPU | 59.7 | 106.888/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.256x256 | CPU | 65.5 | 97.486/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.32x32 | CPU | 64.2 | 99.533/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.512x512 | CPU | 65.7 | 97.236/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.64x64 | CPU | 65.2 | 97.932/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.800x600 | CPU | 65.7 | 97.146/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.8x8 | CPU | 46.7 | 136.665/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.128x128 | CPU | 65.4 | 97.601/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.16x16 | CPU | 63.1 | 101.169/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.256x256 | CPU | 65.4 | 97.587/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.32x32 | CPU | 65.1 | 98.150/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.512x512 | CPU | 65.6 | 97.308/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.64x64 | CPU | 65.7 | 97.239/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.800x600 | CPU | 65.8 | 97.106/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.8x8 | CPU | 55.6 | 114.914/px | 66.0 | 1.2 |  |
| Gdi.Polygon.128x128 | CPU | 58.1 | 109.822/px | 66.0 | - |  |
| Gdi.Polyline | n/a | - | 19,558.903/line | 66.0 | - |  |
| Gdi.Rectangle.128x128 | CPU | 62.8 | 101.642/px | 66.0 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 65.4 | 97.634/px | 66.0 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.5 | 260.330/px | 66.0 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 65.7 | 97.121/px | 66.0 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 14.9 | 429.484/px | 66.0 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 14.9 | 428.302/px | 66.0 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.7 | 258.208/px | 66.0 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 14.9 | 428.362/px | 66.0 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 65.4 | 97.695/px | 66.0 | - |  |
| Gdi.RoundRect.128x128 | CPU | 61.8 | 103.387/px | 66.0 | - |  |
| Gdi.ScrollBitBlt | CPU | 24.0 | 266.204/px | 66.0 | - |  |
| Gdi.ScrollDC | CPU | 24.0 | 266.177/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.400x300 | CPU | 65.5 | 97.508/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.800x600 | CPU | 65.7 | 97.165/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.400x300 | CPU | 65.6 | 97.382/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.800x600 | CPU | 65.7 | 97.178/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.400x300 | CPU | 65.6 | 97.377/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.800x600 | CPU | 65.6 | 97.308/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.400x300 | CPU | 65.6 | 97.364/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.800x600 | CPU | 64.3 | 99.357/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.400x300 | CPU | 65.6 | 97.387/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.800x600 | CPU | 65.7 | 97.171/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.400x300 | CPU | 65.6 | 97.331/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.800x600 | CPU | 65.6 | 97.276/px | 66.0 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 6.6 | 970.728/px | 66.0 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 6.5 | 985.010/px | 66.0 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 6.6 | 969.764/px | 66.0 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 6.5 | 986.728/px | 66.0 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 36.3 | 175.860/px | 66.0 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 37.3 | 171.026/px | 66.0 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 36.3 | 175.966/px | 66.0 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 37.3 | 171.134/px | 66.0 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 48.6 | 131.484/px | 66.0 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 51.6 | 123.706/px | 66.0 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 48.6 | 131.459/px | 66.0 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 51.6 | 123.681/px | 66.0 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 472,153.332/string | 66.0 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 149,904.219/string | 66.0 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 373,696.925/string | 66.0 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 95,933.390/string | 66.0 | - |  |

## CLAIMED-NOT-DELIVERED

None.
