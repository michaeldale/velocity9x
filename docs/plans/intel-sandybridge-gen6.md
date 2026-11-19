# Sandy Bridge (Gen6, HD 3000): bring-up on M-NZHWBR

Status: **steps 1-6 done 2026-11-19** (2D blits at 8, 16 and 32 bpp and
flips on M-NZHWBR boot 61,
`docs\decisions\2026-11-19-sandybridge-bcs-blits-and-flips.md`; RP0 on
boot 62); Direct3D next. Branch `intel-snb`.

## Machine

M-NZHWBR (W9QI54F0), remote help, owner Daniel: Core i3-2105, HD 3000
`8086:0112` rev 09 SUBSYS `1462:2111`, MSI 7-series board (PCH `1E02`),
2 GiB, Dell on HDMI. Until 2026-11-19 the same board ran an i5-3570K's
HD 4000 (`hd4000` in the release list, now disabled).
`remote-help-no-chat-needed` applies. Restart with `reset`.

## Sources

No Sandy Bridge PRM is on this machine. The references are Linux 6.6 i915
and Mesa 24.0.9 (`genxml\gen6.xml`, crocus), both in
`C:\everything\claude\personal\intel driver research\ivybridge-open-source-references\`,
and the IVB PRM where Gen6 and Gen7 share a block. Intel published the
Sandy Bridge PRM (2011, Vol 1-4); downloading it needs Michael's go-ahead.

## How Gen6 differs from the Haswell path's Gen7

| Area | Gen6 against the `hd4000-ivb` path | Source |
|---|---|---|
| Config: GGC, BDSM, BARs, MSAC | Same encodings, same values on this board | measured, `2026-11-19-sandybridge-read-only-mmio.md` |
| GTT PTE | Same layout as IVB (11:4 = addr 39:32, 2:1 cache); BIOS writes cache `00b` | measured |
| Stolen reserved `1082C0h` | Address 31:20, the mask the path already uses; size bits differ and are unused | i915 `gen6_get_stolen_reserved` |
| Forcewake | `FORCEWAKE` `A18Ch` / `FORCEWAKE_ACK` `130090h`, no MT; same reset and thread wait | i915; measured |
| RC6 | Entered at the desktop (IVB never was) | measured |
| Ring registers, BCS | Same offsets; BCS is Gen6's new ring | i915 |
| Status page | `HWS_PGA` at ring base + `2080h` (`4080h`, `24080h`), not Gen7's `4080h`/`4280h` pair | i915 `set_hwsp`; the path does not program it |
| Plane | `DSPLINOFF`/`DSPTILEOFF` as IVB | i915 `i9xx_plane_update_arm` |
| Pipes | Two CPU pipes (no C); PCH has three transcoders | i915 |
| RPS | `RPNSWREQ` freq at 31:25 as IVB; RP0 from `145998h` | i915; unmeasured |
| 3D pipeline | **Different.** `3DSTATE_WM` carries what Gen7 splits into `3DSTATE_PS`/`WM`; `3DSTATE_SF` carries setup (no `SBE`); one `3DSTATE_URB`; `3DSTATE_CC_STATE_POINTERS` with three pointers; `3DSTATE_VIEWPORT_STATE_POINTERS` and binding-table and sampler pointers each one command for all stages; Gen6 `3DSTATE_DEPTH_BUFFER`; GS and CLIP state differ; PIPE_CONTROL post-sync and the SNB workaround flush rules | Mesa `gen6.xml`, crocus `crocus_state.c` |
| EU ISA | Mostly Gen7's; message descriptors differ (render-target write header, URB write for the VS), no `pln` restrictions to check yet | Mesa `brw_inst.h`, `brw_eu_emit.c` |

## Order

1. **Done.** Tier 0: the released `vbe` driver already runs it (enable-ok,
   18 modes to 1920x1200x32).
2. **Done.** Config survey, `V9XHSW.EXE`: the Haswell decode accepts it.
3. **Done.** Read-only display, GTT and GT capture with `V9XIVMM.EXE`,
   extended for Gen6 ids, pipe count and forcewake
   (`docs\decisions\2026-11-19-sandybridge-read-only-mmio.md`).
4. **Done 2026-11-19.** Chip entry and 2D: `intel-gma` claims `0112` as
   `hd3000-snb` on the Haswell path; `HSW_ENGINE_OPEN` chip 2 takes Gen6
   forcewake; caps fill, copy and flip, with the IVB identity bit for the
   plane; no `CAP_D3D`, no render-clock request. V9XDDF passes at 8, 16
   and 32 bpp, all blits on the engine. *Owed:* the engine close on
   Disable, and a DOS box.
5. **Done 2026-11-19.** Flips: V9XDDP's 640x480x16 chain, 23 of 23 taken.
   800x600x16 (back buffer off a 4 KiB page) also 23 of 23, boot 61.
   V9XDDP has no 8 or 32 bpp chain.
6. **Done 2026-11-19.** RPS: Ivy Bridge's fields; RP0 1100 MHz asked for
   and granted on boot 62
   (`docs\decisions\2026-11-19-sandybridge-render-clock-rp0.md`).
7. **Direct3D on Gen6:** its own state builders beside the Gen7 ones
   (`hsw_3d.c`'s table), a Gen6 VS/PS message layer, and the SNB
   PIPE_CONTROL workarounds. Plan it as its own document before code.
8. The other Sandy Bridge ids (0102, 0106, 010A, 0116, 0122, 0126) as
   aliases once a machine or a survey shows each one's layout.
