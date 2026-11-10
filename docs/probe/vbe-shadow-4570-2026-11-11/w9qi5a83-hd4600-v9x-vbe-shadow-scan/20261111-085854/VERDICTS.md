# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow-scan

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
| FbWrite | 11,236.1 |
| FbRead | 227.0 |
| MemFill | 31,004.2 |
| MemCopy | 20,196.9 |
| DibToDibBlt | 8,680.3 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 2,338,908.0 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 60.0 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 104 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 879.0 | 7.264/px | - | 19.3 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,786.2 | 3.575/px | - | 19.3 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,438.6 | 0.612/px | - | 19.3 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,410.3 | 4.528/px | - | 19.3 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | noisy | 2,200.5 | 2.902/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 11,970.9 | 0.533/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,671.1 | 0.435/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,064.2 | 0.634/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,924.8 | 3.317/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU | 14,087.7 | 0.453/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,696.0 | 0.382/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 8,981.5 | 0.711/px | - | 0.7 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 444.9 | 14.353/px | 11,236.1 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 1,740.6 | 3.668/px | 11,236.1 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 5,906.5 | 1.081/px | 11,236.1 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 1,278.5 | 4.994/px | 11,236.1 | 17.4 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 544.3 | 11.731/px | 11,236.1 | 1.2 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 13,189.3 | 0.484/px | 11,236.1 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 16,226.0 | 0.394/px | 11,236.1 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU | 6,298.2 | 1.014/px | 11,236.1 | 1.2 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 579.1 | 11.026/px | 11,236.1 | 0.9 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 16,894.0 | 0.378/px | 11,236.1 | 0.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 18,115.2 | 0.352/px | 11,236.1 | 0.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU | 5,967.1 | 1.070/px | 11,236.1 | 0.9 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 33.9 | 188.463/px | 227.0 | 20.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 34.7 | 184.046/px | 227.0 | 20.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.4 | 180.579/px | 227.0 | 20.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 35.4 | 180.507/px | 227.0 | 20.3 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | CPU | 64.2 | 99.414/px | 227.0 | -0.6 | not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | CPU | 70.8 | 90.241/px | 227.0 | -0.6 | not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | CPU | 70.6 | 90.399/px | 227.0 | -0.6 | not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | CPU | 70.5 | 90.534/px | 227.0 | -0.6 | not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | CPU | 120.3 | 53.064/px | 227.0 | 2.1 | not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | CPU | 141.9 | 45.002/px | 227.0 | 2.1 | not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | CPU | 142.8 | 44.725/px | 227.0 | 2.1 | not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | CPU | 141.5 | 45.132/px | 227.0 | 2.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 23.9 | 267.117/px | 11,236.1 | -0.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.8 | 257.501/px | 11,236.1 | -0.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.8 | 257.646/px | 11,236.1 | -0.1 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.7 | 258.472/px | 11,236.1 | -0.1 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 23.5 | 271.170/px | 11,236.1 | 1.4 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU | 23.9 | 267.323/px | 11,236.1 | 1.4 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU | 23.9 | 267.105/px | 11,236.1 | 1.4 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU | 23.9 | 267.202/px | 11,236.1 | 1.4 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 90.5 | 70.583/px | 11,236.1 | 5.1 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 113.8 | 56.112/px | 11,236.1 | 5.1 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 115.1 | 55.476/px | 11,236.1 | 5.1 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 111.4 | 57.331/px | 11,236.1 | 5.1 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,515.3 | 2.539/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 11,357.5 | 0.562/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,645.3 | 0.436/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 10,468.7 | 0.610/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 617.8 | 10.336/px | 11,236.1 | 1.1 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 13,034.5 | 0.490/px | 11,236.1 | 1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 16,108.5 | 0.396/px | 11,236.1 | 1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU | 6,807.2 | 0.938/px | 11,236.1 | 1.1 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 673.8 | 9.477/px | 227.0 | 1.1 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 11,234.1 | 0.568/px | 227.0 | 1.1 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 14,525.3 | 0.440/px | 227.0 | 1.1 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 10,629.8 | 0.601/px | 227.0 | 1.1 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 23.6 | 270.582/px | 11,236.1 | 0.7 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU | 23.9 | 267.195/px | 11,236.1 | 0.7 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU | 23.9 | 267.106/px | 11,236.1 | 0.7 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU | 23.9 | 267.278/px | 11,236.1 | 0.7 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 459.0 | 13.910/px | 11,236.1 | 18.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 1,687.4 | 3.784/px | 11,236.1 | 18.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 5,823.3 | 1.096/px | 11,236.1 | 18.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 1,150.7 | 5.549/px | 11,236.1 | 18.2 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,767.5 | 2.307/px | - | 0.6 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 15,480.1 | 0.412/px | - | 0.6 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 19,079.4 | 0.335/px | - | 0.6 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,465.4 | 0.413/px | - | 0.6 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 722.7 | 8.835/px | 11,236.1 | 1.4 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU | 7,429.8 | 0.859/px | 11,236.1 | 1.4 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU | 8,868.6 | 0.720/px | 11,236.1 | 1.4 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU | 4,445.8 | 1.436/px | 11,236.1 | 1.4 | claimed |
| Dd.Lock.Back | n/a | - | 1,974.635/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 1,979.163/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 1,979.786/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 561.498/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 105,614.865/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 796.7 | 8.014/px | 11,236.1 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 784.7 | 8.137/px | 11,236.1 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 786.9 | 8.115/px | 11,236.1 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 786.6 | 8.118/px | 11,236.1 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 789.9 | 8.084/px | 11,236.1 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 785.8 | 8.126/px | 11,236.1 | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 6,954.0 | 0.918/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,513.2 | 1.817/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 337.3 | 18.931/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 3,127.6 | 2.042/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,068.8 | 5.974/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 4,986.9 | 1.280/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,543.3 | 2.511/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 97.1 | 65.750/px | 11,236.1 | 3.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,137.4 | 2.035/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,277.8 | 2.803/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 203.5 | 31.370/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,077.1 | 3.074/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 646.7 | 9.874/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,721.1 | 2.347/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,577.8 | 4.047/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 57.6 | 110.938/px | 11,236.1 | 4.0 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | HW? | 6,827.3 | 0.935/px | 227.0 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.128x128 | HW? | 3,484.5 | 1.832/px | 227.0 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.16x16 | HW? | 334.8 | 19.074/px | 227.0 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.256x256 | HW? | 3,123.1 | 2.044/px | 227.0 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.32x32 | noisy | 1,066.7 | 5.986/px | 227.0 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.512x512 | HW? | 4,830.7 | 1.322/px | 227.0 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.64x64 | HW? | 2,539.0 | 2.515/px | 227.0 | 3.4 | D4,D3? |
| Gdi.BitBltScreenToBitmap.8x8 | HW? | 97.6 | 65.415/px | 227.0 | 3.4 | D4 |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,122.2 | 5.690/px | 11,236.1 | 7.1 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 121.0 | 52.760/px | 11,236.1 | 7.1 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,123.5 | 3.007/px | 11,236.1 | 7.1 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 284.5 | 22.445/px | 11,236.1 | 7.1 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 608.5 | 10.494/px | 11,236.1 | 7.1 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 48.4 | 131.809/px | 11,236.1 | 7.1 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 3,177.2 | 2.010/px | 11,236.1 | 3.8 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 244.1 | 26.154/px | 11,236.1 | 3.8 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 2,664.8 | 2.396/px | 11,236.1 | 3.8 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 642.2 | 9.942/px | 11,236.1 | 3.8 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 3,854.8 | 1.656/px | 11,236.1 | 3.8 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 1,997.5 | 3.197/px | 11,236.1 | 3.8 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 88.2 | 72.411/px | 11,236.1 | 3.8 |  |
| Gdi.Ellipse.128x128 | CPU | 144.3 | 44.253/px | 11,236.1 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 25,939.936/string | 11,236.1 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 18,718.695/string | 11,236.1 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 24,343.602/string | 11,236.1 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 19,211.376/string | 11,236.1 | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 7,608.2 | 0.839/px | 11,236.1 | 4.0 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 3,610.0 | 1.769/px | 11,236.1 | 4.0 |  |
| Gdi.FillRect.16x16 | CPU | 264.1 | 24.176/px | 11,236.1 | 4.0 |  |
| Gdi.FillRect.256x256 | CPU | 3,198.5 | 1.996/px | 11,236.1 | 4.0 |  |
| Gdi.FillRect.32x32 | CPU | 873.7 | 7.309/px | 11,236.1 | 4.0 |  |
| Gdi.FillRect.512x512 | CPU | 5,256.7 | 1.215/px | 11,236.1 | 4.0 |  |
| Gdi.FillRect.64x64 | CPU | 2,249.5 | 2.839/px | 11,236.1 | 4.0 |  |
| Gdi.FillRect.8x8 | CPU | 71.8 | 88.870/px | 11,236.1 | 4.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 12,317.813/line | 11,236.1 | - |  |
| Gdi.LineHorizontal | n/a | - | 4,800.002/line | 11,236.1 | - |  |
| Gdi.LineVertical | n/a | - | 10,110.849/line | 11,236.1 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 5,287.4 | 1.208/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,590.5 | 1.778/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 291.3 | 21.923/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltHatch.256x256 | CPU | 2,906.1 | 2.197/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 920.6 | 6.936/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltHatch.512x512 | CPU | 4,117.1 | 1.551/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,347.8 | 2.720/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 82.5 | 77.372/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 5,290.7 | 1.207/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,589.6 | 1.779/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 283.3 | 22.538/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.256x256 | CPU | 2,907.4 | 2.196/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 920.5 | 6.936/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.512x512 | CPU | 4,115.2 | 1.552/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,348.5 | 2.719/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 82.5 | 77.395/px | 11,236.1 | 3.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 7,633.9 | 0.836/px | 11,236.1 | 3.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,901.9 | 1.636/px | 11,236.1 | 3.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 383.2 | 16.662/px | 11,236.1 | 3.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 3,258.4 | 1.960/px | 11,236.1 | 3.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,185.5 | 5.386/px | 11,236.1 | 3.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 5,295.4 | 1.206/px | 11,236.1 | 3.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,714.8 | 2.352/px | 11,236.1 | 3.3 |  |
| Gdi.PatBltSolid.8x8 | CPU | 112.2 | 56.920/px | 11,236.1 | 3.3 |  |
| Gdi.Polygon.128x128 | CPU | 102.6 | 62.258/px | 11,236.1 | - |  |
| Gdi.Polyline | n/a | - | 828.063/line | 11,236.1 | - |  |
| Gdi.Rectangle.128x128 | CPU | 2,299.9 | 2.776/px | 11,236.1 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,286.2 | 2.793/px | 11,236.1 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 574.4 | 11.116/px | 11,236.1 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,278.2 | 2.803/px | 11,236.1 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 540.2 | 11.819/px | 11,236.1 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 559.6 | 11.411/px | 11,236.1 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 608.3 | 10.497/px | 11,236.1 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 559.5 | 11.412/px | 11,236.1 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,286.1 | 2.793/px | 11,236.1 | - |  |
| Gdi.RoundRect.128x128 | CPU | 199.6 | 31.983/px | 11,236.1 | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 7,006.5 | 0.911/px | 11,236.1 | - |  |
| Gdi.ScrollDC | CPU-fast | 6,956.7 | 0.918/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 3,054.2 | 2.091/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 2,666.9 | 2.394/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 3,072.7 | 2.078/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 2,641.0 | 2.418/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 693.7 | 9.204/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 672.1 | 9.500/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 694.5 | 9.194/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 669.4 | 9.539/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 2,554.7 | 2.499/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 2,275.2 | 2.806/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 2,571.0 | 2.484/px | 11,236.1 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 2,253.2 | 2.834/px | 11,236.1 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 39.9 | 160.026/px | 11,236.1 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 35.8 | 178.143/px | 11,236.1 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 39.9 | 159.995/px | 11,236.1 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 35.9 | 177.860/px | 11,236.1 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 176.4 | 36.197/px | 11,236.1 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 204.1 | 31.286/px | 11,236.1 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 176.1 | 36.259/px | 11,236.1 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 204.0 | 31.306/px | 11,236.1 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 175.9 | 36.298/px | 11,236.1 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 225.7 | 28.285/px | 11,236.1 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 175.9 | 36.293/px | 11,236.1 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 225.9 | 28.272/px | 11,236.1 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 25,853.742/string | 11,236.1 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 18,928.906/string | 11,236.1 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,248.498/string | 11,236.1 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 18,813.539/string | 11,236.1 | - |  |

## CLAIMED-NOT-DELIVERED

- Dd.Blt.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.256x256: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.512x512: the caps claim it, the timing says CPU
- Dd.Blt.VidToVid.Plain.64x64: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.16x16: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.256x256: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.512x512: the caps claim it, the timing says CPU
- Dd.BltFast.VidToVid.Plain.64x64: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.16x16: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.256x256: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.512x512: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.64x64: the caps claim it, the timing says CPU
