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
$effectsKey=$null;$changed=$false;$complete=$false;$exitCode=1;$previousVersionLoaded=$false
$oldControls=$null;$oldSlot=$null;$oldDll=$null;$oldConfig=$null;$oldBackup=$null
$logPath=$null;$createdShortcuts=@()
# Console output is for people: short steps and a plain summary. Technical detail goes to the log only.
function Write-Log([string]$Message){if($logPath){[IO.File]::AppendAllText($logPath,[DateTime]::Now.ToString('o')+' '+$Message+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))}}
function Write-Busy([string]$Text,[int]$Tick=0){Write-Host ("`r  "+@('|','/','-','\')[$Tick%4]+'   '+$Text) -NoNewline -ForegroundColor DarkGray}
function Write-Step([string]$Text,[ValidateSet('ok','warn','fail')][string]$Status='ok'){
    $mark=@{ok='  OK  ';warn='  !   ';fail='  X   '}[$Status];$color=@{ok='Green';warn='Yellow';fail='Red'}[$Status]
    Write-Host "`r$mark" -NoNewline -ForegroundColor $color;Write-Host $Text.PadRight(56);Write-Log ($Status.ToUpper()+': '+$Text)
}
function Write-Note([string]$Text,[string]$Color='Gray'){Write-Host ('  '+$Text) -ForegroundColor $Color;Write-Log $Text}
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
    if($errorText){Write-Log $errorText};if($text){Write-Log $text}
    return [pscustomobject]@{Code=$code;Text=$text}
}
function Refresh-SelectedDevice {Restart-CaptureDevice -EndpointText $endpointText -Log {param($Message) Write-Log $Message}}
try {
    $administrator=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if(-not $administrator -or -not [Environment]::Is64BitProcess){throw 'Administrator permission and Windows x64 are required.'}
    New-Item -ItemType Directory -Path (Join-Path $dataDir 'logs') -Force | Out-Null
    $logPath=Join-Path $dataDir ('logs\setup-'+[guid]::NewGuid().ToString('N')+'.log')
    $effectsKey=Open-EndpointEffectsKey -SubKey $fxSubKey -CreateIfMissing
    $oldSlot=$effectsKey.GetValue($effectSlot)
    if(Test-Path -LiteralPath $statePath){$oldControls=Read-State}
    if(Test-Path -LiteralPath $backupPath){$oldBackup=[IO.File]::ReadAllBytes($backupPath);$backup=Get-Content -LiteralPath $backupPath -Raw -Encoding UTF8 | ConvertFrom-Json;if($backup.EndpointGuid -ne $endpointText){throw 'MicFilter is already set up on another microphone. Right-click the MicFilter icon, choose "Remove effect and restore configuration", then run setup again.'}}
    if(Test-Path -LiteralPath $configPath){$oldConfig=(Get-ItemProperty -LiteralPath $configPath).EndpointGuid}
    if(Test-Path -LiteralPath $classPath){$oldDll=(Get-Item -LiteralPath $classPath).GetValue('')}
    foreach($name in @('MicFilter.exe','install.ps1','installer-registry.ps1')){if(Test-Path -LiteralPath (Join-Path $programDir $name)){Copy-Item -LiteralPath (Join-Path $programDir $name) -Destination (Join-Path $source ('previous-'+$name))}}
    if(Get-Process -Name MicFilter,WavoFilter -ErrorAction SilentlyContinue){
        $quit=Run-Native -Executable (Join-Path $source 'MicFilter.exe') -Arguments '--quit' -Name 'close-tray'
        for($i=0;$i -lt 30 -and (Get-Process -Name MicFilter,WavoFilter -ErrorAction SilentlyContinue);$i++){Start-Sleep -Milliseconds 100}
        if(Get-Process -Name MicFilter,WavoFilter -ErrorAction SilentlyContinue){throw 'Close MicFilter before updating it.'}
    }
    Write-Busy 'Installing the filter'
    $installResult=Join-Path $dataDir ('logs\installer-'+[guid]::NewGuid().ToString('N')+'.txt')
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File (Join-Path $source 'install.ps1') -Action Install -EndpointGuid $EndpointGuid -SourceDir $source -ResultPath $installResult -Quiet
    Write-Log ('Installer report: '+$installResult)
    if($LASTEXITCODE -ne 0){
        $reason=Get-Content -LiteralPath $installResult -Encoding UTF8 -ErrorAction SilentlyContinue | Where-Object {$_ -like 'Error: *'} | Select-Object -First 1
        throw $(if($reason){$reason.Substring(7)}else{'Windows did not accept the filter registration.'})
    }
    Write-Step 'Filter installed'
    $changed=$true
    $controls=Read-State
    if($oldControls -and ($oldSlot -eq $ownClsid -or $legacyClsids -contains $oldSlot)){$finalControls=$oldControls}else{$finalControls=$controls;[BitConverter]::GetBytes([int]1).CopyTo($finalControls,8);[BitConverter]::GetBytes([int]0).CopyTo($finalControls,12);[BitConverter]::GetBytes([int]1000).CopyTo($finalControls,20)}
    $testingControls=New-Object byte[] 64;[Array]::Copy($finalControls,$testingControls,24);[BitConverter]::GetBytes([int]1).CopyTo($testingControls,8);Write-Controls $testingControls
    $stream=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);try{$stream.Position=24;$stream.Write((New-Object byte[] 40),0,40)}finally{$stream.Dispose()}
    Write-Busy 'Restarting the microphone'
    $refreshed=Refresh-SelectedDevice
    if($refreshed){Write-Step 'Microphone restarted'}else{Write-Step 'Microphone not restarted automatically' 'warn'}
    $before=Read-State
    for($attempt=0;$attempt -lt 12;$attempt++){
        Write-Busy 'Testing with real audio (nothing is recorded)' $attempt
        $probe=Run-Native -Executable (Join-Path $programDir 'MicFilter.exe') -Arguments '--probe-audio' -Name 'audio-probe'
        if($probe.Text -notmatch 'not detected'){break};Start-Sleep -Milliseconds 500
    }
    $after=Read-State
    $callbacks=[BitConverter]::ToInt64($after,40)-[BitConverter]::ToInt64($before,40)
    $filtered=[BitConverter]::ToInt64($after,48)-[BitConverter]::ToInt64($before,48)
    if($probe.Code -ne 0 -or $probe.Text -notmatch 'Frames=[1-9][0-9]*'){throw 'Your microphone stopped capturing audio with the filter, so it was not installed.'}
    $confirmed=$callbacks -gt 0 -and $filtered -gt 0 -and [BitConverter]::ToInt32($after,32) -eq 1
    Write-Log ('Capture received. Effect callbacks: '+$callbacks+'; filtered frames: '+$filtered)
    if($confirmed){Write-Step 'Tested with real audio'}else{Write-Step 'Audio works; filtering not confirmed yet' 'warn'}
    Write-Controls $finalControls
    Write-Busy 'Adding shortcuts'
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
    Write-Step 'Added to the desktop, Start menu and Settings > Apps'
    $complete=$true;$exitCode=0
    try{
        # Housekeeping after a confirmed install; failures here never roll back the filter.
        $registeredDll=(Get-Item -LiteralPath $classPath).GetValue('')
        $pending=0
        foreach($dll in @(Get-ChildItem -LiteralPath $programDir -Filter 'MicFilterAPO-*.dll' -File)){if($dll.FullName -ne $registeredDll){$pending+=Remove-PathOrSchedule -Path $dll.FullName}}
        # An older effect DLL that cannot be deleted is still loaded by Windows audio (audiodg.exe),
        # which keeps processing with it until Windows restarts.
        $previousVersionLoaded=$pending -gt 0
        $runPath='HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        if(Get-ItemProperty -LiteralPath $runPath -Name 'WavoFilter' -ErrorAction SilentlyContinue){New-ItemProperty -LiteralPath $runPath -Name 'MicFilter' -Value ('"'+$exe+'"') -PropertyType String -Force | Out-Null;Remove-ItemProperty -LiteralPath $runPath -Name 'WavoFilter'}
        $legacyDir=Join-Path $env:ProgramFiles 'WavoFilter'
        if(Remove-LegacyRegistrations){
            $legacyShortcut=Join-Path ($shell.SpecialFolders.Item('AllUsersDesktop')) 'Wavo Filter.lnk'
            if((Test-Path -LiteralPath $legacyShortcut) -and $shell.CreateShortcut($legacyShortcut).TargetPath -like ($legacyDir+'\*')){Remove-Item -LiteralPath $legacyShortcut -Force}
            $pending+=Remove-PathOrSchedule -Path $legacyDir
        }
        if($pending){Write-Log ('Older files still in use by Windows audio will be deleted at the next restart: '+$pending)}
    }catch{Write-Log ('Cleanup of older files skipped: '+$_.Exception.Message)}
    Write-Host ''
    if($previousVersionLoaded){Write-Note 'Almost done: restart Windows to start using the new version.' 'Yellow';Write-Note 'Until then, your microphone keeps using the previous version.'}
    elseif($confirmed){Write-Note 'All set! MicFilter is cleaning up your microphone.' 'Green'}
    else{Write-Note 'Installed. Reconnect the microphone or restart Windows to start filtering.' 'Yellow'}
    if([BitConverter]::ToInt32($finalControls,8)){Write-Note 'Open MicFilter from the desktop or Start menu to change voice options.'}
    else{Write-Note 'The filter is off, as you left it. Open MicFilter and click its icon to turn it on.'}
    Write-Note ('Details: '+$logPath) 'DarkGray'
}catch{
    Write-Step 'MicFilter could not be installed' 'fail'
    Write-Note $_.Exception.Message
    Write-Log ($_ | Out-String)
    if($changed){
        try{
            if($oldSlot){Set-EndpointEffect -Key $effectsKey -Name $effectSlot -Value $oldSlot}else{$effectsKey.DeleteValue($effectSlot,$false)}
            if($oldDll){Set-Item -LiteralPath $classPath -Value $oldDll}else{
                foreach($path in @(('HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid),('HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\'+$ownClsid))){if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path -Recurse -Force}}
            }
            if($oldConfig){New-ItemProperty -LiteralPath $configPath -Name 'EndpointGuid' -Value $oldConfig -PropertyType String -Force | Out-Null}else{if(Test-Path -LiteralPath $configPath){Remove-ItemProperty -LiteralPath $configPath -Name 'EndpointGuid' -ErrorAction SilentlyContinue}}
            if($oldBackup){[IO.File]::WriteAllBytes($backupPath,$oldBackup)}elseif(Test-Path -LiteralPath $backupPath){Remove-Item -LiteralPath $backupPath -Force}
            foreach($shortcutPath in $createdShortcuts){if(Test-Path -LiteralPath $shortcutPath){Remove-Item -LiteralPath $shortcutPath -Force}}
            $restored=Refresh-SelectedDevice;Write-Note 'Your microphone was restored to its previous settings.'
        }catch{Write-Note ('Your microphone could not be fully restored ('+$_.Exception.Message+'). Reconnect it, or uninstall MicFilter from Settings > Apps.') 'Yellow'}
    }
    if($logPath){Write-Note ('Details: '+$logPath) 'DarkGray'}
}finally{
    if(-not $complete){foreach($name in @('MicFilter.exe','install.ps1','installer-registry.ps1')){if(Test-Path -LiteralPath (Join-Path $source ('previous-'+$name))){Copy-Item -LiteralPath (Join-Path $source ('previous-'+$name)) -Destination (Join-Path $programDir $name) -Force}}}
    if(-not $complete -and $oldControls -and (Test-Path -LiteralPath $statePath)){Write-Controls $oldControls}
    if($effectsKey){$effectsKey.Dispose()}
}
exit $exitCode
