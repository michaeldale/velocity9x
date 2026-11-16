# Oland: 3DMark 2000 faults in its own code at 004E23D1 with two texture stages

Date: 2026-11-17

Machine: W9QI0DAD (Oland XT `1002:6610`, FX-6300, Win98 SE), boots 54
and 57. Build: the step 4 HAL of
[ati-gcn-ps11.md](../plans/ati-gcn-ps11.md), `28c632e4-dirty` (two
texture stages on the Oland, record
[2026-11-17-oland-two-texture-stages.md](../decisions/2026-11-17-oland-two-texture-stages.md)).

Status: **cause found, in 3DMark 2000 itself**: a read one key past the
end of an animation track's key array, which faults only when the array
ends at the edge of an unmapped page (below).

## What happens

In two of three default 3DMark 2000 runs on the two-stage build, the
benchmark stops about 4.2 minutes in, with no score. Each time V9XTRACE's
process watch recorded an access violation (`C0000005`) at `004E23D1`,
inside `3DMARK2000.EXE` itself (module base 00400000), not in a
Velocity9x DLL.

Both faulting runs stopped at about flip 11,560 of the run. In the
third run, frame grabs every 1,150 flips put the fill-rate test's
textured tunnel at flip 11,500. That third run passed the same point and
reached a score (3872).

| Run | Build | DrawPrimitives2 records | Two-unit batches | Outcome |
|---|---|---|---|---|
| boot 51 | one stage, DDI 5 | 12 | 0 | score 3808 |
| boot 55 | one stage, DDI 6 forced (`[Velocity9x.Direct3DDdi] 3DMARK2000.EXE=6`) | 4,013,912 | 0 | score 3671 |
| boot 54, run 2 | two stages | about 2.5 M | 869,029 | fault at 004E23D1 |
| boot 57, run 4 | two stages | 2,423,758 | 868,971 | fault at 004E23D1 |
| boot 57, run 5 | two stages, grabs across the run | | 855,905 | score 3872 |

Two stages make the core give every program DDI 6
(`v9x_d3d_select_ddi`), but DDI 6 alone did not fault: the boot 55
run's 4.0 M records completed. In every run no refusal, validation
refusal or engine timeout counter moved beyond the pre-existing texture
op 8 refusal, and V9XD32 at the same boots passes everything,
T12 and T13's two-unit form included. The GT218, with two stages on
DDI 6 since 2026-11-15, completes 3DMark 2000.

## The faulting instruction

From the installed 3DMark2000.exe (image base 00400000, `.text`, file
offset E23D1), decoded by hand: `D9 06`, `fld dword ptr [esi]`. Around
it are `fld [edx]` / `fsub [ecx]` / `fstp [ebp+8]`, `fld [edx]` /
`fsub [ebx]`, then `fld [esi]` / `fsub [ecx]` / `fstp [ebp-14h]`:
differences between floats behind four pointers, which looks like edge
vectors of three-float positions. So the program dereferences a pointer
to float data, most likely vertices, that is no longer valid. Without
the registers it is not known where `esi` came from: a vertex buffer the
driver handed out, the program's own arrays, or something else.

## The registers: 3DMark reads past its own key array

The HAL's fault filter writes the exception context to `V9XTRACE.INI`.
Boot 57 run 4's copy is in its probe folder:

```
Eip=004E23D1  Ebx=00A27FA0  Ecx=00A27FC8  Edx=00A27FC8
Esi=00A28018  Edi=00A27FA0  Eax=00000000
```

Disassembled with Open Watcom's `wdis` (it reads the PE file; listing
offsets are `.text`-relative, `.text` at 00401000), the function
evaluates an animation track:

- An array of 40-byte keys, from a begin pointer to an end pointer
  (`ebx` on entry).
- A loop finds the first key whose time is at or past the time asked
  (`[ebp+8]`).
- From that key it takes the one before (`edi`, later `ebx`), the key
  itself (`ecx`, `edx`) and the one after (`esi`) for a spline.

When the loop runs off the end (the time is past the second-to-last key):

- `esi` is the end pointer.
- The "current" key becomes the last one, and the one before it the one
  before that.
- `esi` is moved one more key on, and kept, because it is no longer
  equal to the end.

The registers are that path to the dword:

- the keys are at 00A27FA0 and 00A27FC8, a two-key track ending at
  00A27FF0;
- `esi` = 00A28018 is one key past the end, 24 bytes into the page
  after;
- the page from 00A28000 is not mapped.

So the fault is 3DMark 2000's own read past the end of a key array. It
faults only when the array ends within one key of an unmapped page, and
only when the time asked is past the track's last segment, as it can be
on a test's last frames. Otherwise the read returns whatever follows and
the run goes on, as run 5 did. The same addresses in runs 2 and 4 say
the heap layout repeats between runs of one build. The two-stage build
changes what is allocated in the process, which is enough to move that
array to a page end.

Status: **cause found, in the application.** Nothing the driver returned
is wrong by this evidence. What it does change is the process's memory
layout, so the fault can come and go with any change to the driver.

## What is not known

- Which allocation the two-stage build adds or moves that puts the array
  at a page end. Not traced: it would explain the layout, not change the
  program's bug.
- Why one run of three passed. Run 5 armed frame grabs, which add work
  at 10 flips of the run, so timing may matter. One passing run proves
  nothing either way.
- Whether a draw, a lock, a caps answer or the DDI 6 state path under
  two stages hands the program something it does not expect.

Evidence: [`../probe/oland-two-stages-w9qi0dad-2026-11-17/`](../probe/oland-two-stages-w9qi0dad-2026-11-17/),
`3dmark2000-2`, `3dmark2000-3-one-stage-ddi6-boot55`,
`3dmark2000-4-two-stage-boot57`, `3dmark2000-5-two-stage-grabs`.
