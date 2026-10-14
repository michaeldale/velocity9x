# A8U4I5: the 3DCIF demos on the Rage XL at 5:5:5, 2026-10-14

Plan: [ati-3dcif-wrapper.md](../../plans/ati-3dcif-wrapper.md), Phase 3.

- **Machine:** A8U4I5, ATI 3D Rage XL PCI, boot 469.
- **Driver:** the `ati` package of `a39c768c`, desktop `555-auto`.
- **DLL:** `build\cif\ATI3DCIF.DLL` (build `96a1077b-dirty`), copied into
  each demo folder under `C:\3DCIF\PACK\ATi 3D C InterFace\`.
- **WIN.INI:** `[AtiDemo] TexAddrTiling=FALSE` as before.
- **The runs:** each demo ran about 40 s, then got Escape, and every one
  exited on it. `C:\V9XDIAG\V9XCIF.LOG` was emptied before the first.
- **The file:** `demo-summaries.txt` is the probe's `summarize_cif.py`
  over that log (570 KB, not kept).

This is the first 3DCIF run on the Rage Pro class engine (engine 4). Stock
3DCIF never supported the Rage XL PCI.

| Demo | Frames | Drawn | Refused | Textured draws skipped | On the Rage IIC (boot 456) |
|---|---|---|---|---|---|
| Cubes | 1,319 | 13,036 | 0 | 0 | — |
| ATi-3D | 1,116 | 11,507 | 0 | 0 | the same |
| Pixie | 859 | 453,408 | 1,718 | 0 | 880 refused |
| Fairy | 892 | 473,261 | 1,784 | 0 | 1,172 refused, 4,631 skipped |
| Laputa | 1,372 | 351,213 | 2,740 | 0 | 914 refused, 64,541 skipped |
| Knight DEMO | 433 | 509,561 | 0 | 0 | 30,753 skipped |
| Knight MORPH | 1,000 | 627,546 | 0 | 0 | the same |
| Knight VQ | 609 | 1,284,005 | 0 | 438,437 | VQ skipped |

- **Video-memory skips are gone.** The Rage XL has 8 MiB, against the
  IIC's 4 MiB.
- **Pixie, Fairy and Laputa still refuse two draws a frame.** These are
  the keyed, blended sprites: format 2 here (the target's XRGB1555), MODULATE
  with texel alpha times vertex alpha. Neither engine has that combine
  (`mach64_policy.c`: "No mode yields A=AtAf"), so the refusal is the
  same on both cards.
- **Knight VQ registers its textures as format 20 (VQ)**: one of
  1024x256 and the rest 256x256, with type-4 (VQ) codebooks from
  TexturePaletteCreate. `cif_texfmt.c` has a tested decoder, but
  `cif_dll.c` does not yet call it, so these draw untextured.

None of these runs was watched on the monitor.

## VQ, after the fix

- `vqt_stats.py` and `vqt-codebook-stats.txt` hold the codebook
  measurement.
- `vq-V9XCIF.LOG` is Knight VQ's run with VQ decoding: no texture skipped,
  and Michael watched it on the monitor.
- [2026-10-14-3dcif-vq-textures.md](../../decisions/2026-10-14-3dcif-vq-textures.md).
