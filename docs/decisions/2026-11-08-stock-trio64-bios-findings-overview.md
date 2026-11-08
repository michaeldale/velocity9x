# Stock Trio64-family BIOS findings from trio64-vbios-tools: the overview

Date: 2026-11-08. Contributed by perchrh, from the measurements behind
[trio64-vbios-tools](https://github.com/perchrh/trio64-vbios-tools) issue
[#138](https://github.com/perchrh/trio64-vbios-tools/issues/138). Facts
about the stock BIOSes, the chips and 86Box only, each with the record that
carries it. One file per fact, so any subset can be taken upstream on its
own.

| Fact | File | Owed here |
|---|---|---|
| Modes the stock BIOSes only offer interlaced, and interlaced defaults | [2026-11-08-stock-s3-interlaced-modes.md](2026-11-08-stock-s3-interlaced-modes.md) | a gap in the mode table, not urgent |
| The stock 2.04.07 refresh defaults an LCD rejects; the 4F14h rate interface | [2026-11-08-stock-775-refresh-defaults.md](2026-11-08-stock-775-refresh-defaults.md) | optional: 4F14h on the V2 |
| 4F15h (DDC) with interrupts off for the whole call, and a PIT write | [2026-11-08-stock-ddc-4f15-interrupts-off.md](2026-11-08-stock-ddc-4f15-interrupts-off.md) | none; two hazards to know |
| The stock POST monitor sense can switch grey-scale summing on | [2026-11-08-stock-post-grey-scale-summing.md](2026-11-08-stock-post-grey-scale-summing.md) | none yet; first suspect for a grey palette |
| 4F02h clears all of video memory unless bit 15 is set | [2026-11-08-4f02-clears-all-video-memory.md](2026-11-08-4f02-clears-all-video-memory.md) | none; the measurement behind the 8000h default |
| The S3 RAMDAC is 6 bits; 4F08h always answers 6 | [2026-11-08-s3-ramdac-6-bit-4f08.md](2026-11-08-s3-ramdac-6-bit-4f08.md) | none; answers the open 4F08h record |
| The stock 4F04h save/restore covers ADVFUNC_CNTL | [2026-11-08-4f04-d3-covers-advfunc-cntl.md](2026-11-08-4f04-d3-covers-advfunc-cntl.md) | none; the missing link in the GDI record |
| 4F03h never returns bit 15 | [2026-11-08-4f03-bit15-never-returned.md](2026-11-08-4f03-bit15-never-returned.md) | a caveat for the mini-VDD sweep |
| The stock mode set leaves the 3C0h flip-flop unknown | [2026-11-08-stock-mode-set-3c0-flip-flop.md](2026-11-08-stock-mode-set-3c0-flip-flop.md) | a footnote for whoever writes 3C0h |
| 4F02h accepts modes that do not fit the memory | [2026-11-08-4f02-accepts-over-memory-modes.md](2026-11-08-4f02-accepts-over-memory-modes.md) | none; already refused or hidden |
| 4F06h changes the upper halves of EAX, ECX and EDX | [2026-11-08-4f06-upper-halves.md](2026-11-08-4f06-upper-halves.md) | none; no 32-bit state is exposed |
