# Five sibling ids claimed from the Linux tables, without a survey

Date: 2026-11-20

Status: **claimed, not surveyed, not run.** Michael chose to claim these
on the Linux tables alone (2026-11-20) rather than wait for a survey from
a card. Every other claim in the tree came from a survey of the part
itself, as `0046` and `0152` did; these five did not.

The candidates came from the refreshed
[PCI ID audit](../reference/2026-11-13-pci-device-id-audit.md): same
silicon as a bound chip, on another bus or as a second id of one product,
with no ASIC-specific constant in our path that the id could change.

| Id | Alias of | Upstream evidence | What is assumed |
|---|---|---|---|
| `102B:051F` Millennium II AGP | `051B` | Linux matroxfb binds `MIL_2_AGP` as `MGA_2164` with the PCI board's tables | The 2164W's BAR fallback order (86Box only; silicon takes the prefetch bit), its 32-bit Z and texture engine |
| `102B:051E` Mystique AGP | `051A` | matroxfb binds `MYS_AGP` as `MGA_1064` | The Mystique's no-fallback BAR rule and Z16-only texture engine |
| `1002:5961`, `1002:5962` Radeon 9200 | `5960` | `drm_pciids.h`: `CHIP_RV280` with the 5960's flags; RV280 attaches to AGP and PCI natively, no bridge | BAR0 the frame buffer, BAR2 the 64 KiB registers, the VBE's mode list carrying the family's seven rows |
| `8086:0122` HD 3000 | `0112` | i915 `INTEL_SNB_D_GT2_IDS` lists 0112 and 0122 together; Mesa's `snb_gt2` covers both | The 0112's BARs, GGC, stolen-reserved register, packed pitch and Gen6 forcewake (`HSW_ENGINE_OPEN` chip 2) |

## What changed

- Each id has its own `V9X_HW16_DEVICE` (the PCI scan matches one per id
  and V9XHW.INI names the part), the backend's id list, the registry and
  the INF.
- Matrox: `millennium_hw16.c`'s three exact-id tests (the BAR fallback,
  the 2164W's D3D and 32-bit depth caps, the Mystique's D3D cap) take the
  AGP ids with their PCI chips.
- Sandy Bridge: `v9x_intel_is_gen7` (the Haswell path's hooks) and
  `v9x_ivb_fill_engine`'s chip choice (Gen6 forcewake, `CAP_GEN6_SNB`)
  take `0122` with `0112`.
- The ati family's `_TEXT`: see
  [2026-11-20-oland-6611-survey.md](2026-11-20-oland-6611-survey.md),
  which made the same change for its own row.

## What would show these wrong

A card that refuses at Enable (`Stage=fail-*` in V9XBOOT.INI), an engine
hook that maps nothing (`EngineCaps=00000000` in V9XHW.INI), or a VBE
that lacks the family's baseline modes. The cheapest evidence is a
V9XSURV `/rom` report from DOS on any of these cards, which records the
BARs and the VBE mode list without loading the driver.
