# A8U4I5: ATI3DCIF.DLL draws Cubes on the Rage IIC, 2026-10-14

Plan: [ati-3dcif-wrapper.md](../../plans/ati-3dcif-wrapper.md), Phase 3.

- Machine: A8U4I5, Rage IIC AGP. Boot 456 runs the `ati` package built
  with render interface ABI 5 (build `a8-cif-abi5`). All six driver files
  were swapped in by WININIT rename, and the driver came up `enable-ok`.
- DLL: `build\cif\ATI3DCIF.DLL`, copied into
  `C:\3DCIF\PACK\ATi 3D C InterFace\CUBES\`. Three builds ran in turn:
  - `cif-p3-1`: every textured draw was refused UNSUPPORTED.
  - `cif-p3-2`: textures follow the D3D-op rules (`cif_state.c`'s
    `texel_alpha` and `white`).
  - `cif-p3-3`: the same, plus the centre-pixel readback.

Files:
- `V9XCIF-cubes-boot456.LOG`: the three builds' runs, from the first
  attach.
  - `cif-p3-1`: `draw refused result=4` on every textured draw.
  - `cif-p3-2`: `drawn=9206 refused=0`.
  - `cif-p3-3`: `pixel before draw` / `pixel after draw` lines. The
    target's centre changes from the clear colour `4210` to the cube's
    `A800` and `AD75` in both flip buffers (`D4455280`, `D43BF280`).
- `cubes-monitor-obs-boot456.png`: Michael's OBS capture of the monitor.
  Both cubes are textured and lit as under ATI's driver. The background is
  purple-blue rather than ATI's grey: Cubes clears with the 555 value
  `4210`, which a 565 screen shows as about RGB(66, 65, 132).

## The other demos (boot 456)

`demo-summaries.txt` is `summarize_cif.py` over each demo's latest run
(the full logs, about 15 MB, are not kept). Triangles drawn, with what
stayed undrawn:

| Demo | Build | Frames | Drawn | Refused | Textured draws skipped |
|---|---|---|---|---|---|
| ATi-3D | run-checks `1019a826` | 478 | 5,061 | 0 | 0 |
| Pixie | `cif-p3-6` | 440 | 238,333 | 880 | 0 |
| Fairy | `1019a826` | 586 | 316,324 | 1,172 | 4,631 (512x512, fixed in `cif-p3-4`) |
| Laputa | `cif-p3-6` | 1,233 | 325,517 | 914 | 64,541 (video memory) |
| Knight DEMO | `cif-p3-5` | 128 | 124,235 | 0 | 30,753 (video memory) |
| Knight MORPH | `cif-p3-5` | 331 | 208,164 | 0 | 0 |
| Knight VQ | `1019a826` | 342 | 720,974 | 0 | VQ, and tiled before the WIN.INI change |

What the runs showed, in the order it was fixed:
- **Textures past 256.** The engine describes 256 as its largest, and
  Pixie and Fairy register 512x512 (`DDERR_OUTOFVIDEOMEMORY`). Fix
  (`cif-p3-4`): a mipmapped texture keeps the first level that fits; one
  without mipmaps is decimated.
- **Tiled textures.** The Knight demos tile because `WIN.INI [AtiDemo]
  TexAddrTiling` defaults to TRUE. It was set to FALSE on A8U4I5 for these
  runs; the original WIN.INI is kept on the host. Knight then registers
  textures this DLL converts.
- **Video memory.** The application's own textures stay in video memory
  beside the DLL's copies, and 4 MiB does not hold both for the Knight
  demos or Laputa. Since `cif-p3-5` the copy drawn with longest ago (never
  one from the current frame) is evicted and re-uploaded when next used.
  MORPH then draws with no skips; DEMO and Laputa still skip some.
- **Keyed, blended sprites (Pixie, Fairy, Laputa: two draws a frame).**
  These are refused UNSUPPORTED. The texture is keyed 1555, MODULATE on
  colour and alpha, blended SRCALPHA/INVSRCALPHA, with no depth buffer.
  Neither of these changed the refusal:
  - dropping the redundant alpha test (`cif-p3-5`);
  - using the texel's alpha alone where every vertex alpha is 255
    (`cif-p3-6`).
  So the sprites' vertex alpha is below 255, and the draw needs texel
  alpha times vertex alpha, which the Rage IIC cannot do: the open
  [issue](../../issues/2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md).
- **Laputa no longer stops after one frame.** It drew 1,233 frames and
  exited on Escape. Under the census DLL, which drew nothing, it stopped
  after one frame twice; whatever it waited for, a drawing DLL provides
  it.

None of these was seen on the monitor except Cubes.

The agent's own screenshots of the same runs show only the flat clear
colour. They read the GDI primary, which does not follow Cubes' flips on
this driver; under ATI's driver they did.
