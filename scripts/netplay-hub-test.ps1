<#
.SYNOPSIS  Online hub end to end on one PC: a local relay and 2..4 copies of the game in one room.
.DESCRIPTION
  Every copy enters the hub with RECOMP_NETPLAY=quick, is put into the same room by quick match,
  readies with its own fighter and votes; the room's match starts by itself on every copy and the
  harness routes online-p1 / online-p2 fight it, go through the summary and fight the second match.
  Each copy past the first gets its own data folder beside the main one (<data>-hub<N>: the dump
  as a junction, a copy of the saves). Prints the [NETPLAY] and [AUTOMATCH] lines of every log.
  Copies need the real input path (virtual pads): the harness's plain scripted pad is one device
  only, and the game would wait for the missing players' controllers.
.PARAMETER Players  2..4 (3 and 4 play Free for All).
.PARAMETER Secs     Upper bound per copy.
.PARAMETER ExtraEnv Extra NAME=VALUE pairs for every copy.
.PARAMETER Route    harness route family: online (two matches) or online5 (five in a row).
.PARAMETER Fighters fighter.xml IDs, one per copy.
.PARAMETER Stage    extra harness stages for every copy (e.g. a START press during the fight).
#>
param([ValidateRange(2, 4)][int]$Players = 2, [int]$Secs = 1200, [string[]]$ExtraEnv = @(),
      [ValidateSet('online', 'online5', 'online-rematch')][string]$Route = 'online', [int[]]$Fighters = @(55, 22, 9, 31),
      [string[]]$Stage = @())
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location $repo
if (Get-Process defjam_recomp -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
$data = if ($env:DEFJAM_DATA) { $env:DEFJAM_DATA } else { 'D:\DefJam\data' }
$logs = Join-Path $repo 'logs\hub-test'
New-Item -ItemType Directory -Force $logs | Out-Null

$relay = Start-Process (Join-Path $repo 'netplay\relay\dist\relay.exe') -ArgumentList '-listen', '127.0.0.1:27777', '-log-interval', '30s' `
         -PassThru -WindowStyle Hidden -RedirectStandardError "$logs\relay.err" -RedirectStandardOutput "$logs\relay.out"
Start-Sleep -Seconds 1

$quick = if ($Players -eq 2) { '0,2' } else { "3,$Players" }
$fighters = $Fighters
$procs = @()
for ($i = 0; $i -lt $Players; $i++) {
    $root = $data
    if ($i -gt 0) {
        $root = "$($data.TrimEnd('\'))-hub$i"
        if (-not (Test-Path "$root\extracted")) {
            New-Item -ItemType Directory -Force $root | Out-Null
            New-Item -ItemType Junction -Path "$root\extracted" -Target (Join-Path $data 'extracted') | Out-Null
        }
        robocopy (Join-Path $data 'save') "$root\save" /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
    }
    $copyRoute = if ($i % 2 -eq 0) { "$Route-p1" } else { "$Route-p2" }
    $args = @('scripts/harness.py', 'run', $copyRoute, '--preset', 'win-x64-release', '--secs', "$Secs",
              '--env', 'RECOMP_NETPLAY=quick', '--env', 'RECOMP_NETPLAY_SERVER=127.0.0.1',
              '--env', "RECOMP_NETPLAY_QUICK=$quick", '--env', "RECOMP_NETPLAY_FIGHTER=$($fighters[$i])",
              '--env', 'RECOMP_TEST_OBSERVATIONS=1',
              '--env', 'RECOMP_INPUT_HOST=1', '--env', 'RECOMP_INPUT_IGNORE_FOCUS=1', '--env', 'RECOMP_PAD_HOST=1',
              '--env', 'RECOMP_INPUT_NO_PADS=1', '--env', 'RECOMP_INPUT_VIRTUAL_PADS=1', '--env', 'RECOMP_PAD_SCRIPT_VIRTUAL=1')
    if ($i -gt 0) { $args += @('--env', 'RECOMP_AUDIO_MUTE=1') }
    # Never the live account service: test results would land on the real leaderboard.
    if (-not ($ExtraEnv -match '^RECOMP_ACCOUNT_SERVER=')) { $args += @('--env', 'RECOMP_ACCOUNT_SERVER=127.0.0.1:9') }
    foreach ($e in $ExtraEnv) { $args += @('--env', $e) }
    foreach ($st in $Stage) { $args += @('--stage', $st) }
    $env:DEFJAM_DATA = $root
    $procs += Start-Process python -ArgumentList $args -PassThru -WindowStyle Hidden `
              -RedirectStandardOutput "$logs\copy$i.txt" -RedirectStandardError "$logs\copy$i.err"
    Start-Sleep -Seconds 3       # the first copy opens the room and is player 1
}
$env:DEFJAM_DATA = $data
foreach ($p in $procs) { $p.WaitForExit(($Secs + 300) * 1000) | Out-Null }
foreach ($p in $procs) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } }
Get-Process defjam_recomp -ErrorAction SilentlyContinue | Stop-Process -Force
if (-not $relay.HasExited) { $relay.Kill() }

for ($i = 0; $i -lt $Players; $i++) {
    $log = (Select-String -Path "$logs\copy$i.txt" -Pattern '^log\s+(\S+)' | Select-Object -First 1).Matches.Groups[1].Value
    "== copy $i  $log"
    Get-Content "$logs\copy$i.txt" | Select-Object -First 4 | Select-Object -Skip 1
    if ($log -and (Test-Path $log)) {
        Select-String -Path $log -Pattern '\[NETPLAY\] (START|match|sync|Match over|A player|refused|desync)|\[AUTOMATCH\] sequence|"event":"(match_over|result)"|CRASH' |
            ForEach-Object { $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
        Select-String -Path $log -Pattern 'checksums compared' | Select-Object -Last 1 | ForEach-Object Line
    }
}
"relay:"; Get-Content "$logs\relay.err", "$logs\relay.out" -ErrorAction SilentlyContinue | Select-String 'room|START|summary' | Select-Object -Last 12 | ForEach-Object Line
