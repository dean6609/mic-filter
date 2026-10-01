param([Parameter(Mandatory=$true)][guid]$EndpointGuid)
$ErrorActionPreference='Stop'
$source=$PSScriptRoot
$endpointText='{'+$EndpointGuid.ToString()+'}'
. (Join-Path $source 'installer-registry.ps1')
$programDir=Join-Path $env:ProgramFiles 'MicFilter'
$dataDir=Join-Path $env:ProgramData 'MicFilter'
if(-not(Test-Path -LiteralPath (Join-Path $dataDir 'state.bin')) -and (Test-Path -LiteralPath (Join-Path $env:ProgramData 'WavoFilter\state.bin'))){$dataDir=Join-Path $env:ProgramData 'WavoFilter'}
$statePath=Join-Path $dataDir 'state.bin'
$backupPath=Join-Path $dataDir 'installation.json'
$configPath='HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid
$classPath='HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid+'\InprocServer32'
$fxSubKey='SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\'+$endpointText+'\FxProperties'
$effectsKey=$null;$changed=$false;$complete=$false;$exitCode=1
$oldControls=$null;$oldSlot=$null;$oldDll=$null;$oldConfig=$null;$oldBackup=$null
$logPath=$null;$createdShortcuts=@()
function Write-ProgressLine([string]$Message){Write-Host $Message;if($logPath){[IO.File]::AppendAllText($logPath,[DateTime]::Now.ToString('o')+' '+$Message+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))}}
function Read-State {
    $stream=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try{$bytes=New-Object byte[] 64;if($stream.Read($bytes,0,64) -ne 64){throw 'The state file is incomplete.'};return ,$bytes}finally{$stream.Dispose()}
}
function Write-Controls([byte[]]$Controls){$stream=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);try{$stream.Write($Controls,0,24);$stream.Flush()}finally{$stream.Dispose()}}
function Run-Native([string]$Executable,[string]$Arguments,[string]$Name){
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Executable;$start.Arguments=$Arguments
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false);$start.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(15000)){$process.Kill();$process.Dispose();throw ('Process '+$Name+' did not finish within the time limit.')}
    $process.WaitForExit();$text=$stdout.Result;$errorText=$stderr.Result;$code=$process.ExitCode;$process.Dispose()
    if($errorText){Write-ProgressLine $errorText};if($text){Write-ProgressLine $text}
    return [pscustomobject]@{Code=$code;Text=$text}
}
function Refresh-SelectedDevice {Restart-CaptureDevice -EndpointText $endpointText -Log {param($Message) Write-ProgressLine $Message}}
try {
    $administrator=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if(-not $administrator -or -not [Environment]::Is64BitProcess){throw 'Administrator permission and Windows x64 are required.'}
    New-Item -ItemType Directory -Path (Join-Path $dataDir 'logs') -Force | Out-Null
    $logPath=Join-Path $dataDir ('logs\setup-'+[guid]::NewGuid().ToString('N')+'.log')
    $effectsKey=Open-EndpointEffectsKey -SubKey $fxSubKey -CreateIfMissing
    $oldSlot=$effectsKey.GetValue($effectSlot)
    if(Test-Path -LiteralPath $statePath){$oldControls=Read-State}
    if(Test-Path -LiteralPath $backupPath){$oldBackup=[IO.File]::ReadAllBytes($backupPath);$backup=Get-Content -LiteralPath $backupPath -Raw -Encoding UTF8 | ConvertFrom-Json;if($backup.EndpointGuid -ne $endpointText){throw 'A filter is installed on another microphone. Remove it from the tray menu, then run this installer to choose another device.'}}
    if(Test-Path -LiteralPath $configPath){$oldConfig=(Get-ItemProperty -LiteralPath $configPath).EndpointGuid}
    if(Test-Path -LiteralPath $classPath){$oldDll=(Get-Item -LiteralPath $classPath).GetValue('')}
    foreach($name in @('MicFilter.exe','install.ps1','installer-registry.ps1')){if(Test-Path -LiteralPath (Join-Path $programDir $name)){Copy-Item -LiteralPath (Join-Path $programDir $name) -Destination (Join-Path $source ('previous-'+$name))}}
    if(Get-Process -Name MicFilter,WavoFilter -ErrorAction SilentlyContinue){
        $quit=Run-Native -Executable (Join-Path $source 'MicFilter.exe') -Arguments '--quit' -Name 'close-tray'
        for($i=0;$i -lt 30 -and (Get-Process -Name MicFilter,WavoFilter -ErrorAction SilentlyContinue);$i++){Start-Sleep -Milliseconds 100}
        if(Get-Process -Name MicFilter,WavoFilter -ErrorAction SilentlyContinue){throw 'Close MicFilter before updating it.'}
    }
    Write-ProgressLine '[3/5] Installing the filter and saving the previous configuration...'
    $installResult=Join-Path $dataDir ('logs\installer-'+[guid]::NewGuid().ToString('N')+'.txt')
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File (Join-Path $source 'install.ps1') -Action Install -EndpointGuid $EndpointGuid -SourceDir $source -ResultPath $installResult
    if($LASTEXITCODE -ne 0){throw ('The installer rejected the operation. Details: '+$installResult)}
    $changed=$true
    $controls=Read-State
    if($oldControls -and ($oldSlot -eq $ownClsid -or $legacyClsids -contains $oldSlot)){$finalControls=$oldControls}else{$finalControls=$controls;[BitConverter]::GetBytes([int]1).CopyTo($finalControls,8);[BitConverter]::GetBytes([int]0).CopyTo($finalControls,12);[BitConverter]::GetBytes([int]1000).CopyTo($finalControls,20)}
    $testingControls=New-Object byte[] 64;[Array]::Copy($finalControls,$testingControls,24);[BitConverter]::GetBytes([int]1).CopyTo($testingControls,8);Write-Controls $testingControls
    $stream=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);try{$stream.Position=24;$stream.Write((New-Object byte[] 40),0,40)}finally{$stream.Dispose()}
    $refreshed=Refresh-SelectedDevice
    Write-ProgressLine '[4/5] Checking real audio capture (no recordings saved)...'
    $before=Read-State
    for($attempt=0;$attempt -lt 12;$attempt++){
        $probe=Run-Native -Executable (Join-Path $programDir 'MicFilter.exe') -Arguments '--probe-audio' -Name 'audio-probe'
        if($probe.Text -notmatch 'not detected'){break};Start-Sleep -Milliseconds 500
    }
    $after=Read-State
    $callbacks=[BitConverter]::ToInt64($after,40)-[BitConverter]::ToInt64($before,40)
    $filtered=[BitConverter]::ToInt64($after,48)-[BitConverter]::ToInt64($before,48)
    if($probe.Code -ne 0 -or $probe.Text -notmatch 'Frames=[1-9][0-9]*'){throw 'Audio capture failed with this effect. The previous configuration will be restored.'}
    $confirmed=$callbacks -gt 0 -and $filtered -gt 0 -and [BitConverter]::ToInt32($after,32) -eq 1
    Write-ProgressLine ('Capture received. Effect callbacks: '+$callbacks+'; filtered frames: '+$filtered)
    Write-Controls $finalControls
    Write-ProgressLine '[5/5] Creating shortcuts and the Windows uninstall entry...'
    $shell=New-Object -ComObject WScript.Shell
    foreach($folder in @('AllUsersDesktop','AllUsersPrograms')){
        $shortcutPath=Join-Path ($shell.SpecialFolders.Item($folder)) 'MicFilter.lnk'
        if(-not(Test-Path -LiteralPath $shortcutPath)){$createdShortcuts+=$shortcutPath}
        $shortcut=$shell.CreateShortcut($shortcutPath);$shortcut.TargetPath=Join-Path $programDir 'MicFilter.exe';$shortcut.WorkingDirectory=$programDir;$shortcut.Description='Enable or disable microphone noise suppression';$shortcut.Save()
    }
    Copy-Item -LiteralPath (Join-Path $source 'GETTING_STARTED.txt') -Destination (Join-Path $programDir 'GETTING_STARTED.txt') -Force
    # Settings > Apps lists MicFilter and runs its uninstaller from here.
    New-Item -Path $uninstallKeyPath -Force | Out-Null
    $exe=Join-Path $programDir 'MicFilter.exe'
    $sizeKb=[int]((Get-ChildItem -LiteralPath $programDir -File | Measure-Object -Property Length -Sum).Sum/1KB)
    $entries=@{DisplayName='MicFilter';DisplayVersion=$micFilterVersion;Publisher='MicFilter contributors';InstallLocation=$programDir;DisplayIcon=$exe;UninstallString=('"'+$exe+'" --uninstall');URLInfoAbout='https://github.com/dean6609/mic-filter';HelpLink='https://github.com/dean6609/mic-filter/issues';InstallDate=[DateTime]::Now.ToString('yyyyMMdd')}
    foreach($entry in $entries.GetEnumerator()){New-ItemProperty -LiteralPath $uninstallKeyPath -Name $entry.Key -Value $entry.Value -PropertyType String -Force | Out-Null}
    foreach($entry in @{NoModify=1;NoRepair=1;EstimatedSize=$sizeKb}.GetEnumerator()){New-ItemProperty -LiteralPath $uninstallKeyPath -Name $entry.Key -Value $entry.Value -PropertyType DWord -Force | Out-Null}
    $complete=$true;$exitCode=0
    try{
        # Housekeeping after a confirmed install; failures here never roll back the filter.
        $registeredDll=(Get-Item -LiteralPath $classPath).GetValue('')
        $pending=0
        foreach($dll in @(Get-ChildItem -LiteralPath $programDir -Filter 'MicFilterAPO-*.dll' -File)){if($dll.FullName -ne $registeredDll){$pending+=Remove-PathOrSchedule -Path $dll.FullName}}
        $runPath='HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        if(Get-ItemProperty -LiteralPath $runPath -Name 'WavoFilter' -ErrorAction SilentlyContinue){New-ItemProperty -LiteralPath $runPath -Name 'MicFilter' -Value ('"'+$exe+'"') -PropertyType String -Force | Out-Null;Remove-ItemProperty -LiteralPath $runPath -Name 'WavoFilter'}
        $legacyDir=Join-Path $env:ProgramFiles 'WavoFilter'
        if(Remove-LegacyRegistrations){
            $legacyShortcut=Join-Path ($shell.SpecialFolders.Item('AllUsersDesktop')) 'Wavo Filter.lnk'
            if((Test-Path -LiteralPath $legacyShortcut) -and $shell.CreateShortcut($legacyShortcut).TargetPath -like ($legacyDir+'\*')){Remove-Item -LiteralPath $legacyShortcut -Force}
            $pending+=Remove-PathOrSchedule -Path $legacyDir
        }
        if($pending){Write-ProgressLine ('Older files still in use by Windows audio will be deleted at the next restart: '+$pending)}
    }catch{Write-ProgressLine ('Cleanup of older files skipped: '+$_.Exception.Message)}
    if($confirmed){Write-ProgressLine 'READY: the filter processed real audio successfully.'}
    else{Write-ProgressLine 'INSTALLED, pending confirmation: reconnect the microphone or restart Windows, then check diagnostics while recording. If "Filter confirmed" does not appear, remove the effect from the tray menu.'}
    if([BitConverter]::ToInt32($finalControls,8)){Write-ProgressLine 'The filter is enabled and will keep this setting after reboot.'}else{Write-ProgressLine 'Previous setting preserved: filter disabled. Open the app and click the tray icon to enable it.'}
    Write-ProgressLine 'Open MicFilter from the desktop or Start menu to see its tray icon and options.'
    Write-ProgressLine 'To uninstall: Settings > Apps > Installed apps > MicFilter > Uninstall.'
    Write-ProgressLine 'One click: enable/disable. Exit: disable and close. Opening the app does not enable it automatically.'
    Write-ProgressLine ('Log: '+$logPath)
}catch{
    Write-ProgressLine ('ERROR: '+$_.Exception.Message)
    if($changed){
        try{
            if($oldSlot){Set-EndpointEffect -Key $effectsKey -Name $effectSlot -Value $oldSlot}else{$effectsKey.DeleteValue($effectSlot,$false)}
            if($oldDll){Set-Item -LiteralPath $classPath -Value $oldDll}else{
                foreach($path in @(('HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid),('HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\'+$ownClsid))){if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path -Recurse -Force}}
            }
            if($oldConfig){New-ItemProperty -LiteralPath $configPath -Name 'EndpointGuid' -Value $oldConfig -PropertyType String -Force | Out-Null}else{if(Test-Path -LiteralPath $configPath){Remove-ItemProperty -LiteralPath $configPath -Name 'EndpointGuid' -ErrorAction SilentlyContinue}}
            if($oldBackup){[IO.File]::WriteAllBytes($backupPath,$oldBackup)}elseif(Test-Path -LiteralPath $backupPath){Remove-Item -LiteralPath $backupPath -Force}
            foreach($shortcutPath in $createdShortcuts){if(Test-Path -LiteralPath $shortcutPath){Remove-Item -LiteralPath $shortcutPath -Force}}
            $restored=Refresh-SelectedDevice;Write-ProgressLine 'The previous microphone effect association was restored.'
        }catch{Write-ProgressLine ('Restore error: '+$_.Exception.Message+'. Keep this log and reconnect the microphone.')}
    }
    if($logPath){Write-ProgressLine ('Log: '+$logPath)}
}finally{
    if(-not $complete){foreach($name in @('MicFilter.exe','install.ps1','installer-registry.ps1')){if(Test-Path -LiteralPath (Join-Path $source ('previous-'+$name))){Copy-Item -LiteralPath (Join-Path $source ('previous-'+$name)) -Destination (Join-Path $programDir $name) -Force}}}
    if(-not $complete -and $oldControls -and (Test-Path -LiteralPath $statePath)){Write-Controls $oldControls}
    if($effectsKey){$effectsKey.Dispose()}
}
exit $exitCode
