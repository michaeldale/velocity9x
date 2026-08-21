# The VLB linear aperture works, at 64 MiB, and the BIOS's own address is still untested

Date: 2026-08-21
Status: **answered in the affirmative. VLB linear framebuffer support is
possible. Three of the five runs were invalidated by a bug in the probe and say
nothing.**

Follows `docs/decisions/2026-08-20-vlb-survey-schema2.md`, which took the survey
as far as a read-only tool could and left one question: does a 486 with a VESA
Local Bus card decode anything at the address the card's linear window claims?
Both candidate addresses were above the 16 MiB ceiling of `INT 15h AH=87h`, so
the survey could not reach either.

`tools/diag/vlb_aperture_dos.c` reaches them through unreal mode. Five runs,
committed beside this file as `...-vlb-aperture-486-trio64-ap[1-5].ini`.

## The answer

**Yes.** With S3VBE 3.18 resident and a linear framebuffer mode requested
(`4101h` = mode `101h` with bit 14 set), on the 486 VLB Trio64:

| Key | Value |
|---|---|
| `ModeSetStatus` | `004F` |
| `ModeAttributes` | `009B` - bit 7, linear framebuffer available |
| `VbeReportedPhysBase` | `04000000` |
| `Probe.0 Linear` | `04000000`, read back from CR59/CR5A |
| `DataBefore` | 32 bytes of `00` |
| `DataAfterWrite` | the 32-byte marker, exactly |
| `Status` | `round-trip-ok` |

A 32-byte marker written through the flat segment at `0x04000000` read back
unchanged. `TopOfRam` is `0x02000000` - 32 MiB, measured on a clean boot with no
memory manager intercepting `AH=88h` - so the address is at twice the installed
RAM and cannot be RAM.

Three independent things agree that this is video memory and not some other
decoder:

- **A dead address returns `FF`, and this returned `00` before we wrote.** S3VBE
  clears the screen on a mode set, so a freshly-cleared framebuffer is exactly
  what all-zeroes at the base means. Nothing decoding would have given `FF`, as
  it did at `0x7F000000` in run 1.
- **The write stuck.** Nothing swallows a write and hands back the same 32 bytes
  except memory.
- **S3VBE reached the same conclusion independently.** It advertised
  `PhysBasePtr = 0x04000000` through `4F01h`, and it had programmed CR59/CR5A to
  match - the probe read `04`/`00` back out of the card. Its "automatic
  configuration of linear frame buffer location", added in version 3.10, picks
  an address this board decodes.

So the question that decided whether VLB support is possible at all is answered,
and the answer is that it is.

**What has not been shown** is that the marker was visible on screen. Offset 0 of
a 640x480x8 linear framebuffer is the top-left corner, so a correct result should
have painted 32 coloured pixels there. Nobody was watching for them. That is the
one cheap confirmation still outstanding and it costs one re-run.

## S3VBE moves the window, and where to

Outstanding question 3 from the previous note, answered as a side effect. The
card's BIOS leaves the window at `0x7F000000` with linear addressing disabled -
runs 1 through 4 all read `CR58`/`CR59`/`CR5A` = `03`/`7F`/`00` before any mode
set. S3VBE reprograms CR59/CR5A to `04`/`00` when it sets a linear mode.

`0x04000000` needs address line A26. `0x7F000000` needs A31. On a 486 VLB board
the first is ordinary and the second is not, which is almost certainly why the
author of S3VBE stopped using a fixed address in 1996.

## Three runs measured nothing, because of a bug in the probe

Runs 2, 3 and 4 all report `Probe.0 Linear=42420000` and `marker-absent`. That
address exists nowhere. It was assembled out of a locked register bank.

**The card's ROM BIOS closes the extended register lock behind a mode set.** The
probe unlocked CR38/CR39, read the window registers correctly, then called
`4F02h` - and every subsequent read of the bank returned the constant `42h`. So
`CR58`, `CR59` and `CR5A` all read `42`, the base computed from them was
`0x42420000`, and the `CR58` write meant to enable linear addressing went through
a closed lock and did nothing at all.

The report then compounded it. `Cr58ReadBackHonoured=no` in runs 3 and 4 reads
like a finding about the card refusing a write. It is not. It is the lock, and
had it gone unnoticed it would have been cited later as "CR58 does not read back
on this part" - a false claim about hardware, arrived at confidently, of exactly
the kind this project keeps writing down warnings about.

What made it visible was the three registers agreeing. `CR58 == CR59 == CR5A` is
not something a real configuration does.

Fixed three ways:

- The lock is re-opened after the mode set, before anything touches the window,
  and the bank's state either side is recorded rather than assumed.
- `extended_bank_readable()` refuses to proceed when the three registers read
  identically, so a locked bank stops the run instead of filling a report with
  confident readings of nothing.
- `Cr58ReadBackHonoured` now reports `unknown-bank-not-readable` rather than
  `no` when the bank is unreadable.

**Runs 2, 3 and 4 need repeating.** Nothing in them speaks to `0x7F000000`.

An S3VBE-shaped consolation: in run 5 the lock was still open afterwards
(`CR38` read `48h`), because S3VBE unlocks and does not re-lock. That is why run
5 escaped the bug entirely, and it is the difference that made the pattern
obvious once all five reports were laid side by side.

## What this means for the driver

`v9x_s3_read_aperture` returns whatever CR59/CR5A hold, and
`v9x_s3_enable_linear_aperture` sets the size and enable bits in CR58 and leaves
the base alone. On the PCI parts that is correct: the host bridge has already
routed a base. On VLB there is no host bridge, and the base the BIOS leaves
behind is one that needs A31.

So the likely change is that the S3 family's aperture hook has to **place** the
window on a non-PCI machine rather than accept it. `0x04000000` is a working
value on this board, arrived at independently by S3VBE and confirmed by
measurement. Whether it is the right general rule - and what it should be derived
from, since S3VBE derives it rather than fixing it - is not established by one
board.

Two things do *not* need changing, and it is worth saying so:

- The driver already unlocks before every extended-register access, in both
  `v9x_s3_read_aperture` and `v9x_s3_enable_linear_aperture`. The lock-after-mode-set
  behaviour that broke the probe cannot break the driver.
- `v9x_s3_enable_linear_aperture`'s read-back guard is not known to be a problem.
  The evidence that suggested it might be was the artefact described above.

## Still outstanding

1. **Re-run 2, 3 and 4 with the fixed probe.** The specific question is whether
   `0x7F000000` - the address the driver would use today - decodes at all. If it
   does not, placing the window is not an optimisation but a requirement.
2. **Look at the screen during a `/pattern /linear` run.** 32 coloured pixels in
   the top-left corner is the last independent confirmation available, and it is
   free.
3. **Whether `0x04000000` is a rule or a coincidence.** One board. S3VBE computes
   its answer; we should understand from what before copying the number.
4. **The schema-2 survey regression on the 86Box PCI targets.** Still outstanding
   from the previous note, still needs no 486.
