# The Gen3 ring TAIL register drops bit 2: submissions must be qword aligned

**Machine:** MICHAEL-NETBOOK, 945GSE A3, `8086:27AE` rev 03, AC power.
**Build:** `0e64061`. **Capture:** `C:\temp\intel43\INTEL3D0.TXT`.
**Outcome:** refused cleanly at scene 0. No hang, no reboot needed.

## The measurement

The mini-VDD wrote `0x000010BC` to RING_TAIL (`0x2030`) and read back
`0x000010B8`. Bit 2 is not writable: the register holds a qword-aligned offset
and silently discards the dword bit.

```
S0ExecFailStep=00000022   step 34, the probe submission
S0ExecFailure=0000001A    26, the code standing when the tail compare failed
S0ExecHead=00001000       the GPU consumed nothing
S0ExecTail=000010B8       written 0x10BC
S0ExecPolls=00000000      it never reached the wait
```

The executor writes the tail and immediately reads it back; the mismatch
poisoned the run and left failure 26 standing from the top of the probe step.
That is why the code names the probe step rather than an alignment fault — it
is the last code set before a check that had nothing to do with alignment.

## What made it happen

Removing the depth `BUF_INFO` on 2026-09-15 took the Phase 5 stream from 66
dwords to 63, and its primitive from dword 50 to 47. Both went from **even to
odd**, and an odd dword count is never a qword-aligned byte offset:

| Boundary | Dwords | Byte offset | Aligned |
|---|---|---|---|
| Phase 5 probe, before the removal | 50 | `0x10C8` | yes |
| Phase 5 draw, before the removal | 66 | `0x1108` | yes |
| Probe, after the removal | 47 | `0x10BC` | **no** |
| Draw, after the removal | 63 | `0x10FC` | **no** |
| Scene 4 draw (two triangles) | 78 | `0x1138` | yes |

The old boundaries were aligned by accident, not by design. Nothing checked
them, so removing three dwords silently broke both — **including the Phase 5
path**, which had drawn correctly twice and would not have again.

## The project already knew the rule

`v9x_i9xx_ring_free_space` in `src/chipsets/intel/i9xx_ring.c` refuses a head or
tail with `(head & 7ul) != 0ul || (tail & 7ul) != 0ul`. The Phase 5 and Phase 6
submission boundaries compute their offsets directly and never pass through it.

A rule enforced in one module and bypassed by the only two callers that submit
is not enforced. The alignment is now a property of the built stream, asserted
host-side against the same constants the executor uses.

## What this boot cost and returned

One cold boot, no hang, no recovery. It returned:

- The alignment rule, measured on the part rather than inferred from a driver.
- A defect in the **shipped Phase 5 path** that no host test could have caught,
  because nothing host-side knew the boundaries had to be even.
- Confirmation that the Phase 6 machinery works up to that point: the scene
  table built, the stream staged in full (`S0StageFail=0`), the verify and
  program steps passed, `S0GLow`/`S0GUpp` intact, the error registers complete,
  and `SceneFailed=0` naming the scene.

The token stayed in flight and `IntelIncomplete=1`, so the next boot refuses
until `V9XCOPY` clears it. That is the transaction behaving correctly on a run
whose result nobody can vouch for.

## Not established

Whether bit 2 is ignored on write or the write is rejected wholesale — only the
read-back was observed. It does not matter for the fix, and no experiment is
proposed to settle it.

Whether any other register in this driver has the same constraint. Only
RING_TAIL was measured.
