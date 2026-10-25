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
| FbWrite | 105.1 |
| FbRead | 25.7 |
| MemFill | 1,274.4 |
| MemCopy | 897.2 |
| DibToDibBlt | 846.0 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 58,027.9 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 34 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 137.3 | 24.214/px | - | 125.7 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 253.6 | 13.110/px | - | 125.7 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 1,144.0 | 2.907/px | - | 125.7 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 233.5 | 14.240/px | - | 125.7 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 327.2 | 10.163/px | - | -1.6 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 1,257.4 | 2.644/px | - | -1.6 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 1,182.3 | 2.812/px | - | -1.6 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 1,261.7 | 2.635/px | - | -1.6 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 311.4 | 10.677/px | - | -5.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 2,301.8 | 1.445/px | - | -5.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 1,630.8 | 2.039/px | - | -5.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 1,500.8 | 2.215/px | - | -5.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 43.8 | 75.870/px | 105.1 | 296.7 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 87.2 | 38.109/px | 105.1 | 296.7 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 235.5 | 14.116/px | 105.1 | 296.7 | D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 80.7 | 41.186/px | 105.1 | 296.7 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | HW? | 65.2 | 51.011/px | 105.1 | 2.3 | D4; not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 210.4 | 15.806/px | 105.1 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 208.1 | 15.980/px | 105.1 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 183.5 | 18.123/px | 105.1 | 2.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 62.5 | 53.202/px | 105.1 | 7.3 | D4; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 206.7 | 16.088/px | 105.1 | 7.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 209.4 | 15.875/px | 105.1 | 7.3 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 179.0 | 18.579/px | 105.1 | 7.3 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 17.3 | 191.881/px | 25.7 | 2.5 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 23.8 | 139.420/px | 25.7 | 2.5 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 23.7 | 140.150/px | 25.7 | 2.5 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 22.5 | 147.769/px | 25.7 | 2.5 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | noisy | 32.7 | 101.760/px | 25.7 | 8.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 50.9 | 65.354/px | 25.7 | 8.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 51.1 | 65.078/px | 25.7 | 8.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 49.0 | 67.810/px | 25.7 | 8.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 46.2 | 71.928/px | 25.7 | 1.9 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 97.1 | 34.227/px | 25.7 | 1.9 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 96.5 | 34.470/px | 25.7 | 1.9 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 90.3 | 36.810/px | 25.7 | 1.9 | D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 14.3 | 232.939/px | 105.1 | 19.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 20.0 | 166.511/px | 105.1 | 19.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 20.1 | 165.677/px | 105.1 | 19.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 19.4 | 171.394/px | 105.1 | 19.0 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW? | 52.7 | 63.141/px | 105.1 | 6.2 | D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 1,076.4 | 3.089/px | 105.1 | 6.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 1,117.5 | 2.975/px | 105.1 | 6.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 782.2 | 4.251/px | 105.1 | 6.2 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 25.3 | 131.403/px | 105.1 | 10.6 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 68.1 | 48.851/px | 105.1 | 10.6 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 68.2 | 48.745/px | 105.1 | 10.6 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 62.5 | 53.220/px | 105.1 | 10.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 359.1 | 9.261/px | - | -2.2 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 1,295.8 | 2.566/px | - | -2.2 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 1,195.2 | 2.782/px | - | -2.2 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 1,307.0 | 2.544/px | - | -2.2 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 67.2 | 49.447/px | 105.1 | 0.3 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 212.1 | 15.676/px | 105.1 | 0.3 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 207.7 | 16.011/px | 105.1 | 0.3 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 184.6 | 18.013/px | 105.1 | 0.3 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 318.0 | 10.455/px | 25.7 | -3.8 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 1,337.8 | 2.485/px | 25.7 | -3.8 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 1,160.0 | 2.866/px | 25.7 | -3.8 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 1,181.4 | 2.814/px | 25.7 | -3.8 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW? | 72.8 | 45.657/px | 105.1 | 5.1 | D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 1,077.3 | 3.087/px | 105.1 | 5.1 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 1,116.8 | 2.977/px | 105.1 | 5.1 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 774.7 | 4.292/px | 105.1 | 5.1 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 44.4 | 74.902/px | 105.1 | 298.0 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 86.9 | 38.248/px | 105.1 | 298.0 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 235.8 | 14.099/px | 105.1 | 298.0 | D3; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 81.6 | 40.762/px | 105.1 | 298.0 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 398.9 | 8.334/px | - | 1.3 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 2,383.7 | 1.395/px | - | 1.3 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 2,422.3 | 1.373/px | - | 1.3 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 1,627.1 | 2.043/px | - | 1.3 |  |
| Dd.ColorFill.Vid.16x16 | HW? | 72.4 | 45.903/px | 105.1 | 4.5 | D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 2,094.6 | 1.587/px | 105.1 | 4.5 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 2,150.4 | 1.546/px | 105.1 | 4.5 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 1,164.0 | 2.856/px | 105.1 | 4.5 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 8,378.059/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 8,411.075/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 8,359.734/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 2,225.295/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 4,224,847.273/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | CPU | 20.8 | 160.108/px | 105.1 | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 17.2 | 192.808/px | 105.1 | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 20.8 | 160.135/px | 105.1 | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 17.2 | 193.071/px | 105.1 | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 20.8 | 159.489/px | 105.1 | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 20.9 | 159.406/px | 105.1 | - |  |
| Gdi.BitBltBitmapToScreen.1024x576 | CPU | 107.5 | 30.941/px | 105.1 | 6.9 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 105.3 | 31.584/px | 105.1 | 6.9 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 41.2 | 80.683/px | 105.1 | 6.9 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 107.4 | 30.969/px | 105.1 | 6.9 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 82.1 | 40.476/px | 105.1 | 6.9 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 107.7 | 30.884/px | 105.1 | 6.9 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 98.6 | 33.717/px | 105.1 | 6.9 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 14.6 | 227.310/px | 105.1 | 6.9 |  |
| Gdi.BitBltDibToScreen.1024x576 | CPU | 107.3 | 30.996/px | 105.1 | 5.8 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 103.5 | 32.130/px | 105.1 | 5.8 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 29.7 | 112.117/px | 105.1 | 5.8 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 107.3 | 30.987/px | 105.1 | 5.8 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 68.6 | 48.479/px | 105.1 | 5.8 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 104.9 | 31.709/px | 105.1 | 5.8 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 91.1 | 36.488/px | 105.1 | 5.8 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 9.5 | 349.132/px | 105.1 | 5.8 |  |
| Gdi.BitBltScreenToBitmap.1024x576 | CPU | 25.2 | 131.709/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 25.1 | 132.645/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 18.7 | 177.765/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 23.9 | 139.410/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 21.9 | 151.617/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 24.0 | 138.499/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 23.6 | 141.077/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 9.7 | 344.226/px | 25.7 | 0.0 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 21.1 | 157.666/px | 105.1 | 5.9 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 17.1 | 194.773/px | 105.1 | 5.9 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 21.1 | 157.373/px | 105.1 | 5.9 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 19.9 | 166.765/px | 105.1 | 5.9 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 20.8 | 160.228/px | 105.1 | 5.9 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 10.9 | 304.761/px | 105.1 | 5.9 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 19.7 | 168.398/px | 105.1 | 10.6 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 14.4 | 230.178/px | 105.1 | 10.6 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 19.9 | 167.202/px | 105.1 | 10.6 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 18.4 | 180.672/px | 105.1 | 10.6 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 19.9 | 166.865/px | 105.1 | 10.6 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 19.5 | 170.168/px | 105.1 | 10.6 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 9.3 | 356.315/px | 105.1 | 10.6 |  |
| Gdi.Ellipse.128x128 | CPU | 53.4 | 62.290/px | 105.1 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 186,561.573/string | 105.1 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 107,821.858/string | 105.1 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 173,274.361/string | 105.1 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 101,343.512/string | 105.1 | - |  |
| Gdi.FillRect.1024x576 | CPU | 108.3 | 30.708/px | 105.1 | 7.4 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 105.4 | 31.533/px | 105.1 | 7.4 |  |
| Gdi.FillRect.16x16 | CPU | 43.5 | 76.513/px | 105.1 | 7.4 |  |
| Gdi.FillRect.256x256 | CPU | 107.9 | 30.823/px | 105.1 | 7.4 |  |
| Gdi.FillRect.32x32 | CPU | 77.3 | 42.988/px | 105.1 | 7.4 |  |
| Gdi.FillRect.512x512 | CPU | 108.3 | 30.697/px | 105.1 | 7.4 |  |
| Gdi.FillRect.64x64 | CPU | 99.4 | 33.463/px | 105.1 | 7.4 |  |
| Gdi.FillRect.8x8 | CPU | 14.9 | 222.604/px | 105.1 | 7.4 |  |
| Gdi.LineDiagonal30 | n/a | - | 20,103.545/line | 105.1 | - |  |
| Gdi.LineHorizontal | n/a | - | 14,682.656/line | 105.1 | - |  |
| Gdi.LineVertical | n/a | - | 21,253.875/line | 105.1 | - |  |
| Gdi.PatBltHatch.1024x576 | CPU | 108.9 | 30.547/px | 105.1 | 7.6 |  |
| Gdi.PatBltHatch.128x128 | CPU | 105.9 | 31.396/px | 105.1 | 7.6 |  |
| Gdi.PatBltHatch.16x16 | CPU | 46.5 | 71.487/px | 105.1 | 7.6 |  |
| Gdi.PatBltHatch.256x256 | CPU | 108.0 | 30.784/px | 105.1 | 7.6 |  |
| Gdi.PatBltHatch.32x32 | CPU | 74.6 | 44.588/px | 105.1 | 7.6 |  |
| Gdi.PatBltHatch.512x512 | CPU | 108.5 | 30.655/px | 105.1 | 7.6 |  |
| Gdi.PatBltHatch.64x64 | CPU | 96.6 | 34.431/px | 105.1 | 7.6 |  |
| Gdi.PatBltHatch.8x8 | CPU | 17.6 | 189.278/px | 105.1 | 7.6 |  |
| Gdi.PatBltPattern.1024x576 | CPU | 108.6 | 30.622/px | 105.1 | 8.0 |  |
| Gdi.PatBltPattern.128x128 | CPU | 105.1 | 31.643/px | 105.1 | 8.0 |  |
| Gdi.PatBltPattern.16x16 | CPU | 46.5 | 71.577/px | 105.1 | 8.0 |  |
| Gdi.PatBltPattern.256x256 | CPU | 107.8 | 30.830/px | 105.1 | 8.0 |  |
| Gdi.PatBltPattern.32x32 | CPU | 74.7 | 44.506/px | 105.1 | 8.0 |  |
| Gdi.PatBltPattern.512x512 | CPU | 108.5 | 30.643/px | 105.1 | 8.0 |  |
| Gdi.PatBltPattern.64x64 | CPU | 98.7 | 33.681/px | 105.1 | 8.0 |  |
| Gdi.PatBltPattern.8x8 | CPU | 17.5 | 190.046/px | 105.1 | 8.0 |  |
| Gdi.PatBltSolid.1024x576 | CPU | 108.8 | 30.562/px | 105.1 | 5.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 106.7 | 31.154/px | 105.1 | 5.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 59.9 | 55.533/px | 105.1 | 5.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 108.3 | 30.712/px | 105.1 | 5.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 90.3 | 36.828/px | 105.1 | 5.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 108.9 | 30.529/px | 105.1 | 5.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 101.0 | 32.935/px | 105.1 | 5.3 |  |
| Gdi.PatBltSolid.8x8 | CPU | 25.5 | 130.553/px | 105.1 | 5.3 |  |
| Gdi.Polygon.128x128 | CPU | 71.5 | 46.509/px | 105.1 | - |  |
| Gdi.Polyline | n/a | - | 6,634.420/line | 105.1 | - |  |
| Gdi.Rectangle.128x128 | CPU | 99.8 | 33.321/px | 105.1 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 99.4 | 33.452/px | 105.1 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 19.3 | 172.152/px | 105.1 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 98.8 | 33.644/px | 105.1 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 11.2 | 298.130/px | 105.1 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 11.0 | 302.070/px | 105.1 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 20.7 | 160.371/px | 105.1 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 11.0 | 302.394/px | 105.1 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 99.4 | 33.450/px | 105.1 | - |  |
| Gdi.RoundRect.128x128 | CPU | 79.9 | 41.592/px | 105.1 | - |  |
| Gdi.ScrollBitBlt | CPU | 21.2 | 156.842/px | 105.1 | - |  |
| Gdi.ScrollDC | CPU | 21.2 | 156.903/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x576 | CPU | 106.5 | 31.210/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x288 | CPU | 107.3 | 30.978/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x576 | CPU | 106.5 | 31.232/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x288 | CPU | 107.4 | 30.966/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x576 | CPU | 105.5 | 31.502/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x288 | CPU | 106.1 | 31.331/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x576 | CPU | 105.9 | 31.396/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x288 | CPU | 106.5 | 31.223/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x576 | CPU | 105.7 | 31.466/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x288 | CPU | 107.5 | 30.925/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x576 | CPU | 107.6 | 30.899/px | 105.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x288 | CPU | 107.3 | 30.995/px | 105.1 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 3.3 | 994.346/px | 105.1 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 3.2 | 1,041.121/px | 105.1 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 3.3 | 993.083/px | 105.1 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 3.2 | 1,045.139/px | 105.1 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 15.9 | 209.589/px | 105.1 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 20.8 | 159.771/px | 105.1 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 19.2 | 173.438/px | 105.1 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 20.8 | 160.133/px | 105.1 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 21.9 | 151.848/px | 105.1 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 25.9 | 128.180/px | 105.1 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 17.8 | 187.317/px | 105.1 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 20.2 | 164.227/px | 105.1 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 186,964.606/string | 105.1 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 108,097.902/string | 105.1 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 174,417.734/string | 105.1 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 101,112.320/string | 105.1 | - |  |

## CLAIMED-NOT-DELIVERED

None.
