<#
.SYNOPSIS  Try online play alone: two copies of the game on this PC, connected to each other.
.DESCRIPTION
  Window 1 hosts and is player 1: your gamepad, read even while the other window has the focus.
  Window 2 joins and is player 2: the keyboard (click into window 2 to play it), without sound,
  on a CPU of its own. Both connect through the relay by themselves (no room code to type) and
  show the launcher: press PLAY in both. Window 2 keeps its saves and settings in a folder of its
  own beside your data folder (<data>-player2; the first run copies your saves there).
  Keys in window 2: W A S D move, I J K L the face buttons, Enter is START (docs/10-input.md).
.PARAMETER Install  The install folder (the one with installed.json); found by itself if left out.
.PARAMETER Server   The relay (default: the project's server).
#>
param([string]$Install = '', [string]$Server = '142.132.178.17')
$ErrorActionPreference = 'Stop'

# The game and its data folder: an installed copy, or this checkout's own build.
function Find-Install {
    $candidates = @($Install, "$env:USERPROFILE\Desktop\DefJam\app", "$env:LOCALAPPDATA\DefJamRecompiled\app") |
        Where-Object { $_ -and (Test-Path (Join-Path $_ 'installed.json')) }
    if ($candidates) {
        $r = Get-Content (Join-Path $candidates[0] 'installed.json') -Raw | ConvertFrom-Json
        return @{ exe = $r.executable; data = $r.data }
    }
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $exe = Join-Path $repo 'build\win-x64-release\defjam_recomp.exe'
    $data = if ($env:DEFJAM_DATA) { $env:DEFJAM_DATA } else { Join-Path (Split-Path $repo) 'defjam' }
    if (-not (Test-Path $exe)) { throw 'No installed game and no build found.' }
    return @{ exe = $exe; data = $data }
}
$game = Find-Install
if (Get-Process defjam_recomp -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
"Game: $($game.exe)"
"Data: $($game.data)"

# Player 2's data folder: the same game files (a junction), its own saves and settings.
$data2 = $game.data.TrimEnd('\') + '-player2'
if (-not (Test-Path "$data2\extracted")) {
    New-Item -ItemType Directory -Force $data2 | Out-Null
    New-Item -ItemType Junction -Path "$data2\extracted" -Target (Join-Path $game.data 'extracted') | Out-Null
}
if (-not (Test-Path "$data2\save")) {
    'Copying your saves for player 2 (once)...'
    robocopy (Join-Path $game.data 'save') "$data2\save" /E /NFL /NDL /NJH /NJS /NP | Out-Null
}
if (-not (Test-Path "$data2\settings.ini") -and (Test-Path (Join-Path $game.data 'settings.ini'))) {
    $ini = (Get-Content (Join-Path $game.data 'settings.ini')) -replace '^\s*fullscreen\s*=.*$', 'fullscreen = false'
    Set-Content -Path "$data2\settings.ini" -Value $ini -Encoding utf8
}
if (Test-Path (Join-Path $game.data 'mods')) {
    if (-not (Test-Path "$data2\mods")) { New-Item -ItemType Junction -Path "$data2\mods" -Target (Join-Path $game.data 'mods') | Out-Null }
}

$code = Join-Path $env:TEMP 'defjam-local-test-room.txt'
Remove-Item $code -ErrorAction SilentlyContinue
$core = [Math]::Min(6, [Environment]::ProcessorCount - 1)

function Start-Copy($data, $vars) {
    $saved = @{}
    foreach ($k in $vars.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k, 'Process'); [Environment]::SetEnvironmentVariable($k, $vars[$k], 'Process') }
    $env:DEFJAM_DATA = $data
    $p = Start-Process $game.exe -ArgumentList '--launcher' -WorkingDirectory (Split-Path $game.exe) -PassThru
    foreach ($k in $vars.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], 'Process') }
    return $p
}

$common = @{ RECOMP_NETPLAY_SERVER = $Server; RECOMP_NETPLAY_CODE_FILE = $code }
$host1 = Start-Copy $game.data ($common + @{ RECOMP_NETPLAY = 'host'; RECOMP_INPUT_IGNORE_FOCUS = '1' })
Start-Sleep -Seconds 4
$guest = Start-Copy $data2 ($common + @{ RECOMP_NETPLAY = 'join'; RECOMP_INPUT_NO_PADS = '1';
                                          RECOMP_AUDIO_MUTE = '1'; RECOMP_GUEST_CORE = "$core" })
""
"Started: window 1 = player 1 (your pad), window 2 = player 2 (keyboard, click into it)."
"Press PLAY in both. They connect by themselves; the Online page shows it."
"In the game: Battle - One on One. Player 1 steers the menus; START on the keyboard (Enter) joins player 2."
