# Intel Phase 3 measured on the netbook: no firmware event moves ownership, and the takeover point is quiet

Date: 2026-09-12
Status: accepted. **Phase 3 of `docs/plans/hardware-d3d-on-intel-gma950.md` is
complete.** Phase 4 (ring sandbox in stolen memory, the first write) is
unblocked, subject to its arm token.
Machine: MICHAEL-NETBOOK, HP Mini 110-1000, 945GSE / GMA 950, `8086:27AE`,
Windows 98 SE direct-booted from the live USB stick.
Package: `build\win98se-intel-gma`, build id `827b2d3-dirty` (mini-VDD API
v5, event journal; the READY mask and disk flush that followed in `5b11f66`
and `497c944` change reporting only, not what is captured).
Sidecar: `2026-09-12-intel-phase3-event-matrix-INTELEVT.txt` (session two).
Raw folders: `build\driver-results\netbook-intel-phase3-20260912-run1\` and
`-run2\`, untracked.

## How it was captured

Two sessions. Each event record is two reads of PGTBL_CTL, the ring TAIL,
HEAD, START and CTL registers, HWS_PGA and fences 0 to 7, plus two complete
FNV-1a hashes of the 256 KiB GTT, taken at ring 0 through the Phase 1 and 2
mappings, with no store to either. The display driver drains the journal to
`C:\V9XDIAG\INTELEVT.TXT` at each display lifecycle event.

Session one: cold boot at 1024x576x16, resolution change to 640x480x16 and
back, then a full-screen DOS box. The DOS box return hard-locked the machine
(`docs\issues\2026-09-12-netbook-dos-box-return-hardlock.md`). Three records
survived on disk.

Session two: cold boot at 1024x576x16, `V9XPWR` at 19:28:08, resolution
change to 640x480x16 and back at 19:28:46, clean shutdown. Four records.

## What the hardware said

Every record in both sessions, seven in all:

| Field | Value |
|---|---|
| Flags | `0000003F`: both reads equal, both GTT hashes equal, ring idle, ring disabled, PGTBL valid |
| PGTBL_CTL | `7FFC0001` |
| RING TAIL / HEAD / START / CTL | `00000000` |
| HWS_PGA | `1FFFF000` |
| Fence 0 to 7 | `00000000` |
| GTT hash | `4D8707C5`, the Phase 2 baseline |

Event kinds observed:

| Kind | Event | Sessions | Context |
|---|---|---|---|
| 1 | boot enable | both | `0161` |
| 4 | ReEnable with mode change | both, twice each | `0111`, `0161` |
| 3 | Disable | session two | `0161`, at shutdown |

The host validator (`scripts\check-intel-event-capture.ps1`) recomputes the
verdict from the records rather than trusting the text `Result`; it accepts
session two with the Phase 2 hash pinned (coverage `0D`: boot, disable,
mode-switch) and reports zero ownership-field changes between adjacent
events. The file itself says `CAPTURED` because the build that wrote it still
required DPMS and mode-restore for `READY`.

## What this confirms

- **The VBIOS leaves the render ring disabled and idle**, at boot, after each
  of its own mode sets, and going into Disable. RING_CTL bit 0 is clear and
  head equals tail in every record. This is the question the plan said the
  whole design rests on.
- **No observed firmware event moves ownership state.** PGTBL, ring, HWS,
  the first eight fences and the entire GTT are identical across boot, two
  VBIOS mode sets and Disable.
- **The takeover point exists and is quiet.** The state after Enable is the
  state at boot. Disable is the matching release point, and the hardware is
  in the same quiet state there too.

## What was learned about the matrix itself

- A Display Properties resolution change is one ReEnable rebuild (kind 4)
  with no Disable. The runbook had assumed Disable then Enable. Nothing in a
  normal session produces a second plain Enable (kind 2). READY was
  re-specified from these measurements.
- **DPMS is unreachable on this package.** `V9XPWR` broadcasts
  `SC_MONITORPOWER` and sleeps; its `PASS` is that broadcast returning. Our
  mini-VDD's `GetMonitorPowerStateCaps` advertises D0 only, so Windows never
  requests a low-power state and `SetMonitorPowerState` is never called. No
  DPMS record can exist until that capability changes, and the August
  `V9XPWR PASS` on this machine meant the same nothing. The instrumentation
  stays in place for when it does.
- **The DOS box row is excluded, not passed.** The return from a full-screen
  DOS box locks the machine on the same-mode ReEnable path, which is shared
  tier-0 display code with prior history on other targets. It is another
  VBIOS mode set with a VGA text-mode excursion, on a path no Direct3D
  application takes and that this plan leaves to the VBIOS and VDD. It is
  filed as an issue with hypotheses; it is not a Phase 3 kill because the
  event it wraps, a VBIOS mode set, was measured twice per session to leave
  ownership untouched.

## Hypotheses this kills or leaves open

- Killed: "the VBIOS may leave the ring enabled or firmware-owned". It is
  disabled and idle everywhere measured.
- Killed: "a VBE mode set may destroy mappings undetectably". Two mode sets
  per session, GTT hash unchanged, all sixty-five thousand PTEs.
- Open: fences 8 to 15 at `3000h` were not captured. All eight captured
  fences are zero, and nothing in the VBIOS's behaviour suggests it programs
  fences, but the second bank is unmeasured.
- Open: what the DOS box return does to the hardware before the lock. The
  flushing build will put the Disable record on disk if anyone repeats it.
- Open: DPMS, until the mini-VDD advertises a low-power capability.

## Rule carried into Phase 4

Enable and Disable are the ownership handoff points, and the hardware is
quiet at both. The ring sandbox must be built after Enable and torn down in
Disable, and never assumed live across either. That rule covers a DOS box,
a mode change and a shutdown alike, whether or not the DOS box return path
is ever fixed.

## Gates

`run-checks.ps1` green on the tree that built the package and on every
follow-up (check-tree, three validator self-tests, host tests, all family
packages). The physical session-two journal passes the event validator.
