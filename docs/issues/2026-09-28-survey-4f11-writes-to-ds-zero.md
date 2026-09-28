# The flat-panel query wrote over the report's own format strings

Date: 2026-09-28
Status: **cause found and fixed in source, with the mechanism corroborated in
the built image; the fixed binary is untested on the machines that showed it**

Four ThinkPad surveys arrived, build `b44c35f`, all run 2026-09-05 in real DOS:
B490 (Ivy Bridge, `8086:0166` plus an NVIDIA `10DE:0DE3`), E460 (Skylake,
`8086:1916`), P14s Gen 1 (Comet Lake, `8086:9B41` plus `10DE:1D34`) and X61
(GM965, `8086:2A02`). They are in `docs\probe\references\` as
`thinkpad-*-vgasurv-2026-09-05.ini`.

**Three of the four are truncated.** Only the P14s carries a `[Result]`.

## What the three broken reports lost

B490, E460 and X61 all end their last intact keyed line at
`DpmsCapabilities=0F10` and never emit another section header or named key.
No `[VBEModes]`, no `[EDID]`, no `[VGARegisters]`, no `[Tier1]`, `[Tier2]`,
`[Chipset]`, `[Aperture]` or `[Result]`.

The data is not missing. `Mode.NN=` rows, `Block0.` rows and the
`Seq.`/`Crtc.`/`Gdc.`/`Atc.` banks are all present and well-formed, sitting
under no section header. The survey ran to the end; it just stopped being able
to write one particular kind of line.

This is the same shape as
`docs\issues\2026-08-28-survey-report-sections-missing.md` on the Pineview
NAV50, which was left open on the guess that it was a consequence of the
null-buffer defect. It was. This is that defect's remaining half.

## The call

The next thing the tool does after `DpmsCapabilities` is `4F11h`/BL=00h, the
VBE/FP flat panel query. The P14s - the one complete report - is the machine
whose BIOS refused it: `FlatPanelStatus=unsupported`. On the three that
answered `0x004F`, the report dies at that call and `FlatPanelStatus` never
appears at all.

`4F11h` returns the flat panel information table **at ES:DI**. The call was
made with a bare `int86`, which leaves ES as the caller's DS in the small
model, and `input` is zeroed, so DI is 0. The BIOS wrote its table to
**DS:0000**.

`vbe_call` was given a scratch buffer for exactly this on 2026-08-28. Five
call sites never went through it, because they need BX or CX back and
`vbe_call` returns only AX: `4F03h`, `4F0Ah`, `4F10h`, `4F11h` and
`4F15h`/BL=00h, plus INT 10h `AH=1Ah` in `[BiosData]`. Of those, `4F11h` is
the one specified to deposit anything.

## Why only the keyed lines died

Every keyed line goes through `wr_section`, `wr_str`, `wr_u`, `wr_x8`,
`wr_x16` or `wr_x32`, and those six share six short format strings -
`"\n[%s]\n"`, `"%s=%s\n"`, `"%s=%lu\n"`, `"%s=%02X\n"`, `"%s=%04X\n"`,
`"%s=%08lX\n"`. They are the first literals the module declares, so they sit
at the head of the literal pool at the start of DGROUP. In the binary built
today they are contiguous in its first 50 bytes, `\n[%s]\n` first and
`Status` the next string after them:

| String | Offset in the image |
|---|---|
| `"\n[%s]\n"` | `0x66669` |
| `"%s=%s\n"` | `0x66670` |
| `"%s=%lu\n"` | `0x66677` |
| `"%s=%02X\n"` | `0x6667F` |
| `"%s=%04X\n"` | `0x66688` |
| `"%s=%08lX\n"` | `0x66691` |
| `"Status"` | `0x6669B` |
| `"Mode.%02u=..."` | `0x710F5` |

A table written at DS:0000 destroys them. `fprintf` with a zeroed format emits
nothing at all - not an empty line, not a newline - which is why the lines are
absent rather than blank or garbled. The `Mode.NN=` rows use a longer format
declared 2.7 KB further into the pool and containing no `%s`, and the hex rows
are assembled in a stack buffer and written with one `fputs`. Both survive.

The split in the files is exactly that split, and no other hypothesis tried
here divides them the same way: a damaged `FILE` would have taken the rows
too, and a corrupted key string would have produced a malformed line rather
than no line.

All six formats died, including `"%s=%08lX\n"` at pool offset 40. That puts
the write at 50 bytes or more past the start of the pool, so the table was
larger than the 64 bytes VBE/FP specifies as its fixed part, the null-check
area ahead of the pool is smaller than assumed, or both. **Not measured** -
nothing in these reports says how many bytes landed, only that the range
covered all six.

## The fix

One door to INT 10h. `int10_call` resolves ES:DI - the caller's buffer, or
`vbe_no_buffer_scratch` when there is none - and every INT 10h call in the
tool now goes through it, `AH=1Ah` and `AH=1Bh` included. `vbe_call_regs`
gives the sites that need BX or CX the whole register block, which is what
they were making their own bare calls to get. `vbe_call` is now a wrapper
over it. The `4F03h` and `4F15h` sites each made two calls, one guarded and
one bare; they make one now.

The scratch grew from 256 to 512 bytes. 128 was justified as "an EDID block,
the largest thing any of these functions could plausibly deposit" - a
flat-panel table is a thing that sentence did not account for, and the margin
is cheap.

The safety gate gains a banned pattern: a bare `int86` to INT 10h. The gate
could already prove `vbe_call` resolved ES:DI and could not see that five
other sites did not. Its self-test grew the matching mutation and rejects 19
now.

## What is not verified

That the fixed binary completes a report on any of these three machines.
`V9XSURV` is a DOS program and DOS work through the v9x agent leaves a guest
unable to finish its own reboot, so nothing was run. The three machines are
third-party; a rebuilt binary has to go back to whoever ran them.

Three things to look at in the next report from a B490, E460 or X61, in
order: whether `FlatPanelStatus` appears at all, whether `[Result]
Complete=yes` is there, and whether `*** NULL assignment detected` still
prints - the message the NAV50 reporter still saw after the 2026-08-28 fix,
which this defect explains.

## Also in these four reports

- The CPUID decode corrected earlier today is confirmed against them.
  `000306A9`, `000406E3` and `000806EC` decode as models 58, 78 and 142
  rather than 10, 14 and 14. The X61's `000006FA` is model 15 before and
  after, which is the case that should not move.
- The X61 is a second `ChecksumStatus=mismatch` on an Intel VBIOS, and a
  different shape from the Ironlake one: `SizeBytes` and `PcirImageLength`
  both read 65536, so there is no second length to sum and the new
  `PcirImageChecksumStatus` will not appear. Intel shadow copies not summing
  clean looks ordinary rather than an Ironlake quirk.
- The X61's Intel NIC option ROM at `D000` reported `PcirClassCode=020000`
  under the **old**, wrong offsets - the same coincidence the 86Box S3 ROM
  produced. Option ROMs carry no `Rom.` dump, so what its bytes actually say
  cannot be checked from the file. Three real display ROMs are conformant and
  two other ROMs look reversed; the corrected code reports the bytes rather
  than the hoped-for value. See
  `docs\issues\2026-09-28-survey-pcir-class-and-cpuid-model.md`.
- Mode tallies the new counters would print: B490 15 described of 39, X61 12
  of 36, E460 and P14s 15 of 15. B490 and X61 both describe `0160`-`0162` as
  768x480 and leave the rest of the Intel OEM block undescribed.
- None of the four has an EDID extension block, so `Block1Status` does not
  appear on any of them.
