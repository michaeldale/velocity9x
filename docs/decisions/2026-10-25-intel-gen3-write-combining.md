# Write-combining the GMA 950's aperture; the HD 4600 has no pair to give

Date: 2026-10-25

Status: implemented for the three Gen3 chips (GMA 950, GMA 900, GMA
3150), measured on the netbook (W9QI2BE2: HP Mini 110, Atom N280, 945GSE
/ GMA 950, Win98 SE) at boots 251-254. The HD 4600 is left uncached: its
BIOS uses every MTRR pair (below). Michael decides whether it gets PAT
once he has these numbers.

Evidence: [`../probe/netbook-gen3-write-combining-2026-10-25/`](../probe/netbook-gen3-write-combining-2026-10-25/).
Rage 128 precedent: [2026-10-24-rage128-write-combining.md](2026-10-24-rage128-write-combining.md).
Plan: [write-combining-stage-b.md](../plans/write-combining-stage-b.md).

## What the two BIOSes leave (`V9XBOOT.INI`)

Netbook, every boot:

```
Mtrr=cpu=000f cap=00000508 def=00000c00 n=8 r=0 s=2 b=d0000000 z=00400000
Mtrr0=00000006 80000800    WB, 0 - 2 GiB
Mtrr1=7f800000 ff800800    UC, 8 MiB at 7F800000 (the stolen memory)
Mtrr2..7 = 0               free
```

Haswell 4570 (W9QI5A83, boot 76):

```
Mtrr=cpu=000f cap=00000d0a def=00000c00 n=8 r=11 s=0 b=00000000 z=00000000
Mtrr0=00000006 00000800       WB, 0 - 4 GiB
Mtrr1=00000006 00000800 hi    WB, above 4 GiB
Mtrr2=00000006 e0000800 hi    WB, 512 MiB above 4 GiB
Mtrr3=e0000000 e0000800       UC, E0000000 - FFFFFFFF (the GMADR is at E0000000)
Mtrr4=de000000 fe000800       UC, 32 MiB at DE000000
Mtrr5=dd000000 ff000800       UC, 16 MiB at DD000000
Mtrr6=1f000000 ff000800 hi    UC, above 4 GiB
Mtrr7=1ee00000 ffe00800 hi    UC, above 4 GiB
```

All eight Haswell pairs are in use, and the aperture sits under both the
WB pair 0 and the UC pair 3 (UC wins). A WC pair could not be added even
if one were free: WC overlapping WB is undefined (SDM vol. 3, 11.11.4.1),
and pair 3 also covers the GTTMMADR, the local APIC and the flash. The
exact route is PAT: entry 4 of `IA32_PAT` reprogrammed to WC (Windows 98
never sets PTE bit 7, so no other page selects it) and bit 7 set in the
PTEs of the aperture mapping below the engine reserve; PAT WC over MTRR
UC gives WC (SDM table 11-7). That is a second WRMSR site and ring-0
page-table edits, which the plan lists as out of scope; Michael (through
the parent session): wait for the netbook's numbers, no PAT work yet.

## Why 16 MiB on the netbook

Stage A planned the window from the usable size VBE reports, 7.69 MiB,
rounded down to 4 MiB. That would leave the top of stolen memory
uncached, and the engine reserve - ring, status page, the netbook's ring
at 0x6B0000 and up - lives there. The window is now the driver's whole
mapping for a chip that opts in (`v9x_mtrr_aperture_bytes`,
`enable16.c`): 16 MiB at D0000000, inside the 256 MiB GMADR BAR,
power-of-two sized and aligned, one pair. Its lower 8 MiB is all of
stolen memory; above that the GTT points every entry at the BIOS's fill
page (2026-09-12 GTT inventory), so a write there reaches the same page
it reached uncached. Every chip that does not opt in keeps Stage A's
VRAM-based report unchanged.

The Rage 128's window does not move: its mapping and its VRAM are both
32 MiB.

## Where the drains are

Every CPU store through the aperture that the GPU must see is published
by one of three TAIL writes, each in one helper:

- `v9x_d3d_i9xx_ring_submit_mode` (`d3d_i9xx.c`): Gen3/Gen4 render ring,
  which also carries the blitter, ring flips and Gen4 batches. Ring
  words, inline vertices, and whatever the CPU drew or zeroed before.
- `v9x_hsw_submit` (`eng_hsw.c`) and `v9x_d3d_hsw_submit` (`d3d_hsw.c`):
  Haswell BCS and RCS. Drained too, so PAT needs no new drain, but on
  today's uncached Haswell they cost a locked instruction and change
  nothing.

Plus one before the plane-base flip in `V9xHalFlip` (`ddhal_core.c`),
which is an MMIO write with no TAIL behind it, and the existing one in
`V9xHalUnlock`. The drain is the Rage 128's `lock or dword ptr [esp],0`.

OpenGL, Glide and 3DCIF reach the engine only through Lock/Unlock and the
render interface, so through these helpers. No code writes a GTT entry.
The 16-bit driver writes no ring. The mini-VDD's Gen3 ring open writes no
ring memory (HEAD = TAIL = 0); its diagnostic executor, built but run only
on an armed boot, writes ring words and TAIL in one step and was not
given a drain. No buffer is written through a cached mapping, so nothing
needs CLFLUSH. A poll of the status page still sees the GPU's store:
WC reads are not cached.

## Boots

