param([Parameter(Mandatory=$true)][guid]$EndpointGuid)
$ErrorActionPreference='Stop'
$source=$PSScriptRoot
$endpointText='{'+$EndpointGuid.ToString()+'}'
$ownClsid='{54F530A1-D045-4C70-8999-11CF13E0DDAF}'
$legacyClsid='{6C78EB4F-8AE4-4461-BE4A-989C7C14C7B2}'
$slot='{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2'
$programDir=Join-Path $env:ProgramFiles 'WavoFilter'
$dataDir=Join-Path $env:ProgramData 'WavoFilter'
$statePath=Join-Path $dataDir 'state.bin'
$backupPath=Join-Path $dataDir 'installation.json'
$configPath='HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid
$classPath='HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid+'\InprocServer32'
$fxSubKey='SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture\'+$endpointText+'\FxProperties'
$effectsKey=$null;$changed=$false;$complete=$false;$exitCode=1
$oldControls=$null;$oldSlot=$null;$oldDll=$null;$oldConfig=$null;$oldBackup=$null
$logPath=$null;$shortcutPath=$null;$shortcutExisted=$false
function Write-ProgressLine([string]$Message){Write-Host $Message;if($logPath){[IO.File]::AppendAllText($logPath,[DateTime]::Now.ToString('o')+' '+$Message+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))}}
function Read-State {
    $stream=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try{$bytes=New-Object byte[] 64;if($stream.Read($bytes,0,64) -ne 64){throw 'El archivo de estado está incompleto.'};return ,$bytes}finally{$stream.Dispose()}
}
function Write-Controls([byte[]]$Controls){$stream=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);try{$stream.Write($Controls,0,24);$stream.Flush()}finally{$stream.Dispose()}}
function Run-Native([string]$Executable,[string]$Arguments,[string]$Name){
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Executable;$start.Arguments=$Arguments
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false);$start.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(15000)){$process.Kill();$process.Dispose();throw ('El proceso '+$Name+' no terminó a tiempo.')}
    $process.WaitForExit();$text=$stdout.Result;$errorText=$stderr.Result;$code=$process.ExitCode;$process.Dispose()
    if($errorText){Write-ProgressLine $errorText};if($text){Write-ProgressLine $text}
    return [pscustomobject]@{Code=$code;Text=$text}
}
function Refresh-SelectedDevice {
    # Restart a USB media function only when its container uniquely matches the selected input.
    # Other devices are left for a reconnect/reboot, without restarting all Windows audio.
    $endpointId='SWD\MMDEVAPI\{0.0.1.00000000}.'+$endpointText
    try{
        $container=(Get-PnpDeviceProperty -InstanceId $endpointId -KeyName 'DEVPKEY_Device_ContainerId' -ErrorAction Stop).Data
        $matches=@(Get-PnpDevice -Class Media -PresentOnly | Where-Object InstanceId -Like 'USB\*' | Where-Object {
            $candidate=Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_ContainerId' -ErrorAction SilentlyContinue
            $candidate -and $candidate.Data -eq $container
        })
        if($matches.Count -eq 1){
            Write-ProgressLine 'Aplicando el cambio al dispositivo USB seleccionado…'
            $result=& "$env:SystemRoot\System32\pnputil.exe" /restart-device $matches[0].InstanceId 2>&1
            Write-ProgressLine ($result -join [Environment]::NewLine)
            if($LASTEXITCODE -eq 0){return $true}
        }
    }catch{Write-ProgressLine ('La ruta requiere reconectar o reiniciar: '+$_.Exception.Message)}
    return $false
}
try {
    $administrator=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if(-not $administrator -or -not [Environment]::Is64BitProcess){throw 'Se requiere permiso de administrador y Windows x64.'}
    New-Item -ItemType Directory -Path (Join-Path $dataDir 'logs') -Force | Out-Null
    $logPath=Join-Path $dataDir ('logs\setup-'+[guid]::NewGuid().ToString('N')+'.log')
    . (Join-Path $source 'installer-registry.ps1')
    $effectsKey=Open-EndpointEffectsKey -SubKey $fxSubKey -CreateIfMissing
    $oldSlot=$effectsKey.GetValue($slot)
    if(Test-Path -LiteralPath $statePath){$oldControls=Read-State}
    if(Test-Path -LiteralPath $backupPath){$oldBackup=[IO.File]::ReadAllBytes($backupPath);$backup=Get-Content -LiteralPath $backupPath -Raw -Encoding UTF8 | ConvertFrom-Json;if($backup.EndpointGuid -ne $endpointText){throw 'Ya hay un filtro en otro micrófono. Retíralo desde el menú del icono y después ejecuta este instalador para cambiar de dispositivo.'}}
    if(Test-Path -LiteralPath $configPath){$oldConfig=(Get-ItemProperty -LiteralPath $configPath).EndpointGuid}
    if(Test-Path -LiteralPath $classPath){$oldDll=(Get-Item -LiteralPath $classPath).GetValue('')}
    foreach($name in @('WavoFilter.exe','install.ps1','installer-registry.ps1')){if(Test-Path -LiteralPath (Join-Path $programDir $name)){Copy-Item -LiteralPath (Join-Path $programDir $name) -Destination (Join-Path $source ('previous-'+$name))}}
    if(Get-Process -Name WavoFilter -ErrorAction SilentlyContinue){
        $quit=Run-Native -Executable (Join-Path $source 'WavoFilter.exe') -Arguments '--quit' -Name 'close-tray'
        for($i=0;$i -lt 30 -and (Get-Process -Name WavoFilter -ErrorAction SilentlyContinue);$i++){Start-Sleep -Milliseconds 100}
        if(Get-Process -Name WavoFilter -ErrorAction SilentlyContinue){throw 'Cierra el programa Wavo Filter antes de actualizarlo.'}
    }
    Write-ProgressLine '[3/5] Instalando el filtro y guardando la configuración anterior…'
    $installResult=Join-Path $dataDir ('logs\installer-'+[guid]::NewGuid().ToString('N')+'.txt')
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File (Join-Path $source 'install.ps1') -Action Install -EndpointGuid $EndpointGuid -SourceDir $source -ResultPath $installResult
    if($LASTEXITCODE -ne 0){throw ('El instalador rechazó la operación. Detalle: '+$installResult)}
    $changed=$true
    $controls=Read-State
    if($oldControls -and ($oldSlot -eq $ownClsid -or $oldSlot -eq $legacyClsid)){$finalControls=$oldControls}else{$finalControls=$controls;[BitConverter]::GetBytes([int]1).CopyTo($finalControls,8);[BitConverter]::GetBytes([int]0).CopyTo($finalControls,12);[BitConverter]::GetBytes([int]1000).CopyTo($finalControls,20)}
    $testingControls=New-Object byte[] 64;[Array]::Copy($finalControls,$testingControls,24);[BitConverter]::GetBytes([int]1).CopyTo($testingControls,8);Write-Controls $testingControls
    $stream=[IO.File]::Open($statePath,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);try{$stream.Position=24;$stream.Write((New-Object byte[] 40),0,40)}finally{$stream.Dispose()}
    $refreshed=Refresh-SelectedDevice
    Write-ProgressLine '[4/5] Comprobando el audio real (sin guardar grabaciones)…'
    $before=Read-State
    for($attempt=0;$attempt -lt 12;$attempt++){
        $probe=Run-Native -Executable (Join-Path $programDir 'WavoFilter.exe') -Arguments '--probe-audio' -Name 'audio-probe'
        if($probe.Text -notmatch 'no detectado'){break};Start-Sleep -Milliseconds 500
    }
    $after=Read-State
    $callbacks=[BitConverter]::ToInt64($after,40)-[BitConverter]::ToInt64($before,40)
    $filtered=[BitConverter]::ToInt64($after,48)-[BitConverter]::ToInt64($before,48)
    if($probe.Code -ne 0 -or $probe.Text -notmatch 'Frames=[1-9][0-9]*'){throw 'La captura de audio no funciona con este efecto. Se restaurará la configuración anterior.'}
    $confirmed=$callbacks -gt 0 -and $filtered -gt 0 -and [BitConverter]::ToInt32($after,32) -eq 1
    Write-ProgressLine ('Captura recibida. Callbacks del efecto: '+$callbacks+'; frames filtrados: '+$filtered)
    Write-Controls $finalControls
    Write-ProgressLine '[5/5] Creando el acceso directo del escritorio…'
    $shell=New-Object -ComObject WScript.Shell
    $shortcutPath=Join-Path ($shell.SpecialFolders.Item('AllUsersDesktop')) 'Wavo Filter.lnk'
    $shortcutExisted=Test-Path -LiteralPath $shortcutPath
    $shortcut=$shell.CreateShortcut($shortcutPath);$shortcut.TargetPath=Join-Path $programDir 'WavoFilter.exe';$shortcut.WorkingDirectory=$programDir;$shortcut.Description='Activar o desactivar la reducción de ruido del micrófono';$shortcut.Save()
    Copy-Item -LiteralPath (Join-Path $source 'LEEME.txt') -Destination (Join-Path $programDir 'LEEME.txt') -Force
    $complete=$true;$exitCode=0
    if($confirmed){Write-ProgressLine 'LISTO: el filtro procesó audio real correctamente.'}
    else{Write-ProgressLine 'INSTALADO, pendiente de confirmar: reconecta el micrófono o reinicia Windows y revisa el diagnóstico mientras grabas. Si no aparece «Filtro confirmado», retira el efecto desde el icono.'}
    if([BitConverter]::ToInt32($finalControls,8)){Write-ProgressLine 'El filtro está activado y conservará este ajuste al reiniciar.'}else{Write-ProgressLine 'Se conservó tu ajuste anterior: filtro desactivado. Abre el programa y haz clic en el icono para activarlo.'}
    Write-ProgressLine 'Abre «Wavo Filter» desde el escritorio para ver el icono y sus opciones.'
    Write-ProgressLine 'Un clic: activar/desactivar. Salir: desactivar y cerrar. Abrir no lo activa automáticamente.'
    Write-ProgressLine ('Registro: '+$logPath)
}catch{
    Write-ProgressLine ('ERROR: '+$_.Exception.Message)
    if($changed){
        try{
            if($oldSlot){Set-EndpointEffect -Key $effectsKey -Name $slot -Value $oldSlot}else{$effectsKey.DeleteValue($slot,$false)}
            if($oldDll){Set-Item -LiteralPath $classPath -Value $oldDll}else{
                foreach($path in @(('HKLM:\SOFTWARE\Classes\CLSID\'+$ownClsid),('HKLM:\SOFTWARE\Classes\AudioEngine\AudioProcessingObjects\'+$ownClsid))){if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path -Recurse -Force}}
            }
            if($oldConfig){New-ItemProperty -LiteralPath $configPath -Name 'EndpointGuid' -Value $oldConfig -PropertyType String -Force | Out-Null}else{if(Test-Path -LiteralPath $configPath){Remove-ItemProperty -LiteralPath $configPath -Name 'EndpointGuid' -ErrorAction SilentlyContinue}}
            if($oldBackup){[IO.File]::WriteAllBytes($backupPath,$oldBackup)}elseif(Test-Path -LiteralPath $backupPath){Remove-Item -LiteralPath $backupPath -Force}
            if($shortcutPath -and -not $shortcutExisted -and (Test-Path -LiteralPath $shortcutPath)){Remove-Item -LiteralPath $shortcutPath -Force}
            $restored=Refresh-SelectedDevice;Write-ProgressLine 'Se restauró la asociación anterior del micrófono.'
        }catch{Write-ProgressLine ('Error al restaurar: '+$_.Exception.Message+'. Conserva este registro y reconecta el micrófono.')}
    }
    if($logPath){Write-ProgressLine ('Registro: '+$logPath)}
}finally{
    if(-not $complete){foreach($name in @('WavoFilter.exe','install.ps1','installer-registry.ps1')){if(Test-Path -LiteralPath (Join-Path $source ('previous-'+$name))){Copy-Item -LiteralPath (Join-Path $source ('previous-'+$name)) -Destination (Join-Path $programDir $name) -Force}}}
    if(-not $complete -and $oldControls -and (Test-Path -LiteralPath $statePath)){Write-Controls $oldControls}
    if($effectsKey){$effectsKey.Dispose()}
}
exit $exitCode
