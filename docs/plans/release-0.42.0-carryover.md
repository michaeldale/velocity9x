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
- ~~**A game on the R3E** (M-JJN1ZZ)~~: settled 2026-11-08. The owner
  installed 3DMark 2000, and it ran to its score (1667) on this build,
  watched, with clean counters and the first game test's frames grabbed
  ([record](../decisions/2026-11-08-mullins-3dmark2000.md)). No other
  game is installed.

## Unmeasured, shipped in 0.41.0

- **Write-combining on Cedar, Juniper, Redwood and Caicos** (HD 5450,
  5770, 5570, 7470): opted in with Trinity's row, measured on Trinity
  only ([record](../decisions/2026-11-08-trinity-write-combining.md)). No
  such card is online. The first boot of one should read
  `MtrrWrite=ok` with an `f8000800` mask in `V9XBOOT.INI`, and the
  desktop should be intact.
- ~~**The R3E's OpenGL renderer string**~~: reads "Velocity9x Radeon
  R3E" on the machine (V9XGLP, 2026-11-08,
  [record](../decisions/2026-11-08-mullins-3dmark2000.md)).
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
- **The R3E's one R3D clear timeout**: V9XR3DP, run last after 3DMark
  and the other probes, got `ClearResult=9` (TIMEOUT), and every read
  after it was 0. Its rerun passed, and the engine timeout counters stayed
  at 0 ([record](../decisions/2026-11-08-mullins-3dmark2000.md)). It may
  be the same as the busy CANBLT answer.

- ~~**Trinity's OpenGL reads at 16 bpp**~~: settled 2026-11-08. A
  16 bpp target an odd number of 8 pixels wide was placed with a pitch
  the target check refused; fixed in `d3d_eg.c` and measured on Trinity
  ([record](../decisions/2026-11-08-render-target-placement-trinity-rage128.md)).
  Owed: the same build on a Cedar, Juniper, Redwood or Caicos card.

## Not started

- **The settings page can show a removed card's PCI revision**: it takes
  the first `Enum\PCI` key for the id
  ([issue](../issues/2026-11-07-settings-page-revision-from-stale-enum-key.md)).
  One file; verifying it needs a machine with two Enum keys for one id.
- **The Dragothic white sky on the GMA 950** (3DMark 2001 SE), parked in
  0.40.0 with the system-memory textures plan
  ([plan](intel-gen3-system-memory-textures.md)).
- **HD 5570 (Redwood)**: recognised since 0.40.0, never run on a card.
