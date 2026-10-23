# Rage 128: a CCE ring in the AGP aperture fetches and idles, but not our packets

Date: 2026-10-23

Status: **measured, one card; not working yet.** A8U4I5's Rage Fury Pro,
boot 535 throughout. The machine stayed up for every run. This follows
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

The probe then reloaded linux-firmware's microcode (256 of 256 words
read back). V9XD32 `/bpp16` passed on the driver (boot 539), and
A8U4I5 is on Velocity9x again.

## Where it stands

- The card's bus-master writes reach system memory.
- Its ring reads deliver nothing, whatever the placement.
- ATI's register state is reproduced exactly, so the remaining difference
  is outside the registers read: most likely the microcode, possibly the
  chipset's side of AGP (ATI's driver owns its aperture pages through
  VGARTD for its whole session).

Two leads, neither tried:

- **Work out the boundary and format of ATI's table**, from how
  `ati2i9aa.dll` loads it: the code that writes `PM4_MICROCODE_ADDR`
  and `DATAH`/`DATAL`.
- **Read the live microcode RAM under ATI's driver.** `RADDR`, `DATAH`,
  `DATAL` were deliberately left out of the dump. Reading them while
  ATI's CCE runs moves its read address and may disturb it, and the
  machine has already crashed twice on ATI's driver this session.

Nothing in the driver has changed. The PIO CCE from 0.28.0 stays.
