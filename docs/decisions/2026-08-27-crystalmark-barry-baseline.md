# CrystalMark Retro on BARRY: the driver-0.5 baseline

Date: 2026-08-27
Status: baseline recorded. The accelerated column is now measured -
[2026-08-27-crystalmark-barry-accelerated.md](2026-08-27-crystalmark-barry-accelerated.md).
The native-S3 column is still outstanding.

The first CrystalMark Retro measurement of Velocity9x on physical S3 silicon,
taken so that the GDI acceleration builds have something real to be compared
against. This is the **before** column. The stock S3 driver and a
GDI-accelerated Velocity9x build are the two runs still to take.

## Machine and configuration

| | |
|---|---|
| Host | BARRY, `10.0.1.47:9869`, agent 0.6.0 |
| CPU | Intel Pentium, 1 core / 1 thread |
| Memory | 32 MB |
| OS | Windows 98 Second Edition, 4.10 build 2222 |
| Adapter | S3 Trio32/64 86C764, 2 MB |
| Core / memory clock | 59957 kHz, shared |
| Driver | Velocity9x 0.5.x - `Acceleration=directdraw-fill-blt`, no `GdiAcceleration` key, so pre-`gdi-accel-000` |
| Mode | **800x600x16**, `Stage=enable-ok`, `Surface=pitch=1600 bpp=16 w=800 h=600` |
| Tool | CrystalMark Retro 2.1.0 for 9x (`CrystalMarkRetro9x.exe`), MIT, staged to `C:\CMR` |

CrystalMark Retro reports the adapter as **"Velocity9x S3 Trio32/64 86C764"**,
which is this driver's own device description reaching a third-party tool.

### Why 800x600x16, and why it matters

BARRY was left at 800x600x**32** by an earlier job. The mode was changed
deliberately, and the reason is worth stating because it decides whether the
comparison can show anything at all: **this driver's GDI acceleration declines
every operation above 16 bpp.** A benchmark taken at 32 bpp would show no
difference between 0.5 and a GDI-accelerated build, by design, and would invite
the conclusion that the feature does nothing.

16 bpp is also inside what BARRY's 2 MB can hold at 800x600 (960 KB) with room
to spare, and it changes only the depth from where the machine already sat.

**All three runs must use this mode.** A score compared across depths is not a
comparison.

## Baseline scores, driver 0.5.x

| Group | Test | Score |
|---|---|---|
| **2D (GDI)** | Text | **2** |
| | Square | **253** |
| | Circle | **134** |
| | Image | **91** |
| CPU | Single | 99 |
| | Multi | 99 |
| Disk | Sequential read | 44 |
| | Random read | 2 |
| | Sequential write | 38 |
| | Random write | 2 |

3D was **not run**. The Trio32/64 has no 3D hardware and this driver advertises
Direct3D as not-advertised on that chip, so the test would exercise a software
path at best; on a session that needs a physical machine powered on, that is not
a risk worth taking for a number nothing will be compared against.

Screenshot:
[`docs/images/crystalmark-barry-trio64-driver05-800x600x16.png`](../images/crystalmark-barry-trio64-driver05-800x600x16.png).
The 2D Image sub-test mid-run is
[`docs/images/crystalmark-barry-trio64-2d-image-subtest.png`](../images/crystalmark-barry-trio64-2d-image-subtest.png),
kept because it shows the BitBlt workload the Image score measures.

## Reading these numbers

**CPU and Disk are controls, not results.** Nothing in a display driver should
move them. If they shift in a later run, suspect the measurement environment -
another process on the machine, a different mode, thermal state - before
believing anything about the 2D column.

**Text = 2 is the striking figure**, and it is the one to be careful about.
Against Square 253 and Circle 134 it is two orders of magnitude lower, which
says text is by far the slowest thing this driver does. It is also the one 2D
score that **GDI acceleration builds 001-003 cannot improve**: glyphs arrive
through `ExtTextOut` and `StrBlt`, ordinals this driver still forwards
unconditionally to the DIB Engine, and nothing in the fill, copy or overlap
primitives touches them. So a later run should show Text unchanged, and that is
correct rather than disappointing.

Where the accelerated builds should show up:

- **Square** - solid rectangle fills, which is exactly what build 001 turned
  over to the engine. The most likely place to see a change.
- **Image** - BitBlt, which builds 002 and 003 turned over for the
  screen-to-screen cases. Whether it moves depends on whether the test's blits
  are screen-to-screen or memory-to-screen; a memory source is still declined,
  and build 004's design records why colour upload is not worth accelerating.
- **Circle** - ellipse drawing, which goes through `Output` and is not
  accelerated at all. Expect no change.

That gives the next run a prediction to be judged against rather than a number
to admire: **Square should rise, Image may rise, Circle and Text should not
move, and CPU and Disk must not move.** A result that improves Circle or Text
would mean something other than what this work did is in play.

## Outstanding

1. The stock S3 driver on the same machine, same mode - the "native" column.
2. A Velocity9x build with `gdi-accel-003` on the same machine, same mode.

Both are BARRY sessions, and BARRY is not always powered on.
