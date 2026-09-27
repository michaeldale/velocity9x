# Optional update checker, guarded auto-updater, and installed-file audit

Date: 2026-09-27

Status: proposed; nothing implemented.

## Outcome

Add one optional Windows 98SE utility that can:

1. report whether a newer Velocity9x release exists for the installed adapter
   family;
2. audit every installed Velocity9x runtime file against the selected release;
3. when separately enabled, download, verify, stage, and finish an update; and
4. preserve a tested standard-VGA or file-restore recovery path.

Checking, downloading, installing, and rebooting are four distinct permissions.
The defaults are no network access, no background task, no automatic install,
and no automatic reboot. A user may enable periodic checks without enabling
downloads or installation. "Auto update" means that an opted-in user can let a
verified update proceed without selecting files by hand; it does not mean an
unattended rewrite of a live display stack.

Windows 95 and Windows Me remain out of scope until the existing Win98SE
installation and recovery gates pass on those systems.

## Existing pieces to retain

- `include/velocity9x/build.h` is the product-version authority. The same build
  identifier is already embedded in the display driver, mini-VDD, HAL, OpenGL
  ICD, and settings components.
- `scripts/build-all-packages.ps1` already writes family, version, build, file
  size, and SHA-256 data to `build/packages.json`.
- Every built package already contains `MANIFEST.TXT` and `SHA256.TXT`.
- The INF installs five runtime files into the Windows system directory:
  `V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL`, `V9XSETP.DLL`, and `V9XGL.DLL`.
- `V9XCOPY.BAT` demonstrates the offline-DOS replacement path and
  `V9XFIX.BAT` demonstrates delayed replacement through `WININIT.INI`.

The updater must use the family manifests and package output as its inputs. It
must not introduce another hand-maintained list of families, hardware IDs, or
versions.

## Release metadata

Publish a small, ASCII, machine-readable channel index separately from the
human release page. The release builder emits it from `packages.json`, then
signs it. Each channel entry contains:

- schema version and monotonically increasing release sequence;
- channel (`stable` initially; test channels may follow);
- product version and build identifier;
- minimum updater version;
- supported family and exact PCI hardware-ID set;
- package URL, byte length, and SHA-256;
- an exact installed-runtime file set, with destination, size, SHA-256, and
  embedded build identifier for each file;
- INF hash and any required registry migration identifier;
- release-notes and recovery-notes URLs; and
- an explicit revocation list for bad release sequences or package hashes.

Sign the canonical bytes of the index with an offline release key. The checker
ships only the public key. Transport encryption is useful for privacy and
availability, but must not be the authenticity boundary: an unpatched Win98
machine cannot be assumed to negotiate current TLS or possess a current root
store. The Phase 0 spike chooses a small verifier that can be built and audited
with this repository's C89/Open Watcom constraints. No update feature ships
until a modified index, modified package, wrong-family package, replayed older
sequence, revoked release, truncation, and key-rotation fixture are all refused.

Store the greatest accepted release sequence locally. A downgrade requires an
explicit manual override and is never selected by an automatic policy. Do not
make correctness depend on the machine's wall clock, which is often wrong on
retro hardware.

## Local installed-version receipt

After a successful INF install or updater commit, keep a compact signed-release
receipt outside the package staging directory. It records:

- family, hardware ID, product version, build identifier, and release sequence;
- the expected five-file runtime set and hashes;
- the installed INF hash and registry migration identifier;
- previous receipt and backup location; and
- transaction state: downloaded, verified, staged, booted, committed, or
  rolled back.

The remote signature remains attached to the receipt. Local state may select a
signed release but may not redefine its files or hashes. If an old hand install
has no receipt, the audit reports `BASELINE UNKNOWN`; it may show embedded build
identifiers, but it must not claim integrity until the matching signed release
metadata has been obtained or a package has been selected locally.

## Installed-file audit

Expose the audit in the settings utility and as a command-line mode suitable
for support (`V9XUPD.EXE /AUDIT`). It is read-only and works with networking
disabled.

For every file in the receipt's runtime set, report:

- missing, expected, modified, wrong version/build, or unreadable;
- expected and actual size and SHA-256;
- expected and discovered embedded build identifier; and
- whether a replacement is already pending in `WININIT.INI`.

Also enumerate `V9X*.DRV`, `V9X*.VXD`, and `V9X*.DLL` in the system directory.
Files outside the signed runtime set are reported as `UNEXPECTED`, never
silently deleted. This catches stale components while avoiding the false claim
that package-only tools such as `V9XDDP.EXE` must be installed. The summary is
one of:

