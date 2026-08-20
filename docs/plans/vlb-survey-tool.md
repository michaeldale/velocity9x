# Survey tool v2: a 486 with an S3 Trio VLB

## Context

We want VLB support. The card in hand is an S3 Trio on VESA Local Bus in a 486,
and the first step is a survey run from that machine. **The survey tool we ship
today would come back nearly empty from it**, because almost everything it
collects is reached through PCI.

That is not a small gap. VLB removes the one thing every existing family relies
on to know what card it is looking at, so the survey has to answer questions the
PCI targets never had to ask.

## What breaks on VLB, and why the survey has to change

Established by reading the tree, not assumed:

1. **There is no PCI configuration space.** `run_tier2`
   (`tools/diag/vga_survey_dos.c:1227-1234`) returns immediately with
   `no-pci-display-device-identified` when the PCI walk found nothing, so the S3
   register probe at `:1085` — the thing we actually want — never runs. The
   `[Pci.*]` sections are empty too. A tester on this machine sends back a
   report consisting of the VGA register file, the ROM image and VBE, and
   nothing that identifies the chip.

2. **The driver cannot identify the card.** Identification is a PCI scan:
   `v9x_pci_vendor` / `v9x_pci_device` / `v9x_pci_match`
   (`src/display16/ddi.c:95-104`), matched by `V9xFindPciDevice`. The `s3`
   family sets its PCI-miss-tolerated flag to `0` (`src/chipsets/s3/s3_hw16.c`,
   last field: *"The card must be one of ours"*), so a VLB Trio is refused at
   stage 1 even though every register the driver then wants is present. Only the
   `vbe` family tolerates a miss — and see (4).

3. **Installation has no hardware ID to match.** The INF advertises
   `PCI\VEN_5333&DEV_8811` and `&DEV_8A01`
   (`packaging/families/s3/family.psd1`). SetupX cannot bind that to a device
   which is not on a bus it enumerates.

4. **Tier-0 is unavailable, so this has to be a native path.** Tier-0 learns the
   framebuffer address from VBE 4F01h `PhysBasePtr`, which requires VBE 2.0.
   Every S3 BIOS measured so far reports **VBE 1.2 with no linear framebuffer on
   any mode** (`docs/decisions/2026-08-20-vbe-mode-inventory.md`), and a VLB
   board's BIOS will be older, not newer. So the aperture must come from the
   chip — which is what the `s3` family's CR59/CR5A hook already does — and the
   survey has to tell us whether that window exists and where it can live.

**The central unknown is the linear aperture.** On the PCI parts it is a BAR the
host bridge routes for us. On VLB the linear address window is programmed into
CR58/CR59/CR5A and the 486 chipset has to route it — on a machine that may have
less RAM than the window is wide, and may not decode high addresses at all.
Everything else here is legwork; this is the question that decides whether VLB
support is possible at all.

## Scope

A new schema revision of the existing tool, not a new tool.
`tools/diag/vga_survey_dos.c` keeps its shape — real-mode DOS, one INI report,
almost no interpretation, blobs decoded host-side by
`scripts/parse-vga-survey.ps1` — and keeps both safety properties: the two-tier
split, and the build-time source gate in `scripts/build-vga-survey.ps1:36-48`
that refuses to compile a mode set or a PCI write.

Bump `V9X_SURVEY_SCHEMA` from `"1"` to `"2"`. The parser accepts both.

## Changes

### 1. Identify the chipset without PCI

The core fix. Today the vendor comes from PCI; on VLB it has to come from the
chip.

- New `[Bus]` section in tier 1: whether a PCI BIOS answered at all
  (`INT 1Ah AX=B101h`), how many display devices it found, and therefore whether
  this is a PCI machine. A 486 VLB box reports no PCI BIOS, and that fact alone
  is worth having in the report.
- `run_tier2` gains a fallback: when the PCI walk found nothing, attempt **S3
  identification by register**. Unlock with the documented CR38/CR39 keys
  (`0x48`, `0xA5` — the same pair `tier2_s3` and the driver already use), read
  CR2D/CR2E, restore, and accept only if the pair is a known S3 device id.
  Anything else reports `unidentified-non-pci-display` and stops.
- Guard rails, because this now writes registers on a card we have not yet
  identified: it stays **tier 2 (opt-in)**, runs only after the tier-1 report is
  closed on disk, restores CR38/CR39 before reading anything else, and restores
  the CRTC index last. That is the existing discipline applied one step earlier
  in the sequence.

The real risk, stated plainly: CR38/CR39 on a non-S3 card mean something else.
That is mitigated by being opt-in and by writing only that documented pair
before checking the id — but it is not zero. The tester is telling us they have
an S3, and the tool should say in its prompt that this is what it is trusting.

### 2. Dump the whole extended register file, not a hand-picked list

`tier2_s3` reads eleven CRTC and five sequencer registers, chosen for the PCI
parts. We do not yet know which registers differ on VLB, and finding out must
not cost another round trip to the tester.

