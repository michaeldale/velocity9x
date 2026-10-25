# A8U4I5: T15's bad words are one bit of RAM in one physical range, not the driver

Date: 2026-10-25. Machine: A8U4I5 (Rage Fury Pro 5046, 440BX, Win98 SE,
about 256 MiB; `MEM`'s 64 MiB is the XMS 2.0 cap). Found by V9XD32's T15
in `2026-10-25-next-program-crashes-after-one-dies-holding-surfaces.md`
("T15 control"): one bad word, page offset 5C0h, on the first run of each
batch.

Status: the writer is not software. Every bad word is bit 23 going from 1
to 0 at a physical address of 15C0h modulo 8000h, inside one 6.6 MiB
window, under Velocity9x and under ATI's own driver alike. With Windows
kept below that window (`MaxPhysPage=0A000` in SYSTEM.INI, 160 MiB), T15
passes. A failing DRAM column is the reading that fits; it is inferred,
not confirmed (no memory tester, no DIMM swap yet).

## What was added to find it

V9XD32 (`tools\diag\d3d32_probe_win32.c`):

- T15 writes every bad word out (first eight per region) as
  `offset linear found expected pte laid now`, and the last bad offset.
- The heap and the mapping are scanned again at every stage line, and
  before and after the fill's file write; `T15Seen` gives the stage, the
  counts and the words, so a run that dies early still says where.
- `T15Paged` counts pages whose page-table entry changed since the fill.
- `/t15early` (fill before DirectDraw loads), `/t15only` (mode set and
  enumeration, no surface, 30 s watch), `/t15bare` (no DirectDraw at all;
  own log `V9XD32W.TXT`), `/t15small` (4 + 1 MiB), `/t15ones` (all bits
  set), `/t15frame=<hex>` (where a physical frame landed),
  `/t15watch=<s>` (scan every 250 ms, log and repair each hit),
  `/t15hunt=<cycles> [/t15mb=<MiB>] [/t15fresh] [/t15frames]` (a block
  written all ones / all zeros by turns, every flipped bit logged with its
  physical address).

`PFRM.VXD` (`tools\diag\page_frames.asm`, `scripts\build-page-frames.ps1`)
copies the caller's page-table entries with `_CopyPageTable`. Ring 3
cannot read Windows 98's page tables: a guarded read at FF800000h failed
on every page.

## Runs

Physical address = frame from the page-table entry + page offset. Run
logs are kept outside the tree.

| Boot | Driver | Run | Result | Bad words (linear, physical, found / expected) |
|---|---|---|---|---|
| 617 | V9x 5dcbdb32 | `/ddi5 /t14nomode` 1st | FAIL | heap D685C0h (other session) |
| 618 | V9x 6bcfac9a | same, 1st | FAIL | map 785C0h (other session) |
| 619 | V9x 5dcbdb32 | 1st | FAIL | map 835505C0, C05AFC2A / C0DAFC2A; seen at stage 4 |
| 619 | | 2nd | FAIL | heap 00F835C0, 5527DFD5 / 55A7DFD5; seen at stage 4 |
| 619 | | `/t15bare` | PASS | |
| 619 | | 4th | FAIL | file read buffer 004095C0, phys 0AEC95C0, A6739A23 / A6F39A23 |
| 619 | | 5th-8th | PASS | 8th: frame 0AEC9 in the map, its word had bit 23 clear already |
| 619 | | 9th, `/t15ones` | FAIL | map 838805C0, phys 0ACE95C0, FF7FFFFF; stage 4 |
| 619 | | `/t15bare /t15watch=240` beside a full run | PASS | no hit in 4.5 min |
| 619 | | 10th, `/t15ones` | crashed in T9 | |
| 619 | | `/t15only` x3 | PASS | |
| 620 | V9x | `/t15only /t15ones` 1st | PASS | |
| 620 | | full, `/t15ones` | 1 heap + 1 map at stage 4, crashed in T9 | |
| 620 | | full x3, no DOS box between | hits at stage 4 (2, 4, 4), all crashed in T9 | |
| 620 | | `/t15only` | FAIL | 8+ words during the fill: phys 0A8715C0, 0A9215C0, 0A9715C0, 0A9D95C0, 0AA515C0, 0AA715C0, 0AA795C0, 0AC515C0 |
| 620 | | `/t15bare /t15watch=60` | hit, repaired | map 8366D5C0, phys 0ABB15C0, during the fill |
| 621 | ATI 4.13.7192 | `/t15bare /t15watch=60` 1st | hit, repaired | heap 025755C0, phys 0AEE15C0, during the fill |
| 621 | | `/t14nomode` (`TraceReadable=0`) | FAIL | heap 0193D5C0, phys 0A8E95C0, during the fill |
| 621 | | `/t15hunt=20`, 96 MiB | 5 hits | cycles 0 (4) and 2 (1), all 00800000, ones cycles only; frames not read (the VxD's 8192-page cap, since chunked) |
| 621 | | `/t15hunt=12 /t15fresh` | 1 hit | cycle 0, phys 0AA695C0; block spanned frames 01000-0E49F |
| 622 | V9x, `MaxPhysPage=0A000` | `/ddi5` 1st | PASS | |
| 622 | | `/t15hunt=12`, 96 MiB | 0 hits | frames 00550-09DDB |
| 622 | | `/ddi6` | PASS | |

Later capped boots are below under "Fix tested".

## What the evidence says

- **One bit, one direction.** Every bad word differs from what was written
  in bit 23 alone, 1 to 0. The hunt's zeros cycles never flipped a bit.
- **Locked to physical addresses.** Fourteen physical addresses read
  (frames 0A871, 0A8E9, 0A921, 0A971, 0A9D9, 0AA51, 0AA69, 0AA71, 0AA79,
  0ABB1, 0AC51, 0ACE9, 0AEC9, 0AEE1): every one is 15C0h modulo 8000h,
  i.e. frame = 1 modulo 8 and page offset 5C0h, and all lie in
  0A8715C0-0AEE15C0. The allocations they came from were spread far wider:
  the boot-621 fresh hunt held frames from 16 MiB to 228 MiB, 3070 of them
  in the 15C0h class. A fixed bank and column across a block of rows is
  the shape of a DRAM column (or bit-line) fault on one data line.
- **Not the driver.** ATI's driver, with no Velocity9x HAL answering
  (`TraceReadable=0`), gave the same signature (boot 621).
- **Not Direct3D or DirectDraw in the process.** `/t15bare` hit on boots
  620 and 621.
- **A location fails once, early.** Hits come during the fill or within
  the first second after it; a repaired word was not hit again in 60 s;
  the 96 MiB hunt failed on its first ones cycle and once on the second,
  never later. The fresh-allocation hunt (same process, freed and
  recommitted) failed only on its first cycle. Why a cell fails on the
  first write after the frame changes hands and then holds is not known.
- **"First run of a batch"** follows from that: after a boot, frames in
  the window are free and a 40 MiB allocation takes some of them; once
  they have failed and been rewritten, or are held by something else, the
  next run does not see them. It is a tendency, not a rule: boot 619's
  second, fourth and ninth runs failed too.
- **With the window excluded, nothing fails** (boots 622 onward).

## Hypotheses killed

- **Velocity9x writes it** (HAL, 2D, CCE PIO, mini-VDD): same signature
  under ATI's driver.
- **Something in the probe's Direct3D path:** `/t15bare` loads no
  DirectDraw and hits.
- **Paging:** no pattern page's frame changed in any run (`T15Paged`).
- **One stuck or weak cell:** fourteen different frames; frame 0AEC9 sat
  in an all-ones pattern on boot 619 and stayed clean.
- **A stale linear pointer:** the linear addresses are in the private heap,
  the shared arena and the probe's own .bss.
- **A writer running all the time:** a 4.5-minute all-ones watch had no
  hit, including during a full Direct3D run beside it.
- **The agent's DOS boxes** (each `shell` creates and destroys a VM):
  three runs with no shell between them all hit.
- **A CPU cache set fault:** a Coppermine L2 way is 32 KiB, which would
  give the same address modulo 32 KiB, but not one 6.6 MiB physical range.
- **Write-combining over RAM:** the boot INI's MTRR record (dated
  2026-10-23, not re-read on these boots) has RAM 0-256 MiB write-back,
  and WC or UC only from D8000000h.
- **Bus-master descriptor write-back** (UHCI TD status is bit 23 too): a
  TD's status dword is at 4 modulo 16, these are at 0, and the range lock
  fits no descriptor pool.

## Not explained

- From the first `/t15ones` full run on (boots 619 and 620), V9XD32
  crashed at T9's `BeginScene`: `mov eax,[eax]` on the Direct3D device's
  own vtable faulted (device 87014088h, then 8731A088h; HAL fault trace),
  and every full run after it on boot 620 did the same, while the hits
  grew from one or two a run to eight or more during the fill. A RAM bit
  in a page table or a DirectDraw structure would do it; nothing measured
  shows which.
- Why 3DMark 2001 SE read its own data back corrupt
  (`2026-10-25-rage128-32mib-map-corrupts.md`) under the ring and not
  under PIO. Disk cache pages in this window would corrupt file data the
  same way; the ring-versus-PIO split was two runs each.

## Fix tested (test machine, not driver)

`MaxPhysPage=0A000` added to `[386Enh]` in A8U4I5's SYSTEM.INI (the
original is kept as `C:\V9XDIAG\SYSINI.ORG`), so Windows uses no frame at
or above 160 MiB. Velocity9x `ati` package from `5dcbdb32` (unchanged),
V9XD32 `t15diag-10`.

