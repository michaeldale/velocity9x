# Verdicts: t5740-gm45-b33-uncached

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x768x16, build 95c470c, driver Velocity9x Intel GMA 4500MHD (GM45)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 100.9 |
| FbRead | 27.2 |
| MemFill | 1,375.5 |
| MemCopy | 1,023.8 |
| DibToDibBlt | 803.5 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 60.1 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 60.1 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 95 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 87.1 | 38.196/px | - | 135.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 240.7 | 13.813/px | - | 135.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 1,140.0 | 2.917/px | - | 135.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 215.7 | 15.414/px | - | 135.4 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 314.6 | 10.570/px | - | 7.1 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 1,078.5 | 3.083/px | - | 7.1 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 1,322.6 | 2.514/px | - | 7.1 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 1,195.8 | 2.781/px | - | 7.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 296.7 | 11.207/px | - | -1.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 2,022.3 | 1.644/px | - | -1.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 1,761.4 | 1.888/px | - | -1.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 1,422.2 | 2.338/px | - | -1.7 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 32.5 | 102.443/px | 100.9 | 305.5 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 84.0 | 39.584/px | 100.9 | 305.5 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 221.6 | 15.007/px | 100.9 | 305.5 | D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 77.7 | 42.778/px | 100.9 | 305.5 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 59.8 | 55.599/px | 100.9 | 1.2 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 197.7 | 16.820/px | 100.9 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 194.5 | 17.098/px | 100.9 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 174.8 | 19.022/px | 100.9 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 57.7 | 57.600/px | 100.9 | 5.4 | D4; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 196.5 | 16.924/px | 100.9 | 5.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 195.5 | 17.004/px | 100.9 | 5.4 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 159.0 | 20.909/px | 100.9 | 5.4 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 16.9 | 197.103/px | 27.2 | 4.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 24.6 | 135.082/px | 27.2 | 4.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 24.6 | 135.365/px | 27.2 | 4.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 23.9 | 138.975/px | 27.2 | 4.2 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 32.3 | 102.886/px | 27.2 | 7.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 53.5 | 62.203/px | 27.2 | 7.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 53.7 | 61.962/px | 27.2 | 7.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 51.5 | 64.516/px | 27.2 | 7.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 44.8 | 74.299/px | 27.2 | 5.9 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 100.2 | 33.170/px | 27.2 | 5.9 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 100.5 | 33.099/px | 27.2 | 5.9 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 93.0 | 35.757/px | 27.2 | 5.9 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 13.2 | 252.580/px | 100.9 | -0.8 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 18.2 | 183.123/px | 100.9 | -0.8 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 18.1 | 183.872/px | 100.9 | -0.8 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 17.8 | 187.294/px | 100.9 | -0.8 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW? | 63.4 | 52.469/px | 100.9 | 6.2 | D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 1,353.0 | 2.457/px | 100.9 | 6.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 1,430.5 | 2.324/px | 100.9 | 6.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 830.1 | 4.006/px | 100.9 | 6.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 23.3 | 142.548/px | 100.9 | 18.4 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 68.3 | 48.695/px | 100.9 | 18.4 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 69.2 | 48.083/px | 100.9 | 18.4 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 61.5 | 54.028/px | 100.9 | 18.4 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 144.9 | 22.946/px | - | 8.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 1,081.2 | 3.075/px | - | 8.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 1,328.7 | 2.502/px | - | 8.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 921.9 | 3.607/px | - | 8.7 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | HW? | 60.7 | 54.816/px | 100.9 | 3.0 | D4; not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 196.0 | 16.964/px | 100.9 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 195.0 | 17.054/px | 100.9 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 176.7 | 18.818/px | 100.9 | 3.0 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 300.6 | 11.060/px | 27.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 1,241.9 | 2.677/px | 27.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 1,306.8 | 2.544/px | 27.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 1,138.7 | 2.920/px | 27.2 | 2.4 | D4,D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW? | 63.9 | 52.005/px | 100.9 | 6.0 | D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 1,353.2 | 2.457/px | 100.9 | 6.0 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 1,420.7 | 2.340/px | 100.9 | 6.0 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 826.1 | 4.025/px | 100.9 | 6.0 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 33.8 | 98.353/px | 100.9 | 306.5 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 83.6 | 39.796/px | 100.9 | 306.5 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 219.1 | 15.175/px | 100.9 | 306.5 | D3; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 75.9 | 43.813/px | 100.9 | 306.5 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 370.4 | 8.976/px | - | 1.4 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 2,283.0 | 1.456/px | - | 1.4 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 2,326.6 | 1.429/px | - | 1.4 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 1,571.4 | 2.116/px | - | 1.4 |  |
| Dd.ColorFill.Vid.16x16 | HW? | 65.8 | 50.538/px | 100.9 | 5.2 | D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 1,893.0 | 1.756/px | 100.9 | 5.2 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 1,989.3 | 1.671/px | 100.9 | 5.2 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 1,139.1 | 2.919/px | 100.9 | 5.2 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 9,478.956/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 13,026.495/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 9,477.605/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 2,334.396/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 4,394,473.000/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | CPU | 18.0 | 184.947/px | 100.9 | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 16.5 | 201.480/px | 100.9 | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 18.1 | 183.913/px | 100.9 | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 16.2 | 205.187/px | 100.9 | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 17.9 | 186.045/px | 100.9 | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 17.9 | 185.807/px | 100.9 | - |  |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 103.0 | 32.266/px | 100.9 | 10.9 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 99.5 | 33.418/px | 100.9 | 10.9 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 38.5 | 86.273/px | 100.9 | 10.9 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 100.0 | 33.249/px | 100.9 | 10.9 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 71.3 | 46.657/px | 100.9 | 10.9 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 102.2 | 32.547/px | 100.9 | 10.9 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 92.3 | 36.012/px | 100.9 | 10.9 |  |
| Gdi.BitBltBitmapToScreen.8x8 | noisy | 17.3 | 191.949/px | 100.9 | 10.9 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 102.3 | 32.507/px | 100.9 | 6.8 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 98.2 | 33.851/px | 100.9 | 6.8 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 32.2 | 103.164/px | 100.9 | 6.8 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 100.7 | 33.024/px | 100.9 | 6.8 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 65.4 | 50.876/px | 100.9 | 6.8 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 99.7 | 33.365/px | 100.9 | 6.8 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 89.5 | 37.143/px | 100.9 | 6.8 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 10.9 | 303.831/px | 100.9 | 6.8 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 25.8 | 128.748/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 25.3 | 131.468/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 19.7 | 168.563/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 26.1 | 127.302/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 23.9 | 139.256/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 26.0 | 127.863/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 25.4 | 130.866/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 11.5 | 290.105/px | 27.2 | 8.3 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 18.3 | 181.359/px | 100.9 | 2.9 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 14.9 | 223.901/px | 100.9 | 2.9 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 18.2 | 183.083/px | 100.9 | 2.9 | D2 0.01 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 17.3 | 192.309/px | 100.9 | 2.9 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 18.0 | 185.023/px | 100.9 | 2.9 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 8.5 | 388.967/px | 100.9 | 2.9 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 17.7 | 188.225/px | 100.9 | -9.6 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 13.8 | 241.753/px | 100.9 | -9.6 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 17.9 | 186.009/px | 100.9 | -9.6 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 16.6 | 200.848/px | 100.9 | -9.6 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 17.6 | 188.749/px | 100.9 | -9.6 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 17.4 | 191.197/px | 100.9 | -9.6 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 9.6 | 346.459/px | 100.9 | -9.6 |  |
| Gdi.Ellipse.128x128 | CPU | 49.2 | 67.532/px | 100.9 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 195,019.187/string | 100.9 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 112,301.570/string | 100.9 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 161,520.897/string | 100.9 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 88,731.402/string | 100.9 | - |  |
| Gdi.FillRect.1024x768 | CPU | 104.8 | 31.724/px | 100.9 | 8.4 | D2 -0.04 |
| Gdi.FillRect.128x128 | CPU | 100.1 | 33.226/px | 100.9 | 8.4 |  |
| Gdi.FillRect.16x16 | CPU | 40.0 | 83.116/px | 100.9 | 8.4 |  |
| Gdi.FillRect.256x256 | CPU | 103.1 | 32.265/px | 100.9 | 8.4 |  |
| Gdi.FillRect.32x32 | CPU | 74.2 | 44.817/px | 100.9 | 8.4 |  |
| Gdi.FillRect.512x512 | CPU | 103.5 | 32.139/px | 100.9 | 8.4 |  |
| Gdi.FillRect.64x64 | CPU | 93.6 | 35.523/px | 100.9 | 8.4 |  |
| Gdi.FillRect.8x8 | CPU | 14.0 | 236.997/px | 100.9 | 8.4 |  |
| Gdi.LineDiagonal30 | n/a | - | 14,737.123/line | 100.9 | - |  |
| Gdi.LineHorizontal | n/a | - | 15,419.333/line | 100.9 | - |  |
| Gdi.LineVertical | n/a | - | 16,105.830/line | 100.9 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 105.0 | 31.671/px | 100.9 | 12.6 |  |
| Gdi.PatBltHatch.128x128 | CPU | 98.7 | 33.691/px | 100.9 | 12.6 |  |
| Gdi.PatBltHatch.16x16 | CPU | 38.3 | 86.895/px | 100.9 | 12.6 |  |
| Gdi.PatBltHatch.256x256 | CPU | 100.7 | 33.027/px | 100.9 | 12.6 |  |
| Gdi.PatBltHatch.32x32 | CPU | 75.0 | 44.314/px | 100.9 | 12.6 |  |
| Gdi.PatBltHatch.512x512 | CPU | 103.1 | 32.249/px | 100.9 | 12.6 |  |
| Gdi.PatBltHatch.64x64 | CPU | 92.6 | 35.892/px | 100.9 | 12.6 |  |
| Gdi.PatBltHatch.8x8 | CPU | 13.2 | 251.536/px | 100.9 | 12.6 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 104.4 | 31.852/px | 100.9 | 11.1 |  |
| Gdi.PatBltPattern.128x128 | CPU | 99.5 | 33.430/px | 100.9 | 11.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 37.7 | 88.192/px | 100.9 | 11.1 |  |
| Gdi.PatBltPattern.256x256 | CPU | 101.0 | 32.912/px | 100.9 | 11.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 71.7 | 46.376/px | 100.9 | 11.1 |  |
| Gdi.PatBltPattern.512x512 | CPU | 102.8 | 32.350/px | 100.9 | 11.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 92.3 | 36.018/px | 100.9 | 11.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 13.4 | 248.778/px | 100.9 | 11.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU | 103.4 | 32.149/px | 100.9 | 2.1 |  |
| Gdi.PatBltSolid.128x128 | CPU | 102.0 | 32.588/px | 100.9 | 2.1 |  |
| Gdi.PatBltSolid.16x16 | CPU | 56.1 | 59.298/px | 100.9 | 2.1 |  |
| Gdi.PatBltSolid.256x256 | CPU | 102.3 | 32.495/px | 100.9 | 2.1 |  |
| Gdi.PatBltSolid.32x32 | CPU | 85.1 | 39.076/px | 100.9 | 2.1 |  |
| Gdi.PatBltSolid.512x512 | CPU | 101.9 | 32.636/px | 100.9 | 2.1 |  |
| Gdi.PatBltSolid.64x64 | CPU | 97.8 | 33.995/px | 100.9 | 2.1 |  |
| Gdi.PatBltSolid.8x8 | CPU | 23.9 | 138.887/px | 100.9 | 2.1 |  |
| Gdi.Polygon.128x128 | CPU | 68.4 | 48.596/px | 100.9 | - |  |
| Gdi.Polyline | n/a | - | 7,015.879/line | 100.9 | - |  |
| Gdi.Rectangle.128x128 | CPU | 93.9 | 35.397/px | 100.9 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 96.8 | 34.339/px | 100.9 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 17.2 | 192.986/px | 100.9 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 95.5 | 34.828/px | 100.9 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 10.3 | 322.319/px | 100.9 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 10.3 | 321.568/px | 100.9 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 18.1 | 183.905/px | 100.9 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 10.4 | 318.809/px | 100.9 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 96.7 | 34.391/px | 100.9 | - |  |
| Gdi.RoundRect.128x128 | CPU | 73.9 | 45.021/px | 100.9 | - |  |
| Gdi.ScrollBitBlt | CPU | 18.3 | 181.779/px | 100.9 | - |  |
| Gdi.ScrollDC | CPU | 18.5 | 179.310/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 101.7 | 32.680/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 97.6 | 34.076/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 102.1 | 32.560/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 98.3 | 33.836/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 99.5 | 33.414/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 98.8 | 33.660/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 101.4 | 32.798/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 99.9 | 33.273/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 100.6 | 33.035/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 101.7 | 32.705/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 101.4 | 32.792/px | 100.9 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 101.7 | 32.700/px | 100.9 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 3.3 | 1,007.419/px | 100.9 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 3.1 | 1,057.450/px | 100.9 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 3.3 | 1,003.445/px | 100.9 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 3.1 | 1,070.493/px | 100.9 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 18.3 | 181.883/px | 100.9 | - |  |
| Gdi.StretchBltUp.256x256 | noisy | 19.8 | 168.049/px | 100.9 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 18.5 | 179.573/px | 100.9 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 19.8 | 168.189/px | 100.9 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 20.5 | 161.935/px | 100.9 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 19.1 | 174.186/px | 100.9 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 20.5 | 162.439/px | 100.9 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 24.5 | 135.902/px | 100.9 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 195,569.061/string | 100.9 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 110,404.782/string | 100.9 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 161,223.776/string | 100.9 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 88,050.456/string | 100.9 | - |  |

## CLAIMED-NOT-DELIVERED

None.
