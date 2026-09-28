# ATI Rage Mobility-M Phase 4 ordered Z16 clear

Target: Gateway Solo 2150, ATI Mobility-M `1002:4C4D` revision `64`, subsystem
`107B:2150`, 4 MiB SGRAM, 1024x768x16 desktop, agent boot counter 9.

This gate draws the passing 256-pixel Z16 triangle, verifies its `0x4000`
depth image against the color coverage, and then clears the same 64x28 surface
to `0xFFFF` with the proven 2D fill path. It verifies all 1,792 depth words and
both physical guards before restoring both 4 KiB pages and all engine state.

Build `ati-phase4-zclear-20260928-a` is retained as REVIEW evidence. The 3D
write passed, but a direct 2D clear left 257 mismatches. Build `b` returned a
literal post-clear Z16 BMP and captured first actual `0x4000`: the exact
256-pixel dirty 3D triangle remained, plus the known physical origin word.
This localized the failure to the 3D-to-2D ordering boundary.

Build `c` disables `Z_CNTL` and waits idle before submitting the 2D clear. It
then applies the established aligned-base origin repair. Two same-boot runs
passed with byte-identical accepted artifacts:

- `ATI4ZC.TXT` and `ATI4ZC-PASS2.TXT`: CRC32 `DAF3CFB8`, SHA-256
  `4E785878493C8357F293853347879AECE03B726B5FF03ADC53231A5F0A72AFE7`;
- `ATI4ZC.BMP` and `ATI4ZC-PASS2.BMP`: CRC32 `D1E15135`, SHA-256
  `86FC485B93CF03D3FBD411FD5E5E03F5D1C9D187A0DEBA28E1552D63F6A00519`;
- every post-clear Z16 word is `0xFFFF` and the returned depth BMP is uniform;
- zero depth, clear, guard, persistent-state, and VRAM-restore mismatches;
- zero timeouts and zero recovery resets;
- the desktop and remote agent remained responsive after every run.

The result proves a required ordering rule: after 3D Z writes, disable Z and
drain the engine before a 2D operation overwrites that allocation. Otherwise
dirty Z data can survive or overwrite the later clear.

Phase 4 item 3, Z16 writes and clear, is complete. Public ATI acceleration
remains disabled while later Phase 4 features are open.