- Dump **CR30-CR6F** and **SR00-SR1F** entire, as two hex blobs, plus the
  standard CR00-CR2F the tool already captures. All read-only behind the same
  unlock.
- Keep the existing named single-register lines so current parser output does
  not regress.
- Add the DAC id (the RS2 read sequence at 0x3C6) and the 0x3C2 input-status
  bits.

This follows the tool's own philosophy — interpret host-side, so a decoding
mistake is a script fix rather than a re-ship. The CR5D/CR5E extended-overflow
bits and the CR40/CR53/CR55 group are the likely VLB differences, and none of
them are in the current list.

### 3. Interrogate the linear address window

The question the run exists to answer. From the state the BIOS left:

- CR58 (window size and enable), CR59/CR5A (window base) — read today, but now
  with the size field decoded host-side rather than the base alone.
- CR53 (MMIO / new-mode enables) and CR40 (engine enable) — already read.
- Whether the window is enabled at boot at all, and where the BIOS put it.

Then the one genuinely intrusive addition:

- **An opt-in, read-only aperture decode test** (`/aperture`, separate from
  `/tier2`). Read a few dwords from the window's physical base via
  `INT 15h AH=87h` extended-memory block move — a BIOS service, read-only, and
  reachable from real mode without unreal mode or DPMI. Report the bytes.
  All-`FF` or all-`00` means nothing decodes there; VGA-looking contents mean
  the window is live.
- **A limitation that must appear in the report itself, not just here.** The
  survey sets no mode, and on these parts the window may only respond once a
  mode has been set with linear addressing enabled. So a negative result is
  suggestive, not conclusive. Settling it needs a mode set, which is out of
  scope for a tool handed to strangers — that belongs in a later bring-up probe
  on our own card.
- `INT 15h AH=87h` only reaches the first 16 MB. If CR59/CR5A point above that,
  report the base and skip the read with a stated reason rather than pretending.

### 4. Platform facts a 486 makes relevant

None of these matter on the PCI targets; all of them constrain where an aperture
can go.

- **CPU class without CPUID.** Early 486s have no CPUID. Detect by the
  documented EFLAGS route — the AC bit distinguishes 386 from 486, the ID bit
  says whether CPUID exists — and only execute CPUID after that. Report the
  class, and the CPUID vendor/family when available.
- **Installed RAM**, via `INT 15h AH=88h` and `AX=E801h`, plus `E820h` if the
  BIOS has it. An aperture must not overlap RAM, and on a 4 MB or 8 MB 486 the
  choice of where it can live is narrow.
- **A20 state**, and whether HIMEM/EMM386 is loaded — both affect what a driver
  can later map.
- **Video BIOS ROM strings and date** — already collected, and worth keeping,
  because on a VLB board the ROM string is what identifies the exact card and
  its default window placement.
- **VBE version and the full mode list with per-mode attributes** — already
  collected. We expect VBE 1.2 and no linear-framebuffer attribute anywhere;
  recording that definitively is what closes off tier-0 in writing rather than
  by inference.

### 5. Host-side parser

`scripts/parse-vga-survey.ps1` gains schema-2 awareness, the `[Bus]` section,
the register-blob decoders (CR58 window size, CR36 memory size, CR2D/CR2E to a
chip name), the aperture verdict and the platform section. Schema-1 reports must
still parse.

## What this run does *not* decide

Worth writing down so the survey is not over-read:

- Whether the driver's DPMI mapping can reach the window on a 486. That needs a
  build, not a survey.
- Whether the 8514/A blitter behaves the same on the VLB part.
- The install path. A VLB card has no PCI hardware ID, so the INF needs a
  different binding — most likely a hand-chosen generic display ID plus Have
  Disk. Worth thinking about before the driver work, but the survey says nothing
  about it.

## Deliverables

1. `tools/diag/vga_survey_dos.c` at schema 2, with the sections above.
2. `scripts/build-vga-survey.ps1`: extend the banned-pattern list to match the
   new surface. It already bans `0x4f02` and PCI writes; add a ban on any `outp`
   to a port outside the documented VGA/S3 index ranges. That gate is the reason
   this tool can be handed to strangers, and it should grow with the tool.
3. `scripts/parse-vga-survey.ps1` updated, schema-1 compatible.
4. A `docs/decisions/` note recording what the 486 run actually returned, shaped
   like `2026-08-20-vbe-mode-inventory.md`, with the raw report committed beside
   it.

## Verification

- Host: `run-checks` green; the build gate rejects a deliberately added
  `0x4f02` and a deliberate PCI write.
- **On PCI hardware we already have:** run schema 2 on the 86Box ViRGE and
  Trio64 and confirm the report is a superset of the schema-1 one — identical
  values for every key that existed before. This is the regression check, and it
  needs no 486.
- **On the 486 VLB target:** tier 1, then tier 2, then `/aperture`, in that
  order, keeping each report. The ordering *is* the safety property — if a later
  stage wedges the machine, the earlier reports survive on disk.
- Read the whole report before drawing conclusions from any single field. Both
  screenshot lessons from 2026-08-20 apply to surveys too: a value that agrees
  with what we expected is not thereby correct.
