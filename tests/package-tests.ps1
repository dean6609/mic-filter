param([Parameter(Mandatory=$true)][string]$DistributionDirectory)
$ErrorActionPreference='Stop'
$setup=Join-Path $DistributionDirectory 'WavoFilter-Setup.exe'
$output=Join-Path $DistributionDirectory 'package-self-test.txt'
$process=Start-Process -FilePath $setup -ArgumentList '--self-test --no-pause' -WindowStyle Hidden -PassThru -RedirectStandardOutput $output
if(-not $process.WaitForExit(15000)){$process.Kill();throw 'La prueba del paquete no terminó.'}
$process.Refresh();$text=Get-Content -LiteralPath $output -Raw -Encoding Unicode
if($process.ExitCode -ne 0 -or $text -notmatch 'PASS paquete'){throw ('Falló la prueba del paquete: '+$text)}
foreach($name in @('setup-install.ps1','install.ps1','installer-registry.ps1')){
    $tokens=$null;$errors=$null;[Management.Automation.Language.Parser]::ParseFile((Join-Path $DistributionDirectory $name),[ref]$tokens,[ref]$errors) | Out-Null
    if($errors.Count){throw ($errors.Message -join '; ')}
    $bytes=[IO.File]::ReadAllBytes((Join-Path $DistributionDirectory $name));if($bytes[0] -ne 239 -or $bytes[1] -ne 187 -or $bytes[2] -ne 191){throw ('Falta BOM UTF-8: '+$name)}
}
$expected=((Get-Content -LiteralPath (Join-Path $DistributionDirectory 'SHA256SUMS.txt') -Raw).Split(' ')[0]).Trim()
if((Get-FileHash -LiteralPath $setup).Hash.ToLowerInvariant() -ne $expected){throw 'No coincide SHA256SUMS.'}
Write-Host 'PASS paquete: recursos embebidos, extracción aislada, scripts PS5.1, checksum del instalador.'
