# Faults the release probes did not catch (0.57.0 cycle)

Date: 2026-11-19. Status: open, a record of gaps in the release probes. No
probe or script has been changed.

The 0.57.0 checks, and the 0.56.x checks before them, passed every probe
on the HD 4600. The faults below were found by watching the screen, mostly
in Michael's OBS captures, or from V9XTRACE counters read by hand. Each
entry names the gap that let it through.

## 1. Half-drawn frames with vsync off (HD 4600, fixed in 0.57.0)

With `VSync=2`, 3DMark 2001 SE showed frames cut off with black or
unfinished bands. A flip was reported done before the display had taken
it, so the game drew into the frame on screen
([record](../decisions/2026-11-19-haswell-vsync-off-async-flips.md)).

**Gap:** no release check runs with vsync forced off. The 3DMark step
flags `DrawsIntoPresented`, but only in a run that has the default
vsync.

## 2. Async flips tear with a strip (HD 4600, replaced by copy-present)

Async flips of a linear surface left a band of about 20 capture lines
at every tear. Linux refuses async flips of linear planes before
display 12 for this reason.

**Gap:** a capture shows it, and no counter does. No probe reads back
a frame that was flipped asynchronously.

## 3. Copy-present into an 8 MiB carve ran at 1-2 fps (HD 4600, fixed before commit)

The first copy-present build carved 8 MiB from the texture heap. Car
Chase then thrashed textures, and 3DMark's meter read 1-2 fps. Every
probe still passed.

**Gap:** the probes allocate small textures, so none of them fills
the heap. The thrash showed only in `MipTreeDeclined` and in the OBS
capture.

## 4. Car Chase out of texture memory: 185 mip trees refused, drops to 1 fps (HD 4600, pool in 0.57.0)

This happened before the system-memory pool, with vsync on or off
([record](../decisions/2026-11-19-haswell-texture-pool-phase4.md)).

**Gap:** the same as 3. The 3DMark step does not report
`MipTreeDeclined`, and the release-check list does not run 3DMark on
the HD 4600.

## 5. DirectX 8 mip filter read with DirectX 7 numbering: speckled road and grass (`intel-gma`, `ati`, `nvidia`, fixed in 0.57.0)

Seen in OBS capture `2026-11-19 19-54-02`, and also present in the
16:40 capture taken before the pool
([issue](2026-11-19-ddi8-mip-filter-read-as-dx7.md)).

**Gap:** on a DDI 8 device, V9XD32 only ever sets `D3DTSS_MIPFILTER`
to NONE (`tools/diag/d3d32_probe_win32.c`, T19). T21 tests mipmaps
through DirectX 7 only. A DDI 8 draw with POINT or LINEAR mips, read
back at a distance where a smaller level is due, would have failed.
`FilterMinSeen` without a MIP filter is a second signal the 3DMark step
could flag, as it already flags a missing linear magnification.

## 6. Desktop stretched into the top half with green garbage below (HD 4600, once, unexplained)

This was after a `reset`, before vsync run A (`obs-desk` in
`docs/probe/hd4600-vsync-off-2026-11-19/`). The next boot's registers
read true 800x600 timing and `PipeSource=matches`, and the fault did
not recur.

**Gap:** nothing captures the desktop after the restart, before the
probes run. V9XHSWMM reads registers, which were already correct by
then.

## 7. Two-minute stall loading Car Chase High (HD 4600, unexplained)

This happened in vsync runs A and B. Later runs, including one with
vsync on, loaded at normal speed. The cause was not found.

**Gap:** the 3DMark step stops only after two minutes without a flip,
and a load screen does not flip, so a stall shorter than that passes.

## 8. Trinity checked on a machine whose card had changed (M-D7TFW4)

The owner fitted an HD 7470 the same morning (remote-help chat,
08:07Z). The runner installed the `ati` package and ran every probe.
The record still names the machine "HD 7660D Trinity" and reports its
failures as probe results
([checks](../decisions/2026-11-19-0.57.0-release-checks.md)). The
mismatch came out only by reading the owner's chat. A rerun of the
released 0.56.2 package failed the same way.

**Gap:** `run-release-checks.ps1` does not compare the adapter's PCI
ID with the one the machine list expects. A check before installing
would have skipped the machine with the reason.

**Addressed (uncommitted at writing):** the runner now runs the
package's V9XTRACE before installing, reads the present card from its
`[Card]` section, and refuses the machine if that card is not the
list's `PciId`. V9XTRACE used to cap `[Card]` at six entries in
`Enum\PCI` order, counting present and absent cards together. A
machine with many old cards could therefore list no present card at
all (A8U4I5's 0.28.0 snapshots did). It now always lists every present
card and caps only the absent ones.

## 9. A run that hangs the machine is reported as probe failures

On Trinity, both V9XD32 runs show `NOT-RUN (no report); failing: T13,
T4Flat, T16`. The failing list comes from a partial report left by
the hang. The agent was offline for minutes (`trinity.log.err`: "agent
last checked in 499 s ago").

**Gap:** the record does not separate "the machine stopped answering"
from "the probe drew wrong". Both look like probe failures.

## 10. Probes left on the machine are reused

The Trinity control run sent 1 of 7 probes. The other 6 were the
0.57.0 (`bfaa8d9e`) probes already on the machine, so the 0.56.2
control ran 0.57.0 probes. This makes no difference to the conclusion,
because the same probes passed on Carrizo.

**Gap:** the summary does not state that a run reused probes from an
earlier build.

## 11. R7 250: random slow, pixelated rendering (not yet reported to us)

Michael mentioned this while testing the HD 4600: "this seems random
issue we had on Radeon R7 250 (I haven't reported that to you yet)".
There are no details, machine or capture yet.

**Gap:** unknown. It needs a report first.

## Not checked

- Whether the Oland, Carrizo or GT218 show the speckle of entry 5 in a
  real game. The fix is on those packages, but only the HD 4600 ran it
  in 3DMark.
- Ivy Bridge (HD 4000): it shares the vsync-off code of entries 1-2,
  and has not been run.
