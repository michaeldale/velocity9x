# Intel Phase 2 measured on the netbook: the VBIOS maps the whole aperture into stolen memory, identically on two cold boots

Date: 2026-09-12
Status: accepted. **Phase 2 of `docs/plans/hardware-d3d-on-intel-gma950.md` is
complete.** Phase 3 (firmware ownership and the event matrix) is unblocked.
Machine: MICHAEL-NETBOOK, HP Mini 110-1000, 945GSE / GMA 950, `8086:27AE`,
Windows 98 SE direct-booted from the live USB stick, desktop at 1024x576x16.
Package: `build\win98se-intel-gma`, build id `deb566d-dirty` (tree at
`deb566d` plus the fixes committed as `1edb537`).
Sidecar: `2026-09-12-intel-phase2-gtt-inventory-INTELGTT.txt` (identical on
both boots). The 256 KiB `INTELGTT.BIN` dumps are kept untracked under
`build\driver-results\netbook-intel-phase2-20260912-boot1\` and `-boot2\`,
SHA-256 `bbe33d225da40cee6286b491a69fa59a11d75b8efe5a1eb0ed582d3f29cba194`
for both.

## How it was captured

Two cold boots with no mode change between them. After each, the stick was
read on the development host and `INTELGTT.TXT` plus `INTELGTT.BIN` were run
through `scripts\check-intel-gtt-capture.ps1`, which recomputes the hash,
every count, the run map, the reservation and both sample relationships from
the binary rather than trusting the text. Both boots: `Result: PASS`. A
separate Python decode of the binary agreed with every figure below.

A first attempt at this boot ran the Phase 1 package instead (`V9XMODES.INI`
`Build=c049b20-dirty`, no `INTELGTT.*`): Update Driver over an existing
binding kept the old binaries. Copying the four files over
`WINDOWS\SYSTEM` from a DOS prompt fixed it; that boot is otherwise a third
reproduction of the Phase 1 fingerprint, byte-identical to the first.

## What the hardware said

| Field | Value |
|---|---|
| BAR3 (GTTADR) | `FE940000`, 256 KiB aligned |
| GMADR BAR2 | `D0000000` |
| BSM | `7F800000` |
| GGC | `0030`, GMS = 3, 8 MiB stolen |
| VBE usable | `7B0000` = 7.69 MiB = 1968 pages |
| PGTBL_CTL | `7FFC0001`; inferred storage `7FFC0000` matches |
| Entries | 65536, all present, all attribute `1` (valid, uncached) |
| HashA / HashB / HashStream | `4D8707C5` on both boots |

Run map, three runs:

| Run | Entries | Maps | Stride |
|---|---|---|---|
| 0 | 0 to 1982 (1983 pages) | `7F800000` upward, linear | 4 KiB |
| 1 | 1983 to 65534 (63552 pages) | all `7FFBE000` | 0 |
| 2 | 65535 (1 page) | `7FFBF000` | single |

Distinct physical pages: 1984, every one inside the 8 MiB stolen block
`7F800000` to `7FFFFFFF`. The linear region is 7.74 MiB, 15 pages more than
the VBE-reported usable size. Above it the stolen block holds the page at
`7FFBF000`, then the 256 KiB GTT at `7FFC0000`.

Reservation and samples: the proposed 128 KiB at aperture offset `790000`
(entries 1936 to 1967) is inside the linear run and backed by distinct pages
`7FF90000` upward. The two GMADR reads at offsets 0 and `790000` went
through PTEs `7F800001` and `7FF90001` and returned zero. Those were the only
GMADR reads in this phase.

## What this confirms

- The GTT is where Phase 1 said it was, and its contents are stable across
  cold boots to the byte. Done-criterion met.
- The framebuffer and the proposed reservation are backed by stable,
  explainable PTEs: linear from BSM, one page each.
- Every aperture page is backed. The VBIOS fills the remainder of the
  256 MiB aperture with the last linear page and points the final entry at
  the one page below the GTT. There is no unbacked GMADR address at boot.

## Hypotheses this kills or leaves open

- Killed: the audit's concern that GMADR access beyond BIOS GTT coverage
  touches unbacked memory. On this VBIOS at boot there is no such range.
  Reads anywhere in the aperture land in stolen memory. This does not license
  writes, which remain Phase 4's question.
- Killed: "the VBIOS might leave PTEs above the framebuffer invalid or with
  cache bits set". All 65536 are valid and uncached.
- Open: what `7FFBE000` and `7FFBF000` hold. `7FFBE000` is the last page of
  the linear framebuffer region and is aliased 63552 times; `7FFBF000` sits
  alone between it and the GTT. Phase 0 predicted a 64 KiB VBIOS scratch
  region here; the map shows two pages, not sixteen. Recorded, not
  interpreted.
- Open: whether the map changes after a VBE mode switch, disable/enable or
  DPMS. That is Phase 3's matrix.

## Gates

`run-checks.ps1` green on the tree that built the package (check-tree, both
validator self-tests, host tests, all family packages). The physical capture
passes the validator on both boots.
