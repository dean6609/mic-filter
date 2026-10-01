$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$metadata=Get-Content -LiteralPath (Join-Path $root 'compiler-source.json') -Raw | ConvertFrom-Json
$directory=Join-Path $root ('tools\'+[IO.Path]::GetFileNameWithoutExtension($metadata.name))
if(Test-Path -LiteralPath (Join-Path $directory 'bin\clang++.exe')){Write-Host 'The portable compiler is already available.';exit 0}
New-Item -ItemType Directory -Path (Join-Path $root 'tools') -Force | Out-Null
$archive=Join-Path $root ('tools\'+$metadata.name)
Write-Host ('Downloading compiler from '+$metadata.source)
Invoke-WebRequest -Uri $metadata.source -OutFile $archive
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $metadata.sha256){throw 'Compiler SHA-256 mismatch. The archive will not be extracted.'}
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $root 'tools') -Force
if(-not(Test-Path -LiteralPath (Join-Path $directory 'bin\clang++.exe'))){throw 'Extraction did not produce the expected compiler.'}
Write-Host 'Portable compiler ready; not installed in Windows.'
