# Haswell, 3DMark 2001 SE: flicker on the i5-4570

Date: 2026-10-20. Status: open. Seen, not measured.

## What was seen

Michael ran 3DMark 2001 SE on the i5-4570 (boot 53, 0.21.x dev build)
and saw some flicker. 3DMark 2000 and Quake 2 on the same boot looked
right to him.

Not yet known: which test or scene, the resolution and depth, whether
the flicker is the whole frame or part of it, and whether it is every
frame or occasional.

## Why it is worth a look

- The pipelined submission in 0.21.0 lets a draw run while the next is
  built. It waits only while GDI may own the screen
  ([GDI rule](../decisions/2026-10-20-haswell-pipelined-gdi-rule.md)).
  3DMark 2000 and Half-Life were measured with it, but 3DMark 2001 SE
  was not.
- 3DMark 99's flicker on this chip was every flip being declined, fixed
  by F3 ([issue](2026-10-17-haswell-3dmark99-flicker-and-game2.md)). A
  V9XTRACE snapshot's flip counters would show whether that has come
  back under DirectX 8.
- The heap is full in this benchmark (7,038 mip trees declined for
  room, [issue](2026-10-20-haswell-3dmark2001se-artifacts-field.md)).
  A surface DirectDraw cannot place changes what the runtime does.

## Next

The test name from Michael, then a V9XTRACE snapshot across that test:
flips handled and declined, the GDI-rule waits, and refused batches.
After the heap plan
([plan](../plans/intel-haswell-heap-beyond-16-mib.md)) lands, run it
again, since the heap is a suspect.
