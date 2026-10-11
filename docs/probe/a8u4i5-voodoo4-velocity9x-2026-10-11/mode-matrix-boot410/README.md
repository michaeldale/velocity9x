# Mode matrix, Voodoo4 4500 on Velocity9x, A8U4I5 boot 410

Package `3dfx` at `ee59550` (V9XDISP.DRV 47,474 bytes). Each of the 18
published modes in `boot410-V9XMODES.INI`, in table order, through
`V9XMSW.EXE /set:WxHxB`; result files `V9XMSW-<mode>.INI`, one-line summary
in `SUMMARY.TXT`.

Result: 18 of 18 switch.

- 16 files read `Result=PASS ChangeResult=0`.
- `320x240x16` and `400x300x16` read `Result=INCOMPLETE`, with exit code 0.
  `V9XMSW` returns 0 only when the switch and its pixel check passed, and the
  following switch records each as its start mode, so both were entered.
  The missing tail is the tool's: its `/set` path calls `ExitProcess`
  without the profile-cache flush its own comment says a mode change needs
  (`tools\diag\mode_switch_win32.c`). `320x240x16` re-run from 640x480x16
  wrote PASS (`-rerun.INI`); `400x300x16` from 320x240x16 lost its tail again.
- The desktop was returned to 640x480x16.

Not checked: the picture on the monitor in each mode (it shows vertical
lines in every mode under both drivers, a card fault set aside), refresh
rates, 24/32 bpp (not published).
