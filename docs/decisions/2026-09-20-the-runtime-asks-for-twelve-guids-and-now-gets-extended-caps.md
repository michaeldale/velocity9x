# The runtime asks for twelve GUIDs, and now gets extended caps

2026-09-20, 86Box `Win86SE` guest (ViRGE/DX, Win98SE, agent on 9869), boot
613. Follow-up to the record of the same day that measured the runtime being
declined. Attached: `2026-09-20-virge-extendedcaps-V9XSNA3.txt`.

## Served

```
Ring47=25758 GetDriverInfo enter 0x7DE41F80
Ring48=25759 GetDriverInfo exit  0x00000000
```

`GUID_D3DExtendedCaps` returns `DD_OK` where the previous run returned
`0x88760028`. `DriverInfoDeclined` fell from 17 of 18 to **14 of 18**, so
four calls are answered now against one before - the runtime asks for
extended caps more than once in a run.

The answer is `D3DHAL_D3DEXTENDEDCAPS` exactly as the Windows 98 DDK declares
it in `inc\win98\D3DHAL.H`: nine DWORDs, `dwSize` plus texture and stipple
minima and maxima. It was transcribed from the DDK and not from memory,
because a later DDK appends fields - guard band, texture-operation caps,
simultaneous textures - and writing those into a caller's buffer that has no
room for them is how a driver corrupts the runtime. `dwActualSize` reports
the size this driver knows and the copy is bounded by the caller's
`dwExpectedSize`.

The values come from the shared block, where each engine's `describe_caps`
now puts its own texture limits - the ViRGE's 4 to 512, the Gen3 sampler's 8
to 256 - so what is published and what the bind enforces are one statement
rather than two that can drift. Stipple is zero: there is no stippled-fill
path, and zero is what a device without one reports.

An engine that filled nothing leaves `dwSize` zero and the GUID is declined
rather than answered with zeros. A maximum texture width of zero is a worse
answer than no answer.

**What is verified and what is not.** The call is served and the struct was
filled by an engine - a `dwSize` of zero would have declined, and it did not.
The individual limits were not read back out of the guest, because the dump
tool does not emit them.

## The twelve

`driver_info_last` held only the most recent GUID and the trace ring is
bounded, so the previous run named three of eighteen calls. The distinct
table names them all:

| Data1 | GUID |
|---|---|
| `0xEFD60CC0` | `GUID_MiscellaneousCallbacks` |
| `0xEFD60CC1` | `GUID_VideoPortCallbacks` |
| `0xEFD60CC2` | `GUID_ColorControlCallbacks` |
| `0x0BA584E1` | `GUID_D3DCallbacks2` |
| `0x80863800` | `GUID_KernelCallbacks` |
| `0xFFAA7540` | `GUID_KernelCaps` |
| `0x7DE41F80` | `GUID_D3DExtendedCaps` |
| `0xD7B70EE0` | not in the Windows 98 DDK |
| `0xB1122B40` | not in the Windows 98 DDK |
| `0xDDF41230` | not in the Windows 98 DDK |
| `0x93869880` | not in the Windows 98 DDK |
| `0x3B8A0466` | not in the Windows 98 DDK |

The seven named ones were matched against `DEFINE_GUID` lines in
`C:\98DDK\inc`, not recalled. The five unnamed ones are absent from that DDK
and are left unnamed rather than guessed at - the values are recorded so the
question stays answerable.

This is the whole channel the driver has been deaf to. Two of the named ones
are plainly worth considering next: `GUID_D3DCallbacks3` is the likely
identity of one of the unnamed values and would be worth confirming from a
later DDK, and the kernel and video-port callbacks are declined by a driver
that genuinely has neither.

## What this does not settle

Nothing here touches 3DMark99's bilinear report. That complaint is about a
capability list, the driver was already filtering bilinear on every draw
(intel96), and whether the extended caps change what the application prints
is unmeasured - this guest has not run it.

The state-block clamp remains untested: `V9XDDP.EXE` does not drive render
states through the execute-buffer path, so `StateMaxCount` and
`StateExeBytesLast` read zero again.
