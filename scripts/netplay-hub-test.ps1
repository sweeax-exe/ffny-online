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
.PARAMETER FastKo   tests: the game knocks the loser out at this fight step of every match
                    (RECOMP_TEST_KO_STEP, src/hooks/test_telemetry.c); 0 = off.
.PARAMETER FastKoWinner  the winner's slot for -FastKo, or 'alt' (the default) to alternate per match.
.PARAMETER Bot      tests: each copy's fighter is driven by the state-reading bot (RECOMP_TEST_BOT,
                    src/hooks/test_bot.c) instead of the route's blind presses.
.PARAMETER BotAggression  0..100 for -Bot (default 60).
.PARAMETER RelayPort  the local relay's port (default 27777); another port lets a worktree's test run
                    beside the main checkout's.
.PARAMETER BaseCore first copy's guest CPU; copy i gets BaseCore + 2i (default 2).
.PARAMETER DataPrefix  every copy's data root is <DataPrefix><i> (made like the -hub roots: the dump as a
                    junction, a copy of the saves); default: the data root itself and <data>-hub<i>.
#>
param([ValidateRange(2, 4)][int]$Players = 2, [int]$Secs = 1200, [string[]]$ExtraEnv = @(),
      [ValidateSet('online', 'online5', 'online-rematch')][string]$Route = 'online', [int[]]$Fighters = @(55, 22, 9, 31),
      [string[]]$Stage = @(), [int]$FastKo = 0, [string]$FastKoWinner = 'alt', [switch]$Bot,
      [ValidateRange(0, 100)][int]$BotAggression = 60, [int]$RelayPort = 27777, [int]$BaseCore = 2,
      [string]$DataPrefix = '')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location $repo
# the owner's own game (installed elsewhere) or this checkout's build (an earlier test) must be closed; a
# test of another checkout of this repository (the main one or a worktree under .claude\worktrees, on its
# own CPUs and relay port) may run beside this one
$main = $repo
$common = (git -C $repo rev-parse --path-format=absolute --git-common-dir 2>$null)
if ($common) { $main = [IO.Path]::GetFullPath((Split-Path $common.Trim() -Parent)) }
if (Get-Process defjam_recomp -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -notlike (Join-Path $main '*') -or $_.Path -like (Join-Path $repo 'build\*') }) {
    throw 'Close the game first.'
}
$data = if ($env:DEFJAM_DATA) { $env:DEFJAM_DATA } else { 'D:\DefJam\data' }
$logs = Join-Path $repo 'logs\hub-test'
New-Item -ItemType Directory -Force $logs | Out-Null

$started = Get-Date
$relay = Start-Process (Join-Path $repo 'netplay\relay\dist\relay.exe') -ArgumentList '-listen', "127.0.0.1:$RelayPort", '-log-interval', '30s' `
         -PassThru -WindowStyle Hidden -RedirectStandardError "$logs\relay.err" -RedirectStandardOutput "$logs\relay.out"
Start-Sleep -Seconds 1

$quick = if ($Players -eq 2) { '0,2' } else { "3,$Players" }
$fighters = $Fighters
$procs = @()
for ($i = 0; $i -lt $Players; $i++) {
    $root = $data
    if ($DataPrefix -or $i -gt 0) {
        $root = if ($DataPrefix) { "$DataPrefix$i" } else { "$($data.TrimEnd('\'))-hub$i" }
        if (-not (Test-Path "$root\extracted")) {
            New-Item -ItemType Directory -Force $root | Out-Null
            New-Item -ItemType Junction -Path "$root\extracted" -Target (Join-Path $data 'extracted') | Out-Null
        }
        # not the cache partitions' images (5 GB, sparse where the runtime made them and dense in a copy;
        # the runtime makes new sparse ones), or every root and each of its harness backups fills the disk
        robocopy (Join-Path $data 'save') "$root\save" /MIR /NFL /NDL /NJH /NJS /NP `
                 /XF Partition1.img Partition2.img Partition3.img Partition4.img Partition5.img | Out-Null
    }
    $copyRoute = if ($i % 2 -eq 0) { "$Route-p1" } else { "$Route-p2" }
    $args = @('scripts/harness.py', 'run', $copyRoute, '--preset', 'win-x64-release', '--secs', "$Secs",
              '--env', 'RECOMP_NETPLAY=quick', '--env', "RECOMP_NETPLAY_SERVER=127.0.0.1:$RelayPort",
              '--env', "RECOMP_NETPLAY_QUICK=$quick", '--env', "RECOMP_NETPLAY_FIGHTER=$($fighters[$i])",
              '--env', 'RECOMP_TEST_OBSERVATIONS=1',
              '--env', 'RECOMP_INPUT_HOST=1', '--env', 'RECOMP_INPUT_IGNORE_FOCUS=1', '--env', 'RECOMP_PAD_HOST=1',
              '--env', 'RECOMP_INPUT_NO_PADS=1', '--env', 'RECOMP_INPUT_VIRTUAL_PADS=1', '--env', 'RECOMP_PAD_SCRIPT_VIRTUAL=1')
    if ($i -gt 0) { $args += @('--env', 'RECOMP_AUDIO_MUTE=1') }
    # Each copy's guest threads on a CPU of their own (2, 4, ...), as on a player's PC: on one shared CPU a
    # copy that spins waiting for the other's input starves it, and both stand still (2026-10-24).
    if (-not ($ExtraEnv -match '^RECOMP_GUEST_CORES?=')) { $args += @('--env', "RECOMP_GUEST_CORE=$($BaseCore + 2 * $i)") }
    # tests: matches that end by themselves (a knockout at a fixed step, or the bot fighting)
    if ($FastKo -gt 0) { $args += @('--env', "RECOMP_TEST_KO_STEP=$FastKo,$FastKoWinner") }
    if ($Bot) { $args += @('--env', "RECOMP_TEST_BOT=1,$BotAggression") }
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
# only the copies this test started (this checkout's build, started after it began): another test's copies,
# from a worktree, keep running
Get-Process defjam_recomp -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like (Join-Path $repo 'build\*') -and $_.StartTime -ge $started } |
    Stop-Process -Force
if (-not $relay.HasExited) { $relay.Kill() }

for ($i = 0; $i -lt $Players; $i++) {
    $log = (Select-String -Path "$logs\copy$i.txt" -Pattern '^log\s+(\S+)' | Select-Object -First 1).Matches.Groups[1].Value
    "== copy $i  $log"
    Get-Content "$logs\copy$i.txt" | Select-Object -First 4 | Select-Object -Skip 1
    if ($log -and (Test-Path $log)) {
        Select-String -Path $log -Pattern '\[NETPLAY\] (START|match|sync|Match over|A player|refused|desync)|\[AUTOMATCH\] sequence|"event":"(match_over|result)"|CRASH' |
            ForEach-Object { $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
        Select-String -Path $log -Pattern 'checksums compared' | Select-Object -Last 1 | ForEach-Object Line
        if ($FastKo -gt 0) { "fast knockouts: " + @(Select-String -Path $log -Pattern '\[TEST-KO\] match').Count }
    }
}
"elapsed: {0:N1} min" -f ((Get-Date) - $started).TotalMinutes
"relay:"; Get-Content "$logs\relay.err", "$logs\relay.out" -ErrorAction SilentlyContinue | Select-String 'room|START|summary' | Select-Object -Last 12 | ForEach-Object Line
