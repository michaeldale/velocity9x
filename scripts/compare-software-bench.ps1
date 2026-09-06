[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Baseline,
    [Parameter(Mandatory = $true)][string]$Candidate
)

$ErrorActionPreference = 'Stop'
function Read-SoftwareBench {
    param([string]$Path)
    $values = @{}
    $inSection = $false
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\[(.+)\]$') {
            $inSection = $Matches[1] -eq 'SoftwareBench'
        } elseif ($inSection -and $line -match '^([^=]+)=(.*)$') {
            $values[$Matches[1]] = $Matches[2]
        }
    }
    if ($values.Result -ne 'PASS' -or $values.SchemaVersion -ne '1') {
        throw "Incomplete or unsupported benchmark: $Path"
    }
    return $values
}

function Get-BenchNumber {
    param([hashtable]$Values, [string]$Key)
    [uint32]$number = 0
    if (-not $Values.ContainsKey($Key) -or
        -not [uint32]::TryParse($Values[$Key], [ref]$number)) {
        throw "Missing or invalid benchmark value: $Key"
    }
    return $number
}

function Get-BenchMedian {
    param([hashtable]$Values, [string]$Prefix, [string]$Counter)
    $samples = foreach ($sample in 0..2) {
        $ms = Get-BenchNumber $Values "${Prefix}_${sample}_Ms"
        $count = Get-BenchNumber $Values "${Prefix}_${sample}_$Counter"
        if ($ms -eq 0 -or $count -eq 0) { throw "Empty timing sample: $Prefix" }
        [double]$ms / [double]$count
    }
    return @($samples | Sort-Object)[1]
}

$before = Read-SoftwareBench $Baseline
$after = Read-SoftwareBench $Candidate
foreach ($key in 'Width', 'Height', 'SampleMinimumMs', 'VramAvailable') {
    if ((Get-BenchNumber $before $key) -ne (Get-BenchNumber $after $key)) {
        throw "Benchmark configuration differs: $key"
    }
}
$locations = @('RAM')
if ((Get-BenchNumber $before 'VramAvailable') -eq 1) { $locations += 'VRAM' }
$rows = foreach ($location in $locations) {
    foreach ($scene in 'Small', 'Gouraud', 'Point', 'Bilinear', 'Depth', 'Alpha', 'Read', 'Write') {
        $prefix = "${location}_$scene"
        $memory = $scene -in @('Read', 'Write')
        if (-not $memory) {
            foreach ($suffix in 'Hash', 'ZHash') {
                $key = "${prefix}_$suffix"
                if ((Get-BenchNumber $before $key) -ne (Get-BenchNumber $after $key)) {
                    throw "Pixel regression: $key"
                }
            }
        }
        $counter = if ($memory) { 'Passes' } else { 'Frames' }
        $oldMs = Get-BenchMedian $before $prefix $counter
        $newMs = Get-BenchMedian $after $prefix $counter
        [pscustomobject]@{
            Location = $location
            Workload = $scene
            BaselineMs = [Math]::Round($oldMs, 3)
            CandidateMs = [Math]::Round($newMs, 3)
            Speedup = [Math]::Round($oldMs / $newMs, 3)
        }
    }
}
# Validate every hash before emitting a comparison that could look successful.
$rows
