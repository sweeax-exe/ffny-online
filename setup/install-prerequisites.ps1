$ErrorActionPreference = 'Stop'
# Two cases, both from paths the setup found itself (no user strings in this script):
#  - DEFJAM_SETUP_VS_MODIFY: Visual Studio or its Build Tools are installed but lack a part the
#    build needs (CMake and Ninja are the usual gap); the Visual Studio Installer adds it there.
#  - otherwise: a fresh Build Tools install from the downloaded bootstrapper.
# Authenticode is checked before requesting elevation either way.
function Assert-Microsoft($path) {
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw 'Microsoft Build Tools publisher verification failed'
    }
}
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
if ($process.ExitCode -eq 3010) { Write-Output 'REBOOT_REQUIRED'; exit 0 }
if ($process.ExitCode -ne 0) { throw "Build Tools installation failed: $($process.ExitCode)" }
