# Verdicts: w9qi5a83-hd4600-v9x-vbe-noshadow

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
| FbWrite | 11,321.5 |
| FbRead | 227.6 |
| MemFill | 30,974.1 |
| MemCopy | 20,241.0 |
| DibToDibBlt | 8,681.0 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 2,349,228.3 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 60.0 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 104 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 892.9 | 7.151/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,795.7 | 3.556/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,450.6 | 0.611/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,418.0 | 4.503/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,217.9 | 2.879/px | - | 0.9 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 11,263.0 | 0.567/px | - | 0.9 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,727.0 | 0.434/px | - | 0.9 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,289.8 | 0.621/px | - | 0.9 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,920.8 | 3.324/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU | 14,029.5 | 0.455/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,690.1 | 0.383/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 9,108.6 | 0.701/px | - | 0.7 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 440.1 | 14.510/px | 11,321.5 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 1,737.7 | 3.675/px | 11,321.5 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 5,859.4 | 1.090/px | 11,321.5 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 1,254.2 | 5.091/px | 11,321.5 | 17.4 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 544.1 | 11.735/px | 11,321.5 | 1.3 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 13,016.7 | 0.491/px | 11,321.5 | 1.3 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 16,279.2 | 0.392/px | 11,321.5 | 1.3 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU | 6,268.1 | 1.019/px | 11,321.5 | 1.3 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 579.6 | 11.018/px | 11,321.5 | 0.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 17,417.0 | 0.367/px | 11,321.5 | 0.8 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 18,133.8 | 0.352/px | 11,321.5 | 0.8 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU | 5,965.4 | 1.070/px | 11,321.5 | 0.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 33.8 | 188.692/px | 227.6 | 0.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 35.4 | 180.226/px | 227.6 | 0.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.4 | 180.363/px | 227.6 | 0.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 34.9 | 183.059/px | 227.6 | 0.8 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | CPU | 64.2 | 99.498/px | 227.6 | -0.8 | not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | CPU | 70.8 | 90.191/px | 227.6 | -0.8 | not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | CPU | 70.6 | 90.389/px | 227.6 | -0.8 | not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | CPU | 70.5 | 90.562/px | 227.6 | -0.8 | not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | CPU | 120.3 | 53.085/px | 227.6 | -3.5 | not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | CPU | 142.0 | 44.967/px | 227.6 | -3.5 | not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | CPU | 140.1 | 45.570/px | 227.6 | -3.5 | not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | CPU | 141.6 | 45.109/px | 227.6 | -3.5 | not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 23.9 | 266.893/px | 11,321.5 | 0.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.8 | 257.418/px | 11,321.5 | 0.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.8 | 257.415/px | 11,321.5 | 0.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.7 | 258.614/px | 11,321.5 | 0.9 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 23.5 | 271.170/px | 11,321.5 | 0.9 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU | 23.9 | 267.144/px | 11,321.5 | 0.9 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU | 23.9 | 267.043/px | 11,321.5 | 0.9 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU | 23.9 | 267.315/px | 11,321.5 | 0.9 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 90.4 | 70.609/px | 11,321.5 | 4.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 114.1 | 55.954/px | 11,321.5 | 4.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 115.1 | 55.467/px | 11,321.5 | 4.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 111.2 | 57.445/px | 11,321.5 | 4.3 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,515.6 | 2.538/px | - | 0.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 12,030.2 | 0.531/px | - | 0.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,583.3 | 0.438/px | - | 0.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 10,670.3 | 0.598/px | - | 0.7 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 617.6 | 10.338/px | 11,321.5 | 1.1 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 13,181.4 | 0.484/px | 11,321.5 | 1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 16,104.9 | 0.396/px | 11,321.5 | 1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU | 6,784.9 | 0.941/px | 11,321.5 | 1.1 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 2,524.0 | 2.530/px | 227.6 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 11,215.8 | 0.569/px | 227.6 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 14,477.9 | 0.441/px | 227.6 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 10,653.5 | 0.599/px | 227.6 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 23.6 | 270.285/px | 11,321.5 | 0.4 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU | 23.9 | 267.167/px | 11,321.5 | 0.4 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU | 23.9 | 267.124/px | 11,321.5 | 0.4 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU | 23.9 | 267.157/px | 11,321.5 | 0.4 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 459.0 | 13.911/px | 11,321.5 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 1,704.0 | 3.747/px | 11,321.5 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 5,887.3 | 1.085/px | 11,321.5 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 1,256.5 | 5.082/px | 11,321.5 | 17.8 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,765.6 | 2.309/px | - | 0.5 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 15,417.0 | 0.414/px | - | 0.5 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 18,782.8 | 0.340/px | - | 0.5 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,450.3 | 0.413/px | - | 0.5 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 721.4 | 8.851/px | 11,321.5 | 1.5 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU | 7,258.8 | 0.880/px | 11,321.5 | 1.5 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU | 8,868.3 | 0.720/px | 11,321.5 | 1.5 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU | 4,452.9 | 1.434/px | 11,321.5 | 1.5 | claimed |
| Dd.Lock.Back | n/a | - | 1,974.142/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 1,972.328/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 1,967.314/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 561.844/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 105,459.198/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 25.2 | 253.069/px | 11,321.5 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 24.1 | 265.174/px | 11,321.5 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 25.2 | 252.999/px | 11,321.5 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 24.1 | 265.245/px | 11,321.5 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 25.3 | 252.746/px | 11,321.5 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 25.2 | 253.031/px | 11,321.5 | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 11,226.0 | 0.569/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU-fast | 8,328.7 | 0.767/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 471.5 | 13.543/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 4,300.7 | 1.485/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,871.6 | 3.412/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 7,481.1 | 0.854/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 4,715.4 | 1.354/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 129.6 | 49.266/px | 11,321.5 | 2.2 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 4,397.2 | 1.452/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 3,509.4 | 1.819/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 242.1 | 26.371/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 4,168.7 | 1.532/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 869.6 | 7.342/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 4,388.3 | 1.455/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 2,189.5 | 2.916/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 66.4 | 96.168/px | 11,321.5 | 2.0 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 225.0 | 28.382/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.4 | 180.410/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 33.4 | 191.173/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 148.0 | 43.142/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 34.7 | 183.910/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 191.1 | 33.409/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.2 | 181.222/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 28.5 | 224.365/px | 227.6 | 203.1 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 25.2 | 253.620/px | 11,321.5 | 230.2 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.3 | 274.427/px | 11,321.5 | 230.2 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 103.2 | 61.884/px | 11,321.5 | 230.2 | D2 0.01 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 23.7 | 268.985/px | 11,321.5 | 230.2 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.6 | 259.694/px | 11,321.5 | 230.2 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 20.7 | 307.756/px | 11,321.5 | 230.2 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 23.7 | 269.585/px | 11,321.5 | 2.6 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 22.2 | 287.019/px | 11,321.5 | 2.6 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 23.8 | 267.863/px | 11,321.5 | 2.6 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.4 | 272.918/px | 11,321.5 | 2.6 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 23.8 | 267.786/px | 11,321.5 | 2.6 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 23.8 | 268.073/px | 11,321.5 | 2.6 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 20.3 | 314.403/px | 11,321.5 | 2.6 |  |
| Gdi.Ellipse.128x128 | CPU | 851.0 | 7.503/px | 11,321.5 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 19,862.600/string | 11,321.5 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 14,395.004/string | 11,321.5 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 18,455.710/string | 11,321.5 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 14,240.597/string | 11,321.5 | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 11,277.6 | 0.566/px | 11,321.5 | 2.7 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU-fast | 7,761.2 | 0.823/px | 11,321.5 | 2.7 |  |
| Gdi.FillRect.16x16 | CPU | 329.6 | 19.371/px | 11,321.5 | 2.7 |  |
| Gdi.FillRect.256x256 | CPU | 4,210.6 | 1.516/px | 11,321.5 | 2.7 |  |
| Gdi.FillRect.32x32 | CPU | 1,318.2 | 4.844/px | 11,321.5 | 2.7 |  |
| Gdi.FillRect.512x512 | CPU-fast | 7,431.7 | 0.859/px | 11,321.5 | 2.7 |  |
| Gdi.FillRect.64x64 | CPU | 3,853.4 | 1.657/px | 11,321.5 | 2.7 |  |
| Gdi.FillRect.8x8 | CPU | 88.2 | 72.383/px | 11,321.5 | 2.7 |  |
| Gdi.LineDiagonal30 | n/a | - | 4,786.571/line | 11,321.5 | - |  |
| Gdi.LineHorizontal | n/a | - | 3,375.970/line | 11,321.5 | - |  |
| Gdi.LineVertical | n/a | - | 6,936.674/line | 11,321.5 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU-fast | 10,080.7 | 0.633/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltHatch.128x128 | CPU-fast | 7,634.0 | 0.836/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltHatch.16x16 | CPU | 398.3 | 16.030/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltHatch.256x256 | CPU-fast | 9,634.4 | 0.663/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltHatch.32x32 | CPU | 1,507.2 | 4.236/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltHatch.512x512 | CPU-fast | 9,809.8 | 0.651/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltHatch.64x64 | CPU | 4,197.1 | 1.521/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltHatch.8x8 | CPU | 105.2 | 60.669/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.1024x768 | CPU-fast | 10,311.2 | 0.619/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.128x128 | CPU-fast | 7,558.3 | 0.845/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 398.2 | 16.033/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.256x256 | CPU-fast | 9,640.7 | 0.662/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 1,453.4 | 4.393/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.512x512 | CPU-fast | 9,810.6 | 0.651/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 4,092.0 | 1.560/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 105.2 | 60.684/px | 11,321.5 | 1.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 11,320.3 | 0.564/px | 11,321.5 | 2.1 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 9,094.3 | 0.702/px | 11,321.5 | 2.1 |  |
| Gdi.PatBltSolid.16x16 | CPU | 561.5 | 11.371/px | 11,321.5 | 2.1 |  |
| Gdi.PatBltSolid.256x256 | CPU | 4,288.2 | 1.489/px | 11,321.5 | 2.1 |  |
| Gdi.PatBltSolid.32x32 | CPU | 2,247.0 | 2.842/px | 11,321.5 | 2.1 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 7,501.5 | 0.851/px | 11,321.5 | 2.1 |  |
| Gdi.PatBltSolid.64x64 | CPU | 5,378.5 | 1.187/px | 11,321.5 | 2.1 |  |
| Gdi.PatBltSolid.8x8 | CPU | 158.4 | 40.311/px | 11,321.5 | 2.1 |  |
| Gdi.Polygon.128x128 | CPU | 1,094.6 | 5.833/px | 11,321.5 | - |  |
| Gdi.Polyline | n/a | - | 686.065/line | 11,321.5 | - |  |
| Gdi.Rectangle.128x128 | CPU | 3,291.0 | 1.940/px | 11,321.5 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,505.8 | 2.548/px | 11,321.5 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.5 | 260.253/px | 11,321.5 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,513.3 | 2.541/px | 11,321.5 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 14.3 | 446.096/px | 11,321.5 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 14.3 | 445.824/px | 11,321.5 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.6 | 259.763/px | 11,321.5 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 14.3 | 445.407/px | 11,321.5 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,497.3 | 2.557/px | 11,321.5 | - |  |
| Gdi.RoundRect.128x128 | CPU | 1,824.1 | 3.500/px | 11,321.5 | - |  |
| Gdi.ScrollBitBlt | CPU | 153.2 | 41.672/px | 11,321.5 | - |  |
| Gdi.ScrollDC | CPU | 153.2 | 41.675/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 4,234.2 | 1.508/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 4,167.3 | 1.532/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 4,266.8 | 1.497/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 4,193.8 | 1.523/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 741.6 | 8.611/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 738.8 | 8.642/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 739.9 | 8.630/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 738.8 | 8.642/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 3,346.3 | 1.908/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 3,329.1 | 1.918/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 3,375.4 | 1.892/px | 11,321.5 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 3,327.3 | 1.919/px | 11,321.5 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 18.3 | 349.218/px | 11,321.5 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 7.2 | 891.245/px | 11,321.5 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 18.3 | 348.988/px | 11,321.5 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 7.2 | 890.395/px | 11,321.5 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 80.3 | 79.511/px | 11,321.5 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 84.9 | 75.233/px | 11,321.5 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 78.9 | 80.910/px | 11,321.5 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 84.9 | 75.226/px | 11,321.5 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 182.1 | 35.063/px | 11,321.5 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 231.7 | 27.564/px | 11,321.5 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 181.2 | 35.247/px | 11,321.5 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 231.3 | 27.601/px | 11,321.5 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 19,731.332/string | 11,321.5 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 14,266.401/string | 11,321.5 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 18,320.576/string | 11,321.5 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 14,077.507/string | 11,321.5 | - |  |

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
