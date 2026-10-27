# Verdicts: w9qi5a83-hd4600-v9x

Derived by parse-bench.ps1 from the reports beside this file; re-derived
by run-checks.ps1. Caveats, always printed: on Win9x GetDeviceCaps
RASTERCAPS says nothing about GDI acceleration (the DIB engine sets it);
HW is proof (D1 still-drawing, D2 overlap, D3 above the machine's own
ceiling), HW? is a hint only (D4 setup cost, D5 ROP step, D6 flat
alignment), CPU-fast is a CPU path near the DIB engine's own speed.
Thresholds are provisional (scripts\lib\DbVerdict.ps1, DbBench.ps1).
There is no single score.

Environment metal, mode 800x600x16, build f2dd1cd, driver Velocity9x Intel HD Graphics 4600 (Haswell)

| Ceiling | MB/s |
|---|---:|
| FbWrite | 66.0 |
| FbRead | 35.5 |
| MemFill | 31,064.5 |
| MemCopy | 19,623.7 |
| DibToDibBlt | 8,683.0 |

| Check | Result |
|---|---|
| CRTC (D7) | GetScanLine failed 1000 of 1000 times, reached line 0: no CRTC answer |
| Flip | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Vblank | 60.3 per second = refresh (60 Hz): locked to the CRTC |
| Video memory (D8) | 230 256x256 offscreen surfaces fit (stopped at 0x8876017C) |
| Async (D1) | 4523 still-drawing observations over 7 trials (Dd.AsyncProbe.VidToVid.512x512) |

