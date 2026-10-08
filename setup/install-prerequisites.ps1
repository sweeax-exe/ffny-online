$ErrorActionPreference = 'Stop'
$signature = Get-AuthenticodeSignature -LiteralPath $env:DEFJAM_SETUP_BOOTSTRAPPER
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'Microsoft Build Tools publisher verification failed'
}
$arguments = '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.CMake.Project'
$process = Start-Process -FilePath $env:DEFJAM_SETUP_BOOTSTRAPPER -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -eq 3010) { Write-Output 'REBOOT_REQUIRED'; exit 0 }
if ($process.ExitCode -ne 0) { throw "Build Tools installation failed: $($process.ExitCode)" }
