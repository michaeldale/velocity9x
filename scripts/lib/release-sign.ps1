# Write and sign a release folder's SIGNED.TXT.
#
# The file lists each family zip's name, size and SHA-256 under an Ed25519
# signature made with the offline release key. V9XUPD.EXE verifies it
# against the public key compiled into it (include\velocity9x\release_key.h)
# before it believes any hash the update server sends: plain HTTP and
# server-supplied hashes alone cannot tell a substituted zip from a genuine
# one (docs\plans\optional-update-checker-and-auto-updater.md). The format
# and its rules are in include\velocity9x\update_release.h.
#
# The private key is V9X_SIGNING_KEY: from the environment, else from the
# developer's .env beside this checkout. It is passed to v9xsign through the
# environment, never on a command line, and cleared afterwards.
function Write-V9xSignedRelease {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$ReleaseDir,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$BuildId,
        # Objects with .Family and .Name (the zip's file name in ReleaseDir).
        [Parameter(Mandatory = $true)][object[]]$Packages,
        [string]$EnvFile
    )

    & (Join-Path $RepoRoot "scripts\build-release-tools.ps1") | Out-Null
    $signTool = Join-Path $RepoRoot "build\release-tools\v9xsign.exe"

    $keyFromEnvironment = [bool]$env:V9X_SIGNING_KEY
    if (-not $keyFromEnvironment) {
        if (-not $EnvFile) {
            $EnvFile = $env:V9X_SIGNING_ENV_FILE
        }
        if ($EnvFile -and (Test-Path -LiteralPath $EnvFile)) {
            $keyLine = @(Get-Content -LiteralPath $EnvFile |
                Where-Object { $_ -match '^\s*V9X_SIGNING_KEY\s*=' })
            if ($keyLine.Count -eq 1) {
                $env:V9X_SIGNING_KEY = ($keyLine[0] -split '=', 2)[1].Trim()
            }
        }
    }
    try {
        if ($env:V9X_SIGNING_KEY -notmatch '^[0-9a-fA-F]{64}$') {
            throw ("No usable V9X_SIGNING_KEY (64 hex digits) in the " +
                   "environment or $EnvFile. A release has to be signed, or " +
                   "no installed updater will accept it.")
        }

        # The key must be the one shipped updaters trust, or every client
        # refuses the release.
        $headerText = Get-Content -LiteralPath (Join-Path $RepoRoot `
            "include\velocity9x\release_key.h") -Raw
        if ($headerText -notmatch 'V9X_RELEASE_PUBLIC_KEY_HEX\s*\\\s*"([0-9a-f]{64})"') {
            throw "include\velocity9x\release_key.h has no V9X_RELEASE_PUBLIC_KEY_HEX."
        }
        $publicKey = $Matches[1]
        $derived = (& $signTool public env | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $derived -ne $publicKey) {
            throw ("V9X_SIGNING_KEY does not derive the public key in " +
                   "release_key.h ($publicKey). Refusing to sign with it.")
        }

        $lines = @(
            "[Velocity9xRelease]",
            "Schema=1",
            "App=velocity9x",
            "Version=$Version",
            "Build=$BuildId",
            ""
        )
        foreach ($package in $Packages) {
            $zipPath = Join-Path $ReleaseDir $package.Name
            $lines += "[Family.$($package.Family)]"
            $lines += "File=$($package.Name)"
            $lines += "Size=$((Get-Item -LiteralPath $zipPath).Length)"
            $lines += "Sha256=" + (Get-FileHash -Algorithm SHA256 `
                -LiteralPath $zipPath).Hash.ToLowerInvariant()
            $lines += ""
        }
        $body = ($lines -join "`r`n") + "`r`n"
        $bodyPath = Join-Path $RepoRoot "build\release-tools\SIGNED.BODY"
        [System.IO.File]::WriteAllBytes($bodyPath,
            [System.Text.Encoding]::ASCII.GetBytes($body))
        $signature = (& $signTool sign env $bodyPath | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $signature -notmatch '^[0-9a-f]{128}$') {
            throw "v9xsign could not sign SIGNED.TXT."
        }
        & $signTool verify $publicKey $signature $bodyPath | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "SIGNED.TXT's signature does not verify."
        }
        $signedPath = Join-Path $ReleaseDir "SIGNED.TXT"
        [System.IO.File]::WriteAllBytes($signedPath,
            [System.Text.Encoding]::ASCII.GetBytes(
                $body + "[Signature]`r`nEd25519=$signature`r`n"))
        return $signedPath
    } finally {
        if (-not $keyFromEnvironment) {
            Remove-Item Env:\V9X_SIGNING_KEY -ErrorAction SilentlyContinue
        }
    }
}
