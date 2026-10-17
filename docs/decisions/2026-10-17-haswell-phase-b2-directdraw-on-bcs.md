# Haswell Phase B2: DirectDraw fills and copies run on BCS in the driver, at 8, 16 and 32 bpp

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, the `intel-gma`
family's new `hd4600-hsw` chip, development build `2576615b-dirty` (the
B2 code as committed with this record). Designed and approved in
`docs\plans\intel-haswell-direct3d.md` Phase B2. Evidence:
`docs\probe\hsw-4570-b2-2026-10-17\`.

## Install and first boot (boot 7)

Have Disk from `C:\V9XGMA` over the `vbe` driver: the exact-ID model
"Velocity9x Intel HD Graphics 4600 (Haswell)" was the one compatible
entry, without "Show all". SetupX left three in-use renames in
WININIT.INI and no `NUL=` line. Restarted through `reset`.

`V9XHW.INI`: `EngineStatusEnable=open-reused`, `EngineCaps=00000003`,
`Acceleration=directdraw-fill-blt`. "Reused" means the ring was opened by
an earlier Enable call in the same boot (`EnableCount=3` in the trace) and
handed back unchanged. `V9XBOOT.INI` reaches `Stage=enable-ok` at
640x480x8 on the VBE mode, as tier-0 did.

## DirectDraw, measured

**V9XDDP** (`GblHalCaps=0x04000440`: BLT and BLTCOLORFILL from the HAL,
SRCCOPY in the ROPs). All fill and overlap checks `PixelOk=1`, `Result=
COMPLETE`, with the desktop at 8, 16 and 32 bpp. V9XDDP sets its own
640x480x16 exclusive mode for these checks (`PrimaryBpp=16` in all three
runs), so these three runs measured 16 bpp only. The trace counts 6 Blt calls, 2 on the
engine (`CountBltEngine=2`): the fill and one copy. The four overlapping
same-surface copies took the CPU path, as designed.

**V9XDDF**, extended for this phase (step 7, and colours per depth), runs at
the desktop's own depth with no mode change: a whole-surface fill, a
rectangle fill, and a non-overlapping copy on one 64x16 off-screen
surface, every pixel compared against a CPU reference. Desktop depth set
with `V9XMSW /set:640x480xN`.

| Desktop | Pitch | Fill | Rect fill + copy | BCS TAIL (= HEAD) |
|---|---|---|---|---|
| 32 bpp | 100h | `0012A5C3`, 0 mismatches | `00ED5A3C` at column 32, 0 mismatches | 108h → 188h |
| 8 bpp | 40h | `5A`, 0 | `A5`, 0 | 188h → 208h |
| 16 bpp | 80h | `07E0`, 0 | `F81F`, 0 | 208h → 288h |
| 8 bpp, boot 8 | 40h | 0 | 0 | 0 → 80h |

Each run advanced the ring by 80h bytes. That is exactly three
submissions, each followed by its status store: 40 + 40 bytes for the two
fills and 48 for the copy. The two V9XDDP sessions advanced it by 58h
each, which is one fill (40) and one copy (48). The ring was idle
(HEAD = TAIL) at every read. `EngineIdleTimeouts=0`, `EngineFlags=1`.

## Findings

- **The driver runs DirectDraw fills and copies on BCS at 8, 16 and 32
  bpp**, pixel-exact against a CPU reference, with the ring and status
  page B1 measured. The BR13 colour depth field (0, 1, 3) and the 32-bpp
  byte mask are right: every byte of the 32-bpp colours differs, and none
  was lost.
- **The `XY_SRC_COPY_BLT` pitches are in bytes**, as B1 found for the
  fill. Both pitch fields carry Vol 2b's "in DWords" text; byte pitches
  of 40h, 80h and 100h copied exactly.
- **A VBE mode set does not disturb a live BCS ring.** After 8 → 16 →
  32 → 8 → 16 switches the ring kept START `01800000`, CTL `00000001`
  and HEAD = TAIL, forcewake stayed held (`A188` = 1, `130044` = 1), and
  the next submission ran. The 945 behaved the same way
  (`2026-09-12-intel-phase3-event-matrix.md`).
- **The engine reopens after `reset`** (boot 8): `open-reused`, ring at 0
  and enabled, and the first fill and copy pass.

## Not measured

- **Close.** `reset` bypasses Windows' shutdown, so the Disable event,
  `HSW_ENGINE_CLOSE` and the forcewake release have not run on hardware.
  An ordinary restart or a DOS-box switch would exercise them. The
  full-screen DOS box stays excluded, as on the netbook.
- The V9XTRACE snapshot's `ModeBpp` read 8 after the desktop had moved to
  16 and 32 bpp. Its `CountBlt` figures match the last V9XDDP session, not
  the V9XDDF runs. Neither was investigated. The ring registers are the
  evidence of engine work here, not those counters.
- GDI stays on the CPU. TAIL read 0 at the boot 8 desktop: nothing
  outside DirectDraw submits.
- `V9XHSWMM.INI` keeps sections from earlier runs, because
  `WritePrivateProfileString` does not clear them. The `[Bcs]` values
  `C0DE0001`/`C0DE0002` in these captures are from B1. The live reads are
  the `R0xxxxx=` keys.

## Next

Phase C, the render context (RCS ring, the dummy context, a
`PIPE_CONTROL` breadcrumb). It needs its own design for approval.
