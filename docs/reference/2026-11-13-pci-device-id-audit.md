# Supported GPU PCI device ID audit

Date: 2026-11-13. Repository HEAD: `bf18b41201a3706716e5789fa3e781e7f789d5a7`.
Research only: no driver, manifest or binding changed. The working tree already
contained unrelated changes, including `gma950_hw16.c`.

## Result

There are **71 distinct PCI bindings across eight packages: 33 chip entries
and 38 aliases**. All 71 are present in the generated INF text and backend
registry. The existing host family-matrix tests also pass, including backend
probe acceptance. There is no declared GPU whose ID was accidentally omitted
from one of those two generated layers.

Coverage of related primary GPU IDs is incomplete, however. The best concrete
model-coverage gaps are **Matrox Millennium II AGP `102B:051F` and Mystique
AGP `102B:051E`**. Both have explicit entries in Linux's
[Matrox board table](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/matrox/matroxfb_base.c),
using the same respective chip classes as the supported PCI models. They
still need surveys and runtime classification changes before we bind them.

Several supported Radeon models have other IDs worth investigating, especially
X1200 `1002:791E`, HD 5570-class `1002:68DA`, and HD 2600 XT AGP
`1002:9586`. Many more IDs belong to different products built from the same
ASIC. Linux's [Radeon family table](https://github.com/torvalds/linux/blob/master/include/drm/drm_pciids.h)
corroborates the ASIC relationship; it does not establish that our measured
configuration works unchanged. The AGP HD 2600 in particular must have its
bridge and BAR behaviour checked.

The full comparison below records **227 absent IDs**: 155 same-family/sibling
survey candidates, three deliberate exclusions, 22 different resource tiers,
30 reserved/development entries, and 17 database-only leads. This is a bounded
comparison of the cited tables, not a claim that 227 extra GPUs work and not
an exhaustive inventory of every OEM identity ever shipped.

## First candidates to investigate

| Supported part | Bound IDs | Absent IDs to investigate | Assessment |
|---|---|---|---|
| Millennium II | `102B:051B` | `102B:051F` | Explicit AGP variant of MGA-2164; strong model-coverage gap. |
| Mystique | `102B:051A` | `102B:051E` | Explicit AGP variants of MGA-1064/1164; check revision and BAR order. |
| Radeon X1200 / RS690 | `1002:791F` | `1002:791E` | Desktop RS690 identity missing; bound identity is named RS690M in the database. |
| Radeon HD 5570 / Redwood | `1002:68D9` | `1002:68DA` | Another HD 5570-class identity, LE rather than PRO; configuration needs checking. |
| Radeon HD 2600 XT / RV630 | `1002:9588` | `1002:9586` | Same advertised model on AGP; bridge/BAR handling makes this more than a packaging edit. |
| GMA 3100 | `8086:29C2` | `8086:29B2`, `8086:29D2` | Q35 and Q33 siblings of the G33/G31 entry. |
| GMA X3100 | `8086:2A02` | `8086:2A12` | Embedded GME965/GLE960 sibling; exact-ID GTT and panel paths need extending. |
| GMA 4500 / X4500 | `8086:2E12`, `8086:2E22` | `8086:2E02`, `8086:2E32`, `8086:2E42`, `8086:2E92` | Other G4x desktop identities; exact-ID GTT offset handling needs extending. |
| Ironlake | `8086:0042` | `8086:0046` | Mobile sibling; panel/display and GTT handling need checking. |
| Ivy Bridge HD 4000 | `8086:0162`, `8086:0166` | `8086:016A` | GT2 server sibling; retain the Gen7 path explicitly. |
| Haswell GT2 | `8086:0412`, `8086:041E` | Start with `8086:0416`, `8086:041A`; ULT/ULX and CRW IDs are listed below | Mobile/server and platform variants; do not include GT1/GT3 as unchanged aliases. |

