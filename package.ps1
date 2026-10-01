param([string]$DistributionDirectory=(Join-Path $PSScriptRoot 'dist'),[switch]$RunTests)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot;$distribution=(Resolve-Path -LiteralPath $DistributionDirectory).Path
$compiler=Get-ChildItem -LiteralPath (Join-Path $root 'tools') -Directory -Filter 'llvm-mingw-*' | Select-Object -First 1
if(-not $compiler){throw 'Falta el compilador portátil.'}
$packageBuild=Join-Path $root 'build\package'
New-Item -ItemType Directory -Path $packageBuild -Force | Out-Null
foreach($name in @('setup-install.ps1','LEEME.txt')){[IO.File]::WriteAllText((Join-Path $distribution $name),(Get-Content -LiteralPath (Join-Path $root $name) -Raw -Encoding UTF8),[Text.UTF8Encoding]::new($true))}
$files=@('WavoFilter.exe','WavoFilterAPO-v3.dll','install.ps1','installer-registry.ps1','LICENSE','RNNOISE-LICENSE.txt','SPEEX-LICENSE.txt','setup-install.ps1','LEEME.txt')
$resourceLines=@('#include <windows.h>',('1 RT_MANIFEST "'+((Join-Path $root 'src\setup.manifest') -replace '\\','/')+'"'))
for($i=0;$i -lt $files.Count;$i++){$path=Join-Path $distribution $files[$i];if(-not(Test-Path -LiteralPath $path)){throw ('Falta '+$files[$i])};$resourceLines+=(($i+101).ToString()+' RCDATA "'+($path -replace '\\','/')+'"')}
$rc=Join-Path $packageBuild 'setup.rc';[IO.File]::WriteAllLines($rc,$resourceLines,[Text.UTF8Encoding]::new($false))
$resource=Join-Path $packageBuild 'setup.o'
& (Join-Path $compiler.FullName 'bin\windres.exe') '-i' $rc '-o' $resource '-O' 'coff'
if($LASTEXITCODE -ne 0){throw 'No se pudieron empaquetar los recursos.'}
$setup=Join-Path $distribution 'WavoFilter-Setup.exe'
& (Join-Path $compiler.FullName 'bin\clang++.exe') '-std=c++20' '-O2' '-Wall' '-Wextra' '-static' '-municode' '-DUNICODE' '-D_UNICODE' '-D_WIN32_WINNT=0x0A00' '-fms-extensions' (Join-Path $root 'src\setup.cpp') $resource '-lole32' '-luuid' '-lshell32' '-lpropsys' '-ladvapi32' '-luser32' '-o' $setup
if($LASTEXITCODE -ne 0){throw 'No se pudo compilar el instalador.'}
$hash=(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $distribution 'SHA256SUMS.txt'),$hash+'  WavoFilter-Setup.exe'+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Host ('Instalador listo: '+$setup+' ('+[math]::Round((Get-Item -LiteralPath $setup).Length/1MB,2)+' MB)')
if($RunTests){& (Join-Path $root 'tests\package-tests.ps1') -DistributionDirectory $distribution}
