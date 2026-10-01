$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$metadata=Get-Content -LiteralPath (Join-Path $root 'compiler-source.json') -Raw | ConvertFrom-Json
$directory=Join-Path $root ('tools\'+[IO.Path]::GetFileNameWithoutExtension($metadata.name))
if(Test-Path -LiteralPath (Join-Path $directory 'bin\clang++.exe')){Write-Host 'El compilador portátil ya está disponible.';exit 0}
New-Item -ItemType Directory -Path (Join-Path $root 'tools') -Force | Out-Null
$archive=Join-Path $root ('tools\'+$metadata.name)
Write-Host ('Descargando el compilador de '+$metadata.source)
Invoke-WebRequest -Uri $metadata.source -OutFile $archive
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $metadata.sha256){throw 'El SHA-256 del compilador no coincide. No se extraerá.'}
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $root 'tools') -Force
if(-not(Test-Path -LiteralPath (Join-Path $directory 'bin\clang++.exe'))){throw 'La extracción no produjo el compilador esperado.'}
Write-Host 'Compilador portátil preparado; no se instaló en Windows.'
