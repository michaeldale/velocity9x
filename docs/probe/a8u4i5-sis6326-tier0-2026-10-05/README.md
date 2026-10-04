# A8U4I5: first Velocity9x boot on the SiS 6326, 2026-10-05

- Machine: A8U4I5 (10.0.1.172), boot 208, agent 0.8.0.
- Card: SiS 6326 card 2: rev 0Bh, subsystem `63261569`, BIOS 1.28q.
- Driver: the `sis` family package, content of `c58dfe5` (the binaries carry
  build id `7202853-dirty`: built from the working tree before that commit).
  Installed through Device Manager, Update Driver, Have Disk `C:\V9XSIS`,
  replacing SiS/AOpen 2.28. Preflight `V9XSTAGE.EXE`: PASS.
- Rollback: `../a8u4i5-sis6326-registers-2026-10-04/display-class-before-velocity9x.reg`
  is the Display class under SiS 2.28.

| File | What |
|---|---|
| `B208-V9XBOOT.INI` | Driver boot record: `Stage=enable-ok`, VBE 2.0, 44 modes listed, 29 cached |
| `B208-V9XHW.INI` | Published hardware identity |
| `B208-V9XMODES.INI` | 22 published modes, 7 static and 15 from the BIOS |
| `B208-V9XGDI.INI` | `V9XGDI /auto` at 640x480x8 |
| `B208-V9XMSW-cycle-10.INI`, `-depth-10.INI` | `V9XMSW /cycle:10`, `/depth:10` |
| `B208-V9XMSW-SETS.TXT` | `V9XMSW /set:` to every published mode and back, plus the repeat of the INCOMPLETE cases |
| `B208-V9XDD.INI`, `B208-V9XDDH.INI` | `V9XDDP` default run and the HAL hook record |
| `B208-V9XDD-pal8.INI`, `-modestress.INI` | `V9XDDP /pal8`, `/modestress` |
| `b208-desktop-after-tests.png` | The desktop after the runs, 640x480x8 |
