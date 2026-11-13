# Verdicts: t5740-gm45-b34-wc

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
| FbWrite | 2,550.0 |
| FbRead | 27.4 |
| MemFill | 1,388.1 |
| MemCopy | 1,023.7 |
| DibToDibBlt | 847.5 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 60.1 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 60.1 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 95 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 87.0 | 38.204/px | - | 135.6 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 241.4 | 13.774/px | - | 135.6 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 1,177.4 | 2.824/px | - | 135.6 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 223.3 | 14.888/px | - | 135.6 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 140.0 | 23.754/px | - | 5.2 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 1,200.0 | 2.771/px | - | 5.2 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 1,319.9 | 2.519/px | - | 5.2 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 906.2 | 3.669/px | - | 5.2 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 295.8 | 11.241/px | - | -2.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 1,980.3 | 1.679/px | - | -2.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 1,705.4 | 1.950/px | - | -2.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 1,437.7 | 2.313/px | - | -2.1 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 41.1 | 80.804/px | 2,550.0 | 24.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 239.6 | 13.878/px | 2,550.0 | 24.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 265.1 | 12.541/px | 2,550.0 | 24.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 179.4 | 18.531/px | 2,550.0 | 24.9 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 59.3 | 56.044/px | 2,550.0 | 1.6 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | CPU-fast | 1,676.2 | 1.984/px | 2,550.0 | 1.6 | not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | CPU-fast | 1,419.7 | 2.342/px | 2,550.0 | 1.6 | not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU-fast | 717.2 | 4.636/px | 2,550.0 | 1.6 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 60.0 | 55.423/px | 2,550.0 | 8.0 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | CPU-fast | 2,076.5 | 1.601/px | 2,550.0 | 8.0 | not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | CPU-fast | 2,371.5 | 1.402/px | 2,550.0 | 8.0 | not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU-fast | 836.7 | 3.974/px | 2,550.0 | 8.0 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 17.0 | 195.836/px | 27.4 | 31.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 24.4 | 136.209/px | 27.4 | 31.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 24.8 | 134.054/px | 27.4 | 31.3 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 24.2 | 137.667/px | 27.4 | 31.3 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 27.2 | 122.266/px | 27.4 | 13.7 | D4; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 53.5 | 62.182/px | 27.4 | 13.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 54.0 | 61.618/px | 27.4 | 13.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 50.9 | 65.375/px | 27.4 | 13.7 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 34.9 | 95.143/px | 27.4 | 5.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 100.8 | 32.973/px | 27.4 | 5.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 100.3 | 33.147/px | 27.4 | 5.6 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 89.9 | 36.973/px | 27.4 | 5.6 | D4,D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 15.7 | 211.990/px | 2,550.0 | 19.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.6 | 135.001/px | 2,550.0 | 19.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.8 | 134.236/px | 2,550.0 | 19.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 23.6 | 141.000/px | 2,550.0 | 19.9 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 65.2 | 50.977/px | 2,550.0 | 6.2 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU-fast | 1,351.7 | 2.460/px | 2,550.0 | 6.2 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU-fast | 1,430.5 | 2.324/px | 2,550.0 | 6.2 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU-fast | 828.4 | 4.014/px | 2,550.0 | 6.2 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 26.0 | 127.784/px | 2,550.0 | 10.7 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 98.4 | 33.792/px | 2,550.0 | 10.7 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 98.6 | 33.734/px | 2,550.0 | 10.7 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 87.2 | 38.115/px | 2,550.0 | 10.7 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 351.4 | 9.462/px | - | 2.2 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 1,262.6 | 2.633/px | - | 2.2 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 1,332.0 | 2.496/px | - | 2.2 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 1,227.9 | 2.708/px | - | 2.2 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 62.2 | 53.427/px | 2,550.0 | 2.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | CPU-fast | 1,686.1 | 1.972/px | 2,550.0 | 2.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | CPU-fast | 1,495.0 | 2.224/px | 2,550.0 | 2.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU-fast | 731.2 | 4.548/px | 2,550.0 | 2.6 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 298.0 | 11.158/px | 27.4 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 1,216.6 | 2.733/px | 27.4 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 1,245.6 | 2.669/px | 27.4 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 1,135.8 | 2.928/px | 27.4 | 1.5 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 66.3 | 50.143/px | 2,550.0 | 6.1 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU-fast | 1,353.2 | 2.457/px | 2,550.0 | 6.1 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU-fast | 1,430.6 | 2.324/px | 2,550.0 | 6.1 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU-fast | 827.4 | 4.019/px | 2,550.0 | 6.1 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 55.0 | 60.461/px | 2,550.0 | 21.4 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 239.0 | 13.913/px | 2,550.0 | 21.4 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 262.8 | 12.651/px | 2,550.0 | 21.4 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 193.5 | 17.188/px | 2,550.0 | 21.4 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 377.6 | 8.805/px | - | 1.6 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 2,237.0 | 1.486/px | - | 1.6 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 2,299.3 | 1.446/px | - | 1.6 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 1,553.1 | 2.141/px | - | 1.6 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 68.1 | 48.859/px | 2,550.0 | 5.0 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU-fast | 1,893.4 | 1.756/px | 2,550.0 | 5.0 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU-fast | 1,990.6 | 1.670/px | 2,550.0 | 5.0 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU-fast | 1,176.3 | 2.827/px | 2,550.0 | 5.0 | claimed |
| Dd.Lock.Back | n/a | - | 9,650.086/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 9,644.688/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 9,513.991/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 2,344.850/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 636,201.194/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | CPU | 25.3 | 131.632/px | 2,550.0 | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 22.2 | 149.502/px | 2,550.0 | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 24.9 | 133.500/px | 2,550.0 | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 21.8 | 152.530/px | 2,550.0 | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 25.0 | 132.986/px | 2,550.0 | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 25.2 | 131.897/px | 2,550.0 | - |  |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU | 330.8 | 10.052/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 296.2 | 11.225/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 57.1 | 58.238/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 319.5 | 10.406/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 163.6 | 20.328/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 324.9 | 10.235/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 259.1 | 12.834/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 17.8 | 186.276/px | 2,550.0 | 7.5 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 395.7 | 8.403/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 350.0 | 9.499/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 36.9 | 90.014/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 394.6 | 8.427/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 102.2 | 32.537/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 400.8 | 8.295/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 173.4 | 19.181/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 11.1 | 300.430/px | 2,550.0 | 14.9 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | CPU | 24.8 | 133.854/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 24.6 | 135.173/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 18.9 | 175.661/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 25.0 | 132.911/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 23.2 | 143.575/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 25.1 | 132.734/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 24.4 | 136.369/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 11.5 | 288.740/px | 27.4 | 10.3 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 25.5 | 130.510/px | 2,550.0 | 10.5 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 18.0 | 184.900/px | 2,550.0 | 10.5 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 25.7 | 129.299/px | 2,550.0 | 10.5 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 22.8 | 145.595/px | 2,550.0 | 10.5 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.8 | 133.852/px | 2,550.0 | 10.5 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 9.4 | 353.129/px | 2,550.0 | 10.5 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 25.4 | 130.993/px | 2,550.0 | 13.3 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 17.8 | 186.882/px | 2,550.0 | 13.3 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 25.7 | 129.393/px | 2,550.0 | 13.3 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.3 | 142.722/px | 2,550.0 | 13.3 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 25.8 | 128.915/px | 2,550.0 | 13.3 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 24.6 | 135.010/px | 2,550.0 | 13.3 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 10.9 | 304.641/px | 2,550.0 | 13.3 |  |
| Gdi.Ellipse.128x128 | CPU | 67.5 | 49.248/px | 2,550.0 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 94,394.761/string | 2,550.0 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 84,392.124/string | 2,550.0 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 81,185.201/string | 2,550.0 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 76,474.562/string | 2,550.0 | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 1,769.3 | 1.879/px | 2,550.0 | 9.8 | D2 -0.01 |
| Gdi.FillRect.128x128 | CPU-fast | 1,105.4 | 3.008/px | 2,550.0 | 9.8 |  |
| Gdi.FillRect.16x16 | CPU | 48.6 | 68.360/px | 2,550.0 | 9.8 |  |
| Gdi.FillRect.256x256 | CPU-fast | 1,516.8 | 2.192/px | 2,550.0 | 9.8 |  |
| Gdi.FillRect.32x32 | CPU | 208.6 | 15.937/px | 2,550.0 | 9.8 |  |
| Gdi.FillRect.512x512 | CPU-fast | 1,694.2 | 1.963/px | 2,550.0 | 9.8 |  |
| Gdi.FillRect.64x64 | CPU | 585.6 | 5.678/px | 2,550.0 | 9.8 |  |
| Gdi.FillRect.8x8 | CPU | 14.5 | 229.365/px | 2,550.0 | 9.8 |  |
| Gdi.LineDiagonal30 | n/a | - | 24,614.245/line | 2,550.0 | - |  |
| Gdi.LineHorizontal | n/a | - | 9,844.134/line | 2,550.0 | - |  |
| Gdi.LineVertical | n/a | - | 35,629.570/line | 2,550.0 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 469.1 | 7.089/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltHatch.128x128 | CPU | 413.5 | 8.041/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltHatch.16x16 | CPU | 55.2 | 60.259/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltHatch.256x256 | CPU | 452.2 | 7.353/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltHatch.32x32 | CPU | 183.6 | 18.109/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltHatch.512x512 | CPU | 471.4 | 7.054/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltHatch.64x64 | CPU | 337.8 | 9.842/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltHatch.8x8 | CPU | 17.3 | 191.864/px | 2,550.0 | 8.6 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 466.7 | 7.124/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltPattern.128x128 | CPU | 405.0 | 8.210/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltPattern.16x16 | CPU | 56.3 | 59.013/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltPattern.256x256 | CPU | 447.0 | 7.438/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltPattern.32x32 | CPU | 181.7 | 18.304/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltPattern.512x512 | CPU | 457.2 | 7.272/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltPattern.64x64 | CPU | 329.7 | 10.084/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltPattern.8x8 | CPU | 17.3 | 192.125/px | 2,550.0 | 7.8 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 1,803.4 | 1.844/px | 2,550.0 | 6.6 |  |
| Gdi.PatBltSolid.128x128 | CPU-fast | 1,290.0 | 2.578/px | 2,550.0 | 6.6 |  |
| Gdi.PatBltSolid.16x16 | CPU | 71.5 | 46.472/px | 2,550.0 | 6.6 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 1,585.6 | 2.097/px | 2,550.0 | 6.6 |  |
| Gdi.PatBltSolid.32x32 | CPU | 339.0 | 9.809/px | 2,550.0 | 6.6 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 1,739.6 | 1.911/px | 2,550.0 | 6.6 |  |
| Gdi.PatBltSolid.64x64 | CPU-fast | 806.2 | 4.124/px | 2,550.0 | 6.6 |  |
| Gdi.PatBltSolid.8x8 | CPU | 17.3 | 192.599/px | 2,550.0 | 6.6 |  |
| Gdi.Polygon.128x128 | CPU | 96.8 | 34.353/px | 2,550.0 | - |  |
| Gdi.Polyline | n/a | - | 1,608.888/line | 2,550.0 | - |  |
| Gdi.Rectangle.128x128 | CPU | 345.0 | 9.637/px | 2,550.0 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 292.5 | 11.368/px | 2,550.0 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.3 | 136.900/px | 2,550.0 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 276.6 | 12.020/px | 2,550.0 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 9.5 | 349.018/px | 2,550.0 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 9.5 | 349.457/px | 2,550.0 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 25.3 | 131.634/px | 2,550.0 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 9.5 | 348.715/px | 2,550.0 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 299.4 | 11.105/px | 2,550.0 | - |  |
| Gdi.RoundRect.128x128 | CPU | 155.9 | 21.333/px | 2,550.0 | - |  |
| Gdi.ScrollBitBlt | CPU | 25.4 | 131.147/px | 2,550.0 | - |  |
| Gdi.ScrollDC | CPU | 25.6 | 129.882/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 374.8 | 8.871/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 376.8 | 8.824/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 390.4 | 8.518/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 389.7 | 8.533/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 127.4 | 26.101/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 122.8 | 27.072/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 129.5 | 25.669/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 123.6 | 26.904/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 147.6 | 22.521/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 149.3 | 22.265/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 150.8 | 22.055/px | 2,550.0 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 151.2 | 21.986/px | 2,550.0 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 3.2 | 1,042.405/px | 2,550.0 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 3.0 | 1,118.726/px | 2,550.0 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 3.2 | 1,047.843/px | 2,550.0 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 3.0 | 1,102.248/px | 2,550.0 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 17.6 | 188.464/px | 2,550.0 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 19.0 | 174.599/px | 2,550.0 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 17.5 | 190.002/px | 2,550.0 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 19.2 | 173.552/px | 2,550.0 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 19.9 | 166.881/px | 2,550.0 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 23.0 | 144.540/px | 2,550.0 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 24.2 | 137.387/px | 2,550.0 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 29.9 | 111.187/px | 2,550.0 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 92,679.524/string | 2,550.0 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 83,389.167/string | 2,550.0 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 83,321.297/string | 2,550.0 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 75,198.858/string | 2,550.0 | - |  |

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
