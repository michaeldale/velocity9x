# 3DMark 2000 faults in its own code at 004E23D1 (Oland, two texture stages): the known over-read

Date: 2026-11-17

Machine: W9QI0DAD (Oland XT `1002:6610`, FX-6300, Win98 SE), boots 54
and 57. Build: the step 4 HAL of
[ati-gcn-ps11.md](../plans/ati-gcn-ps11.md) (record
[2026-11-17-oland-two-texture-stages.md](../decisions/2026-11-17-oland-two-texture-stages.md)).

Status: **known, not a driver defect.** This is the fault traced on
2026-11-15 on the same machine's GeForce 210
([2026-11-14-gt218-3d-d3d-builders.md](../decisions/2026-11-14-gt218-3d-d3d-builders.md),
"The crash is 3DMark's own over-read"). It was seen first on the
GMA 950 in the 0.35.0 release checks
([2026-11-04-0.35.0-release-checks.md](../decisions/2026-11-04-0.35.0-release-checks.md)).
3DMark 2000's keyframe evaluator at 004E2270 reads one 28h-byte key past
the end of its key vector in a track's last segment. The read faults
only when the vector ends within a key of uncommitted memory. The keys
are 3DMark's own animation data; no driver data reaches the read.

This record was opened before that earlier trace was found, and traced
the fault again. It is kept for the Oland's runs and as an index entry
that names the fault by its address.

## The Oland's runs

| Run | Build | DrawPrimitives2 records | Two-unit batches | Outcome |
|---|---|---|---|---|
| boot 51 | one stage, DDI 5 | 12 | 0 | score 3808 |
| boot 55 | one stage, DDI 6 forced (`[Velocity9x.Direct3DDdi] 3DMARK2000.EXE=6`) | 4,013,912 | 0 | score 3671 |
| boot 54, run 2 | two stages | about 2.5 M | 869,029 | fault at 004E23D1 |
| boot 57, run 4 | two stages | 2,423,758 | 868,971 | fault at 004E23D1 |
| boot 57, run 5 | two stages, grabs across the run | | 855,905 | score 3872 |

The two faults came at about flip 11,560, in the fill-rate test. Two
one-stage runs that completed do not show that two stages cause the
fault: the GT218's one-stage builds and the GMA 950 hit it too, in one
run of two or three. The rate on the Oland with and without two stages
is not measured.

## The registers

From boot 57 run 4's `V9XTRACE.INI` (written by the HAL's fault
filter):

```
Eip=004E23D1  Ebx=00A27FA0  Ecx=00A27FC8  Edx=00A27FC8
Esi=00A28018  Edi=00A27FA0  Eax=00000000
```

These match the GT218 record's path:
- the last key is at 00A27FC8, so `end` is 00A27FF0;
- ESI = `end` + 28h = 00A28018 is in the page from 00A28000, which is
  not committed.

On the GT218 the same layout was 28h-aligned at a different address,
`end` 009FFFF0h. Open Watcom's `wdis` disassembles the PE file;
listing offsets are `.text`-relative, with `.text` at 00401000.

Evidence: [`../probe/oland-two-stages-w9qi0dad-2026-11-17/`](../probe/oland-two-stages-w9qi0dad-2026-11-17/).
