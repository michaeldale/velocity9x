# V9XDDP kills itself writing its own results

Date: 2026-09-17
Machine: MICHAEL-NETBOOK, Intel 945GSE
Captures: `C:\temp\intel53`, `intel54`, `intel55`
Status: cause measured from a photographed fault dialog; fixed, not yet re-run.

`V9XDDP.EXE` began dying with an illegal-operation dialog, which is why
`V9XDD.INI` was stale in intel54 and intel55 and why two files were missing
from those captures. **It is nothing to do with the display driver.**

## The dialog is the measurement

```
V9XDDP caused a general protection fault
in module KRNL386.EXE at 0002:000064aa.
EAX=2dcf84e7  CS=0157  EIP=000064aa  EFLGS=00000613
EBX=00001609  SS=2eef  ESP=00006842  EBP=0000684a
ECX=000084e4  DS=017f  ESI=000084e3  FS=0000
EDX=0000017f  ES=017f  EDI=0000ffff  GS=0000
Bytes at CS:EIP:
f3 a4 fc 1f 8b 5e 04 8b 46 06 01 07 5e 5f c9 c2
```

`f3 a4` is `rep movsb`. The operands say the rest:

- **`ECX = 0x84E4` is 34,020** - byte for byte the size `C:\V9XDIAG\V9XDD.INI`
  had reached.
- **`EDI = 0xFFFF`** is the last byte of a 64 KiB segment, and `ES` is the
  same selector as `DS`.

Windows 9x's profile writer copies the **whole file** through one 16-bit
segment on every key it writes. A file that grows past what that segment
holds runs the copy off the end, and the fault is in KRNL386 rather than in
the caller because that is where the copy is.

The threshold is bracketed by the captures rather than assumed: intel52's
file was **29,778 bytes and the probe completed**; intel53's was **34,020 and
every run after it crashed**. The exact limit is not measured and is not
needed.

What crossed it is the texture sweep the probe gained: 554 keys and 17,120
bytes, half the file.

## The fix

`v9x_write_text` counts what it writes and rolls over to `V9XDD2.INI`,
`V9XDD3.INI` and so on at 24,000 bytes - comfortably below the boundary
rather than at it, because the boundary is bracketed rather than known and
the cost of being wrong is losing the diagnostic that was meant to explain
something else.

`V9XDD.INI` gains `ResultFiles` saying how many there are, so a reader knows
to look for the rest instead of concluding the probe stopped early. Every
file is flushed at exit, not just the last: Windows caches profile writes and
a mode change at exit discards the cached tail of any of them.

## What this does not explain

Nothing about rendering. The probe's own D3D results were never the thing in
question - it drew its triangle correctly in intel52 - and this fault happens
while writing results down, after the measurements are taken.
