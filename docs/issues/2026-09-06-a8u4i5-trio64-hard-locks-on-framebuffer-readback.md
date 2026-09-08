# A8U4I5 with the PCI Trio64 hard-locks on framebuffer read-after-write, under any driver

Date: 2026-09-06
Status: **open, not a Velocity9x defect.** Reproduced eight times on the
machine; reproduced once under Microsoft's own S3 driver. The remaining
variable is the board's PCI configuration, untested.

**Update, 2026-09-08:** the card itself is now exonerated. The same Trio64 was
moved to a Linux host on an AMD RS780/SB710 chipset and driven through the same
workload class under instrumentation: interleaved CPU write/read-back to the
linear framebuffer, 12,012,288 accesses, no lock, no corruption, no PCI master
abort — and posting measured at 285 ns for a posted write against 1622 ns for a
non-posted read, interleaving merely additive. See
[the cross-platform evidence](../handoffs/2026-09-08-a8u4i5-trio64-cross-platform-evidence.md).
That leaves the platform as the surviving candidate, and raises one experiment
ahead of the BIOS toggles below: establish whether A8U4I5's framebuffer aperture
is mapped **write-combining**. The Linux runs used uncached individually posted
writes, which is the conservative case and not the one most likely to wedge a
bridge.
Severity: for the project, a lost test target. A8U4I5 cannot host anything
that reads the framebuffer back at speed until this is cured.

## The machine

A8U4I5, `10.0.1.172:9869`, Windows 98 SE, Pentium III, real MTRRs. Until
2026-09-06 it carried an S3 Trio3D/2X; that day the S3 Trio64 (`5333:8811`,
2 MB, a PCI card of 1994) was moved into it from BARRY after BARRY hung
([issue](2026-09-06-text-acceleration-hangs-physical-trio64.md)). The Trio64
had run the full `V9XGDI /accel` harness clean in BARRY's Pentium on
2026-08-27 ([record](../decisions/2026-08-27-crystalmark-barry-accelerated.md)).
The Trio3D had run 3DMark 99 and Final Reality in this same slot.

## What happens

`V9XGDI /accel` - five hundred GDI fills and copies into a 320x240 window,
with a screen-to-memory copy and GetDIBits of that window every twenty-five
operations - stops the machine dead within about two seconds. No ICMP reply,
no agent, no cursor. Only a physical reset recovers it. The disk cache never
flushes the harness's per-operation progress key, so the last operation is
unknown; the desktop shows the harness's first fills drawn and nothing after.

## What was ruled in and out, on the machine

Every row is one boot of A8U4I5, one variable changed from its neighbours,
verdict by whether the agent kept answering.

| Run | Driver | Verdict |
|---|---|---|
| `/accel`, launched from a DOS box, mini-VDD with the V86 port trap | Velocity9x `29fb2d8` | **lock** |
| `/accel`, launched by Win32 exec, no DOS box, same mini-VDD | Velocity9x | **lock** |
| `/accel`, exec, trap-free mini-VDD | Velocity9x | **lock** |
| `V9XTC32 pump`: 2000 engine fills from ring 3, idle wait before each | none involved | done |
| `V9XTC32 pumpn`: 2000 engine fills, no waiting of any kind | none involved | done |
| `V9XTC32 pumpc`: 2000 overlapping engine BitBLTs | none involved | done |
| `/accel /kinds:1 /nocompare`: 500 fills, no readback | Velocity9x, accel on | done |
| `/accel /kinds:1`: fills with readback | Velocity9x, accel on | **lock** |
| `/accel /kinds:1`, `GdiAccelSync=1` (engine idle before every return) | Velocity9x | **lock** |
| `/accel /kinds:1`, `GdiAccel=0` (DIB Engine fills, same readback) | Velocity9x, engine untouched | **lock** |
| `/accel /kinds:1 /noescape` | **Windows 98 `S3.DRV` + `S3.VXD`** | **lock** |
| Agent screenshots (full-screen GetDIBits), dozens per day | any | fine |
| The desktop, hours of use | any | fine |

Reading down the table: the engine is exonerated by the three pumps and by
the `GdiAccel=0` row; the driver is exonerated by the stock-driver row; the
readback alone is exonerated by the screenshots. What locks is **CPU writes to
the linear framebuffer followed closely by CPU reads of the same region**, at
the rate a Pentium III drives them. Fills without readback survive; readback
without fills survives; the two interleaved do not.

## What it is, most likely

A PCI 2.0-era S3 Trio64 behind a Pentium III chipset. The host posts writes to
the aperture and then issues a read that must drain them; a target that
retries or stalls that read on a bridge configured for delayed transactions
or fast back-to-back can wedge the bus with interrupts off, which is what a
hard lock with no ICMP is. It is the fault class the board's BIOS settings for
PCI delayed transaction, passive release and PCI burst exist to trade off, and
the one S3's own driver of the period carried a `BusThrottle` switch for. None
of that is measured here; it is the shape that fits every row above.

Hypotheses killed along the way, each by a row above: an engine FIFO the CPU
could overrun; an engine command left in flight when the CPU touched the
framebuffer; the mini-VDD's V86 port trap; the DOS box; any Velocity9x
code path at all.

## What to try, in order

1. **BIOS:** PCI delayed transaction off, passive release off, PCI burst /
   dynamic bursting off, one at a time, with `/accel /kinds:1 /noescape` under
   the stock driver as the probe - it locks in two seconds when the fault is
   present and passes in ten when it is not, and it commits Velocity9x to
   nothing.
2. **Another slot**, if the board has one on a different PCI segment.
3. If neither cures it, the card and this board do not go together, and the
   Trio64's hardware target is BARRY once BARRY is back.

## Instruments this left behind

`V9XGDI /accel` takes `/kinds:N` (bitmask of operation kinds), `/ops:N`,
`/nocompare` and `/noescape`, and writes a `Progress=` key per operation.
`V9XTC32` takes `pump`, `pumpn`, `pumpf` and `pumpc`. `GdiAccelSync=1` in
`[Velocity9x]` makes every Trio64 operation wait for idle before returning
(default 0; it did not help here and costs nothing to keep). The stock
driver files and the two registry switch files are on the machine:
`REGEDIT /S C:\S3STOCK.REG` binds `S3.DRV`, `REGEDIT /S C:\V9XBACK.REG`
binds Velocity9x again, each followed by a reboot.

## State left behind

Hard-locked at the end of the session; boots the **stock S3 driver** on
reset. Velocity9x's binaries (`29fb2d8` display driver and HAL with the
`GdiAccelSync` build, the `-NoVramSize` mini-VDD) are still installed and
one registry file away. `SYSTEM.INI` `[Velocity9x]` has `GdiAccel=0`.
