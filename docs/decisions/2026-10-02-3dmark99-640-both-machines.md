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

## Not established

- Per-test figures; the Result Browser was not read.
- The pictures; nobody watched either run.
