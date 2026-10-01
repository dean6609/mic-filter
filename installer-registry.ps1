# Shared constants and helpers, dot-sourced by install.ps1 and setup-install.ps1.
$micFilterVersion='0.5.1'
$ownClsid='{CDB2B27A-3B40-4B79-95AA-123C7136D873}'
# CLSIDs registered by builds released as WavoFilter. Updates and uninstall remove them once unused.
$legacyClsids=@('{54F530A1-D045-4C70-8999-11CF13E0DDAF}','{6C78EB4F-8AE4-4461-BE4A-989C7C14C7B2}')
$effectSlot='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2'
$captureRoot='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'
$uninstallKeyPath='HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\MicFilter'

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
        $value=(Get-Item -LiteralPath $fx).GetValue($effectSlot)
        if($ours -contains $value){[pscustomobject]@{EndpointText=$device.PSChildName;Clsid=$value}}
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
    # Restart a USB media function only when its container uniquely matches the selected input.
    # Other devices are left for a reconnect/reboot, without restarting all Windows audio.
    param([Parameter(Mandatory=$true)][string]$EndpointText,[Parameter(Mandatory=$true)][scriptblock]$Log)
    $endpointId='SWD\MMDEVAPI\{0.0.1.00000000}.'+$EndpointText
    try{
        $container=(Get-PnpDeviceProperty -InstanceId $endpointId -KeyName 'DEVPKEY_Device_ContainerId' -ErrorAction Stop).Data
        $matches=@(Get-PnpDevice -Class Media -PresentOnly | Where-Object InstanceId -Like 'USB\*' | Where-Object {
            $candidate=Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_ContainerId' -ErrorAction SilentlyContinue
            $candidate -and $candidate.Data -eq $container
        })
        if($matches.Count -eq 1){
            & $Log 'Applying the change to the selected USB device...'
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
