# A8U4I5: OpenGL on the SiS 6326 engine, 2026-10-05

- Machine: A8U4I5, P3 1 GHz, SiS 6326 card 2 (rev 0Bh), desktop 800x600x16.
- Record: [OpenGL on the SiS engine](../../decisions/2026-10-05-sis6326-opengl.md).
- Plan: [sis-6326-opengl.md](../../plans/sis-6326-opengl.md).

| File | Boot | What it is |
|---|---|---|
| `B294-SIS3D-P8.TXT` | 294 | SIS3D `/phase8` (build `sis3d-20261005-p8a`): an explicit draw with a scissor of x 10-40, y 12-44 through the driver's mapping; ten pixels at the edges |
| `B295-V9XGLP.INI` | 295 | V9XGLP on the driver taking explicit draws: `Result=PASS`, `Renderer=Velocity9x SiS 6326` |
| `B295-V9XSNAP-AFTER-GLP.INI` | 295 | V9XTRACE after V9XGLP |
| `B295-HL-GL-QCONSOLE.LOG`, `B295-HL-GL-RUN2.png`, `B295-V9XSNAP-AFTER-HL-GL.INI` | 295 | Half-Life `-gl`, `timedemo mwd5`, 640x480, three runs |
| `B296-HL-GL-QCONSOLE.LOG`, `B296-V9XSNAP-AFTER-HL-GL.INI` | 296 | The same on the build that counts render-interface refusals: 13, all reason 2 |
| `B296-Q2-QCONSOLE.LOG`, `B296-Q2-TIMEREFRESH.png`, `B296-V9XSNAP-AFTER-Q2.INI` | 296 | Quake 2 demo, `map demo1`, `notarget`, `timerefresh`, 640x480 fullscreen, three runs |

## Results

| Application | Boot | Run 1 | Run 2 | Run 3 |
|---|---|---|---|---|
| Half-Life OpenGL, mwd5 | 295 | 6.008 fps | 6.468 fps | 6.442 fps |
| Half-Life OpenGL, mwd5 | 296 | 5.972 fps | 6.470 fps | 6.440 fps |
| Quake 2 demo1 timerefresh | 296 | 7.775 fps | 7.815 fps | 7.816 fps |

No idle timeout and no declined flip in any run. Scores recorded, not
compared. Half-Life's OpenGL renderer ran at 0.8 fps on the CPU engine at
boot 292 ([evidence](../a8u4i5-sis6326-halflife-2026-10-05/README.md)).
