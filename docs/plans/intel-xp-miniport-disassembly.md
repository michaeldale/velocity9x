# Finding the erratum 12 workaround in Intel's XP miniport

Status: plan, 2026-09-13. Desk work, no hardware. The Phase 4 write gate
(`docs/decisions/2026-09-12-intel-phase4-errata-gate.md`) opens only on a
result from this plan or an explicit narrower risk decision.

## Question

What did `igxpmp32.sys` 6.14.10.4864 start doing on 945 silicon that
6.14.10.4814 did not, once the erratum 7 change (removal of render-clock
switching, `2026-09-13-intel-xp-miniport-diff-across-the-erratum-fix.md`) is
set aside? Whatever remains and survives into 6.14.10.4885 is the candidate
set for the erratum 12 workaround.

## Inputs

`C:\temp\intelcompare\{Old,Other,New}\...\igxpmp32.sys`, versions 4814, 4864,
4885. Also `igxpdx32.dll` from each, second priority. The 945GSE datasheet
309219-006 and spec update 309220-0132 for register names.

## Method, in order

0. **Tooling.** Ghidra headless or IDA Free, whichever is installed; both
   load PE drivers and resolve `VideoPort*` imports. Record the tool and
   version in the write-up. Do not spend time on a diff plugin until steps 1
   and 2 have run by hand.

1. **Configuration-space access sites.** In each build, list every call to
   `VideoPortGetBusData` and `VideoPortSetBusData`, and every caller of the
   driver's own `PCIReadCfgReg` wrapper (the string exists, so the routine
   does). For each site record the slot or bus/device/function argument and
   the offset. Diff the three lists. A new **write** to device 0 function 0,
   or any access at offset `60h`, is the first-class candidate. Host-bridge
   offsets that matter on 945: `52h` GGC, `5Ch` BSM, `60h` (undocumented,
   the Linux flush page), `A0h`-`AFh` PAM region, and anything in `90h`-`FFh`.

2. **The WA table.** The string "disabled in WA file" says workarounds are
   data. Find the table: look for a structure indexed by device id (`27A2`,
   `27AE`, `2772`) and revision, near the device-name strings. Diff its rows
   for the 945 entries across the three builds. A bit that flips at 4864 and
   stays flipped is a candidate; then find the code that reads that bit.

3. **New MMIO writes in the 945 path.** From the functions reached by the
   945 device-id branch, list MMIO stores with immediate offsets. Compare
   across builds. Candidates of interest by name from `i915_reg.h`:
   `GFX_FLSH_CNTL` `2170h`, `MI_ARB_STATE` `20E4h`, `CACHE_MODE_0` `2120h`,
   `INSTPM` `20C0h`, `ECOSKPD` `21D0h`, and anything in the `2000h`-`21FFh`
   render control block. A new store there at 4864 that persists to 4885 is
   a candidate.

4. **Ring submission path.** Find the tail write (`2030h`) and walk backwards.
   Any new instruction sequence between command emission and the tail write
   in 4864 (a fence, an extra flush command, a read-back of a register) is a
   candidate. Compare the emitted `MI_FLUSH` variants too.

5. **Cross-check against 4885.** Every candidate from 1-4 must persist into
   4885 unchanged in substance. Drop those that do not.

## Output

A decision record, `docs/decisions/YYYY-MM-DD-intel-erratum-12-<finding>.md`,
with: the tool used, the exact sites (function offsets in each build), the
diff, and a verdict. Three possible verdicts:

- **Identified**: a specific register write or command-stream change, with
  the datasheet or i915 name for the register, and an argument for why it
  addresses "an incorrect internal buffer flush" for "a specific sequence of
  processor and internal graphics memory access". Then a second record
  decides whether Velocity9x can implement it under the arm contract, and
  the gate may open.
- **Candidate, unlicensed**: a change is visible but its meaning cannot be
  tied to a documented register. Record it; the gate stays closed unless a
  narrower risk decision is taken explicitly.
- **Not found**: nothing in the 945 path changed apart from erratum 7. Then
  either the workaround is outside the miniport (the DX driver, step 6) or
  Intel's note is wrong about the build. The gate stays closed.

6. Only if step 5 yields nothing: repeat 1-4 on `igxpdx32.dll`, which owns the
   user-mode command building.

## Rules

Read-only analysis of third-party binaries for interoperability; nothing is
copied into the tree, only the described behaviour. No hardware step follows
from this plan on its own. Record the hypotheses the diff kills, not only the
one it supports.
