# Reading a disabled pipe's line or frame counter hard-locks the 945

Date: 2026-10-15. Status: accepted; fix in `i9xx_scanout.c` and
`d3d_trace_dump_win32.c`. Source issue:
`docs/issues/2026-10-14-github-6-gma950-embedded-hard-lock-on-3d.md`
(GitHub issue 6).

## Finding

On the 945GSE, a CPU read of the display line register (PIPExDSL) or the
frame counter (PIPExFRAMEHIGH) of a pipe whose PIPECONF enable bit is
clear hard-locks the machine: black screen, no network, power cycle
needed. Two places read pipe B's pair unconditionally:

- the HAL's flip-time layout snapshot, `v9x_i9xx_note_layout`
  (`i9xx_scanout.c`), added with intel84 (`2347f592`);
- `V9XTRACE`'s live register dump, `v9x_write_live_display`
  (`d3d_trace_dump_win32.c`), which copied the same list.

On the netbook's panel pipe B is the live pipe, so neither read ever
touched a disabled pipe there. GitHub issue 6's board scans out on pipe A
alone, so both read a disabled pipe B: the first at the first accepted
flip, the second whenever `V9XTRACE` runs while DirectDraw has the engine
window mapped.

Which of the two registers locks, DSL or FRAMEHIGH, was not separated.
Both are now read only on a live pipe.

## How it was measured

MICHAEL-NETBOOK, moved to pipe A with `V9XDDEV SET 0001` from
`AUTOEXEC.BAT` (boot 186 `INTELMM.TXT`: PIPEACONF `80000000`, PIPEBCONF
`0`, DSPACNTR `94000000`, `LivePipe=0`, `LivePlane=0`, the reporter's
registers). Desktop 1024x576x16 on the external monitor, `INTELARM.TXT`
with no `IntelFlip` key (flip on). Each build is `9418ef6d` plus one
change, built in a clean worktree, installed as DRV, VxD, HAL, SETP and
GL by WININIT rename, then Carmageddon II (`CARMA2_HW.EXE`) launched over
the agent and the agent pinged every 10 s. A lock is the agent going
silent and Michael finding the monitor black and the machine frozen.

| Build | Change from default | Boot | Result |
|---|---|---|---|
| default | none | 186 | locked within the exec reply |
| A `issue6-a-nowm` | FW_BLC watermark never written | 188 | locked by 16 s |
| B `issue6-b-noring` | no ring flip; plain DSPAADDR write only | 190 | locked |
| C `issue6-c-decline` | every flip declined at entry to `v9x_i9xx_set_display_start` | 192 | ran 145 s; game played (flicker); Half-Life D3D and GL fine |
| D `issue6-d-nopipestat` | PIPESTAT clear never written | 193-194 | locked |
| E `issue6-e-nocover` | no frame-coverage reads of the presented buffer | 195 | locked by 15 s |
| F `issue6-f-nowrite` | flip accepted and tracked, no base write and no ring flip | 197 | locked by 15 s |
| G `issue6-g-nopipebcount` | layout snapshot reads FBC_CONTROL and FW_BLC_SELF in place of PIPEB_DSL and PIPEB_FRAMEHIGH | 199 | ran past 73 s; locked at about 85 s, when `V9XTRACE` was run mid-game |
| G again, no trace | same | 200 | answered every ping to 249 s; Michael confirmed the game works |

F is the turn: with no display register written at all, the accepted
flip still locked, so the base change itself was never the cause. G's
first run locked only when `V9XTRACE`, which carried the same two reads,
was started during the game; that also confounded the run, and the
clean rerun settled it.

D's boot number is not recorded exactly.

## The fix, verified

Build `issue6-fix` (the two reads chosen by the live pipe, nothing else
changed) on the same netbook setup, boot 201: Carmageddon II launched,
`V9XTRACE` run mid-game at 63 s returned in 2.6 s, and the agent answered
every ping to 159 s. Its snapshot: `FlipHandled=1395`, `FlipDeclined=0`,
`FlipBaseImmediate=1395`, `DrawsToBack=22239`, `LiveCounterPipe=A`,
`LiveReg22Off=0x00070000`, `LiveReg23Off=0x00070040`, and VGACNTRL
`8020008E`, so the VGA plane was disabled throughout. The flips in this
run were all plain base writes; the ring flip on plane A was not
exercised by it.

## Corroboration

- **intel57 and intel58 (2026-09-17, `4f801852`)** hung the netbook to a
  blank screen while a scan watch read the display line and frame
  registers of pipe A, then powered down. The record concluded "the
  powered-down pipe A, as suspected" after the live pipe's reads proved
  harmless, and the watch was rebuilt to read only enabled pipes
  (`d3d_i9xx.c`, "Registers in a powered-down pipe are the untested read
  this watch is suspected of hanging on"). The layout snapshot written a
  week later did not take the same guard.
- **Field report V9X-01D0DQ**: under `V9X3D ON` (flip off) the reporter's
  game ran, and the machine "hard lock first time i hit Send report".
  "Send report" runs `V9XTRACE` immediately after the game, with the
  engine window still mapped, so it made the same read with the flip
  off. Every snapshot that did arrive was dumped on a fresh boot with
  `LiveRegError=no-control-window`, which reads no registers.
- Old i915 returned early from its vblank-counter read for a disabled
  pipe. That fits; it does not say the read hangs.

## Hypotheses this killed

- Two pipes live: `INTELMM.TXT` shows exactly one.
- The FW_BLC watermark write on a live plane A (A locked).
- The ring flip, MI_DISPLAY_FLIP on plane 0 (B locked without it).
- The plain plane-A base write (F locked without any write).
- The PIPESTAT clear on both pipes (D locked without it).
- CPU reads of the presented buffer (E locked without them).
- Asynchronous submission (the reporter's `V9X3D SYNC` locked).
- The 3D engine at 640x480 or 1024x768: Glide drew 1.29 million
  triangles with the flip off, and Half-Life ran in both APIs on build C.

## Not measured

- Frame buffer compression, the VGA plane and memory self-refresh, the
  online and source research's leading candidates for a plane-A-only
  fault, were not needed to explain the lock and were not read. Build G
  sampled FBC_CONTROL (0x3208) and FW_BLC_SELF (0x20E0) into the layout
  slots, but no snapshot of a G session was taken.
- The flicker in Carmageddon II on build C is the cost of declined flips
  and is not studied here.
- PIPESTAT of a disabled pipe is read (and on the netbook's panel, pipe
  A's has been written) at every first flip without harm, so the status
  register is not in the class that hangs. Pipe timing and control
  registers of a disabled pipe are read at every boot by `INTELMM.TXT`
  without harm.
