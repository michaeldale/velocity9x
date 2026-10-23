# Rage 128: a CCE ring in the AGP aperture fetches and idles, but not our packets

Date: 2026-10-23

Status: **measured, one card; not working yet.** A8U4I5's Rage Fury Pro,
boots 535 to 539. The machine stayed up for every run. This follows
[the ring-not-in-VRAM record](2026-10-23-rage128-cce-ring-not-in-vram.md),
for the bus-master ring Michael asked for after 0.28.0. Evidence: every
run's `R128CCE.TXT` in
[`../probe/a8u4i5-rage128-cce-ring-2026-10-23/`](../probe/a8u4i5-rage128-cce-ring-2026-10-23/).

## The probe

`R128CCE.EXE` (`tools\diag\r128_cce*`) gained:

- **ring placements:**
  - `/gart`: a locked, physically contiguous block (`_PageAllocate`),
    with a 32 KiB PCI GART table and a 64 KiB ring;
  - `/gpn`: the same, with page-number table entries;
  - `/agp`: the same block reached through `AGP_BASE`;
  - `/vgart`: 16 pages of the chipset's AGP aperture from the VMM's
    `_GARTReserve` and `_GARTCommit`, which is what ATI's driver uses;
- **setting switches:**
  - `/m8`: mode 8 (64BM_64VCBM_64INDBM) instead of 192BM;
  - `/wb`: read-pointer write-back into the locked block;
  - `/ati`: ATI's own `AGP_CNTL` `00000038`, `BUS_CNTL` `5033A300` and
    `PM4_BUFFER_WM_CNTL` `40070707`;
  - `/zero`: `PM4_BUFFER_OFFSET` as the plain distance from `AGP_BASE`;
- **one test packet:** a write of a magic value to
  `GUI_SCRATCH_REG0` (`15E0h`), read back over MMIO. This separates "the
  CCE fetched our dwords" from "the 2D engine drew".

## Settled

- **The packet and the reset are good.** The same scratch packet through
  the PIO ports writes the magic value, with and without the engine
  soft-reset first (`PioScratch`, `PioScratchAfterReset`: PASS).
- **The pointers need the reset first.** Without it, writes of 0 to
  `DL_RPTR` did not take, and it kept the previous run's 12. With the
  SDK's order (reset, then the ring registers), they take. All earlier
  ring results are confounded by that stale pointer.
- **AGP is on.** `AGP_COMMAND` (config `58h`) reads `1F000302`: `AGP_EN`
  set, 2x. The RRG says the PCI GART is for PCI mode and must be disabled
  under AGP, and with it enabled the `/gart` and `/gpn` table formats
  behaved identically.
- **VGARTD's services work with a null device object.** `_GARTReserve`
  plus `_GARTCommit` gave aperture pages at bus address `D9C00000`,
  beside ATI's `AGP_BASE` `DA000000`.

## The bus-master fetch

Reset first, scratch packet, `DL_WPTR` = 2 with `DL_DONE`:

| Ring | Mode | Write-back | ATI values | Outcome |
|---|---|---|---|---|
| VRAM, bus mastering off | 192BM | off | no | `DL_RPTR` stays 0: no fetch |
| `/agp` (locked block via `AGP_BASE`) | 192BM | off | no | `DL_RPTR` 2; FIFO empty, `MICRO_BUSY` |
| `/gart`, `/gpn` | 192BM | off | no | the same |
| `/vgart` | 192BM | off | no | the same |
| `/vgart` | mode 8 | off | no | the same (64-entry FIFO empty) |
| `/vgart` | mode 8 | on | no | the same |
| `/vgart` | 192BM | on | yes | the same |
| `/vgart` | mode 8 | off | yes | the same |
| **`/vgart`** | **mode 8** | **on** | **yes** | **`DL_RPTR` 2, then 4, then 16; PM4 idle (`PM4_STAT` `40`); the scratch register not written** |
| `/vgart /zero` (offset 0) | mode 8 | on | yes | `MICRO_BUSY` again |

`MICRO_BUSY` is `PM4_STAT` bit 17 (RRG 3-243). With the FIFO empty it
means the microengine is waiting on a bus read that never delivers.

Only ATI's combination lets the read complete:

- mode 8;
- the read pointer written back;
- `AGP_CNTL` with a 32 MiB aperture and bits 22:20 clear;
- `BUS_CNTL` with bit 29 clear;
- `PM4_BUFFER_OFFSET` = `AGP_APER_OFFSET` (`02000000`) plus the ring's
  distance from `AGP_BASE`.

With all of those, the CCE consumes exactly the dwords the write pointer
offers and goes idle, but the packet it consumes is not ours. That is
consistent with the CCE reading zeros (a type-0 write to register 0)
rather than the aperture page the CPU wrote. The probe reads its first
ring dword back correctly through the aperture's linear address
(`0000050B`, the earlier fill runs), so the CPU side holds the packet.

