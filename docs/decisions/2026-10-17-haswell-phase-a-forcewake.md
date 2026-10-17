# Haswell Phase A: forcewake is acknowledged, and the GT's zeros were RC6

2026-10-17, i5-4570 (`8086:0412` rev 06), Windows 98 SE, agent uptime about
an hour into the boot. `V9XHSWMM.EXE /forcewake` + `HSWMM.VXD`, build
`65a07e03-dirty`. The first write to this GPU, approved the same day as
designed in `docs\plans\intel-haswell-direct3d.md` Phase A. Evidence:
`docs\probe\hsw-4570-forcewake-2026-10-17\` (a read-only pass immediately
before, and the forcewake pass).

## The handshake

Interrupts off throughout, two writes to `A188h`:

| Step | Value |
|---|---|
| acknowledge `130044h` before | `00000000` (nobody held it) |
| write `A188h` | `00010001h` |
| polls until acknowledge bit 0 | 493 (`1EDh`) |
| acknowledge while held | `00000001` |
| `A188h` read while held | `00000001` |
| write `A188h` | `00010000h` |
| polls until bit 0 clear | 3 |
| acknowledge after | `00000000` |

Status `0000166D`: requested, acknowledged, released. The agent answered
normally afterwards and every display register read the same as the
read-only pass, so the write disturbed nothing visible.

The masked-write request format (bit 16 mask, bit 0 value) and the
acknowledge at `130044h` bit 0 were i915's, with the PRM only naming the
registers; the measurement now stands for them on this part.

## What the GT reads while awake

Asleep, every GT offset read zero (the read-only pass and
`2026-10-17-haswell-phase2-read-only-mmio.md`). Held awake:

| Offset | Asleep | Awake | Name |
|---|---|---|---|
| `A090h` | 0 | `88040000` | RC_CONTROL (i915) |
| `A094h` | 0 | `00040000` | RC_STATE (i915) |
| `A188h` | 0 | `00000001` | MULTIFORCEWAKE |
| `130044h` | 0 | `00000001` | forcewake acknowledge |
| `2050h` | 0 | `00000010` | unnamed here |
| `2054h` | 0 | `0000000A` | unnamed here |
| `2068h` | 0 | `11001059` | unnamed here |
| `206Ch` | 0 | `FFFFFFFE` | unnamed here |
| `2098h` | 0 | `FFFFFFFF` | unnamed here |
| `209Ch` | 0 | `00000200` | unnamed here |
| `20A0h` | 0 | `28A01010` | unnamed here |
| `20A8h` | 0 | `FFFFFFFF` | unnamed here |
| `20B4h` | 0 | `FFFFFFFF` | EMR (i915) |
| `20C0h` | 0 | `00006080` | unnamed here |
| `20E8h` | 0 | `00000001` | unnamed here |

Still zero while awake: the render ring's TAIL, HEAD, START and CTL
(`2030h`-`203Ch`), ACTHD (`2074h`), EIR (`20B0h`), and the blitter ring
registers. The render-streamer offsets this table leaves unnamed are
published raw; naming them waits for a PRM address, not a guess.

## What it means

Both readings Phase 2 could not separate are true together: the GT was in
RC6, which is why it read zero, and it is unprogrammed - no ring has been
set up, as a VBE-only boot would leave it. `RC_CONTROL` reading
`88040000` says the BIOS enabled RC6 (decoding its bits is i915's, so it is
stated as a reading, not a decode).

So every later phase must hold forcewake for as long as it touches GT
registers, and release it after. Phase B can proceed: set up the blitter
ring under forcewake.

## Not checked

- Whether the GT drops back into RC6 between two forcewake windows fast
  enough to matter for a driver that holds it only around register access.
- The blitter's own power domain: BCS ring registers read zero while awake
  too, which is consistent with "unprogrammed" but was not separately
  probed.
