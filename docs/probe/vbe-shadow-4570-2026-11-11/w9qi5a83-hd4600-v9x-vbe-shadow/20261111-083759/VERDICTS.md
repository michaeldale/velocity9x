# Verdicts: w9qi5a83-hd4600-v9x-vbe-shadow

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
| FbWrite | 11,245.8 |
| FbRead | 227.2 |
| MemFill | 31,029.5 |
| MemCopy | 20,255.7 |
| DibToDibBlt | 8,695.0 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 2,338,899.4 per second against 60 Hz refresh: not refresh-locked |
| Vblank | 60.0 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 104 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 877.7 | 7.275/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,793.3 | 3.561/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,425.1 | 0.612/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,416.6 | 4.507/px | - | 19.2 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,217.0 | 2.880/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 11,964.1 | 0.534/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,695.5 | 0.434/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,274.6 | 0.621/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 1,926.1 | 3.315/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU | 14,098.3 | 0.453/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,667.9 | 0.383/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 8,963.5 | 0.712/px | - | 0.7 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | CPU | 446.2 | 14.310/px | 11,245.8 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | CPU | 1,739.7 | 3.670/px | 11,245.8 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | CPU | 5,914.4 | 1.080/px | 11,245.8 | 17.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | CPU | 1,278.5 | 4.994/px | 11,245.8 | 17.4 | not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 543.8 | 11.742/px | 11,245.8 | 1.2 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 13,396.5 | 0.477/px | 11,245.8 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 16,323.5 | 0.391/px | 11,245.8 | 1.2 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | CPU | 6,294.2 | 1.014/px | 11,245.8 | 1.2 | not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | CPU | 580.1 | 11.006/px | 11,245.8 | 0.8 | not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 17,410.0 | 0.367/px | 11,245.8 | 0.8 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 18,127.5 | 0.352/px | 11,245.8 | 0.8 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | CPU | 5,962.9 | 1.071/px | 11,245.8 | 0.8 | not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 33.9 | 188.235/px | 227.2 | 0.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 35.4 | 180.279/px | 227.2 | 0.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.4 | 180.349/px | 227.2 | 0.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 35.3 | 180.835/px | 227.2 | 0.1 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | CPU | 64.2 | 99.528/px | 227.2 | -1.3 | not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | CPU | 70.8 | 90.201/px | 227.2 | -1.3 | not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | CPU | 70.6 | 90.489/px | 227.2 | -1.3 | not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | CPU | 70.5 | 90.571/px | 227.2 | -1.3 | not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | CPU | 120.3 | 53.091/px | 227.2 | 1.7 | not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | CPU | 142.0 | 44.971/px | 227.2 | 1.7 | not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | CPU | 142.7 | 44.747/px | 227.2 | 1.7 | not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | CPU | 141.5 | 45.120/px | 227.2 | 1.7 | not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 23.9 | 266.762/px | 11,245.8 | 4.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.8 | 257.891/px | 11,245.8 | 4.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.8 | 257.343/px | 11,245.8 | 4.0 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.7 | 258.482/px | 11,245.8 | 4.0 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | CPU | 23.5 | 271.270/px | 11,245.8 | 4.5 | claimed |
| Dd.Blt.VidToVid.Plain.256x256 | CPU | 23.8 | 267.771/px | 11,245.8 | 4.5 | claimed |
| Dd.Blt.VidToVid.Plain.512x512 | CPU | 23.9 | 267.038/px | 11,245.8 | 4.5 | claimed |
| Dd.Blt.VidToVid.Plain.64x64 | CPU | 23.9 | 267.252/px | 11,245.8 | 4.5 | claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 82.2 | 77.699/px | 11,245.8 | 4.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 114.1 | 55.958/px | 11,245.8 | 4.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 115.1 | 55.489/px | 11,245.8 | 4.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 111.5 | 57.276/px | 11,245.8 | 4.3 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | noisy | 2,513.9 | 2.540/px | - | 1.1 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 11,331.1 | 0.564/px | - | 1.1 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,476.2 | 0.441/px | - | 1.1 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 6,057.4 | 1.054/px | - | 1.1 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 617.3 | 10.344/px | 11,245.8 | 1.1 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 13,029.9 | 0.490/px | 11,245.8 | 1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 16,172.7 | 0.395/px | 11,245.8 | 1.1 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | CPU | 6,816.2 | 0.937/px | 11,245.8 | 1.1 | not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 2,524.3 | 2.530/px | 227.2 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 11,973.3 | 0.533/px | 227.2 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 14,489.3 | 0.441/px | 227.2 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 10,651.4 | 0.599/px | 227.2 | 0.7 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | CPU | 23.6 | 270.361/px | 11,245.8 | 1.6 | claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | CPU | 23.9 | 267.378/px | 11,245.8 | 1.6 | claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | CPU | 23.9 | 267.119/px | 11,245.8 | 1.6 | claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | CPU | 23.9 | 267.181/px | 11,245.8 | 1.6 | claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 461.7 | 13.829/px | 11,245.8 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 1,704.4 | 3.746/px | 11,245.8 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 5,888.7 | 1.084/px | 11,245.8 | 17.8 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 1,257.0 | 5.080/px | 11,245.8 | 17.8 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,769.6 | 2.305/px | - | 0.4 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 16,320.1 | 0.391/px | - | 0.4 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 19,001.3 | 0.336/px | - | 0.4 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,415.6 | 0.414/px | - | 0.4 |  |
| Dd.ColorFill.Vid.16x16 | CPU | 720.8 | 8.859/px | 11,245.8 | 1.4 | claimed |
| Dd.ColorFill.Vid.256x256 | CPU | 7,417.0 | 0.861/px | 11,245.8 | 1.4 | claimed |
| Dd.ColorFill.Vid.512x512 | CPU | 8,870.0 | 0.720/px | 11,245.8 | 1.4 | claimed |
| Dd.ColorFill.Vid.64x64 | CPU | 4,451.5 | 1.434/px | 11,245.8 | 1.4 | claimed |
| Dd.Lock.Back | n/a | - | 1,976.612/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 1,977.327/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 1,973.935/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 561.393/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 105,884.573/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 797.4 | 8.007/px | 11,245.8 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 784.7 | 8.137/px | 11,245.8 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 787.1 | 8.112/px | 11,245.8 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 787.1 | 8.112/px | 11,245.8 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 789.8 | 8.085/px | 11,245.8 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 773.7 | 8.253/px | 11,245.8 | - | D6 |
| Gdi.BitBltBitmapToScreen.1024x768 | CPU-fast | 6,930.9 | 0.921/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 3,513.4 | 1.817/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 338.2 | 18.880/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 3,115.6 | 2.049/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 1,079.4 | 5.915/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 5,037.6 | 1.267/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 2,538.8 | 2.515/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 97.3 | 65.643/px | 11,245.8 | 3.6 |  |
| Gdi.BitBltDibToScreen.1024x768 | CPU | 3,143.0 | 2.032/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 2,280.5 | 2.800/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 203.5 | 31.380/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 2,078.3 | 3.072/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 648.4 | 9.847/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 2,720.7 | 2.347/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 1,563.6 | 4.084/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 56.9 | 112.192/px | 11,245.8 | 4.0 |  |
| Gdi.BitBltScreenToBitmap.1024x768 | HW? | 6,774.3 | 0.943/px | 227.2 | 3.6 | D4,D3? |
| Gdi.BitBltScreenToBitmap.128x128 | HW? | 3,429.7 | 1.862/px | 227.2 | 3.6 | D4,D3? |
| Gdi.BitBltScreenToBitmap.16x16 | HW? | 337.5 | 18.920/px | 227.2 | 3.6 | D4,D3? |
| Gdi.BitBltScreenToBitmap.256x256 | HW? | 3,031.2 | 2.106/px | 227.2 | 3.6 | D4,D3? |
| Gdi.BitBltScreenToBitmap.32x32 | HW? | 1,066.0 | 5.990/px | 227.2 | 3.6 | D4,D3? |
| Gdi.BitBltScreenToBitmap.512x512 | HW? | 4,797.5 | 1.331/px | 227.2 | 3.6 | D4,D3? |
| Gdi.BitBltScreenToBitmap.64x64 | HW? | 2,539.9 | 2.514/px | 227.2 | 3.6 | D4,D3? |
| Gdi.BitBltScreenToBitmap.8x8 | HW? | 97.9 | 65.213/px | 227.2 | 3.6 | D4 |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 1,123.4 | 5.684/px | 11,245.8 | 7.1 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 121.4 | 52.585/px | 11,245.8 | 7.1 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 2,122.3 | 3.009/px | 11,245.8 | 7.1 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 285.0 | 22.403/px | 11,245.8 | 7.1 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 608.0 | 10.503/px | 11,245.8 | 7.1 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 48.9 | 130.690/px | 11,245.8 | 7.1 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 3,176.6 | 2.010/px | 11,245.8 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 244.1 | 26.153/px | 11,245.8 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 2,659.6 | 2.401/px | 11,245.8 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 789.8 | 8.085/px | 11,245.8 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 3,854.3 | 1.657/px | 11,245.8 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 2,000.8 | 3.191/px | 11,245.8 | 3.7 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 88.4 | 72.262/px | 11,245.8 | 3.7 |  |
| Gdi.Ellipse.128x128 | CPU | 83.0 | 76.929/px | 11,245.8 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 25,842.308/string | 11,245.8 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 20,498.347/string | 11,245.8 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 24,331.191/string | 11,245.8 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 18,849.223/string | 11,245.8 | - |  |
| Gdi.FillRect.1024x768 | CPU-fast | 7,613.1 | 0.839/px | 11,245.8 | 4.0 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 3,641.8 | 1.753/px | 11,245.8 | 4.0 |  |
| Gdi.FillRect.16x16 | CPU | 259.5 | 24.607/px | 11,245.8 | 4.0 |  |
| Gdi.FillRect.256x256 | CPU | 3,200.4 | 1.995/px | 11,245.8 | 4.0 |  |
| Gdi.FillRect.32x32 | CPU | 876.0 | 7.289/px | 11,245.8 | 4.0 |  |
| Gdi.FillRect.512x512 | CPU | 5,251.4 | 1.216/px | 11,245.8 | 4.0 |  |
| Gdi.FillRect.64x64 | CPU | 2,258.0 | 2.828/px | 11,245.8 | 4.0 |  |
| Gdi.FillRect.8x8 | CPU | 72.1 | 88.609/px | 11,245.8 | 4.0 |  |
| Gdi.LineDiagonal30 | n/a | - | 452,648.344/line | 11,245.8 | - |  |
| Gdi.LineHorizontal | n/a | - | 452,514.554/line | 11,245.8 | - |  |
| Gdi.LineVertical | n/a | - | 453,595.964/line | 11,245.8 | - |  |
| Gdi.PatBltHatch.1024x768 | CPU | 5,287.4 | 1.208/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltHatch.128x128 | CPU | 3,588.6 | 1.779/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltHatch.16x16 | CPU | 294.5 | 21.682/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltHatch.256x256 | CPU | 2,857.9 | 2.234/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltHatch.32x32 | CPU | 927.0 | 6.888/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltHatch.512x512 | CPU | 4,116.4 | 1.551/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltHatch.64x64 | CPU | 2,354.5 | 2.712/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltHatch.8x8 | CPU | 83.4 | 76.582/px | 11,245.8 | 3.2 |  |
| Gdi.PatBltPattern.1024x768 | CPU | 5,286.1 | 1.208/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltPattern.128x128 | CPU | 3,589.2 | 1.779/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltPattern.16x16 | CPU | 286.5 | 22.283/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltPattern.256x256 | CPU | 2,910.1 | 2.194/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltPattern.32x32 | CPU | 926.0 | 6.896/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltPattern.512x512 | CPU | 4,111.1 | 1.553/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltPattern.64x64 | CPU | 2,352.1 | 2.715/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltPattern.8x8 | CPU | 83.3 | 76.617/px | 11,245.8 | 3.1 |  |
| Gdi.PatBltSolid.1024x768 | CPU-fast | 7,636.7 | 0.836/px | 11,245.8 | 3.3 |  |
| Gdi.PatBltSolid.128x128 | CPU | 3,916.9 | 1.630/px | 11,245.8 | 3.3 |  |
| Gdi.PatBltSolid.16x16 | CPU | 386.3 | 16.529/px | 11,245.8 | 3.3 |  |
| Gdi.PatBltSolid.256x256 | CPU | 3,257.3 | 1.960/px | 11,245.8 | 3.3 |  |
| Gdi.PatBltSolid.32x32 | CPU | 1,188.4 | 5.373/px | 11,245.8 | 3.3 |  |
| Gdi.PatBltSolid.512x512 | CPU | 5,294.4 | 1.206/px | 11,245.8 | 3.3 |  |
| Gdi.PatBltSolid.64x64 | CPU | 2,727.9 | 2.341/px | 11,245.8 | 3.3 |  |
| Gdi.PatBltSolid.8x8 | noisy | 113.2 | 56.404/px | 11,245.8 | 3.3 |  |
| Gdi.Polygon.128x128 | CPU | 56.2 | 113.581/px | 11,245.8 | - |  |
| Gdi.Polyline | n/a | - | 1,585.797/line | 11,245.8 | - |  |
| Gdi.Rectangle.128x128 | CPU | 216.0 | 29.558/px | 11,245.8 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 2,293.1 | 2.784/px | 11,245.8 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 574.2 | 11.120/px | 11,245.8 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 2,282.6 | 2.797/px | 11,245.8 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 540.1 | 11.822/px | 11,245.8 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 560.2 | 11.397/px | 11,245.8 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 608.2 | 10.499/px | 11,245.8 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 560.1 | 11.401/px | 11,245.8 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 2,293.6 | 2.784/px | 11,245.8 | - |  |
| Gdi.RoundRect.128x128 | CPU | 109.0 | 58.558/px | 11,245.8 | - |  |
| Gdi.ScrollBitBlt | CPU-fast | 7,017.8 | 0.910/px | 11,245.8 | - |  |
| Gdi.ScrollDC | CPU-fast | 6,959.1 | 0.918/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1024x768 | CPU | 3,045.2 | 2.097/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.512x384 | CPU | 2,667.8 | 2.393/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1024x768 | CPU | 3,068.9 | 2.081/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.512x384 | CPU | 2,637.5 | 2.421/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1024x768 | CPU | 694.1 | 9.200/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.512x384 | CPU | 672.2 | 9.500/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1024x768 | CPU | 694.6 | 9.193/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.512x384 | CPU | 669.5 | 9.537/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1024x768 | CPU | 2,551.3 | 2.503/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.512x384 | CPU | 2,275.9 | 2.806/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1024x768 | CPU | 2,572.0 | 2.483/px | 11,245.8 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.512x384 | CPU | 2,253.2 | 2.834/px | 11,245.8 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 39.9 | 160.042/px | 11,245.8 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 35.9 | 177.832/px | 11,245.8 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 40.0 | 159.490/px | 11,245.8 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 35.9 | 177.670/px | 11,245.8 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 176.0 | 36.284/px | 11,245.8 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 204.4 | 31.245/px | 11,245.8 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 176.2 | 36.245/px | 11,245.8 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 204.0 | 31.302/px | 11,245.8 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 175.8 | 36.314/px | 11,245.8 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 225.5 | 28.313/px | 11,245.8 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 175.2 | 36.455/px | 11,245.8 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 225.4 | 28.334/px | 11,245.8 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 25,748.901/string | 11,245.8 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 20,543.096/string | 11,245.8 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 24,189.843/string | 11,245.8 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 20,811.119/string | 11,245.8 | - |  |

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
