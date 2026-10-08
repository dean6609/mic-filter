param([Parameter(Mandatory=$true)][string]$DistributionDirectory)
$ErrorActionPreference='Stop'
$testRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $testRoot 'installer-registry.ps1')
$fixture='Software\MicFilter.Tests.'+[guid]::NewGuid().ToString('N')
$registry='HKCU:\'+$fixture
$privateRoot=Join-Path ([IO.Path]::GetTempPath()) ('MicFilter-multi-test-'+[guid]::NewGuid().ToString('N'))
$source=Join-Path $privateRoot 'payload'
$program=Join-Path $privateRoot 'Programs'
$data=Join-Path $privateRoot 'Data'
$backup=Join-Path $data 'MicFilter\installation.json'
$class=$registry+'\Classes\CLSID\'+$ownClsid
$first='{00000000-0000-0000-0000-000000000001}'
$second='{00000000-0000-0000-0000-000000000002}'
$third='{00000000-0000-0000-0000-000000000003}'
$utf8=[Text.UTF8Encoding]::new($true)
$runtimeFiles=@()
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Run-Fixture([string]$Action,[string]$Endpoint,[int]$Expected=0){
    $report=Join-Path $privateRoot ('report-'+[guid]::NewGuid().ToString('N')+'.txt')
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$source+'\install.ps1" -Quiet -Action '+$Action+' -SourceDir "'+$source+'" -ResultPath "'+$report+'"'
    if($Endpoint){$start.Arguments+=' -EndpointGuid '+$Endpoint}
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.EnvironmentVariables['ProgramFiles']=$program;$start.EnvironmentVariables['ProgramData']=$data
    $start.EnvironmentVariables['LOCALAPPDATA']=Join-Path $privateRoot 'Local'
    $start.EnvironmentVariables.Remove('PSModulePath')
    $process=[Diagnostics.Process]::Start($start)
    if(-not $process.WaitForExit(15000)){$process.Kill();throw 'Isolated installer timed out.'}
    $code=$process.ExitCode;$process.Dispose()
    Assert ($code -eq $Expected) ('Isolated '+$Action+' failed: '+[IO.File]::ReadAllText($report))
}
function Slot([string]$Endpoint){
    $key=Get-Item -LiteralPath ($registry+'\Capture\'+$Endpoint+'\FxProperties')
    $value=$key.GetValue($streamEffectSlot);if($value){return $value};return $key.GetValue($effectSlot)
}
try{
    New-Item -ItemType Directory -Path $source,$program,$data,(Join-Path $privateRoot 'Links') -Force | Out-Null
    foreach($name in @('MicFilter.exe','MicFilterAPO.dll','LICENSE','RNNOISE-LICENSE.txt','SPEEX-LICENSE.txt')){Copy-Item -LiteralPath (Join-Path $DistributionDirectory $name) -Destination $source}
    Copy-Item -LiteralPath (Join-Path $testRoot 'GETTING_STARTED.txt') -Destination $source
    # Run the production scripts against private HKCU keys and folders. The only replacements
    # are environment access, elevation, process enumeration and hardware/shortcut boundaries.
    # No production switch permits bypassing elevation or redirecting installed device keys.
    $helper=Get-Content -LiteralPath (Join-Path $testRoot 'installer-registry.ps1') -Raw -Encoding UTF8
    $helper=$helper.Replace('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture',$registry+'\Capture')
    $helper=$helper.Replace('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\MicFilter',$registry+'\Uninstall')
    $helper=$helper.Replace('HKLM:\SOFTWARE\Classes\',$registry+'\Classes\')
    $helper=$helper.Replace('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run',$registry+'\Startup')
    $helper=$helper.Replace('[Microsoft.Win32.RegistryHive]::LocalMachine','[Microsoft.Win32.RegistryHive]::CurrentUser')
    $helper+=@'

function Restart-CaptureDevice {param($EndpointText,$Log) return $false}
function Get-Process {param($Name,$ErrorAction) return}
function New-FixtureShell {
    $folders=New-Object PSObject
    $folders | Add-Member ScriptMethod Item {param($Name) return (Join-Path $env:ProgramFiles '..\Links')}
    $shell=New-Object PSObject -Property @{SpecialFolders=$folders}
    $shell | Add-Member ScriptMethod CreateShortcut {
        param($Path)
        $shortcut=New-Object PSObject -Property @{TargetPath='';WorkingDirectory='';Description='';IconLocation=''}
        $shortcut | Add-Member ScriptMethod Save {}
        return $shortcut
    }
    return $shell
}
'@
    $helper=$helper.Replace('$env:ProgramFiles',("'"+$program.Replace("'","''")+"'"))
    [IO.File]::WriteAllText((Join-Path $source 'installer-registry.ps1'),$helper,$utf8)
    $install=Get-Content -LiteralPath (Join-Path $testRoot 'install.ps1') -Raw -Encoding UTF8
    $install=$install.Replace('HKLM:\SOFTWARE\Classes\',$registry+'\Classes\')
    $install=$install.Replace('SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\',$fixture+'\Capture\')
    $install=$install.Replace('HKCU:\Software\Microsoft\Windows\CurrentVersion\Run',$registry+'\Run')
    $install=$install.Replace("if(-not `$adminCheck.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Windows requires running the installer as administrator.'}",'')
    $install=$install.Replace('New-Object -ComObject WScript.Shell','New-FixtureShell')
    $install=$install.Replace('$env:ProgramFiles',("'"+$program.Replace("'","''")+"'"))
    $install=$install.Replace('$env:ProgramData',("'"+$data.Replace("'","''")+"'"))
    Assert ($install -notmatch 'HKLM:|CurrentVersion\\MMDevices|Stop-Process -Name') 'Fixture still refers to production device/registration keys.'
    [IO.File]::WriteAllText((Join-Path $source 'install.ps1'),$install,$utf8)
    foreach($endpoint in @($first,$second,$third)){
        New-Item -Path ($registry+'\Capture\'+$endpoint+'\FxProperties') -Force | Out-Null
        New-ItemProperty -LiteralPath ($registry+'\Capture\'+$endpoint+'\FxProperties') -Name 'unrelated' -Value 'kept' -Force | Out-Null
    }
    $discoverySlot='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7'
    $discoveryProxy='{889C03C8-ABAD-4004-BF0A-BC7BB825E166}'
    $associationSlot='{9e6136e0-57ab-4949-b57a-3627be142855},100'
    New-ItemProperty -LiteralPath ($registry+'\Capture\'+$first+'\FxProperties') -Name $discoverySlot -Value $discoveryProxy -Force | Out-Null
    New-ItemProperty -LiteralPath ($registry+'\Capture\'+$second+'\FxProperties') -Name $associationSlot -Value 'test-driver-association' -Force | Out-Null
    # Existing releases installed LFX on modern endpoints. Migrate without backing up ourselves.
    New-Item -ItemType Directory -Path (Split-Path $backup -Parent) -Force | Out-Null
    $originalModes=@('{FC1CFC9B-B9D6-4CFA-B5E0-4BB2166878B2}')
    $modernKey=Open-EndpointEffectsKey -SubKey ($fixture+'\Capture\'+$first+'\FxProperties') -Hive ([Microsoft.Win32.RegistryHive]::CurrentUser)
    try{
        $modernKey.SetValue($effectSlot,$ownClsid,[Microsoft.Win32.RegistryValueKind]::String)
        $modernKey.SetValue($streamModesSlot,[string[]]$originalModes,[Microsoft.Win32.RegistryValueKind]::MultiString)
    }finally{$modernKey.Dispose()}
    [IO.File]::WriteAllText($backup,([pscustomobject]@{EndpointGuid=$first;HadSlot=$false;PreviousSlot=$null} | ConvertTo-Json),$utf8)
    Run-Fixture Install $first
    $modernKey=Get-Item -LiteralPath ($registry+'\Capture\'+$first+'\FxProperties')
    Assert ($modernKey.GetValue($streamEffectSlot) -eq $ownClsid -and -not $modernKey.GetValue($effectSlot)) 'A modern endpoint was attached to the ignored legacy slot.'
    Assert ($modernKey.GetValueKind($streamModesSlot) -eq [Microsoft.Win32.RegistryValueKind]::MultiString -and (Same-Values $modernKey.GetValue($streamModesSlot) $streamModes)) 'Modern capture modes were not registered as a multi-string.'
    $migrated=@(Read-InstallationBackups $backup)[0]
    Assert (-not $migrated.HadSlot -and $migrated.StreamValues.Count -eq 2 -and -not $migrated.StreamValues[0].Exists) 'Migration lost the original restore configuration.'
    Assert ([BitConverter]::ToInt32([IO.File]::ReadAllBytes((Join-Path $data 'MicFilter\options.bin')),8) -eq 0) 'A first installation did not default to Natural.'
    # A 0.5 backup must migrate without making MicFilter its own original effect.
    $legacy=@(Read-InstallationBackups $backup)[0]
    [IO.File]::WriteAllText($backup,($legacy | ConvertTo-Json),$utf8)
    Run-Fixture Install $second
    Assert (@(Read-InstallationBackups $backup).Count -eq 2) 'Adding an input lost the old backup.'
    Assert ((Slot $first) -eq $ownClsid -and (Slot $second) -eq $ownClsid) 'Both inputs were not attached.'
    Assert ((Get-Item -LiteralPath ($registry+'\Capture\'+$first+'\FxProperties')).GetValue($discoverySlot) -eq $discoveryProxy) 'The discovery proxy was replaced.'
    Assert ((Get-Item -LiteralPath ($registry+'\Capture\'+$second+'\FxProperties')).GetValue($associationSlot) -eq 'test-driver-association') 'The driver association was changed.'
    $saved=[IO.File]::ReadAllText($backup)
    Run-Fixture Install $first
    Assert ([IO.File]::ReadAllText($backup) -eq $saved) 'Reinstall changed the original restore backups.'
    $stateFile=Join-Path $data 'MicFilter\state.bin';$optionsFile=Join-Path $data 'MicFilter\options.bin'
    $controls=[IO.File]::ReadAllBytes($stateFile);[BitConverter]::GetBytes([int]0).CopyTo($controls,8);[IO.File]::WriteAllBytes($stateFile,$controls)
    $options=[IO.File]::ReadAllBytes($optionsFile);[BitConverter]::GetBytes([int]3).CopyTo($options,8);[IO.File]::WriteAllBytes($optionsFile,$options)
    Run-Fixture Install $second
    Assert ([BitConverter]::ToInt32([IO.File]::ReadAllBytes($stateFile),8) -eq 0) 'Adding/updating an input enabled a disabled filter.'
    Assert ([BitConverter]::ToInt32([IO.File]::ReadAllBytes($optionsFile),8) -eq 3) 'The saved Deep profile was lost.'
    $other='{00000000-0000-0000-0000-000000000099}'
    New-ItemProperty -LiteralPath ($registry+'\Capture\'+$third+'\FxProperties') -Name $streamEffectSlot -Value $other -Force | Out-Null
    Run-Fixture Install $third 1
    Assert ((Slot $third) -eq $other -and [IO.File]::ReadAllText($backup) -eq $saved) 'A foreign stream effect was replaced.'
    Remove-ItemProperty -LiteralPath ($registry+'\Capture\'+$third+'\FxProperties') -Name $streamEffectSlot
    New-ItemProperty -LiteralPath ($registry+'\Capture\'+$third+'\FxProperties') -Name $discoverySlot -Value $other -Force | Out-Null
    Run-Fixture Install $third 1
    Assert (-not(Slot $third) -and [IO.File]::ReadAllText($backup) -eq $saved) 'A real processing-effect conflict was ignored.'
    Remove-ItemProperty -LiteralPath ($registry+'\Capture\'+$third+'\FxProperties') -Name $discoverySlot
    New-ItemProperty -LiteralPath ($registry+'\Capture\'+$third+'\FxProperties') -Name $effectSlot -Value $other -Force | Out-Null
    Run-Fixture Install $third 1
    Assert ((Slot $third) -eq $other -and [IO.File]::ReadAllText($backup) -eq $saved) 'A real conflict changed an effect or backup.'
    # Exercise the batch transaction itself: attach a new input, fail capture, restore both.
    $fourth='{00000000-0000-0000-0000-000000000004}'
    New-Item -Path ($registry+'\Capture\'+$fourth+'\FxProperties') -Force | Out-Null
    $batch=Get-Content -LiteralPath (Join-Path $testRoot 'setup-install.ps1') -Raw -Encoding UTF8
    $batch=$batch.Replace('HKLM:\SOFTWARE\Classes\',$registry+'\Classes\')
    $batch=$batch.Replace('SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\',$fixture+'\Capture\')
    $batch=$batch.Replace('HKCU:\Software\Microsoft\Windows\CurrentVersion\Run',$registry+'\Run')
    $batch=$batch.Replace('New-Object -ComObject WScript.Shell','New-FixtureShell')
    $batch=$batch.Replace('$env:ProgramFiles',("'"+$program.Replace("'","''")+"'"))
    $batch=$batch.Replace('$env:ProgramData',("'"+$data.Replace("'","''")+"'"))
    $batch=$batch.Replace("if(-not `$administrator -or -not [Environment]::Is64BitProcess){throw 'Administrator permission and Windows x64 are required.'}",'')
    $batch=$batch.Replace('try {',@'
function Run-Native {param($Executable,$Arguments,$Name) return [pscustomobject]@{Code=1;Text='Frames=0 Packets=0'}}
try {
'@)
    [IO.File]::WriteAllText((Join-Path $source 'setup-install.ps1'),$batch,$utf8)
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    $summary=Join-Path $privateRoot 'batch-summary.txt'
    $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$source+'\setup-install.ps1" -EndpointList "'+$first+','+$fourth+'" -VoicePreset 1 -SummaryPath "'+$summary+'"'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.EnvironmentVariables.Remove('PSModulePath')
    # Keep the same read/write handles as the tray/APO while setup runs in another process.
    foreach($path in @($stateFile,$optionsFile)){$runtimeFiles+=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)}
    $process=[Diagnostics.Process]::Start($start);$output=$process.StandardOutput.ReadToEndAsync();$errorOutput=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(15000)){$process.Kill();$process.WaitForExit();throw 'The isolated batch rollback timed out.'}
    Assert ($process.ExitCode -eq 1) 'Capture failure did not fail the batch.'
    $process.Dispose();Assert (Test-Path -LiteralPath $summary) ('The batch did not produce its summary: '+$errorOutput.Result)
    Assert ([IO.File]::ReadAllText($summary) -match 'could not capture audio') ('The batch did not reach capture verification: '+[IO.File]::ReadAllText($summary))
    Assert ((Slot $first) -eq $ownClsid -and (Slot $second) -eq $ownClsid -and -not(Slot $fourth)) ('Batch rollback did not restore each selected input: '+$output.Result+$errorOutput.Result)
    Assert ([IO.File]::ReadAllText($backup) -eq $saved) ('Batch rollback lost the original backup document: '+$output.Result+$errorOutput.Result)
    $runtimeFiles[0].Position=8;$enabled=New-Object byte[] 4;$null=$runtimeFiles[0].Read($enabled,0,4)
    $runtimeFiles[1].Position=8;$voice=New-Object byte[] 4;$null=$runtimeFiles[1].Read($voice,0,4)
    Assert ([BitConverter]::ToInt32($enabled,0) -eq 0 -and [BitConverter]::ToInt32($voice,0) -eq 3) 'Open runtime handles did not see restored settings.'
    # A successful update must also work with both handles still open and keep saved settings.
    $batch=$batch.Replace("Code=1;Text='Frames=0 Packets=0'","Code=0;Text='Frames=1024 Packets=4'")
    Assert ($batch -notmatch 'HKLM:|CurrentVersion\\MMDevices') 'The batch fixture still refers to production registration keys.'
    [IO.File]::WriteAllText((Join-Path $source 'setup-install.ps1'),$batch,$utf8)
    $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$source+'\setup-install.ps1" -EndpointList "'+$first+','+$second+'" -SummaryPath "'+$summary+'"'
    $process=[Diagnostics.Process]::Start($start);$output=$process.StandardOutput.ReadToEndAsync();$errorOutput=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(15000)){$process.Kill();$process.WaitForExit();throw 'The isolated shared-file update timed out.'}
    Assert ($process.ExitCode -eq 0) ('Update with runtime handles open failed: '+$output.Result+$errorOutput.Result)
    $process.Dispose()
    $runtimeFiles[0].Position=8;$null=$runtimeFiles[0].Read($enabled,0,4)
    $runtimeFiles[1].Position=8;$null=$runtimeFiles[1].Read($voice,0,4)
    Assert ([BitConverter]::ToInt32($enabled,0) -eq 0 -and [BitConverter]::ToInt32($voice,0) -eq 3) 'Shared-file update lost disabled/Deep settings.'
    Assert ((Slot $first) -eq $ownClsid -and (Slot $second) -eq $ownClsid -and [IO.File]::ReadAllText($backup) -eq $saved) 'Shared-file update changed inputs or original backups.'
    foreach($stream in $runtimeFiles){$stream.Dispose()};$runtimeFiles=@()
    Assert ([BitConverter]::ToInt32([IO.File]::ReadAllBytes($stateFile),8) -eq 0 -and [BitConverter]::ToInt32([IO.File]::ReadAllBytes($optionsFile),8) -eq 3) 'Batch rollback lost disabled/profile settings.'
    Run-Fixture Remove $first
    Assert (-not(Slot $first) -and (Slot $second) -eq $ownClsid -and (Test-Path -LiteralPath $class)) 'Removing one input broke the other.'
    $modernKey=Get-Item -LiteralPath ($registry+'\Capture\'+$first+'\FxProperties')
    Assert ($modernKey.GetValueKind($streamModesSlot) -eq [Microsoft.Win32.RegistryValueKind]::MultiString -and (Same-Values $modernKey.GetValue($streamModesSlot) $originalModes)) 'Removal did not restore the original processing modes and type.'
    Assert (@(Read-InstallationBackups $backup).Count -eq 1) 'Removal did not keep the remaining backup.'
    Assert ([BitConverter]::ToInt32([IO.File]::ReadAllBytes($stateFile),8) -eq 0) 'Removing one changed shared enable state.'
    Run-Fixture Remove $second
    Assert (-not(Slot $second) -and -not(Test-Path -LiteralPath $class) -and -not(Test-Path -LiteralPath $backup)) 'Last-input removal left its registration.'
    Run-Fixture Install $first;Run-Fixture Install $second
    $previousStartupKey=$machineRunKeyPath;$machineRunKeyPath=$registry+'\Startup'
    try{Register-MicFilterStartup (Join-Path $program 'MicFilter\MicFilter.exe')}finally{$machineRunKeyPath=$previousStartupKey}
    Assert ((Get-Item -LiteralPath ($registry+'\Startup')).GetValue('MicFilter') -eq ('"'+(Join-Path $program 'MicFilter\MicFilter.exe')+'"')) 'The automatic startup command is incorrect.'
    Run-Fixture Uninstall ''
    Assert (-not(Get-ItemProperty -LiteralPath ($registry+'\Startup') -Name 'MicFilter' -ErrorAction SilentlyContinue)) 'Uninstall left its automatic startup entry.'
    Assert (-not(Slot $first) -and -not(Slot $second) -and (Slot $third) -eq $other) 'Uninstall did not restore all inputs or changed an unrelated app.'
    Assert (-not(Test-Path -LiteralPath $class) -and -not(Test-Path -LiteralPath (Join-Path $program 'MicFilter'))) 'Uninstall left application registration/files.'
    foreach($endpoint in @($first,$second,$third)){Assert ((Get-Item -LiteralPath ($registry+'\Capture\'+$endpoint+'\FxProperties')).GetValue('unrelated') -eq 'kept') 'An unrelated property was changed.'}
    Assert ((Get-Item -LiteralPath ($registry+'\Capture\'+$first+'\FxProperties')).GetValue($discoverySlot) -eq $discoveryProxy) 'Uninstall removed the discovery proxy.'
    Assert ((Get-Item -LiteralPath ($registry+'\Capture\'+$second+'\FxProperties')).GetValue($associationSlot) -eq 'test-driver-association') 'Uninstall removed the driver association.'
    Write-Host 'PASS multiple inputs: legacy migration, idempotent install, shared-file update/rollback, saved controls, conflict preservation, capture-failure batch rollback, individual removal, all-input uninstall; isolated HKCU and files.'
}finally{
    foreach($stream in $runtimeFiles){$stream.Dispose()}
    if(Test-Path -LiteralPath $registry){Remove-Item -LiteralPath $registry -Recurse -Force}
    $resolved=[IO.Path]::GetFullPath($privateRoot)
    $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if(-not $resolved.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or -not([IO.Path]::GetFileName($resolved).StartsWith('MicFilter-multi-test-'))){throw 'Unsafe fixture cleanup path.'}
    if(Test-Path -LiteralPath $resolved){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
