# Pineview (GMA 3150): the Gen3 engine on a G33-class part, then the panel

Status: **Phases 0 and 1 done. Phase 1 is coded and gated but has not run
on Pineview.** TOMMO33 is a
remote-help Lenovo with an Atom N455 (`M-BWCR7F`). Results, in
`docs\decisions\2026-10-20-pineview-tommo33-tier0.md`:

- the `vbe` tier-0 driver reaches `enable-ok`;
- config space matches the Acer's;
- VBE reports `0x7F0000`, so item 1 below blocks here too;
- this BIOS describes 1024x600 at 8, 16 and 32 bpp.

The earlier evidence is a third-party Acer
Aspire One NAV50 (`docs\decisions\2026-08-28-pineview-vbe-mode-list.md`,
`docs\issues\2026-08-29-pineview-driver-will-not-load-after-survey.md`).

GMA 3150 is the IGD on the Atom N4xx/N5xx (`8086:A011`, mobile) and
D4xx/D5xx (`8086:A001`, desktop). Linux and Mesa treat it as a G33: Gen3
with a 36-bit GTT. Its 3D engine is the GMA 950's, so the work is mostly
identity, layout and display policy, plus one new thing, the panel's native
mode.

## Documentation

`C:\everything\claude\personal\intel driver research\pineview-research-notes.md`
covers what exists. In short: no Pineview PRM was ever published. The Atom
N400/N500 datasheet Vol 2 (322848-003) documents configuration space: GGC,
GBSM, and Device 2's MMADR, GMADR, GTTADR, MGGC and GDRST. It has no graphics
MMIO. Everything about display registers comes from Linux i915 v6.6 in
`pineview-open-source-references\linux-6.6\`. Mesa's `i915` and `i915g` in
`gen3-source-references\` cover the 3D engine. Linux is a reference for what
i915 does, not a measurement of the silicon.

## What the Acer survey already answers

From `claude\personal\v9x-centaurhauls-acer\source-data\V9xsurv nav50 98se.ini`,
real DOS:

| Fact | Acer (A011) | Netbook (27AE) |
|---|---|---|
| BAR0 MMIO | `98180000` (512 KB) | `FE980000` (DOS) |
| BAR2 GMADR | `80000008`, prefetchable | `D0000000` |
| BAR3 GTTADR | `98000000` | `FE940000` (DOS) |
| GGC (52h) | `0130`: GMS 3 = 8 MiB, **GGMS 1 = 1 MiB GTT** | `0030`: GGMS 0 |
| BSM (5Ch) | `7F800000` | `7F800000` |
| VBE TotalMemory | **`0x7F0000`** (8,323,072) | `0x7B0000` |
| Second function | `A012`, class `038000` | `27A6` |
| VBIOS image | 55,808 bytes in `[VideoBios]` | - |

The survey does not reach any MMIO register.

## What carries over from the 945

- The 3D engine and its packets. Gallium sets `is_i945` for both Pineview
  ids (`i915_screen.c`), so the 945's mip layout applies, and the descriptor
  takes no `V9X_DD_ENGINE_CAP_I9XX_MIP_STACKED`.
- The ring, MI commands and the blitter.
- The plane base register for flips. On every display before Gen4, i915 writes
  `DSPADDR`, and that includes G33.
- `PIPEDSL` for the scanline.
- The BAR arrangement: MMIO in BAR0, aperture in BAR2, GTT in BAR3.
- A GMS of 3 decodes as 8 MiB in `intel_gtt16.c`'s table.
- The PTE format below 4 GB. G33's PTE carries address bits 35:32 in bits 7:4
  (`i965_write_entry`), but an Atom tops out at 2 GB, so those bits are zero.

## What does not, and where it bites

1. **The ring reserve is fixed.** `V9X_I9XX_RUNTIME_RING_START` (`0x6B0000`,
   generated into `i9xx3d.inc`) is the reserve for a VBE size of `0x7B0000`.
   The Acer reports `0x7F0000`, so `v9x_i9xx_sandbox_calculate` puts the
   reserve at `0x6F0000`. The descriptor in `gma950_hw16.c` would refuse
   with `EngineStatus=ring-layout-mismatch`. That means no ring, and so no
   Direct3D, blits or ring flips. This is the first blocker, and it is
   host-testable. The mini-VDD must take the ring offset at runtime from the
   same calculation that ends the DirectDraw heap, not from a build constant.
   The armed Phase 4 and 5 diagnostics
   (`intel_exec16.c`, `intel_3d16.c`) keep their fixed netbook layout. They
   refuse anywhere but `27AE` rev 3 anyway (`i9xx_arm.c`).
2. **The watermark write.** `i9xx_scanout.c` writes `FW_BLC` (`0x20D8`)
   when its arithmetic disagrees with the register. On Pineview, i915 never
   writes `FW_BLC`. `pnv_update_wm` programs only the self-refresh fields of
   `DSPFW1` and `DSPFW3` (display, cursor, and HPLL-off), from a CxSR latency
   table keyed on DDR type, FSB and memory clock. When no table entry matches,
   it disables CxSR. Nothing says what `0x20D8` is on this part. Pineview has
   to withhold the write. Which flag carries that through the descriptor is
   decided in Phase 1.
3. **Identity.** Several places need Pineview added:
   - the `intel_backend.c` allowlist
   - an `intel-gma` chip entry in `family.psd1`
   - an hw16 device beside `v9x_gma900_device`

   `A012` must not be claimed. The tester saw two Device Manager entries, and
   removing the second one made the Settings tab reachable. That the second
   entry was function 1 is inferred, not recorded.
4. **GGMS is non-zero.** The 945 decode has only ever seen GGMS 0, and the
   GTT here lives in its own 1 MiB of stolen memory below BSM. Both
   `V9xPciReadIntelGttConfig` and the GTT inventory must accept `0130`.
   Linux sizes a G33 GTT from `PGTBL_CTL`, not from the aperture
   (`intel_gtt_total_entries`), and the inventory's backed-prefix scan has to
   agree with that.
5. **Chipset flush.** G33 locates its flush page through `I965_IFPADDR`
   (host bridge `70h`), not the 945's `60h`. The tree only reads `60h` raw
   for a plan file. Phase 1 confirms that no runtime path uses either.
6. **Cube maps.** A Mesa comment in `i915_texstate.c` records that Pineview
   samples cube faces wrongly unless their layout is aligned to 4. This only
   matters if the HAL ever exposes cube maps.
7. **DPLL.** Pineview has one M divider and a one-hot ring-counter N
   (`pnv_calc_dpll_params`). The driver programs no DPLL on any Intel part, so
   this only matters for Phase 4.

## Phase 0: a machine, Windows 98 SE, tier-0

Install Windows 98 SE and the v9x agent on a Pineview machine, then run the
`vbe` family. Record:

- which VBE modes the BIOS describes;
- the desktop at 800x600 at 8, 16 and 32 bpp;
- the Device Manager entries for both functions.

Run V9XSURV from real DOS too. If the VBE size differs from the Acer's, item 1
covers it anyway, but the number goes in the record. The DOS-box return
(`docs\issues\2026-08-28-dos-box-entry-hang-gma950.md`) is a known Gen3 fault.
Do not open a full-screen DOS box during these runs.

## Phase 1: desk work, host-tested, before the first claim (done, untested on hardware)

- **Item 1, done.** `RING_OPEN` takes the reserve offset in ECX. The
  descriptor passes `v9x_gma950_ring_reserve(v9x_vbe_vram_bytes)`, which is
  the same calculation the heap hook uses, so the ring, the heap boundary
  and the HAL's status page move together. The mini-VDD refuses a reserve
  that is unaligned, reaches past 64 MiB, or differs from an
  already-mapped window. `V9X_I9XX_RUNTIME_RING_START` is gone. A check-tree
  rule now refuses the generated `V9X_I9XX_RING_START` inside the runtime
  `Ring_Open`, and fails on the old code. The armed Phase 4 and 5 paths
  keep their netbook constants.
  - The descriptor now refuses with `vbe-size-unread` while the VBE size is
    unknown, where it used to open the ring at `0x6B0000`. The first calls
    run before the windows are mapped, so the netbook should not see a
    difference. Not yet checked on the netbook.
  - The host test pins `0x7F0000` to a reserve at `0x6F0000` and a status
    page at `0x700000`. The arithmetic was already right; the defect was
    the constant, which no host test reaches.
- **Item 2, done.** `V9X_DD_ENGINE_CAP_I9XX_NO_FW_BLC` (`0x20000`) is
  stamped by the Pineview descriptor only. `i9xx_scanout.c` still reads and
  logs `FW_BLC` and `DSPARB` but does not write `FW_BLC` when the bit is set.
- **Item 4, no change needed.** `intel_gtt16.c` decodes GMS from bits 6:4
  (3 → 8 MiB). Nothing on the runtime path reads GGMS, and
  `V9xPciReadIntelGttConfig` does not validate GGC.
- **The chip entry, done.** `gma3150-pineview` claims `8086:A011`:
  `V9X_PCI_DEVICE_GMA3150_PNV`, `v9x_gma3150_device`, the backend allowlist
  and the regenerated registry. `A001` waits for a desktop survey. `A012`
  is not claimed.

Gates: `check-tree.ps1`, `build-host.ps1`, `run-checks.ps1`.

## Phase 2: read-only MMIO from Windows

The `intel-gen3-mmio-fingerprint-v1` capture on `A011`, plus these
Pineview-specific registers:

- `PGTBL_CTL` (the GTT size)
- `DSPFW1` to `DSPFW3`, including the self-refresh enable bit
- `FW_BLC` and `DSPARB`, read only
- the live pipe and plane
- `PIPEDSL`
- the GTT head against stolen memory
- the ring registers idle

Record it as a decision doc, including the hypotheses it kills. CxSR is the
question to answer: whether the BIOS leaves self-refresh on, and what it
programmed for the VBE mode.

## Phase 3: the claim

Run the claim with the engine on by default; `IntelRuntime3D=0` stays the off
switch. Then, in order:

1. DirectDraw fills, copies and flips.
2. The V9XDDP and V9XGLP probes at 16 and 32 bpp, with Z16 and Z24.
3. A Quake 2 `timerefresh` on `demo1`, recorded beside the netbook's.

If the panel shows underrun bands under load, that is item 2 coming due. The
fix is Pineview's own self-refresh watermarks, not the 945's.

## Phase 4: the panel's native mode, its own plan

The Acer's BIOS lists 36 modes and describes six, none at 1024x600. Velocity9x
sets no Intel mode itself, so this is new ground. Desk work comes first, from
the ROM image already in the survey:

- find the VBT, its LFP DTD (the panel timing), and the mode table behind
  `0x0160`-`0x0171`;
- find why `4F01h` returns an empty ModeInfoBlock for those modes.

That decides between two routes:

- **Patch the shadowed mode table** the way 915resolution does. The VBIOS then
  keeps the mode set.
- **Program the mode ourselves:** pipe timings, the Pineview DPLL,
  `PFIT`, and LVDS.

The second route is a module boundary change and needs agreement before any
code.

## Test hardware

- **TOMMO33** (`M-BWCR7F`): Tom Rossell's Lenovo, `8086:A011` SUBSYS
  `17AA:3908`, Atom N455, `CMO1018` 1024x600 panel, Windows 98 SE. It ran
  VBEMP and has run the Velocity9x `vbe` family since 2026-10-20. Its BIOS
  describes the panel mode. It is lent over remote help, so DOS work needs
  Tom at the machine. None has been needed so far.
- **The Acer NAV50:** a third-party report only, not reachable remotely.

## Order

1. Config survey from Windows, then `vbe` tier-0, whose boot report answers
   the VBE questions without DOS. Done on TOMMO33, 2026-10-20.
2. Phase 1 desk work and gates.
3. Read-only MMIO capture; decision doc.
4. Claim, probes, games.
5. Native-mode desk work from the ROM image; a plan for Phase 4.
