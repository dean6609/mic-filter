param([Parameter(Mandatory=$true)][string]$DistributionDirectory)
$ErrorActionPreference='Stop'
$setup=Join-Path $DistributionDirectory 'MicFilter-Setup.exe'
$output=Join-Path $DistributionDirectory 'package-self-test.txt'
$process=Start-Process -FilePath $setup -ArgumentList '--self-test --no-pause' -WindowStyle Hidden -PassThru -RedirectStandardOutput $output
# PowerShell 5.1 reports a null ExitCode unless the process handle is opened while it runs.
$null=$process.Handle
if(-not $process.WaitForExit(15000)){$process.Kill();throw 'The package test did not finish.'}
$process.Refresh();$text=Get-Content -LiteralPath $output -Raw -Encoding Unicode
if($process.ExitCode -ne 0 -or $text -notmatch 'PASS package'){throw ('Package test failed: '+$text)}
foreach($name in @('setup-install.ps1','install.ps1','installer-registry.ps1')){
    $tokens=$null;$errors=$null;[Management.Automation.Language.Parser]::ParseFile((Join-Path $DistributionDirectory $name),[ref]$tokens,[ref]$errors) | Out-Null
    if($errors.Count){throw ($errors.Message -join '; ')}
    $bytes=[IO.File]::ReadAllBytes((Join-Path $DistributionDirectory $name));if($bytes[0] -ne 239 -or $bytes[1] -ne 187 -or $bytes[2] -ne 191){throw ('UTF-8 BOM missing: '+$name)}
}
Write-Host 'PASS package: embedded resources, isolated extraction, PS5.1 scripts.'
