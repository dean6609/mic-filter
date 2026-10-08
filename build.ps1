param([switch]$RunTests,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$compilerFolder=Get-ChildItem -LiteralPath (Join-Path $root 'tools') -Directory -Filter 'llvm-mingw-*' | Select-Object -First 1
if(-not $compilerFolder){throw 'Portable compiler missing in tools. See CONTRIBUTING.md.'}
$cc=Join-Path $compilerFolder.FullName 'bin\clang.exe'
$cxx=Join-Path $compilerFolder.FullName 'bin\clang++.exe'
$rn=Join-Path $root 'vendor\rnnoise'
$output=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $root 'dist'}
$objects=Join-Path $root 'build'
New-Item -ItemType Directory -Path $output,$objects -Force | Out-Null
$common=@('-O2','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-fms-extensions',('-I'+(Join-Path $rn 'include')),('-I'+(Join-Path $root 'src')))
$rnSources=@('celt_lpc','denoise','kiss_fft','nnet','nnet_default','parse_lpcnet_weights','pitch','rnn','rnnoise_tables','rnnoise_data')
$rnObjects=@()
$resampler=Join-Path $root 'vendor\speex-resampler'
$common+=@('-DOUTSIDE_SPEEX','-DRANDOM_PREFIX=micfilter','-DFLOATING_POINT',('-I'+$resampler))
$resamplerObject=Join-Path $objects 'resample.o'
& $cc @common '-c' (Join-Path $resampler 'resample.c') '-o' $resamplerObject
if($LASTEXITCODE -ne 0){throw 'Could not compile the resampler'}
$rnObjects+=$resamplerObject
foreach($file in $rnSources){
    $obj=Join-Path $objects ($file+'.o');$rnObjects+=$obj
    & $cc @common '-DDISABLE_DEBUG_FLOAT' '-DRNN_ENABLE_X86_RTCD' '-DCPU_INFO_BY_ASM' ('-I'+(Join-Path $rn 'src')) '-c' (Join-Path $rn ('src\'+$file+'.c')) '-o' $obj
    if($LASTEXITCODE -ne 0){throw ('Could not compile '+$file)}
}
foreach($file in @('x86cpu','x86_dnn_map','nnet_avx2','nnet_sse4_1')){
    $obj=Join-Path $objects ($file+'.o');$rnObjects+=$obj;$extra=@()
    if($file -eq 'nnet_avx2'){$extra=@('-mavx2','-mfma')}
    if($file -eq 'nnet_sse4_1'){$extra=@('-msse4.1')}
    & $cc @common @extra '-DDISABLE_DEBUG_FLOAT' '-DRNN_ENABLE_X86_RTCD' '-DCPU_INFO_BY_ASM' ('-I'+(Join-Path $rn 'src')) '-c' (Join-Path $rn ('src\x86\'+$file+'.c')) '-o' $obj
    if($LASTEXITCODE -ne 0){throw ('Could not compile '+$file)}
}
$cppFlags=@('-std=c++20','-Wall','-Wextra','-Wno-unknown-pragmas','-static')
$appResource=Join-Path $objects 'app.o'
Push-Location (Join-Path $root 'src')
try{& (Join-Path $compilerFolder.FullName 'bin\windres.exe') '-i' 'app.rc' '-o' $appResource '-O' 'coff';if($LASTEXITCODE -ne 0){throw 'Could not compile the application icon.'}}finally{Pop-Location}
& $cxx @common @cppFlags '-shared' (Join-Path $root 'src\apo.cpp') (Join-Path $root 'src\dsp.cpp') (Join-Path $root 'src\voice.cpp') (Join-Path $root 'src\rate_processor.cpp') (Join-Path $root 'src\apo.def') @rnObjects '-lole32' '-luuid' '-ladvapi32' '-o' (Join-Path $output 'MicFilterAPO.dll')
if($LASTEXITCODE -ne 0){throw 'Could not compile APO'}
& $cxx @common @cppFlags '-municode' '-mwindows' (Join-Path $root 'src\tray.cpp') $appResource '-lole32' '-luuid' '-lshell32' '-lpropsys' '-ladvapi32' '-lgdi32' '-luser32' '-o' (Join-Path $output 'MicFilter.exe')
if($LASTEXITCODE -ne 0){throw 'Could not compile the tray application'}
& $cxx @common @cppFlags (Join-Path $root 'tests\tests.cpp') (Join-Path $root 'src\dsp.cpp') (Join-Path $root 'src\voice.cpp') (Join-Path $root 'src\rate_processor.cpp') @rnObjects '-lole32' '-luuid' '-o' (Join-Path $output 'MicFilterTests.exe')
if($LASTEXITCODE -ne 0){throw 'Could not compile tests'}
foreach($scriptName in @('install.ps1','installer-registry.ps1')){
    $scriptText=Get-Content -LiteralPath (Join-Path $root $scriptName) -Raw -Encoding UTF8
    [IO.File]::WriteAllText((Join-Path $output $scriptName),$scriptText,[Text.UTF8Encoding]::new($true))
}
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $output 'LICENSE') -Force
Copy-Item -LiteralPath (Join-Path $rn 'COPYING') -Destination (Join-Path $output 'RNNOISE-LICENSE.txt') -Force
$resampleNotice=(Get-Content -LiteralPath (Join-Path $resampler 'resample.c') -TotalCount 29) -join [Environment]::NewLine
[IO.File]::WriteAllText((Join-Path $output 'SPEEX-LICENSE.txt'),$resampleNotice+[Environment]::NewLine+[Environment]::NewLine+(Get-Content -LiteralPath (Join-Path $resampler 'COPYING') -Raw),[Text.UTF8Encoding]::new($false))
Write-Host ('Build ready: '+$output)
if($RunTests){
    Push-Location $output;try{& '.\MicFilterTests.exe';if($LASTEXITCODE -ne 0){throw 'Tests failed'}}finally{Pop-Location}
    & (Join-Path $root 'tests\installer-tests.ps1') -DistributionDirectory $output
    & (Join-Path $root 'tests\multi-device-tests.ps1') -DistributionDirectory $output
}
