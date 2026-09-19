# The runtime asks for extended caps, and we decline

2026-09-20, 86Box `Win86SE` guest (ViRGE/DX, `virge_dx_pci`, Win98SE, agent
on 9869), build `af880cb-dirty` carrying the uncommitted `e33c6d6` changes.
First hardware run of the `DDHALINFO_GETDRIVERINFOSET` fix.

## It works, and the first call in the project's history was served

```
DriverInfoCalls=18      DriverInfoDeclined=17      DriverInfoLast=0x3B8A0466
CountGetDriverInfo=18
```

Before this build the count was zero on every capture ever taken, because
DirectDraw is told about `GetDriverInfo` by a flag that was never set. With
the flag set the runtime calls it eighteen times in a single probe run.

Seventeen were declined. **One was served** - the only GUID this driver
answers is `GUID_D3DCallbacks2`, so the `V9X_C3_SERVE_D3D_CALLBACKS2` path
executed for the first time since it was written.

## And the runtime wants extended caps

The trace ring caught three distinct GUIDs by their first four bytes:

```
Ring47=27390 GetDriverInfo enter 0x7DE41F80
Ring48=27391 GetDriverInfo exit  0x88760028
Ring49=27392 GetDriverInfo enter 0xFFAA7540
Ring50=27393 GetDriverInfo exit  0x88760028
Ring51=27394 GetDriverInfo enter 0x3B8A0466
Ring52=27395 GetDriverInfo exit  0x88760028
```

`0x7DE41F80` is the `Data1` of `GUID_D3DExtendedCaps`,
`{7de41f80-9d93-11d0-89ab-00a0c9054129}`. **The runtime asks this driver for
extended caps and is turned away with `0x88760028`.**

That was a hypothesis in the intel95 record and it is now a measurement. It
is where a DirectX 6 application's texture-size limits, texture-operation
caps and simultaneous-texture count come from, and the application has been
getting the runtime's defaults for all of it.

The other two values are not identified here. Guessing at a GUID from four
bytes is how this investigation has wasted time before; the ring is bounded
at 56 entries so earlier calls scrolled out, and a capture that records every
distinct GUID would name them properly.

## The uptime bracket works

```
UptimeDriverInit=39894   UptimeFirstD3d=42242   UptimeFirstFlip=44400
```

2.3 seconds from driver ready to the first Direct3D context, 2.2 more to the
first flip, on a probe rather than on 3DMark99. The instrument functions;
the slow start still has no measurement of its own.

## The state-block change is untested

```
StateMaxCount=0   StateClamped=0   StateExeBytesLast=0   RenderStateDropped=0
```

`V9XDDP.EXE` does not drive Direct3D render states through the execute-buffer
path, so nothing reached the clamp. The change is not shown working and is
not shown broken. `StateExeBytesLast` reading zero here says only that the
path was not taken - it does not bear on what `dwBlockSizeX` holds, which is
still open.

## How the run was done, and what it cost

The guest is `C:\Users\michael\86Box VMs\Win86SE`, backed up to
`claude-backup-20260920` beside it before anything was touched. Its
`net_01_link` was 0 and is now 1, and `cdrom_01_image_path` now points at
`build\win98se-s3` as a folder CD.

Two things went wrong and are recorded because they cost real time:

- `C:\86box\86box.cfg` is the install directory's configuration, not a VM's.
  Editing and launching it started the wrong machine. Restored from backup.
- The 86Box instance running since the previous day would not close: it
  raised an exit confirmation, and this session cannot inject input, so it
  was force-stopped. The VHD hash taken beforehand was of the wrong file, so
  there is no evidence that guest was idle when it died.

Replacing `V9XHAL.DLL` on disk alone is not enough and the ABI guard says so:
the first dump returned `Error=abi-mismatch, SnapshotAbi=2026091907`, because
the 16-bit driver holds the shared block from boot and carries the same
stamp. `update-associated-driver.ps1` stages both halves through
`WININIT.INI` and reboots, which is what the deploy route note says and what
worked.

## What this makes worth doing

Answering `GUID_D3DExtendedCaps` is no longer a hunch. The next step is to
record every distinct GUID the runtime asks for rather than the last one, so
the other two are named, and then to answer extended caps with this device's
real limits instead of letting the runtime invent them.
