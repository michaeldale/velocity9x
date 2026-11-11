# GeForce 210 tier-0 in the nvidia family: enable-ok on M-BXMFA8

Date: 2026-11-11

Phase 1 of [docs/plans/nvidia-gt218.md](../plans/nvidia-gt218.md).

## What was installed

The `nvidia` package from branch `nvidia-gt218`, build `bb72bc63-dirty`
(the `gt218` chip row, `src/chipsets/nvidia/gt218/gt218_hw16.c`):
V9XDISP.DRV, V9XMINI.VXD, V9XHAL.DLL, V9XSETP.DLL and V9XGL.DLL, staged at
8.3 names in `C:\` and renamed over the `vbe` package's files by
`WININIT.INI` (plain `dest=src` pairs, no `NUL=` lines), then `reset`.
The `vbe` registry and INF binding stay as they were. Boot 82 consumed the
renames (staged files gone) and all five installed files hash-match the
package. The `vbe` files are saved in
`build/driver-results/m-bxmfa8-gt218-boot81/vbe-installed/` for rollback.

Gates before deploy: check-tree, build-host, run-checks, all passed.

## Boot 82

Evidence: `build/driver-results/m-bxmfa8-gt218-boot82/`.

- `Stage=enable-ok`. V9XHW.INI: `Adapter=NVIDIA GeForce 210`,
  `VendorId=10DE`, `DeviceId=0A65`, `Acceleration=none`,
  `EngineCaps=00000000`, `ColourLayout=565-auto`.
- Same as under `vbe` at boot 81: aperture `CF000000`, `DpmiMap=01000000:ok`,
  MTRR 0 write-combining 8 MiB at `CF000000` (`MtrrWrite=ok`), 1024x768x16
  at pitch 2048, VBE 14 MiB, the VBE mode cache (29 listed, 26 cached).
- V9XGDI `/auto`: `Result=PASS` at 1024x768x16.
- V9XMSW `/depth:4` and `/cycle:4`: `Result=PASS`, 4/4 each; the desktop
  came back at 1024x768.
- The agent's screenshot of the desktop is clean. That is a framebuffer
  readback; nobody has looked at the monitor.

## What differs from `vbe`, and is open

- `Direct3DMode=none`, `Direct3DDefault=0`, `D3DSoftSysMem=not-applicable`.
  The `vbe` package on the same card had `Direct3DMode=software`,
  `Direct3DDefault=2`. The row's `Direct3D = 'not-advertised'` takes the
  software Direct3D path away that `vbe` offered. Not decided whether the
  tier-0 row should advertise software Direct3D until the engine exists.
- The family's mode table publishes only its seven VESA rows; the boot-time
  BIOS merge adds the rest, as on the RIVA 128. V9XMODES.INI for boot 82 is
  saved but not compared row by row with boot 81.

## Note on running V9XGDI over the agent

Without `/auto` V9XGDI shows a result message box and waits for OK, and
writes no INI. Agent 0.15.0 cannot read the box's text (`windows -Controls`
needs 0.16.0). Always run it as `V9XGDI.EXE /auto` with `exec -Detach`, then
`wait-file -Match <build id>`.