| Boot | Build | `MtrrWrite=` |
|---|---|---|
| 251 | 6bcfac9a + the T14 fix (= 5dcbdb32 code), uncached | (no line: not opted in) |
| 252 | this change | `ok s=2 b=d0000001 m=ff000800` |
| 253 | same files, `C:\V9XDIAG\NOWC.TXT` present | `off-marker` |
| 254 | same files, NOWC renamed to NOWC.OLD | `ok s=2 b=d0000001 m=ff000800` |

Deployed by WININIT rename (DRV, VxD, HAL, SETP, GL), plain rename pairs,
restarts by `reset`. The previous five files are in this session's
scratchpad, `nb-before-wc`.

## What was measured (1024x576x16; no Intel Win98 driver exists to compare)

dispbench 31b28a4. Before = boot 251; NOWC = boot 253, the control on
the same binaries; WC = boot 252.

| | Before | NOWC | WC |
|---|---:|---:|---:|
| FbWrite MB/s | 105.5 | 105.1 | **2,814.4** |
| FbRead MB/s | 25.6 | 25.7 | 25.7 |
| Gdi.FillRect.1024x576 cyc/px | 30.53 | 30.71 | 1.76 |
| Gdi.PatBltSolid.1024x576 cyc/px | 30.60 | 30.56 | 1.76 |
| Gdi.BitBltDibToScreen.1024x576 cyc/px | 31.02 | 31.00 | 7.92 |
| Gdi.SetDIBitsToDevice.16TopDown.1024x576 cyc/px | 31.21 | 31.23 | 8.09 |
| Gdi.TextOut.FixedOpaque cyc/string | 185,556 | 186,965 | 81,688 |
| Gdi.BitBltScreenToScreen.256x256 cyc/px | 156.96 | 157.37 | 130.43 |
| Dd.BltFast.SysToVid.Plain.512x512 cyc/px | 16.15 | 16.01 | 2.00 |
| Dd.BltFast.VidToVid.Plain.512x512 cyc/px (engine) | 2.98 | 2.98 | 2.98 |
| Dd.Lock.Primary cyc/lock | 8,446 | 8,360 | 8,296 |
| DB3D TexUpload.Lock cyc/byte | 31.44 | 31.93 | **4.22** |
| DB3D TexUpload.Blt cyc/byte | 7.73 | 7.83 | 0.90 |
| DB3D Clear.Target cyc/px (engine) | 1.54 | 1.54 | 1.52 |

| | Before | NOWC | WC |
|---|---:|---:|---:|
| V9XD32 /ddi6, /ddi5 | PASS, PASS | PASS, PASS | PASS, PASS |
| Half-Life mwd5, Direct3D fps | 37.4 | 37.6 | 40.1 |
| Half-Life mwd5, OpenGL fps | 18.8 | 18.7 | 23.4 |
| 3DMark 2000, default tests at 1024x576x16 | not run | 913 | 1189 |

One run each; Half-Life runs are cold (first demo after launch). 3DMark
2000 picked 1024x576x16 itself once its 1024x768 default was refused.

Readings:

- **CPU writes to the aperture go 27 times faster**, and everything that
  is mostly CPU stores follows: solid GDI fills 17x, DIB-to-screen and
  SetDIBits 4x, blits from system memory 8x, texture upload through Lock
  7.5x.
- **The engine's own work does not move**: VidToVid blits, colour fills
  and the Direct3D clear take the same cycles. dispbench now reports
  those as `CPU-fast` and lists them as claimed but not delivered; that
  is its ceiling moving 27x, not the engine slowing. Its classifier
  judges against FbWrite.
- **Reads stay uncached**, so screen-to-screen GDI copies gain only
  about 20%.
- **The games gain where the CPU fills the ring**: the plan measured
  ring writes at 9.1% of Half-Life Direct3D's time and 5.6% of Quake 2's;
  Half-Life OpenGL, whose lightmap uploads go through Lock, gains most.
- **V9XD32 is value-for-value the same**: every key of /ddi5 and /ddi6
  at boot 252 equals boot 251's but a texture handle and a process id
  (T10's Gouraud samples included). The trace after all of boot 252's
  runs: no engine, breadcrumb, ring-space or flip-wait timeouts, nothing
  refused.

## What the evidence disputes or leaves open

- **DB3D died after its last test on the uncached control boot** (253):
  every test, the Cube checks included, verified, then no `[Result]`; the
  desktop was intact, no dialog, and the HAL left no fault dump (the
  `V9XTRACE.INI` on the box is an older build's). The rerun completed.
  The Rage 128 record lists the same death once on its WC boot and calls
  it "exactly where a missing drain would show". Here it happened with no
  WC at all, so it is a teardown fault of its own, not a drain. Cause not
  known.
- **The NOWC marker was cleared by a WININIT rename** to `NOWC.OLD`, not
  a delete (no deletes through the agent).
- **One machine, one boot each.** The GMA 900 and GMA 3150 take the same
  opt-in by the same-design rule; neither has run it. Their maps are the
  family's 16 MiB of a 256 MiB GMADR, which this rests on.
- **Gen4, Gen5, Ivy Bridge** share the Gen3 submit helper and so its
  drain, but are not opted in: their machines have not been read for a
  free pair and none has been measured.
- **No corruption was watched by an operator.** Correctness rests on
  V9XD32's read-backs, dispbench's verified tests and two games that ran
  to their scores.
