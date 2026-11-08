$ErrorActionPreference = 'Stop'
# Two cases, both from paths the setup found itself (no user strings in this script):
#  - DEFJAM_SETUP_VS_MODIFY: Visual Studio or its Build Tools are installed but lack a part the
#    build needs (CMake and Ninja are the usual gap); the Visual Studio Installer adds it there.
#  - otherwise: a fresh Build Tools install from the downloaded bootstrapper.
# Authenticode is checked before requesting elevation either way.
# Every failure is written to standard output as one "ERROR: ..." line before exit 1: the setup
# log keeps this script's output, and a player's log once held only "Stage failed with exit 1".
function Assert-Microsoft($path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw 'Microsoft Build Tools publisher verification failed'
    }
}
# What the Visual Studio Installer's exit codes mean for a player.
function Explain-Exit($code) {
    switch ($code) {
        1602 { 'the installation was cancelled (in the installer window or at the Windows administrator prompt)' }
        1618 { 'another installation is running; wait for it to finish (or restart Windows) and try again' }
        5007 { 'the installation is blocked on this PC (a company policy or security software)' }
        8006 { 'Visual Studio or another installer is running; close it and try again' }
        default { "the Visual Studio Installer stopped with code $code; its own logs are the dd_*.log files in %TEMP%" }
    }
}
try {
    $components = '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.CMake.Project'
    if ($env:DEFJAM_SETUP_VS_MODIFY) {
        $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
        Assert-Microsoft $installer
        $arguments = "modify --installPath `"$($env:DEFJAM_SETUP_VS_MODIFY)`" $components --passive --norestart"
        $process = Start-Process -FilePath $installer -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    } else {
        Assert-Microsoft $env:DEFJAM_SETUP_BOOTSTRAPPER
        $arguments = '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.CMake.Project'
        $process = Start-Process -FilePath $env:DEFJAM_SETUP_BOOTSTRAPPER -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    }
    Write-Output "Build Tools installer exit code: $($process.ExitCode)"
    if ($process.ExitCode -eq 3010) { Write-Output 'REBOOT_REQUIRED'; exit 0 }
    if ($process.ExitCode -ne 0) {
        Write-Output "ERROR: Build Tools installation failed: $(Explain-Exit $process.ExitCode)"
        exit 1
    }
} catch {
    $message = $_.Exception.Message
    if ($_.Exception -is [System.ComponentModel.Win32Exception] -and $_.Exception.NativeErrorCode -eq 1223) {
        $message = 'the Windows administrator prompt was declined; run setup again and choose Yes'
    } elseif ($message -match 'canceled by the user|cancelled by the user') {
        $message = 'the Windows administrator prompt was declined; run setup again and choose Yes'
    }
    Write-Output "ERROR: Build Tools installation failed: $message"
    exit 1
}
