# Shared constants and helpers, dot-sourced by install.ps1 and setup-install.ps1.
$micFilterVersion='0.6.0'
$ownClsid='{CDB2B27A-3B40-4B79-95AA-123C7136D873}'
# CLSIDs registered by builds released as WavoFilter. Updates and uninstall remove them once unused.
$legacyClsids=@('{54F530A1-D045-4C70-8999-11CF13E0DDAF}','{6C78EB4F-8AE4-4461-BE4A-989C7C14C7B2}')
$effectSlot='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2'
$streamEffectSlot='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5'
$streamModesSlot='{d3993a3f-99c2-4402-b5ec-a92a0367664b},5'
# Default capture and communications. Raw streams intentionally retain Windows' bypass semantics.
$streamModes=@('{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}','{98951333-B9CD-48B1-A0A3-FF40682D73F7}')
$captureRoot='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'
$uninstallKeyPath='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\MicFilter'
$machineRunKeyPath='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run'

function Register-MicFilterStartup([string]$Executable) {
    New-Item -Path $machineRunKeyPath -Force | Out-Null
    New-ItemProperty -LiteralPath $machineRunKeyPath -Name 'MicFilter' -Value ('"'+$Executable+'"') -PropertyType String -Force | Out-Null
}

# Version 2 keeps one original backup per endpoint. Read version 1 without losing its restore data.
function Read-InstallationBackups([string]$Path) {
    if(-not(Test-Path -LiteralPath $Path)){return}
    $document=Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    if($document.EndpointGuid){$entries=@($document)}
    elseif($document.Version -eq 2){$entries=@($document.Endpoints)}
    else{throw 'The microphone restore backup has an unsupported format. Nothing was changed.'}
    $seen=@{}
    foreach($entry in $entries){
        if(-not $entry.EndpointGuid -or [guid]$entry.EndpointGuid -eq [guid]::Empty -or $null -eq $entry.PSObject.Properties['HadSlot']){throw 'The microphone restore backup is incomplete. Nothing was changed.'}
        $id='{'+([guid]$entry.EndpointGuid).ToString()+'}'
        if($seen.ContainsKey($id)){throw 'The microphone restore backup contains duplicate entries.'}
        $seen[$id]=$true;$entry.EndpointGuid=$id;$entry
    }
}
function Get-StreamSnapshot($Key) {
    foreach($name in @($streamEffectSlot,$streamModesSlot)){
        $exists=$Key.GetValueNames() -contains $name
        [pscustomobject]@{Name=$name;Exists=$exists;Kind=$(if($exists){$Key.GetValueKind($name).ToString()}else{'String'});Value=$Key.GetValue($name)}
    }
}
function Use-StreamEffect($Key) {
    # Modern effects cause Windows to choose the SFX/MFX/EFX graph instead of LFX/GFX.
    foreach($slot in @(5,6,7)){if($Key.GetValue('{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},'+$slot)){return $true}}
    return $false
}
function Same-Values($Left,$Right) {return (@($Left) -join "`0") -ceq (@($Right) -join "`0")}
function Restore-EndpointSnapshot([Microsoft.Win32.RegistryKey]$Key,$Original,[object[]]$StreamValues=@()) {
    $current=$Key.GetValue($effectSlot)
    if($current -ne $Original -and $current -and $current -ne $ownClsid -and $legacyClsids -notcontains $current){throw 'Another application changed a microphone during setup. Its effect was kept.'}
    # Validate everything before restoring any value. Never overwrite concurrent foreign changes.
    foreach($entry in $StreamValues){
        if($entry.Name -notin @($streamEffectSlot,$streamModesSlot)){throw 'Unknown effect property in the restore backup.'}
        $now=$Key.GetValue($entry.Name)
        if(Same-Values $now $entry.Value){continue}
        $owned=if($entry.Name -eq $streamEffectSlot){$now -eq $ownClsid -or $legacyClsids -contains $now}else{Same-Values $now $streamModes}
        if($now -and -not $owned){throw 'Another application changed a microphone during setup. Its effect was kept.'}
    }
    if($current -ne $Original){if($Original){Set-EndpointEffect -Key $Key -Name $effectSlot -Value $Original}else{$Key.DeleteValue($effectSlot,$false)}}
    foreach($entry in $StreamValues){
        if($entry.Exists){
            if($entry.Kind -eq 'MultiString'){$Key.SetValue($entry.Name,[string[]]@($entry.Value),[Microsoft.Win32.RegistryValueKind]::MultiString)}
            else{$Key.SetValue($entry.Name,$entry.Value,[Microsoft.Win32.RegistryValueKind]$entry.Kind)}
        }else{$Key.DeleteValue($entry.Name,$false)}
    }
}
function Save-InstallationBackups([string]$Path,[object[]]$Entries) {
    $document=[pscustomobject]@{Version=2;Endpoints=@($Entries)}
    $temporary=$Path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try{
        [IO.File]::WriteAllText($temporary,($document | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($true))
        if(Test-Path -LiteralPath $Path){[IO.File]::Replace($temporary,$Path,[NullString]::Value)}else{[IO.File]::Move($temporary,$Path)}
    }finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}
}
function Assert-CompatibleEffectChain($Key) {
    $ours=@($ownClsid)+$legacyClsids
    $value=$Key.GetValue($effectSlot)
    if($value -and $ours -notcontains $value){throw "Another audio app manages this microphone. Choose a different input, or remove that app's effect from this input and try again."}
    foreach($slot in @(1,5,6,7)){
        $value=$Key.GetValue('{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},'+$slot)
        $discoveryOnly=$slot -eq 7 -and $value -eq '{889C03C8-ABAD-4004-BF0A-BC7BB825E166}'
        $ownedStream=$slot -eq 5 -and $ours -contains $value
        if($value -and -not $discoveryOnly -and -not $ownedStream){throw 'This microphone uses manufacturer audio enhancements. Choose another input; its existing effects have been kept.'}
    }
    # Keep driver associations and Microsoft's discovery-only proxy untouched. Neither proves
    # that our legacy effect slot conflicts; setup checks capture and per-input APO activity.
}

