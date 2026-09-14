# A CPU write to stolen memory does not stick; the GMADR aperture is the path

Date: 2026-09-14
Status: measured on MICHAEL-NETBOOK, build `a8f798f`. The Phase 4 staging
step is changed accordingly; the replacement is not yet confirmed on the
machine.
Machine: 945GSE / GMA 950, Windows 98 SE from the live USB stick.

## What was measured

Phase 4's staging step wrote ten command dwords and a guard pattern into the
reserved 128 KiB at physical `7FF90000` through a mini-VDD
`_MapPhysToLinear` mapping, then read them back. On the armed boot it
reported:

```
Result=GTT-MIRROR-FAILED
IntentStep=stage-write
StageFail=00000001      ; the mini-VDD refused the first dword
StageIndex=00000000
StageVxdFail=00000009   ; the store did not read back
```

Code 9 is set only after the mapping succeeded, the target address passed
every check, and the store instruction executed. The routine does

```asm
mov  [edi+ecx*4], edx
cmp  [edi+ecx*4], edx
jne  refuse
```

with `ecx` zero and `edx` zero, so it wrote zero and immediately read
something else back from the same linear address, in ring 0, with no fault.

## What it means

The mapping is fine: `_MapPhysToLinear` returned an address, which rules out
the first suspicion. What does not work is the access itself. Stolen memory is
carved out of system RAM by the BIOS and claimed by the chipset; a CPU access
aimed straight at those physical addresses is not routed to it. The **GMADR
aperture, translated by the GTT, is the CPU's path into that memory**, which is
what the aperture is for and what the Linux driver uses.

This is consistent with everything measured before. Phase 2 read aperture
offsets 0 and `790000` successfully, both returning zero, and its PTE dump
shows entry 1936 mapping `7FF90000`. The aperture path has always worked. The
direct physical path had never been tried until Phase 4 staging tried it, and
the very first attempt failed.

The design note assumed the opposite, calling the physical write "the first
write to memory the VBIOS owns" and treating the aperture read as the
independent check. The truth is the reverse: there is one usable path, and it
is the aperture.

## What changed

- `V9xGmadrWrite` added beside `V9xGmadrRead`, same selector and the same
  policy split: the C caller proves the PTE and keeps the offset inside the
  VBE-reported aperture; the primitive cannot search for a boundary.
- Staging now declares each dword to the mini-VDD, which still refuses
  anything outside the reviewed stream, and then writes the bytes through the
  aperture. The guard pattern is written the same way.
- The mini-VDD's physical store and read-back are kept, but their result is
  recorded as `StagePhysRead` and `StageVxdFail=10` rather than refusing. That
  round trip was never a security property; the value check is.

The execute gate is unchanged: the mini-VDD still requires all ten dwords
declared and the whole-execution CRC to match before it will program a
register.

## Hypotheses killed

- "`_MapPhysToLinear` refuses stolen memory." It returned a mapping.
- "The parameters reach the routine wrong." Checked against the PASCAL frame
  first: value at `bp+6`, index at `bp+10`, physical at `bp+12`, `retf 10`,
  and the handler maps `Client_EBX/ECX/EDX` onto `EAX/ECX/EDX`.
- "The aperture and the physical view disagree because of a chipset write
  buffer, which the Intel Flush Page would drain." Possible in general, but it
  cannot explain a store and a load at the same linear address disagreeing.
  The flush page remains unimplemented and unneeded so far.

## Confirmed the same day

The armed boot on build `ec1b061`, with staging switched to the aperture,
reported `StageMirror=PASS`: ten dwords and a 4 KiB guard pattern written and
read back through GMADR. It then failed at `S05Failure=3`, and step 5's only
memory access is the mini-VDD verifying the staged stream through the *same*
BSM mapping. Two independent failures on the physical path and success on the
aperture path, which settles it.

The mini-VDD's ring window is therefore mapped at `GMADR + 790000` rather than
`BSM + 790000`. It is a device BAR window like the two it already maps, and it
lets the VxD verify the exact bytes the GPU will fetch, through the path the
GPU fetches them by.

## Still unknown

What the physical read actually returns. `StagePhysRead` records it from the
next boot, which will say whether the range reads as all-ones, as stale zeroes
or as something else. That distinguishes "not routed at all" from "reads work,
writes are discarded", and the answer belongs in this record.
