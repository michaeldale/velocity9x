# A8U4I5: 3DCIF demo census, 2026-10-14

Record: [2026-10-14-3dcif-demo-census.md](../../decisions/2026-10-14-3dcif-demo-census.md).

- Machine: A8U4I5, Rage IIC AGP on the Velocity9x `ati` package
  (0c26021), 1024x768x16 565, boots 453 (first runs) and 454 (reruns of
  ATi-3D, the Knight demos and Laputa).
- DLL: the census `ATI3DCIF.DLL` from c61e09d8 (`build\cif`, build id
  `cif-census-1`), copied into each demo folder under
  `C:\3DCIF\PACK\ATi 3D C InterFace\`.
- `run-demo.ps1` (session scratchpad, not committed) launched each demo
  detached, took two screenshots, listed windows with `V9XWND`, sent
  Escape, and listed windows again. The census draws nothing, so the
  screenshots show only what the demos drew themselves.

Files:
- `V9XCIF-excerpt.LOG`: per process, the first 300 lines and the final
  summary, cut by `excerpt_cif.py` from the full log. The full log
  (14.5 MB, 242,042 lines, both boots) is not committed.
- `analysis.txt`: `analyze_cif.py` over the full log, one section per
  process (attach).
- `*-running-V9XWND.INI`, `*-after-escape-V9XWND.INI`, `*.png`: per run.
  Names without `-run2` are boot 453; Laputa, ATi-3D and the Knight
  demos from that boot ran behind a hung Laputa.
- `ati-3d-fault-details.png`: the boot-453 ATi-3D fault dialog.
- `laputa-hung-V9XSNA7.INI`, `-b.INI`: `V9XTRACE` snapshots 52 s apart
  with Laputa hung; the flip and draw counters do not move.
