# 3dfx Voodoo3: what the retro-agent work supplies to the 2D and D3D engines

Date: 2026-09-23

Status: proposed — desk work only, nothing coded. Follows
[3dfx-voodoo3-family.md](3dfx-voodoo3-family.md), whose tier-0 phases remain
the gate for everything below.

## Why this plan

The retro-agent repository (`C:\everything\voidsstr-retro-agent`) spent
2026-07 to 2026-09 on 3dfx under Windows XP: a MesaFX OpenGL ICD, rebuilt
Glide 2/3 for Voodoo3 (h3), Voodoo2 (cvg) and Voodoo4/5 (h5), a clean-room
display-driver attempt (`voodoo-cleanroom/vcr-disp/`), and a Direct3D HAL
design (`scripts/3dfx/`). None of it is a Win9x display driver, but it
contains register facts, an ordered FIFO bring-up and a list of wedge
conditions. This plan records what transfers, what does not, and the licence
of each source, so the Voodoo3 engine phases start from that instead of
rediscovering it.

Nothing here is a velocity9x measurement. Every fact below is a hypothesis
until a `docs/decisions/` record on the 86Box guest or the physical card
confirms it.

## Sources and their licences

This decides what may be lifted as code and what may only be cited as fact.
velocity9x is GPLv3.

| Source | Licence | Use |
|---|---|---|
| `C:\everything\smoltdfx` (Daniel Palmer) — `smoltdfx.h`, register-level Voodoo3 2D/3D, command FIFO, triangle setup; frames checksummed against QEMU's Voodoo3 model and a real Voodoo3 3000 | GPLv3 | Compatible. Code may be adapted with attribution. |
| retro-agent `scripts/3dfx/driver/nt/hw/h3hw.h`, `gbk/gbk_fifo.c` — register offsets and FIFO state, each line cited to the Glide h3 source | 3DFX GLIDE Source Code General Public License (not the GNU GPL) | Not assumed GPLv3-compatible. Cite as a pointer to facts; re-derive each value from the published register spec and write fresh code. |
| Open Glide (`sezero/glide`, fork `voidsstr/retro3dfx-glide`) | 3DFX GLIDE Source Code General Public License | Same as above. Already accepted as reference by the mode-4 plan. |
| `C:\everything\3dfx-driver` — the leaked 3dfx tree, including the Win9x display driver in `H5/Win9x/DX/` | None granted | Reading to understand hardware behaviour only. Never copied, paraphrased into code, or cited as licensing a register write. |
| Published Banshee/Voodoo3 register specification | 3dfx public release | The authority every register write must cite (working agreement, "C"). |

Open item: locate and archive a copy of the published Banshee/Voodoo3 spec
in the tree, the way the mode-4 plan cites the Voodoo2 spec. The roadmap
assumes it exists; no path is recorded yet.

## What transfers

### Register facts (unmeasured here; source is Glide h3 via `h3hw.h`)

- BAR0 is 32 MiB of registers; BAR1 a 32 MiB linear framebuffer; BAR2 I/O.
- BAR0 sub-spaces: I/O `0x000000`, command FIFO/AGP `0x080000`, 2D
  `0x100000`, 3D `0x200000`, 3D LFB window `0x1000000`.
- 2D register offsets within `0x100000`: `clip0min` `0x008`, `clip0max`
  `0x00C`, `dstBaseAddr` `0x010`, `dstFormat` `0x014`, `rop` `0x030`,
  `srcBaseAddr` `0x034`, `commandEx` `0x038`, `srcFormat` `0x054`
  (remainder in `h3hw.h`).
- `miscInit0` bits 18-29 hold the Y-origin; `dramInit1` bit 30 marks SDRAM.
- Memory layout rules Glide applies: colour buffers on even 4 KiB pages,
  depth on odd; FIFO at most `0x40000`, or `0xFF000` on boards over 8 MiB;
  `0x2000` reserved after the FIFO; at least 2 MiB kept for textures.

### Command FIFO bring-up

Glide arms CMDFIFO 0 with ten stores in a fixed order: disable
(`baseSize = 0`), base page, read pointer low/high, `aMin`/`aMax` just
below the ring, depth, hole count, threshold `(0xF << 5) | 8`, then arm
(`size-in-pages - 1 | enable`). The FIFO must stay disabled while the
pointers move. Ring invariants: the hardware never wraps by itself, so
software writes the jump packet; the writer keeps a 4-byte margin behind the
read pointer; the last 32 bytes are never used for data. retro-agent maps
the FIFO uncached because it has no store fence.

