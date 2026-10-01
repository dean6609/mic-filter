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
            if($separator -le 0){throw 'Ruta de efectos inválida.'}
            $parent=$baseKey.OpenSubKey($SubKey.Substring(0,$separator),[Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,([Security.AccessControl.RegistryRights]::ReadKey -bor [Security.AccessControl.RegistryRights]::CreateSubKey))
            if(-not $parent){throw 'No existe el dispositivo seleccionado.'}
            try{$created=$parent.CreateSubKey($SubKey.Substring($separator+1));$created.Dispose()}finally{$parent.Dispose()}
            $key=$baseKey.OpenSubKey($SubKey,[Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree,$rights)
        }
        if(-not $key){throw 'No existe la clave de efectos del dispositivo.'}
        return $key
    } finally {$baseKey.Dispose()}
}
function Set-EndpointEffect {
    param([Parameter(Mandatory=$true)][Microsoft.Win32.RegistryKey]$Key,
          [Parameter(Mandatory=$true)][string]$Name,
          [Parameter(Mandatory=$true)][string]$Value)
    $Key.SetValue($Name,$Value,[Microsoft.Win32.RegistryValueKind]::String)
    if($Key.GetValue($Name) -ne $Value){throw 'El valor del efecto no coincide después de escribirlo.'}
}
