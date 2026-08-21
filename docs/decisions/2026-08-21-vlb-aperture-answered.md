# The VLB linear aperture works, at both addresses, and the driver already does enough

Date: 2026-08-21
Status: **answered. The aperture works at both candidate addresses, including
the one the BIOS chose, so the driver needs less change than this note first
predicted. Runs 2-4 were invalidated by a bug in the probe; runs 6 and 7 replace
them.**

Follows `docs/decisions/2026-08-20-vlb-survey-schema2.md`, which took the survey
as far as a read-only tool could and left one question: does a 486 with a VESA
Local Bus card decode anything at the address the card's linear window claims?
Both candidate addresses were above the 16 MiB ceiling of `INT 15h AH=87h`, so
the survey could not reach either.

`tools/diag/vlb_aperture_dos.c` reaches them through unreal mode. Seven runs,
committed beside this file as `...-vlb-aperture-486-trio64-ap[1-7].ini`. Runs 1
and 5 to 7 carry the findings; 2 to 4 are kept because they are what exposed the
bug described below, and because a report that measured nothing is worth being
able to recognise later.

## The answer

**Yes, at both addresses.** Runs 6 and 7, with the probe's lock bug fixed, found
the marker at each of them by the strongest test the tool has - the banked
cross-check, where the marker goes into video memory through the A0000h window
and comes back out at the linear base, proving the two are the same memory.

| Run | Base | How it got there | Result |
|---|---|---|---|
| 1 | `7F000000` | as the BIOS left it, linear addressing **off** | all `FF` |
| 6 | `7F000000` | CR58[4] set by us | **marker found** |
| 7 | `04000000` | CR59/CR5A written by us, then CR58[4] | **marker found** |
| 5 | `04000000` | S3VBE placed it, linear mode | round trip ok |

Runs 1 and 6 are the same address with one bit changed between them, and they
give opposite answers. That is as clean a control as this hardware affords: `FF`
with the window disabled, the marker with it enabled.

`Cr58ReadBackHonoured=yes` in both runs 6 and 7. **CR58 does read back what is
written to it on this card.** The `no` that runs 3 and 4 reported was entirely
the locked bank, as suspected, and it is now contradicted twice.

### The earlier evidence, from run 5

With S3VBE 3.18 resident and a linear framebuffer mode requested
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

`0x04000000` needs address line A26 and `0x7F000000` needs A31, and this note
originally reasoned from that to a prediction that the second would fail. **It
does not, and the reasoning was wrong.**

The error was applying a PCI-shaped model to a bus that is not PCI. On PCI the
host bridge has to be persuaded to route a range to the card, so a high address
is a question about the bridge. VESA Local Bus is the 486's own local bus brought
out to a slot: the card sees A31-A2 directly from the CPU and decodes them
itself. Nothing has to route anything. All the chipset has to do is *not* claim
the same range, which at either of these addresses it does not.

So S3VBE's relocation to 64 MiB is its own preference, not a necessity - and why
its author replaced a fixed address with a computed one in 1996 remains unknown.
Some board somewhere presumably needed it.

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

Runs 6 and 7 are the repeat, with the fix in place, and they are where the
answer above comes from. Nothing in runs 2 to 4 speaks to any address.

An S3VBE-shaped consolation: in run 5 the lock was still open afterwards
(`CR38` read `48h`), because S3VBE unlocks and does not re-lock. That is why run
5 escaped the bug entirely, and it is the difference that made the pattern
obvious once all five reports were laid side by side.

## What this means for the driver: less than expected

`v9x_s3_read_aperture` returns whatever CR59/CR5A hold, and
`v9x_s3_enable_linear_aperture` sets the size and enable bits in CR58 and leaves
the base alone. This note previously predicted that would have to change - that a
non-PCI machine would need the driver to *place* the window rather than accept
it. **Run 6 shows it does not.** The base the BIOS leaves behind works, once
CR58[4] is set, which is exactly and only what the driver already does.

So on this card the existing aperture path is sufficient as written. Three
specific worries are now closed:

- **The read-back guard passes.** `Cr58ReadBackHonoured=yes`. The guard in
  `v9x_s3_enable_linear_aperture` that requires the `13h` bits to have stuck will
  not reject this card.
- **The base passes the range check.** `0x7F000000` is inside
  `v9x_s3_read_aperture`'s accepted window of `0x01000000` to `0xffc00000`.
- **The lock cannot bite.** The card's ROM closes the extended bank behind every
  mode set - measured three times now, `BankReadableBeforeReunlock=no` in runs 6
  and 7 - but the driver unlocks before each extended access in both functions,
  so it never reads a locked bank. This broke the probe precisely because the
  probe was the thing that did not do what the driver already does.

The ability to relocate is worth keeping in mind rather than building: run 7
proves CR59/CR5A accept a new base and the window follows it, so if a board
turns up where something else claims the BIOS's choice, the fix exists. It is not
needed here.

## Still outstanding

1. ~~Re-run 2, 3 and 4.~~ **Done - runs 6 and 7.** `0x7F000000` decodes.
2. **Look at the screen during a `/pattern` run.** 32 coloured pixels in the
   top-left corner is the last independent confirmation available and it is free.
   Not needed for the verdict - the banked cross-check already establishes the
   aperture is the framebuffer - but it would close the loop with an observation
   that depends on none of this tool's machinery.
3. **A build.** The survey and the probe have said everything they can. What is
   untested is the driver itself on this card: whether its DPMI mapping reaches
   the window from protected mode, whether the 8514/A blitter behaves the same on
   the VLB part, and the install path - a VLB card has no PCI hardware ID, so the
   generated INF has nothing for SetupX to bind and needs a different binding
   entirely. None of those are survey questions.
4. **The schema-2 survey regression on the 86Box PCI targets.** Still outstanding
   from the previous note, still needs no 486, and now the only host-side item
   left.
