# Current status and next work

Reviewed on 2026-11-27 against the checkout at `bdd303fe` (the 0.61.0
tree, to be released as 1.0.0), the [1.0 plan](plans/release-1.0.0.md),
the README's card tables and the 2026-11-21 to 11-27 run records. A
desk review: no machine was run for it.
Update this page when a default, a validation result or the next step
changes; the dated decision records and issues are the history.
[PLAN.md](../PLAN.md) is the original planning baseline, not the current
backlog.

## Release and checkout

The latest release is [1.0.0](../releases/1.0.0/README.md), built from
`82112c9c`, tagged `v1.0.0` and published on 2026-11-28 with no release
candidate (plan, decision 4); its checks are in
[the 1.0.0 record](decisions/2026-11-28-1.0.0-release-checks.md). The
checkout is 1.1.0, unreleased. 1.0.x takes fixes only.

1.0 supports **Windows 98 SE only** (plan, B14). Windows 95, 98 first
edition and Me install by design and are not supported.

Where the 1.0 gate stands:

| Gate item | State |
|---|---|
| 1. `run-checks.ps1` green from a clean tree | Passed on `389c37a6` (2026-11-27). Any later code change needs it again |
| 2. Every package booted and probed | Done: `intel-gma`, `ati` and `nvidia` on the release-check fleet and remote-help machines; `s3` on the ViRGE/DX and Trio3D/2X, `matrox` on the Mystique and Millennium, `sis` and `3dfx` in A8U4I5; `vbe`, `s3` and the Mach64 VT2 on the 86Box and QEMU guests |
| 3. B10's Have Disk installs and B13's update | B10 closed. B13: an update and a rollback on a GeForce 210 (0.59.0 to 0.60.0); 1.0.0 is published and served (2026-11-28), and the step to it through Check for updates on an installed machine is owed |
| 4. The issue index re-read | Done 2026-11-26 |
| 5. This page | This rewrite |
| 6. The README's tables carry the tiers | Written 2026-11-26; the "1.0 run owed" cells replaced by last-run dates on 2026-11-27 |

Blockers B1 to B14 are closed or decided for 1.0; only B13's last step
remains. B5 (a full-screen DOS box round trip on a physical S3 card),
the Trio64 and Millennium II runs, and the re-runs the issue re-read
listed were left for a later release on 2026-11-27 (Michael).

## Target coverage

The README's [card tables](../README.md#supported-cards) are the
per-card record: tier, last run and what was tested, with links to the
records. This section only counts them. A8U4I5 is the Pentium III test
box whose card is swapped; most other machines are testers' reached
through remote help.

**Supported, run on the 0.61.0 tree (2026-11-21 to 11-27):** GMA 900,
GMA 950, GMA X3100, HD 3000 (`0112`), HD 4600; Mach64 VT2 (86Box only),
Rage IIC, Rage XL, Rage 128 Pro, Radeon X1300, HD 5450, HD 5770,
HD 7660D, Oland XT, Oland 6611 (probes only), R3E, R7E; ViRGE/DX,
Trio3D/2X; Millennium, Mystique; GeForce 210; SiS 6326; Voodoo4; `vbe`.
The R7E did not come back from its 1.0.0 release-check reset
(2026-11-28) and has not run 1.0.0 itself (README known issues).

**Supported on an older run, not repeated for 1.0:** GMA 3150, GMA
4500MHD (0.57.0, offline), Ironlake (0.32.0, card gone), HD 4000
(0.56.0, card gone), HD 4000 mobile, Radeon 9250, X1200 (0.51.0,
offline), HD 2600 XT and HD 2400 Pro (cards gone), HD 7470; Trio32/64;
Millennium II.

**Supported on a field report only:** GMA X4500 and 4500 (G45, Q45),
HD 2500 (Ivy Bridge GT1).

**Experimental:** RIVA 128ZX (V9XD32 T21 and T22 and the Glide
BlendHalf fail, as on 21 November).

**Recognised, never run:** GMA 3100, Ironlake mobile, HD 3000 `0122`,
Radeon 9200 (`5961`, `5962`), HD 5570, Mystique AGP and Millennium II
AGP, and the close relatives the README lists (other Trio models, the
rest of the Rage II and Rage Pro lines, the Voodoo5, the HD 4400).

