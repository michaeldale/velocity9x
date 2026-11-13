# Verdicts: tommohp-gm965-b51-wc

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 1280x800x16, build 95c470c, driver Velocity9x Intel GMA X3100 (GM965)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 3,310.3 |
| FbRead | 217.4 |
| MemFill | 4,339.7 |
| MemCopy | 1,264.2 |
| DibToDibBlt | 3,744.6 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 59.6 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 59.6 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 20 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 363.9 | 10.964/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 613.9 | 6.500/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 3,887.7 | 1.026/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 580.8 | 6.870/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU-fast | 992.4 | 4.020/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 5,832.7 | 0.684/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 6,035.9 | 0.661/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 3,859.4 | 1.034/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 860.7 | 4.636/px | - | 1.4 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 6,970.9 | 0.572/px | - | 1.4 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 8,125.8 | 0.491/px | - | 1.4 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 3,983.6 | 1.002/px | - | 1.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 162.7 | 24.530/px | 3,310.3 | 7.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 448.3 | 8.901/px | 3,310.3 | 7.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 480.0 | 8.312/px | 3,310.3 | 7.9 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 388.0 | 10.282/px | 3,310.3 | 7.9 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 176.8 | 22.569/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | CPU-fast | 2,994.9 | 1.332/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | CPU-fast | 3,277.1 | 1.218/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU-fast | 2,012.0 | 1.983/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 171.5 | 23.267/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | CPU-fast | 3,018.5 | 1.322/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | CPU-fast | 3,311.0 | 1.205/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU-fast | 2,010.3 | 1.985/px | 3,310.3 | 2.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 27.1 | 147.281/px | 217.4 | 25.7 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 32.2 | 124.098/px | 217.4 | 25.7 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 32.6 | 122.299/px | 217.4 | 25.7 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 30.3 | 131.864/px | 217.4 | 25.7 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | CPU | 83.3 | 47.910/px | 217.4 | -17.2 | not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | CPU | 72.0 | 55.429/px | 217.4 | -17.2 | not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | CPU | 70.1 | 56.912/px | 217.4 | -17.2 | not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | CPU | 75.0 | 53.182/px | 217.4 | -17.2 | not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 128.3 | 31.104/px | 217.4 | 22.1 | D4; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 222.5 | 17.930/px | 217.4 | 22.1 | D4; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 240.5 | 16.589/px | 217.4 | 22.1 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 144.5 | 27.619/px | 217.4 | 22.1 | D4; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 24.7 | 161.849/px | 3,310.3 | 24.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 31.0 | 128.697/px | 3,310.3 | 24.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 31.4 | 127.046/px | 3,310.3 | 24.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 29.2 | 136.729/px | 3,310.3 | 24.9 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 157.8 | 25.279/px | 3,310.3 | 2.6 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU-fast | 1,088.7 | 3.665/px | 3,310.3 | 2.6 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU-fast | 1,117.9 | 3.569/px | 3,310.3 | 2.6 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU-fast | 929.5 | 4.293/px | 3,310.3 | 2.6 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 74.7 | 53.395/px | 3,310.3 | 32.6 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 206.4 | 19.332/px | 3,310.3 | 32.6 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 234.5 | 17.015/px | 3,310.3 | 32.6 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 140.0 | 28.490/px | 3,310.3 | 32.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU-fast | 1,227.4 | 3.251/px | - | 0.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 5,928.7 | 0.673/px | - | 0.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 6,085.0 | 0.656/px | - | 0.6 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 4,060.3 | 0.983/px | - | 0.6 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 181.9 | 21.932/px | 3,310.3 | 2.5 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | CPU-fast | 3,034.8 | 1.315/px | 3,310.3 | 2.5 | not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | CPU-fast | 3,269.4 | 1.220/px | 3,310.3 | 2.5 | not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU-fast | 2,071.1 | 1.927/px | 3,310.3 | 2.5 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 1,174.7 | 3.397/px | 217.4 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 5,790.2 | 0.689/px | 217.4 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 6,282.1 | 0.635/px | 217.4 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 3,996.9 | 0.998/px | 217.4 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 157.4 | 25.349/px | 3,310.3 | 2.5 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU-fast | 1,088.5 | 3.666/px | 3,310.3 | 2.5 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU-fast | 1,115.5 | 3.577/px | 3,310.3 | 2.5 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU-fast | 929.3 | 4.294/px | 3,310.3 | 2.5 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 166.8 | 23.927/px | 3,310.3 | 13.9 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 420.9 | 9.479/px | 3,310.3 | 13.9 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 480.7 | 8.300/px | 3,310.3 | 13.9 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 362.4 | 11.011/px | 3,310.3 | 13.9 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 1,179.6 | 3.383/px | - | 1.5 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 7,618.4 | 0.524/px | - | 1.5 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 9,698.3 | 0.411/px | - | 1.5 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 5,209.3 | 0.766/px | - | 1.5 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 185.9 | 21.464/px | 3,310.3 | 2.6 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU | 2,467.9 | 1.617/px | 3,310.3 | 2.6 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU | 2,608.7 | 1.529/px | 3,310.3 | 2.6 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU | 1,584.1 | 2.519/px | 3,310.3 | 2.6 | claimed |
| Dd.Lock.Back | n/a | - | 3,333.420/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,338.238/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,332.848/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 785.987/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 324,109.519/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | CPU | 60.8 | 65.675/px | 3,310.3 | - |  |
| Gdi.Align.Dx1.64x64 | CPU | 49.8 | 80.150/px | 3,310.3 | - |  |
| Gdi.Align.Dx2.64x64 | CPU | 58.1 | 68.641/px | 3,310.3 | - |  |
| Gdi.Align.Dx3.64x64 | CPU | 49.4 | 80.746/px | 3,310.3 | - |  |
| Gdi.Align.Dx4.64x64 | CPU | 59.1 | 67.555/px | 3,310.3 | - |  |
| Gdi.Align.Dx8.64x64 | CPU | 58.3 | 68.475/px | 3,310.3 | - |  |
| Gdi.BitBltBitmapToScreen.1280x800 | CPU-fast | 2,637.5 | 1.513/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 2,233.9 | 1.786/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 222.1 | 17.963/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 2,573.6 | 1.550/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,074.6 | 3.713/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU-fast | 2,726.0 | 1.464/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,335.2 | 1.709/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 80.6 | 49.509/px | 3,310.3 | 1.8 |  |
| Gdi.BitBltDibToScreen.1280x800 | CPU | 2,142.6 | 1.862/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 1,810.2 | 2.204/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 141.2 | 28.251/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,103.3 | 1.897/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 583.1 | 6.843/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,250.5 | 1.773/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,401.9 | 2.846/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 43.3 | 92.194/px | 3,310.3 | 3.2 |  |
| Gdi.BitBltScreenToBitmap.1280x800 | CPU | 45.7 | 87.333/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 67.4 | 59.213/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 51.2 | 77.964/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 165.2 | 24.154/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 56.8 | 70.295/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 191.3 | 20.856/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 61.8 | 64.599/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 37.5 | 106.379/px | 217.4 | 91.5 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 65.0 | 61.344/px | 3,310.3 | 97.2 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 47.9 | 83.288/px | 3,310.3 | 97.2 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 153.9 | 25.932/px | 3,310.3 | 97.2 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 53.2 | 75.020/px | 3,310.3 | 97.2 |  |
| Gdi.BitBltScreenToScreen.512x512 | CPU | 181.4 | 21.993/px | 3,310.3 | 97.2 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 58.2 | 68.521/px | 3,310.3 | 97.2 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 34.8 | 114.501/px | 3,310.3 | 97.2 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 68.7 | 58.097/px | 3,310.3 | 8.1 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 56.8 | 70.227/px | 3,310.3 | 8.1 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 70.4 | 56.684/px | 3,310.3 | 8.1 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 54.3 | 73.532/px | 3,310.3 | 8.1 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 70.8 | 56.331/px | 3,310.3 | 8.1 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 65.3 | 61.131/px | 3,310.3 | 8.1 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 34.9 | 114.397/px | 3,310.3 | 8.1 |  |
| Gdi.Ellipse.128x128 | CPU | 267.4 | 14.920/px | 3,310.3 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 26,355.770/string | 3,310.3 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 31,067.190/string | 3,310.3 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 27,380.382/string | 3,310.3 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 25,836.643/string | 3,310.3 | - |  |
| Gdi.FillRect.1280x800 | CPU-fast | 3,290.4 | 1.213/px | 3,310.3 | 2.1 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 2,286.2 | 1.745/px | 3,310.3 | 2.1 |  |
| Gdi.FillRect.16x16 | CPU | 183.8 | 21.705/px | 3,310.3 | 2.1 |  |
| Gdi.FillRect.256x256 | CPU-fast | 2,640.9 | 1.511/px | 3,310.3 | 2.1 |  |
| Gdi.FillRect.32x32 | CPU | 886.5 | 4.501/px | 3,310.3 | 2.1 |  |
| Gdi.FillRect.512x512 | CPU-fast | 2,781.2 | 1.435/px | 3,310.3 | 2.1 |  |
| Gdi.FillRect.64x64 | CPU | 2,063.7 | 1.933/px | 3,310.3 | 2.1 |  |
| Gdi.FillRect.8x8 | CPU | 58.8 | 67.875/px | 3,310.3 | 2.1 |  |
| Gdi.LineDiagonal30 | n/a | - | 4,518.331/line | 3,310.3 | - |  |
| Gdi.LineHorizontal | n/a | - | 3,121.227/line | 3,310.3 | - |  |
| Gdi.LineVertical | n/a | - | 8,508.616/line | 3,310.3 | - |  |
| Gdi.PatBltHatch.1280x800 | CPU | 1,913.8 | 2.085/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltHatch.128x128 | CPU | 1,433.5 | 2.783/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltHatch.16x16 | CPU | 200.9 | 19.865/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltHatch.256x256 | CPU | 1,674.8 | 2.382/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltHatch.32x32 | CPU | 899.3 | 4.437/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltHatch.512x512 | CPU | 1,814.7 | 2.199/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltHatch.64x64 | CPU | 1,592.2 | 2.506/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltHatch.8x8 | CPU | 68.6 | 58.161/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.1280x800 | CPU | 1,914.5 | 2.084/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.128x128 | CPU | 1,433.7 | 2.783/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.16x16 | CPU | 201.5 | 19.806/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.256x256 | CPU | 1,677.0 | 2.379/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.32x32 | CPU | 899.0 | 4.438/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.512x512 | CPU | 1,811.6 | 2.202/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.64x64 | CPU | 1,591.5 | 2.507/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltPattern.8x8 | CPU | 68.5 | 58.237/px | 3,310.3 | 2.7 |  |
| Gdi.PatBltSolid.1280x800 | CPU-fast | 3,294.6 | 1.211/px | 3,310.3 | 1.2 |  |
| Gdi.PatBltSolid.128x128 | CPU | 2,414.7 | 1.652/px | 3,310.3 | 1.2 |  |
| Gdi.PatBltSolid.16x16 | CPU | 263.1 | 15.163/px | 3,310.3 | 1.2 |  |
| Gdi.PatBltSolid.256x256 | CPU-fast | 2,734.4 | 1.459/px | 3,310.3 | 1.2 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,370.4 | 2.912/px | 3,310.3 | 1.2 |  |
| Gdi.PatBltSolid.512x512 | CPU-fast | 2,795.9 | 1.427/px | 3,310.3 | 1.2 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,600.8 | 1.534/px | 3,310.3 | 1.2 |  |
| Gdi.PatBltSolid.8x8 | CPU | 103.7 | 38.463/px | 3,310.3 | 1.2 |  |
| Gdi.Polygon.128x128 | CPU | 378.0 | 10.555/px | 3,310.3 | - |  |
| Gdi.Polyline | n/a | - | 821.269/line | 3,310.3 | - |  |
| Gdi.Rectangle.128x128 | CPU | 710.4 | 5.616/px | 3,310.3 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 572.9 | 6.965/px | 3,310.3 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 57.4 | 69.470/px | 3,310.3 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 570.7 | 6.991/px | 3,310.3 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 35.2 | 113.365/px | 3,310.3 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 38.4 | 103.963/px | 3,310.3 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 58.3 | 68.471/px | 3,310.3 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 38.3 | 104.061/px | 3,310.3 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 569.8 | 7.003/px | 3,310.3 | - |  |
| Gdi.RoundRect.128x128 | CPU | 502.2 | 7.945/px | 3,310.3 | - |  |
| Gdi.ScrollBitBlt | CPU | 210.4 | 18.968/px | 3,310.3 | - |  |
| Gdi.ScrollDC | CPU | 210.2 | 18.980/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1280x800 | CPU | 1,921.6 | 2.076/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.640x400 | CPU | 2,239.6 | 1.782/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1280x800 | CPU | 1,964.7 | 2.031/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.640x400 | CPU | 2,268.1 | 1.759/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1280x800 | CPU | 558.0 | 7.151/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.640x400 | CPU | 584.4 | 6.827/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1280x800 | CPU | 574.5 | 6.945/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.640x400 | CPU | 586.3 | 6.805/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1280x800 | CPU | 544.7 | 7.325/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.640x400 | CPU | 542.3 | 7.358/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1280x800 | CPU | 545.0 | 7.321/px | 3,310.3 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.640x400 | CPU | 541.8 | 7.364/px | 3,310.3 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 17.3 | 231.005/px | 3,310.3 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 10.3 | 388.040/px | 3,310.3 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 17.3 | 230.656/px | 3,310.3 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 10.4 | 383.944/px | 3,310.3 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 79.6 | 50.126/px | 3,310.3 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 95.0 | 41.984/px | 3,310.3 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 79.6 | 50.098/px | 3,310.3 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 94.4 | 42.266/px | 3,310.3 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 114.0 | 34.999/px | 3,310.3 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 153.6 | 25.971/px | 3,310.3 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 114.0 | 34.985/px | 3,310.3 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 153.6 | 25.972/px | 3,310.3 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 26,400.232/string | 3,310.3 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 30,981.088/string | 3,310.3 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 27,554.235/string | 3,310.3 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 27,505.680/string | 3,310.3 | - |  |

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
- Dd.ColorFill.Vid.256x256: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.512x512: the caps claim it, the timing says CPU
- Dd.ColorFill.Vid.64x64: the caps claim it, the timing says CPU
