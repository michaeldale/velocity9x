# Two survey reports arrived missing the same section headers and the Result

Date: 2026-08-28
Status: **open, unexplained** - the cause is not established and no fix is
proposed here

Both surveys returned from the Acer NAV50 by CentaurHauls are incomplete, and
incomplete in the same way. One was taken from a DOS box under Windows Me, the
other from real DOS under Windows 98 SE, hours apart. Identical damage across
two independent runs is not a transfer accident.

Files: `claude\personal\v9x-centaurhauls-acer\source-data\`
(`V9xsurv Acer NAV50 WinMe.ini`, `V9xsurv nav50 98se.ini`). Both from survey
build `8774305`, run as `V9XSURV /rom`.

## What is missing

**The `[Result]` section, in both.** `vga_survey_dos.c` writes it
unconditionally as the last thing before `fclose`, `Complete=yes` last of all,
precisely so a cut-off report is identifiable. `parse-vga-survey.ps1` flags it:
`truncated report (no Result/Complete marker)`. Neither run reached the end.

**Three section headers and their fixed keys, in both.** These are absent:

| Section | What went with it |
|---|---|
| `[VBEModes]` | `Status`, `ModeListPointer`, `Count`, `Truncated`, `Fields` |
| `[EDID]` | `Status` |
| `[VGARegisters]` | `Status` |

What survives underneath each of them is intact and well formed: all 36
`Mode.NN` rows, all eight `Block0.NN` EDID lines, and the full `Seq.`/`Crtc.`/
`Gdc.`/`Atc.` register dump.

`[Report]`, `[System]`, `[Platform]`, `[BiosData]`, `[PciBios]`,
`[PciInventory]`, `[PciDevice.0]`, `[PciDevice.1]`, `[VideoBios]`,
`[OptionRom.0]`, `[OptionRomScan]` and `[VBE]` are all present with their keys,
so it is not that section headers in general are being lost.

## What it is not

Not a truncated tail: the missing headers sit mid-file with valid content
after them.

Not NUL corruption or a mangled encoding: both files are clean CRLF text with
no NUL bytes.

Not the vendor probe being declined. The tester declined tier 2, but `[Result]`
is written whichever way that question is answered, so a decline does not
explain its absence.

Not divergent damage from two transfers: the two files' mode rows and EDID
blocks `diff` byte-identical, so whatever removed the surrounding lines removed
the same ones twice.

## Why it matters, and how much

The findings in `docs\decisions\2026-08-28-pineview-vbe-mode-list.md` rest on
the `Mode.NN` rows and the EDID block, both of which are self-describing and
survived. Those stand.

What is lost is the ability to say a report is complete. `Count` and
`Truncated` from `[VBEModes]` would confirm the mode list was fully walked;
`[EDID] Status` would say how the block was obtained; `[Result]` would carry
`IdentifiedBy` and `DisplayDeviceCount`. Their absence also means the parser
declines to summarise the mode table at all - it reports `0 modes` for a
report that plainly contains 36.

## What to try next

Ask for one report copied straight off the machine with no intermediate step -
`V9XSURV /rom /out:A:\V9XSURV.INI` to removable media, sent as an attachment
rather than pasted. If that copy is complete, the tool is fine and the loss is
in the reporter's transfer path. If it shows the same gaps, the tool is
dropping writes on this machine and the next question is what those particular
`fprintf` calls have in common.

Worth checking either way: whether `parse-vga-survey.ps1` should read the
`Mode.NN` rows on their own rather than requiring `[VBEModes] Count`, so a
damaged report still yields its mode table.
