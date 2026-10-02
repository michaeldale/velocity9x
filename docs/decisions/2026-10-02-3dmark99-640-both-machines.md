# 3DMark 99 Max at 640x480 on both machines: netbook 1253, Gateway 604

Date: 2026-10-02
Machines and drivers, as left by the day's work (no switch changed):
- MICHAEL-NETBOOK, GMA 950, boot 88: Intel set at ABI 2026100106, the HAL
  from `5653a86` (textures to 1024), the ICD at `bafa3d1`, async
  submission on by default.
- Gateway SOLO2150, Rage Mobility-M, boot 89: ATI set from `d978707`.
Evidence: [`../probe/3dmark99-640-both-2026-10-02/`](../probe/3dmark99-640-both-2026-10-02/)

Recorded at Michael's request with a comparison against the last figures
on file; per the 2026-09-26 rule these are not chased.

## Procedure

`3dmark.exe` launched over the agent; New 3DMark Benchmark (the netbook
first warns that it has no 800x600); Display and CPU Settings: 640x480 16
bit, 16-bit Z, triple frame buffer, Pentium III optimisations; all tests
selected, looping off, repeat 1, titles and sounds as installed;
Benchmark. The two ran at the same time on their own machines. No screen
was captured on the Gateway until `V9XWND` showed the score dialog.
`V9XTRACE` before and after each.

## Results

| | netbook | Gateway |
|---|---|---|
| 3DMarks | **1253** | **604** |
| CPU 3DMarks | 14752 | 7075 |
| duration | 10:02:42 to before 10:12 | 10:05:57 to before 10:31 |
| engine draws | 423,071 submitted, 428,532 async + 5,611 sync | 189,231 Mach64 draws, 7,252,857 triangles |
| refused | 416 (reason 6, vertices) | 0 |
| timeouts, abandonments, resets | 0 | 0 |
| texture creates | 28,809 | 21,024 |

## Against the last figures on file

| | 3DMarks | CPU | when, build, conditions |
|---|---|---|---|
| netbook, now | **1253** | 14752 | boot 88, async, textures to 1024 |
| netbook, last 640x480 | 852 | 14762 | 2026-09-30 boot 60, `c52281d-dirty`, synchronous, textures to 256, 262 reason-6 refusals ([record](../probe/d3d-record-merge-2026-09-30/README.md)) |
| netbook, earlier 640x480 | 717 | - | 2026-09-25 ([record](2026-09-25-netbook-3dmark99-with-mip-trees.md)) |
| Gateway, now | **604** | 7075 | boot 89 |
| Gateway, last 640x480 | 589 | 7065 | 2026-09-30 boot 66, record-merge candidate |
| Gateway, run 8 | 643 | 6572 | 2026-09-29 boot 57 ([record](../probe/ati-rage-mobility-m-3dmark99-2026-09-29/README.md)) |

The netbook's 852 to 1253 spans asynchronous submission, the 1-tick flip,
textures to 1024 and the record-merge work, and is not attributed among
them; the CPU score is unchanged. The Gateway is within the spread of its
earlier runs. Whether the earlier runs used triple buffering was not
recorded; these did.

## Per test, from the Result Browser

Read from the Details tab on each machine
(`netbook-details-{1,2,3}.png`, `gateway-details-{1,2}.png`). The netbook's
earlier column is its 2026-09-25 run (boot 8, 717 3DMarks,
[record](2026-09-25-netbook-3dmark99-with-mip-trees.md)), the last with
per-test figures on file; the 852 run kept only its score, and no Gateway
run kept per-test figures.

| Test | netbook now | netbook 2026-09-25 | Gateway now |
|---|---|---|---|
| 3DMark result | 1,253 | 717 | 604 |
| Synthetic CPU 3D speed | 14,752 | 14,572 | 7,075 |
| Rasterizer score (3DRasterMarks) | 653 | 347 | 302 |
| Game 1 - Race | 25.9 FPS | 12.3 | 8.5 |
| Game 2 - First Person | 8.3 FPS | 5.1 | 4.7 |
| Fill rate | 72.8 MTexels/s | 38.8 | 35.0 |
| Fill rate with multi-texturing | 73.0 MTexels/s | 39.1 | 35.3 |
| 2 MB texture rendering | 74.1 FPS | 22.7 | 18.8 |
| 4 MB texture rendering | 76.6 FPS | 18.8 | 11.7 |
| 8 MB texture rendering | 17.8 FPS | 12.4 | 6.9 |
| 16 MB texture rendering | 9.8 FPS | 7.7 | 3.9 |
| 32 MB texture rendering | 5.1 FPS | 4.4 | 2.1 |
| Bump mapping, emboss 3/2/1-pass | Not Supported | Not Supported | Not Supported |
| Point sample filtering | 105.9 % | 98.7 | 113.5 |
| Bilinear filtering | 100.0 % | 100.0 | 100.0 |
| Trilinear filtering | 100.7 % | 100.8 | 66.7 |
| Anisotropic filtering | Not Supported | Not Supported | Not Supported |
| 6 pixel, individual / strips | 639.9 / 592.5 KPolygons/s | 531.7 / 469.4 | 135.8 / 140.6 |
| 25 pixel, individual / strips | 633.8 / 557.0 | - | 135.4 / 141.4 |
| 50 pixel, individual / strips | 626.0 / 481.0 | - | 123.3 / 140.0 |
| 250 pixel, individual / strips | 92.3 / 70.6 | - | 71.1 / 71.3 |
| 1000 pixel, individual / strips | 15.7 / 14.8 | 7.6 / 7.4 | 22.5 / 22.9 |
| Refresh rate reported | 59 Hz | 28 | VSync Off |
| Missing features | none listed | - | Sub-Pixel Accuracy |

What the columns show, without attributing it: the netbook's gains are
largest where the GPU was waited on - fill rate and the 2 and 4 MB
texture tests roughly double to treble, Game 1 doubles - and smallest in
the 16 and 32 MB texture tests, where texture memory, not submission, is
the limit. Its reported refresh rate, 28 Hz on 2026-09-25, is now 59 Hz,
the flip completing at the first tick. On the Gateway trilinear reads
66.7 % of bilinear (the netbook ~100 %), and 3DMark lists sub-pixel
accuracy as missing; neither has been looked into.

## Not established

- The pictures; nobody watched either run.