- `MATCH`: exact set and hashes match the receipt;
- `MIXED`: recognised Velocity9x files come from different builds;
- `MODIFIED`: the expected version is named but one or more hashes differ;
- `INCOMPLETE`: an expected runtime file is absent or unreadable;
- `PENDING`: a staged update makes the on-disk set intentionally transitional;
- `BASELINE UNKNOWN`: no trusted expected set is available.

Write the full result to `C:\V9XDIAG\V9XUPDATE.INI` using the existing bounded
diagnostic-writing conventions. The settings page shows only the summary,
installed version/build, and a button to open the utility; network and update
work must not run inside the Display Properties process.

An audit compares hashes, not just version strings. Matching version resources
or embedded strings cannot prove that all driver files came from one package.

## Update check and policy

`V9XUPD.EXE /CHECK` reads the installed family and hardware ID from the current
driver status, downloads only the signed channel index, verifies it, applies
revocations, and selects the newest compatible release. It never selects a
generic package merely because its version is newer.

Configuration is explicit and independently switchable:

- `EnableChecks=0|1` (default `0`);
- `CheckIntervalDays` with a conservative default when checks are enabled;
- `Channel=stable`;
- `AutoDownload=0|1` (default `0`);
- `AutoInstall=0|1` (default `0` and unavailable until the install gates pass);
- `IncludePrerelease=0|1` (default `0`); and
- `AllowMetered` is not needed on Win98; a check has a documented byte bound.

Periodic checking uses a visible Startup/Task Scheduler entry created only by
the user's opt-in. Failures are quiet apart from status/logging; success may
offer the release notes and update action. Retry uses a bounded interval and
never delays boot or desktop startup.

## Download and verification

Download into a versioned directory outside the Windows system directory. Use
temporary names and resume only when the server and local partial-file metadata
agree. Before staging anything:

1. verify the signed channel index;
2. confirm family and exact hardware-ID compatibility;
3. enforce release sequence and minimum-updater rules;
4. enforce a maximum package and per-file size;
5. verify the complete package hash;
6. unpack into a new directory with fixed filenames, rejecting absolute paths,
   `..`, duplicate/case-colliding names, links, devices, and undeclared files;
7. verify every extracted file against the signed installed-file list; and
8. run the existing no-install preflight against the staged display driver and
   mini-VDD pair.

Phase 0 decides the transport container. Prefer a format that has a small,
bounded Win98 implementation and no shell dependency. Do not depend on a
browser, modern TLS library already being installed, or a general-purpose
self-extracting executable.

## Guarded installation transaction

The updater does not overwrite loaded display files. Its state machine is:

1. **Preflight**: require the exact supported adapter, sufficient free space,
   no existing `WININIT.INI` work, no other update transaction, and an audit of
   the current installation.
2. **Recover**: copy the current five runtime files, receipt, relevant registry
   keys, and recovery instructions to a versioned backup. Hash the backup and
   prove it can be read. Refuse automatic installation if no standard-VGA or
   tested offline restore path is available.
3. **Stage**: copy all five new runtime files to unique temporary names, verify
   them again, then append one bounded, fully parsed rename transaction to
   `WININIT.INI`. Preserve unrelated valid entries rather than replacing the
   file. Stage registry changes separately and record exactly when they apply.
4. **Consent**: show old/new versions, family, audit result, backup location,
   release notes, and that the next boot may require recovery. Installation and
   reboot remain separate choices.
5. **Boot**: on the next boot, existing driver diagnostics must report the new
   version/build pair. A startup finisher audits all five installed hashes and
   checks the display-driver readiness evidence before committing the receipt.
6. **Commit or recover**: a complete match commits and retires only temporary
   files. A mismatch records `INCOMPLETE`, stops further automatic action, and
   offers the offline restore procedure. Do not attempt a second live rewrite
   of a partially loaded display stack.

The first implementation is interactive `Download and stage`. `AutoInstall`
stays compile-time disabled until the same transaction has survived the guest
and physical-machine gates below. Even after enablement it may stage an update
automatically, but it must notify the user before reboot and retain recovery.

Updating must replace the entire signed runtime set even when one hash already
matches. This makes the post-boot state all-or-nothing and prevents a plausible
but unsupported mixed build. Package utilities and diagnostics are updated in
the download cache, not copied into the system directory.

## Implementation boundaries

- New user-mode `tools/diag` code owns UI, policy, network transfer, hashing,
  signature verification, receipts, and transaction logs. No network or update
  policy enters the 16-bit display driver, 32-bit HAL, mini-VDD, or OpenGL ICD.
- Pure parsing, version ordering, manifest selection, path validation, and
  transaction planning live in host-testable C modules.
- A narrow Win32 layer supplies file, registry, downloader, and reboot-staging
  operations. Every write operation has a dry-run representation that tests can
  compare.
- Release scripts generate and sign metadata; the private signing key is never
  committed and is not required for ordinary developer builds. Test keys and
  deterministic fixtures are committed for host tests only.
