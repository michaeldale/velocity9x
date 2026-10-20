# 0.22.0 release build on the i5-4570 (HD 4600)

2026-10-20, boot 59. The `intel-gma` package built for the 0.22.0 release,
from `275369a1` (version 0.22.0, clean tree), was run before publishing. It
was the first build carrying the GMA 3150 device entry to boot on Haswell.

- `V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL`, `V9XGL.DLL` and
  `V9XSETP.DLL` went in by rename-only `WININIT.INI`, then an agent
  `reset`: boot 58 to 59, reconnected, desktop ready. The rename ran, and
  all five installed sizes equal the package's.
- `V9XBOOT.INI`: `Stage=enable-ok`. `V9XHW.INI`: "Intel HD Graphics 4600
  (Haswell)", `HeapLayout=chip`, `HeapEnd=01ED0000`, `Direct3DMode=hardware`.
- V9XDDP (`Build=275369a1`, `COMPLETE`): 211 `Ok=1`, 17 `Ok=0`. The 17
  are exactly boot 55's (`hsw-4570-32bpp-textures-flip-wait-2026-10-20`).
- V9XTRACE (`V9XSNA7.INI`): `DriverBuild`/`HalBuild` 0.22.0 `275369a1`,
  `BuildsAgree=1`. Breadcrumb, ring-space, flip-wait and blit flip-wait
  timeouts all 0; `M64Policy10`-`19` all 0.

No game was run. The machine was left on this build. It had been running
the other session's `0800cb48-dirty` set since boot 56.
