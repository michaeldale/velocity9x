# The survey misreported the PCIR class code and every CPUID model

Date: 2026-09-28
Status: **both fixed in source; verified by replaying the recorded bytes from
five archived reports, not by a fresh run on any machine**

Two reports arrived from machines that have not been surveyed before - an Asus
P7H55-M with an NVIDIA NV43 (Quadro FX 550), and a board whose DOS hostname is
"chinaboard-bulldozer" but which is a Clarkdale with the Ironlake IGP, not an
AMD part. Both say `Result Status=PASS` and both are internally consistent
(`ConventionalKB` matches E820 entry 0; the VBE `PhysBase` matches the
prefetchable BAR). Two decoded fields in them are wrong.

Both are stored in `docs\probe\references\` under the date they were received.
Their own `[Report] Date` keys read 2019-12-06 and 2018-08-17: the RTCs on
those DOS boxes are not set, so a survey's internal date cannot be used to
order anything.

## PcirClassCode read two bytes early

Both reports say `PcirClassCode=000000` for a device whose configuration
header says `ClassCode=030000`. The ROM bytes recorded in the same files say
the header was right.

The PCI Data Structure puts the three-byte class code at PCIR+0Dh, low byte
first: programming interface, sub-class, base class. A display ROM therefore
holds `00 00 03`, which both of these do - the NVIDIA PCIR at 0108h and the
Intel one at 0040h. The tool printed the bytes at PCIR+0Dh, +0Ch and +0Bh
instead, which are the low class byte, the structure revision and the high
byte of the structure length. Those last two are zero on every ROM seen, so
the field came out `000000`: wrong, but shaped like an answer.

`PcirImageLength` and `PcirCodeRevision` next to it use the right offsets, so
this was one field, not a misplaced structure base.

## The evidence disputed the obvious sanity check

Replaying both the old and the corrected expression over the `Rom.` dumps of
five archived reports:

| Report | Card | old | corrected |
|---|---|---|---|
| `p7h55m-nv43-vgasurv-2026-09-28.ini` | NVIDIA NV43 | `000000` | `030000` |
| `chinaboard-ironlake-vgasurv-2026-09-28.ini` | Intel Ironlake | `000000` | `030000` |
| `trio3d-a8u4i5-vgasurv-b40-good-2026-09-05.ini` | S3 Trio3D, real card | `000000` | `030000` |
| `2026-08-21-vlb-survey-486-trio64-clean.ini` | S3 Trio64 VLB | `000000` | `000100` |
| `build\driver-results\V9XSURV-9869.INI` | 86Box S3 | `030000` | `000003` |

The three real PCI cards agree with the specification once the offsets are
corrected. The two that do not are the interesting ones, and they are the
reason this is written down rather than committed silently:

- The 86Box guest's ROM holds `03 00 00` at PCIR+0Dh - the class code stored
  the wrong way round, declaring base class 00h. The old, wrong code printed
  `030000` there by coincidence. Had that report been the one anybody checked,
  the bug would have looked like correct behaviour.
- The VLB Trio64's ROM holds `00 01 00`. A VLB card has no configuration space
  for the field to agree with and nothing in the ROM is obliged to be
  meaningful; the corrected code reports the bytes that are there.

So `PcirClassCode` in every archived report predating this commit is wrong,
and a value of `030000` in one of them is not evidence that the ROM says so.
No script or test consumed the field - it is written and never read back - so
nothing else was built on it.

## CpuIdModel ignored the extended model field

`wr_u("CpuIdModel", (leaf[0] >> 4) & 0x0f)` takes only the base model bits.
Both new reports carry signature `00020652` / `00020655` and were decoded as
family 6, **model 5** - a Pentium. The extended model nibble at EAX bits 19:16
makes the real model 25h, Westmere. Every CPU made since about 1999 was
misreported this way; every archived report is affected in the same direction.

The corrected code adds the extended model for base family 6 and Fh and the
extended family for Fh, which is what both vendors' manuals specify. The
486 and Pentium II archived reports (`00000480`, `00000686`) decode
identically before and after, as they must - their extended bits are zero.

`CpuIdSignature` was always written raw and is correct in every report, so
nothing was lost; the decoded fields simply should not have been trusted.

## Why there is no host test

`tools\diag\vga_survey_dos.c` is a single freestanding DOS translation unit
behind a source-audit gate; it shares no module with the host test executable
and giving it one would be a larger change than either defect. The equivalent
here is the replay above, run over bytes the tool itself recorded on five
machines, with the two disagreements chased down rather than averaged away.

`scripts\run-checks.ps1` is green, including the survey safety gate, and
`V9XSURV.EXE` rebuilds at 34,226 bytes.

## Next

Neither fix has run on hardware. The next survey from any machine confirms
both at a glance: `PcirClassCode=030000` on a display card, and a
`CpuIdModel` that is not 5 on anything modern.

## Not fixed here

Three further observations came from the same two reports. Two are now
reported by the tool, in the commit after this one:

- The Ironlake BIOS returns block 0 again when asked for EDID block 1. The
  tool passes DX=1 correctly, so this is the BIOS; `DdcBlockTransferMs=2`
  against 130 on the NVIDIA box suggests it never went to the wire. The
  extension block now lands in the second half of the buffer and is compared
  against block 0, so the report says `Block1Status=duplicate-of-block0`
  rather than leaving every consumer to rediscover it.
- Fifteen of the Intel box's 27 VBE modes are listed with `Attributes=0000`
  and zero geometry. `4F01h` did not fail for them - it returned `0x004F`
  and left the block zeroed, which is the VBE-sanctioned way to say a mode is
  in the table but unavailable in the present configuration. The rows still
  say so byte for byte, and `DescribedCount`, `UndescribedCount` and
  `QueryFailedCount` now close over the list.

Two commits later, the third is measured as far as these reports allow:

- `ChecksumStatus=mismatch` on that Intel VBIOS: the 55AA header says 52224
  bytes, its own PCIR says the image is 65536. The tool sums the header's own
  length, which is the length the system BIOS validates, so that verdict is
  the right one to report - but it does not say whether the ROM is corrupt or
  whether the shorter length is simply the wrong thing to sum. A video BIOS
  that shrinks itself during init produces exactly this.

  The survey now also sums the longer image when the two lengths differ and it
  fits the segment (`PcirImageChecksumStatus`), and reports the residue of
  both. Recomputing the sums host-side from the three archived full-image
  dumps - the 86Box S3, the ViRGE/DX and the VLB Trio64 - gives residue 00 on
  all three, agreeing with the `ChecksumStatus=ok` each of them reported. The
  Ironlake case cannot be settled that way: its dump is `header-only`, and
  `/rom` would not help either, because `/rom` dumps `SizeBytes` and the bytes
  in dispute are the ones past it. That is why the second sum is computed in
  the tool rather than derived from the report.

  Still open until a run on that machine says which sum is clean.
