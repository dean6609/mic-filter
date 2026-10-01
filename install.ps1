param(
    [Parameter(Mandatory=$true)][ValidateSet('Install','Remove')][string]$Action,
    [Parameter(Mandatory=$true)][guid]$EndpointGuid,
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$ResultPath
)
$ErrorActionPreference='Stop'
$ownClsid='{CDB2B27A-3B40-4B79-95AA-123C7136D873}'
$legacyClsids=@('{54F530A1-D045-4C70-8999-11CF13E0DDAF}','{6C78EB4F-8AE4-4461-BE4A-989C7C14C7B2}')
$knownClownfish='{80E0C6D1-9465-43B2-9BD5-27A3A56CF1B3}'
$endpointText='{'+$EndpointGuid.ToString()+'}'
$endpointPath='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\'+$endpointText
$fxPath=$endpointPath+'\FxProperties'
$slot='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2'
$programDir=Join-Path $env:ProgramFiles 'MicFilter'
$stateDir=Join-Path $env:ProgramData 'MicFilter'
if(-not(Test-Path -LiteralPath (Join-Path $stateDir 'state.bin')) -and (Test-Path -LiteralPath (Join-Path $env:ProgramData 'WavoFilter\state.bin'))){$stateDir=Join-Path $env:ProgramData 'WavoFilter'}
$backupPath=Join-Path $stateDir 'installation.json'
$classPath='HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid
$apoPath='HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\'+$ownClsid
$configPath=$classPath
$adminCheck=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$step='Preparation'
$effectsKey=$null
$logPath=$null
$exitCode=1
function Write-InstallerLog([string]$Message) {
    Write-Host $Message
    if($logPath){[IO.File]::AppendAllText($logPath,([DateTime]::Now.ToString('o')+' '+$Message+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))}
}
function Write-InstallerResult([string]$Message) {
    if($ResultPath){[IO.File]::WriteAllText($ResultPath,$Message,[Text.UTF8Encoding]::new($false))}
}
function Restore-Slot($backup) {
    $currentValue=$effectsKey.GetValue($slot)
    $canRestorePrevious=$backup.HadSlot
    if($backup.PreviousSlot -eq $knownClownfish -and -not(Test-Path -LiteralPath ('HKLM:\SOFTWARE\Classes\CLSID\'+$knownClownfish+'\InprocServer32'))){$canRestorePrevious=$false}
    $expectedPrevious=if($canRestorePrevious){$backup.PreviousSlot}else{$null}
    if($currentValue -eq $ownClsid -or $legacyClsids -contains $currentValue) {
        if($canRestorePrevious){Set-EndpointEffect -Key $effectsKey -Name $slot -Value $backup.PreviousSlot}
        else{$effectsKey.DeleteValue($slot,$false)}
    } elseif($currentValue -ne $expectedPrevious) {throw 'Another application changed the device effect. Its configuration will not be overwritten.'}
}
try {
    if(-not $ResultPath){
        $logDirectory=Join-Path $env:LOCALAPPDATA 'MicFilter\logs'
        New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
        $ResultPath=Join-Path $logDirectory ('installer-'+[guid]::NewGuid().ToString()+'.txt')
    }
    $logPath=[IO.Path]::ChangeExtension($ResultPath,'.log')
    Write-InstallerLog ('Started: '+$Action+'; microphone '+$endpointText)
    . (Join-Path $PSScriptRoot 'installer-registry.ps1')
    $step='Check administrator permissions'
    if(-not $adminCheck.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Windows requires running the installer as administrator.'}
    if(-not [Environment]::Is64BitProcess){throw '64-bit PowerShell is required.'}
    $step='Open microphone configuration'
    if(-not(Test-Path -LiteralPath $endpointPath)){throw 'The selected device no longer exists.'}
    $fxSubKey='SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\'+$endpointText+'\FxProperties'
    $effectsKey=Open-EndpointEffectsKey -SubKey $fxSubKey -CreateIfMissing:($Action -eq 'Install')
    Write-InstallerLog 'Configuration opened with query and set-value permissions.'
    if($Action -eq 'Remove') {
        $step='Restore previous effect'
        if(-not(Test-Path -LiteralPath $backupPath)){throw 'No restore backup exists for this installation.'}
        $backup=Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
        if($backup.EndpointGuid -ne $endpointText){throw 'The restore backup belongs to another device.'}
        Restore-Slot $backup
        $stateFile=Join-Path $stateDir 'state.bin'
        if(Test-Path -LiteralPath $stateFile){$stream=[IO.File]::Open($stateFile,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);try{$stream.Position=8;$stream.Write([BitConverter]::GetBytes([int]0),0,4)}finally{$stream.Dispose()}}
        if(Test-Path -LiteralPath $classPath){Remove-Item -LiteralPath $classPath -Recurse -Force}
        if(Test-Path -LiteralPath $apoPath){Remove-Item -LiteralPath $apoPath -Recurse -Force}
        Remove-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'MicFilter' -ErrorAction SilentlyContinue
        if(Test-Path -LiteralPath $configPath){Remove-Item -LiteralPath $configPath -Recurse -Force}
        Remove-Item -LiteralPath $backupPath -Force
        Write-InstallerLog 'Effect removed; previous configuration restored.'
        Write-InstallerResult ('Effect removed and previous configuration restored.'+[Environment]::NewLine+'Reconnect the microphone or restart Windows to apply the change.'+[Environment]::NewLine+[Environment]::NewLine+'Log: '+$logPath)
    } else {
        $step='Check files and effect chain'
        $sourceRoot=(Resolve-Path -LiteralPath $SourceDir).Path
        foreach($name in @('MicFilter.exe','MicFilterAPO.dll','install.ps1','installer-registry.ps1','LICENSE','RNNOISE-LICENSE.txt','SPEEX-LICENSE.txt')){if(-not(Test-Path -LiteralPath (Join-Path $sourceRoot $name))){throw ('Missing '+$name)}}
        $previousValue=$effectsKey.GetValue($slot)
        if($previousValue -and $previousValue -ne $knownClownfish -and $previousValue -ne $ownClsid -and $legacyClsids -notcontains $previousValue){throw 'This microphone has another effect in this chain position. It is preserved rather than replaced.'}
        foreach($otherSlot in @(1,5,6,7)){
            $otherName='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},'+$otherSlot
            if($effectsKey.GetValue($otherName)){throw 'Another audio effect was detected; review the chain first to avoid double filtering.'}
        }
        $step='Save restore backup'
        New-Item -ItemType Directory -Path $stateDir,$programDir -Force | Out-Null
        if(Test-Path -LiteralPath $backupPath){
            $backup=Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
            if($backup.EndpointGuid -ne $endpointText){throw 'A filter is installed on another microphone. Remove it from the tray menu before choosing another device.'}
        }else{
            $backup=[pscustomobject]@{EndpointGuid=$endpointText;HadSlot=[bool]$previousValue;PreviousSlot=$previousValue;CreatedUtc=[DateTime]::UtcNow.ToString('o');SourceRelease='werman/v1.21'}
            $backup | ConvertTo-Json | Set-Content -LiteralPath $backupPath -Encoding UTF8
        }
        $step='Copy application files'
        foreach($name in @('MicFilter.exe','MicFilterAPO.dll','install.ps1','installer-registry.ps1','LICENSE','RNNOISE-LICENSE.txt','SPEEX-LICENSE.txt')){
            $sourceFile=Join-Path $sourceRoot $name;$destination=Join-Path $programDir $name
            if($sourceFile -ne $destination -and ((-not(Test-Path -LiteralPath $destination)) -or (Get-FileHash -LiteralPath $sourceFile).Hash -ne (Get-FileHash -LiteralPath $destination).Hash)){Copy-Item -LiteralPath $sourceFile -Destination $destination -Force}
        }
        # Payload name used before 0.5; the registered DLL is always the hashed copy below.
        $legacyPayload=Join-Path $programDir 'MicFilterAPO-v4.dll';if(Test-Path -LiteralPath $legacyPayload){Remove-Item -LiteralPath $legacyPayload -Force -ErrorAction SilentlyContinue}
        $dllSource=Join-Path $sourceRoot 'MicFilterAPO.dll'
        $dllHash=(Get-FileHash -LiteralPath $dllSource -Algorithm SHA256).Hash.ToLowerInvariant()
        $dllDestination=Join-Path $programDir ('MicFilterAPO-'+$dllHash.Substring(0,16)+'.dll')
        if(-not(Test-Path -LiteralPath $dllDestination)){Copy-Item -LiteralPath $dllSource -Destination $dllDestination}
        $step='Prepare control file'
        $stateFile=Join-Path $stateDir 'state.bin'
        if(-not(Test-Path -LiteralPath $stateFile)){
            $stateBytes=New-Object byte[] 64
            [BitConverter]::GetBytes([int]0x5741564f).CopyTo($stateBytes,0)
            [BitConverter]::GetBytes([int]1).CopyTo($stateBytes,4)
            [BitConverter]::GetBytes([int]1).CopyTo($stateBytes,8)
            [BitConverter]::GetBytes([int]0).CopyTo($stateBytes,12)
            [BitConverter]::GetBytes([int]20).CopyTo($stateBytes,16)
            [BitConverter]::GetBytes([int]1000).CopyTo($stateBytes,20)
            [IO.File]::WriteAllBytes($stateFile,$stateBytes)
        }
        # Only the non-executable control file is writable by interactive users.
        $stateAcl=Get-Acl -LiteralPath $stateFile
        $usersSid=[Security.Principal.SecurityIdentifier]::new('S-1-5-32-545')
        $stateRule=[Security.AccessControl.FileSystemAccessRule]::new($usersSid,'Read,Write','Allow')
        $stateAcl.SetAccessRule($stateRule);Set-Acl -LiteralPath $stateFile -AclObject $stateAcl
        $stateAcl=Get-Acl -LiteralPath $stateFile
        $serviceSid=[Security.Principal.SecurityIdentifier]::new('S-1-5-19')
        $stateAcl.SetAccessRule([Security.AccessControl.FileSystemAccessRule]::new($serviceSid,'Read,Write','Allow'))
        Set-Acl -LiteralPath $stateFile -AclObject $stateAcl
        $oldDllPath=if(Test-Path -LiteralPath ($classPath+'\InprocServer32')){(Get-Item -LiteralPath ($classPath+'\InprocServer32')).GetValue('')}else{$null}
        try{
            $step='Register effect DLL'
            New-Item -Path ($classPath+'\InprocServer32') -Force | Out-Null
            Set-Item -LiteralPath ($classPath+'\InprocServer32') -Value $dllDestination
            New-ItemProperty -LiteralPath ($classPath+'\InprocServer32') -Name 'ThreadingModel' -PropertyType String -Value 'Both' -Force | Out-Null
            New-Item -Path $apoPath -Force | Out-Null
            $props=@{Flags=15;MajorVersion=0;MinorVersion=4;MinInputConnections=1;MaxInputConnections=1;MinOutputConnections=1;MaxOutputConnections=1;MaxInstances=[uint32]::MaxValue;NumAPOInterfaces=1}
            foreach($entry in $props.GetEnumerator()){New-ItemProperty -LiteralPath $apoPath -Name $entry.Key -Value $entry.Value -PropertyType DWord -Force | Out-Null}
            New-ItemProperty -LiteralPath $apoPath -Name 'FriendlyName' -Value 'MicFilter - RNNoise v1.21' -PropertyType String -Force | Out-Null
            New-ItemProperty -LiteralPath $apoPath -Name 'Copyright' -Value 'GPL-3.0; RNNoise Xiph.Org BSD-3-Clause' -PropertyType String -Force | Out-Null
            $interfaces=@('{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}')
            for($i=0;$i -lt $interfaces.Count;$i++){New-ItemProperty -LiteralPath $apoPath -Name ('APOInterface'+$i) -Value $interfaces[$i] -PropertyType String -Force | Out-Null}
            $step='Attach effect to microphone'
            Set-EndpointEffect -Key $effectsKey -Name $slot -Value $ownClsid
            New-ItemProperty -LiteralPath $configPath -Name 'EndpointGuid' -Value $endpointText -PropertyType String -Force | Out-Null
        }catch{
            $installError=$_
            try{
                if($previousValue){Set-EndpointEffect -Key $effectsKey -Name $slot -Value $previousValue}else{$effectsKey.DeleteValue($slot,$false)}
                if($oldDllPath){Set-Item -LiteralPath ($classPath+'\InprocServer32') -Value $oldDllPath}
                else{if(Test-Path -LiteralPath $classPath){Remove-Item -LiteralPath $classPath -Recurse -Force};if(Test-Path -LiteralPath $apoPath){Remove-Item -LiteralPath $apoPath -Recurse -Force}}
                Write-InstallerLog 'The previous effect was preserved or restored.'
            }catch{Write-InstallerLog ('Restore error: '+$_.Exception.Message)}
            throw $installError
        }
        Write-InstallerLog 'Installation completed. The microphone is associated with MicFilter.'
        Write-InstallerResult ('Installation completed.'+[Environment]::NewLine+'Reconnect the microphone and open a capture app. The tray should show "Filter confirmed".'+[Environment]::NewLine+[Environment]::NewLine+'The previous configuration was saved.'+[Environment]::NewLine+'Log: '+$logPath)
    }
    $exitCode=0
}catch{
    $failure=$_
    $failureMessage='The operation could not be completed.'+[Environment]::NewLine+'Step: '+$step+[Environment]::NewLine+'Error: '+$failure.Exception.Message
    try{
        Write-InstallerLog $failureMessage
        Write-InstallerLog ($failure | Out-String)
        Write-InstallerResult ($failureMessage+[Environment]::NewLine+[Environment]::NewLine+'Log: '+$logPath)
    }catch{Write-Host $failureMessage -ForegroundColor Red}
}finally{if($effectsKey){$effectsKey.Dispose()}}
exit $exitCode