| Operation | Verdict | MB/s | cyc/unit | ceiling MB/s | setup us | evidence |
|---|---|---:|---:|---:|---:|---|
| Dd.Blt.SysToSys.KeySrc.16x16 | noisy | 573.0 | 11.143/px | - | 19.7 | not claimed |
| Dd.Blt.SysToSys.KeySrc.256x256 | CPU | 1,778.0 | 3.591/px | - | 19.7 | not claimed |
| Dd.Blt.SysToSys.KeySrc.512x512 | CPU | 10,299.7 | 0.620/px | - | 19.7 | not claimed |
| Dd.Blt.SysToSys.KeySrc.64x64 | CPU | 1,283.6 | 4.974/px | - | 19.7 | not claimed |
| Dd.Blt.SysToSys.Plain.16x16 | CPU | 2,221.2 | 2.875/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.256x256 | CPU | 11,978.8 | 0.533/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.512x512 | CPU-fast | 14,702.0 | 0.434/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Plain.64x64 | CPU | 10,266.1 | 0.622/px | - | 0.7 | not claimed |
| Dd.Blt.SysToSys.Stretch.16x16 | CPU | 616.5 | 10.356/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.256x256 | CPU-fast | 14,506.7 | 0.440/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.512x512 | CPU-fast | 16,689.4 | 0.383/px | - | 1.1 | not claimed |
| Dd.Blt.SysToSys.Stretch.64x64 | CPU | 5,441.1 | 1.174/px | - | 1.1 | not claimed |
| Dd.Blt.SysToVid.KeySrc.16x16 | HW? | 64.1 | 99.598/px | 66.0 | 276.6 | D4; not claimed |
| Dd.Blt.SysToVid.KeySrc.256x256 | HW? | 74.2 | 86.002/px | 66.0 | 276.6 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.512x512 | HW? | 150.7 | 42.370/px | 66.0 | 276.6 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.KeySrc.64x64 | HW? | 73.8 | 86.490/px | 66.0 | 276.6 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Plain.16x16 | HW? | 103.2 | 61.866/px | 66.0 | 1.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.256x256 | HW? | 131.3 | 48.631/px | 66.0 | 1.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.512x512 | HW? | 131.8 | 48.438/px | 66.0 | 1.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Plain.64x64 | HW? | 130.2 | 49.036/px | 66.0 | 1.9 | D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.16x16 | HW? | 102.3 | 62.438/px | 66.0 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.256x256 | HW? | 131.0 | 48.755/px | 66.0 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.512x512 | HW? | 131.6 | 48.517/px | 66.0 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.SysToVid.Stretch.64x64 | HW? | 129.9 | 49.156/px | 66.0 | 2.1 | D4,D3?; not claimed |
| Dd.Blt.VidToSys.KeySrc.16x16 | CPU | 32.7 | 195.232/px | 35.5 | 1.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.256x256 | CPU | 35.4 | 180.206/px | 35.5 | 1.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.512x512 | CPU | 35.4 | 180.136/px | 35.5 | 1.1 | not claimed |
| Dd.Blt.VidToSys.KeySrc.64x64 | CPU | 35.3 | 180.644/px | 35.5 | 1.1 | not claimed |
| Dd.Blt.VidToSys.Plain.16x16 | HW? | 60.6 | 105.355/px | 35.5 | 1.6 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.256x256 | HW? | 70.5 | 90.553/px | 35.5 | 1.6 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.512x512 | HW? | 70.6 | 90.396/px | 35.5 | 1.6 | D3?; not claimed |
| Dd.Blt.VidToSys.Plain.64x64 | HW? | 70.3 | 90.821/px | 35.5 | 1.6 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.16x16 | HW? | 108.2 | 59.025/px | 35.5 | -1.8 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.256x256 | HW? | 140.7 | 45.370/px | 35.5 | -1.8 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.512x512 | HW? | 139.5 | 45.766/px | 35.5 | -1.8 | D3?; not claimed |
| Dd.Blt.VidToSys.Stretch.64x64 | HW? | 139.0 | 45.946/px | 35.5 | -1.8 | D3?; not claimed |
| Dd.Blt.VidToVid.KeySrc.16x16 | CPU | 22.7 | 281.164/px | 66.0 | -3.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.256x256 | CPU | 24.6 | 259.059/px | 66.0 | -3.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.512x512 | CPU | 24.6 | 259.831/px | 66.0 | -3.9 | not claimed |
| Dd.Blt.VidToVid.KeySrc.64x64 | CPU | 24.6 | 259.523/px | 66.0 | -3.9 | not claimed |
| Dd.Blt.VidToVid.Plain.16x16 | HW | 154.4 | 41.342/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.256x256 | HW | 2,423.2 | 2.635/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.512x512 | HW | 2,611.9 | 2.445/px | 66.0 | 3.9 | D1 4523; D3; D4; claimed |
| Dd.Blt.VidToVid.Plain.64x64 | HW | 1,153.1 | 5.537/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.Blt.VidToVid.Stretch.16x16 | CPU | 52.6 | 121.341/px | 66.0 | 3.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.256x256 | CPU | 65.8 | 97.024/px | 66.0 | 3.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.512x512 | CPU | 66.0 | 96.690/px | 66.0 | 3.3 | not claimed |
| Dd.Blt.VidToVid.Stretch.64x64 | CPU | 65.3 | 97.801/px | 66.0 | 3.3 | not claimed |
| Dd.BltFast.SysToSys.Plain.16x16 | CPU | 2,514.3 | 2.540/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.256x256 | CPU | 11,352.0 | 0.562/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.512x512 | CPU-fast | 14,647.3 | 0.436/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToSys.Plain.64x64 | CPU | 10,666.1 | 0.599/px | - | 0.9 | not claimed |
| Dd.BltFast.SysToVid.Plain.16x16 | HW? | 104.0 | 61.368/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.256x256 | HW? | 131.3 | 48.625/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.512x512 | HW? | 132.0 | 48.373/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.BltFast.SysToVid.Plain.64x64 | HW? | 129.9 | 49.172/px | 66.0 | 2.3 | D4,D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.16x16 | HW? | 57.7 | 110.572/px | 35.5 | 1.3 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.256x256 | HW? | 70.6 | 90.445/px | 35.5 | 1.3 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.512x512 | HW? | 70.6 | 90.401/px | 35.5 | 1.3 | D3?; not claimed |
| Dd.BltFast.VidToSys.Plain.64x64 | HW? | 70.1 | 91.141/px | 35.5 | 1.3 | D3?; not claimed |
| Dd.BltFast.VidToVid.Plain.16x16 | HW | 156.7 | 40.737/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.256x256 | HW | 2,422.5 | 2.636/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.512x512 | HW | 2,611.8 | 2.445/px | 66.0 | 3.9 | D1 4523; D3; D4; claimed |
| Dd.BltFast.VidToVid.Plain.64x64 | HW | 1,153.3 | 5.536/px | 66.0 | 3.9 | D3; D4; claimed |
| Dd.BltFast.VidToVid.SrcColorKey.16x16 | CPU | 22.7 | 281.475/px | 66.0 | -3.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.256x256 | CPU | 24.6 | 259.276/px | 66.0 | -3.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.512x512 | CPU | 24.6 | 259.896/px | 66.0 | -3.2 | not claimed |
| Dd.BltFast.VidToVid.SrcColorKey.64x64 | CPU | 24.6 | 259.347/px | 66.0 | -3.2 | not claimed |
| Dd.ColorFill.Sys.16x16 | CPU | 2,769.5 | 2.306/px | - | 0.6 |  |
| Dd.ColorFill.Sys.256x256 | CPU | 15,483.4 | 0.412/px | - | 0.6 |  |
| Dd.ColorFill.Sys.512x512 | CPU | 19,167.0 | 0.333/px | - | 0.6 |  |
| Dd.ColorFill.Sys.64x64 | CPU | 15,403.7 | 0.415/px | - | 0.6 |  |
| Dd.ColorFill.Vid.16x16 | HW | 195.7 | 32.631/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.256x256 | HW | 4,616.7 | 1.383/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.512x512 | HW | 4,980.6 | 1.282/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.ColorFill.Vid.64x64 | HW | 1,735.6 | 3.679/px | 66.0 | 2.8 | D3; D4; claimed |
| Dd.Lock.Back | n/a | - | 3,394.183/lock | - | - |  |
| Dd.Lock.OffscreenVid | n/a | - | 3,396.923/lock | - | - |  |
| Dd.Lock.Primary | n/a | - | 3,390.435/lock | - | - |  |
| Dd.Lock.Sys | n/a | - | 557.742/lock | - | - |  |
| Dd.LockAfterBlt.VidToVid | n/a | - | 645,193.721/lock | - | - |  |
| Gdi.Align.Dx0.64x64 | HW? | 24.8 | 257.745/px | 66.0 | - | D6 |
| Gdi.Align.Dx1.64x64 | HW? | 23.6 | 270.993/px | 66.0 | - | D6 |
| Gdi.Align.Dx2.64x64 | HW? | 24.8 | 257.872/px | 66.0 | - | D6 |
| Gdi.Align.Dx3.64x64 | HW? | 23.6 | 270.607/px | 66.0 | - | D6 |
| Gdi.Align.Dx4.64x64 | HW? | 24.8 | 257.584/px | 66.0 | - | D6 |
| Gdi.Align.Dx8.64x64 | HW? | 24.8 | 257.697/px | 66.0 | - | D6 |
| Gdi.BitBltBitmapToScreen.128x128 | CPU | 65.7 | 97.200/px | 66.0 | 1.0 |  |
| Gdi.BitBltBitmapToScreen.16x16 | CPU | 61.8 | 103.272/px | 66.0 | 1.0 |  |
| Gdi.BitBltBitmapToScreen.256x256 | CPU | 65.5 | 97.458/px | 66.0 | 1.0 |  |
| Gdi.BitBltBitmapToScreen.32x32 | CPU | 64.7 | 98.691/px | 66.0 | 1.0 |  |
| Gdi.BitBltBitmapToScreen.512x512 | CPU | 65.7 | 97.210/px | 66.0 | 1.0 |  |
| Gdi.BitBltBitmapToScreen.64x64 | CPU | 65.5 | 97.493/px | 66.0 | 1.0 |  |
| Gdi.BitBltBitmapToScreen.800x600 | CPU | 65.7 | 97.158/px | 66.0 | 1.0 |  |
| Gdi.BitBltBitmapToScreen.8x8 | CPU | 52.0 | 122.710/px | 66.0 | 1.0 |  |
| Gdi.BitBltDibToScreen.128x128 | CPU | 65.6 | 97.379/px | 66.0 | 2.0 |  |
| Gdi.BitBltDibToScreen.16x16 | CPU | 55.6 | 114.930/px | 66.0 | 2.0 |  |
| Gdi.BitBltDibToScreen.256x256 | CPU | 65.5 | 97.515/px | 66.0 | 2.0 |  |
| Gdi.BitBltDibToScreen.32x32 | CPU | 62.1 | 102.821/px | 66.0 | 2.0 |  |
| Gdi.BitBltDibToScreen.512x512 | CPU | 65.7 | 97.233/px | 66.0 | 2.0 |  |
| Gdi.BitBltDibToScreen.64x64 | CPU | 64.8 | 98.506/px | 66.0 | 2.0 |  |
| Gdi.BitBltDibToScreen.800x600 | CPU | 65.7 | 97.170/px | 66.0 | 2.0 |  |
| Gdi.BitBltDibToScreen.8x8 | CPU | 37.6 | 169.634/px | 66.0 | 2.0 |  |
| Gdi.BitBltScreenToBitmap.128x128 | CPU | 35.4 | 180.158/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToBitmap.16x16 | CPU | 33.3 | 191.749/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToBitmap.256x256 | CPU | 35.3 | 180.882/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToBitmap.32x32 | CPU | 34.9 | 183.045/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToBitmap.512x512 | CPU | 35.4 | 180.474/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToBitmap.64x64 | CPU | 35.3 | 180.908/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToBitmap.800x600 | CPU | 35.4 | 180.236/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToBitmap.8x8 | CPU | 28.4 | 225.089/px | 35.5 | 1.4 |  |
| Gdi.BitBltScreenToScreen.128x128 | CPU | 24.7 | 258.255/px | 66.0 | 3.1 |  |
| Gdi.BitBltScreenToScreen.16x16 | CPU | 23.6 | 270.912/px | 66.0 | 3.1 |  |
| Gdi.BitBltScreenToScreen.256x256 | CPU | 24.9 | 256.203/px | 66.0 | 3.1 | D2 0.00 |
| Gdi.BitBltScreenToScreen.32x32 | CPU | 24.5 | 261.136/px | 66.0 | 3.1 |  |
| Gdi.BitBltScreenToScreen.64x64 | CPU | 24.7 | 258.412/px | 66.0 | 3.1 |  |
| Gdi.BitBltScreenToScreen.8x8 | CPU | 21.2 | 301.050/px | 66.0 | 3.1 |  |
| Gdi.BitBltScreenToScreenOverlap.128x128 | CPU | 24.2 | 264.308/px | 66.0 | 3.0 |  |
| Gdi.BitBltScreenToScreenOverlap.16x16 | CPU | 22.6 | 282.428/px | 66.0 | 3.0 |  |
| Gdi.BitBltScreenToScreenOverlap.256x256 | CPU | 24.1 | 264.444/px | 66.0 | 3.0 |  |
| Gdi.BitBltScreenToScreenOverlap.32x32 | CPU | 23.8 | 268.801/px | 66.0 | 3.0 |  |
| Gdi.BitBltScreenToScreenOverlap.512x512 | CPU | 24.2 | 263.915/px | 66.0 | 3.0 |  |
| Gdi.BitBltScreenToScreenOverlap.64x64 | CPU | 24.0 | 266.086/px | 66.0 | 3.0 |  |
| Gdi.BitBltScreenToScreenOverlap.8x8 | CPU | 20.9 | 306.067/px | 66.0 | 3.0 |  |
| Gdi.Ellipse.128x128 | CPU | 59.6 | 107.218/px | 66.0 | - |  |
| Gdi.ExtTextOut.FixedOpaque | n/a | - | 473,003.512/string | 66.0 | - |  |
| Gdi.ExtTextOut.FixedTransparent | n/a | - | 151,469.134/string | 66.0 | - |  |
| Gdi.ExtTextOut.TrueTypeOpaque | n/a | - | 379,466.793/string | 66.0 | - |  |
| Gdi.ExtTextOut.TrueTypeTransparent | n/a | - | 101,251.844/string | 66.0 | - |  |
| Gdi.FillRect.128x128 | CPU | 65.7 | 97.194/px | 66.0 | 1.3 |  |
| Gdi.FillRect.16x16 | CPU | 59.1 | 107.982/px | 66.0 | 1.3 |  |
| Gdi.FillRect.256x256 | CPU | 65.5 | 97.427/px | 66.0 | 1.3 |  |
| Gdi.FillRect.32x32 | CPU | 64.0 | 99.708/px | 66.0 | 1.3 |  |
| Gdi.FillRect.512x512 | CPU | 65.7 | 97.183/px | 66.0 | 1.3 |  |
| Gdi.FillRect.64x64 | CPU | 65.4 | 97.707/px | 66.0 | 1.3 |  |
| Gdi.FillRect.800x600 | CPU | 65.7 | 97.236/px | 66.0 | 1.3 | D2 0.00 |
| Gdi.FillRect.8x8 | CPU | 44.1 | 144.769/px | 66.0 | 1.3 |  |
| Gdi.LineDiagonal30 | n/a | - | 19,157.415/line | 66.0 | - |  |
| Gdi.LineHorizontal | n/a | - | 21,819.650/line | 66.0 | - |  |
| Gdi.LineVertical | n/a | - | 21,312.904/line | 66.0 | - |  |
| Gdi.PatBltHatch.128x128 | CPU | 65.7 | 97.238/px | 66.0 | 1.6 |  |
| Gdi.PatBltHatch.16x16 | CPU | 59.7 | 106.885/px | 66.0 | 1.6 |  |
| Gdi.PatBltHatch.256x256 | CPU | 65.5 | 97.517/px | 66.0 | 1.6 |  |
| Gdi.PatBltHatch.32x32 | CPU | 63.0 | 101.393/px | 66.0 | 1.6 |  |
| Gdi.PatBltHatch.512x512 | CPU | 65.7 | 97.175/px | 66.0 | 1.6 |  |
| Gdi.PatBltHatch.64x64 | CPU | 65.4 | 97.699/px | 66.0 | 1.6 |  |
| Gdi.PatBltHatch.800x600 | CPU | 65.7 | 97.122/px | 66.0 | 1.6 |  |
| Gdi.PatBltHatch.8x8 | CPU | 46.6 | 136.894/px | 66.0 | 1.6 |  |
| Gdi.PatBltPattern.128x128 | CPU | 65.7 | 97.181/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.16x16 | CPU | 59.7 | 106.881/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.256x256 | CPU | 65.5 | 97.462/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.32x32 | CPU | 64.1 | 99.600/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.512x512 | CPU | 65.7 | 97.199/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.64x64 | CPU | 65.3 | 97.758/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.800x600 | CPU | 65.7 | 97.187/px | 66.0 | 1.2 |  |
| Gdi.PatBltPattern.8x8 | CPU | 46.7 | 136.693/px | 66.0 | 1.2 |  |
| Gdi.PatBltSolid.128x128 | CPU | 65.7 | 97.118/px | 66.0 | 0.8 |  |
| Gdi.PatBltSolid.16x16 | CPU | 63.1 | 101.250/px | 66.0 | 0.8 |  |
| Gdi.PatBltSolid.256x256 | CPU | 65.5 | 97.432/px | 66.0 | 0.8 |  |
| Gdi.PatBltSolid.32x32 | CPU | 65.0 | 98.167/px | 66.0 | 0.8 |  |
| Gdi.PatBltSolid.512x512 | CPU | 65.7 | 97.192/px | 66.0 | 0.8 |  |
| Gdi.PatBltSolid.64x64 | CPU | 65.6 | 97.360/px | 66.0 | 0.8 |  |
| Gdi.PatBltSolid.800x600 | CPU | 65.8 | 97.106/px | 66.0 | 0.8 |  |
| Gdi.PatBltSolid.8x8 | CPU | 55.5 | 115.021/px | 66.0 | 0.8 |  |
| Gdi.Polygon.128x128 | CPU | 58.1 | 109.880/px | 66.0 | - |  |
| Gdi.Polyline | n/a | - | 19,593.433/line | 66.0 | - |  |
| Gdi.Rectangle.128x128 | CPU | 62.7 | 101.789/px | 66.0 | - |  |
| Gdi.Rop.BLACKNESS.64x64 | CPU | 65.7 | 97.142/px | 66.0 | - |  |
| Gdi.Rop.NOTSRCCOPY.64x64 | CPU | 24.7 | 258.777/px | 66.0 | - |  |
| Gdi.Rop.PATCOPY.64x64 | CPU | 65.6 | 97.318/px | 66.0 | - |  |
| Gdi.Rop.PSDPXAX.64x64 | CPU | 14.9 | 429.772/px | 66.0 | - |  |
| Gdi.Rop.SRCAND.64x64 | CPU | 14.9 | 428.375/px | 66.0 | - |  |
| Gdi.Rop.SRCCOPY.64x64 | CPU | 24.7 | 258.303/px | 66.0 | - |  |
| Gdi.Rop.SRCINVERT.64x64 | CPU | 14.9 | 428.411/px | 66.0 | - |  |
| Gdi.Rop.WHITENESS.64x64 | CPU | 65.7 | 97.116/px | 66.0 | - |  |
| Gdi.RoundRect.128x128 | CPU | 61.8 | 103.391/px | 66.0 | - |  |
| Gdi.ScrollBitBlt | CPU | 23.6 | 271.059/px | 66.0 | - |  |
| Gdi.ScrollDC | CPU | 24.1 | 265.015/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.400x300 | CPU | 65.6 | 97.381/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16BottomUp.800x600 | CPU | 65.6 | 97.279/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.400x300 | CPU | 65.6 | 97.405/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.16TopDown.800x600 | CPU | 65.6 | 97.365/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.400x300 | CPU | 65.6 | 97.385/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24BottomUp.800x600 | CPU | 65.6 | 97.293/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.400x300 | CPU | 65.6 | 97.403/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.24TopDown.800x600 | CPU | 65.7 | 97.213/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.400x300 | CPU | 65.6 | 97.330/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8BottomUp.800x600 | CPU | 65.7 | 97.166/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.400x300 | CPU | 65.6 | 97.364/px | 66.0 | - |  |
| Gdi.SetDIBitsToDevice.8TopDown.800x600 | CPU | 65.7 | 97.186/px | 66.0 | - |  |
| Gdi.StretchBltDown.128x128 | CPU | 6.6 | 970.567/px | 66.0 | - |  |
| Gdi.StretchBltDown.64x64 | CPU | 6.5 | 986.448/px | 66.0 | - |  |
| Gdi.StretchBltDownHalftone.128x128 | CPU | 6.6 | 970.190/px | 66.0 | - |  |
| Gdi.StretchBltDownHalftone.64x64 | CPU | 6.4 | 991.681/px | 66.0 | - |  |
| Gdi.StretchBltUp.128x128 | CPU | 36.3 | 176.063/px | 66.0 | - |  |
| Gdi.StretchBltUp.256x256 | CPU | 37.3 | 171.090/px | 66.0 | - |  |
| Gdi.StretchBltUpHalftone.128x128 | CPU | 36.3 | 176.023/px | 66.0 | - |  |
| Gdi.StretchBltUpHalftone.256x256 | CPU | 37.3 | 171.028/px | 66.0 | - |  |
| Gdi.StretchDIBits.128x128 | CPU | 48.4 | 131.952/px | 66.0 | - |  |
| Gdi.StretchDIBits.256x256 | CPU | 51.6 | 123.793/px | 66.0 | - |  |
| Gdi.StretchDIBitsHalftone.128x128 | CPU | 48.4 | 131.976/px | 66.0 | - |  |
| Gdi.StretchDIBitsHalftone.256x256 | CPU | 51.5 | 123.926/px | 66.0 | - |  |
| Gdi.TextOut.FixedOpaque | n/a | - | 472,678.602/string | 66.0 | - |  |
| Gdi.TextOut.FixedTransparent | n/a | - | 151,416.701/string | 66.0 | - |  |
| Gdi.TextOut.TrueTypeOpaque | n/a | - | 379,410.444/string | 66.0 | - |  |
| Gdi.TextOut.TrueTypeTransparent | n/a | - | 101,258.174/string | 66.0 | - |  |

## CLAIMED-NOT-DELIVERED

None.