| Boot | First run | Second run |
|---|---|---|
| 622 | `/ddi5` PASS | `/ddi6` PASS (after a 0-hit hunt) |
| 623 | `/ddi6` PASS | `/ddi5` PASS |
| 624 | `/ddi5` PASS | `/ddi6` PASS |
| 625 | `/ddi6` PASS | `/ddi5` PASS |
| 626 | `/ddi5` PASS | `/ddi6` PASS |

All with `/t14nomode`, `Result=PASS`, no engine timeouts: five first runs
of a batch, ten runs, five at each DDI. Uncapped, the first run of a boot
failed T15 on boots 617, 618, 619 and 621 (ATI); the one first run that
passed, boot 620's, was `/t15only`.

Gates: `scripts\run-checks.ps1` passed with the probe change.

## Next

- Michael: a memory tester on A8U4I5 over 0A800000-0AF00000, or swap the
  DIMMs and see whether the window moves (the 440BX DRB registers would
  say which row holds it). Until then `MaxPhysPage=0A000` stays.
- Re-run the 3DMark 2001 SE ring/PIO comparison with the cap before
  blaming the ring.
- Results measured on A8U4I5 before this date that touched memory in
  quantity carry this fault; T14's next-program crash was reproduced on
  the netbook, so that fix does not rest on A8U4I5.
