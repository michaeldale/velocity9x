# One missing `push si` refused three armed Phase 4 boots

Filed: 2026-09-14
Status: fixed in source and guarded by check-tree; not yet confirmed on the
machine
Machine: MICHAEL-NETBOOK, 945GSE / GMA 950, Windows 98 SE from the live USB
stick
Found in: `intel-gma` builds `ca0c45c` and `1b156ea`

## Symptom

Three armed Phase 4 boots refused before any hardware write. The first said
only `PRECONDITION-REFUSED`. After the preflight was given reason codes, the
next two both reported `PreconditionCode=00000008`, the sandbox layout check:

```c
if (layout->reserve_offset != 0x00790000ul ||
    layout->reserve_physical != 0x7ff90000ul ||
    layout->scratch_offset != 0x007a1000ul) {
    return V9X_P4_PRE_LAYOUT;
}
```

The same file published those three fields, from the same struct, as
`00790000`, `7FF90000` and `007A1000`: exactly the constants being compared.
The arithmetic is also right by hand. Adding `RefReserveOffset`,
`RefReservePhysical` and `RefScratchOffset`, read through the same pointer at
the moment of refusal, produced the same correct values a third time.

So the operands were right and the check still failed.

## Cause

`wdis` on `intel_exec16.obj` shows what the compiler emitted for that check:

```asm
mov  es,word ptr -6[bp]
cmp  word ptr es:6[si],79H      ; reserve_offset  high
cmp  word ptr es:4[si],0        ; reserve_offset  low
cmp  word ptr es:0aH[si],7ff9H  ; reserve_physical high
cmp  word ptr es:8[si],0        ; reserve_physical low
cmp  word ptr es:22H[si],7aH    ; scratch_offset  high
cmp  word ptr es:20H[si],1000H  ; scratch_offset  low
```

The field offsets are correct. Watcom reloads `ES` from the stack but keeps
the pointer's **offset in SI** across the preceding calls, because its 16-bit
convention makes SI callee-saved.

The statement immediately before the check is

```c
request.revision = V9xPciReadIntelRevision();
```

and `V9XPCIREADINTELREVISION` in `src\display16\runtime.asm` saved BX, CX, DX
and DI, but not SI. It calls `V9xFindPciDevice`, which does `xor si, si`
before `INT 1Ah AX=B102h`. SI came back as zero or as a bus/device number, so
the six comparisons read from a garbage offset and one of them differed.

Everything else fits: the check is the first SI-relative access after that
call, which is why no earlier precondition failed; and the `Ref*` values were
read later from a different frame, where the compiler had reloaded the
pointer, which is why they looked correct.

## Fix

`push si` / `pop si` added to `V9XPCIREADINTELREVISION`.

An audit of all 56 far procedures in `runtime.asm` found this was the only
offender. Five mini-VDD helpers looked suspect at first because they save
`esi` rather than `si`; that covers SI and they are fine.

`check-tree.ps1` now fails any far procedure in `runtime.asm` that calls
`V9xFindPciDevice` without saving SI or ESI. Verified by removing the push and
watching the check fire.

## Hypotheses killed

- "The executor receives a different layout than the publisher." It receives
  the same pointer; the offset register was being clobbered under it.
- "The struct is packed differently in two translation units." Every field is
  `v9x_u32`, so packing cannot change the layout, and the emitted offsets
  (4, 8, 32) are correct.
- "EMR must be clear." A separate wrong guess, corrected in `1b156ea`: the
  diag table is EIR, EMR, ESR at `20B0`, `20B4`, `20B8`, and index 5 reads
  `FFFFFFFF` on this machine.
- "The stale `intel_exec16_test.obj` in the build directory is being linked."
  It is debris from 2026-09-13 and appears nowhere in the map.

## What this cost, and what made it findable

Three armed boots. The first was unreadable because twenty-odd preconditions
sat behind one boolean; the second named the check; the third carried the
operands, and it was only the contradiction between a correct operand and a
failing comparison that pointed at the generated code rather than at the
logic. Each step of instrumentation was worth its boot, and the disassembly
that settled it cost none.

## Still unknown

Whether the Phase 4 write sequence runs. No armed boot has yet reached the
first register store.
