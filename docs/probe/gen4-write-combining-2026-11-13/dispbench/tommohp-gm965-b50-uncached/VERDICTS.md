# Verdicts: tommohp-gm965-b50-uncached

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
| FbWrite | 130.2 |
| FbRead | 31.3 |
| MemFill | 4,366.5 |
| MemCopy | 1,255.8 |
| DibToDibBlt | 3,769.0 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 59.6 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 59.6 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 20 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 0 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | CPU | 364.5 | 10.947/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 614.0 | 6.498/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU-fast | 3,867.9 | 1.032/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 580.4 | 6.875/px | - | 55.9 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU-fast | 1,016.5 | 3.925/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU-fast | 5,812.7 | 0.686/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 6,060.5 | 0.658/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU-fast | 3,915.8 | 1.019/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 856.7 | 4.657/px | - | 1.4 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 6,960.3 | 0.573/px | - | 1.4 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 8,102.0 | 0.492/px | - | 1.4 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU-fast | 3,946.4 | 1.011/px | - | 1.4 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | HW? | 96.0 | 41.567/px | 130.2 | 142.2 | D4; not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | HW? | 145.9 | 27.343/px | 130.2 | 142.2 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 296.9 | 13.440/px | 130.2 | 142.2 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | HW? | 141.1 | 28.268/px | 130.2 | 142.2 | D4; not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | CPU | 137.8 | 28.954/px | 130.2 | 1.9 | not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 259.0 | 15.405/px | 130.2 | 1.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 259.9 | 15.355/px | 130.2 | 1.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 246.1 | 16.215/px | 130.2 | 1.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 133.5 | 29.887/px | 130.2 | 2.0 | D4; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 258.8 | 15.420/px | 130.2 | 2.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 259.7 | 15.365/px | 130.2 | 2.0 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 244.5 | 16.321/px | 130.2 | 2.0 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 27.1 | 146.973/px | 31.3 | 6.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 30.1 | 132.382/px | 31.3 | 6.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 30.2 | 131.964/px | 31.3 | 6.2 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 29.8 | 133.946/px | 31.3 | 6.2 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 51.1 | 78.106/px | 31.3 | 2.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 60.7 | 65.743/px | 31.3 | 2.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 60.8 | 65.600/px | 31.3 | 2.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 59.9 | 66.633/px | 31.3 | 2.8 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 84.4 | 47.252/px | 31.3 | 1.8 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 118.3 | 33.724/px | 31.3 | 1.8 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 118.4 | 33.693/px | 31.3 | 1.8 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 115.1 | 34.663/px | 31.3 | 1.8 | D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 17.4 | 229.385/px | 130.2 | 14.5 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 19.6 | 203.585/px | 130.2 | 14.5 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 19.7 | 202.523/px | 130.2 | 14.5 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 19.3 | 206.637/px | 130.2 | 14.5 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW | 148.3 | 26.903/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 1,093.4 | 3.649/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 1,116.3 | 3.574/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 929.8 | 4.291/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 49.9 | 79.974/px | 130.2 | 4.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 72.6 | 54.961/px | 130.2 | 4.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 72.9 | 54.745/px | 130.2 | 4.5 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 71.0 | 56.206/px | 130.2 | 4.5 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU-fast | 1,220.0 | 3.270/px | - | 0.5 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU-fast | 5,925.9 | 0.673/px | - | 0.5 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 6,061.1 | 0.658/px | - | 0.5 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU-fast | 4,094.6 | 0.974/px | - | 0.5 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | CPU | 140.6 | 28.378/px | 130.2 | 1.7 | not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 259.2 | 15.396/px | 130.2 | 1.7 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 259.9 | 15.353/px | 130.2 | 1.7 | D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 246.8 | 16.168/px | 130.2 | 1.7 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 1,181.4 | 3.377/px | 31.3 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 5,783.9 | 0.690/px | 31.3 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 6,282.5 | 0.635/px | 31.3 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 3,988.5 | 1.000/px | 31.3 | 0.9 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW | 152.3 | 26.197/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 1,092.6 | 3.652/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 1,116.1 | 3.575/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 931.2 | 4.285/px | 130.2 | 2.5 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | HW? | 97.8 | 40.781/px | 130.2 | 141.9 | D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | HW | 146.1 | 27.318/px | 130.2 | 141.9 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | HW | 296.9 | 13.438/px | 130.2 | 141.9 | D3; D4; not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | HW? | 141.3 | 28.235/px | 130.2 | 141.9 | D4; not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 1,178.5 | 3.386/px | - | 1.5 |  |
| Dd.ColorFill.Sys.256x256 | CPU-fast | 7,620.7 | 0.524/px | - | 1.5 |  |
| Dd.ColorFill.Sys.512x512 | CPU-fast | 9,678.3 | 0.412/px | - | 1.5 |  |
| Dd.ColorFill.Sys.64x64 | CPU-fast | 5,205.5 | 0.767/px | - | 1.5 |  |
| Dd.ColorFill.Vid.16x16 | HW | 159.6 | 24.996/px | 130.2 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 2,470.5 | 1.615/px | 130.2 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 2,608.9 | 1.529/px | 130.2 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 1,589.9 | 2.510/px | 130.2 | 2.8 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 3,283.692/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,287.108/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,333.412/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 791.768/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 4,027,821.429/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 19.4 | 205.958/px | 130.2 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 17.6 | 226.282/px | 130.2 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 19.4 | 205.850/px | 130.2 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 17.6 | 226.300/px | 130.2 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 19.4 | 205.794/px | 130.2 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 19.4 | 205.858/px | 130.2 | - | D6 |
| Gdi.BitBltBitmapToScreen.1280x800 | CPU | 129.6 | 30.794/px | 130.2 | 1.5 |  |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 129.3 | 30.869/px | 130.2 | 1.5 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 98.8 | 40.401/px | 130.2 | 1.5 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 129.4 | 30.846/px | 130.2 | 1.5 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 120.6 | 33.081/px | 130.2 | 1.5 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 129.8 | 30.746/px | 130.2 | 1.5 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 127.6 | 31.271/px | 130.2 | 1.5 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 57.3 | 69.618/px | 130.2 | 1.5 |  |
| Gdi.BitBltDibToScreen.1280x800 | CPU | 129.7 | 30.770/px | 130.2 | 2.8 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 128.5 | 31.058/px | 130.2 | 2.8 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 77.9 | 51.207/px | 130.2 | 2.8 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 129.6 | 30.780/px | 130.2 | 2.8 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 111.5 | 35.791/px | 130.2 | 2.8 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 130.0 | 30.696/px | 130.2 | 2.8 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 124.9 | 31.940/px | 130.2 | 2.8 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 35.9 | 111.226/px | 130.2 | 2.8 |  |
| Gdi.BitBltScreenToBitmap.1280x800 | CPU | 30.7 | 129.894/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 30.6 | 130.560/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 28.6 | 139.451/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 30.7 | 130.107/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 30.6 | 130.306/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 30.8 | 129.404/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 31.1 | 128.289/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 23.3 | 171.202/px | 31.3 | 4.8 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 19.3 | 206.341/px | 130.2 | 3.0 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 18.6 | 214.606/px | 130.2 | 3.0 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 19.4 | 205.692/px | 130.2 | 3.0 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 19.2 | 207.585/px | 130.2 | 3.0 |  |
| Gdi.BitBltScreenToScreen.512x512 | CPU | 19.4 | 205.470/px | 130.2 | 3.0 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 19.4 | 205.952/px | 130.2 | 3.0 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 16.4 | 243.619/px | 130.2 | 3.0 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 19.2 | 207.533/px | 130.2 | 6.4 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 17.4 | 229.524/px | 130.2 | 6.4 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 19.3 | 206.790/px | 130.2 | 6.4 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 18.8 | 212.226/px | 130.2 | 6.4 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 19.4 | 206.179/px | 130.2 | 6.4 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 19.3 | 206.917/px | 130.2 | 6.4 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 14.4 | 276.927/px | 130.2 | 6.4 |  |
| Gdi.Ellipse.128x128 | CPU | 100.6 | 39.654/px | 130.2 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 156,066.474/string | 130.2 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 57,009.960/string | 130.2 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 127,563.894/string | 130.2 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 39,330.535/string | 130.2 | - |  |
| Gdi.FillRect.1280x800 | CPU | 130.2 | 30.653/px | 130.2 | 2.1 | D2 0.00 |
| Gdi.FillRect.128x128 | CPU | 129.2 | 30.888/px | 130.2 | 2.1 |  |
| Gdi.FillRect.16x16 | CPU | 89.9 | 44.405/px | 130.2 | 2.1 |  |
| Gdi.FillRect.256x256 | CPU | 129.4 | 30.840/px | 130.2 | 2.1 |  |
| Gdi.FillRect.32x32 | CPU | 117.2 | 34.051/px | 130.2 | 2.1 |  |
| Gdi.FillRect.512x512 | CPU | 129.9 | 30.716/px | 130.2 | 2.1 |  |
| Gdi.FillRect.64x64 | CPU | 126.7 | 31.501/px | 130.2 | 2.1 |  |
| Gdi.FillRect.8x8 | CPU | 45.6 | 87.482/px | 130.2 | 2.1 |  |
| Gdi.LineDiagonal30 | n/a | - | 7,932.847/line | 130.2 | - |  |
| Gdi.LineHorizontal | n/a | - | 8,771.692/line | 130.2 | - |  |
| Gdi.LineVertical | n/a | - | 9,361.212/line | 130.2 | - |  |
| Gdi.PatBltHatch.1280x800 | CPU | 130.5 | 30.579/px | 130.2 | 1.7 |  |
| Gdi.PatBltHatch.128x128 | CPU | 129.3 | 30.867/px | 130.2 | 1.7 |  |
| Gdi.PatBltHatch.16x16 | CPU | 93.5 | 42.666/px | 130.2 | 1.7 |  |
| Gdi.PatBltHatch.256x256 | CPU | 130.0 | 30.702/px | 130.2 | 1.7 |  |
| Gdi.PatBltHatch.32x32 | CPU | 118.6 | 33.653/px | 130.2 | 1.7 |  |
| Gdi.PatBltHatch.512x512 | CPU | 130.2 | 30.656/px | 130.2 | 1.7 |  |
| Gdi.PatBltHatch.64x64 | CPU | 126.9 | 31.439/px | 130.2 | 1.7 |  |
| Gdi.PatBltHatch.8x8 | CPU | 51.0 | 78.288/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.1280x800 | CPU | 130.3 | 30.619/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.128x128 | CPU | 129.2 | 30.879/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.16x16 | CPU | 93.4 | 42.703/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.256x256 | CPU | 129.9 | 30.712/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.32x32 | CPU | 118.5 | 33.682/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.512x512 | CPU | 130.2 | 30.656/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.64x64 | CPU | 127.0 | 31.407/px | 130.2 | 1.7 |  |
| Gdi.PatBltPattern.8x8 | CPU | 50.5 | 78.961/px | 130.2 | 1.7 |  |
| Gdi.PatBltSolid.1280x800 | CPU | 130.3 | 30.623/px | 130.2 | 1.2 |  |
| Gdi.PatBltSolid.128x128 | CPU | 129.5 | 30.802/px | 130.2 | 1.2 |  |
| Gdi.PatBltSolid.16x16 | CPU | 105.9 | 37.667/px | 130.2 | 1.2 |  |
| Gdi.PatBltSolid.256x256 | CPU | 129.5 | 30.805/px | 130.2 | 1.2 |  |
| Gdi.PatBltSolid.32x32 | CPU | 123.2 | 32.397/px | 130.2 | 1.2 |  |
| Gdi.PatBltSolid.512x512 | CPU | 129.9 | 30.710/px | 130.2 | 1.2 |  |
| Gdi.PatBltSolid.64x64 | CPU | 128.4 | 31.087/px | 130.2 | 1.2 |  |
| Gdi.PatBltSolid.8x8 | CPU | 68.1 | 58.583/px | 130.2 | 1.2 |  |
| Gdi.Polygon.128x128 | CPU | 107.5 | 37.120/px | 130.2 | - |  |
| Gdi.Polyline | n/a | - | 6,193.616/line | 130.2 | - |  |
| Gdi.Rectangle.128x128 | CPU | 123.0 | 32.437/px | 130.2 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 127.9 | 31.192/px | 130.2 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 18.9 | 211.368/px | 130.2 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 127.7 | 31.244/px | 130.2 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 11.7 | 342.476/px | 130.2 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 11.7 | 342.305/px | 130.2 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 19.4 | 205.969/px | 130.2 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 11.7 | 342.204/px | 130.2 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 127.9 | 31.197/px | 130.2 | - |  |
| Gdi.RoundRect.128x128 | CPU | 115.2 | 34.646/px | 130.2 | - |  |
| Gdi.ScrollBitBlt | CPU | 19.3 | 207.163/px | 130.2 | - |  |
| Gdi.ScrollDC | CPU | 19.3 | 207.176/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.1280x800 | CPU | 129.2 | 30.874/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.640x400 | CPU | 129.8 | 30.737/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.1280x800 | CPU | 129.2 | 30.876/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.640x400 | CPU | 129.7 | 30.754/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.1280x800 | CPU | 127.2 | 31.371/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.640x400 | CPU | 129.8 | 30.749/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.1280x800 | CPU | 127.7 | 31.248/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.640x400 | CPU | 129.8 | 30.739/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.1280x800 | CPU | 129.9 | 30.708/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.640x400 | CPU | 129.9 | 30.707/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.1280x800 | CPU | 130.2 | 30.637/px | 130.2 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.640x400 | CPU | 129.9 | 30.712/px | 130.2 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 5.9 | 672.943/px | 130.2 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 5.8 | 691.357/px | 130.2 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 5.9 | 672.879/px | 130.2 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 5.8 | 690.938/px | 130.2 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 43.1 | 92.608/px | 130.2 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 45.7 | 87.311/px | 130.2 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 43.0 | 92.693/px | 130.2 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 45.7 | 87.267/px | 130.2 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 62.7 | 63.636/px | 130.2 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 72.4 | 55.093/px | 130.2 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 62.7 | 63.609/px | 130.2 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 72.4 | 55.127/px | 130.2 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 156,082.368/string | 130.2 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 56,947.897/string | 130.2 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 127,416.998/string | 130.2 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 39,335.081/string | 130.2 | - |  |

## CLAIMED-NOT-DELIVERED

None.