# Registry value updates intentionally request QueryValues + SetValue only.
# Windows audio endpoint keys need not allow CreateSubKey / WriteKey.
function Open-EndpointEffectsKey {
    param(
        [Parameter(Mandatory=$true)][string]$SubKey,
        [Microsoft.Win32.RegistryHive]$Hive=[Microsoft.Win32.RegistryHive]::LocalMachine,
        [switch]$CreateIfMissing
    )
    $baseKey=[Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive,[Microsoft.Win32.RegistryView]::Registry64)
    try {
        $rights=[Security.AccessControl.RegistryRights]::QueryValues -bor [Security.AccessControl.RegistryRights]::SetValue
        $key=$baseKey.OpenSubKey($SubKey,[Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,$rights)
        if(-not $key -and $CreateIfMissing){
            $separator=$SubKey.LastIndexOf('\')
            if($separator -le 0){throw 'Invalid effect path.'}
            $parent=$baseKey.OpenSubKey($SubKey.Substring(0,$separator),[Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,([Security.AccessControl.RegistryRights]::ReadKey -bor [Security.AccessControl.RegistryRights]::CreateSubKey))
            if(-not $parent){throw 'The selected device does not exist.'}
            try{$created=$parent.CreateSubKey($SubKey.Substring($separator+1));$created.Dispose()}finally{$parent.Dispose()}
            $key=$baseKey.OpenSubKey($SubKey,[Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,$rights)
        }
        if(-not $key){throw 'The device effect key does not exist.'}
        return $key
    } finally {$baseKey.Dispose()}
}
function Set-EndpointEffect {
    param([Parameter(Mandatory=$true)][Microsoft.Win32.RegistryKey]$Key,
          [Parameter(Mandatory=$true)][string]$Name,
          [Parameter(Mandatory=$true)][string]$Value)
    $Key.SetValue($Name,$Value,[Microsoft.Win32.RegistryValueKind]::String)
    if($Key.GetValue($Name) -ne $Value){throw 'The effect value does not match after writing it.'}
}
# File hashing and ACLs use .NET directly: these scripts must not depend on module autoloading,
# which can fail when Windows PowerShell inherits a PowerShell 7 module path.
function Get-Sha256([Parameter(Mandatory=$true)][string]$Path) {
    $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::OpenRead($Path)
    try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
}
function Get-CaptureEffectSlots {
    # Every capture device whose effect slot holds MicFilter or a WavoFilter build.
    $ours=@($ownClsid)+$legacyClsids
    foreach($device in @(Get-ChildItem -LiteralPath $captureRoot -ErrorAction SilentlyContinue)){
        $fx=Join-Path $device.PSPath 'FxProperties'
        if(-not(Test-Path -LiteralPath $fx)){continue}
        $key=Get-Item -LiteralPath $fx
        foreach($slot in @($effectSlot,$streamEffectSlot)){
            $value=$key.GetValue($slot)
            if($ours -contains $value){[pscustomobject]@{EndpointText=$device.PSChildName;Clsid=$value};break}
        }
    }
}
function Remove-LegacyRegistrations {
    # Drops WavoFilter CLSIDs that no microphone references. Returns $true when none remain in use.
    $used=@(Get-CaptureEffectSlots | ForEach-Object Clsid)
    foreach($clsid in $legacyClsids){
        if($used -contains $clsid){continue}
        foreach($path in @(('HKLM:\SOFTWARE\Classes\CLSID\'+$clsid),('HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\'+$clsid))){
            if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path -Recurse -Force}
        }
    }
    return -not($used | Where-Object {$legacyClsids -contains $_})
}
function Restart-CaptureDevice {
    # Restart a USB or Bluetooth hands-free media function only when its container uniquely matches the selected input.
    # Other devices are left for a reconnect/reboot, without restarting all Windows audio.
    param([Parameter(Mandatory=$true)][string]$EndpointText,[Parameter(Mandatory=$true)][scriptblock]$Log)
    $endpointId='SWD\MMDEVAPI\{0.0.1.00000000}.'+$EndpointText
    try{
        $container=(Get-PnpDeviceProperty -InstanceId $endpointId -KeyName 'DEVPKEY_Device_ContainerId' -ErrorAction Stop).Data
        $matches=@(Get-PnpDevice -Class Media -PresentOnly | Where-Object {$_.InstanceId -like 'USB\*' -or $_.InstanceId -like 'BTHHFENUM\*'} | Where-Object {
            $candidate=Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_ContainerId' -ErrorAction SilentlyContinue
            $candidate -and $candidate.Data -eq $container
        })
        if($matches.Count -eq 1){
            & $Log 'Applying the change to the selected microphone device...'
            $result=& "$env:SystemRoot\System32\pnputil.exe" /restart-device $matches[0].InstanceId 2>&1
            & $Log ($result -join [Environment]::NewLine)
            if($LASTEXITCODE -eq 0){return $true}
        }
    }catch{& $Log ('The audio path needs reconnecting or rebooting: '+$_.Exception.Message)}
    return $false
}
function Remove-PathOrSchedule {
    # Deletes a file or folder. Anything Windows still holds open (the effect DLL inside
    # audiodg.exe, the mapped state file, a running executable) is deleted at the next restart.
    # Returns the number of entries left for the restart.
    param([Parameter(Mandatory=$true)][string]$Path)
    if(-not(Test-Path -LiteralPath $Path)){return 0}
    try{Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop;return 0}catch{}
    if(-not('MicFilter.NativeFile' -as [type])){
        Add-Type -Namespace MicFilter -Name NativeFile -MemberDefinition '[DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern bool MoveFileEx(string existing,string replacement,int flags);'
    }
    # [NullString]::Value: PowerShell would pass $null as an empty string, which MoveFileEx rejects.
    $delayUntilReboot=4;$pending=0
    foreach($file in @(Get-ChildItem -LiteralPath $Path -Recurse -Force -File -ErrorAction SilentlyContinue)){
        try{Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop}
        catch{if([MicFilter.NativeFile]::MoveFileEx($file.FullName,[NullString]::Value,$delayUntilReboot)){$pending++}else{throw ('Could not delete '+$file.FullName+' (Windows error '+[Runtime.InteropServices.Marshal]::GetLastWin32Error()+')')}}
    }
    if(Test-Path -LiteralPath $Path -PathType Container){
        # Deepest folders first: Windows runs queued deletions in order, after the files.
        $folders=@(Get-ChildItem -LiteralPath $Path -Recurse -Force -Directory | Sort-Object {$_.FullName.Length} -Descending | ForEach-Object FullName)+$Path
        foreach($folder in $folders){
            if(@(Get-ChildItem -LiteralPath $folder -Force).Count -eq 0){Remove-Item -LiteralPath $folder -Force}
            elseif([MicFilter.NativeFile]::MoveFileEx($folder,[NullString]::Value,$delayUntilReboot)){$pending++}
        }
    }
    return $pending
}
