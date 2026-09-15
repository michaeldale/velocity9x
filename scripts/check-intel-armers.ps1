# Behavioural self-test for the Intel armers' in-flight guard.
#
# The check-tree rules that pin this guard are source-pattern checks: they
# confirm the right lines are present and the old inert idiom has not returned.
# They cannot tell whether the logic REACHES the right branch, and the two holes
# found in review were both of that kind - a guard that was present, matched,
# and still fell through to the overwrite.
#
# So this runs the shipped batch files against fixture arm files and asserts
# which branch each takes.
#
# Limits, stated plainly. This runs under cmd.exe, not COMMAND.COM, so it
# exercises the guard's LOGIC and not DOS compatibility; a construct cmd.exe
# accepts and COMMAND.COM does not would pass here. The IF /I defect found in
# review was exactly that shape, and this test would not have caught it - the
# check-tree rule forbidding IF /I is what covers that.
[CmdletBinding()]
param([switch]$SelfTest)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# Each case: the IntelIncomplete line to plant, and whether the armer should
# proceed to write the arm block.
$cases = @(
    @{ Name = 'resolved';            Line = 'IntelIncomplete=0'; Proceed = $true },
    @{ Name = 'in flight';           Line = 'IntelIncomplete=1'; Proceed = $false },
    @{ Name = 'empty value';         Line = 'IntelIncomplete=';  Proceed = $false },
    @{ Name = 'malformed value';     Line = 'IntelIncomplete=garbage'; Proceed = $false },
    @{ Name = 'legacy, key absent';  Line = $null;               Proceed = $false },
    @{ Name = 'resolved, variable unset'; Line = 'IntelIncomplete=0'; Proceed = $true; Unset = $true },
    # Both conditions together, which is the only combination that exercises
    # the default assignment's POSITION. With the variable set, the first
    # IF NOT EXIST expands correctly however late the assignment sits; with a
    # file present, that branch is never taken. Needs both.
    @{ Name = 'no arm file, variable unset'; Line = $null; NoFile = $true; Proceed = $true; Unset = $true }
)

