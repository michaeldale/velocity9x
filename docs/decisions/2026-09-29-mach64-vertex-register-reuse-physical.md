# Mach64 vertex-register reuse runs on the Gateway, without the estimated gain

Gateway Solo 2150, Rage Mobility-M `1002:4C4D` revision `0x64`, Windows 98 SE,
remote agent `10.0.1.22:9869`.  Build `fa99461-dirty`, boot 58.  The candidate
is the implementation of `docs/plans/mach64-vertex-register-reuse.md`: its
Mach64 draw path keeps a three-slot cache for one batch, chooses the slot
permutation retaining the most equal encoded vertices, writes only changed
slots, and computes `ONE_OVER_AREA` for the resulting slot order.

## Deployment and broad probe

`scripts/update-associated-driver.ps1` installed the ATI package and compared
all four installed driver files byte-for-byte with the staged package after the
reboot.  The machine returned at 1024x768x16 as boot 58.

`V9XDDP.EXE` then completed in 40.227 seconds.  Its report says
`Build=fa99461-dirty`, `Result=COMPLETE`, spans two files, and contains 125
`*_Ok=1` observations and no `*_Ok=0` observation.  The retrieved files are:

- `build/driver-results/mach64-vertex-reuse-physical/probe/V9XDD.INI`, CRC32
  `3FBB54E0`;
- `build/driver-results/mach64-vertex-reuse-physical/probe/V9XDD2.INI`, CRC32
  `EB32C299`.

This is physical evidence that the enabled reuse path survives the existing
Direct3D scene matrix.  The cache is local to one batch; state batches, fills
and copies therefore start the next batch invalid regardless.

## Isolated slot experiment

Probe build `mach64-slot-permutation-20260929-a` added two bounded tests to
`V9XDDP` and ran them on the same installed HAL and boot:

1. It disabled culling, submitted one triangle in each of all six vertex
   orders as separate batches, captured the first complete 64x64 RGB565
   target, and compared every pixel of the other five targets against it.
   `D3DSlotPerm0Mismatch` through `D3DSlotPerm5Mismatch` are all zero and
   `D3DSlotPermutationsOk=1`.
2. It drew a two-triangle fan first as two separate calls, for two complete
   19-write setup packets, then in one six-vertex call.  The latter enters the
   reuse builder as one two-triangle batch and emits only one changed vertex
   plus `ONE_OVER_AREA` for the second triangle.  Comparing all 4096 pixels
   gives `D3DSlotPartialMismatch=0` and `D3DSlotPartialOk=1`.

The probe exited zero in 19.032 seconds and retained `Result=COMPLETE`.
Retrieved evidence:

- `build/driver-results/mach64-slot-permutation/V9XDD.INI`, CRC32 `7462C3A8`;
- `build/driver-results/mach64-slot-permutation/V9XDD2.INI`, CRC32 `FE86869E`;
- uploaded `V9XDDP.EXE`, CRC32 `71C76CA6`.

This closes the plan's two central hardware questions: the vertex registers
survive a triangle long enough for the next triangle in the same batch, and
all six slot permutations render identically when paired with their own signed
area word.  It does not claim retention across another engine operation; the
runtime deliberately does not rely on that.

## Quake 2

Quake 2 Demo in `C:\Q2Demo` ran with `+set timedemo 1 +demomap demo1`, the same
640x480 fullscreen workload used for the 8.4 fps baseline.  A 75-second bounded
run produced five steady ten-second intervals:

| Interval | Swaps | Present ms | Draw ms | Upload ms |
|---|---:|---:|---:|---:|
| 1 | 78 | 312 | 2635 | 637 |
| 2 | 81 | 326 | 2693 | 641 |
| 3 | 82 | 335 | 2678 | 608 |
| 4 | 82 | 327 | 2700 | 616 |
| 5 | 81 | 325 | 2665 | 639 |

The last four intervals are 8.1 to 8.2 fps.  The prior build reached about
8.4 fps.  Register reuse therefore did not deliver the plan's estimated gain;
within run-to-run variation it made no measurable improvement.  The final
interval had accumulated 1,259,195 hardware-textured triangles.  The process
was ended by the controller's bound, returned to the desktop, and the remote
agent remained responsive on boot 58.

The ICD log is
`build/driver-results/mach64-vertex-reuse-physical/V9XGL-75S.LOG`, CRC32
`C377B47C`.  A post-run `V9XTRACE` invocation recorded zero engine FIFO
timeouts, zero idle timeouts and zero engine resets in
`build/driver-results/mach64-vertex-reuse-physical/V9XSNAP-AFTER.INI`, CRC32
`F19CC983`.  That snapshot loads DirectDraw in a new process and is useful for
the shared engine health, not as a lifetime total for the terminated Quake 2
process.

## Decision

The implementation is physically validated on this one board: the isolated
permutation and partial-packet comparisons are exact, the broad probe
completes, Quake 2 sustains more than a million triangles, the machine returns
to the desktop, and no shared-engine timeout or reset is reported.  The
performance hypothesis is rejected.  Do not claim a speedup, and do not
generalise register retention beyond a draw batch or this Mobility-M.