Intel group membership comes from the primary
[Intel PCI ID header](https://github.com/torvalds/linux/blob/aee2d33da882d2349ae1d69e4f81f0f341e2ded7/include/drm/intel/pciids.h).
Radeon family assignments come from the Radeon table linked above. Marketing
names are corroboration from the [PCI ID database](https://github.com/pciutils/pciids/blob/edbfcd6566f827430fe0f369294de9ccdae5b947/pci.ids),
version 2026.11.13. An ID can cover several marketed products; another board
brand or a rebrand using an already-bound vendor/device pair needs no extra
device ID.

## Deliberate exclusions and weaker leads

- **945GSE has no missing alternative primary ID.** `27AE` also covers GME.
  `27A2` and `2772` are different 945 SKUs, already deferred by the
  [945GSE identity investigation](../decisions/2026-09-17-intel-945gse-pci-id-survey.md).
  A BIOS PCIR string containing `27A2` is not evidence that a `27AE` PCI
  function has another identity.
- **Pineview desktop `A001` was deliberately deferred** pending a desktop
  survey in the [GMA 3150 plan](../plans/intel-pineview-gma3150.md).
- **RIVA needs identity confirmation first.** The database lists
  `12D2:0019` and `10DE:0018/0019`, but the inspected
  [Linux RIVA PCI table](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/riva/fbdev.c)
  binds `12D2:0018` only. Our physically measured ZX is also `12D2:0018`,
  revision 22. These are leads, not demonstrated omissions affecting an
  observed card. The alternate vendor also needs backend changes.
- **Rage 128 Pro has 23 other upstream Pro IDs.** The
  [aty128 table](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/aty/aty128fb.c)
  distinguishes Pro, Pro PCI, original Rage 128, Mobility and Ultra. The
  table below includes only the other Pro entries, not every Rage 128 name.
- **Legacy ATI is already broadly covered.** VT/VT3/VT4, the Rage II/IIC
  identities, and Rage Pro/LT Pro/XL/XC/Mobility identities in the inspected
  [atyfb table](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/aty/atyfb_base.c)
  are already represented in the current bindings. Mach64 GX/CX/CT/ET are
  separate chips; their absence is not evidence of a missing alias for VT2.
- **S3's demonstrated current parts are covered.** ViRGE/GX shares
  `8A01`, Trio3D/1X and /2X share `8A13`, and Trio64V+ uses `8811` with
  revision-specific model names. They do not need invented separate device
  IDs. The database's `8900`, `8903` and `8905`-`890F` Trio labels need
  stronger evidence: none is accepted by the inspected
  [s3fb PCI table](https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/s3fb.c).
  Original ViRGE, VX, GX2 and mobile ViRGE chips are separate extensions.
- **No additional primary ID was corroborated for VSA-100 or the supported
  SiS 6326.** Voodoo4 and Voodoo5 use `121A:0009`. Other Voodoo generations
  are different chips. `1039:0200` denotes the older/integrated
  5597/5598/6326-labelled group in the database; that name alone does not
  prove compatibility with the discrete `1039:6326` engine path.
- **Generic VBE is not a GPU allowlist.** `1234:1111` is its declared
  QEMU/Bochs test device. The package also provides manual selection for
  other cards with a suitable VESA BIOS. Their absence from native-family
  manifests does not mean the generic package cannot display on them.

Do not add Intel secondary-function IDs such as `2586`, `2596`, `2776`,
`27A6`, `29B3`, `29C3`, `29D3`, `2A03`, `2A13`, `2A43`, `2E13`, `2E23`,
`A002` or `A012` as primary GPUs. Similarly, Radeon HDMI audio identities
are not display adapters. Subsystem IDs and revision-specific names are
not additional device IDs.

## Why adding INF aliases alone would not work

The manifest specification requires an alias to have a generated INF model,
a backend row, a backend that accepts it, and a `V9X_HW16_DEVICE` in the
family's actual device list. The host matrix checks existing declared IDs;
it does not discover external IDs that are absent everywhere.

There are additional dispatch points which make blind alias expansion wrong:

- `src/display16/runtime.asm`, `V9xPciReadIntelGttConfig`, explicitly
  recognises `2A02`, `0042`, `2E22`, `2E12` and `2A42` for the non-Gen3
  BAR/GTT path. The later offset choice is also an exact-ID list. New
  `2A12`, `0046` or G4x IDs would otherwise take the wrong path.
- `src/chipsets/intel/intel_hw16.c`, `v9x_intel_is_gen7`, uses four device
  object addresses. New Ivy Bridge/Haswell objects must join that
  classification so Gen3 hooks do not run on them.
- `src/chipsets/matrox/millennium/millennium_hw16.c` tests exactly `051B`
  and `051A` when assigning 3D/depth capabilities and Mystique BAR fallback.
  AGP aliases need those classifications extended as well as the IDs.
- `src/chipsets/ati/ati_hw16.c` uses exact device object addresses for
  diagnostics and memory measurement. New Radeon objects must reach the
  correct ASIC path and preserve those hooks.
- `src/chipsets/ati/evergreen/eg_3d.c` and the GCN initialization modules
  carry ASIC-specific resource settings. A shared Radeon generation, or
  even a shared ASIC with different harvested units, is not proof that the
  current resource constants are suitable.
- The 945GSE system-memory texture pool is deliberately gated to `27AE`
  in `intel_pool16.c` and `src/minivdd32/loader.asm`. Do not widen that
  allocation gate just because another Gen3 ID is added. The old
  `i9xx_arm.c` diagnostic gate is also explicitly `27AE`, revision 03.

Recommended next work: survey the two Matrox AGP variants, RS690 `791E`
and the closest Intel siblings first. Add only measured-compatible aliases,
update every classification they depend on, regenerate the backend table
and INF, then run the normal code gates and a real device probe. Keep the
other rows as separate support work until their configuration is established.

## Verification and data

- `scripts/check-tree.ps1`: passed, eight families, 468 source/header files.
- `scripts/build-host.ps1`: passed, including the generated family matrix.
- Generated each family's INF in memory and checked every declared
  vendor/device pair: all 71 present.
- Compared every declared pair with `backend_registry_table.inc`: all 71
  present; registry has exactly 71 rows.
- Checked the source tables downloaded on 2026-11-13. The
  [source ledger](2026-11-13-pci-device-id-audit/sources.csv) records URLs
  and SHA-256 hashes of the exact reference bytes.
- [All current bindings](2026-11-13-pci-device-id-audit/current-bindings.csv)
  and [all examined missing IDs](2026-11-13-pci-device-id-audit/missing-ids.csv)
  are supplied as CSV. Blank reference names mean the upstream driver
  has an ID but the downloaded name database does not name it.

No hardware was newly exercised, and no newly identified ID was enabled.

## Current inventory

IDs below are device IDs; the vendor is shown separately. Aliases are bindings, not independent hardware validation. The CSV contains every individual binding.

| Package | Chip | Vendor | Primary ID | Alias IDs |
|---|---|---|---|---|
| 3dfx | 3dfx VSA-100 | `121A` | `0009` | none |
| ati | ATI Mach64 VT2 264VT2 | `1002` | `5654` | `5655`, `5656` |
| ati | ATI Rage Mobility-M AGP | `1002` | `4C4D` | `4742`, `4744`, `4747`, `4749`, `4750`, `4751`, `474C`, `474D`, `474E`, `474F`, `4752`, `4753`, `4C42`, `4C44`, `4C49`, `4C50`, `4C51`, `4C4E`, `4C52`, `4C53` |
| ati | ATI 3D Rage IIC AGP | `1002` | `4757` | `4754`, `4755`, `4756`, `4759`, `475A`, `4C54`, `4C47` |
| ati | ATI Rage 128 Pro | `1002` | `5046` | none |
| ati | AMD Radeon HD 5450 (Cedar) | `1002` | `68F9` | none |
| ati | AMD Radeon HD 7470 (Caicos) | `1002` | `6778` | none |
| ati | AMD Radeon HD 5770 (Juniper) | `1002` | `68B8` | none |
| ati | AMD Radeon HD 5570 (Redwood) | `1002` | `68D9` | none |
| ati | AMD Radeon HD 7660D (Trinity) | `1002` | `9901` | none |
| ati | AMD Radeon R7E (Carrizo) | `1002` | `9874` | none |
| ati | AMD Radeon R3E (Mullins) | `1002` | `9854` | none |
| ati | ATI Radeon X1200 (RS690) | `1002` | `791F` | none |
| ati | ATI Radeon HD 2600 XT (RV630) | `1002` | `9588` | none |
| intel-gma | Intel GMA 950 (945GSE) | `8086` | `27AE` | none |
| intel-gma | Intel GMA 900 (915GM) | `8086` | `2592` | none |
| intel-gma | Intel GMA 3150 (Pineview) | `8086` | `A011` | none |
| intel-gma | Intel GMA 3100 (G33/G31) | `8086` | `29C2` | none |
| intel-gma | Intel GMA X3100 (GM965) | `8086` | `2A02` | none |
| intel-gma | Intel GMA X4500 (G45) | `8086` | `2E22` | none |
| intel-gma | Intel GMA 4500 (Q45) | `8086` | `2E12` | none |
| intel-gma | Intel GMA 4500MHD (GM45) | `8086` | `2A42` | none |
| intel-gma | Intel HD Graphics (Ironlake) | `8086` | `0042` | none |
| intel-gma | Intel HD Graphics 4600 (Haswell) | `8086` | `0412` | `041E` |
| intel-gma | Intel HD Graphics 4000 (Ivy Bridge) | `8086` | `0162` | `0166` |
| matrox | Matrox Millennium MGA-2064W | `102B` | `0519` | none |
| matrox | Matrox Millennium II MGA-2164W | `102B` | `051B` | none |
| matrox | Matrox Mystique | `102B` | `051A` | none |
| nvidia | NVIDIA RIVA 128 | `12D2` | `0018` | none |
| s3 | S3 ViRGE/DX 86C375 | `5333` | `8A01` | `8A13`, `8904` |
| s3 | S3 Trio32/64 86C764 | `5333` | `8811` | `8810`, `8812`, `8813`, `8814`, `8901` |
| sis | SiS 6326 | `1039` | `6326` | none |
| vbe | QEMU/Bochs VBE std-vga | `1234` | `1111` | none |

## Missing primary IDs in the compared tables

These are absent from all current manifests. A row is a research result, not a promise of compatible hardware. [missing-ids.csv](2026-11-13-pci-device-id-audit/missing-ids.csv) gives one row per ID, its reference name when available, source and disposition.

| Compared GPU group | Vendor | Missing IDs | Disposition |
|---|---|---|---|
| GMA 900 desktop / E7221 | `8086` | `2582`, `258A` | sibling-survey-required |
| GMA 950 desktop / mobile 945 | `8086` | `2772`, `27A2` | deliberate-exclusion |
| GMA 3150 desktop | `8086` | `A001` | deliberate-exclusion |
| GMA 3100 Q35 / Q33 | `8086` | `29B2`, `29D2` | sibling-survey-required |
| GMA X3100 embedded | `8086` | `2A12` | sibling-survey-required |
| G4x desktop variants | `8086` | `2E02`, `2E32`, `2E42`, `2E92` | sibling-survey-required |
| Ironlake mobile | `8086` | `0046` | sibling-survey-required |
| Ivy Bridge GT2 server | `8086` | `016A` | sibling-survey-required |
| Ivy Bridge GT1 | `8086` | `0152`, `0156`, `015A` | resource-variant |
| Haswell GT1 production variants | `8086` | `0402`, `0406`, `040A`, `0A02`, `0A06`, `0A0A`, `0A0E`, `0D02`, `0D06`, `0D0A` | resource-variant |
| Haswell GT1 reserved / SDV | `8086` | `040B`, `040E`, `0A0B`, `0C02`, `0C06`, `0C0A`, `0C0B`, `0C0E`, `0D0B`, `0D0E` | do-not-auto-bind |
| Haswell GT2 production variants | `8086` | `0416`, `041A`, `0A12`, `0A16`, `0A1A`, `0A1E`, `0D12`, `0D16`, `0D1A` | sibling-survey-required |
| Haswell GT2 reserved / SDV | `8086` | `041B`, `0A1B`, `0C12`, `0C16`, `0C1A`, `0C1B`, `0C1E`, `0D1B`, `0D1E` | do-not-auto-bind |
| Haswell GT3 production variants | `8086` | `0422`, `0426`, `042A`, `0A22`, `0A26`, `0A2A`, `0D22`, `0D26`, `0D2A` | resource-variant |
| Haswell GT3 reserved / SDV | `8086` | `042B`, `042E`, `0A2B`, `0A2E`, `0C22`, `0C26`, `0C2A`, `0C2B`, `0C2E`, `0D2B`, `0D2E` | do-not-auto-bind |
| RV630 | `1002` | `9580`, `9581`, `9583`, `9586`, `9587`, `9589`, `958A`, `958B`, `958C`, `958D`, `958E`, `958F` | same-family-survey-required |
| RS690 | `1002` | `791E` | same-family-survey-required |
| CEDAR | `1002` | `68E0`, `68E1`, `68E4`, `68E5`, `68E8`, `68E9`, `68F1`, `68F2`, `68F8`, `68FA`, `68FE` | same-family-survey-required |
| REDWOOD | `1002` | `68C0`, `68C1`, `68C7`, `68C8`, `68C9`, `68D8`, `68DA`, `68DE` | same-family-survey-required |
| JUNIPER | `1002` | `68A0`, `68A1`, `68A8`, `68A9`, `68B0`, `68B9`, `68BA`, `68BE`, `68BF` | same-family-survey-required |
| CAICOS | `1002` | `6760`, `6761`, `6762`, `6763`, `6764`, `6765`, `6766`, `6767`, `6768`, `6770`, `6771`, `6772`, `6779`, `677B` | same-family-survey-required |
| ARUBA | `1002` | `9900`, `9903`, `9904`, `9905`, `9906`, `9907`, `9908`, `9909`, `990A`, `990B`, `990C`, `990D`, `990E`, `990F`, `9910`, `9913`, `9917`, `9918`, `9919`, `9990`, `9991`, `9992`, `9993`, `9994`, `9995`, `9996`, `9997`, `9998`, `9999`, `999A`, `999B`, `999C`, `999D`, `99A0`, `99A2`, `99A4` | same-family-survey-required |
| MULLINS | `1002` | `9850`, `9851`, `9852`, `9853`, `9855`, `9856`, `9857`, `9858`, `9859`, `985A`, `985B`, `985C`, `985D`, `985E`, `985F` | same-family-survey-required |
| CARRIZO | `1002` | `9870`, `9875`, `9876`, `9877` | same-family-survey-required |
| Rage 128 Pro | `1002` | `5041`, `5042`, `5043`, `5044`, `5045`, `5047`, `5048`, `5049`, `504A`, `504B`, `504C`, `504D`, `504E`, `504F`, `5050`, `5051`, `5052`, `5053`, `5054`, `5055`, `5056`, `5057`, `5058` | same-family-survey-required |
| Millennium II AGP | `102B` | `051F` | sibling-survey-required |
| Mystique AGP | `102B` | `051E` | sibling-survey-required |
| Mystique database-only identity | `102B` | `0100` | database-only-confirm-first |
| RIVA / RIVA ZX alternate identity | `12D2` | `0019` | database-only-confirm-first |
| RIVA / RIVA ZX alternate vendor | `10DE` | `0018`, `0019` | database-only-confirm-first |
| S3 Trio database-only variants | `5333` | `8900`, `8903`, `8905`, `8906`, `8907`, `8908`, `8909`, `890A`, `890B`, `890C`, `890D`, `890E`, `890F` | database-only-confirm-first |

### Counts

| Disposition | IDs |
|---|---|
| database-only-confirm-first | 17 |
| deliberate-exclusion | 3 |
| do-not-auto-bind | 30 |
| resource-variant | 22 |
| same-family-survey-required | 133 |
| sibling-survey-required | 22 |

Total examined absent IDs: 227. This includes exclusions and database-only leads; it is not a count of GPUs this driver already supports.
