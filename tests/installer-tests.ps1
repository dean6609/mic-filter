param([Parameter(Mandatory=$true)][string]$DistributionDirectory)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $PSScriptRoot '..'
. (Join-Path $testRoot 'installer-registry.ps1')
foreach($scriptName in @('install.ps1','installer-registry.ps1')){
    $tokens=$null;$errors=$null
    [Management.Automation.Language.Parser]::ParseFile((Join-Path $DistributionDirectory $scriptName),[ref]$tokens,[ref]$errors) | Out-Null
    if($errors.Count){throw ($errors.Message -join '; ')}
    $bytes=[IO.File]::ReadAllBytes((Join-Path $DistributionDirectory $scriptName))
    if($bytes[0] -ne 239 -or $bytes[1] -ne 187 -or $bytes[2] -ne 191){throw 'UTF-8 BOM missing for Windows PowerShell 5.1.'}
}
$fixture='Software\MicFilter.Tests.'+[guid]::NewGuid().ToString('N')
$base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
$key=$null
try{
    $created=$base.CreateSubKey($fixture);$created.SetValue('unrelated','preserved');$created.SetValue('effect','old');$created.Dispose()
    $key=Open-EndpointEffectsKey -SubKey $fixture -Hive ([Microsoft.Win32.RegistryHive]::CurrentUser)
    Set-EndpointEffect -Key $key -Name 'effect' -Value 'new'
    if($key.GetValue('effect') -ne 'new' -or $key.GetValue('unrelated') -ne 'preserved'){throw 'Updating one value changed unrelated configuration.'}
    Set-EndpointEffect -Key $key -Name 'effect' -Value 'old'
    if($key.GetValue('effect') -ne 'old'){throw 'The previous value was not restored.'}
    $fresh=Open-EndpointEffectsKey -SubKey ($fixture+'\FxProperties') -Hive ([Microsoft.Win32.RegistryHive]::CurrentUser) -CreateIfMissing
    try{Set-EndpointEffect -Key $fresh -Name 'effect' -Value 'test';if($fresh.GetValue('effect') -ne 'test'){throw 'Could not create an initially missing effect key.'}}finally{$fresh.Dispose()}
}finally{if($key){$key.Dispose()};$base.DeleteSubKeyTree($fixture,$false);$base.Dispose()}
$isAdmin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $isAdmin){
    $failureReport=Join-Path $DistributionDirectory 'test-installer-denied.txt'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $DistributionDirectory 'install.ps1') -Action Install -EndpointGuid ([guid]::NewGuid()) -SourceDir $DistributionDirectory -ResultPath $failureReport
    if($LASTEXITCODE -ne 1){throw 'The non-elevated installer did not return a failure code.'}
    $message=Get-Content -LiteralPath $failureReport -Raw -Encoding UTF8
    if($message -notmatch 'administrator' -or $message -notmatch 'Step:'){throw 'The report does not identify the error and its step.'}
    if(-not(Test-Path -LiteralPath ([IO.Path]::ChangeExtension($failureReport,'.log')))){throw 'The persistent log was not created.'}
}
Write-Host 'PASS installer: scripts PS5.1, UTF-8, targeted registry value update/restore, persistent failure report and exit code.'
