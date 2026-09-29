# Intel GMA 950 asynchronous-submission A/B evidence

Machine: MICHAEL-NETBOOK, 945GSE / GMA 950 `8086:27AE` revision 03,
Windows 98 SE, remote agent `10.0.1.248:9869`. Candidate build
`8b58304-dirty`.

- `sync-probe-V9XSNA7.INI`: boot 50, `IntelAsyncSubmit=0`, after the broad
  DirectDraw/Direct3D probe.
- `sync-3dmark-score.bmp`: boot 50, 1024x576x16, 759 3DMarks and 16490 CPU
  3DMarks, visually correct.
- `async-probe-V9XSNA7.INI`: boot 51, `IntelAsyncSubmit=1`, after the same
  probe. The probe completed and the counters report no timeout, reset or
  abandonment.
- `async-corruption-{1,2,3}.jpg`: operator photographs during the following
  3DMark 99 run. They show stale horizontal bands, displaced geometry and
  large stale or blank rectangles. The run was aborted and has no accepted
  score.
- `revert-probe-V9XSNA7.INI`: boot 52 after writing
  `IntelAsyncSubmit=0` and rebooting. The same broad probe completed with the
  synchronous capability and no failure counters.
- `halflife-missing-hldemo1.bmp`: the installed Half-Life GOTY developer
  console. The proposed `hldemo1` A/B cannot be run because this installation
  has no `valve\hldemo1.dem`.

The photographs are the correctness result. The clean async probe counters do
not override visible corruption under sustained load.