- `scripts/check-tree.ps1` asserts that the INF runtime copy set, package
  runtime set, updater manifest set, and recovery copy set stay identical.

## Phases and gates

### Phase 0: feasibility and threat-model spike

- Measure candidate networking and signature-verification code on the oldest
  supported Win98SE image and Open Watcom toolchain.
- Choose the signed-index canonical form and bounded package container.
- Record code size, memory use, download behavior, proxy behavior, and failure
  behavior with no network and a wrong system clock.

Gate: a decision record selects the mechanisms and contains passing tamper,
replay, wrong-family, revocation, and malformed-container evidence. Otherwise
the supported design becomes offline package selection plus audit only.

### Phase 1: release metadata and offline audit

- Generate deterministic channel metadata from `packages.json`.
- Add signing as a release-only step and public-key verification fixtures.
- Implement receipt import from a selected local package and `/AUDIT`.
- Add the settings-page summary and diagnostic report.

Gate: host tests cover every audit state, and each current family package
produces `MATCH` only against its own five installed runtime files. No network
or installation occurs in this phase.

### Phase 2: opt-in update checking

- Implement manual `/CHECK`, compatible-family selection, release notes, and
  persisted opt-in settings.
- Add the optional periodic launcher with bounded retries.

Gate: a controlled server fixture proves no request occurs before opt-in; the
checker refuses bad signatures, older sequences, revoked builds, wrong hardware,
oversized responses, redirects outside policy, and interrupted metadata.

### Phase 3: verified download

- Add resumable download, bounded unpacking, full-file verification, cache
  cleanup, and the staged-pair preflight.
- Offer `Download` and `Download and stage` separately.

Gate: power loss or process termination at every download/unpack boundary
leaves the installed driver unchanged and either a resumable partial or a
deletable cache. Fuzzed paths cannot write outside the new staging directory.

### Phase 4: interactive guarded install

- Implement backup, complete five-file `WININIT.INI` transaction, boot
  finisher, commit, and documented offline restore.
- Keep automatic installation disabled.

Gate: on a cold-copy Win98SE VM for every install model (INF and guarded Matrox
replacement), complete 20 update cycles including same-version repair,
new-version update, refused downgrade, interrupted staging, deliberately
corrupted download, deliberately mixed installed files, and rollback. Each
successful cycle ends with a hash `MATCH`, the expected build in driver
diagnostics, working Display Properties, DirectDraw probe, OpenGL probe, clean
restart/shutdown, and no leftover pending rename.

### Phase 5: opt-in automatic staging

- Enable `AutoDownload`, then `AutoInstall`, as two separate rollout changes.
- Automatic installation may prepare and stage only after a clean audit or a
  user-approved repair; it never automatically reboots.

Gate: repeat the Phase 4 matrix on the physical S3 and each other family before
  enabling that family in signed metadata. Induce a failed first boot and prove
  the documented standard-VGA/offline restore from the retained backup. A
  family with no measured recovery result remains check/download-only.

## Required host tests

- Strict parser limits, duplicate fields, unknown required fields, integer
  overflow, malformed UTF/ASCII, and canonical-signature bytes.
- Semantic version comparison plus release-sequence precedence; no string sort.
- Exact family/hardware-ID selection and no compatible candidate.
- Signature success/failure, test-key rotation, revocation, rollback, package
  hash mismatch, file hash mismatch, size mismatch, and unexpected files.
- Case-insensitive DOS/Windows filename collisions, reserved names, traversal,
  absolute paths, and overlong paths.
- Audit state table, including locked/unreadable files and a pending rename.
- Transaction planning with pre-existing `WININIT.INI` content, insufficient
  space, partial backup, partial staging, repeated invocation, and reboot resume.
- Equality of the five runtime-file authorities listed under implementation
  boundaries.

## Documentation and release work

- Add an updater section to `docs/INSTALL.md` with defaults, consent boundaries,
  bandwidth, proxy limitations, recovery, and how to disable/remove scheduling.
- Add an audit/support section explaining all six results and attach
  `V9XUPDATE.INI` to reports.
- Add release-key rotation and revocation procedures to the private release
  runbook without placing private key material in the repository.
- Release notes state separately whether a family is check-only,
  check-and-download, interactive-install, or automatic-stage qualified.

## Non-goals

- Updating Windows, DirectX, certificates, browsers, or unrelated drivers.
- Installing a package for an unlisted PCI ID or changing the selected family.
- Silent telemetry, inventory upload, crash upload, or unique client IDs.
- Peer-to-peer distribution, delta patches, or updating directly from a source
  checkout.
- Automatic reboot, automatic deletion of unexpected files, or hiding a failed
  integrity check behind a version string.

