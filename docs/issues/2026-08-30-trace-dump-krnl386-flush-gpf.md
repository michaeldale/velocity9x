# V9XTRACE.EXE faults in KRNL386 once DirectDraw has run

Date: 2026-08-30
Guest: `Win86SE`, 86Box, ViRGE/DX, agent port 9869. Packages `zpriv-001`,
`zfifo-001`.
Evidence: `build\driver-results\zpriv-C-tracefault`, and the snapshots in the
`zpriv-*` / `zfifo-*` directories.

`V9XTRACE.EXE` reads the driver's shared trace block through the
`V9X_DDGETTRACE` DCI escape and writes `C:\V9XDIAG\V9XSNAP.INI`. On a boot
where no DirectDraw application has run it completes in ~30 ms with exit 0
and 49 keys. After **any** run of `V9XDDP.EXE` — including `/status-only`,
which never touches Direct3D — it either hangs until the agent's timeout or
dies with a general protection fault:

```
V9XTRACE caused a general protection fault
in module KRNL386.EXE at 0002:00005c12.
EAX=2807003d CS=0157 EIP=00005c12 EFLGS=00000287
ECX=0000ffff DS=2807 ESI=00000000 FS=2f2f
EDX=0001ffff ES=017f EDI=000008c0 GS=0000
Bytes at CS:EIP: f2 ae 2b d1 4a 5f 3b 56 fe 74 03 e9 23 00 b3 3d
```

`f2 ae` is `repne scasb`: KRNL386 scanning `ES:EDI` for 64 KB with no
terminator. A second capture faulted the same way at `0002:00005c48` with
`EDI=000005a0`. `ESI=0` in both.

## What is known

- It is in the profile-write path, not the escape: keys the tool wrote before
  the fault are on disk. The values quoted in
  [`2026-08-30-virge-depth-fifo-reservation.md`](../decisions/2026-08-30-virge-depth-fifo-reservation.md)
  were all read from files a faulting run had produced.
- The faulting call is not fixed. With an added
  `WritePrivateProfileStringA(0, 0, 0, path)` after the scalar keys, the fault
  moved to that call and truncated the file at 40 keys. Without it, one run
  reached 64 keys and died inside `v9x_write_counters`, another died on the
  final flush after writing everything.
- The trigger is a DirectDraw run, not a Direct3D one, and not the volume of
  keys: a `/status-only` probe is enough.
- It is not the shared block outgrowing its DPMI selector. Measured with the
  same header the driver is built from: `sizeof(V9X_DD_SHARED)` is 3122 of the
  4096 the 16-bit side allocates, `V9X_DD_TRACE` 574, the snapshot 750.
- Whether it predates this branch is **not established**. The comparable run
  of 2026-08-29 (`build\driver-results\zfix-003`) produced a complete 117-key
  snapshot, but its exit code was not checked, and a fault on the final flush
  loses nothing but the exit code. So "it always did this and nobody looked"
  is a live possibility and the more likely one.

## Why it is filed rather than fixed

Nothing here is in the driver. It costs the exit code and, on a bad run, the
counters and ring tail. `StageScalars` / `StageLast` / `StageCounters` /
`StageRing` markers were added to the tool so a truncated file says how far it
got; the scalar counters, which include everything the depth work needs, are
written before any of the stages that fault.

## Where to start

The probe writes its own results with the same
`WritePrivateProfileStringA(0, 0, 0, path)` idiom and does not fault, so the
difference is worth isolating first: same call, same guest, one faults and one
does not. After that, whether the display driver's escape path leaves a 16-bit
selector or KRNL386's profile cache in a state the next profile write trips
over.
