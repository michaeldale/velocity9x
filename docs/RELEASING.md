# Releasing Velocity9x

From a tested `main` to a release people can download from GitHub and that
installed copies can update to with **Check for updates...**. The order
matters: the update server mirrors the GitHub release, and the updater
trusts only what `SIGNED.TXT` in that release says.

Building itself is in [BUILDING.md](BUILDING.md); the updater's design is in
[plans/optional-update-checker-and-auto-updater.md](plans/optional-update-checker-and-auto-updater.md).

## What you need

- The build toolchain from BUILDING.md (Open Watcom, the Windows 98 DDK).
- `gh`, signed in with permission to create releases on
  `michaeldale/velocity9x`.
- The **release signing key**: a 64-hex-digit Ed25519 seed whose public half
  is `V9X_RELEASE_PUBLIC_KEY_HEX` in `include/velocity9x/release_key.h`.
  It is never committed. Provide it one of three ways:
  - `V9X_SIGNING_KEY` set in the environment;
  - `V9X_SIGNING_ENV_FILE` naming a private `KEY=value` file that holds a
    `V9X_SIGNING_KEY=` line;
  - `build-release.ps1 -EnvFile <that file>`.

  `build-release.ps1` refuses to sign unless the key derives exactly the
  public key in `release_key.h`.
- Admin access to the update server (the `v9x_update_checker` plugin), either
  through its MCP tools (`updates_*`) or its admin page, **Updates >
  Releases**.

## 1. Finish the changelog

The version in `include/velocity9x/build.h` is already the one being
released; it was bumped when the previous release went out (step 8).

- In `CHANGELOG.md`, change `## X.Y.Z - not yet released` to
  `## X.Y.Z - YYYY-MM-DD`, and check that every user-visible change since the
  last release is listed with its evidence.
- In `README.md`, update the **Current version** paragraph and its
  `releases/X.Y.Z` link.

Commit: `Date the X.Y.Z changelog for release`.

## 2. Build and gate from a clean tree

```powershell
./scripts/run-checks.ps1
./scripts/build-vga-survey.ps1
```

`run-checks.ps1` builds every family package (`build-all-packages.ps1`) and
must pass. The tree must be clean: a `-dirty` build id is refused in step 3.

## 3. Build the release folder

```powershell
./scripts/build-release.ps1
```

This writes `releases/X.Y.Z/`:

- one `velocity9x-X.Y.Z-<family>.zip` per family, plus
  `velocity9x-survey-X.Y.Z.zip`;
- `SHA256SUMS.txt` over the zips;
- `SIGNED.TXT`: each family zip's name, size and SHA-256 under the release
  key's Ed25519 signature, already verified by the script;
- `README.md`, the per-version index, and an updated `releases/README.md`.

It compiles nothing and refuses if `build/packages.json` disagrees with
`build.h` or was built from a dirty tree. A published version folder is
fixed: `-Force` replaces it, and is only for a version that has not gone out.

## 4. Commit the downloads

Commit `releases/X.Y.Z/` and `releases/README.md`:
`Publish the X.Y.Z downloads`, naming the commit the packages were built
from. Push `main`.

## 5. Create the GitHub release

Tag the publish commit and attach every zip, `SHA256SUMS.txt` and
`SIGNED.TXT`:

```powershell
$v = "X.Y.Z"
gh release create "v$v" --repo michaeldale/velocity9x --target main `
    --title "Velocity9x $v - <one-line headline>" `
    --notes-file <notes.md> `
    (Get-ChildItem "releases/$v/*.zip").FullName `
    "releases/$v/SHA256SUMS.txt" "releases/$v/SIGNED.TXT"
```

The notes follow the previous releases: one paragraph on what the release
is, `Built from <commit>`, a link to `CHANGELOG.md` at the tag, and the
"read before installing" warning about what was and was not tested from
these archives. Mark it `--prerelease` only for a beta: the update server
then puts it on the beta channel, which installed updaters do not ask for.

Zip names must stay `velocity9x-<version>-<family>.zip`: the server takes
the family from the name. A family renamed since an earlier release (0.14.0
called the Matrox package `matrox-m2`) needs an alias on the server
(**Updates > Apps**, `matrox-m2=matrox`).

## 6. Mirror it to the update server

Through MCP:

```
updates_publish_github_release(app: "velocity9x", tag: "vX.Y.Z")
```

or **Updates > Releases > Fetch from GitHub** in the admin page. The import
downloads every zip, checks each against `SHA256SUMS.txt` and GitHub's own
digest, takes `SIGNED.TXT` (the asset, else the committed copy at the tag),
and **refuses the whole release** if a zip's signed SHA-256 differs from the
one it mirrors. Read the warnings it returns: "No SIGNED.TXT" means installed
updaters cannot update to this release.

## 7. Check what clients will see

```bash
curl "http://michaeldale.com.au/v9update/check?app=velocity9x&version=<previous>&family=s3"
```

expect `status=update`, `latest=X.Y.Z` and a `signed=` line. Then confirm
the server serves the signed file and a package byte for byte:

```powershell
$v = "X.Y.Z"
Invoke-WebRequest "http://michaeldale.com.au/v9update/signed/velocity9x/$v" -OutFile signed.txt
(Get-FileHash signed.txt).Hash -eq (Get-FileHash "releases/$v/SIGNED.TXT").Hash
```

and the same for one zip's `url=` from the check reply against its file in
`releases/$v/`. Finally, on a machine running the previous release, click
**Check for updates...** on the Velocity9x tab and take it through to the
restart. Updaters exist from 0.15.0 on; anything older updates by hand once.

## 8. Open the next version

Bump `include/velocity9x/build.h` (the string and the three numbers) and add
`## <next> - not yet released` to `CHANGELOG.md`. Commit:
`Bump the version to <next>, unreleased: ...`.

## Things not to do

- Never sign with the test key (`release-sign.ps1 -TestKey`,
  `build-update.ps1 -TestKey`). They exist for local update-cycle fixtures;
  anyone can sign with the test key.
- Never commit the signing key or a file that holds it.
- Never replace a published zip. Publish a new version: installed updaters
  refuse anything that is not newer, and changed bytes under an old version
  break every checksum already handed out.
- Replacing the release key means every installed updater refuses releases
  signed with the new key. Ship the new public key in a release still signed
  with the old one first.
