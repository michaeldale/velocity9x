# Update checker, updater and diagnostic report submission

Date: 2026-09-27, rewritten 2026-10-09

Status: phases 1-5 implemented. Gates passed: report submission (V9X-AF9SKW),
host tests, and three update cycles on the ViRGE guest against a local
fixture (docs/decisions/2026-10-09-updater-cycles-on-the-virge-guest.md).
Open: the physical-machine cycle, deploying the plugin's SIGNED.TXT change,
and the first signed release. The 2026-09-27 version of this plan (a signed channel
index, periodic checks, `AutoInstall`, 20-cycle gates per install model) is
superseded by the decisions below, made with Michael on 2026-10-09.

## Outcome

Three things a Win98 user can do from Display Properties without copying a
file or typing anything:

1. **Check for updates** (Velocity9x tab): see whether a newer release exists
   for the installed family.
2. **Update** (same flow): download, verify, install and restart, in place of
   reinstalling through Device Manager.
3. **Send diagnostics** (Velocity9x Advanced tab): run `V9XTRACE.EXE` and send
   its snapshot and the other `C:\V9XDIAG` files to the update server, getting
   back a report code to quote in a GitHub issue.

The server side is the `v9x_update_checker` Bluetrait plugin at
`http://michaeldale.com.au/v9update/` (its `README.md` and
`docs/REPORT-SUBMISSION.md` are the protocol). Nothing happens without a
click: no background checks, no scheduled task, no automatic install, no
automatic upload, no client ID.

## Decisions (2026-10-09)

- **Manual only.** One button each. No periodic checks, no `AutoInstall`,
  no beta channel switch. Stable channel.
- **Authenticity is an Ed25519 signature, not the transport.** Plain HTTP and
  the server's MD5/SHA-256 lines protect against a corrupt download, not a
  substituted one. `build-release.ps1` writes `SIGNED.TXT` listing the version
  and each family zip's size and SHA-256, and signs it with an offline key. The
  updater ships the public key and refuses anything that does not verify.
- **The private key** lives outside the repository, in the developer's environment or a private `KEY=value` file, as
  `V9X_SIGNING_KEY=<64 hex seed>`, never in the repository. Only a release
  build needs it.
- **Downgrade protection is the signed version**, compared numerically against
  the updater's own compiled-in version. The server's `sequence=` is assigned
  at publish time, so it cannot be signed and is not trusted.
- **No third-party code.** SHA-256, SHA-512, Ed25519 (verify, plus sign for
  the host tool), CRC-32, Deflate decoding and the zip reader are written here
  from FIPS 180-4, RFC 8032, RFC 1951 and APPNOTE, and host-tested against the
  standards' own vectors and every committed release zip.
- **The updater applies the INF, not just the files.** The INF changes in about
  half of all releases (mode tables live in it), so a files-only update would
  drift from what Device Manager would install. `V9XUPD.EXE` interprets the
  subset of INF our generator emits: the installed model's `CopyFiles`,
  `DelReg` and `AddReg`, with `HKR` bound to this machine's display driver key.
  `check-tree.ps1` fails if the generator emits any directive outside that
  subset.
- **`V9XTRACE.EXE` and `V9XUPD.EXE` are installed** by the INF into the system
  directory beside the driver, so the buttons always have something to launch
  and an update replaces the tools with the driver.
- **Gates are one or two cycles**, not twenty: the UTM Win98 guest and one
  physical machine.
- **Old installs** (0.15.0 and earlier) update by hand once to the first
  release that carries the updater.
- **File audit**: optional; the updater hashes installed files anyway, and
  `V9XUPD.INI` records what it found.

## Components

| Piece | Where | Notes |
|---|---|---|
| `V9XUPD.EXE` | `tools/diag/update_win32.c` | The only process that touches the network. `/CHECK`, `/REPORT`, `/FINISH`. Launched by the property pages; network work never runs inside Display Properties. |
| HTTP | `tools/diag/update_net_win32.c` | WinInet (`INTERNET_OPEN_TYPE_PRECONFIG`, honours IE proxy) loaded at run time, falling back to raw Winsock HTTP/1.0. |
| Pure logic | `src/common/` | `sha256`, `sha512`, `ed25519`, `crc32`, `inflate`, `zipread`, release-file parsing and version order, URL encoding and HTTP response parsing, the INF-subset interpreter's planner, the `WININIT.INI` planner. Host-tested. |
| Signing tool | `tools/release/v9xsign.c` | Host-only. Derives the public key, signs, verifies. |
| Release step | `scripts/build-release.ps1` | Writes and signs `releases/<version>/SIGNED.TXT`. |
| Server | `v9x_update_checker` | Mirrors `SIGNED.TXT` with the zips and returns a `signed=` URL in every check response. |