## Not established

- **Where the card's AGP read actually lands.** The mapping `AGP_BASE` +
  (offset − `AGP_APER_OFFSET`) is inferred from the RRG's memory-map
  figure and Linux's `R128_AGP_OFFSET`. It has not been seen to deliver
  data.
- **Whether the write-back lands.** `PM4_BUFFER_DL_RPTR_ADDR` pointed into
  the locked block, but the probe did not read that dword back.
- **ATI's other registers.** The boot-520 capture is a fixed register
  list. It does not include the memory controller's view of the AGP
  space, or anything ATI writes only while its CCE runs.

## The three follow-ups (Michael: "do all 3"), boots 535 to 539

**1. The write-back lands.** `PM4_BUFFER_DL_RPTR_ADDR` pointed into the
locked block, and that dword read `00000002`, the read pointer, in every
`/wb` run. The card's bus-master writes reach system memory.

**2. The reads land nowhere we wrote.** `/loc` fills the whole ring with
pairs, each writing `5A000000h` | its own dword index into
`GUI_SCRATCH_REG0`, so any pair the CCE executed would name itself.
Through the aperture (`/vgart`) and through `AGP_BASE` straight at the
locked block (`/agp`), each with mode 8, write-back and ATI's values, the
register stayed 0. The CCE sat in `MICRO_BUSY` with its FIFO empty. Not
one dword of the 64 KiB ring reached the microengine. The "fetches and
idles" outcome above did not repeat: the later runs stall, as the rest
of the table does.

**3. ATI's own state, captured.** The first boot onto ATI's driver
(boot 536) crashed at the desktop before any probe ran. So did boot 537,
around a full register sweep, with no way to separate the two. Boot
538 stayed up on its own for five minutes. A narrow `/dump` (bus
control, the AGP and memory-controller block, the PM4 registers,
configuration space) then ran cleanly
(`R128CCE-dump-ati-b538.TXT`):

- ATI's ring runs at bus address `DA000000` = `AGP_BASE` +
  `PM4_BUFFER_OFFSET` (0). The words around its read pointer are live
  command data, an image upload's rows (`F800F800`, `BDF7BDF7`), so the
  offset is the plain distance from `AGP_BASE`, not
  `AGP_APER_OFFSET` + distance.
- Its write-back dword equals its read pointer (`0002C625`).
- Against our driver's dump (`R128CCE-dump-v9x-b535.TXT`), the only
  differences are:
  - the ring registers;
  - the values `/ati` already copies (`BUS_CNTL`, `AGP_BASE`,
    `AGP_CNTL`);
  - `PC_NGUI_CTLSTAT`'s dirty bit;
  - the PCI latency timer (`FF` against `40`).
- So with `/ati /zero` the probe reproduced ATI's register state, and its
  ring still stalled.

The one thing no register shows is the **microcode**. ATI's
`ati2i9aa.dll` carries one table, expanded from the driver package. Only
140 of its 256 instructions match linux-firmware's, in place.

`/atiuc` loads that table, converted on the host and copied to the test
machine only; it is ATI's code and is not in the repository. With it:

- the PIO control fill and both PIO scratch controls stall;
- the RAM read back differs in 3 words;
- the ring stalls as before.

So ATI's table is not a drop-in. Either it is cut at the wrong boundary,
or it speaks ATI's own packet protocol.

It was the boundary. The table is NE segment 32 of `ati2i9aa.dll`, file
offset `65CA0h`, `800h` bytes, each instruction stored low dword first;
the cut above started at `65DE0h`. ATI's code is Linux's with 40
instructions inserted, so the later jump targets shift by `28h`. Cut at
the segment, ATI's microcode passes all three PIO controls
(`R128CCE-seg32_*.TXT`), and the ring still stalls.

The probe then reloaded linux-firmware's microcode (256 of 256 words
read back). V9XD32 `/bpp16` passed on the driver (boot 539), and
A8U4I5 is on Velocity9x again.

## Offline: ATI's start-up and mini-VDD, boot 539

Michael: "do offline". Ghidra's listing of `ati2i9aa.dll` (the CCE
start, `1008:6B90`-`6F4A`) and a capstone listing of the mini-VDD
`ati2vxaa.vxd` (LE objects relocated, `int 20h` service dwords decoded),
both kept outside the repository.

**ATI's CCE start**, in order:

1. `PM4_VC_DEBUG_CONFIG |= 8`.
2. `PM4_MICRO_CNTL` = `401F0000`. Bits 20:16 are undocumented; the live
   value under ATI's driver is the same.