$failures = 0
foreach ($armer in @('V9XARM.BAT', 'V9XARM5.BAT')) {
    $source = Join-Path $repoRoot "packaging\win98se\$armer"
    if (-not (Test-Path -LiteralPath $source)) {
        throw "packaging\win98se\$armer is missing."
    }
    foreach ($case in $cases) {
        $dir = Join-Path ([IO.Path]::GetTempPath()) ("v9x-arm-" + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $dir | Out-Null
        try {
            # The armer refuses unless it is beside V9XDISP.DRV, and unless
            # C:\V9XDIAG exists - both are real guards, satisfied here so the
            # case under test is the one that decides.
            Set-Content -LiteralPath (Join-Path $dir 'V9XDISP.DRV') -Value 'stub' -Encoding Ascii
            $armFile = Join-Path $dir 'INTELARM.TXT'
            $lines = @('[Velocity9x]', 'IntelArmOnce=', 'IntelInFlight=')
            if ($case.Line) { $lines += $case.Line }
            if (-not $case.NoFile) {
                Set-Content -LiteralPath $armFile -Value $lines -Encoding Ascii
            }

            # Placeholders are substituted at package build; fill them here so
            # the batch under test is the shape that ships.
            $text = (Get-Content -LiteralPath $source -Raw).
                Replace('@@BUILDID@@', 'selftest').
                Replace('@@ARMCRC@@', 'AAAAAAAA').
                Replace('@@COMBINEDCRC@@', 'BBBBBBBB').
                Replace('@@P4CRC@@', 'CCCCCCCC').
                Replace('@@P5CRC@@', 'DDDDDDDD').
                Replace('@@TOKEN@@', 'selftest-token')
            if ($case.Unset) {
                # The replacement is a literal path, not a pattern: escaping it
                # here put the escaped form INTO the batch.
                $text = $text -replace '(?m)^(IF "%V9XARMFILE%"=="" SET V9XARMFILE=).*$', ('$1' + $armFile)
            }
            # The C:\V9XDIAG existence guard is not what this tests.
            $text = $text -replace '(?m)^IF NOT EXIST C:\\V9XDIAG\\NUL GOTO NODIAG', 'REM (self-test)'

            # A test that can reach a real path is a hazard, not a test. Two
            # IntelArmOnce writes still named the real arm file directly
            # instead of the selected path, and THIS HOST has C:\V9XDIAG -
            # the self-test appended twenty-one tokens to the operator arm
            # file before the defect was reported. Refuse to run a batch that
            # still names the real path anywhere but its default assignment.
            $offenders = @($text -split '\r?\n' | Where-Object {
                $_ -match 'V9XDIAG.INTELARM' -and
                $_ -notmatch 'SET V9XARMFILE=' -and $_ -notmatch '^REM' })
            if ($offenders.Count -ne 0) {
                throw ("$armer reaches the real arm path outside its default " +
                       'assignment: ' + ($offenders -join ' / ') +
                       '. Every read and write must go through %V9XARMFILE%, ' +
                       'or this test writes to the operator arm file.')
            }
            $runner = Join-Path $dir 'RUN.BAT'
            Set-Content -LiteralPath $runner -Value $text -Encoding Ascii

            $log = Join-Path $dir 'RUN.LOG'
            # cd INSIDE cmd. PowerShell's Push-Location changes the shell's
            # location, not the child process's working directory, so the
            # armer's relative "IF NOT EXIST V9XDISP.DRV" was resolving against
            # whatever directory PowerShell happened to start in - which made
            # the result depend on where the gate was run from.
            # The unset case is how every real DOS run enters: the variable
            # does not exist and the batch falls back to its own default. It
            # is exercised by pointing that default at the fixture rather
            # than by letting it resolve to the operator arm file - which is
            # what the hazard guard above exists to prevent.
            #
            # This is what catches the assignment sitting AFTER its first
            # use: the first IF NOT EXIST then expands to nothing, and the
            # run refuses instead of writing.
            if ($case.Unset) {
                $env:V9XARMFILE = $null
            } else {
                $env:V9XARMFILE = $armFile
            }
            # Redirect INSIDE cmd, never with PowerShell 2>&1: redirecting a
            # native command's stderr wraps each line in an ErrorRecord, which
            # $ErrorActionPreference=Stop turns into a failure even when the
            # command succeeded. FIND writes to stderr when it finds nothing,
            # which is the normal case here.
            # System32 FIRST on PATH. This machine has Git Bash on PATH and
            # its find.exe shadows Windows FIND.EXE, treating the search
            # string as a path - "No such file or directory". Every case then
            # refused because the search failed rather than because the guard
            # worked, so four of five passed for the wrong reason. The batch
            # itself is fine: DOS has exactly one FIND.
            $sys32 = Join-Path $env:SystemRoot 'system32'
            $cmdLine = "cd /d ""$dir"" && set ""PATH=$sys32"" && ""$runner"" < NUL > ""$log"" 2>&1"
            & cmd.exe /c $cmdLine | Out-Null
            $env:V9XARMFILE = $null
            $out = Get-Content -LiteralPath $log -Raw

            # Assert on the BRANCH TAKEN, not on the file contents.
            #
            # cmd.exe parses `ECHO Key=0>>file` as a redirect of handle 0, so
            # the trailing digit never reaches the file and the write path
            # produces different output here than on the target. COMMAND.COM
            # does not do this - the netbook's arm file after V9XCOPY is
            # exactly 37 bytes, "[Velocity9x]" plus "IntelEnableThisBoot=0",
            # with the 0 present. So the batch is correct where it runs, and
            # this test must not be used to "fix" it to suit cmd.exe.
            $wrote = $out -notmatch 'FAIL:' -and $out -notmatch 'REFUSED:'
            if ($wrote -ne $case.Proceed) {
                $expected = if ($case.Proceed) { 'write' } else { 'REFUSE' }
                $actual = if ($wrote) { 'wrote' } else { 'refused' }
                Write-Output ("  FAIL {0}: {1} - expected {2}, {3}" -f
                              $armer, $case.Name, $expected, $actual)
                Write-Output ("       log: " + ($out -replace '?
', ' | '))
                ++$failures
            }
        } finally {
            Remove-Item -Recurse -Force -LiteralPath $dir -ErrorAction SilentlyContinue
        }
    }
}

if ($failures -ne 0) {
    throw "Intel armer guard self-test: $failures case(s) behaved wrongly."
}
Write-Output ("Intel armer guard self-test passed (2 armers x " +
              "$($cases.Count) arm-file states; refuses everything but an " +
              "explicit IntelIncomplete=0).")