## Signed release file

```
[Velocity9xRelease]
Schema=1
App=velocity9x
Version=0.16.0
Build=1a2b3c4

[Family.ati]
File=velocity9x-0.16.0-ati.zip
Size=347696
Sha256=8c48...

[Signature]
Ed25519=<128 hex>
```

The signature covers every byte before the `[Signature]` line. The client
verifies first, then parses only the signed bytes with its own parser (not
`GetPrivateProfileString`, which would also read anything appended after the
signature), and requires exactly one `[Signature]` section with nothing after
it.

## Report flow (`V9XUPD.EXE /REPORT`)

1. Run `V9XTRACE.EXE` from the system directory and wait for it.
2. Gather, in the server brief's order: the newest snapshot (`SnapshotFile=`
   from the dump, else the newest of `V9XSNAP.INI`, `V9XSNA1-7.INI`),
   `V9XTRACE.INI`, `V9XBOOT.INI`, `V9XHW.INI`, `V9XDD.INI`, `V9XGL.LOG`,
   `V9XUPD.INI`. Skip missing files; send the last 512 KB of a longer one.
3. Show the files, sizes and the privacy summary, an optional one-line
   description, Send / Cancel.
4. POST the snapshot, then the rest with `report=`/`key=`, following the
   server's per-status rules.
5. Show the code with a Copy button; record `LastReport=` in
   `C:\V9XDIAG\V9XRPT.INI`. The key is held in memory only.

## Update flow (`V9XUPD.EXE /CHECK`)

1. **Network present?** `InternetGetConnectedState`, then resolve the host.
   Neither: say so and stop.
2. **Consent.** "Check michaeldale.com.au for a newer Velocity9x?"
3. **Check.** `GET /v9update/check?app=velocity9x&version=<compiled>&family=<HKR V9xFamily>`.
   `current`: say so. `update`: fetch `signed=`, verify, and require its
   version to equal `latest=` and exceed the installed version, and its entry
   for this family to match the package's size and SHA-256.
4. **Offer.** Old/new version, release notes, **Update now** / **Cancel**.
5. **Download** to `C:\V9XDIAG\UPDATE\`, resuming with `Range`, and check
   size and SHA-256 against the signed file.
6. **Unpack** the INF and every file its model's `CopyFiles` names; check each
   CRC-32 and the zip name rules.
7. **Preflight.** No pending `WININIT.INI` work of anyone else's; the
   installed model's `InfSection` exists in the new INF; free space.
8. **Back up** the installed copies of every file being replaced, the
   registry values the INF will change, and the live `OEMn.INF`, to
   `C:\V9XDIAG\UPDATE\BACKUP\<old version>\`.
9. **Stage.** New files to 8.3 names in the root of the Windows drive;
   `WININIT.INI` `[Rename]` lines `dest=src` only, never `NUL=` (a `NUL=`
   line deletes even when the rename fails, which is how a 0-byte display
   driver happened on 2026-08-30). Apply `DelReg`/`AddReg`. Replace the live
   `OEMn.INF` (read `InfPath`; never guess from the newest file). Add a
   `RunOnce` entry for `/FINISH`.
10. **Restart now** / **Later**.
11. **Finish** (`/FINISH`, after the reboot): hash every installed file
    against the staged ones and read the driver's build from `V9XBOOT.INI`.
    Match: record success in `V9XUPD.INI`. Mismatch: say so and name the
    backup folder and `RECOVER.TXT`.

`GLIDE2X.DLL` keeps the INF's rule (flag 40): it is not replaced when the
installed copy is newer, so a 3dfx card's own DLL survives.

## Phases and gates

1. **Report submission.** Gate: one report from the UTM guest reaches
   production with every file, the code shown and recorded. Delete the test
   report afterwards.
2. **Crypto, inflate, zip.** Gate: host tests pass the FIPS and RFC 8032
   vectors and the negatives; every zip under `releases/` extracts byte-equal
   to .NET.
3. **Signed release metadata and server.** Gate: `build-release.ps1` writes a
   `SIGNED.TXT` that `v9xsign verify` accepts; the server returns `signed=`
   and serves the file byte-identical.
4. **Check.** Gate: on the UTM guest, `current` and `update` both shown
   correctly; a tampered `SIGNED.TXT` is refused.
5. **Update.** Gate: one update cycle on the UTM guest and one on a physical
   machine end with matching hashes, the new build in `V9XBOOT.INI`, working
   Display Properties and no pending `WININIT.INI`.

## Non-goals

- Silent telemetry, scheduled uploads, crash uploads or unique client IDs.
- Updating Windows, DirectX, browsers or another vendor's driver.
- Changing the installed family, or installing for an unlisted PCI ID.
- Automatic reboot, or deleting files the updater did not put there.
