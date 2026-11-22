param([string]$TargetHost = '10.0.1.254', [int]$Minutes = 40)
# Watches a running 3DMark 2001 SE in one-minute agent-side rounds: stops on
# a fault, an exit, or a dialog that is not the NETGEAR wizard (the score
# dialog is one), and prints the window list whenever it changes.
$ctl = 'C:\everything\v9x-remote-agent\scripts\v9xctl.ps1'
$h = @{ Host = $TargetHost }
$deadline = (Get-Date).AddMinutes($Minutes)
$last = ''
while ((Get-Date) -lt $deadline) {
    $f = & $ctl wait-fault @h -Name '3DMark2001SE.exe' -WaitSeconds 60 -Json 2>$null | Out-String
    $code = $LASTEXITCODE
    if ($code -eq 0 -or $code -eq 50) {
        "$(Get-Date -Format HH:mm:ss) wait-fault exit $code : " + ($f -replace '\s+', ' ').Substring(0, [Math]::Min(400, ($f -replace '\s+', ' ').Length))
        break
    }
    $wins = @()
    try {
        $wins = @((& $ctl windows @h -Json | ConvertFrom-Json).Windows)
    } catch {
        "$(Get-Date -Format HH:mm:ss) windows failed: $($_.Exception.Message)"
        continue
    }
    $text = ($wins | ForEach-Object { "$($_.Title) [$($_.Class)]" }) -join ' | '
    if ($text -ne $last) {
        "$(Get-Date -Format HH:mm:ss) $text"
        $last = $text
    }
    $dialog = $wins | Where-Object { $_.Class -eq '#32770' -and $_.Title -notmatch 'NETGEAR' }
    if ($dialog) {
        "$(Get-Date -Format HH:mm:ss) DIALOG: " + (($dialog | ForEach-Object { $_.Title }) -join ' | ')
        break
    }
}
"$(Get-Date -Format HH:mm:ss) watch done"
