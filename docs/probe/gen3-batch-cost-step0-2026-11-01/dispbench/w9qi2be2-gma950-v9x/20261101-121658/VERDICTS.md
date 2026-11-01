# Verdicts: w9qi2be2-gma950-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1024x576x16, build 082ccfd, driver Velocity9x Intel GMA 950 (945GSE)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 2,810.7 |
| FbRead | 25.7 |
| MemFill | 1,266.3 |
| MemCopy | 932.2 |
| DibToDibBlt | 876.1 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 58,129.3 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 59.8 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 34 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 137.5 | 24.175/px | - | 125.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 253.4 | 13.123/px | - | 125.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 1,130.4 | 2.941/px | - | 125.4 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 233.9 | 14.214/px | - | 125.4 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 327.7 | 10.146/px | - | -0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 1,239.8 | 2.682/px | - | -0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 1,196.5 | 2.779/px | - | -0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 1,238.4 | 2.685/px | - | -0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 307.4 | 10.815/px | - | -3.8 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 2,093.7 | 1.588/px | - | -3.8 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 1,663.1 | 1.999/px | - | -3.8 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 1,465.4 | 2.269/px | - | -3.8 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 56.7 | 58.676/px | 2,810.7 | 29.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 254.1 | 13.085/px | 2,810.7 | 29.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 298.2 | 11.150/px | 2,810.7 | 29.6 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 202.2 | 16.441/px | 2,810.7 | 29.6 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 64.1 | 51.842/px | 2,810.7 | 3.4 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | CPU-fast | 1,813.0 | 1.834/px | 2,810.7 | 3.4 | not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | CPU-fast | 1,647.4 | 2.018/px | 2,810.7 | 3.4 | not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU-fast | 748.0 | 4.445/px | 2,810.7 | 3.4 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 65.7 | 50.636/px | 2,810.7 | 7.9 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | CPU-fast | 2,342.8 | 1.419/px | 2,810.7 | 7.9 | not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | CPU-fast | 2,746.1 | 1.211/px | 2,810.7 | 7.9 | not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU-fast | 873.8 | 3.805/px | 2,810.7 | 7.9 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 18.4 | 181.081/px | 25.7 | -0.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 23.9 | 139.187/px | 25.7 | -0.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 23.8 | 139.691/px | 25.7 | -0.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 23.3 | 142.723/px | 25.7 | -0.1 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 31.7 | 104.935/px | 25.7 | 12.1 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 50.5 | 65.807/px | 25.7 | 12.1 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 51.0 | 65.206/px | 25.7 | 12.1 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 48.7 | 68.229/px | 25.7 | 12.1 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 35.3 | 94.202/px | 25.7 | 5.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 97.6 | 34.085/px | 25.7 | 5.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 97.1 | 34.232/px | 25.7 | 5.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 87.5 | 37.980/px | 25.7 | 5.7 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 15.7 | 212.442/px | 2,810.7 | 16.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 23.8 | 139.855/px | 2,810.7 | 16.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 23.9 | 139.299/px | 2,810.7 | 16.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 22.9 | 144.894/px | 2,810.7 | 16.1 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 72.9 | 45.640/px | 2,810.7 | 5.1 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU-fast | 1,078.4 | 3.083/px | 2,810.7 | 5.1 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU-fast | 1,116.4 | 2.978/px | 2,810.7 | 5.1 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU-fast | 777.5 | 4.277/px | 2,810.7 | 5.1 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 26.4 | 125.724/px | 2,810.7 | 14.9 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 100.0 | 33.236/px | 2,810.7 | 14.9 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 101.0 | 32.922/px | 2,810.7 | 14.9 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 85.7 | 38.794/px | 2,810.7 | 14.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 372.3 | 8.931/px | - | -1.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 1,272.6 | 2.613/px | - | -1.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 1,199.2 | 2.773/px | - | -1.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 1,281.6 | 2.594/px | - | -1.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 66.6 | 49.898/px | 2,810.7 | 3.2 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | CPU-fast | 1,821.8 | 1.825/px | 2,810.7 | 3.2 | not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | CPU-fast | 1,657.0 | 2.007/px | 2,810.7 | 3.2 | not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU-fast | 766.3 | 4.339/px | 2,810.7 | 3.2 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 140.6 | 23.641/px | 25.7 | 3.6 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 1,148.2 | 2.896/px | 25.7 | 3.6 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 1,198.9 | 2.773/px | 25.7 | 3.6 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 897.0 | 3.707/px | 25.7 | 3.6 | D4,D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 75.3 | 44.139/px | 2,810.7 | 5.0 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU-fast | 1,076.8 | 3.088/px | 2,810.7 | 5.0 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU-fast | 1,116.0 | 2.979/px | 2,810.7 | 5.0 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU-fast | 777.1 | 4.279/px | 2,810.7 | 5.0 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 58.8 | 56.594/px | 2,810.7 | 29.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 252.8 | 13.152/px | 2,810.7 | 29.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 295.6 | 11.247/px | 2,810.7 | 29.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 200.3 | 16.596/px | 2,810.7 | 29.2 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 392.8 | 8.465/px | - | 0.6 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 2,383.9 | 1.395/px | - | 0.6 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 2,315.5 | 1.436/px | - | 0.6 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 1,611.2 | 2.064/px | - | 0.6 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 75.6 | 43.993/px | 2,810.7 | 4.5 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU-fast | 2,081.8 | 1.597/px | 2,810.7 | 4.5 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU-fast | 2,164.3 | 1.536/px | 2,810.7 | 4.5 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU-fast | 1,184.7 | 2.807/px | 2,810.7 | 4.5 | claimed |
| Dd.Lock.Back | n/a | - | 8,951.156/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 8,892.067/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 8,929.934/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 2,257.498/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 553,901.481/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | CPU | 25.0 | 133.148/px | 2,810.7 | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 21.4 | 155.261/px | 2,810.7 | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 24.6 | 135.096/px | 2,810.7 | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 21.5 | 154.743/px | 2,810.7 | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 24.7 | 134.462/px | 2,810.7 | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 24.8 | 133.874/px | 2,810.7 | - |  |
| Gdi.BitBltBitmapToScreen.1024x576 | CPU-fast | 1,017.4 | 3.268/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 831.8 | 3.997/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 65.1 | 51.045/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU-fast | 993.0 | 3.349/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 251.7 | 13.209/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 990.9 | 3.356/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 588.9 | 5.646/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 18.5 | 179.827/px | 2,810.7 | 5.9 |  |
| Gdi.BitBltDibToScreen.1024x576 | CPU | 419.1 | 7.934/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 369.2 | 9.005/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 38.7 | 85.865/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 411.4 | 8.082/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 107.3 | 30.994/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 417.9 | 7.956/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 177.2 | 18.769/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 11.5 | 288.034/px | 2,810.7 | 14.2 |  |
| Gdi.BitBltScreenToBitmap.1024x576 | CPU | 25.3 | 131.338/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 23.8 | 139.625/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 19.5 | 170.489/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 24.0 | 138.482/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 23.5 | 141.533/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 24.0 | 138.764/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 23.6 | 140.653/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 11.6 | 287.342/px | 25.7 | 3.2 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 25.0 | 132.852/px | 2,810.7 | 10.8 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 18.9 | 175.783/px | 2,810.7 | 10.8 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 25.5 | 130.617/px | 2,810.7 | 10.8 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 23.4 | 142.232/px | 2,810.7 | 10.8 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.5 | 135.515/px | 2,810.7 | 10.8 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 11.1 | 298.948/px | 2,810.7 | 10.8 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 25.1 | 132.515/px | 2,810.7 | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 17.7 | 188.068/px | 2,810.7 | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 25.4 | 130.748/px | 2,810.7 | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 22.9 | 145.122/px | 2,810.7 | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 25.5 | 130.400/px | 2,810.7 | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 24.7 | 134.555/px | 2,810.7 | 11.8 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 11.1 | 300.024/px | 2,810.7 | 11.8 |  |
| Gdi.Ellipse.128x128 | CPU | 84.2 | 39.500/px | 2,810.7 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 85,989.093/string | 2,810.7 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 75,386.136/string | 2,810.7 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 85,194.792/string | 2,810.7 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 73,458.903/string | 2,810.7 | - |  |
| Gdi.FillRect.1024x576 | CPU-fast | 1,884.7 | 1.764/px | 2,810.7 | 9.0 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU-fast | 1,197.8 | 2.776/px | 2,810.7 | 9.0 |  |
| Gdi.FillRect.16x16 | CPU | 54.5 | 61.031/px | 2,810.7 | 9.0 |  |
| Gdi.FillRect.256x256 | CPU-fast | 1,625.0 | 2.046/px | 2,810.7 | 9.0 |  |
| Gdi.FillRect.32x32 | CPU | 223.3 | 14.888/px | 2,810.7 | 9.0 |  |
| Gdi.FillRect.512x512 | CPU-fast | 1,816.1 | 1.831/px | 2,810.7 | 9.0 |  |
| Gdi.FillRect.64x64 | CPU-fast | 628.2 | 5.293/px | 2,810.7 | 9.0 |  |
| Gdi.FillRect.8x8 | CPU | 15.3 | 217.064/px | 2,810.7 | 9.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 14,341.540/line | 2,810.7 | - |  |
| Gdi.LineHorizontal | n/a | - | 9,401.310/line | 2,810.7 | - |  |
| Gdi.LineVertical | n/a | - | 19,113.348/line | 2,810.7 | - |  |
| Gdi.PatBltHatch.1024x576 | CPU | 488.0 | 6.814/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltHatch.128x128 | CPU | 433.4 | 7.672/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltHatch.16x16 | CPU | 60.6 | 54.838/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltHatch.256x256 | CPU | 470.4 | 7.069/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltHatch.32x32 | CPU | 191.4 | 17.371/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltHatch.512x512 | CPU | 483.8 | 6.873/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltHatch.64x64 | CPU | 349.0 | 9.528/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltHatch.8x8 | CPU | 18.4 | 180.581/px | 2,810.7 | 7.4 |  |
| Gdi.PatBltPattern.1024x576 | CPU | 488.3 | 6.809/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltPattern.128x128 | CPU | 433.5 | 7.671/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltPattern.16x16 | CPU | 60.5 | 54.997/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltPattern.256x256 | CPU | 471.1 | 7.059/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltPattern.32x32 | CPU | 191.2 | 17.387/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltPattern.512x512 | CPU | 483.2 | 6.882/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltPattern.64x64 | CPU | 348.9 | 9.530/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltPattern.8x8 | CPU | 18.4 | 180.599/px | 2,810.7 | 7.3 |  |
| Gdi.PatBltSolid.1024x576 | CPU-fast | 1,892.7 | 1.757/px | 2,810.7 | 5.8 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 1,369.0 | 2.429/px | 2,810.7 | 5.8 |  |
| Gdi.PatBltSolid.16x16 | CPU | 83.4 | 39.849/px | 2,810.7 | 5.8 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 1,697.2 | 1.959/px | 2,810.7 | 5.8 |  |
| Gdi.PatBltSolid.32x32 | CPU | 351.0 | 9.474/px | 2,810.7 | 5.8 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 1,840.9 | 1.806/px | 2,810.7 | 5.8 |  |
| Gdi.PatBltSolid.64x64 | CPU-fast | 782.5 | 4.249/px | 2,810.7 | 5.8 |  |
| Gdi.PatBltSolid.8x8 | CPU | 26.9 | 123.620/px | 2,810.7 | 5.8 |  |
| Gdi.Polygon.128x128 | CPU | 153.3 | 21.688/px | 2,810.7 | - |  |
| Gdi.Polyline | n/a | - | 1,436.268/line | 2,810.7 | - |  |
| Gdi.Rectangle.128x128 | CPU | 493.9 | 6.732/px | 2,810.7 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 353.0 | 9.420/px | 2,810.7 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.5 | 135.757/px | 2,810.7 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 347.8 | 9.560/px | 2,810.7 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 8.9 | 374.990/px | 2,810.7 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 8.9 | 375.313/px | 2,810.7 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.8 | 133.880/px | 2,810.7 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 8.8 | 379.446/px | 2,810.7 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 352.2 | 9.441/px | 2,810.7 | - |  |
| Gdi.RoundRect.128x128 | CPU | 212.9 | 15.619/px | 2,810.7 | - |  |
| Gdi.ScrollBitBlt | CPU | 25.2 | 131.751/px | 2,810.7 | - |  |
| Gdi.ScrollDC | CPU | 25.3 | 131.383/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x576 | CPU | 398.6 | 8.341/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x288 | CPU | 403.6 | 8.239/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x576 | CPU | 411.2 | 8.086/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x288 | CPU | 407.7 | 8.155/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x576 | CPU | 131.6 | 25.264/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x288 | CPU | 131.5 | 25.281/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x576 | CPU | 132.6 | 25.069/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x288 | CPU | 131.6 | 25.263/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x576 | CPU | 154.9 | 21.470/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x288 | CPU | 155.0 | 21.448/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x576 | CPU | 157.2 | 21.153/px | 2,810.7 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x288 | CPU | 156.3 | 21.271/px | 2,810.7 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 3.4 | 973.905/px | 2,810.7 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 3.3 | 1,021.312/px | 2,810.7 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 3.2 | 1,046.953/px | 2,810.7 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 2.9 | 1,162.669/px | 2,810.7 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 21.7 | 153.075/px | 2,810.7 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 23.9 | 139.224/px | 2,810.7 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 18.0 | 184.307/px | 2,810.7 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 19.7 | 168.945/px | 2,810.7 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 25.0 | 132.736/px | 2,810.7 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 24.3 | 136.605/px | 2,810.7 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 20.6 | 161.427/px | 2,810.7 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 24.3 | 137.025/px | 2,810.7 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 86,171.984/string | 2,810.7 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 78,437.950/string | 2,810.7 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 85,118.784/string | 2,810.7 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 73,777.823/string | 2,810.7 | - |  |

## CLAIMED-NOT-DELIVERED

- Dd.Blt.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.256x256: the caps claim it, the timing says CPU-fast
- Dd.Blt.VidToVid.Plain.512x512: the caps claim it, the timing says CPU-fast
- Dd.Blt.VidToVid.Plain.64x64: the caps claim it, the timing says CPU-fast
- Dd.BltFast.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.256x256: the caps claim it, the timing says CPU-fast
- Dd.BltFast.VidToVid.Plain.512x512: the caps claim it, the timing says CPU-fast
- Dd.BltFast.VidToVid.Plain.64x64: the caps claim it, the timing says CPU-fast
- Dd.ColorFill.Vid.16x16: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.256x256: the caps claim it, the timing says CPU-fast
- Dd.ColorFill.Vid.512x512: the caps claim it, the timing says CPU-fast
- Dd.ColorFill.Vid.64x64: the caps claim it, the timing says CPU-fast
