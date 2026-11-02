<#
.SYNOPSIS
Run the local controller experiment with physical pads, a separate save copy and input tracing.
.EXAMPLE
.\scripts\playtest-input.ps1
.EXAMPLE
.\scripts\playtest-input.ps1 -LateRefresh 0
#>
param(
    [ValidateSet('0','1')][string]$LateRefresh = '1',
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name defjam_recomp -ErrorAction SilentlyContinue) {
    throw 'A game is already running. Exit it before preparing or starting this isolated playtest.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$playRoot = Join-Path $repo 'logs\input-playtest-data'
$saveCopy = Join-Path $playRoot 'save'
$sourceSave = Join-Path $repo 'data\save'
if (-not (Test-Path -LiteralPath $sourceSave -PathType Container)) {
    throw "This local test checkout needs its disposable data/save copy: $sourceSave"
}
New-Item -ItemType Directory -Force -Path $playRoot | Out-Null
if (-not (Test-Path -LiteralPath $saveCopy)) {
    Copy-Item -LiteralPath $sourceSave -Destination $saveCopy -Recurse
}
$sourceSettings = Join-Path $repo 'data\settings.ini'
$playSettings = Join-Path $playRoot 'settings.ini'
if ((Test-Path -LiteralPath $sourceSettings) -and -not (Test-Path -LiteralPath $playSettings)) {
    Copy-Item -LiteralPath $sourceSettings -Destination $playSettings
}
Write-Host "Playtest saves/settings: $playRoot"
if ($PrepareOnly) { return }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$trace = Join-Path $repo "logs\input-playtest-$stamp-late$LateRefresh.csv"
$changes = @{
    DEFJAM_DATA = $playRoot
    RECOMP_SETTINGS = $playSettings
    RECOMP_INPUT_HOST = '1'
    RECOMP_PAD_HOST = '1'
    RECOMP_INPUT_EDGES = '1'
    RECOMP_INPUT_POLL_MS = '1'
    RECOMP_INPUT_LATE_REFRESH = $LateRefresh
    RECOMP_INPUT_IGNORE_FOCUS = '0'
    RECOMP_INPUT_NO_PADS = '0'
    RECOMP_INPUT_VIRTUAL_PADS = '0'
    RECOMP_INPUT_VIRTUAL_LATE_MS = '0'
    RECOMP_INPUT_VIRTUAL_CHORD_MS = '0'
    RECOMP_PAD_SCRIPT = $null
    RECOMP_PAD_LIVE = $null
    RECOMP_PAD_SCRIPT_VIRTUAL = $null
    RECOMP_PAD_SCRIPT_KEYS = $null
    RECOMP_TEST_INPUT = $null
    RECOMP_TEST_RNG_SEED = $null
    RECOMP_INPUT_TRACE = $trace
    RECOMP_INPUT_TRACE_LIMIT = '2000000'
    RECOMP_RECORD = '1'
}
$previous = @{}
foreach ($name in $changes.Keys) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
}
try {
    foreach ($name in $changes.Keys) {
        [Environment]::SetEnvironmentVariable($name,$changes[$name],'Process')
    }
    Write-Host "Late refresh $LateRefresh; 1 ms acquisition; physical controllers."
    Write-Host "Input trace: $trace"
    Write-Host 'Try a few minutes of rapid taps, combos, movement, triggers and pause/resume; exit normally afterward.'
    & (Join-Path $PSScriptRoot 'run.ps1') -Preset win-x64-release
} finally {
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')
    }
}