## Interfaces

| Interface | State | Limit |
|---|---|---|
| Direct3D HAL (DirectX 5 to 7) | Every card with a 3D engine; the CPU rasterizer elsewhere (default in `vbe` at 16 bpp, opt-in otherwise) | Only what each engine draws exactly. A refused draw goes to the CPU on the GMA 950; elsewhere it is not drawn (README, Direct3D) |
| DirectX 8 (DDI 6) | Offered to the programs that need it; DxDiag and 3DMark 2001 SE to a score on the Rage XL, GMA 950 and GMA 900. DDI 8 with pixel shader 1.1 on the HD 4600, HD 3000 and Oland | No stencil. No Direct3D 9 device |
| OpenGL 1.1 ICD | Every 3D engine and the CPU rasterizer; `GL_ARB_multitexture` and `GL_SGIS_multitexture` where the engine has two units; `GL_EXT_compiled_vertex_array`; lighting on the HD 4600 only | Evaluators, feedback and selection not implemented ([inventory](plans/opengl-1.1-requirements.md)). Display lists and texgen from 1.1.0 ([record](decisions/2026-11-28-opengl-display-lists-and-texgen.md)); image commands inside a list run when called, not when the list is |
| Glide 2 and 3 | Every package but `3dfx`, which runs 3dfx's own | Wrappers from 0.23.0 or earlier do not work. The half blend fails on the engines that cannot blend (Trio3D/2X, RIVA 128ZX, the Matrox cards) |
| ATI 3DCIF | Every package; ATI's eight SDK demos on the Rage IIC, Rage XL and GMA 950 | No mipmaps, tiled textures, meshes or TLVERTEX ([issue](issues/2026-10-14-3dcif-unimplemented-features.md)) |
| Updates and reports | Check for updates and Send report on the settings page | One physical update and rollback (B13, [record](decisions/2026-11-25-1.0-b13-update-rollback-gt218.md)); RECOVER.TXT's Safe Mode steps not run by hand |

No card has a hardware cursor.

## Left for after 1.0

The plan's [Not in 1.0](plans/release-1.0.0.md#not-in-10) list is the
full record, with who decided what. In short:

- **1.1 first:** OpenGL display lists and texgen (Tux Racer and
  Minecraft need them in the field; done 2026-11-28, Tux Racer run,
  Minecraft not); GitHub PR 32 (Gen3 blitter copies
  and a hardware cursor); branch `rv280-geomscan`. (`hsw-panel-fit`
  was listed here too, but its commit has been in main since 0.56.0;
  its write path ran on the 4570 on 2026-11-28.)
- **Parked branches:** `ati-cayman`, `wip-caicos-6779`, the Doom 3
  worktree.
- **Windows 95, 98 first edition and Me**; Stand By on a laptop.
- **B5 on a physical S3 card**, and the Trio64's live mode switch
  repaint and black level.
- **Card defects found in the 1.0 round, left for later:** the
  Mystique's black Adventure areas, the ViRGE/DX's 640x480 engine
  timeouts, black X8R8G8B8 or 32-bit textures in V9XD32 on the
  Rage 128 Pro, SiS 6326 and Voodoo4, the Millennium's shifted analogue
  picture, and the Rage IIC's and Trio3D/2X's V9XD32 failures recorded in
  their rows.
- **Re-runs skipped for 1.0:** Forsaken on the GM45 and GM965, Final
  Reality on the GMA 950 netbook, Doom95 and the `vbe` 1024x768x16
  scanout on the guests, and the record re-runs (RV630/RV610 constant Z,
  the R3E's first clear, the Rage 128's 3DMark 2000 hang, the
  unreproduced hard locks).
- **Feature gaps:** YUV surfaces and overlays, DPMS beyond S3, banked-only
  VBE modes, the Bochs DISPI interface, VMware SVGA II, stencil, 8 bpp
  gamma, an Intel GPU clock readout.

Every open issue is in [issues/README.md](issues/README.md) and every
plan in [plans/README.md](plans/README.md).