3. The microcode, then `DL_RPTR_ADDR`.
4. `PM4_BUFFER_OFFSET` = 0, `DL_WPTR` = the current `DL_RPTR`,
   `WM_CNTL` = `40070707`.
5. A call into the VDD's protected-mode API (`INT 2Fh 1684h`, VxD
   `0Ah`), EAX `83h`, ECX 7.
6. From its results:
   - `AGP_BASE`;
   - `AGP_CNTL`'s size code (`38h` for 32 MiB);
   - `AGP_CNTL_B |= 10h` on the AGP path;
   - `BUS_CNTL`;
   - `PM4_BUFFER_CNTL`.

**The VDD call holds nothing hidden.** EAX `83h` is
`VDD_REGISTER_DISPLAY_DRIVER_INFO` (`MINIVDD.INC`: `80h` + 3). It reaches
the mini-VDD's `REGISTER_DISPLAY_DRIVER` handler, which dispatches on
ECX. Entry 7 only returns fields of ATI's device record: the aperture
base, its size, and AGP (1) or PCI GART (2). Those fields are filled at
start-up:

- `_GARTReserve(0, pages, 0, &dev, PG_WRITECOMBINED)`;
- a check that the address lies inside the chipset aperture;
- `_GARTCommit(lin, 0, pages, &dev, 0)`.

The mini-VDD calls no `_GARTMemAttributes`. On that path it writes no
configuration space, of the card or of the 440BX.

**The host bridge, read on our driver** (`R128CCE-dumpcfg-v9x-b539.TXT`,
`R128CCE-gartcheck.TXT`):

- `APBASE` `D8000000`, `APSIZE` `30h` (64 MiB).
- `AGPCTRL` `80h`: the GTLB is enabled.
- `AGPCMD` `302h`: AGP enabled at 2x, with SBA.
- `ATTBASE` is valid while VGARTD holds pages.
- The ring page's GART entry is `0FD98001`: bit 0 set, so the entry is
  valid.

The AGP bridge has bus mastering and the card's ranges enabled.

## On the card, boot 539

New probe switches:

- `/atis`: ATI's start-up extras (steps 1, 2 and 6 above);
- `/flush`: `WBINVD` between filling the ring and the kick;
- `/wc`: reserve the aperture pages write-combined, as ATI does;
- `/long`: a stricter locating pattern (below).

`/loc` now also seeds `MM_INDEX` with `F00h` and reads it after the
kick. A ring of zeros is type-0 writes of 0 to register 0, so zeros
would clear it.

| Run | Outcome |
|---|---|
| `/vgart /m8 /wb /atiuc /atis` (with and without `/zero`) | `MICRO_BUSY` stall, as before |
| `/vgart ... /flush` | the same |
| `/vgart ... /wc` | `_GARTReserve` refused (16 pages, null device object) |
| `/agp`, any of mode 8 or 192BM, write-back on or off, `/zero` or not, `/flush` or not, `/atis` | `DL_RPTR` 2, then 4; PM4 idle; scratch and `MM_INDEX` untouched |

The RAM path's "fetch and idle" is new: the step-2 runs above stalled.
The flush is not the cause; runs without it fetch and idle the same way.

**`/long` settles what the RAM path reads.** The ring is filled with
`5A000000h` | the index, with a header every 17 dwords: type 0, 16
writes, `ONE_REG_WR`, all to `GUI_SCRATCH_REG0`. The PIO control
(`PioScratchOneReg`: two writes to the one register) passes, so the chip
honours `ONE_REG_WR`.

A CCE that read one of those headers would:

- write a tagged value at once; or
- wait for the rest of the packet.

On the RAM path it consumed two dwords, went idle, and left scratch at
0. **The card's bus-master reads return data that is not the ring's,**
even though its writes land (the write-back). On the aperture path, the
read never completes.

## Where it stands

Measured:

- The card's bus-master writes reach system memory.
- **RAM path:** its reads complete with data that is not ours.
- **Aperture path:** its reads never complete.
- ATI's register state, start-up extras and microcode are all
  reproduced, and none of them changes either outcome.
- ATI's mini-VDD does nothing to the chipset that VGARTD does not.

Not established:

- The address the card actually reads.
- Why ATI's identical-looking set-up works.

Untried differences:

- ATI reserves its aperture once at boot and write-combined. Ours is
  per run, uncached, and the write-combined reserve was refused.
- ATI's ring starts at `AGP_BASE` + 0 with `AGP_BASE` at a 32 MiB
  boundary inside the aperture. Ours sits at the reserve's address,
  which is 4 MiB aligned.
- ATI sets `DL_WPTR` from the live `DL_RPTR` instead of writing 0.

Nothing in the driver has changed. The PIO CCE from 0.28.0 stays.
