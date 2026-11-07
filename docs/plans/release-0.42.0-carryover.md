# Carried into 0.42.0

Date: 2026-11-08, as 0.41.0 went to its release checks. What 0.41.0 did
not finish or did not measure, each with where its evidence is. Strike an
item when a later record settles it.

## Waiting on someone

- **The S3 shutdown flash** (Trio64V+ and V2/DX: cyan text-cell garbage
  for a second or two before the "safe to turn off" screen). Two test
  builds are with perchrh on
  [PR 24](https://github.com/michaeldale/velocity9x/pull/24): A blanks
  the screen around Disable's mode 03 (his change), B turns the S3 linear
  window (CR58 bit 4) and engine (CR40 bit 0) off before it. Branches
  `test/pr24-a-disable-blank` and `test/pr24-b-s3-disable-restore`
  (GitLab), downloads on the GitHub pre-release `test-pr24-shutdown`.
  Neither has run on a card. Its issue record and probe files are in the
  PR, not yet on main.
- **A game on the R3E** (M-JJN1ZZ). The owner was asked on 2026-11-08 to
  install a Direct3D game or 3DMark. Nothing real has run on Mullins,
  and nobody has looked at its screen.

## Unmeasured, shipped in 0.41.0

- **Write-combining on Cedar, Juniper, Redwood and Caicos** (HD 5450,
  5770, 5570, 7470): opted in with Trinity's row, measured on Trinity
  only ([record](../decisions/2026-11-08-trinity-write-combining.md)). No
  such card is online. The first boot of one should read
  `MtrrWrite=ok` with an `f8000800` mask in `V9XBOOT.INI`, and the
  desktop should be intact.
- **The R3E's OpenGL renderer string** now reads "Velocity9x Radeon R3E"
  (`d3d_core.c`); not run on the machine since the change.
- **3DCIF on the R3E**: not run.

## Open, not explained

- **`GB_ADDR_CONFIG` reads 0 on Mullins** after gpu_init writes
  02010001h, with the CP running, while the tile table reads back
  ([record](../decisions/2026-11-08-mullins-opengl-glide-row-size.md)).
  Matters only if tiled 2D surfaces are used.
- **The R3E's one busy CANBLT answer**: V9XDDP now retries and records
  `BltCanFirstHr`, `BltCanTries` and `BltCanWaitMs`
  ([record](../decisions/2026-11-08-mullins-directdraw-on-sdma.md));
  eight runs since have not reproduced it. Read those keys in the next
  release checks on any engine-backed chip.

- **Trinity's OpenGL reads at 16 bpp**: from V9XGLP's SGIS mip sweep
  on, every read is white or stale (`Cva` fails, `Offset*` and `Fog*`
  read FFFFFF). The same with write-combining off, so not 0.41.0's
  change ([record](../decisions/2026-11-08-0.41.0-release-checks.md)).
  Next: the same probe at 32 bpp, then bisect the sweep.

## Not started

- **The settings page can show a removed card's PCI revision**: it takes
  the first `Enum\PCI` key for the id
  ([issue](../issues/2026-11-07-settings-page-revision-from-stale-enum-key.md)).
  One file; verifying it needs a machine with two Enum keys for one id.
- **The Dragothic white sky on the GMA 950** (3DMark 2001 SE), parked in
  0.40.0 with the system-memory textures plan
  ([plan](intel-gen3-system-memory-textures.md)).
- **HD 5570 (Redwood)**: recognised since 0.40.0, never run on a card.
