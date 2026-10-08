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
function Test-ConsoleFlow([string]$Answers,[string]$Expected){
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$setup;$start.Arguments='--test-console --no-pause'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.Encoding]::Unicode;$start.StandardErrorEncoding=[Text.Encoding]::Unicode
    $console=[Diagnostics.Process]::Start($start)
    $out=$console.StandardOutput.ReadToEndAsync();$errors=$console.StandardError.ReadToEndAsync()
    $console.StandardInput.Write($Answers);$console.StandardInput.Close()
    if(-not $console.WaitForExit(15000)){$console.Kill();$console.Dispose();throw 'Console selection did not finish.'}
    $code=$console.ExitCode;$console.Dispose()
    if($code -ne 0 -or $out.Result -notmatch [regex]::Escape($Expected)){throw ('Console flow failed: '+$out.Result+$errors.Result)}
}
Test-ConsoleFlow "1`n2`n`n4`n" 'TEST console selection=1,2 voice=3'
Test-ConsoleFlow "A`n1`n`n`n" 'TEST console selection=2 voice=-1'
Test-ConsoleFlow "1`n2,3`n`n2`n" 'TEST console selection=1 voice=1'
foreach($name in @('setup-install.ps1','install.ps1','installer-registry.ps1')){
    $tokens=$null;$errors=$null;[Management.Automation.Language.Parser]::ParseFile((Join-Path $DistributionDirectory $name),[ref]$tokens,[ref]$errors) | Out-Null
    if($errors.Count){throw ($errors.Message -join '; ')}
    $bytes=[IO.File]::ReadAllBytes((Join-Path $DistributionDirectory $name));if($bytes[0] -ne 239 -or $bytes[1] -ne 187 -or $bytes[2] -ne 191){throw ('UTF-8 BOM missing: '+$name)}
}
Write-Host 'PASS package: embedded resources/icons, isolated extraction, persistent multi-input console flow and voice choice, PS5.1 scripts.'