Evidence status: the sequence ran on real hardware only as part of retail
and rebuilt Glide on the XP Voodoo3 (2026-07). retro-agent's own `gbk_*`
kernel backend never ran on hardware (its milestone M4d is open). smoltdfx
drives the same FIFO on real hardware and is the second, compatible
reference.

### Operational findings from the XP Voodoo3 (retro-agent `voodoo-cleanroom/DEBUGGING-NOTES.md`)

- A fullscreen Glide application programs the video timing itself; game and
  Windows refresh settings are bypassed. The display driver's refresh choice
  reaches Glide titles only if Glide is told it.
- Fullscreen Glide never shows the Windows cursor.
- A Glide shutdown and re-init with a mode change inside one process wedges
  the Voodoo3. Force-killing a fullscreen Glide 2 game mid-command wedges the
  chip until power-off.
- GDI screen capture cannot read fullscreen Glide output.

## What does not transfer

- `voodoo-cleanroom/vcr-disp/` — an NT GDI skeleton that does not build and
  has no mode set, 2D, DirectDraw or D3D.
- `scripts/3dfx/driver/win9x/d3dcb.c` — never registers
  `ContextCreate`/`ContextDestroy`, ignores the DrawPrimitives2 vertex
  buffer, hard-codes 640x480x16. A design sketch, not a base.
- The NT `ExtEscape` memory-mapping design; Win9x maps through the
  mini-VDD.
- The MesaFX ICD. OpenGL is deferred in `PLAN.md`.

## The open question that decides compatibility: Glide coexistence

Most Voodoo3-era games use Glide, not D3D. On Windows 9x the Glide h3
hardware layer (`minihwc`) finds and maps the board through `\\.\CONFIGMG`,
not through the display driver's escapes as it does on NT. Whether retail or
open `glide2x.dll`/`glide3x.dll` still runs when velocity9x, not the 3dfx
driver, owns the card is unknown. So is whether Glide expects registry keys,
escapes or exclusive-mode coordination from the display driver, and whether
a velocity9x desktop survives a Glide application taking and returning the
card.

A driver that runs D3D and breaks every Glide title is a regression for this
card's users, so this is answered before the 2D engine ships.

## Phases

### A — desk (no hardware)

1. Licence decision record: what may be adapted from smoltdfx, and the
   confirmation that Glide-licensed and leaked material is fact-only.
2. Register fact sheet: every value above cross-checked across the published
   spec, smoltdfx and `h3hw.h`. Disagreements are listed, not resolved by
   majority.
3. Glide coexistence: read `minihwc`'s Win9x branch to list what it
   requires of the system. The rebuilt Glide fork holds it; the leaked
   `H5/MINIHWC/MINIHWC.C` may be read for behaviour. Output is a list of
   testable predictions, not a conclusion.

Exit gate: the three documents, no code.

### B — Glide on the tier-0 driver

After tier-0 Phase 2 (`Stage=enable-ok`), run one Glide 3 and one Glide 2
title on the `Win98SE-BX-Voodoo3` 86Box guest (host port 9874) with
velocity9x installed, then on the physical card. Record whether each
starts, renders, and returns a usable desktop. Record separately what
86Box's Voodoo3 model proves nothing about, as the family plan does for the
VBE survey.

Exit gate: a decision record with screenshots or captures from both targets.

### C — 2D engine (`eng_tdfx.c`)

Start with direct 2D register writes (solid fill, then screen-to-screen
blt), which need no ring. Move to the command FIFO once the direct path is
proven, using the Phase A sequence. Run through the existing `gdi-accel`
harness, 86Box first. The FIFO mapping type (uncached vs write-combining
with a fence) is a measured decision, tied to the MTRR work in
[tier0-quality.md](tier0-quality.md).

Exit gate: as for the other `gdi-accel` arms, plus Phase B's Glide titles
still passing.

### D — Direct3D engine (sketch)

Plugs into `V9X_D3D_ENGINE_OPS`. Triangle setup and texture formats come
from the spec, with smoltdfx as a compatible worked example. The milestone
game is chosen then, confirmed to use hardware D3D before it becomes the
gate (the Hellbender lesson).

## Hardware

- 86Box: `Win98SE-BX-Voodoo3` (`voodoo3_3k_agp`), see `docs/vm-environment.md`.
- Physical: the Voodoo3 on hand named in the roadmap. The retro fleet has no
  Voodoo3 in service since 2026-08-11, so retro-agent cannot supply a second
  physical target.
