# A8U4I5: SiS 6326 Direct3D engine, first V9XDDP run, 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), boot 212, desktop
  800x600x16; V9XDDP switched to exclusive 640x480x16.
- Build: working tree on `1feb92a` (`Build=1feb92a-dirty`), the first
  `d3d_sis6326.c`.
- Run: `V9XDDP.EXE` detached from `C:\V9XDIAG`. The machine hard-locked
  about four minutes in; Michael reset it (boot 213). See
  [the issue](../../issues/2026-10-05-a8u4i5-hard-lock-under-sis-d3d-v9xddp.md).

| File | What it is |
|---|---|
| `B212-V9XHW.INI` | The driver's report before the run: `Direct3D=hardware-sis6326`, `Direct3DMode=hardware` |
| `B212-V9XDD.INI` | V9XDDP's report as the lock left it. It lags the step log and stops at `TexM_128_1555_plain_near`. |
| `B212-V9XDDT.TXT` | V9XDDP's write-through step log. The last record is `ZDepthFillHr`. |
| `ddp_compare.py` | Key-by-key comparison against SiS's HAL run (`../a8u4i5-sis6326-registers-2026-10-04/V9XDD-CARD2-SIS228-B207.INI`) |

    python ddp_compare.py ../a8u4i5-sis6326-registers-2026-10-04/V9XDD-CARD2-SIS228-B207.INI B212-V9XDD.INI
