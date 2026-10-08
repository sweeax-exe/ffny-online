$ErrorActionPreference = 'Stop'
$shell = New-Object -ComObject WScript.Shell
$menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Def Jam Recompiled'
New-Item -ItemType Directory -Path $menu -Force | Out-Null
foreach ($folder in @($menu, [Environment]::GetFolderPath('Desktop'))) {
    $path = Join-Path $folder 'Def Jam Recompiled.lnk'
    $shortcut = $shell.CreateShortcut($path)
    if ($env:DEFJAM_SETUP_REMOVE_SHORTCUTS) {
        if ((Test-Path -LiteralPath $path) -and $shortcut.TargetPath -eq $env:DEFJAM_SETUP_LAUNCHER) {
            Remove-Item -LiteralPath $path -Force
        }
        continue
    }
    $shortcut.TargetPath = $env:DEFJAM_SETUP_LAUNCHER
    $shortcut.Arguments = '--launch'
    $shortcut.WorkingDirectory = $env:DEFJAM_SETUP_ROOT
    $shortcut.Description = 'Play your locally built Def Jam: Fight for NY'
    $shortcut.Save()
}
