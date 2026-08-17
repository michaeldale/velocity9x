# Multi-chip restructure plan

Status: proposed (2026-08-15)

## Context

Velocity9x today supports S3 ViRGE/DX and Trio64 via per-chip compiles (`-dV9X_TARGET_*`), with chip code scattered across four chokepoints: `src\display16\runtime.asm` (IFDEF-switched PCI find, aperture read, S3 register pokes), `src\display16\ddi.c` (#ifdef mode tables/identity), `src\display16\dd16.c` (#ifdef engine descriptor), and `src\display32\ddhal.c` (runtime virge/trio function pairs). The designed backend abstraction (`include\velocity9x\backend.h`, `src\common\backend_registry.c`, `src\chipsets\`) exists but is only exercised by host tests — no live hardware path flows through it. The goal is to restructure so new chips (ATI Rage Pro, Intel GMA950/3100, ...) can be added as data plus small backend modules.

Decisions taken:

- **Per-family binaries**: one built package per chip family (S3, ATI, Intel, VBE-generic), runtime PCI-ID dispatch within a family. Not one universal binary; not one build per exact chip.
- **VBE tier-0 backend**: a first-class chip-agnostic backend (BIOS mode-set via VBE 2.0+, linear framebuffer, unaccelerated). Every new chip starts life on it; native backends add acceleration on top.
- **Scope**: rewire existing S3/Matrox code through the backend interface with functional equivalence, then validate with an ATI Rage Pro pilot brought to unaccelerated desktop.

## Target architecture

Three layers, because the three execution contexts cannot share code:

### 1. Pure policy layer (unchanged shape)

`include\velocity9x\backend.h` (`v9x_backend_ops`: probe / bind_framebuffer / validate_mode / ...) stays the host-testable, I/O-free layer. Do NOT grow it into PLAN.md §3.2's 13-op contract: ops that do port I/O or INT 10h/31h cannot be host-tested and belong in the 16-bit hardware layer below.

### 2. 16-bit hardware layer — new `include\velocity9x\hw16.h`

One statically linked `struct v9x_hw16_ops` table per family binary:

- `v9x_hw16_device[]` device list (vendor/device IDs, adapter names, quirk flags) — within-family PCI dispatch.
- The audited VBE mode table (currently the `#ifdef` tables at `ddi.c:66-92`) and VBE mode-set flags (0x8000 S3 no-clear vs 0x4000 LFB).
- **Nullable hooks**: `read_aperture`, `post_mode_set`, `detect_vram`, `publish_diagnostics`, `fill_engine_descriptor`. NULL hook = VBE-generic default. This rule *is* the tier-0 backend: a family with all-NULL hooks is the generic VBE package.

Hooks must stay non-FAR: the driver builds with `wcc -mc` (compact = near code), so hook tables cost near calls only as long as implementations live in the single code segment.

### 3. VBE: common service and tier-0 family

- Service: `src\display16\hw\vbe16.c` wrapping 4F00/4F01/4F02/4F06 plus text-mode restore. 4F00/4F01 are new code — today the S3 path reads CR59/CR5A instead of asking the BIOS for PhysBasePtr; 4F01 is what makes tier-0 chip-agnostic.
- Backend: `src\chipsets\generic\vbe\vbe_hw16.c` — all hooks NULL, aperture from 4F01 PhysBasePtr, VRAM from 4F00 TotalMemory, engine_type = NONE, LFB flag 0x4000.

### 4. 32-bit engine layer — data-selected, not code-selected

`ddhal.c` cannot call 16-bit code; chip dispatch flows through a generalized engine descriptor in `V9X_DD_SHARED`. `V9X_DD_ENGINE` gains `engine_type`, `engine_caps` (SOLID_FILL / SCREEN_COPY / FLIP / VBLANK / D3D), `io_base`, `crtc_index_port`, two reserved DWORDs; existing fields and VALID/STATUS_VALIDATED bits keep their positions. One deliberate `V9X_DD_SHARED_ABI` bump; the 4096-byte DPMI bound (`runtime.asm` `V9X_DD_SHARED_BYTES`, `v9x_dd_assert_shared_fits_dpmi_block`) must be re-asserted on both compilers.

`ddhal.c` (3455 lines) splits into (**landed 2026-08-16**, with
`src\display32\ddhal_internal.h` as the private seam and the module's only
`<windows.h>`):

- `ddhal_core.c` — DriverInit, shared-block validation, trace ring, surface bookkeeping, Lock/Unlock, engine-ops selection. Engine ops resolve **lazily** from `engine.engine_type` on first use after VALID, because DriverInit runs before the 16-bit descriptor refresh; the 16-bit data-driven caps clamp remains the caps authority.
- `blt_cpu.c` — CPU fill/copy fallbacks used by all engines.
- `engines\eng_s3_virge.c`, `engines\eng_s3_trio.c`, `engines\vga_scanout.c` (shared CRTC 0C/0D/69 display-start + 3DA vblank). `engines\eng_none.c` was **not** written: a null ops pointer already is tier-0, since the resolver returns null for an unknown `engine_type` and every call site then falls to `blt_cpu.c`. It becomes worth a file when `build_caps` joins the table and tier-0 has Lock/Unlock-only caps of its own to publish.
- `d3d\d3d_virge.c` — the entire D3D block, plus `v9x_d3d_publish`, the capability block lifted out of `DriverInit` so the D3D module owns what it advertises. Still gated by the 16-bit caps clamp; it moves behind `eng_s3_virge`'s caps builder when that exists.
- The S3 register vocabulary went into `ddhal_internal.h`, not `include\velocity9x\regs\s3.h`: nothing outside the 32-bit HAL uses those constants yet. The public header is worth creating when the GDI acceleration work needs the same registers from the 16-bit driver.

The runtime dual dispatch (`v9x_trio_engine_ready() ? v9x_trio_copy : v9x_virge_copy` at `:1274`, fills at `:1482`, drains at `:1028`, blt status at `:1542`) collapses into one `const struct v9x_engine32_ops *` vtable. **Landed 2026-08-16** as `ready / validate_status / status_validated / can_blt / wait_idle / fill / copy`, resolved per call from `engine.engine_type`. The three predicates are separate because the four call sites genuinely ask three different questions — the drain wants the passive `status_validated`, the blit paths want the latching `validate_status`, and DDGBS_CANBLT is a Trio64 idle poll but a ViRGE latch. `recover` is deliberately **not** a member: recovery is only ever called from inside the bounded wait that expired, and the Trio64 has none, so it stays file-local when the waits move to `engines\eng_s3_virge.c`. `set_display_start`, `in_vblank` and `build_caps` join the table when the files move; none of them is dual-dispatched today. `V9X_DD_ENGINE_TYPE_*` and `V9X_DD_ENGINE_CAP_*` moved out of the 16-bit-only `hw16.h` into a new dependency-free `include\velocity9x\engine_abi.h`, so the 32-bit HAL no longer includes the 16-bit hardware layer. The `V9X_DD_ENGINE_S3_*` identity bits are **retired**: `engine.flags` is runtime state only and `engine_type` is the sole chip identity, with `V9XTRACE` gaining `EngineType`/`EngineCaps` so the diagnostics did not lose the information. See `docs\decisions\2026-08-16-engine32-vtable.md`.

## Build and packaging: family manifests

Per-family manifest `packaging\families\<familyId>\family.psd1` (`Import-PowerShellDataFile`: built into PS 5.1, data-only, no JSON-escaping of regex audit patterns). Declares: chips (PCI hardware IDs, DeviceDesc, per-chip MODES capability), source module list, compiler defines, wdis audit signature patterns, required map symbols, INF metadata, floppy folder, VM profile/ports/modes. Loader lib `scripts\lib\family.ps1` (validate schema, enumerate families, derive cross-family forbidden pattern lists, assert no PCI ID appears in two families).

Consumers:

- `build-win16-ddi-skeleton.ps1 -Family <id>` — source list and defines from the manifest; old `-S3Trio64`-style switches become deprecated aliases until retirement.
- `build-active-package.ps1 -Family <id>` — output `build\win98se-<familyId>`; `LegacyOutputName` preserves `win98se-active` / `win98se-trio64` / `vm-probe\ACTIVE` during migration.
- New `scripts\build-all-packages.ps1` — loops families, writes `build\packages.json` (family, version, buildId, per-file SHA256).
- New `scripts\run-checks.ps1` — the local CI gate: check-tree → host tests → per-family builds + audits + INF assertions → floppy.

### Post-link audit replacement

The current per-chip wdis instruction audits break when a family binary legitimately contains several chips' code. Replacement in a new `scripts\audit-family-binary.ps1`, three manifest-driven layers:

1. **Cross-family contamination**: the family image must match all of its own chips' signature patterns and none of any other family's (derived automatically from the other manifests — adding a family strengthens every existing family's forbidden list with zero script edits).
2. **Per-chip-object audits**: once per-chip code splits into separate objects, apply the chip's signatures as required and sibling chips' signatures as forbidden within each object (catches intra-family leakage).
3. **Link-map symbol audits**: required per-chip symbols, required family dispatch-table symbol, forbidden other-family backend symbols. Existing chip-agnostic audits (VDD handoff, DIB thunk guards, NE header, exports, segment flags) stay verbatim as script logic.

### INF generation

Replace the string-replacement rewrite in `build-active-package.ps1:55-114` with structured generation (`scripts\lib\inf.ps1`): multi-model Chicago INF, one `[Velocity9x.Models]` line and one install section per chip, shared copy/registry sections, per-chip `MODES` AddReg driven by manifest capability (this is how a VRAM-limited chip omits 1024x768x16 and the VBE family gets a conservative set). The single-hwid assertion becomes hardware-ID **set equality** against the manifest. The checked-in `packaging\win98se\velocity9x.inf` is retired once generation lands. The VBE family uses an explicit hardware-ID allowlist — never a wildcard — plus Have-Disk instructions for unlisted cards. (**2026-08-16:** those instructions only became true when the VBE family gained `pci_match_optional`. The INF allowlist is not the only one a family carries — `v9x_hw16.devices[]` is compiled into the driver and Have-Disk cannot reach it, so tier-0 had to stop treating a PCI miss as fatal. See `docs\issues\2026-08-16-tier0-defects-deferred.md` D3.)

## Proposed tree (new/changed)

```
include/velocity9x/   hw16.h, engine_abi.h, regs/s3.h (lifts ddhal.c:36-147 consts;
                      supersedes gdi-acceleration.md's proposed s3_engine_regs.h)
src/display16/        enable16.c (staged enable sequence in C, stage codes verbatim),
                      hw/vbe16.c, hw/pci16.c; runtime.asm keeps only chip-agnostic
                      INT 10h/1Ah/31h primitives parametrized by DGROUP vars
src/chipsets/         generic/vbe/vbe_hw16.c
                      s3/common/s3_regs16.c (CR38/39 unlock, CR58/40/53, CR59/5A,
                      CR36 VRAM, PLL — C port I/O via the #pragma aux pattern
                      already proven at ddi.c:165-169)
                      s3/virge/virge_hw16.c, s3/trio64/trio_hw16.c
                      matrox/millennium2/mga2_hw16.c
                      ati/ragepro/{ragepro_backend.c, ragepro_hw16.c}
src/display32/        ddhal_core.c, blt_cpu.c, engines/*.c, d3d/d3d_virge.c
packaging/families/   s3-virge/, s3-trio64/, matrox-m2/ → later s3/, vbe/, rage/
scripts/              lib/family.ps1, lib/inf.ps1, audit-family-binary.ps1,
                      build-all-packages.ps1, run-checks.ps1
```

## Migration phases (green at every step)

Through phase 7 the existing ViRGE/Trio64 packages must stay **byte-comparable** built with a pinned `-BuildId golden-compare`; after the family merge the gate becomes functional (VM matrix + host tests).

1. **Baselines.** Snapshot per target: serial boot log, full `C:\V9XHW.INI`, `V9XBOOT.INI` stage strings (normal boot and induced failures per stage), DDGETTRACE/V9XDDP output, Ironfield BltFast FPS, Hellbender D3D run (ViRGE), NE map segment sizes, full `run-vm-mode-matrix.ps1` pass on both S3 profiles. Archive golden SHA256 of both packages and the floppy outside `build\`.
2. **Manifests, no consumers.** Add `scripts\lib\family.ps1` plus `s3-virge` / `s3-trio64` / `matrox-m2` manifests encoding exactly today's source lists, defines, audit patterns (the existing ViRGE `or al,13H` etc. lists transfer verbatim), INF fields, floppy folders, VM ports. Extend `check-tree.ps1` to validate them. Trivially green.
3. **Builders consume manifests.** `-Family` on the skeleton and package builders; extract `audit-family-binary.ps1`; INF generation reproducing the current rewritten INF byte-for-byte before switching the assertion to set-equality; manifest-driven floppy; `build-all-packages.ps1`; `run-checks.ps1`. Gate: golden compare of both packages and the floppy tree.
4. **ddi.c extraction** (lowest risk). Introduce `hw16.h` and per-family `*_hw16.c` tables (mode tables including the Doom95 640x400 row ordering, device IDs, identity/diagnostic strings); move CR36/PLL reads into `s3\common\s3_regs16.c`. No `#ifdef V9X_TARGET_*` left in `ddi.c`. Gate: byte-identical `V9XHW.INI` per target, mode-matrix pass, map code delta under 1 KiB.
5. **runtime.asm de-IFDEF** (highest risk; two sub-steps).
   - 5a: replace IFDEF-selected literals with DGROUP variables stamped from the active hw16 table before `V9XHARDWAREENABLE` (`_v9x_pci_vendor/_device`, `_v9x_vbe_mode_flags`, `_v9x_map_pages_hi/lo`). `V9xFindPciDevice` loops the device list — ViRGE+Trio64 become one S3 binary here.
   - 5b: lift the staged enable body into `enable16.c` (C), calling vbe16/pci16 and the hw16 hooks; the DPMI selector+map stages stay as one chip-agnostic asm helper. **Preserve verbatim:** stage code numbering (`v9x_trace_hardware_failure` depends on it), selector reuse across Enable cycles, and the never-free-the-selector Disable behavior (`docs\issues\2026-08-14-hellbender-dibeng-gpf.md`). Rule: vbe16 functions may only be called between VddPreMode and VddRegister (or from ResetHiResMode/Disable as today).
   - Gate: identical stage strings including induced failures, identical serial log, full-screen DOS-box round trip per target, enable/disable soak.
   - **5b constraint found while scoping (2026-08-16):** `wcc` is invoked without a CPU flag (`-bt=windows -mc -zu -zc -zls -s -zq -wx`), so the C is compiled for 8086 while `runtime.asm` is `.386p`. Port I/O and INT 10h lower cleanly into `#pragma aux` because they only need 16-bit registers, but PCI BIOS B10Ah returns its configuration dword in `ECX`, and the aperture validation masks are 32-bit. So `pci16` cannot simply become C the way `vbe16` can. Either add a CPU flag to the family compile options — which changes codegen everywhere and needs its own golden re-baseline — or keep the INT 1Ah config-dword read in `runtime.asm` as a chip-agnostic primitive parametrized by DGROUP, which is what §"Proposed tree" already allows. The second is the smaller change and is the recommended route.
   - Also note before porting: stage code 1 (`fail-hardware-pci`) is unreachable, because `V9xHardwarePresent` short-circuits first and both paths call the same `V9xFindPciDevice`. See `docs\decisions\2026-08-16-restructure-baseline.md`.
6. **dd16.c + ABI bump.** The `#ifdef` block at `dd16.c:162-173` becomes the `fill_engine_descriptor` hook; the Trio64 caps clamp (`dd16.c:299-324`) becomes data-driven on `engine_caps` (no-D3D ⇒ null lpD3D*/GetDriverInfo exactly as today). Single ABI bump; keep the old `V9X_DD_ENGINE_S3_*` flag bits until phase 7 so this can ship with `ddhal.c` unmodified. Gate: DDGETTRACE/V9XDDP caps identical per target; Trio64 still shows no D3D.
7. **ddhal.c split.** Mechanical extraction along the seams listed above; vtable resolved lazily. Gate: Ironfield FPS unchanged, BLT_ENGINE trace counters nonzero on both S3 targets, the full `V9XDDP` Direct3D gate set unchanged on ViRGE (**not** Hellbender — measured 2026-08-16, it renders in software and moves no `ddhal.c` counter at all, so it covers the 16-bit enable path and nothing this phase touches; keep running it as an enable/mode-switch regression, not as D3D coverage), timeout-injection recovery matches the per-target baseline in `docs\decisions\2026-08-16-engine-fault-injection.md` (`V9XTRACE.EXE -inject=N` → workload → dump). Note the two targets are **not** symmetric: ViRGE forces through `v9x_wait_fifo` and recovers, so resets track timeouts one for one; Trio64 forces through `v9x_trio_wait_idle`, which has no recovery call, so its `reset_count` stays flat. The vtable must permit a null `recover`.
8. **S3 family merge** (sync point between source and build tracks). **Done 2026-08-16** — see `docs\decisions\2026-08-16-s3-family-merge.md`. One `s3` family, one binary, two INF models, runtime PCI dispatch. `enable_aperture` and `fill_engine_descriptor` moved from the family ops table into `V9X_HW16_DEVICE` (the only two hooks the chips disagree about), `devices` became an array of pointers so each chip module owns its entry, and `V9xFindPciDevice` records the matched index. The per-object audit layer now carries the weight the image-wide one cannot: the ViRGE's CR53 sequence is required in its object and forbidden in the Trio's. Gate met — the mode matrix passed all six modes on both 86Box profiles from one package, and the phase 7 counter/D3D set reproduced exactly with the Trio64 still advertising no D3D. `s3-virge`/`s3-trio64` manifests, legacy switches, `LegacyOutputName` and the checked-in INF retired; byte-comparability ended as intended. Cost: +360 bytes of code to carry the second chip.
9. **VBE tier-0 end-to-end. DONE 2026-08-17** — the exit gate is met, and on stronger evidence than the phase asked for: the full six-mode matrix passes with the `vbe` package on the 86Box **Mach64 VT2**, a card whose id that package's INF does not claim, reached exactly as an owner of an untested card would reach it. `enable-ok`, GDI and palette passes on every mode; results in `build\driver-results\mode-matrix-vbe-std-vga-20260817-164318`. Getting there needed four defects fixed (`docs\issues\2026-08-16-tier0-defects-deferred.md`), of which two were structural: a family carried a second PCI allowlist compiled into the driver that the INF could not reach, so the documented Have-Disk route was inert (D3); and ring 3 has no working way to hand the VBE BIOS a buffer, so 4F00h/4F01h now run at ring 0 in the mini-VDD and the 16-bit side reads a cache through a private VxD API (D4). The QEMU std-vga guest named below was never built - no Win98 media on the build host - and is no longer on the critical path. **Original notes follow.** Source and packaging done 2026-08-16 — see `docs\decisions\2026-08-16-vbe-tier0-family.md`. The `generic/vbe` family package builds, `run-checks` is green across all three families, and the host suite covers the pure parsers (4F00/4F01 validation, mode-whitelist intersection). The NULL-hook default in `enable16.c` is the whole backend; 4F00h/4F01h reach the BIOS through DPMI 0100h + 0300h because they pass a buffer and the driver is in protected mode. Cost: +2048 bytes of `_TEXT` on the S3 image, exactly at the per-step budget, because the default lives in shared code — accepted, with the reasoning and the escape hatch in the decision record. Two corrections to what this item originally said: the **Matrox profile cannot be the first validation vehicle** (`matrox-m2` is `Vm.Emulator = 'none'`, real hardware only — the QEMU std-vga guest is the vehicle, with the S3 86Box profiles covering the shared-path regression); and the ViRGE audit pattern `test\s+al,8` needed anchoring, because unanchored it also matches the `test al,80H` that the new LFB attribute check compiles to in *every* family. **Remaining:** the QEMU std-vga guest itself (profile `Win98SE-QEMU-StdVGA`, port 9872) and the mode matrix against it. **Known limit:** the ViRGE/DX BIOS ignores the generic LFB bit 0x4000 (`runtime.asm:432-436`) — tier-0 is not expected to work there, and refuses at stage 3. Families with no emulator (`Vm.Emulator = 'none'`, e.g. Intel GMA) make the VM runner refuse with an explicit real-hardware-only error.
10. **Pilot: ATI Rage Pro.** Chosen over Intel GMA: pure PCI, era-correct, VBE 2.0 BIOS, and 86Box ships mach64-family emulation; GMA950/3100 has no emulatable i945/G31 platform and validates nothing about the refactor that ATI does not — it becomes the second post-pilot family, on physical hardware. `src\chipsets\ati\ragepro\` with `devices[]` carrying both 0x4750 (Rage Pro) and 0x5654 (Mach64 VT2 fallback if the local 86Box build lacks Rage Pro — within-family dispatch handles this). Tier-0 only: stage progression pci → vbe-mode → aperture(4F01) → dpmi-map → enable-ok, unaccelerated desktop at 640x480x8, full mode matrix, palette smoke, mode-switch, DOS-box round trip, Lock/Unlock-only DirectDraw, Doom95 640x400. Native mach64 acceleration (the register set is 8514/A-like, a close cousin of the Trio path) is a later `eng_mach64.c` — out of scope.
11. **Host tests + docs.** **Done 2026-08-16.** `scripts\lib\family-matrix.ps1` generates `v9x_family_matrix.h` from the manifests, dot-sourced by both host build scripts so the Watcom and MSVC passes compile the same header. `tests\host\test_family_matrix.c` asserts: every declared PCI ID resolves to a backend, chips of one family share it and chips of different families do not, undeclared IDs return null, and no capability is claimed against `EngineType = 'NONE'`. Chips gained a `VideoMemoryBytes` key, which is what the mode-agreement check binds before asking the backend to lay out each advertised mode. **Scope correction:** `validate_mode` is a VRAM-bounded layout calculator, not a whitelist, so it cannot be asserted to accept *exactly* the advertised modes — it will lay out many no INF mentions. The check runs the direction that matters instead: every advertised mode must be servable, and a mode exceeding the declared VRAM must be refused. The "VBE backend advertises no accel caps" assertion waits for phase 9 to create that backend; the `EngineType = 'NONE'` rule already covers the shape of it. Docs: `family-manifest.md` and `BUILDING.md` updated, per-family packaging decision record and VM profile/port docs already in place from phases 3 and 8.

## Critical files

- `src\display16\runtime.asm`, `src\display16\ddi.c`, `src\display16\dd16.c` — chokepoints 1–3
- `src\display32\ddhal.c` — chokepoint 4 (split)
- `include\velocity9x\win9x_ddraw_abi.h` — `V9X_DD_ENGINE` v2 + ABI bump
- `include\velocity9x\backend.h`, `src\common\backend_registry.c` — kept; registry gains `v9x_hw16_for_pci`
- `scripts\build-win16-ddi-skeleton.ps1`, `scripts\build-active-package.ps1`, `scripts\build-floppy-package.ps1`, `scripts\run-vm-mode-matrix.ps1`, `scripts\check-tree.ps1` — manifest consumers
- `packaging\win98se\velocity9x.inf` — becomes generated; retired at phase 8

## Risks and mitigations

- **16-bit code-segment growth** (`wcc -mc`, one 64K code segment): per-step map-file budget — fail review if a step grows code more than 2 KiB without justification; hooks non-FAR by convention plus map review; per-family linking keeps exactly one hw16 table and one chip-regs file per binary; VBE 4F00/4F01 parse buffers in far data, not DGROUP.
- **Enable-sequence regressions**: the order (VDD pre-mode → PCI find → 4F02 → pitch → aperture → chip pokes → DPMI map → VDD register) encodes two hard-won fixes (never-free selector, selector reuse). Stage codes preserved verbatim so boot-trace tooling detects divergence; whole sequence stays in one function with hooks as leaf calls; induced-failure gates per stage.
- **D3D leaking to non-ViRGE engines**: double-gated — the 16-bit clamp nulls lpD3D*/GetDriverInfo whenever `engine_caps` lacks D3D, and the 32-bit caps builder exists only in `eng_s3_virge`. Verify with V9XDDP on Trio64/VBE (no D3D GUIDs) and Hellbender on ViRGE.
- **ABI bump hazards**: mixed old/new DRV+DLL during development already fails safe (DriverInit rejects on dwSize/abi mismatch → driverinit-pending trace). Bump once (phases 6+7 land in one release train); re-verify the 4096-byte asserts on both compilers.
- **Plan conflict**: `docs\plans\gdi-acceleration.md` proposes an S3-named shared register header and edits `V9XHARDWAREENABLE`/BeginAccess territory. Its header lands as `include\velocity9x\regs\s3.h` instead; sequence GDI-accel work after phase 5.
- **SetupX multi-model INF behavior**: per-model MODES AddReg is the one Win98-specific mechanism worth an empirical check — verify on a throwaway VM snapshot at phase 3.

## Verification

- Every phase ends with `run-checks.ps1` green (check-tree → host tests → per-family builds + audits + INF assertions → floppy).
- Phases 1–7: byte-identical golden compare of both S3 packages (pinned BuildId), plus the per-phase behavioral gates above (V9XHW.INI diff, stage strings, DDGETTRACE/V9XDDP caps, Ironfield FPS, Hellbender).
- Phase 8 onward: `run-vm-mode-matrix.ps1 -Family <id>` on the matching 86Box profile pair (9869 test / 9870 reference). Rage pilot exit gate = the same suite the S3 targets pass, minus engine counters.
